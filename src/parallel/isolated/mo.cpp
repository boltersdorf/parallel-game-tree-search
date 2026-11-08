// root splitting with move ordering only, no table, no deepening
// no table move here, the ordering is killers, move type and history alone

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

namespace { enum { MAX_PLY = 32 }; }

static std::atomic<int> g_history_s1[Board::AllFields][8];


namespace {

struct alignas(64) Slot
{
    long interior = 0;
    long leaf = 0;
    double secs = 0.0;
    long cutoffs = 0;
    long searched = 0;
    long cutoffAt[CUTOFF_BUCKETS] = {0};
    Move killer[MAX_PLY][2];

};

}


class AbParOrd: public SearchStrategy
{
 public:
    AbParOrd(): SearchStrategy("AB Par Ord", 10) {}

    SearchStrategy* clone() { return new AbParOrd(); }

 private:
    void searchBestMove();

    int abSequential(Board& board, Evaluator& ev, int depth,
                     int alpha, int beta, Slot& st);

    std::vector<Slot> _slot;

    alignas(64) std::atomic<int> _rootAlpha;
    std::vector<int> _lo;
};

AbParOrd abParOrd;


// ordered alpha/beta below the root, board and ev belong to the calling task
int AbParOrd::abSequential(Board& board, Evaluator& ev, int depth,
                        int alpha, int beta, Slot& st)
{
    if (!depth) { st.leaf++; return -ev.calcEvaluation(&board); }

    auto moves = generateMovesBoard(board);
    st.interior++;
    const std::size_t n = moves.size();
    if (n == 0) return minEvaluation();

    const int ply = _maxDepth - depth;
    const Move* kill = (ply >= 0 && ply < MAX_PLY) ? st.killer[ply] : nullptr;

    std::vector<int> score(n);
    for (std::size_t i = 0; i < n; i++) {
        const Move& m = moves[i];
        if (kill && m.field == kill[0].field && m.direction == kill[0].direction &&
            m.type == kill[0].type)
            score[i] = (1 << 29) + 1;
        else if (kill && m.field == kill[1].field && m.direction == kill[1].direction &&
                 m.type == kill[1].type)
            score[i] = 1 << 29;
        else
            score[i] = (Move::typeCount - (int)m.type) * 100000
                     + g_history_s1[m.field][m.direction].load(std::memory_order_relaxed);
    }

    int best_eval = minEvaluation();
    int eval;
    bool cut = false;

    for (std::size_t j = 0; j < n; j++)
    {
        std::size_t bi = j;
        for (std::size_t k = j + 1; k < n; k++)
            if (score[k] > score[bi]) bi = k;
        if (bi != j) { std::swap(moves[j], moves[bi]); std::swap(score[j], score[bi]); }

        Move& move = moves[j];
        board.playMove(move);
        eval = -abSequential(board, ev, depth - 1, -beta, -alpha, st);
        board.takeBack();

        best_eval = std::max(best_eval, eval);
        alpha = std::max(alpha, eval);
        if (alpha >= beta) {
            st.cutoffs++;
            st.cutoffAt[j < CUTOFF_BUCKETS ? j : CUTOFF_BUCKETS - 1]++;
            if (kill && !(move.field == kill[0].field &&
                          move.direction == kill[0].direction &&
                          move.type == kill[0].type)) {
                st.killer[ply][1] = st.killer[ply][0];
                st.killer[ply][0] = move;
            }
            g_history_s1[move.field][move.direction]
                .fetch_add(depth * depth, std::memory_order_relaxed);
            cut = true;
            break;
        }
    }
    if (!cut) st.searched++;

    return best_eval;
}


void AbParOrd::searchBestMove()
{
    // !!! calcEvaluation builds the scheme when there is none, and that writes
    // the static field values, has to happen before the threads start
    if (!_ev->evalScheme()) _ev->setEvalScheme();

    for (int f = 0; f < Board::AllFields; f++)
        for (int d = 0; d < 8; d++)
            g_history_s1[f][d].store(0, std::memory_order_relaxed);

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
