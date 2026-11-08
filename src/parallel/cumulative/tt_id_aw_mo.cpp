// root splitting, shared table, deepening, window, plus move ordering:
// table move, 2 killers per ply, move type, history
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

#ifndef ASP_DELTA
#define ASP_DELTA 200
#endif

namespace {

struct alignas(64) OrdStat
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

class AbParTTIDAspOrd: public SearchStrategy
{
 public:
#ifdef TT_DEPTH_EQ
    AbParTTIDAspOrd(): SearchStrategy("AB Par TT ID Asp Ord EqDepth", 8) {}
#else
    AbParTTIDAspOrd(): SearchStrategy("AB Par TT ID Asp Ord", 8) {}
#endif

    SearchStrategy* clone() { return new AbParTTIDAspOrd(); }

 private:
    void searchBestMove();
    int iterate(std::vector<Move>& moves, std::vector<int>& scores,
                std::vector<long>& rootNodes, int depth,
                uint64_t rootKey, int rootColor, int alpha, int beta);
    long nodesSoFar() const;

    int abTT(Board& board, Evaluator& ev, int depth, int alpha, int beta,
             uint64_t key, OrdStat& st);

    std::vector<OrdStat> _slot;
    bool _ttReady = false;

    alignas(64) std::atomic<int> _rootAlpha;
    std::vector<int> _lo;
};

AbParTTIDAspOrd abParTTIDAspOrd;


int AbParTTIDAspOrd::abTT(Board& board, Evaluator& ev, int depth, int alpha, int beta,
                    uint64_t key, OrdStat& st)
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

    for (std::size_t j = 0; j < moves.size(); j++)
    {
        std::size_t bi = j;
        for (std::size_t k = j + 1; k < moves.size(); k++)
            if (score[k] > score[bi]) bi = k;
        if (bi != j) { std::swap(moves[j], moves[bi]); std::swap(score[j], score[bi]); }
        Move& move = moves[j];
        uint64_t ck = ZobristHashtable::advanceHash(key, move, board.actColor());
        board.playMove(move);
        int eval = -abTT(board, ev, depth - 1, -beta, -alpha, ck, st);
        board.takeBack();

        if (eval > best_eval) { best_eval = eval; best_move = move; }
        alpha = std::max(alpha, eval);
        if (alpha >= beta) {
            st.cutoffs++;
            st.cutoffAt[idx < CUTOFF_BUCKETS ? idx : CUTOFF_BUCKETS - 1]++;
            if (kill && !(move.field == kill[0].field &&
                          move.direction == kill[0].direction &&
                          move.type == kill[0].type)) {
                st.killer[ply][1] = st.killer[ply][0];
                st.killer[ply][0] = move;
            }
            g_history[move.field][move.direction]
                .fetch_add(depth * depth, std::memory_order_relaxed);
            cut = true;
            break;
        }
        idx++;
    }
    if (!cut) st.searched++;

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


long AbParTTIDAspOrd::nodesSoFar() const
{
    long s = 0;
    for (const auto& x : _slot) s += x.interior + x.leaf + x.ttcut;
    return s;
}


int AbParTTIDAspOrd::iterate(std::vector<Move>& moves, std::vector<int>& scores,
                       std::vector<long>& rootNodes, int depth,
                       uint64_t rootKey, int rootColor, int alpha, int beta)
{
    const int n = (int) moves.size();
    _rootAlpha.store(alpha, std::memory_order_relaxed);
    _lo.assign(n, 0);
    const int rootBeta = beta;

    #pragma omp parallel
    {
        #pragma omp single
        {
            for (int i = 0; i < n; i++)
            {
                #pragma omp task firstprivate(i) shared(moves, scores, rootNodes)
                {
                    OrdStat& st = _slot[omp_get_thread_num()];
                    double t0 = omp_get_wtime();
                    long n0 = st.interior + st.leaf + st.ttcut;

                    Board board = *_board;
                    Evaluator ev = *_ev;

                    uint64_t ck = ZobristHashtable::advanceHash(rootKey, moves[i], rootColor);
                    board.playMove(moves[i]);

                    int a = _rootAlpha.load(std::memory_order_relaxed);
                    if (a >= rootBeta) a = rootBeta - 1;
                    _lo[i] = a;
                    scores[i] = -abTT(board, ev, depth - 1, -rootBeta, -a, ck, st);

                    int cur = _rootAlpha.load(std::memory_order_relaxed);
                    while (scores[i] > cur &&
                           !_rootAlpha.compare_exchange_weak(
                               cur, scores[i], std::memory_order_relaxed))
                        ;

                    rootNodes[i] = (st.interior + st.leaf + st.ttcut) - n0;
                    st.secs += omp_get_wtime() - t0;
                }
            }
        }
    }

    auto it = scores.cbegin() + pickBest(scores, _lo);
    return (int) std::distance(scores.cbegin(), it);
}


void AbParTTIDAspOrd::searchBestMove()
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
    _slot.assign(nthreads, OrdStat());

    auto moves = generateMovesBoard(*_board);
    int n = (int) moves.size();
    if (n == 0) return;

    std::vector<int> scores(n, minEvaluation());
    std::vector<long> rootNodes(n, 0);

    const uint64_t rootKey = ZobristHashtable::getHash(*_board);
    const int rootColor = _board->actColor();

    int best = 0;
    int bestScore = minEvaluation();
    long failHigh = 0, failLow = 0, inWindow = 0, researchNodes = 0;

    for (int d = 1; d <= _maxDepth; d++) {
        if (best != 0) { std::swap(moves[0], moves[best]); }

        int alpha = minEvaluation(), beta = maxEvaluation();
        int delta = ASP_DELTA;
        if (d > 1) {
            alpha = std::max(minEvaluation(), bestScore - delta);
            beta  = std::min(maxEvaluation(), bestScore + delta);
        }

        for (int attempt = 0; ; attempt++) {
            long before = nodesSoFar();
            best = iterate(moves, scores, rootNodes, d, rootKey, rootColor,
                           alpha, beta);
            int score = scores[best];
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

    _bestMove = moves[best];

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
    parStats.rootNodes = rootNodes;
    parStats.aspFailHigh = failHigh;
    parStats.aspFailLow = failLow;
    parStats.aspInWindow = inWindow;
    parStats.aspResearchNodes = researchNodes;

    foundBestMove(0, _bestMove, bestScore);
}
