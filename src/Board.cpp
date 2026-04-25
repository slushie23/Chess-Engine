#include <iostream>
#include "../include/Board.h"
#include <cctype>
#include <vector>
#include <climits>
#include <random>
#include <algorithm>

using namespace std;

Board::Board() {
    char initial[8][8] = {
        {'r','n','b','q','k','b','n','r'},
        {'p','p','p','p','p','p','p','p'},
        {'.','.','.','.','.','.','.','.'},
        {'.','.','.','.','.','.','.','.'},
        {'.','.','.','.','.','.','.','.'},
        {'.','.','.','.','.','.','.','.'},
        {'P','P','P','P','P','P','P','P'},
        {'R','N','B','Q','K','B','N','R'}
    };

    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
            board[i][j] = initial[i][j];

    initZobrist();
    hash = computeHash();
    transpositionTable.resize(TT_SIZE);
}

int Board::pieceIndex(char piece) const {
    switch (piece) {
        case 'P': return 0; case 'N': return 1; case 'B': return 2;
        case 'R': return 3; case 'Q': return 4; case 'K': return 5;
        case 'p': return 6; case 'n': return 7; case 'b': return 8;
        case 'r': return 9; case 'q': return 10; case 'k': return 11;
        default:  return -1;
    }
}

void Board::initZobrist() {
    mt19937_64 rng(0xDEADBEEFCAFEBABEULL);
    for (int p = 0; p < 12; p++)
        for (int sq = 0; sq < 64; sq++)
            zobristTable[p][sq] = rng();
    zobristSideToMove = rng();
}

uint64_t Board::computeHash() const {
    uint64_t h = 0;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            if (board[r][c] != '.')
                h ^= zobristTable[pieceIndex(board[r][c])][r * 8 + c];
    return h;
}

void Board::printBoard() {
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++)
            cout << board[i][j] << " ";
        cout << endl;
    }
}

void Board::movePiece(int fromR, int fromC, int toR, int toC) {
    if (board[fromR][fromC] == '.') {
        cout << "No piece at the source position!" << endl;
        return;
    }
    if (toR < 0 || toR >= 8 || toC < 0 || toC >= 8) {
        cout << "Destination position is out of bounds!" << endl;
        return;
    }
    if (!isValidMove(fromR, fromC, toR, toC)) {
        cout << "Invalid move!" << endl;
        return;
    }

    char fromPiece = board[fromR][fromC];
    char toPiece   = board[toR][toC];
    if (toPiece != '.') {
        if ((isupper(fromPiece) && isupper(toPiece)) ||
            (islower(fromPiece) && islower(toPiece))) {
            cout << "Cannot capture your own piece!" << endl;
            return;
        }
    }

    Move m = {fromR, fromC, toR, toC};
    makeMove(m);
}

bool Board::isValidMove(int fromR, int fromC, int toR, int toC) {
    if (toR < 0 || toR >= 8 || toC < 0 || toC >= 8)
        return false;

    char piece = board[fromR][fromC];
    if (piece == '.') return false;

    char target = board[toR][toC];
    if (target != '.') {
        if ((isupper(piece) && isupper(target)) ||
            (islower(piece) && islower(target)))
            return false;
    }

    switch (tolower(piece)) {
        case 'p': return isValidPawnMove(fromR, fromC, toR, toC);
        case 'n': return isValidKnightMove(fromR, fromC, toR, toC);
        case 'r': return isValidRookMove(fromR, fromC, toR, toC);
        case 'b': return isValidBishopMove(fromR, fromC, toR, toC);
        case 'q': return isValidQueenMove(fromR, fromC, toR, toC);
        case 'k': return isValidKingMove(fromR, fromC, toR, toC);
    }
    return false;
}

bool Board::isValidPawnMove(int fromR, int fromC, int toR, int toC) {
    char piece = board[fromR][fromC];
    int dRow = toR - fromR;
    int dCol = toC - fromC;

    if (piece == 'P') {
        if (dCol == 0 && dRow == -1 && board[toR][toC] == '.') return true;
        if (dCol == 0 && dRow == -2 && fromR == 6 &&
            board[toR][toC] == '.' && board[fromR-1][fromC] == '.') return true;
        if (abs(dCol) == 1 && dRow == -1 && islower(board[toR][toC])) return true;
    }
    if (piece == 'p') {
        if (dCol == 0 && dRow == 1 && board[toR][toC] == '.') return true;
        if (dCol == 0 && dRow == 2 && fromR == 1 &&
            board[toR][toC] == '.' && board[fromR+1][fromC] == '.') return true;
        if (abs(dCol) == 1 && dRow == 1 && isupper(board[toR][toC])) return true;
    }
    return false;
}

bool Board::isValidKnightMove(int fromR, int fromC, int toR, int toC) {
    int dRow = abs(toR - fromR);
    int dCol = abs(toC - fromC);
    return (dRow == 2 && dCol == 1) || (dRow == 1 && dCol == 2);
}

