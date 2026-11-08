// table, deepening, window, ordering, and the young brothers wait concept,
// the root split is gone, the root is just the first node that splits
// -DTT_DEPTH_EQ cuts on equal depth only, that build matches the oracle

#include "search.h"
#include "board.h"
#include "eval.h"
#include "movegen.h"
#include "pick_best.h"
#include "counters.h"
#include "zobrist.h"
#include "table.h"

#include <omp.h>
#include <vector>
#include <algorithm>
#include <atomic>

namespace { enum { MAX_PLY = 32 }; }

// !!! measured at 48 threads, threshold 2 is 2.5x to 3x faster than 3,
// more threads need more split points
#ifndef YBW_MIN_DEPTH
#define YBW_MIN_DEPTH 2
#endif

#ifndef ASP_DELTA
#define ASP_DELTA 200
#endif

namespace {

struct alignas(64) YbwStat
{
    long interior = 0;
    long leaf = 0;
    long ttcut = 0;
    double secs = 0.0;
    long cutoffs = 0;
    long searched = 0;
    long cutoffAt[CUTOFF_BUCKETS] = {0};

    long probes = 0, hitsCut = 0, hitsOrder = 0, misses = 0, stores = 0;
    long exact = 0, lower = 0, upper = 0, cross = 0;
    Move killer[MAX_PLY][2];
};

}


static std::atomic<int> g_history[Board::AllFields][8];

class AbParYBW: public SearchStrategy
{
 public:
#ifdef TT_DEPTH_EQ
    AbParYBW(): SearchStrategy("AB Par TT ID Asp Ord YBW EqDepth", 9) {}
#else
    AbParYBW(): SearchStrategy("AB Par TT ID Asp Ord YBW", 9) {}
#endif

    SearchStrategy* clone() { return new AbParYBW(); }

 private:
    void searchBestMove();
    int iterate(int depth, uint64_t rootKey, int alpha, int beta);
    Move _rootBest;
    long nodesSoFar() const;

    int abTT(Board& board, Evaluator& ev, int depth, int alpha, int beta,
             uint64_t key, YbwStat& st, Move* outBest = nullptr);

    std::vector<YbwStat> _slot;
    bool _ttReady = false;

};

AbParYBW abParYBW;


