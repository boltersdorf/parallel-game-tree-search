// engine for real games: the full ladder plus a clock, a table kept for the
// whole game, a defensive evaluation and a short opening book

#include "search.h"
#include "board.h"
#include "eval.h"
#include "movegen.h"
#include "pick_best.h"
#include "zobrist.h"
#include "table.h"

#include <omp.h>
#include <vector>
#include <algorithm>
#include <atomic>
#include <chrono>

namespace { enum { MAX_PLY = 32 }; }

// remaining depth below which a node stays sequential
#ifndef YBW_MIN_DEPTH
#define YBW_MIN_DEPTH 2
#endif

#ifndef ASP_DELTA
#define ASP_DELTA 200
#endif

namespace {

enum {
    RESERVE_MS     = 500,
    MIN_BUDGET_MS  = 5,
    MAX_BUDGET_MS  = 150,
    CLOCK_SHARE    = 60,
    OPENING_MOVES  = 6,
    OPENING_CAP_MS = 100,
    BEHIND_PCT     = 80,
    NODE_CHECK     = 512,
    MAX_ID_DEPTH   = 40
};

// per thread, padded to a cache line so two threads never share one
struct alignas(64) ThreadSlot
{
    long nodeCounter = 0;
    Move killer[MAX_PLY][2];
};

struct BookEntry { int ply; int color; short field; unsigned char dir; Move::MoveType type; };

const BookEntry g_book[] = {
    { 0, Board::color1, 36, Move::Right, Move::right3 },
    { 2, Board::color1, 48, Move::Right, Move::right3 },
    { 4, Board::color1, 23, Move::Right, Move::right3 },
    { 1, Board::color2, 82, Move::Right, Move::left3  },
    { 3, Board::color2, 92, Move::Right, Move::left3  },
};

}


static std::atomic<int> g_history[Board::AllFields][8];

class AbParTimed: public SearchStrategy
{
 public:
    AbParTimed(): SearchStrategy("AB Par TT ID Asp Ord YBW Clock", 1) {}

    SearchStrategy* clone() { return new AbParTimed(); }

 private:
    void searchBestMove();
    int iterate(int depth, uint64_t rootKey, int alpha, int beta);
    Move _rootBest;
    int _rootDepth = 0;

    int abTT(Board& board, Evaluator& ev, int depth, int alpha, int beta,
             uint64_t key, ThreadSlot& st, Move* outBest = nullptr);

    int computeBudgetMs();
    bool outOfTime(ThreadSlot& st);
    bool bookMove(Move& out);

    std::vector<ThreadSlot> _slot;
    bool _ttReady = false;

    int  _lastMoveNo = -1;
    bool _bookAlive = true;

    std::atomic<bool> _abort{false};
    bool _timed = false;
    std::chrono::steady_clock::time_point _deadline;
};

AbParTimed abParTimed;


// reading the clock at every node costs more than a node, so look every
// NODE_CHECK nodes and tell the other threads through the flag
bool AbParTimed::outOfTime(ThreadSlot& st)
{
    if (_abort.load(std::memory_order_relaxed)) return true;
    if (!_timed) return false;
    if (++st.nodeCounter < NODE_CHECK) return false;
    st.nodeCounter = 0;
    if (std::chrono::steady_clock::now() >= _deadline) {
        _abort.store(true, std::memory_order_relaxed);
        return true;
    }
    return false;
}


