#include <iostream>
#include "../include/Board.h"
#include <cctype>
#include <vector>
#include <climits>
#include <random>
#include <algorithm>

using namespace std;

static const uint8_t WK_CASTLE = 1;
static const uint8_t WQ_CASTLE = 2;
static const uint8_t BK_CASTLE = 4;
static const uint8_t BQ_CASTLE = 8;

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

    castleRights = WK_CASTLE | WQ_CASTLE | BK_CASTLE | BQ_CASTLE;
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
    for (int i = 0; i < 4; i++)
        zobristCastle[i] = rng();
}

uint64_t Board::computeHash() const {
    uint64_t h = 0;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            if (board[r][c] != '.')
                h ^= zobristTable[pieceIndex(board[r][c])][r * 8 + c];
    for (int i = 0; i < 4; i++)
        if (castleRights & (1 << i))
            h ^= zobristCastle[i];
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

    char fromPiece = board[fromR][fromC];
    bool isCastling = (fromPiece == 'K' || fromPiece == 'k')
                   && fromR == toR && abs(toC - fromC) == 2;

    if (!isCastling) {
        if (!isValidMove(fromR, fromC, toR, toC)) {
            cout << "Invalid move!" << endl;
            return;
        }
        char toPiece = board[toR][toC];
        if (toPiece != '.' && ((isupper(fromPiece) && isupper(toPiece)) ||
                               (islower(fromPiece) && islower(toPiece)))) {
            cout << "Cannot capture your own piece!" << endl;
            return;
        }
    }

    Move m = {fromR, fromC, toR, toC};
    if ((fromPiece == 'P' && toR == 0) || (fromPiece == 'p' && toR == 7))
        m.promotion = isupper(fromPiece) ? 'Q' : 'q';
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
        r += stepR; c += stepC;
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
        r += stepR; c += stepC;
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

            for (int tr = 0; tr < 8; tr++) {
                for (int tc = 0; tc < 8; tc++) {
                    if (!isValidMove(r, c, tr, tc)) continue;

                    bool isPromotion = (piece == 'P' && tr == 0) ||
                                       (piece == 'p' && tr == 7);
                    if (isPromotion) {
                        const char* promos = isupper(piece) ? "QRBN" : "qrbn";
                        for (int i = 0; i < 4; i++) {
                            Move m = {r, c, tr, tc};
                            m.promotion = promos[i];
                            makeMove(m);
                            if (!isInCheck(whiteTurn))
                                moves.push_back(m);
                            undoMove(m);
                        }
                    } else {
                        Move m = {r, c, tr, tc};
                        makeMove(m);
                        if (!isInCheck(whiteTurn))
                            moves.push_back(m);
                        undoMove(m);
                    }
                }
            }
        }
    }

    // Castling: checked separately because king must not pass through check
    if (whiteTurn) {
        // Kingside: e1-f1-g1 must be empty and unattacked; rook on h1
        if ((castleRights & WK_CASTLE) &&
            board[7][5] == '.' && board[7][6] == '.' &&
            !isSquareAttacked(7, 4, false) &&
            !isSquareAttacked(7, 5, false)) {
            Move m = {7, 4, 7, 6};
            makeMove(m);
            if (!isInCheck(true)) moves.push_back(m);
            undoMove(m);
        }
        // Queenside: b1-c1-d1 empty; d1-c1 unattacked; rook on a1
        if ((castleRights & WQ_CASTLE) &&
            board[7][1] == '.' && board[7][2] == '.' && board[7][3] == '.' &&
            !isSquareAttacked(7, 4, false) &&
            !isSquareAttacked(7, 3, false)) {
            Move m = {7, 4, 7, 2};
            makeMove(m);
            if (!isInCheck(true)) moves.push_back(m);
            undoMove(m);
        }
    } else {
        // Kingside: e8-f8-g8
        if ((castleRights & BK_CASTLE) &&
            board[0][5] == '.' && board[0][6] == '.' &&
            !isSquareAttacked(0, 4, true) &&
            !isSquareAttacked(0, 5, true)) {
            Move m = {0, 4, 0, 6};
            makeMove(m);
            if (!isInCheck(false)) moves.push_back(m);
            undoMove(m);
        }
        // Queenside: b8-c8-d8
        if ((castleRights & BQ_CASTLE) &&
            board[0][1] == '.' && board[0][2] == '.' && board[0][3] == '.' &&
            !isSquareAttacked(0, 4, true) &&
            !isSquareAttacked(0, 3, true)) {
            Move m = {0, 4, 0, 2};
            makeMove(m);
            if (!isInCheck(false)) moves.push_back(m);
            undoMove(m);
        }
    }

    return moves;
}