int AbParYBW::abTT(Board& board, Evaluator& ev, int depth, int alpha, int beta,
                    uint64_t key, YbwStat& st, Move* outBest)
{
    if (!depth) { st.leaf++; return -ev.calcEvaluation(&board); }

    const int alpha0 = alpha;
    TTSlot* s = parTT.slot(key);

    uint64_t k = s->key.load(std::memory_order_relaxed);
    uint64_t d = s->data.load(std::memory_order_relaxed);
    st.probes++;

    Move ttMove;
    bool haveTTMove = false;
    int storedDepth = -1;

    if ((k ^ d) == key) {
        int tval, tdepth, ttid;
        TTBound tb;
        ttUnpack(d, tval, tdepth, tb, ttMove, ttid);
        storedDepth = tdepth;
        haveTTMove = (ttMove.type != Move::none);
        if (ttid != omp_get_thread_num()) st.cross++;

#ifdef TT_DEPTH_EQ
        bool deepEnough = (tdepth == depth);
#else
        bool deepEnough = (tdepth >= depth);
#endif
        if (deepEnough) {
            if (tb == TTBound::EXACT) { st.hitsCut++; st.exact++; st.ttcut++; return tval; }
            if (tb == TTBound::LOWER && tval >= beta) { st.hitsCut++; st.lower++; st.ttcut++; return tval; }
            if (tb == TTBound::UPPER && tval <= alpha) { st.hitsCut++; st.upper++; st.ttcut++; return tval; }
        }
        st.hitsOrder++;
    }
    else st.misses++;

    auto moves = generateMovesBoard(board);
    st.interior++;
    if (moves.empty()) return minEvaluation();

    const int ply = _maxDepth - depth;
    const Move* kill = (ply >= 0 && ply < MAX_PLY) ? st.killer[ply] : nullptr;

    std::vector<int> score(moves.size());
    for (std::size_t i = 0; i < moves.size(); i++) {
        const Move& m = moves[i];
        int v;
        if (haveTTMove && m.field == ttMove.field &&
            m.direction == ttMove.direction && m.type == ttMove.type)
            v = 1 << 30;
        else if (kill && m.field == kill[0].field &&
                 m.direction == kill[0].direction && m.type == kill[0].type)
            v = (1 << 29) + 1;
        else if (kill && m.field == kill[1].field &&
                 m.direction == kill[1].direction && m.type == kill[1].type)
            v = 1 << 29;
        else
            v = (Move::typeCount - (int)m.type) * 100000
              + g_history[m.field][m.direction].load(std::memory_order_relaxed);
        score[i] = v;
    }

    int best_eval = minEvaluation();
    Move best_move;
    int idx = 0;
    bool cut = false;
    const std::size_t n = moves.size();

    auto pick = [&](std::size_t j) {
        std::size_t bi = j;
        for (std::size_t k = j + 1; k < n; k++)
            if (score[k] > score[bi]) bi = k;
        if (bi != j) { std::swap(moves[j], moves[bi]); std::swap(score[j], score[bi]); }
    };

    auto account = [&](const Move& m, int at) {
        st.cutoffs++;
        st.cutoffAt[at < CUTOFF_BUCKETS ? at : CUTOFF_BUCKETS - 1]++;
        if (kill && !(m.field == kill[0].field && m.direction == kill[0].direction &&
                      m.type == kill[0].type)) {
            st.killer[ply][1] = st.killer[ply][0];
            st.killer[ply][0] = m;
        }
        g_history[m.field][m.direction].fetch_add(depth * depth, std::memory_order_relaxed);
    };

    // eldest brother alone, it decides the window every younger one gets
    pick(0);
    {
        Move& m = moves[0];
        uint64_t ck = ZobristHashtable::advanceHash(key, m, board.actColor());
        board.playMove(m);
        best_eval = -abTT(board, ev, depth - 1, -beta, -alpha, ck, st);
        board.takeBack();
        best_move = m;
        alpha = std::max(alpha, best_eval);
        if (alpha >= beta) { account(m, 0); cut = true; }
    }

    if (!cut && n > 1) {
        if (depth <= YBW_MIN_DEPTH) {
            for (std::size_t j = 1; j < n; j++) {
                pick(j);
                Move& m = moves[j];
                uint64_t ck = ZobristHashtable::advanceHash(key, m, board.actColor());
                board.playMove(m);
                int e = -abTT(board, ev, depth - 1, -beta, -alpha, ck, st);
                board.takeBack();
                if (e > best_eval) { best_eval = e; best_move = m; }
                alpha = std::max(alpha, e);
                idx = (int) j;
                if (alpha >= beta) { account(m, idx); cut = true; break; }
            }
        } else {
            for (std::size_t j = 1; j < n; j++) pick(j);
            std::vector<int> sc(n, minEvaluation());
            std::vector<int> lo(n, maxEvaluation());
            std::atomic<int> nodeAlpha(alpha);
            std::atomic<bool> nodeCut(false);

            for (std::size_t j = 1; j < n; j++) {
                #pragma omp task firstprivate(j) shared(moves, sc, lo, nodeAlpha, nodeCut, board, ev)
                {
                  if (!nodeCut.load(std::memory_order_relaxed)) {
                    YbwStat& ts = _slot[omp_get_thread_num()];
                    Board b = board;
                    Evaluator e = ev;
                    const Move& m = moves[j];
                    uint64_t ck = ZobristHashtable::advanceHash(key, m, b.actColor());
                    b.playMove(m);
                    // !!! clamp, a sibling that fails high pushes nodeAlpha past
                    // beta and the next one would get an inverted window
                    int a = nodeAlpha.load(std::memory_order_relaxed);
                    if (a >= beta) a = beta - 1;
                    lo[j] = a;
                    sc[j] = -abTT(b, e, depth - 1, -beta, -a, ck, ts);
                    int cur = nodeAlpha.load(std::memory_order_relaxed);
                    while (sc[j] > cur &&
                           !nodeAlpha.compare_exchange_weak(cur, sc[j],
                                                            std::memory_order_relaxed))
                        ;
                    if (sc[j] >= beta) nodeCut.store(true, std::memory_order_relaxed);
                  }
                }
            }
            #pragma omp taskwait

            // the eldest brother ran in the node's own window
            sc[0] = best_eval;
            lo[0] = alpha0;
            idx = pickBest(sc, lo);
            best_eval = sc[idx];
            best_move = moves[idx];
            alpha = std::max(alpha, best_eval);
            if (alpha >= beta) { account(best_move, idx); cut = true; }
        }
    }

    if (!cut) st.searched++;

    if (outBest) *outBest = best_move;

    if (depth >= storedDepth) {
        uint64_t nd = ttPack(best_eval, depth,
                             (best_eval >= beta)   ? TTBound::LOWER :
                             (best_eval <= alpha0) ? TTBound::UPPER : TTBound::EXACT,
                             best_move, omp_get_thread_num());
        s->data.store(nd, std::memory_order_relaxed);
        s->key.store(key ^ nd, std::memory_order_relaxed);
        st.stores++;
    }

    return best_eval;
}


