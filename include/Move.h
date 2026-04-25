#ifndef MOVE_H
#define MOVE_H

struct Move {
    int fromR, fromC;
    int toR, toC;
    char captured = '.';
};

#endif