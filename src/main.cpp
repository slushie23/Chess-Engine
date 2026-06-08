#include <iostream>
#include <vector>
#include "../include/Board.h"

using namespace std;

int main() {
    Board board;

    while (true) {
        board.printBoard();
        cout << "Evaluation: " << board.evaluate() << endl;

        // --- Human (White) ---
        vector<Move> whiteMoves = board.generateAllMoves(true);
        if (whiteMoves.empty()) {
            cout << (board.isInCheck(true) ? "Checkmate! Black wins." : "Stalemate!") << endl;
            break;
        }

        cout << "White to move (e2 e4) or '9' to quit: ";
        string input;
        getline(cin, input);
        if (input == "9") break;

        if (input.length() < 5 || input[2] != ' ') {
            cout << "Invalid input format! Use: e2 e4" << endl;
            continue;
        }

        char fc = input[0], fr = input[1], tc = input[3], tr = input[4];
        if (fc < 'a' || fc > 'h' || tc < 'a' || tc > 'h' ||
            fr < '1' || fr > '8' || tr < '1' || tr > '8') {
            cout << "Invalid coordinates! Columns a-h, rows 1-8." << endl;
            continue;
        }

        int fromC = fc - 'a';
        int fromR = 8 - (fr - '0');
        int toC   = tc - 'a';
        int toR   = 8 - (tr - '0');

        bool found = false;
        for (const Move& m : whiteMoves) {
            if (m.fromR == fromR && m.fromC == fromC && m.toR == toR && m.toC == toC) {
                found = true;
                break;
            }
        }
        if (!found) {
            cout << "Invalid move!" << endl;
            continue;
        }

        board.movePiece(fromR, fromC, toR, toC);

        // --- Engine (Black) ---
        board.printBoard();
        cout << "Evaluation: " << board.evaluate() << endl;

        vector<Move> blackMoves = board.generateAllMoves(false);
        if (blackMoves.empty()) {
            cout << (board.isInCheck(false) ? "Checkmate! White wins." : "Stalemate!") << endl;
            break;
        }

        cout << "Black (engine) is thinking..." << endl;
        Move engineMove = board.getBestMove(4, false);
        cout << "Black plays: "
             << char('a' + engineMove.fromC) << (8 - engineMove.fromR) << " to "
             << char('a' + engineMove.toC)   << (8 - engineMove.toR)   << endl;

        board.movePiece(engineMove.fromR, engineMove.fromC, engineMove.toR, engineMove.toC);
    }

    return 0;
}
