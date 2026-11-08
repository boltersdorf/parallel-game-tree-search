// measurement driver, one strategy on one position at fixed depth
// abtest <strategy> <position file|start> <depth> [repeats], threads from
// OMP_NUM_THREADS, no network and no referee, so a run is repeatable

#include "board.h"
#include "eval.h"
#include "search.h"
#include "counters.h"

#include <omp.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

class ValueGrabber: public SearchCallbacks
{
 public:
    ValueGrabber(): SearchCallbacks(0), value(0), got(false) {}

    void foundBestMove(int, const Move&, int v) { value = v; got = true; }

    int value;
    bool got;
};

static bool readPosition(const char* path, Board& b)
{
    if (strcmp(path, "start") == 0) {
        b.begin(Board::color1);
        return true;
    }

    FILE* f = fopen(path, "r");
    if (!f) { fprintf(stderr, "abtest: can not open %s\n", path); return false; }

    std::string s;
    char line[512];
    while (fgets(line, sizeof(line), f)) s += line;
    fclose(f);

    if (!b.setState(const_cast<char*>(s.c_str()))) {
        fprintf(stderr, "abtest: %s is not a valid position\n", path);
        return false;
    }
    return true;
}

int main(int argc, char* argv[])
{
    if (argc < 4) {
        fprintf(stderr,
                "usage: %s <strategy> <position file|start> <depth> [repeats]\n"
                "available strategies:\n", argv[0]);
        const char** s = SearchStrategy::strategies();
        for (int i = 0; s[i]; i++) fprintf(stderr, "  %s\n", s[i]);
        return 1;
    }

    // !!! likwid-perfctr re-quotes an argument which contains spaces, so under
    // likwid the strategy name arrives as "AB Par TT" with the quotes included
    // and no lookup can match it, underscores are accepted for that reason
    std::string name(argv[1]);
    for (char& c : name) if (c == '_') c = ' ';
    const char* stratName = name.c_str();
    const char* posPath   = argv[2];
    int depth   = atoi(argv[3]);
    int repeats = (argc > 4) ? atoi(argv[4]) : 1;

    SearchStrategy* strat = SearchStrategy::create(const_cast<char*>(stratName));
    if (!strat) {
        fprintf(stderr, "abtest: no strategy called '%s'\n", stratName);
        return 1;
    }

    Board board;
    if (!readPosition(posPath, board)) return 1;

    // !!! never call changeEvaluation here, it rotates the field values and
    // then two runs are not comparable any more
    Evaluator ev;
    ev.setEvalScheme();

    ValueGrabber grab;
    strat->setEvaluator(&ev);
    strat->setMaxDepth(depth);
    strat->registerCallbacks(&grab);

    int threads = omp_get_max_threads();

    for (int r = 0; r < repeats; r++) {
        Board work = board;

        double t0 = omp_get_wtime();
        Move& m = strat->bestMove(&work);
        double t = omp_get_wtime() - t0;

        if (!grab.got) {
            fprintf(stderr, "abtest: strategy reported no value\n");
            return 1;
        }

        // strategy,position,depth,threads,repeat,value,move,secs,interior,leaf,
        // ttcut,ceiling,cutoffs,searched
        printf("%s,%s,%d,%d,%d,%d,%s,%.6f,%ld,%ld,%ld,%.3f,%ld,%ld",
               stratName, posPath, depth, threads, r,
               grab.value, m.name(), t,
               parStats.totalInterior(), parStats.totalLeaf(),
               parStats.totalTTCut(), parStats.splitCeiling(),
               parStats.cutoffs, parStats.searched);

        // per thread interior:leaf:ttcut:secs, the three node categories the
        // load imbalance histogram needs, ttcut is 0 in versions without a table
        for (int i = 0; i < parStats.threads; i++)
            printf(",T%d=%ld:%ld:%ld:%.6f", i,
                   parStats.interior[i], parStats.leaf[i], parStats.ttcut[i],
                   parStats.secs[i]);

        printf("\n");

        printf("ROOTNODES,%s,%s,%d,%d,%d", stratName, posPath, depth, threads, r);
        for (std::size_t i = 0; i < parStats.rootNodes.size(); i++)
            printf(",%ld", parStats.rootNodes[i]);
        printf("\n");

        printf("CUTOFFAT,%s,%s,%d,%d,%d", stratName, posPath, depth, threads, r);
        for (std::size_t i = 0; i < parStats.cutoffAt.size(); i++)
            printf(",%ld", parStats.cutoffAt[i]);
        printf("\n");

        if (parStats.ttProbes) {
            printf("TTSTATS,%s,%s,%d,%d,%d,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld\n",
                   stratName, posPath, depth, threads, r,
                   parStats.ttProbes, parStats.ttHitsCut, parStats.ttHitsOrder,
                   parStats.ttMisses, parStats.ttStores,
                   parStats.ttExact, parStats.ttLower, parStats.ttUpper,
                   parStats.ttCrossThread);
        }

        if (parStats.aspInWindow || parStats.aspFailHigh || parStats.aspFailLow) {
            printf("ASPSTATS,%s,%s,%d,%d,%d,%ld,%ld,%ld,%ld\n",
                   stratName, posPath, depth, threads, r,
                   parStats.aspInWindow, parStats.aspFailHigh,
                   parStats.aspFailLow, parStats.aspResearchNodes);
        }

        fflush(stdout);
    }

    return 0;
}
