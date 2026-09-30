#include <iostream>
#include "../include/Board.h"
#include <cctype>
#include <vector>
#include <climits>
#include <random>
#include <algorithm>
#include <chrono>
#include <sstream>

using namespace std;

// Inline ASCII piece tests; the <cctype> versions are out-of-line library
// calls on some toolchains, and these run for every square in eval and movegen.
static inline bool isWhitePiece(char p) { return p >= 'A' && p <= 'Z'; }
static inline bool isBlackPiece(char p) { return p >= 'a' && p <= 'z'; }
static inline char pieceType(char p)    { return (char)(p | 0x20); } // lowercase letter; '.' unchanged

static const uint8_t WK_CASTLE = 1;
static const uint8_t WQ_CASTLE = 2;
static const uint8_t BK_CASTLE = 4;
static const uint8_t BQ_CASTLE = 8;

// MVV-LVA piece values indexed by pieceIndex() (P=0..K=5, p=6..k=11)
static const int MVV_VAL[12] = {100, 320, 330, 500, 900, 10000,
                                 100, 320, 330, 500, 900, 10000};

Board::Board() {
    initZobrist();
    transpositionTable.resize(TT_SIZE);
    resetToStart();
}

void Board::resetToStart() {
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
    epFile = -1;
    kingR[0] = 7; kingC[0] = 4;
    kingR[1] = 0; kingC[1] = 4;
    halfMoveClock = 0;
    whiteTurn = true;
    hash = computeHash();
    fill(transpositionTable.begin(), transpositionTable.end(), TTEntry{});
    for (auto& pair : killers) { pair[0] = Move{}; pair[1] = Move{}; }
    for (auto& row : history) for (auto& h : row) h = 0;
    hashHistory.clear();
}

void Board::setFromFen(const std::string& fen) {
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
            board[i][j] = '.';
    castleRights = 0;
    epFile = -1;
    kingR[0] = kingC[0] = kingR[1] = kingC[1] = -1;
    fill(transpositionTable.begin(), transpositionTable.end(), TTEntry{});
    hashHistory.clear();
    halfMoveClock = 0;

    istringstream ss(fen);
    string pieces, side, castling, ep;
    ss >> pieces >> side >> castling >> ep;
    int halfMove = 0;
    ss >> halfMove;
    halfMoveClock = halfMove;

    // Piece placement: FEN rank 8 first (row 0 in our array)
    int r = 0, c = 0;
    for (char ch : pieces) {
        if (ch == '/') { r++; c = 0; }
        else if (isdigit(ch)) c += ch - '0';
        else {
            if (ch == 'K') { kingR[0] = r; kingC[0] = c; }
            if (ch == 'k') { kingR[1] = r; kingC[1] = c; }
            board[r][c++] = ch;
        }
    }

    whiteTurn = (side == "w");

    for (char ch : castling) {
        if      (ch == 'K') castleRights |= WK_CASTLE;
        else if (ch == 'Q') castleRights |= WQ_CASTLE;
        else if (ch == 'k') castleRights |= BK_CASTLE;
        else if (ch == 'q') castleRights |= BQ_CASTLE;
    }

    if (ep.size() >= 2 && ep[0] != '-')
        epFile = ep[0] - 'a';

    hash = computeHash();
    if (!whiteTurn) hash ^= zobristSideToMove;
}

Move Board::parseUciMove(const std::string& uci) {
    Move m;
    m.fromC = uci[0] - 'a';
    m.fromR = 8 - (uci[1] - '0');
    m.toC   = uci[2] - 'a';
    m.toR   = 8 - (uci[3] - '0');

    if (uci.size() >= 5) {
        char p = uci[4];
        char piece = board[m.fromR][m.fromC];
        m.promotion = isWhitePiece(piece) ? (char)toupper(p) : (char)pieceType(p);
    }

    char piece = board[m.fromR][m.fromC];
    if ((piece == 'P' || piece == 'p') && m.fromC != m.toC && board[m.toR][m.toC] == '.')
        m.enPassant = true;

    return m;
}

std::string Board::moveToUci(const Move& m) const {
    std::string s;
    s += char('a' + m.fromC);
    s += char('0' + 8 - m.fromR);
    s += char('a' + m.toC);
    s += char('0' + 8 - m.toR);
    if (m.promotion != '.') s += (char)pieceType(m.promotion);
    return s;
}

