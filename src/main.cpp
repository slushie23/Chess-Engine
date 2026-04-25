#include <iostream>
#include <vector>
#include "../include/Board.h"

using namespace std;

int main() {
    Board board;
    bool whiteTurn = true;

    while (true) {
        board.printBoard();
        cout << "Evaluation: " << board.evaluate() << endl;

        cout << "Best move for " << (whiteTurn ? "White" : "Black") << ": ";
        Move bestMove = board.getBestMove(4, whiteTurn);
        cout << char('a' + bestMove.fromC) << (8 - bestMove.fromR) << " to "
             << char('a' + bestMove.toC) << (8 - bestMove.toR) << endl;
        


        // show whose turn it is
        cout << (whiteTurn ? "White" : "Black") << " to move" << endl;

        // Show all possible moves 
        vector<Move> moves = board.generateAllMoves(whiteTurn);
        cout << "Available moves: " << moves.size() << endl;

        //Get input for next move
        string move;
        cout << "Enter move (e2 e4) or '9' to quit: ";
        getline(cin, move);

        if (move == "9") break;

        if (move.length() < 5 || move[2] != ' ') {
            cout << "Invalid input format! Use: e2 e4" << endl;
            continue;
        }

        char fc = move[0], fr = move[1], tc = move[3], tr = move[4];
        if (fc < 'a' || fc > 'h' || tc < 'a' || tc > 'h' ||
            fr < '1' || fr > '8' || tr < '1' || tr > '8') {
            cout << "Invalid coordinates! Columns a-h, rows 1-8." << endl;
            continue;
        }

        //Convert to 0-based index
        int fromC = fc - 'a';
        int fromR = 8 - (fr - '0');
        int toC   = tc - 'a';
        int toR   = 8 - (tr - '0');

        // Check if move is in the list of generated moves
        bool found = false;
        for (const Move& m : moves) {
            if (m.fromR == fromR && m.fromC == fromC && m.toR == toR && m.toC == toC) {
                found = true;
                break;
            }
        }

        if (!found) {
            cout << "Invalid move!" << endl;
            continue;
        }

        // Make the move
        board.movePiece(fromR, fromC, toR, toC);
        whiteTurn = !whiteTurn;
    }

    return 0;
}