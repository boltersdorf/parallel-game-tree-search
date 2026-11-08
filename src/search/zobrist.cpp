#include "zobrist.h"
#include <random>

std::vector<uint64_t> ZobristHashtable::hashVariables;
std::vector<int>      ZobristHashtable::field2Ordermap;

static inline void xorPiece(uint64_t& hash, int f, int pc)
{
    int oi = ZobristHashtable::field2Ordermap[f];
    hash ^= ZobristHashtable::hashVariables[oi * 2 + (pc == Board::color2 ? 1 : 0)];
}


void ZobristHashtable::init()
{
    if (!hashVariables.empty()) return;

    field2Ordermap.assign(Board::AllFields, -1);
    for (int i = 0; i < Board::RealFields; i++)
        field2Ordermap[Board::order[i]] = i;

    // 61 fields x 2 colors = 122 piece keys, index 122 is side to move
    hashVariables.resize(123);
    std::mt19937_64 rng(0x1234567890ABCDEFULL);
    for (auto& v : hashVariables) v = rng();
}

uint64_t ZobristHashtable::getHash(const Board& board)
{
    uint64_t hash = 0;
    for (int i = 0; i < Board::RealFields; i++) {
        int occ = board.field[Board::order[i]];
        if (occ == Board::color1)
            hash ^= hashVariables[i * 2];
        else if (occ == Board::color2)
            hash ^= hashVariables[i * 2 + 1];
    }
    if (board.color == Board::color2)
        hash ^= hashVariables[122];
    return hash;
}


uint64_t ZobristHashtable::advanceHash(uint64_t hash, const Move& move, int color)
{
    const int opponent = (color == Board::color1) ? Board::color2 : Board::color1;
    const int f   = move.field;
    const int dir = Board::direction[move.direction];
    int dir2;

    xorPiece(hash, f, color);

    switch (move.type) {
    case Move::move1:
        xorPiece(hash, f + dir, color);
        break;

    case Move::move2:
        xorPiece(hash, f + 2*dir, color);
        break;

    case Move::move3:
        xorPiece(hash, f + 3*dir, color);
        break;

    case Move::left2:
        dir2 = Board::direction[move.direction - 1];
        xorPiece(hash, f + dir,        color);
        xorPiece(hash, f + dir2,       color);
        xorPiece(hash, f + dir + dir2, color);
        break;

    case Move::right2:
        dir2 = Board::direction[move.direction + 1];
        xorPiece(hash, f + dir,        color);
        xorPiece(hash, f + dir2,       color);
        xorPiece(hash, f + dir + dir2, color);
        break;

    case Move::left3:
        dir2 = Board::direction[move.direction - 1];
        xorPiece(hash, f + dir,          color);
        xorPiece(hash, f + 2*dir,        color);
        xorPiece(hash, f + dir2,         color);
        xorPiece(hash, f + dir  + dir2,  color);
        xorPiece(hash, f + 2*dir + dir2, color);
        break;

    case Move::right3:
        dir2 = Board::direction[move.direction + 1];
        xorPiece(hash, f + dir,          color);
        xorPiece(hash, f + 2*dir,        color);
        xorPiece(hash, f + dir2,         color);
        xorPiece(hash, f + dir  + dir2,  color);
        xorPiece(hash, f + 2*dir + dir2, color);
        break;

    case Move::push1with2:
        xorPiece(hash, f + 2*dir, opponent);
        xorPiece(hash, f + 2*dir, color);
        xorPiece(hash, f + 3*dir, opponent);
        break;

    case Move::push1with3:
        xorPiece(hash, f + 3*dir, opponent);
        xorPiece(hash, f + 3*dir, color);
        xorPiece(hash, f + 4*dir, opponent);
        break;

    case Move::push2:
        xorPiece(hash, f + 3*dir, opponent);
        xorPiece(hash, f + 3*dir, color);
        xorPiece(hash, f + 5*dir, opponent);
        break;

    case Move::out1with2:
        xorPiece(hash, f + 2*dir, opponent);
        xorPiece(hash, f + 2*dir, color);
        break;

    case Move::out1with3:
        xorPiece(hash, f + 3*dir, opponent);
        xorPiece(hash, f + 3*dir, color);
        break;

    // (c c c o o |) rear opponent at f+3dir is displaced by color, the front
    // one falls off the board, f+4dir does not change so it needs no delta
    case Move::out2:
        xorPiece(hash, f + 3*dir, opponent);
        xorPiece(hash, f + 3*dir, color);
        break;

    default:
        break;
    }

    hash ^= hashVariables[122];
    return hash;
}