Move Board::getBestMoveTime(int maxDepth, int timeLimitMs) {
    for (auto& row : history) for (auto& h : row) h /= 2;

    using namespace std::chrono;
    searchStart   = steady_clock::now();
    searchLimitMs = timeLimitMs;
    nodes = 0;
    stopRequested = false;
    maxDepth = min(maxDepth, MAX_PLY);

    vector<Move> moves = generateAllMoves(whiteTurn);
    if (moves.empty()) return Move{-1, -1, -1, -1};
    if (moves.size() == 1 && timeLimitMs > 0) return moves[0]; // forced move, save the clock

    Move bestMove = moves[0];
    int prevScore = 0;

    for (int d = 1; d <= maxDepth; d++) {
        if (stopRequested) break;
        // Each iteration costs several times the previous one, so don't start
        // a new one once half the budget is gone — it almost certainly won't finish.
        if (timeLimitMs > 0 && d > 1) {
            auto ms = duration_cast<milliseconds>(steady_clock::now() - searchStart).count();
            if (ms >= timeLimitMs / 2) break;
        }

        const int ASP = 50;
        int alpha = (d >= 3) ? prevScore - ASP : INT_MIN;
        int beta  = (d >= 3) ? prevScore + ASP : INT_MAX;

        int bestEval = whiteTurn ? INT_MIN : INT_MAX;
        Move iterBest = moves[0];

        // Search with aspiration window; on fail, retry with full window
        for (int attempt = 0; attempt < 2; attempt++) {
            bestEval = whiteTurn ? INT_MIN : INT_MAX;
            iterBest = moves[0];

            for (Move& mv : moves) {
                makeMove(mv);
                int eval = minimax(d - 1, 1, !whiteTurn, alpha, beta);
                undoMove(mv);
                if (stopRequested) break;
                bool better = whiteTurn ? (eval > bestEval) : (eval < bestEval);
                if (better) { bestEval = eval; iterBest = mv; }
            }
            if (stopRequested) break;

            if (attempt == 0 && d >= 3 && (bestEval <= alpha || bestEval >= beta)) {
                alpha = INT_MIN; beta = INT_MAX; // widen to full window and retry
            } else {
                break;
            }
        }

        // An interrupted iteration's scores are unreliable — keep the last completed result
        if (stopRequested) break;

        prevScore = bestEval;
        bestMove  = iterBest;

        for (int i = 0; i < (int)moves.size(); i++) {
            if (moves[i].fromR == bestMove.fromR && moves[i].fromC == bestMove.fromC &&
                moves[i].toR  == bestMove.toR   && moves[i].toC  == bestMove.toC) {
                swap(moves[0], moves[i]);
                break;
            }
        }

        auto ms = duration_cast<milliseconds>(steady_clock::now() - searchStart).count();
        int engineScore = whiteTurn ? bestEval : -bestEval;
        // Build the line first so it can't interleave with output from the UCI thread
        ostringstream info;
        info << "info depth " << d << " score cp " << engineScore
             << " nodes " << nodes << " time " << ms
             << " nps " << (ms > 0 ? nodes * 1000 / ms : nodes)
             << " pv " << moveToUci(bestMove) << "\n";
        cout << info.str() << flush;
    }

    return bestMove;
}

bool Board::shouldStop() {
    if (stopRequested.load(std::memory_order_relaxed)) return true;
    ++nodes;
    // Reading the clock is relatively expensive, so only do it every 2048 nodes
    if (searchLimitMs > 0 && (nodes & 2047) == 0) {
        using namespace std::chrono;
        auto ms = duration_cast<milliseconds>(steady_clock::now() - searchStart).count();
        if (ms >= searchLimitMs) stopRequested = true;
    }
    return stopRequested.load(std::memory_order_relaxed);
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
    for (int i = 0; i < 8; i++)
        zobristEP[i] = rng();
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
    if (epFile >= 0)
        h ^= zobristEP[epFile];
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
        if (toPiece != '.' && ((isWhitePiece(fromPiece) && isWhitePiece(toPiece)) ||
                               (isBlackPiece(fromPiece) && isBlackPiece(toPiece)))) {
            cout << "Cannot capture your own piece!" << endl;
            return;
        }
    }

    Move m = {fromR, fromC, toR, toC};
    if ((fromPiece == 'P' && toR == 0) || (fromPiece == 'p' && toR == 7))
        m.promotion = isWhitePiece(fromPiece) ? 'Q' : 'q';
    if ((fromPiece == 'P' || fromPiece == 'p') && fromC != toC && board[toR][toC] == '.')
        m.enPassant = true;
    makeMove(m);
}