bool Board::isValidRookMove(int fromR, int fromC, int toR, int toC) {
    if (fromR != toR && fromC != toC) return false;

    int stepR = (toR == fromR) ? 0 : (toR > fromR ? 1 : -1);
    int stepC = (toC == fromC) ? 0 : (toC > fromC ? 1 : -1);

    int r = fromR + stepR, c = fromC + stepC;
    while (r != toR || c != toC) {
        if (board[r][c] != '.') return false;
        r += stepR;
        c += stepC;
    }
    return true;
}

bool Board::isValidBishopMove(int fromR, int fromC, int toR, int toC) {
    if (abs(toR - fromR) != abs(toC - fromC)) return false;

    int stepR = (toR > fromR) ? 1 : -1;
    int stepC = (toC > fromC) ? 1 : -1;

    int r = fromR + stepR, c = fromC + stepC;
    while (r != toR && c != toC) {
        if (board[r][c] != '.') return false;
        r += stepR;
        c += stepC;
    }
    return true;
}

bool Board::isValidQueenMove(int fromR, int fromC, int toR, int toC) {
    return isValidRookMove(fromR, fromC, toR, toC) ||
           isValidBishopMove(fromR, fromC, toR, toC);
}

bool Board::isValidKingMove(int fromR, int fromC, int toR, int toC) {
    int dRow = abs(toR - fromR);
    int dCol = abs(toC - fromC);
    return dRow <= 1 && dCol <= 1 && !(dRow == 0 && dCol == 0);
}

std::vector<Move> Board::generateAllMoves(bool whiteTurn) {
    std::vector<Move> moves;
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            char piece = board[r][c];
            if (piece == '.') continue;
            if (whiteTurn  && islower(piece)) continue;
            if (!whiteTurn && isupper(piece)) continue;

            for (int tr = 0; tr < 8; tr++)
                for (int tc = 0; tc < 8; tc++)
                    if (isValidMove(r, c, tr, tc))
                        moves.push_back({r, c, tr, tc, board[tr][tc]});
        }
    }
    return moves;
}

void Board::makeMove(Move& move) {
    char moving = board[move.fromR][move.fromC];
    move.captured = board[move.toR][move.toC];

    hash ^= zobristTable[pieceIndex(moving)][move.fromR * 8 + move.fromC];
    if (move.captured != '.')
        hash ^= zobristTable[pieceIndex(move.captured)][move.toR * 8 + move.toC];
    hash ^= zobristTable[pieceIndex(moving)][move.toR * 8 + move.toC];
    hash ^= zobristSideToMove;

    board[move.toR][move.toC]   = moving;
    board[move.fromR][move.fromC] = '.';
}

void Board::undoMove(const Move& move) {
    char moving = board[move.toR][move.toC];

    hash ^= zobristTable[pieceIndex(moving)][move.toR * 8 + move.toC];
    hash ^= zobristTable[pieceIndex(moving)][move.fromR * 8 + move.fromC];
    if (move.captured != '.')
        hash ^= zobristTable[pieceIndex(move.captured)][move.toR * 8 + move.toC];
    hash ^= zobristSideToMove;

    board[move.fromR][move.fromC] = moving;
    board[move.toR][move.toC]     = move.captured;
}

int Board::minimax(int depth, bool maximizingPlayer, int alpha, int beta) {
    // Transposition table lookup
    TTEntry& entry = transpositionTable[hash % TT_SIZE];
    if (entry.key == hash && entry.depth >= depth) {
        if (entry.flag == 0) return entry.score;               // exact
        if (entry.flag == 1) alpha = max(alpha, entry.score);  // lower bound
        if (entry.flag == 2) beta  = min(beta,  entry.score);  // upper bound
        if (alpha >= beta) return entry.score;
    }

    if (depth == 0)
        return evaluate();

    vector<Move> moves = generateAllMoves(maximizingPlayer);
    if (moves.empty())
        return evaluate();

    // Move ordering: captures first (improves alpha-beta cutoffs)
    sort(moves.begin(), moves.end(), [](const Move& a, const Move& b) {
        return (a.captured != '.') > (b.captured != '.');
    });

    int originalAlpha = alpha;
    int best;

    if (maximizingPlayer) {
        best = INT_MIN;
        for (Move& m : moves) {
            makeMove(m);
            best = max(best, minimax(depth - 1, false, alpha, beta));
            undoMove(m);
            alpha = max(alpha, best);
            if (alpha >= beta) break; // beta cutoff
        }
    } else {
        best = INT_MAX;
        for (Move& m : moves) {
            makeMove(m);
            best = min(best, minimax(depth - 1, true, alpha, beta));
            undoMove(m);
            beta = min(beta, best);
            if (alpha >= beta) break; // alpha cutoff
        }
    }

    // Store in transposition table
    TTEntry& slot = transpositionTable[hash % TT_SIZE];
    slot.key   = hash;
    slot.depth = depth;
    slot.score = best;
    if      (best <= originalAlpha) slot.flag = 2; // upper bound (fail-low)
    else if (best >= beta)          slot.flag = 1; // lower bound (fail-high)
    else                            slot.flag = 0; // exact

    return best;
}