bool Board::isSquareAttacked(int r, int c, bool byWhite) const {
    if (byWhite) {
        if (r + 1 < 8) {
            if (c - 1 >= 0 && board[r+1][c-1] == 'P') return true;
            if (c + 1 < 8 && board[r+1][c+1] == 'P') return true;
        }
    } else {
        if (r - 1 >= 0) {
            if (c - 1 >= 0 && board[r-1][c-1] == 'p') return true;
            if (c + 1 < 8 && board[r-1][c+1] == 'p') return true;
        }
    }

    char knight = byWhite ? 'N' : 'n';
    static const int knightDirs[8][2] = {
        {-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}
    };
    for (auto& d : knightDirs) {
        int nr = r + d[0], nc = c + d[1];
        if (nr >= 0 && nr < 8 && nc >= 0 && nc < 8 && board[nr][nc] == knight)
            return true;
    }

    char bishop = byWhite ? 'B' : 'b';
    char queen  = byWhite ? 'Q' : 'q';
    static const int diagDirs[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
    for (auto& d : diagDirs) {
        int nr = r + d[0], nc = c + d[1];
        while (nr >= 0 && nr < 8 && nc >= 0 && nc < 8) {
            if (board[nr][nc] != '.') {
                if (board[nr][nc] == bishop || board[nr][nc] == queen) return true;
                break;
            }
            nr += d[0]; nc += d[1];
        }
    }

    char rook = byWhite ? 'R' : 'r';
    static const int orthDirs[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
    for (auto& d : orthDirs) {
        int nr = r + d[0], nc = c + d[1];
        while (nr >= 0 && nr < 8 && nc >= 0 && nc < 8) {
            if (board[nr][nc] != '.') {
                if (board[nr][nc] == rook || board[nr][nc] == queen) return true;
                break;
            }
            nr += d[0]; nc += d[1];
        }
    }

    char king = byWhite ? 'K' : 'k';
    static const int kingDirs[8][2] = {
        {-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1}
    };
    for (auto& d : kingDirs) {
        int nr = r + d[0], nc = c + d[1];
        if (nr >= 0 && nr < 8 && nc >= 0 && nc < 8 && board[nr][nc] == king)
            return true;
    }

    return false;
}

bool Board::isInCheck(bool white) const {
    char king = white ? 'K' : 'k';
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            if (board[r][c] == king)
                return isSquareAttacked(r, c, !white);
    return false;
}

void Board::makeMove(Move& move) {
    char moving = board[move.fromR][move.fromC];
    move.captured = board[move.toR][move.toC];
    move.prevCastleRights = castleRights;

    // XOR out old castling rights contribution
    for (int i = 0; i < 4; i++)
        if (castleRights & (1 << i))
            hash ^= zobristCastle[i];

    // Revoke rights when king or rook moves
    if (moving == 'K') castleRights &= ~(WK_CASTLE | WQ_CASTLE);
    if (moving == 'k') castleRights &= ~(BK_CASTLE | BQ_CASTLE);
    if (moving == 'R') {
        if (move.fromR == 7 && move.fromC == 7) castleRights &= ~WK_CASTLE;
        if (move.fromR == 7 && move.fromC == 0) castleRights &= ~WQ_CASTLE;
    }
    if (moving == 'r') {
        if (move.fromR == 0 && move.fromC == 7) castleRights &= ~BK_CASTLE;
        if (move.fromR == 0 && move.fromC == 0) castleRights &= ~BQ_CASTLE;
    }
    // Revoke rights when a rook is captured on its home square
    if (move.captured == 'R') {
        if (move.toR == 7 && move.toC == 7) castleRights &= ~WK_CASTLE;
        if (move.toR == 7 && move.toC == 0) castleRights &= ~WQ_CASTLE;
    }
    if (move.captured == 'r') {
        if (move.toR == 0 && move.toC == 7) castleRights &= ~BK_CASTLE;
        if (move.toR == 0 && move.toC == 0) castleRights &= ~BQ_CASTLE;
    }

    // XOR in new castling rights contribution
    for (int i = 0; i < 4; i++)
        if (castleRights & (1 << i))
            hash ^= zobristCastle[i];

    // Update piece hash
    hash ^= zobristTable[pieceIndex(moving)][move.fromR * 8 + move.fromC];
    if (move.captured != '.')
        hash ^= zobristTable[pieceIndex(move.captured)][move.toR * 8 + move.toC];
    hash ^= zobristTable[pieceIndex(moving)][move.toR * 8 + move.toC];
    hash ^= zobristSideToMove;

    board[move.toR][move.toC]    = moving;
    board[move.fromR][move.fromC] = '.';

    // Castling: also move the rook
    if (moving == 'K' && move.fromC == 4 && abs(move.toC - 4) == 2) {
        if (move.toC == 6) { // kingside
            hash ^= zobristTable[pieceIndex('R')][7*8+7];
            hash ^= zobristTable[pieceIndex('R')][7*8+5];
            board[7][5] = 'R'; board[7][7] = '.';
        } else {             // queenside
            hash ^= zobristTable[pieceIndex('R')][7*8+0];
            hash ^= zobristTable[pieceIndex('R')][7*8+3];
            board[7][3] = 'R'; board[7][0] = '.';
        }
    }
    if (moving == 'k' && move.fromC == 4 && abs(move.toC - 4) == 2) {
        if (move.toC == 6) { // kingside
            hash ^= zobristTable[pieceIndex('r')][0*8+7];
            hash ^= zobristTable[pieceIndex('r')][0*8+5];
            board[0][5] = 'r'; board[0][7] = '.';
        } else {             // queenside
            hash ^= zobristTable[pieceIndex('r')][0*8+0];
            hash ^= zobristTable[pieceIndex('r')][0*8+3];
            board[0][3] = 'r'; board[0][0] = '.';
        }
    }

    // Promotion: swap pawn on destination for the promoted piece
    if (move.promotion != '.') {
        hash ^= zobristTable[pieceIndex(moving)][move.toR * 8 + move.toC];
        hash ^= zobristTable[pieceIndex(move.promotion)][move.toR * 8 + move.toC];
        board[move.toR][move.toC] = move.promotion;
    }
}

void Board::undoMove(const Move& move) {
    char moving = board[move.toR][move.toC];
    // If this was a promotion, the piece to put back at fromR,fromC is the pawn
    char originalPiece = (move.promotion != '.')
                       ? (isupper(move.promotion) ? 'P' : 'p')
                       : moving;

    // Undo rook movement for castling (before restoring king position)
    if (moving == 'K' && move.fromC == 4 && abs(move.toC - 4) == 2) {
        if (move.toC == 6) {
            hash ^= zobristTable[pieceIndex('R')][7*8+5];
            hash ^= zobristTable[pieceIndex('R')][7*8+7];
            board[7][7] = 'R'; board[7][5] = '.';
        } else {
            hash ^= zobristTable[pieceIndex('R')][7*8+3];
            hash ^= zobristTable[pieceIndex('R')][7*8+0];
            board[7][0] = 'R'; board[7][3] = '.';
        }
    }
    if (moving == 'k' && move.fromC == 4 && abs(move.toC - 4) == 2) {
        if (move.toC == 6) {
            hash ^= zobristTable[pieceIndex('r')][0*8+5];
            hash ^= zobristTable[pieceIndex('r')][0*8+7];
            board[0][7] = 'r'; board[0][5] = '.';
        } else {
            hash ^= zobristTable[pieceIndex('r')][0*8+3];
            hash ^= zobristTable[pieceIndex('r')][0*8+0];
            board[0][0] = 'r'; board[0][3] = '.';
        }
    }

    // Undo piece hash (use originalPiece so promotion is correctly reversed)
    hash ^= zobristTable[pieceIndex(moving)][move.toR * 8 + move.toC];
    hash ^= zobristTable[pieceIndex(originalPiece)][move.fromR * 8 + move.fromC];
    if (move.captured != '.')
        hash ^= zobristTable[pieceIndex(move.captured)][move.toR * 8 + move.toC];
    hash ^= zobristSideToMove;

    board[move.fromR][move.fromC] = originalPiece;
    board[move.toR][move.toC]     = move.captured;

    // Restore castling rights
    for (int i = 0; i < 4; i++)
        if (castleRights & (1 << i))
            hash ^= zobristCastle[i];
    castleRights = move.prevCastleRights;
    for (int i = 0; i < 4; i++)
        if (castleRights & (1 << i))
            hash ^= zobristCastle[i];
}

int Board::quiescence(int alpha, int beta, bool maximizingPlayer) {
    int standPat = evaluate();

    if (maximizingPlayer) {
        if (standPat >= beta) return standPat;
        alpha = max(alpha, standPat);
    } else {
        if (standPat <= alpha) return standPat;
        beta = min(beta, standPat);
    }

    vector<Move> moves = generateAllMoves(maximizingPlayer);
    for (Move& m : moves) {
        if (m.captured == '.') continue;
        makeMove(m);
        int score = quiescence(alpha, beta, !maximizingPlayer);
        undoMove(m);
        if (maximizingPlayer) {
            if (score >= beta) return score;
            alpha = max(alpha, score);
        } else {
            if (score <= alpha) return score;
            beta = min(beta, score);
        }
    }

    return maximizingPlayer ? alpha : beta;
}

int Board::minimax(int depth, bool maximizingPlayer, int alpha, int beta) {
    TTEntry& entry = transpositionTable[hash % TT_SIZE];
    if (entry.key == hash && entry.depth >= depth) {
        if (entry.flag == 0) return entry.score;
        if (entry.flag == 1) alpha = max(alpha, entry.score);
        if (entry.flag == 2) beta  = min(beta,  entry.score);
        if (alpha >= beta) return entry.score;
    }

    if (depth == 0)
        return quiescence(alpha, beta, maximizingPlayer);

    vector<Move> moves = generateAllMoves(maximizingPlayer);
    if (moves.empty())
        return evaluate();

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
            if (alpha >= beta) break;
        }
    } else {
        best = INT_MAX;
        for (Move& m : moves) {
            makeMove(m);
            best = min(best, minimax(depth - 1, true, alpha, beta));
            undoMove(m);
            beta = min(beta, best);
            if (alpha >= beta) break;
        }
    }

    TTEntry& slot = transpositionTable[hash % TT_SIZE];
    slot.key   = hash;
    slot.depth = depth;
    slot.score = best;
    if      (best <= originalAlpha) slot.flag = 2;
    else if (best >= beta)          slot.flag = 1;
    else                            slot.flag = 0;

    return best;
}

