// sequential alpha/beta, the reference every parallel version is checked against
// move generation from movegen.h, so the tree is the same as in the parallel ones

#include "search.h"
#include "board.h"
#include "eval.h"
#include "movegen.h"
#include "counters.h"

#include <vector>
#include <algorithm>

class AbSeqOracle: public SearchStrategy
{
 public:
    AbSeqOracle(): SearchStrategy("AB Seq Oracle", 3) {}

    SearchStrategy* clone() { return new AbSeqOracle(); }

 private:
    void searchBestMove();

    int abSequential(Board& board, int depth, int alpha, int beta);

    long _interior, _leaf;
    long _cutoffs, _searched;
    long _cutoffAt[CUTOFF_BUCKETS];
};

AbSeqOracle abSeqOracle;


int AbSeqOracle::abSequential(Board& board, int depth, int alpha, int beta)
{
    if (!depth) { _leaf++; return -_ev->calcEvaluation(&board); }

    auto moves = generateMovesBoard(board);
    _interior++;

    int best_eval = minEvaluation();
    int eval;
    int idx = 0;
    bool cut = false;

    for (auto& move: moves)
    {
        board.playMove(move);
        eval = -abSequential(board, depth - 1, -beta, -alpha);
        board.takeBack();

        best_eval = std::max(best_eval, eval);
        alpha = std::max(alpha, eval);
        if (alpha >= beta) {
            _cutoffs++;
            _cutoffAt[idx < CUTOFF_BUCKETS ? idx : CUTOFF_BUCKETS - 1]++;
            cut = true;
            break; // beta cutoff
        }
        idx++;
    }
    if (!cut) _searched++;

    return best_eval;
}


void AbSeqOracle::searchBestMove()
{
    if (!_ev->evalScheme()) _ev->setEvalScheme();

    Board& board = *_board;
    _interior = 0;
    _leaf = 0;
    _cutoffs = 0;
    _searched = 0;
    for (int b = 0; b < CUTOFF_BUCKETS; b++) _cutoffAt[b] = 0;

    auto moves = generateMovesBoard(board);
    if (moves.empty()) return;

    std::vector<int> scores(moves.size());
    std::vector<long> rootNodes(moves.size(), 0);

    int alpha = minEvaluation(), beta = maxEvaluation();

    for (std::size_t i = 0; i < moves.size(); i++)
    {
        long n0 = _interior + _leaf;

        board.playMove(moves[i]);
        scores[i] = -abSequential(board, _maxDepth - 1, -beta, -alpha);
        board.takeBack();

        rootNodes[i] = (_interior + _leaf) - n0;
        alpha = std::max(alpha, scores[i]);
    }

    auto it = std::max_element(scores.cbegin(), scores.cend());
    _bestMove = moves[std::distance(scores.cbegin(), it)];

    parStats.reset(1);
    parStats.interior[0] = _interior;
    parStats.leaf[0] = _leaf;
    parStats.cutoffs = _cutoffs;
    parStats.searched = _searched;
    for (int b = 0; b < CUTOFF_BUCKETS; b++) parStats.cutoffAt[b] = _cutoffAt[b];
    parStats.rootNodes = rootNodes;

    foundBestMove(0, _bestMove, *it);
}
