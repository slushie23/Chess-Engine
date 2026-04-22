#include <iostream>
#include "../include/Board.h"
using namespace std;

int main() {
    Board board;
    board.printBoard();

    string move;

    cout << "Enter move (e2 e4): ";
    getline(cin, move);

    int fromC = move[0] - 'a';
    int fromR = 8 - (move[1] - '0');

    int toC = move[3] - 'a';
    int toR = 8 - (move[4] - '0');

    cout << "From: " << char('a' + fromC) << char('8' - fromR) << endl;
    cout << "To: "   << char('a' + toC)   << char('8' - toR)   << endl;

    board.movePiece(fromR, fromC, toR, toC);
    board.printBoard();

    return 0;
}