Move Board::getBestMove(int depth, bool whiteTurn) {
    vector<Move> moves = generateAllMoves(whiteTurn);
    if (moves.empty()) return Move{};
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

int Board::kingSafety(bool white) const {
    char king = white ? 'K' : 'k';
    char pawn = white ? 'P' : 'p';
    int kingR = -1, kingC = -1;

    for (int r = 0; r < 8 && kingR == -1; r++)
        for (int c = 0; c < 8 && kingR == -1; c++)
            if (board[r][c] == king) { kingR = r; kingC = c; }

    if (kingR == -1) return 0;

    int score = 0;

    int shieldRow = white ? kingR - 1 : kingR + 1;
    if (shieldRow >= 0 && shieldRow < 8) {
        for (int dc = -1; dc <= 1; dc++) {
            int fc = kingC + dc;
            if (fc >= 0 && fc < 8 && board[shieldRow][fc] == pawn)
                score += 10;
        }
    }

    for (int dc = -1; dc <= 1; dc++) {
        int fc = kingC + dc;
        if (fc < 0 || fc >= 8) continue;
        bool hasPawn = false;
        for (int r = 0; r < 8; r++)
            if (board[r][fc] == pawn) { hasPawn = true; break; }
        if (!hasPawn) score -= 20;
    }

    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            int zr = kingR + dr, zc = kingC + dc;
            if (zr < 0 || zr >= 8 || zc < 0 || zc >= 8) continue;
            if (isSquareAttacked(zr, zc, !white))
                score -= 8;
        }
    }

    return score;
}

