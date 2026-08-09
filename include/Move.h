#ifndef MOVE_H
#define MOVE_H
#include <cstdint>

struct Move {
    int fromR, fromC;
    int toR, toC;
    char captured = '.';
    uint8_t prevCastleRights = 0;
    char promotion = '.'; // '.' = none; else the piece char (e.g. 'Q','q')
    int prevEpFile = -1;  // ep file before this move (-1 = none)
    bool enPassant = false;
    int prevHalfMoveClock = 0;
};

#endif
