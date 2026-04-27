#ifndef BOARD_H
#define BOARD_H
#include <vector>
#include <cstdint>
#include <climits>
#include "Move.h"

class Board {
private:
    char board[8][8];

    uint64_t zobristTable[12][64];
    uint64_t zobristSideToMove;
    uint64_t hash;

    static const int TT_SIZE = 1 << 20; // ~1M slots

    struct TTEntry {
        uint64_t key;
        int depth;
        int score;
        int flag; // 0=EXACT, 1=LOWER, 2=UPPER
    };
    std::vector<TTEntry> transpositionTable;

    void initZobrist();
    uint64_t computeHash() const;
    int pieceIndex(char piece) const;
    int quiescence(int alpha, int beta, bool maximizingPlayer);
    int kingSafety(bool white) const;

public:
    Board();
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
    int minimax(int depth, bool maximizingPlayer, int alpha, int beta);
    Move getBestMove(int depth, bool whiteTurn);

    std::vector<Move> generateAllMoves(bool whiteTurn);
};

#endif
