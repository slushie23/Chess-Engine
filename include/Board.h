#ifndef BOARD_H
#define BOARD_H
#include <vector>
#include <string>
#include <cstdint>
#include <climits>
#include <atomic>
#include <chrono>
#include "Move.h"

class Board {
private:
    char board[8][8];

    uint64_t zobristTable[12][64];
    uint64_t zobristSideToMove;
    uint64_t zobristCastle[4]; // WK, WQ, BK, BQ
    uint64_t zobristEP[8];     // one per file
    uint64_t hash;
    uint8_t  castleRights;    // bits: 1=WK 2=WQ 4=BK 8=BQ
    int      epFile;          // file (0-7) of en-passant target, -1 if none
    int      kingR[2], kingC[2]; // king squares, [0]=white [1]=black

    static constexpr int TT_SIZE = 1 << 20; // ~1M slots

    struct TTEntry {
        uint64_t key  = 0;
        int depth     = 0;
        int score     = 0;
        int flag      = 0;    // 0=EXACT, 1=LOWER, 2=UPPER
        uint8_t ttFrom  = 255; // fromR*8+fromC, 255=none
        uint8_t ttTo    = 255; // toR*8+toC
        char    ttPromo = '.';
    };
    std::vector<TTEntry> transpositionTable;
    static constexpr int MAX_PLY = 64; // hard cap on search depth / ply

    Move killers[MAX_PLY][2]; // two killer slots per ply (distance from root)
    int  history[64][64];  // history[fromSq][toSq] — quiet-move cutoff frequency
    std::vector<uint64_t> hashHistory; // position hashes for repetition detection
    int halfMoveClock = 0;

    // Search control
    std::chrono::steady_clock::time_point searchStart;
    long long searchLimitMs = 0; // 0 = no time limit
    uint64_t  nodes = 0;
    bool shouldStop();

    void initZobrist();
    uint64_t computeHash() const;
    int pieceIndex(char piece) const;
    int quiescence(int alpha, int beta, bool maximizingPlayer);
    int kingSafety(bool white, uint64_t enemyAttacks, const int pawnsPerFile[8]) const;
    int countMobility(int r, int c, uint64_t& attacks) const;

public:
    bool whiteTurn;
    std::atomic<bool> stopRequested{false}; // set by UCI "stop" or when time runs out

    Board();
    void resetToStart();
    void setFromFen(const std::string& fen);
    Move parseUciMove(const std::string& uci);
    std::string moveToUci(const Move& m) const;
    Move getBestMoveTime(int maxDepth, int timeLimitMs);

    void printBoard();
    void movePiece(int fromR, int fromC, int toR, int toC);
    bool isValidMove(int fromR, int fromC, int toR, int toC);

    bool isValidPawnMove(int fromR, int fromC, int toR, int toC);
    bool isValidKnightMove(int fromR, int fromC, int toR, int toC);
    bool isValidRookMove(int fromR, int fromC, int toR, int toC);
    bool isValidBishopMove(int fromR, int fromC, int toR, int toC);
    bool isValidQueenMove(int fromR, int fromC, int toR, int toC);
    bool isValidKingMove(int fromR, int fromC, int toR, int toC);

    int evaluate();
    bool isSquareAttacked(int r, int c, bool byWhite) const;
    bool isInCheck(bool white) const;

    void makeMove(Move& move);
    void undoMove(const Move& move);
    int minimax(int depth, int ply, bool maximizingPlayer, int alpha, int beta, bool nullMoveAllowed = true);
    Move getBestMove(int depth, bool whiteTurn);

    // Legal moves for the side; capturesOnly limits it to moves onto enemy pieces (for quiescence)
    std::vector<Move> generateAllMoves(bool whiteTurn, bool capturesOnly = false);
};

#endif
