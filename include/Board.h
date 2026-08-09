#ifndef BOARD_H
#define BOARD_H
#include <vector>
#include <string>
#include <cstdint>
#include <climits>
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

    static const int TT_SIZE = 1 << 20; // ~1M slots

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
    Move killers[64][2];   // two killer slots per remaining-depth level
    int  history[64][64];  // history[fromSq][toSq] — quiet-move cutoff frequency
    std::vector<uint64_t> hashHistory; // position hashes for repetition detection
    int halfMoveClock = 0;

    void initZobrist();
    uint64_t computeHash() const;
    int pieceIndex(char piece) const;
    int quiescence(int alpha, int beta, bool maximizingPlayer);
    int kingSafety(bool white) const;
    int countMobility(int r, int c) const;

public:
    bool whiteTurn;

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
    int minimax(int depth, bool maximizingPlayer, int alpha, int beta, bool nullMoveAllowed = true);
    Move getBestMove(int depth, bool whiteTurn);

    std::vector<Move> generateAllMoves(bool whiteTurn);
};

#endif