bool Board::isValidMove(int fromR, int fromC, int toR, int toC) {
    if (toR < 0 || toR >= 8 || toC < 0 || toC >= 8)
        return false;

    char piece = board[fromR][fromC];
    if (piece == '.') return false;

    char target = board[toR][toC];
    if (target != '.') {
        if ((isWhitePiece(piece) && isWhitePiece(target)) ||
            (isBlackPiece(piece) && isBlackPiece(target)))
            return false;
    }

    switch (pieceType(piece)) {
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
        if (abs(dCol) == 1 && dRow == -1 && isBlackPiece(board[toR][toC])) return true;
        // En passant
        if (abs(dCol) == 1 && dRow == -1 && board[toR][toC] == '.' &&
            epFile == toC && fromR == 3) return true;
    }
    if (piece == 'p') {
        if (dCol == 0 && dRow == 1 && board[toR][toC] == '.') return true;
        if (dCol == 0 && dRow == 2 && fromR == 1 &&
            board[toR][toC] == '.' && board[fromR+1][fromC] == '.') return true;
        if (abs(dCol) == 1 && dRow == 1 && isWhitePiece(board[toR][toC])) return true;
        // En passant
        if (abs(dCol) == 1 && dRow == 1 && board[toR][toC] == '.' &&
            epFile == toC && fromR == 4) return true;
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

std::vector<Move> Board::generateAllMoves(bool whiteTurn, bool capturesOnly) {
    static const int knightDirs[8][2] = {{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
    static const int kingDirs[8][2]   = {{-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1}};
    static const int diagDirs[4][2]   = {{-1,-1},{-1,1},{1,-1},{1,1}};
    static const int orthDirs[4][2]   = {{-1,0},{1,0},{0,-1},{0,1}};

    std::vector<Move> moves;
    moves.reserve(capturesOnly ? 16 : 64);

    auto isEnemy = [&](char t) {
        return t != '.' && (whiteTurn ? isBlackPiece(t) : isWhitePiece(t));
    };
    // A move can only expose our own king if we're already in check, the king itself
    // moves, it's en passant, or the moving piece shares a line with the king (a possible
    // pin). Only those need the make/test/undo check; every other move is legal as-is.
    const int side = whiteTurn ? 0 : 1;
    const int kr = kingR[side], kc = kingC[side];
    const bool inCheck = isInCheck(whiteTurn);
    auto tryAdd = [&](Move m) {
        int dr = m.fromR - kr, dc = m.fromC - kc;
        bool mayExposeKing = inCheck || m.enPassant ||
                             dr == 0 || dc == 0 || dr == dc || dr == -dc;
        if (!mayExposeKing) {
            m.captured = board[m.toR][m.toC];
            moves.push_back(m);
            return;
        }
        makeMove(m);
        if (!isInCheck(whiteTurn)) moves.push_back(m);
        undoMove(m);
    };
    // Single-square moves (knight, king): empty squares unless capturesOnly, or enemy pieces
    auto addSteps = [&](int r, int c, const int (*dirs)[2], int n) {
        for (int i = 0; i < n; i++) {
            int tr = r + dirs[i][0], tc = c + dirs[i][1];
            if (tr < 0 || tr >= 8 || tc < 0 || tc >= 8) continue;
            char t = board[tr][tc];
            if ((t == '.' && !capturesOnly) || isEnemy(t)) tryAdd(Move{r, c, tr, tc});
        }
    };
    // Sliding moves (bishop, rook, queen): walk each ray until blocked
    auto addSlides = [&](int r, int c, const int (*dirs)[2], int n) {
        for (int i = 0; i < n; i++) {
            int tr = r + dirs[i][0], tc = c + dirs[i][1];
            while (tr >= 0 && tr < 8 && tc >= 0 && tc < 8) {
                char t = board[tr][tc];
                if (t == '.') {
                    if (!capturesOnly) tryAdd(Move{r, c, tr, tc});
                } else {
                    if (isEnemy(t)) tryAdd(Move{r, c, tr, tc});
                    break;
                }
                tr += dirs[i][0]; tc += dirs[i][1];
            }
        }
    };

    const int dir      = whiteTurn ? -1 : 1;
    const int startRow = whiteTurn ? 6 : 1;
    const int promoRow = whiteTurn ? 0 : 7;
    const int epRow    = whiteTurn ? 3 : 4; // row a pawn must be on to capture en passant
    const char* promos = whiteTurn ? "QRBN" : "qrbn";

    auto addPawnMove = [&](int r, int c, int tr, int tc) {
        if (tr == promoRow) {
            for (int i = 0; i < 4; i++) {
                Move m = {r, c, tr, tc};
                m.promotion = promos[i];
                tryAdd(m);
            }
        } else {
            tryAdd(Move{r, c, tr, tc});
        }
    };

    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            char piece = board[r][c];
            if (piece == '.') continue;
            if (whiteTurn  && isBlackPiece(piece)) continue;
            if (!whiteTurn && isWhitePiece(piece)) continue;

            switch (pieceType(piece)) {
                case 'p': {
                    int nr = r + dir;
                    if (!capturesOnly && board[nr][c] == '.') {
                        addPawnMove(r, c, nr, c);
                        if (r == startRow && board[r + 2 * dir][c] == '.')
                            tryAdd(Move{r, c, r + 2 * dir, c});
                    }
                    for (int dc = -1; dc <= 1; dc += 2) {
                        int tc = c + dc;
                        if (tc < 0 || tc >= 8) continue;
                        if (isEnemy(board[nr][tc])) {
                            addPawnMove(r, c, nr, tc);
                        } else if (!capturesOnly && board[nr][tc] == '.' &&
                                   epFile == tc && r == epRow) {
                            Move m = {r, c, nr, tc};
                            m.enPassant = true;
                            tryAdd(m);
                        }
                    }
                    break;
                }
                case 'n': addSteps(r, c, knightDirs, 8); break;
                case 'b': addSlides(r, c, diagDirs, 4); break;
                case 'r': addSlides(r, c, orthDirs, 4); break;
                case 'q': addSlides(r, c, diagDirs, 4); addSlides(r, c, orthDirs, 4); break;
                case 'k': addSteps(r, c, kingDirs, 8); break;
            }
        }
    }

    if (capturesOnly) return moves;

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
    int side = white ? 0 : 1;
    if (kingR[side] < 0) return false;
    return isSquareAttacked(kingR[side], kingC[side], !white);
}

void Board::makeMove(Move& move) {
    hashHistory.push_back(hash); // record pre-move position for repetition detection
    char moving = board[move.fromR][move.fromC];
    move.captured = board[move.toR][move.toC];
    move.prevCastleRights = castleRights;
    move.prevEpFile = epFile;
    move.prevHalfMoveClock = halfMoveClock;
    if (moving == 'P' || moving == 'p' || move.captured != '.' || move.enPassant)
        halfMoveClock = 0;
    else
        halfMoveClock++;

    // XOR out old ep and castle contributions
    if (epFile >= 0) hash ^= zobristEP[epFile];
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
    if (moving == 'K') { kingR[0] = move.toR; kingC[0] = move.toC; }
    if (moving == 'k') { kingR[1] = move.toR; kingC[1] = move.toC; }

    // En passant: remove the captured pawn from the side square
    if (move.enPassant) {
        char capturedPawn = (moving == 'P') ? 'p' : 'P';
        hash ^= zobristTable[pieceIndex(capturedPawn)][move.fromR * 8 + move.toC];
        board[move.fromR][move.toC] = '.';
    }

    // Set new ep file: only after a double pawn push
    if (moving == 'P' && move.fromR == 6 && move.toR == 4)
        epFile = move.toC;
    else if (moving == 'p' && move.fromR == 1 && move.toR == 3)
        epFile = move.toC;
    else
        epFile = -1;
    if (epFile >= 0) hash ^= zobristEP[epFile];

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
    hashHistory.pop_back();
    halfMoveClock = move.prevHalfMoveClock;
    char moving = board[move.toR][move.toC];
    // If this was a promotion, the piece to put back at fromR,fromC is the pawn
    char originalPiece = (move.promotion != '.')
                       ? (isWhitePiece(move.promotion) ? 'P' : 'p')
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
    if (move.captured != '.' && !move.enPassant)
        hash ^= zobristTable[pieceIndex(move.captured)][move.toR * 8 + move.toC];
    hash ^= zobristSideToMove;

    board[move.fromR][move.fromC] = originalPiece;
    if (originalPiece == 'K') { kingR[0] = move.fromR; kingC[0] = move.fromC; }
    if (originalPiece == 'k') { kingR[1] = move.fromR; kingC[1] = move.fromC; }
    board[move.toR][move.toC]     = move.enPassant ? '.' : move.captured;

    // Restore the en-passant captured pawn to its original square
    if (move.enPassant) {
        char capturedPawn = (originalPiece == 'P') ? 'p' : 'P';
        hash ^= zobristTable[pieceIndex(capturedPawn)][move.fromR * 8 + move.toC];
        board[move.fromR][move.toC] = capturedPawn;
    }

    // Restore ep file
    if (epFile >= 0) hash ^= zobristEP[epFile];
    epFile = move.prevEpFile;
    if (epFile >= 0) hash ^= zobristEP[epFile];

    // Restore castling rights
    for (int i = 0; i < 4; i++)
        if (castleRights & (1 << i))
            hash ^= zobristCastle[i];
    castleRights = move.prevCastleRights;
    for (int i = 0; i < 4; i++)
        if (castleRights & (1 << i))
            hash ^= zobristCastle[i];
}

// Sorts moves best-first, scoring each move once rather than on every comparison
template <typename ScoreFn>
static void sortMoves(vector<Move>& moves, ScoreFn score) {
    vector<pair<int, Move>> scored;
    scored.reserve(moves.size());
    for (const Move& m : moves) scored.push_back({score(m), m});
    stable_sort(scored.begin(), scored.end(),
                [](const pair<int, Move>& a, const pair<int, Move>& b) { return a.first > b.first; });
    for (size_t i = 0; i < moves.size(); i++) moves[i] = scored[i].second;
}

int Board::quiescence(int alpha, int beta, bool maximizingPlayer) {
    if (shouldStop()) return 0;
    int standPat = evaluate();

    if (maximizingPlayer) {
        if (standPat >= beta) return standPat;
        alpha = max(alpha, standPat);
    } else {
        if (standPat <= alpha) return standPat;
        beta = min(beta, standPat);
    }

    vector<Move> moves = generateAllMoves(maximizingPlayer, true);
    sortMoves(moves, [&](const Move& m) {
        return MVV_VAL[pieceIndex(m.captured)] * 10 - MVV_VAL[pieceIndex(board[m.fromR][m.fromC])];
    });
    for (Move& m : moves) {
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

int Board::minimax(int depth, int ply, bool maximizingPlayer, int alpha, int beta, bool nullMoveAllowed) {
    if (shouldStop()) return 0; // result is discarded by the caller once stopped
    if (ply >= MAX_PLY) return evaluate();

    // Repetition detection: if current position appeared before, treat as draw
    if (halfMoveClock >= 100) return 0;
    {
        int limit = min((int)hashHistory.size(), halfMoveClock + 1);
        int base  = (int)hashHistory.size() - limit;
        for (int i = base; i < (int)hashHistory.size(); i++)
            if (hashHistory[i] == hash) return 0;
    }

    // TT lookup — extract best move even when depth is insufficient
    TTEntry& entry = transpositionTable[hash % TT_SIZE];
    Move ttMove{-1, -1, -1, -1};
    if (entry.key == hash) {
        if (entry.ttFrom != 255) {
            ttMove.fromR = entry.ttFrom >> 3;
            ttMove.fromC = entry.ttFrom & 7;
            ttMove.toR   = entry.ttTo   >> 3;
            ttMove.toC   = entry.ttTo   & 7;
            ttMove.promotion = entry.ttPromo;
        }
        if (entry.depth >= depth) {
            if (entry.flag == 0) return entry.score;
            if (entry.flag == 1) alpha = max(alpha, entry.score);
            if (entry.flag == 2) beta  = min(beta,  entry.score);
            if (alpha >= beta) return entry.score;
        }
    }

    if (depth == 0) return quiescence(alpha, beta, maximizingPlayer);

    vector<Move> moves = generateAllMoves(maximizingPlayer);
    if (moves.empty()) {
        if (isInCheck(maximizingPlayer))
            return maximizingPlayer ? -(100000 + depth) : (100000 + depth);
        return 0;
    }

    // Null move pruning
    bool inCheck = isInCheck(maximizingPlayer);
    if (nullMoveAllowed && depth >= 3 && !inCheck) {
        int savedEpFile = epFile;
        if (epFile >= 0) hash ^= zobristEP[epFile];
        epFile = -1;
        hash ^= zobristSideToMove;

        int nullScore = minimax(depth - 3, ply + 1, !maximizingPlayer, alpha, beta, false);

        hash ^= zobristSideToMove;
        epFile = savedEpFile;
        if (epFile >= 0) hash ^= zobristEP[epFile];

        if (maximizingPlayer && nullScore >= beta) return beta;
        if (!maximizingPlayer && nullScore <= alpha) return alpha;
    }

    // Move ordering: TT move > captures (MVV-LVA) > killers > history
    auto moveScore = [&](const Move& m) -> int {
        if (ttMove.fromR != -1 &&
            m.fromR == ttMove.fromR && m.fromC == ttMove.fromC &&
            m.toR   == ttMove.toR   && m.toC   == ttMove.toC   &&
            m.promotion == ttMove.promotion)
            return 2000000;
        if (m.captured != '.')
            return 1000000 + MVV_VAL[pieceIndex(m.captured)] * 10
                           - MVV_VAL[pieceIndex(board[m.fromR][m.fromC])];
        if (m.fromR == killers[ply][0].fromR && m.fromC == killers[ply][0].fromC &&
            m.toR   == killers[ply][0].toR   && m.toC   == killers[ply][0].toC) return 900000;
        if (m.fromR == killers[ply][1].fromR && m.fromC == killers[ply][1].fromC &&
            m.toR   == killers[ply][1].toR   && m.toC   == killers[ply][1].toC) return 800000;
        return history[m.fromR * 8 + m.fromC][m.toR * 8 + m.toC];
    };
    sortMoves(moves, moveScore);

    int originalAlpha = alpha;
    int best;
    Move bestMoveInNode; bestMoveInNode.fromR = -1;

    if (maximizingPlayer) {
        best = INT_MIN;
        int moveCount = 0;
        for (Move& m : moves) {
            bool isQuiet = (m.captured == '.' && m.promotion == '.');
            bool doLMR   = depth >= 3 && moveCount >= 4 && isQuiet && !inCheck;
            makeMove(m);
            int val;
            if (doLMR) {
                val = minimax(depth - 2, ply + 1, false, alpha, beta, true);
                if (val > alpha)
                    val = minimax(depth - 1, ply + 1, false, alpha, beta, true);
            } else {
                val = minimax(depth - 1, ply + 1, false, alpha, beta, true);
            }
            undoMove(m);
            moveCount++;
            if (val > best) { best = val; bestMoveInNode = m; }
            if (best > alpha) alpha = best;
            if (alpha >= beta) {
                if (isQuiet) {
                    killers[ply][1] = killers[ply][0];
                    killers[ply][0] = m;
                    history[m.fromR * 8 + m.fromC][m.toR * 8 + m.toC] += depth * depth;
                }
                break;
            }
        }
    } else {
        best = INT_MAX;
        int moveCount = 0;
        for (Move& m : moves) {
            bool isQuiet = (m.captured == '.' && m.promotion == '.');
            bool doLMR   = depth >= 3 && moveCount >= 4 && isQuiet && !inCheck;
            makeMove(m);
            int val;
            if (doLMR) {
                val = minimax(depth - 2, ply + 1, true, alpha, beta, true);
                if (val < beta)
                    val = minimax(depth - 1, ply + 1, true, alpha, beta, true);
            } else {
                val = minimax(depth - 1, ply + 1, true, alpha, beta, true);
            }
            undoMove(m);
            moveCount++;
            if (val < best) { best = val; bestMoveInNode = m; }
            if (best < beta) beta = best;
            if (alpha >= beta) {
                if (isQuiet) {
                    killers[ply][1] = killers[ply][0];
                    killers[ply][0] = m;
                    history[m.fromR * 8 + m.fromC][m.toR * 8 + m.toC] += depth * depth;
                }
                break;
            }
        }
    }

    if (stopRequested) return best; // don't store scores from an aborted search

    TTEntry& slot = transpositionTable[hash % TT_SIZE];
    slot.key   = hash;
    slot.depth = depth;
    slot.score = best;
    if (bestMoveInNode.fromR != -1) {
        slot.ttFrom  = bestMoveInNode.fromR * 8 + bestMoveInNode.fromC;
        slot.ttTo    = bestMoveInNode.toR   * 8 + bestMoveInNode.toC;
        slot.ttPromo = bestMoveInNode.promotion;
    }
    if      (best <= originalAlpha) slot.flag = 2;
    else if (best >= beta)          slot.flag = 1;
    else                            slot.flag = 0;

    return best;
}

Move Board::getBestMove(int depth, bool whiteTurn) {
    for (auto& row : history) for (auto& h : row) h /= 2;
    searchLimitMs = 0;
    stopRequested = false;

    vector<Move> moves = generateAllMoves(whiteTurn);
    if (moves.empty()) return Move{};

    Move bestMove = moves[0];
    int prevScore = 0;

    for (int d = 1; d <= depth; d++) {
        const int ASP = 50;
        int alpha = (d >= 3) ? prevScore - ASP : INT_MIN;
        int beta  = (d >= 3) ? prevScore + ASP : INT_MAX;

        int bestEval = whiteTurn ? INT_MIN : INT_MAX;
        Move iterBest = moves[0];

        for (int attempt = 0; attempt < 2; attempt++) {
            bestEval = whiteTurn ? INT_MIN : INT_MAX;
            iterBest = moves[0];

            for (Move& m : moves) {
                makeMove(m);
                int eval = minimax(d - 1, 1, !whiteTurn, alpha, beta);
                undoMove(m);
                bool better = whiteTurn ? (eval > bestEval) : (eval < bestEval);
                if (better) { bestEval = eval; iterBest = m; }
            }

            if (attempt == 0 && d >= 3 && (bestEval <= alpha || bestEval >= beta)) {
                alpha = INT_MIN; beta = INT_MAX;
            } else {
                break;
            }
        }

        prevScore = bestEval;
        bestMove  = iterBest;

        for (int i = 0; i < (int)moves.size(); i++) {
            if (moves[i].fromR == bestMove.fromR && moves[i].fromC == bestMove.fromC &&
                moves[i].toR  == bestMove.toR   && moves[i].toC  == bestMove.toC) {
                swap(moves[0], moves[i]);
                break;
            }
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

static const int kingEndgamePST[8][8] = {
    {-50,-40,-30,-20,-20,-30,-40,-50},
    {-30,-20,-10,  0,  0,-10,-20,-30},
    {-30,-10, 20, 30, 30, 20,-10,-30},
    {-30,-10, 30, 40, 40, 30,-10,-30},
    {-30,-10, 30, 40, 40, 30,-10,-30},
    {-30,-10, 20, 30, 30, 20,-10,-30},
    {-30,-30,  0,  0,  0,  0,-30,-30},
    {-50,-30,-30,-30,-30,-30,-30,-50},
};

static const int passedPawnBonus[8] = {0, 10, 20, 35, 60, 90, 120, 0};

int Board::kingSafety(bool white, uint64_t enemyAttacks, const int pawnsPerFile[8]) const {
    char pawn = white ? 'P' : 'p';
    int kr = kingR[white ? 0 : 1], kc = kingC[white ? 0 : 1];
    if (kr == -1) return 0;

    int score = 0;

    int shieldRow = white ? kr - 1 : kr + 1;
    if (shieldRow >= 0 && shieldRow < 8) {
        for (int dc = -1; dc <= 1; dc++) {
            int fc = kc + dc;
            if (fc >= 0 && fc < 8 && board[shieldRow][fc] == pawn)
                score += 10;
        }
    }

    // Penalise open files next to the king
    for (int dc = -1; dc <= 1; dc++) {
        int fc = kc + dc;
        if (fc < 0 || fc >= 8) continue;
        if (pawnsPerFile[fc] == 0) score -= 20;
    }

    // Penalise enemy attacks on the king and its surrounding squares
    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            int zr = kr + dr, zc = kc + dc;
            if (zr < 0 || zr >= 8 || zc < 0 || zc >= 8) continue;
            if ((enemyAttacks >> (zr * 8 + zc)) & 1)
                score -= 8;
        }
    }

    return score;
}

// Returns the piece's mobility (squares it can move to) and marks every square it
// attacks in `attacks`, including squares held by its own side. The attack map lets
// kingSafety() avoid calling isSquareAttacked() for each square around the king.
int Board::countMobility(int r, int c, uint64_t& attacks) const {
    char piece = board[r][c];
    bool white = isWhitePiece(piece);
    char lower = pieceType(piece);
    int count = 0;

    auto canLand = [&](int nr, int nc) {
        char t = board[nr][nc];
        return t == '.' || (white ? isBlackPiece(t) : isWhitePiece(t));
    };
    auto mark = [&](int nr, int nc) { attacks |= 1ULL << (nr * 8 + nc); };

    if (lower == 'p') {
        int dir = white ? -1 : 1;
        int nr = r + dir;
        if (nr >= 0 && nr < 8) {
            if (board[nr][c] == '.') count++;
            if (c > 0) { mark(nr, c-1); if (board[nr][c-1] != '.' && canLand(nr, c-1)) count++; }
            if (c < 7) { mark(nr, c+1); if (board[nr][c+1] != '.' && canLand(nr, c+1)) count++; }
        }
    } else if (lower == 'n') {
        static const int nd[8][2] = {{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
        for (auto& d : nd) {
            int nr = r+d[0], nc = c+d[1];
            if (nr>=0&&nr<8&&nc>=0&&nc<8) { mark(nr, nc); if (canLand(nr,nc)) count++; }
        }
    } else if (lower == 'k') {
        // King mobility intentionally excluded to avoid incentivising early king movement
        static const int kd[8][2] = {{-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1}};
        for (auto& d : kd) {
            int nr = r+d[0], nc = c+d[1];
            if (nr>=0&&nr<8&&nc>=0&&nc<8) mark(nr, nc);
        }
    }
    if (lower == 'b' || lower == 'q') {
        static const int dd[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
        for (auto& d : dd) {
            int nr = r+d[0], nc = c+d[1];
            while (nr>=0&&nr<8&&nc>=0&&nc<8) {
                mark(nr, nc);
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
                mark(nr, nc);
                if (board[nr][nc] == '.') { count++; nr+=d[0]; nc+=d[1]; }
                else { if (canLand(nr,nc)) count++; break; }
            }
        }
    }

    return count;
}

int Board::evaluate() {
    // Single pass: material, PST, mobility, attack maps, and pawn data
    int totalMat = 0; // non-pawn material, for endgame detection
    int minBPawnRow[8], maxWPawnRow[8];
    fill(minBPawnRow, minBPawnRow + 8, 8);  // 8 = no black pawn
    fill(maxWPawnRow, maxWPawnRow + 8, -1); // -1 = no white pawn
    int wPawns[8] = {}, bPawns[8] = {};
    uint64_t attacks[2] = {0, 0};           // squares attacked by [0]=white [1]=black

    int score = 0;
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            char piece = board[r][c];
            if (piece == '.') continue;

            bool white = isWhitePiece(piece);
            int pstRow = white ? (7 - r) : r;

            int material = 0, pst = 0;
            switch (pieceType(piece)) {
                case 'p':
                    material = 100; pst = pawnPST[pstRow][c];
                    if (white) { wPawns[c]++; if (r > maxWPawnRow[c]) maxWPawnRow[c] = r; }
                    else       { bPawns[c]++; if (r < minBPawnRow[c]) minBPawnRow[c] = r; }
                    break;
                case 'n': material = 320; pst = knightPST[pstRow][c]; totalMat += 300; break;
                case 'b': material = 330; pst = bishopPST[pstRow][c]; totalMat += 300; break;
                case 'r': material = 500; pst = rookPST[pstRow][c];   totalMat += 500; break;
                case 'q': material = 900; pst = queenPST[pstRow][c];  totalMat += 900; break;
                case 'k': break; // king PST depends on game phase, added below
            }

            int mobility = countMobility(r, c, attacks[white ? 0 : 1]);
            if (white) score += material + pst + mobility * 3;
            else       score -= material + pst + mobility * 3;
        }
    }
    bool endgame = (totalMat <= 1300);

    const int (*kingTable)[8] = endgame ? kingEndgamePST : kingPST;
    if (kingR[0] >= 0) score += kingTable[7 - kingR[0]][kingC[0]];
    if (kingR[1] >= 0) score -= kingTable[kingR[1]][kingC[1]];

    score += kingSafety(true,  attacks[1], wPawns);
    score -= kingSafety(false, attacks[0], bPawns);

    // Pawn structure penalties
    for (int c = 0; c < 8; c++) {
        if (wPawns[c] > 1) score -= 20 * (wPawns[c] - 1);
        if (bPawns[c] > 1) score += 20 * (bPawns[c] - 1);
        if (wPawns[c] > 0) {
            bool iso = (c == 0 || wPawns[c-1] == 0) && (c == 7 || wPawns[c+1] == 0);
            if (iso) score -= 15 * wPawns[c];
        }
        if (bPawns[c] > 0) {
            bool iso = (c == 0 || bPawns[c-1] == 0) && (c == 7 || bPawns[c+1] == 0);
            if (iso) score += 15 * bPawns[c];
        }
    }

    // Passed pawn bonuses
    for (int r = 1; r < 7; r++) {
        for (int c = 0; c < 8; c++) {
            if (board[r][c] == 'P') {
                bool passed = true;
                for (int fc = max(0, c-1); fc <= min(7, c+1) && passed; fc++)
                    if (minBPawnRow[fc] < r) passed = false;
                if (passed) score += passedPawnBonus[7 - r];
            } else if (board[r][c] == 'p') {
                bool passed = true;
                for (int fc = max(0, c-1); fc <= min(7, c+1) && passed; fc++)
                    if (maxWPawnRow[fc] > r) passed = false;
                if (passed) score -= passedPawnBonus[r];
            }
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
