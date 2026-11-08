// move generation of Board::generateMoves into a vector
// !!! every strategy includes this one copy, otherwise the move order drifts
// between them and the reference compares two different trees

#ifndef MOVEGEN_H
#define MOVEGEN_H

#include "board.h"
#include "move.h"
#include <vector>

inline std::vector<Move> generateMovesBoard(const Board& board)
{
    std::vector<Move> moves;
    moves.reserve(80);

    for (int oi = 0; oi < Board::RealFields; oi++) {
        int startField = board.order[oi];

        if (board.field[startField] != board.color) continue;
        int d, dir, c, actField;
        bool left, right;
        int opponent = (board.color == board.color1) ? board.color2 : board.color1;

        /* 6 directions	*/
        for(d=1;d<7;d++) {
            dir = board.direction[d];

            /* 2nd field */
            c = board.field[actField = startField+dir];
            if (c == board.free) {
                /* (c .) */
                moves.emplace_back(startField, d, Move::move1);
                continue;
            }
            if (c != board.color)
                continue;

            /* 2nd == color */

            left = (board.field[startField + board.direction[d-1]] == board.free);
            if (left) {
                left = (board.field[actField + board.direction[d-1]] == board.free);
                if (left)
        	        /* 2 left */
        	        moves.emplace_back(startField, d, Move::left2);
            }

            right = (board.field[startField + board.direction[d+1]] == board.free);
            if (right) {
                right = (board.field[actField + board.direction[d+1]] == board.free);
                if (right)
        	        /* 2 right */
        	        moves.emplace_back(startField, d, Move::right2);
            }

            /* 3rd field */
            c = board.field[actField += dir];
            if (c == board.free) {
                /* (c c .) */
                moves.emplace_back(startField, d, Move::move2);
                continue;
            }
            else if (c == opponent) {

                /* 4th field */
                c = board.field[actField += dir];
                if (c == board.free) {
        	        /* (c c o .) */
        	        moves.emplace_back(startField, d, Move::push1with2);
                }
                else if (c == board.out) {
        	        /* (c c o |) */
        	        moves.emplace_back(startField, d, Move::out1with2);
                }
                continue;
            }
            if (c != board.color)
                continue;

            /* 3nd == color */

            if (left) {
                if (board.field[actField + board.direction[d-1]] == board.free)
            	    /* 3 left */
            	    moves.emplace_back(startField, d, Move::left3);
            }

            if (right) {
                if (board.field[actField + board.direction[d+1]] == board.free)
            	    /* 3 right */
            	    moves.emplace_back(startField, d, Move::right3);
            }

            /* 4th field */
            c = board.field[actField += dir];
            if (c == board.free) {
                /* (c c c .) */
                moves.emplace_back(startField, d, Move::move3);
                continue;
            }
            if (c != opponent)
                continue;

            /* 4nd == opponent */

            /* 5. field */
            c = board.field[actField += dir];
            if (c == board.free) {
                /* (c c c o .) */
                moves.emplace_back(startField, d, Move::push1with3);
                continue;
            }
            else if (c == board.out) {
                /* (c c c o |) */
                moves.emplace_back(startField, d, Move::out1with3);
                continue;
            }
            if (c != opponent)
                continue;

            /* 5nd == opponent */

            /* 6. field */
            c = board.field[actField += dir];
            if (c == board.free) {
                /* (c c c o o .) */
                moves.emplace_back(startField, d, Move::push2);
            }
            else if (c == board.out) {
                /* (c c c o o |) */
                moves.emplace_back(startField, d, Move::out2);
            }
        }
    }
    return moves;
}

#endif