int Board::countMobility(int r, int c) const {
    char piece = board[r][c];
    bool white = isupper(piece);
    char lower = tolower(piece);
    int count = 0;

    auto canLand = [&](int nr, int nc) {
        char t = board[nr][nc];
        return t == '.' || (white ? islower(t) : isupper(t));
    };

    if (lower == 'p') {
        int dir = white ? -1 : 1;
        int nr = r + dir;
        if (nr >= 0 && nr < 8) {
            if (board[nr][c] == '.') count++;
            if (c > 0 && board[nr][c-1] != '.' && canLand(nr, c-1)) count++;
            if (c < 7 && board[nr][c+1] != '.' && canLand(nr, c+1)) count++;
        }
    } else if (lower == 'n') {
        static const int nd[8][2] = {{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
        for (auto& d : nd) {
            int nr = r+d[0], nc = c+d[1];
            if (nr>=0&&nr<8&&nc>=0&&nc<8&&canLand(nr,nc)) count++;
        }
    } else if (lower == 'b' || lower == 'q') {
        static const int dd[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
        for (auto& d : dd) {
            int nr = r+d[0], nc = c+d[1];
            while (nr>=0&&nr<8&&nc>=0&&nc<8) {
                if (board[nr][nc] == '.') { count++; nr+=d[0]; nc+=d[1]; }
                else { if (canLand(nr,nc)) count++; break; }
            }
        }
    }
    if (lower == 'r' || lower == 'q') {
        static const int od[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
        for (auto& d : od) {
            int nr = r+d[0], nc = c+d[1];
            while (nr>=0&&nr<8&&nc>=0&&nc<8) {
                if (board[nr][nc] == '.') { count++; nr+=d[0]; nc+=d[1]; }
                else { if (canLand(nr,nc)) count++; break; }
            }
        }
    }
    // King mobility intentionally excluded to avoid incentivising early king movement

    return count;
}

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

            int mobility = (tolower(piece) == 'k') ? 0 : countMobility(r, c);
            if (white) score += material + pst + mobility * 3;
            else       score -= material + pst + mobility * 3;
        }
    }

    score += kingSafety(true);
    score -= kingSafety(false);

    // Pawn structure penalties
    int wPawns[8] = {}, bPawns[8] = {};
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            if      (board[r][c] == 'P') wPawns[c]++;
            else if (board[r][c] == 'p') bPawns[c]++;

    for (int c = 0; c < 8; c++) {
        // Doubled: each extra pawn on the same file
        if (wPawns[c] > 1) score -= 20 * (wPawns[c] - 1);
        if (bPawns[c] > 1) score += 20 * (bPawns[c] - 1);

        // Isolated: no friendly pawns on either adjacent file
        if (wPawns[c] > 0) {
            bool iso = (c == 0 || wPawns[c-1] == 0) && (c == 7 || wPawns[c+1] == 0);
            if (iso) score -= 15 * wPawns[c];
        }
        if (bPawns[c] > 0) {
            bool iso = (c == 0 || bPawns[c-1] == 0) && (c == 7 || bPawns[c+1] == 0);
            if (iso) score += 15 * bPawns[c];
        }
    }

    // Reward retaining castling options
    const int CASTLE_BONUS = 15;
    if (castleRights & WK_CASTLE) score += CASTLE_BONUS;
    if (castleRights & WQ_CASTLE) score += CASTLE_BONUS;
    if (castleRights & BK_CASTLE) score -= CASTLE_BONUS;
    if (castleRights & BQ_CASTLE) score -= CASTLE_BONUS;

    return score;
}
