// parallel alpha/beta, root splitting, one openmp task per root move
// only shared state is the root alpha, no table, no deepening, no ordering

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


class AbPar: public SearchStrategy
{
 public:
    AbPar(): SearchStrategy("AB Parallel", 4) {}

    SearchStrategy* clone() { return new AbPar(); }

 private:
    void searchBestMove();

    int abSequential(Board& board, Evaluator& ev, int depth,
                     int alpha, int beta, Slot& st);

    std::vector<Slot> _slot;

    alignas(64) std::atomic<int> _rootAlpha;
    std::vector<int> _lo;
};

AbPar abPar;


// plain alpha/beta below the root, board and ev belong to the calling task
int AbPar::abSequential(Board& board, Evaluator& ev, int depth,
                        int alpha, int beta, Slot& st)
{
    if (!depth) { st.leaf++; return -ev.calcEvaluation(&board); }

    auto moves = generateMovesBoard(board);
    st.interior++;

    int best_eval = minEvaluation();
    int eval;
    int idx = 0;
    bool cut = false;

    for (auto& move: moves)
    {
        board.playMove(move);
        eval = -abSequential(board, ev, depth - 1, -beta, -alpha, st);
        board.takeBack();

        best_eval = std::max(best_eval, eval);
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

    return best_eval;
}


void AbPar::searchBestMove()
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
