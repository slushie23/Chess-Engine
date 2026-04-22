#ifndef BOARD_H
#define BOARD_H

class Board {
private:
    char board[8][8];

public:
    Board();
    void printBoard();
    void movePiece(int fromR, int fromC, int toR, int toC);
};

#endif