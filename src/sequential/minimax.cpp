// sequential minimax, no pruning, the baseline alpha/beta is measured against

#include "search.h"
#include "board.h"
#include "eval.h"
#include "movegen.h"
#include "counters.h"

#include <vector>
#include <algorithm>

class MinmaxSeqOracle: public SearchStrategy
{
 public:
    MinmaxSeqOracle(): SearchStrategy("Minmax Seq Oracle", 2) {}

    SearchStrategy* clone() { return new MinmaxSeqOracle(); }

 private:
    void searchBestMove();

    int minimaxSequential(Board& board, int depth);

    long _interior, _leaf;
};

MinmaxSeqOracle minmaxSeqOracle;


int MinmaxSeqOracle::minimaxSequential(Board& board, int depth)
{
    if (!depth) { _leaf++; return -_ev->calcEvaluation(&board); }

    auto moves = generateMovesBoard(board);
    _interior++;

    int best_eval = minEvaluation();
    int eval;

    for (auto& move: moves)
    {
        board.playMove(move);
        eval = -minimaxSequential(board, depth - 1);
        board.takeBack();

        if (eval <= best_eval) continue;

        best_eval = eval;
    }

    return best_eval;
}


void MinmaxSeqOracle::searchBestMove()
{
    if (!_ev->evalScheme()) _ev->setEvalScheme();

    Board& board = *_board;
    _interior = 0;
    _leaf = 0;

    auto moves = generateMovesBoard(board);
    if (moves.empty()) return;

    std::vector<int> scores(moves.size());
    std::vector<long> rootNodes(moves.size(), 0);

    for (std::size_t i = 0; i < moves.size(); i++)
    {
        long n0 = _interior + _leaf;

        board.playMove(moves[i]);
        scores[i] = -minimaxSequential(board, _maxDepth - 1);
        board.takeBack();

        rootNodes[i] = (_interior + _leaf) - n0;
    }

    auto it = std::max_element(scores.cbegin(), scores.cend());
    _bestMove = moves[std::distance(scores.cbegin(), it)];

    parStats.reset(1);
    parStats.interior[0] = _interior;
    parStats.leaf[0] = _leaf;
    parStats.rootNodes = rootNodes;

    foundBestMove(0, _bestMove, *it);
}
