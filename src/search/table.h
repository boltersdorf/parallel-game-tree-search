// lockless transposition table, shared by all threads
// the key is xored against the whole packed word, so a torn pair fails the check
// the entry is 16 bytes, four per cache line and none straddling

#ifndef TABLE_H
#define TABLE_H

#include <cstdint>
#include <vector>
#include <atomic>
#include <omp.h>
#include "move.h"

#ifndef TT_BITS
#define TT_BITS 23
#endif

enum class TTBound : uint8_t { NONE = 0, EXACT = 1, LOWER = 2, UPPER = 3 };

// val 0..31, depth 32..39, bound 40..41, field 42..49, dir 50..53,
// type 54..57, writer thread 58..63
inline uint64_t ttPack(int val, int depth, TTBound b, const Move& m, int tid)
{
    return  (uint64_t)(uint32_t)val
         | ((uint64_t)(depth & 0xFF)        << 32)
         | ((uint64_t)((uint8_t)b & 0x3)    << 40)
         | ((uint64_t)(m.field & 0xFF)      << 42)
         | ((uint64_t)(m.direction & 0xF)   << 50)
         | ((uint64_t)((int)m.type & 0xF)   << 54)
         | ((uint64_t)(tid & 0x3F)          << 58);
}

inline void ttUnpack(uint64_t d, int& val, int& depth, TTBound& b, Move& m, int& tid)
{
    val   = (int)(uint32_t)(d & 0xFFFFFFFFull);
    depth = (int)((d >> 32) & 0xFF);
    b     = (TTBound)((d >> 40) & 0x3);
    m.field     = (short)((d >> 42) & 0xFF);
    m.direction = (unsigned char)((d >> 50) & 0xF);
    m.type      = (Move::MoveType)((d >> 54) & 0xF);
    tid   = (int)((d >> 58) & 0x3F);
}

struct TTSlot
{
    std::atomic<uint64_t> key{0};
    std::atomic<uint64_t> data{0};
};

class ParTT
{
 public:
    void setSize(uint64_t n) {
        uint64_t sz = 1;
        while (sz < n) sz <<= 1;
        _e = std::vector<TTSlot>(sz);
        _mask = sz - 1;
    }

    // measurement builds clear before every search, so repeats stay independent,
    // the engine only when a new game starts. parallel, one thread writing
    // 134 MB would land in the timed region
    void clear() {
        const long n = (long) _e.size();
        #pragma omp parallel for schedule(static)
        for (long i = 0; i < n; i++) {
            _e[i].key.store(0, std::memory_order_relaxed);
            _e[i].data.store(0, std::memory_order_relaxed);
        }
    }

    uint64_t entries() const { return _e.size(); }
    TTSlot* slot(uint64_t h) { return &_e[h & _mask]; }

 private:
    std::vector<TTSlot> _e;
    uint64_t _mask = 0;
};

extern ParTT parTT;

#endif