long AbParYBW::nodesSoFar() const
{
    long s = 0;
    for (const auto& x : _slot) s += x.interior + x.leaf + x.ttcut;
    return s;
}


int AbParYBW::iterate(int depth, uint64_t rootKey, int alpha, int beta)
{
    Board root = *_board;
    Evaluator ev = *_ev;
    Move best;
    int score = minEvaluation();

    #pragma omp parallel
    {
        #pragma omp single
        {
            YbwStat& st = _slot[omp_get_thread_num()];
            score = abTT(root, ev, depth, alpha, beta, rootKey, st, &best);
        }
    }

    _rootBest = best;
    return score;
}


void AbParYBW::searchBestMove()
{
    if (!_ev->evalScheme()) _ev->setEvalScheme();

    if (!_ttReady) {
        ZobristHashtable::init();
        parTT.setSize(1ull << TT_BITS);
        _ttReady = true;
    }
    parTT.clear();

    for (int f = 0; f < Board::AllFields; f++)
        for (int d = 0; d < 8; d++)
            g_history[f][d].store(0, std::memory_order_relaxed);

    int nthreads = omp_get_max_threads();
    _slot.assign(nthreads, YbwStat());

    const uint64_t rootKey = ZobristHashtable::getHash(*_board);

    int bestScore = minEvaluation();
    long failHigh = 0, failLow = 0, inWindow = 0, researchNodes = 0;

    for (int d = 1; d <= _maxDepth; d++) {
        int alpha = minEvaluation(), beta = maxEvaluation();
        int delta = ASP_DELTA;
        if (d > 1) {
            alpha = std::max(minEvaluation(), bestScore - delta);
            beta  = std::min(maxEvaluation(), bestScore + delta);
        }

        for (int attempt = 0; ; attempt++) {
            long before = nodesSoFar();
            int score = iterate(d, rootKey, alpha, beta);
            if (attempt > 0) researchNodes += nodesSoFar() - before;

            if (score <= alpha && alpha > minEvaluation()) {
                failLow++;
                beta  = (alpha + beta) / 2;
                alpha = std::max(minEvaluation(), score - delta);
                delta += delta;
                continue;
            }
            if (score >= beta && beta < maxEvaluation()) {
                failHigh++;
                beta = std::min(maxEvaluation(), score + delta);
                delta += delta;
                continue;
            }
            inWindow++;
            bestScore = score;
            break;
        }
    }

    _bestMove = _rootBest;

    parStats.reset(nthreads);
    for (int t = 0; t < nthreads; t++) {
        parStats.interior[t] = _slot[t].interior;
        parStats.leaf[t]     = _slot[t].leaf;
        parStats.ttcut[t]    = _slot[t].ttcut;
        parStats.secs[t]     = _slot[t].secs;
        parStats.cutoffs    += _slot[t].cutoffs;
        parStats.searched   += _slot[t].searched;
        parStats.ttProbes   += _slot[t].probes;
        parStats.ttHitsCut  += _slot[t].hitsCut;
        parStats.ttHitsOrder+= _slot[t].hitsOrder;
        parStats.ttMisses   += _slot[t].misses;
        parStats.ttStores   += _slot[t].stores;
        parStats.ttExact    += _slot[t].exact;
        parStats.ttLower    += _slot[t].lower;
        parStats.ttUpper    += _slot[t].upper;
        parStats.ttCrossThread += _slot[t].cross;
        for (int b = 0; b < CUTOFF_BUCKETS; b++)
            parStats.cutoffAt[b] += _slot[t].cutoffAt[b];
    }
    parStats.aspFailHigh = failHigh;
    parStats.aspFailLow = failLow;
    parStats.aspInWindow = inWindow;
    parStats.aspResearchNodes = researchNodes;

    foundBestMove(0, _bestMove, bestScore);
}
