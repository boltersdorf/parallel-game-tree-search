// root splitting with the wait concept below the root, no table, no ordering
// !!! without a table the eldest brother is just the first generated move

#include "search.h"
#include "board.h"
#include "eval.h"
#include "movegen.h"
#include "pick_best.h"
#include "counters.h"

#include <omp.h>
#include <vector>
#include <algorithm>
#include <atomic>

// !!! measured at 48 threads, threshold 2 is 2.5x to 3x faster than 3,
// more threads need more split points
#ifndef YBW_MIN_DEPTH
#define YBW_MIN_DEPTH 2
#endif


namespace {

struct alignas(64) Slot
{
    long interior = 0;
    long leaf = 0;
    double secs = 0.0;
    long cutoffs = 0;
    long searched = 0;
    long cutoffAt[CUTOFF_BUCKETS] = {0};

};

}


class AbParYbwOnly: public SearchStrategy
{
 public:
    AbParYbwOnly(): SearchStrategy("AB Par YBW", 11) {}

    SearchStrategy* clone() { return new AbParYbwOnly(); }

 private:
    void searchBestMove();

    int abSequential(Board& board, Evaluator& ev, int depth,
                     int alpha, int beta, Slot& st);

    std::vector<Slot> _slot;

    alignas(64) std::atomic<int> _rootAlpha;
    std::vector<int> _lo;
};

AbParYbwOnly abParYbwOnly;


// alpha/beta with the wait concept below the root, board and ev belong to the
// calling task
int AbParYbwOnly::abSequential(Board& board, Evaluator& ev, int depth,
                        int alpha, int beta, Slot& st)
{
    if (!depth) { st.leaf++; return -ev.calcEvaluation(&board); }

    auto moves = generateMovesBoard(board);
    st.interior++;
    const std::size_t n = moves.size();
    if (n == 0) return minEvaluation();

    int best_eval = minEvaluation();
    bool cut = false;
    int idx = 0;

    board.playMove(moves[0]);
    best_eval = -abSequential(board, ev, depth - 1, -beta, -alpha, st);
    board.takeBack();
    alpha = std::max(alpha, best_eval);
    if (alpha >= beta) { st.cutoffs++; st.cutoffAt[0]++; cut = true; }

    if (!cut && n > 1) {
        if (depth <= YBW_MIN_DEPTH) {
            for (std::size_t j = 1; j < n; j++) {
                board.playMove(moves[j]);
                int e = -abSequential(board, ev, depth - 1, -beta, -alpha, st);
                board.takeBack();
                best_eval = std::max(best_eval, e);
                alpha = std::max(alpha, e);
                idx = (int) j;
                if (alpha >= beta) {
                    st.cutoffs++;
                    st.cutoffAt[idx < CUTOFF_BUCKETS ? idx : CUTOFF_BUCKETS - 1]++;
                    cut = true; break;
                }
            }
        } else {
            std::vector<int> sc(n, minEvaluation());
            std::atomic<int> nodeAlpha(alpha);
            std::atomic<bool> nodeCut(false);

            for (std::size_t j = 1; j < n; j++) {
                #pragma omp task firstprivate(j) shared(moves, sc, nodeAlpha, nodeCut, board, ev)
                {
                  if (!nodeCut.load(std::memory_order_relaxed)) {
                    Slot& ts = _slot[omp_get_thread_num()];
                    Board b = board;
                    Evaluator e = ev;
                    b.playMove(moves[j]);
                    int a = nodeAlpha.load(std::memory_order_relaxed);
                    if (a >= beta) a = beta - 1;
                    sc[j] = -abSequential(b, e, depth - 1, -beta, -a, ts);
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

            for (std::size_t j = 1; j < n; j++)
                if (sc[j] > best_eval) { best_eval = sc[j]; idx = (int) j; }
            alpha = std::max(alpha, best_eval);
            if (alpha >= beta) {
                st.cutoffs++;
                st.cutoffAt[idx < CUTOFF_BUCKETS ? idx : CUTOFF_BUCKETS - 1]++;
                cut = true;
            }
        }
    }
    if (!cut) st.searched++;

    return best_eval;
}


void AbParYbwOnly::searchBestMove()
{
    // !!! calcEvaluation builds the scheme when there is none, and that writes
    // the static field values, has to happen before the threads start
    if (!_ev->evalScheme()) _ev->setEvalScheme();

    int nthreads = omp_get_max_threads();
    _slot.assign(nthreads, Slot());

    auto moves = generateMovesBoard(*_board);
    int n = (int) moves.size();
    if (n == 0) return;

    std::vector<int> scores(n, minEvaluation());
    std::vector<long> rootNodes(n, 0);

    _rootAlpha.store(minEvaluation(), std::memory_order_relaxed);

    _lo.assign(n, 0);
    const int rootBeta = maxEvaluation();

    #pragma omp parallel
    {
        #pragma omp single
        {
            for (int i = 0; i < n; i++)
            {
                #pragma omp task firstprivate(i) shared(moves, scores, rootNodes)
                {
                    Slot& st = _slot[omp_get_thread_num()];
                    double t0 = omp_get_wtime();
                    long n0 = st.interior + st.leaf;

                    Board board = *_board;
                    Evaluator ev = *_ev;

                    board.playMove(moves[i]);

                    int a = _rootAlpha.load(std::memory_order_relaxed);
                    if (a >= rootBeta) a = rootBeta - 1;
                    _lo[i] = a;
                    scores[i] = -abSequential(board, ev, _maxDepth - 1,
                                              -rootBeta, -a, st);

                    int cur = _rootAlpha.load(std::memory_order_relaxed);
                    while (scores[i] > cur &&
                           !_rootAlpha.compare_exchange_weak(
                               cur, scores[i], std::memory_order_relaxed))
                        ;

                    rootNodes[i] = (st.interior + st.leaf) - n0;
                    st.secs += omp_get_wtime() - t0;
                }
            }
        }
    }

    auto it = scores.cbegin() + pickBest(scores, _lo);
    _bestMove = moves[std::distance(scores.cbegin(), it)];

    parStats.reset(nthreads);
    for (int t = 0; t < nthreads; t++) {
        parStats.interior[t] = _slot[t].interior;
        parStats.leaf[t]     = _slot[t].leaf;
        parStats.secs[t]     = _slot[t].secs;
        parStats.cutoffs    += _slot[t].cutoffs;
        parStats.searched   += _slot[t].searched;
        for (int b = 0; b < CUTOFF_BUCKETS; b++)
            parStats.cutoffAt[b] += _slot[t].cutoffAt[b];
    }
    parStats.rootNodes = rootNodes;

    foundBestMove(0, _bestMove, *it);
}
