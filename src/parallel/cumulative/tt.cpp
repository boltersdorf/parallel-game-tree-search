// root splitting plus a shared lockless transposition table
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


namespace {

struct alignas(64) TTSlotStat
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
};

}


class AbParTTNew: public SearchStrategy
{
 public:
#ifdef TT_DEPTH_EQ
    AbParTTNew(): SearchStrategy("AB Par TT EqDepth", 5) {}
#else
    AbParTTNew(): SearchStrategy("AB Par TT", 5) {}
#endif

    SearchStrategy* clone() { return new AbParTTNew(); }

 private:
    void searchBestMove();

    int abTT(Board& board, Evaluator& ev, int depth, int alpha, int beta,
             uint64_t key, TTSlotStat& st);

    void rootSplit(Board& root, std::vector<Move>& moves, std::vector<int>& scores,
                   std::vector<long>* rootNodes, int depth,
                   uint64_t rootKey, int rootColor, std::vector<TTSlotStat>& stats);

    std::vector<TTSlotStat> _slot;
    bool _ttReady = false;


    alignas(64) std::atomic<int> _rootAlpha;
    std::vector<int> _lo;
};

AbParTTNew abParTTNew;


// !!! take this before playMove, actColor flips there
static inline uint64_t childKey(uint64_t key, const Board& board, const Move& m)
{
    return ZobristHashtable::advanceHash(key, m, board.actColor());
}


int AbParTTNew::abTT(Board& board, Evaluator& ev, int depth, int alpha, int beta,
                     uint64_t key, TTSlotStat& st)
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

    if (haveTTMove) {
        for (std::size_t i = 1; i < moves.size(); i++) {
            if (moves[i].field == ttMove.field &&
                moves[i].direction == ttMove.direction &&
                moves[i].type == ttMove.type) {
                std::swap(moves[0], moves[i]);
                break;
            }
        }
    }

    int best_eval = minEvaluation();
    Move best_move;
    int idx = 0;
    bool cut = false;

    for (auto& move: moves)
    {
        uint64_t ck = childKey(key, board, move);
        board.playMove(move);
        int eval = -abTT(board, ev, depth - 1, -beta, -alpha, ck, st);
        board.takeBack();

        if (eval > best_eval) { best_eval = eval; best_move = move; }
        alpha = std::max(alpha, eval);
        if (alpha >= beta) {
            st.cutoffs++;
            st.cutoffAt[idx < CUTOFF_BUCKETS ? idx : CUTOFF_BUCKETS - 1]++;
            cut = true;
            break;
        }
        idx++;
    }
    if (!cut) st.searched++;

    // !!! only store after the loop or a cutoff, a bound from one move is not one
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


void AbParTTNew::rootSplit(Board& root, std::vector<Move>& moves,
                           std::vector<int>& scores, std::vector<long>* rootNodes,
                           int depth, uint64_t rootKey, int rootColor,
                           std::vector<TTSlotStat>& stats)
{
    const int n = (int) moves.size();
    _rootAlpha.store(minEvaluation(), std::memory_order_relaxed);
    _lo.assign(n, 0);
    const int rootBeta = maxEvaluation();

    #pragma omp parallel
    {
        #pragma omp single
        {
            for (int i = 0; i < n; i++)
            {
                #pragma omp task firstprivate(i) shared(moves, scores, rootNodes, stats)
                {
                    TTSlotStat& st = stats[omp_get_thread_num()];
                    double t0 = omp_get_wtime();
                    long n0 = st.interior + st.leaf + st.ttcut;

                    Board board = root;
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

                    if (rootNodes) (*rootNodes)[i] = (st.interior + st.leaf + st.ttcut) - n0;
                    st.secs += omp_get_wtime() - t0;
                }
            }
        }
    }
}




void AbParTTNew::searchBestMove()
{
    if (!_ev->evalScheme()) _ev->setEvalScheme();

    if (!_ttReady) {
        ZobristHashtable::init();
        parTT.setSize(1ull << TT_BITS);
        _ttReady = true;
    }
    // !!! clearing makes repeats independent for measurement, in a game it would
    // delete exactly what the ponder thread built
    parTT.clear();

    int nthreads = omp_get_max_threads();
    _slot.assign(nthreads, TTSlotStat());

    auto moves = generateMovesBoard(*_board);
    int n = (int) moves.size();
    if (n == 0) return;

    std::vector<int> scores(n, minEvaluation());
    std::vector<long> rootNodes(n, 0);

    const uint64_t rootKey = ZobristHashtable::getHash(*_board);
    const int rootColor = _board->actColor();

    rootSplit(*_board, moves, scores, &rootNodes, _maxDepth, rootKey, rootColor, _slot);

    auto it = scores.cbegin() + pickBest(scores, _lo);
    _bestMove = moves[std::distance(scores.cbegin(), it)];

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

    foundBestMove(0, _bestMove, *it);

}
