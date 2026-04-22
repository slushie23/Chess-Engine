#include <iostream>
#include "../include/Board.h"

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
}

void Board::printBoard() {
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            cout << board[i][j] << " ";
        }
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
    char toPiece = board[toR][toC];

    if (toPiece != '.') {
        if ((isupper(fromPiece) && isupper(toPiece)) ||
            (islower(fromPiece) && islower(toPiece))) {
            cout << "Cannot capture your own piece!" << endl;
            return;
        }
    }
    
    board[toR][toC] = board[fromR][fromC];
    board[fromR][fromC] = '.';
}