int AbParTimed::abTT(Board& board, Evaluator& ev, int depth, int alpha, int beta,
                    uint64_t key, ThreadSlot& st, Move* outBest)
{
    if (!depth) return -ev.calcEvaluation(&board);

    if (outOfTime(st)) return 0;

    const int alpha0 = alpha;
    TTSlot* s = parTT.slot(key);

    uint64_t k = s->key.load(std::memory_order_relaxed);
    uint64_t d = s->data.load(std::memory_order_relaxed);

    Move ttMove;
    bool haveTTMove = false;
    int storedDepth = -1;

    if ((k ^ d) == key) {
        int tval, tdepth, ttid;
        TTBound tb;
        ttUnpack(d, tval, tdepth, tb, ttMove, ttid);
        storedDepth = tdepth;
        haveTTMove = (ttMove.type != Move::none);

        // no cutoff at the root, it would return a value without a move
        bool deepEnough = (tdepth >= depth) && !outBest;
        if (deepEnough) {
            if (tb == TTBound::EXACT) return tval;
            if (tb == TTBound::LOWER && tval >= beta) return tval;
            if (tb == TTBound::UPPER && tval <= alpha) return tval;
        }
    }

    auto moves = generateMovesBoard(board);
    if (moves.empty()) return minEvaluation();

    const int ply = _rootDepth - depth;
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
    bool cut = false;
    const std::size_t n = moves.size();

    // selection on demand, the whole list is never sorted
    auto pick = [&](std::size_t j) {
        std::size_t bi = j;
        for (std::size_t k = j + 1; k < n; k++)
            if (score[k] > score[bi]) bi = k;
        if (bi != j) { std::swap(moves[j], moves[bi]); std::swap(score[j], score[bi]); }
    };

    auto account = [&](const Move& m) {
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
        if (alpha >= beta) { account(m); cut = true; }
    }

    if (!cut && n > 1 && !_abort.load(std::memory_order_relaxed)) {
        if (depth <= YBW_MIN_DEPTH) {
            for (std::size_t j = 1; j < n; j++) {
                pick(j);
                Move& m = moves[j];
                uint64_t ck = ZobristHashtable::advanceHash(key, m, board.actColor());
                board.playMove(m);
                int e = -abTT(board, ev, depth - 1, -beta, -alpha, ck, st);
                board.takeBack();
                if (_abort.load(std::memory_order_relaxed)) break;
                if (e > best_eval) { best_eval = e; best_move = m; }
                alpha = std::max(alpha, e);
                if (alpha >= beta) { account(m); cut = true; break; }
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
                  if (!nodeCut.load(std::memory_order_relaxed) &&
                      !_abort.load(std::memory_order_relaxed)) {
                    ThreadSlot& ts = _slot[omp_get_thread_num()];
                    Board b = board;
                    Evaluator e = ev;
                    const Move& m = moves[j];
                    uint64_t ck = ZobristHashtable::advanceHash(key, m, b.actColor());
                    b.playMove(m);
                    // clamp, a sibling failing high would invert the next window
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
            int bi = pickBest(sc, lo);
            best_eval = sc[bi];
            best_move = moves[bi];
            alpha = std::max(alpha, best_eval);
            if (alpha >= beta) { account(best_move); cut = true; }
        }
    }

    if (outBest) *outBest = best_move;

    // an aborted node has an incomplete move loop, its value bounds nothing
    if (_abort.load(std::memory_order_relaxed)) return best_eval;

    if (depth >= storedDepth) {
        uint64_t nd = ttPack(best_eval, depth,
                             (best_eval >= beta)   ? TTBound::LOWER :
                             (best_eval <= alpha0) ? TTBound::UPPER : TTBound::EXACT,
                             best_move, omp_get_thread_num());
        s->data.store(nd, std::memory_order_relaxed);
        s->key.store(key ^ nd, std::memory_order_relaxed);
    }

    return best_eval;
}


int AbParTimed::computeBudgetMs()
{
    int me = _board->msecsToPlay(_board->actColor());
    int other = _board->msecsToPlay(_board->actColor() == Board::color1
                                    ? Board::color2 : Board::color1);

    if (me <= 0) return MAX_BUDGET_MS;

    if (_board->moveNo() >= 0 && _board->moveNo() < OPENING_MOVES)
        return std::min((int) OPENING_CAP_MS, std::max((int) MIN_BUDGET_MS, me - RESERVE_MS));

    int usable = me - RESERVE_MS;
    if (usable < MIN_BUDGET_MS) return MIN_BUDGET_MS;

    // a fixed share of what is left, so the clock can never reach zero
    long ms = usable / CLOCK_SHARE;

    // spend less while we hold under BEHIND_PCT of the opponent's clock
    if (other > 0 && (long) me * 100 < (long) other * BEHIND_PCT) ms /= 2;

    if (ms < MIN_BUDGET_MS) ms = MIN_BUDGET_MS;
    if (ms > MAX_BUDGET_MS) ms = MAX_BUDGET_MS;
    return (int) ms;
}


bool AbParTimed::bookMove(Move& out)
{
    if (!_bookAlive) return false;

    const int ply = _board->moveNo();
    const int col = _board->actColor();

    for (const BookEntry& b : g_book) {
        if (b.ply != ply || b.color != col) continue;

        auto moves = generateMovesBoard(*_board);
        for (const Move& m : moves)
            if (m.field == b.field && m.direction == b.dir && m.type == b.type) {
                out = m;
                return true;
            }

        // playMove asserts on a move the generator did not produce
        _bookAlive = false;
        return false;
    }
    return false;
}


int AbParTimed::iterate(int depth, uint64_t rootKey, int alpha, int beta)
{
    // killers by distance from this root, _maxDepth is 40 when no depth is given
    _rootDepth = depth;
    Board root = *_board;
    Evaluator ev = *_ev;
    Move best;
    int score = minEvaluation();

    #pragma omp parallel
    {
        #pragma omp single
        {
            ThreadSlot& st = _slot[omp_get_thread_num()];
            score = abTT(root, ev, depth, alpha, beta, rootKey, st, &best);
        }
    }

    // an aborted iteration is partial, keep the last depth which finished
    if (!_abort.load(std::memory_order_relaxed)) _rootBest = best;
    return score;
}


void AbParTimed::searchBestMove()
{
    if (!_ev->evalScheme()) _ev->setEvalScheme();

    // bestMove only runs on our turn, so this is the colour we play
    Evaluator::setDefensive(_board->actColor());

    if (!_ttReady) {
        ZobristHashtable::init();
        parTT.setSize(1ull << TT_BITS);
        _ttReady = true;
    }

    int nthreads = omp_get_max_threads();
    if ((int) _slot.size() != nthreads) _slot.assign(nthreads, ThreadSlot());

    const int moveNo = _board->moveNo();
    // a move number which did not advance means a new game
    if (moveNo < 0 || moveNo <= _lastMoveNo) {
        parTT.clear();
        for (int f = 0; f < Board::AllFields; f++)
            for (int d = 0; d < 8; d++)
                g_history[f][d].store(0, std::memory_order_relaxed);
        _slot.assign(nthreads, ThreadSlot());
        _bookAlive = true;
    }
    _lastMoveNo = moveNo;

    Move book;
    if (bookMove(book)) {
        _bestMove = book;
        foundBestMove(0, _bestMove, 0);
        return;
    }

    // player leaves the strength at 0 when started without a depth argument
    if (_maxDepth <= 0) _maxDepth = MAX_ID_DEPTH;

    const uint64_t rootKey = ZobristHashtable::getHash(*_board);

    const int budget = computeBudgetMs();
    _abort.store(false, std::memory_order_relaxed);
    _timed = false;
    _deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budget);

    int bestScore = minEvaluation();

    for (int d = 1; d <= _maxDepth; d++) {
        int alpha = minEvaluation(), beta = maxEvaluation();
        int delta = ASP_DELTA;
        if (d > 1) {
            alpha = std::max(minEvaluation(), bestScore - delta);
            beta  = std::min(maxEvaluation(), bestScore + delta);
        }

        for (;;) {
            int score = iterate(d, rootKey, alpha, beta);

            // the clock stops a re-search too, it has to leave both loops
            if (_abort.load(std::memory_order_relaxed)) break;

            if (score <= alpha && alpha > minEvaluation()) {
                beta  = (alpha + beta) / 2;
                alpha = std::max(minEvaluation(), score - delta);
                delta += delta;
                continue;
            }
            if (score >= beta && beta < maxEvaluation()) {
                beta = std::min(maxEvaluation(), score + delta);
                delta += delta;
                continue;
            }
            bestScore = score;
            break;
        }

        if (_abort.load(std::memory_order_relaxed)) break;

        // depth 1 runs off the clock, so a legal move always exists
        _timed = true;
    }

    _bestMove = _rootBest;
    foundBestMove(0, _bestMove, bestScore);
}