Move Board::getBestMove(int depth, bool whiteTurn) {
    vector<Move> moves = generateAllMoves(whiteTurn);
    Move bestMove = moves[0];

    if (whiteTurn) {
        int bestEval = INT_MIN;
        for (Move& m : moves) {
            makeMove(m);
            int eval = minimax(depth - 1, false, INT_MIN, INT_MAX);
            undoMove(m);
            if (eval > bestEval) { bestEval = eval; bestMove = m; }
        }
    } else {
        int bestEval = INT_MAX;
        for (Move& m : moves) {
            makeMove(m);
            int eval = minimax(depth - 1, true, INT_MIN, INT_MAX);
            undoMove(m);
            if (eval < bestEval) { bestEval = eval; bestMove = m; }
        }
    }

    return bestMove;
}

// Piece-square tables (PST[0] = own back rank, PST[7] = opponent's back rank).
// White: pstRow = 7 - boardRow.  Black: pstRow = boardRow.
static const int pawnPST[8][8] = {
    {  0,  0,  0,  0,  0,  0,  0,  0},
    {  5, 10, 10,-20,-20, 10, 10,  5},
    {  5, -5,-10,  0,  0,-10, -5,  5},
    {  0,  0,  0, 20, 20,  0,  0,  0},
    {  5,  5, 10, 25, 25, 10,  5,  5},
    { 10, 10, 20, 30, 30, 20, 10, 10},
    { 50, 50, 50, 50, 50, 50, 50, 50},
    {  0,  0,  0,  0,  0,  0,  0,  0},
};
static const int knightPST[8][8] = {
    {-50,-40,-30,-30,-30,-30,-40,-50},
    {-40,-20,  0,  5,  5,  0,-20,-40},
    {-30,  5, 10, 15, 15, 10,  5,-30},
    {-30,  0, 15, 20, 20, 15,  0,-30},
    {-30,  5, 15, 20, 20, 15,  5,-30},
    {-30,  0, 10, 15, 15, 10,  0,-30},
    {-40,-20,  0,  0,  0,  0,-20,-40},
    {-50,-40,-30,-30,-30,-30,-40,-50},
};
static const int bishopPST[8][8] = {
    {-20,-10,-10,-10,-10,-10,-10,-20},
    {-10,  5,  0,  0,  0,  0,  5,-10},
    {-10, 10, 10, 10, 10, 10, 10,-10},
    {-10,  0, 10, 10, 10, 10,  0,-10},
    {-10,  5,  5, 10, 10,  5,  5,-10},
    {-10,  0,  5, 10, 10,  5,  0,-10},
    {-10,  0,  0,  0,  0,  0,  0,-10},
    {-20,-10,-10,-10,-10,-10,-10,-20},
};
static const int rookPST[8][8] = {
    {  0,  0,  0,  5,  5,  0,  0,  0},
    { -5,  0,  0,  0,  0,  0,  0, -5},
    { -5,  0,  0,  0,  0,  0,  0, -5},
    { -5,  0,  0,  0,  0,  0,  0, -5},
    { -5,  0,  0,  0,  0,  0,  0, -5},
    { -5,  0,  0,  0,  0,  0,  0, -5},
    {  5, 10, 10, 10, 10, 10, 10,  5},
    {  0,  0,  0,  0,  0,  0,  0,  0},
};
static const int queenPST[8][8] = {
    {-20,-10,-10, -5, -5,-10,-10,-20},
    {-10,  0,  5,  0,  0,  0,  0,-10},
    {-10,  5,  5,  5,  5,  5,  0,-10},
    {  0,  0,  5,  5,  5,  5,  0, -5},
    { -5,  0,  5,  5,  5,  5,  0, -5},
    {-10,  0,  5,  5,  5,  5,  0,-10},
    {-10,  0,  0,  0,  0,  0,  0,-10},
    {-20,-10,-10, -5, -5,-10,-10,-20},
};
static const int kingPST[8][8] = {
    { 20, 30, 10,  0,  0, 10, 30, 20},
    { 20, 20,  0,  0,  0,  0, 20, 20},
    {-10,-20,-20,-20,-20,-20,-20,-10},
    {-20,-30,-30,-40,-40,-30,-30,-20},
    {-30,-40,-40,-50,-50,-40,-40,-30},
    {-30,-40,-40,-50,-50,-40,-40,-30},
    {-30,-40,-40,-50,-50,-40,-40,-30},
    {-30,-40,-40,-50,-50,-40,-40,-30},
};

int Board::evaluate() {
    int score = 0;
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            char piece = board[r][c];
            if (piece == '.') continue;

            bool white = isupper(piece);
            int pstRow = white ? (7 - r) : r;

            int material = 0, pst = 0;
            switch (tolower(piece)) {
                case 'p': material = 100; pst = pawnPST[pstRow][c];   break;
                case 'n': material = 320; pst = knightPST[pstRow][c]; break;
                case 'b': material = 330; pst = bishopPST[pstRow][c]; break;
                case 'r': material = 500; pst = rookPST[pstRow][c];   break;
                case 'q': material = 900; pst = queenPST[pstRow][c];  break;
                case 'k': material =   0; pst = kingPST[pstRow][c];   break;
            }

            if (white) score += material + pst;
            else       score -= material + pst;
        }
    }
    return score;
}
