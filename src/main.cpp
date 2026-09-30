#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <chrono>
#include "../include/Board.h"

using namespace std;

// ── Interactive human-vs-engine game ─────────────────────────────────────────

static void runInteractive() {
    Board board;

    while (true) {
        board.printBoard();
        cout << "Evaluation: " << board.evaluate() << "\n";

        // Human (White)
        vector<Move> whiteMoves = board.generateAllMoves(true);
        if (whiteMoves.empty()) {
            cout << (board.isInCheck(true) ? "Checkmate! Black wins." : "Stalemate!") << "\n";
            break;
        }

        cout << "White to move (e2 e4) or '9' to quit: ";
        string input;
        if (!getline(cin, input)) break;
        if (input == "9") break;

        if (input.size() < 5 || input[2] != ' ') {
            cout << "Invalid input format. Use: e2 e4\n";
            continue;
        }

        char fc = input[0], fr = input[1], tc = input[3], tr = input[4];
        if (fc < 'a' || fc > 'h' || tc < 'a' || tc > 'h' ||
            fr < '1' || fr > '8' || tr < '1' || tr > '8') {
            cout << "Invalid coordinates. Columns a-h, rows 1-8.\n";
            continue;
        }

        int fromC = fc - 'a', fromR = 8 - (fr - '0');
        int toC   = tc - 'a', toR   = 8 - (tr - '0');

        bool found = false;
        for (const Move& m : whiteMoves)
            if (m.fromR == fromR && m.fromC == fromC && m.toR == toR && m.toC == toC)
                { found = true; break; }

        if (!found) { cout << "Invalid move!\n"; continue; }

        board.movePiece(fromR, fromC, toR, toC);
        board.whiteTurn = false;

        // Engine (Black)
        board.printBoard();
        cout << "Evaluation: " << board.evaluate() << "\n";

        vector<Move> blackMoves = board.generateAllMoves(false);
        if (blackMoves.empty()) {
            cout << (board.isInCheck(false) ? "Checkmate! White wins." : "Stalemate!") << "\n";
            break;
        }

        cout << "Black (engine) is thinking...\n";
        Move engineMove = board.getBestMove(6, false);
        cout << "Black plays: "
             << char('a' + engineMove.fromC) << (8 - engineMove.fromR) << " to "
             << char('a' + engineMove.toC)   << (8 - engineMove.toR);
        if (engineMove.promotion != '.') cout << "=" << (char)tolower(engineMove.promotion);
        cout << "\n";
        // Use makeMove directly so the engine's exact promotion choice is respected
        board.makeMove(engineMove);
        board.whiteTurn = true;
    }
}

// ── Perft / bench (correctness and speed checks) ─────────────────────────────

// Counts leaf nodes of the legal move tree; compared against known values
// to verify move generation.
static uint64_t perft(Board& board, int depth) {
    if (depth == 0) return 1;
    vector<Move> moves = board.generateAllMoves(board.whiteTurn);
    if (depth == 1) return moves.size();
    uint64_t total = 0;
    for (Move& m : moves) {
        board.makeMove(m);
        board.whiteTurn = !board.whiteTurn;
        total += perft(board, depth - 1);
        board.whiteTurn = !board.whiteTurn;
        board.undoMove(m);
    }
    return total;
}

struct PerftCase { const char* fen; int depth; uint64_t expected; };

// Standard positions from the Chess Programming Wiki
static const PerftCase PERFT_SUITE[] = {
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",               5, 4865609},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",   4, 4085603},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",                              5, 674624},
    {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",       4, 422333},
    {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",              4, 2103487},
    {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3894594},
};

static bool runPerftSuite() {
    bool allOk = true;
    for (const PerftCase& pc : PERFT_SUITE) {
        Board board;
        board.setFromFen(pc.fen);
        auto t0 = chrono::steady_clock::now();
        uint64_t n = perft(board, pc.depth);
        auto ms = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - t0).count();
        bool ok = (n == pc.expected);
        allOk &= ok;
        cout << (ok ? "OK   " : "FAIL ") << "depth " << pc.depth << "  " << n
             << " (expected " << pc.expected << ")  " << ms << " ms  " << pc.fen << "\n";
    }
    cout << (allOk ? "perft: all passed\n" : "perft: FAILURES\n");
    return allOk;
}

