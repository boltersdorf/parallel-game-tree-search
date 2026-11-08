// best move after a split, at the root or at a young brothers node
// !!! a move searched in a window it failed low on returns only an upper bound,
// and that bound can equal the exact maximum, so on a tie the move whose score
// lies inside its window wins

#ifndef PICK_BEST_H
#define PICK_BEST_H

#include <vector>

// alpha[i] is the alpha move i was searched with
inline int pickBest(const std::vector<int>& score, const std::vector<int>& alpha)
{
    int best = 0;
    for (int i = 1; i < (int) score.size(); i++) {
        bool exact = score[i] > alpha[i];
        bool bestExact = score[best] > alpha[best];
        if (score[i] > score[best] || (score[i] == score[best] && exact && !bestExact))
            best = i;
    }
    return best;
}

#endif
