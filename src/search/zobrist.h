#pragma once
#include <cstdint>
#include <vector>
#include "move.h"
#include "board.h"

// zobrist hashing, 122 piece keys and one side to move key
// advanceHash updates the key per move, no rebuild over 61 fields
struct ZobristHashtable {
    static std::vector<uint64_t> hashVariables;  // 122 piece keys + 1 side to move key
    static std::vector<int>      field2Ordermap; // field index to order index, or -1

    static void     init();
    static uint64_t getHash(const Board& board);
    static uint64_t advanceHash(uint64_t hash, const Move& move, int color);
};