// Fixed-depth searches over a few varied positions; reports total time and nodes.
static void runBench(int depth) {
    static const char* BENCH_FENS[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
    };
    auto t0 = chrono::steady_clock::now();
    for (const char* fen : BENCH_FENS) {
        Board board;
        board.setFromFen(fen);
        Move best = board.getBestMoveTime(depth, 0);
        cout << "bestmove " << board.moveToUci(best) << "  " << fen << "\n";
    }
    auto ms = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - t0).count();
    cout << "bench depth " << depth << ": " << ms << " ms\n";
}

// ── UCI protocol loop (for Arena / Cute Chess / Lichess) ─────────────────────

static void runUCI() {
    Board board;
    string line;

    while (getline(cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        if (line == "uci") {
            cout << "id name ChessEngine\n"
                 << "id author Yuvraj Sahota\n"
                 << "uciok\n";
            cout.flush();
        }
        else if (line == "isready") {
            cout << "readyok\n";
            cout.flush();
        }
        else if (line == "ucinewgame") {
            board.resetToStart();
        }
        else if (line.rfind("position", 0) == 0) {
            string rest = line.size() > 9 ? line.substr(9) : "";
            string posPart = rest, movesPart = "";
            size_t mi = rest.find(" moves");
            if (mi != string::npos) {
                posPart   = rest.substr(0, mi);
                movesPart = rest.size() > mi + 7 ? rest.substr(mi + 7) : "";
            }

            if (posPart == "startpos") {
                board.resetToStart();
            } else if (posPart.rfind("fen ", 0) == 0) {
                board.setFromFen(posPart.substr(4));
            }

            if (!movesPart.empty()) {
                istringstream iss(movesPart);
                string uci;
                while (iss >> uci) {
                    Move m = board.parseUciMove(uci);
                    board.makeMove(m);
                    board.whiteTurn = !board.whiteTurn;
                }
            }
        }
        else if (line.rfind("go", 0) == 0) {
            int depth = 6, timeLimitMs = 0;
            int wtime = 0, btime = 0, winc = 0, binc = 0, movestogo = 0;

            // substr(2) safely skips "go" and leaves the space+args for >> parsing
            istringstream iss(line.size() > 2 ? line.substr(2) : "");
            string token;
            while (iss >> token) {
                if      (token == "depth")     iss >> depth;
                else if (token == "movetime")  { iss >> timeLimitMs; depth = 100; }
                else if (token == "wtime")     iss >> wtime;
                else if (token == "btime")     iss >> btime;
                else if (token == "winc")      iss >> winc;
                else if (token == "binc")      iss >> binc;
                else if (token == "movestogo") iss >> movestogo;
                else if (token == "infinite")  { depth = 7; timeLimitMs = 0; }
            }

            if (wtime > 0 || btime > 0) {
                int myTime = board.whiteTurn ? wtime : btime;
                int myInc  = board.whiteTurn ? winc  : binc;
                // Use movestogo when provided; otherwise assume ~25 moves remaining
                int divisor = (movestogo > 0) ? movestogo + 2 : 25;
                timeLimitMs = max(50, myTime / divisor + myInc / 2);
                depth = 100;
            }

            Move best = board.getBestMoveTime(depth, timeLimitMs);
            cout << "bestmove " << (best.fromR < 0 ? "0000" : board.moveToUci(best)) << "\n";
            cout.flush();
        }
        else if (line == "stop") {
            // Single-threaded: search already finished and bestmove already sent.
            // Nothing to do, but we must not crash on this command.
        }
        else if (line == "quit") {
            break;
        }
    }
}

// ── Entry point ───────────────────────────────────────────────────────────────
//
//   chess.exe          →  UCI mode  (for Arena / any GUI)
//   chess.exe --play   →  interactive terminal game
//   chess.exe --perft  →  move generation correctness suite
//   chess.exe --bench [depth]  →  fixed-depth search timing

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if (arg == "--play") {
            runInteractive();
            return 0;
        }
        if (arg == "--perft") {
            return runPerftSuite() ? 0 : 1;
        }
        if (arg == "--bench") {
            runBench(i + 1 < argc ? stoi(argv[i + 1]) : 6);
            return 0;
        }
    }

    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    runUCI();
    return 0;
}
