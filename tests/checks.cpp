// unit and soundness checks, run by run_tests.py
//   checks zobrist <games>      incremental key against a full rebuild, random games
//   checks table <pairs>        entry packing, torn entries
//   checks pick                 tie between a bound and an exact score
//   checks move <strategy> <position|start> <depth> <repeats>
//                               the returned move reaches the returned value
// built with -DTT_DEPTH_EQ, so the table versions search the fixed-depth tree

#include "board.h"
#include "eval.h"
#include "search.h"
#include "movegen.h"
#include "zobrist.h"
#include "table.h"
#include "pick_best.h"

#include <omp.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

class Grab: public SearchCallbacks
{
 public:
    Grab(): SearchCallbacks(0) {}
    void foundBestMove(int, const Move&, int v) { value = v; }
    int value = 0;
};

static bool readPosition(const char* path, Board& b)
{
    if (strcmp(path, "start") == 0) { b.begin(Board::color1); return true; }
    FILE* f = fopen(path, "r");
    if (!f) return false;
    std::string s;
    char line[512];
    while (fgets(line, sizeof(line), f)) s += line;
    fclose(f);
    return b.setState(const_cast<char*>(s.c_str()));
}

static int zobrist(int games)
{
    ZobristHashtable::init();
    std::mt19937 rng(12345);
    long moves = 0, bad = 0;
    long seen[Move::typeCount] = {};
    const char* starts[] = { "start", "positions/position-midgame1",
                             "positions/position-midgame2", "positions/position-endgame" };

    for (int g = 0; g < games; g++) {
        Board b;
        if (!readPosition(starts[g % 4], b)) { fprintf(stderr, "no position\n"); return 2; }
        uint64_t key = ZobristHashtable::getHash(b);

        for (int ply = 0; ply < 200 && b.isValid(); ply++) {
            auto ms = generateMovesBoard(b);
            if (ms.empty()) break;
            // out moves first half the time, so every type turns up
            Move m = ms[rng() % ms.size()];
            if (rng() % 2) for (const Move& x : ms) if (x.isOutMove()) { m = x; break; }

            key = ZobristHashtable::advanceHash(key, m, b.actColor());
            b.playMove(m);
            moves++;
            seen[m.type]++;
            if (key != ZobristHashtable::getHash(b)) bad++;
        }
    }

    int missing = 0;
    for (int t = 0; t < Move::typeCount; t++) if (!seen[t]) missing++;
    printf("zobrist: %ld moves, %ld keys differ from the rebuild, %d of %d move types not seen\n",
           moves, bad, missing, (int) Move::typeCount);
    return (bad || missing) ? 1 : 0;
}

static int table(int pairs)
{
    std::mt19937_64 rng(777);
    Board b;
    b.begin(Board::color1);
    auto ms = generateMovesBoard(b);
    long bad = 0;

    for (int i = 0; i < pairs; i++) {
        const Move& m = ms[rng() % ms.size()];
        int val = (int)(rng() % 40001) - 20000, depth = rng() % 64, tid = rng() % 48;
        TTBound bd = (TTBound)(1 + rng() % 3);
        int v2, d2, t2;
        TTBound b2;
        Move m2;
        ttUnpack(ttPack(val, depth, bd, m, tid), v2, d2, b2, m2, t2);
        if (v2 != val || d2 != depth || b2 != bd || t2 != tid ||
            m2.field != m.field || m2.direction != m.direction || m2.type != m.type) bad++;

        // two writers on one slot, the key of the first with the data of the second
        uint64_t kA = rng(), kB = rng();
        uint64_t dA = ttPack(val, depth, bd, m, 0), dB = ttPack(val + 1, depth, bd, m, 1);
        TTSlot s;
        s.key.store(kA ^ dA);
        s.data.store(dB);
        uint64_t k = s.key.load(), d = s.data.load();
        if ((k ^ d) == kA || (k ^ d) == kB) bad++;
        s.key.store(kB ^ dB);
        if ((s.key.load() ^ s.data.load()) != kB) bad++;
    }
    printf("table: %d entries, %ld wrong unpacks or accepted torn pairs\n", pairs, bad);
    return bad ? 1 : 0;
}

// score, alpha it was searched with, expected pick
static int pick()
{
    struct Case { std::vector<int> score, alpha; int want; };
    const Case cases[] = {
        { {5, 5},     {5, 3},     1 },  // first one failed low onto the max
        { {5, 5},     {3, 3},     0 },  // both exact, the first stays
        { {5, 5},     {3, 5},     0 },  // second failed low
        { {4, 5, 5},  {0, 5, 0},  2 },
        { {5, 7, 7},  {0, 0, 7},  1 },
        { {-9, -9},   {-9, -20},  1 },
    };
    int bad = 0;
    for (const Case& c : cases)
        if (pickBest(c.score, c.alpha) != c.want) bad++;
    printf("pick: %d of %d cases pick the wrong move\n", bad, (int) (sizeof cases / sizeof cases[0]));
    return bad ? 1 : 0;
}

static int move(const char* strategy, const char* pos, int depth, int reps)
{
    std::string name(strategy);
    for (char& c : name) if (c == '_') c = ' ';
    SearchStrategy* s = SearchStrategy::create(const_cast<char*>(name.c_str()));
    SearchStrategy* r = SearchStrategy::create(const_cast<char*>("Minmax Seq Oracle"));
    if (!s || !r) { fprintf(stderr, "no strategy %s\n", name.c_str()); return 2; }
    // create returns the registered instance, the reference needs its own
    r = r->clone();

    Board b;
    if (!readPosition(pos, b)) { fprintf(stderr, "no position %s\n", pos); return 2; }
    Evaluator ev;
    ev.setEvalScheme();
    Grab gs, gr;
    s->setEvaluator(&ev); s->setMaxDepth(depth);     s->registerCallbacks(&gs);
    r->setEvaluator(&ev); r->setMaxDepth(depth - 1); r->registerCallbacks(&gr);

    int bad = 0;
    for (int i = 0; i < reps; i++) {
        Board w = b;
        Move m = s->bestMove(&w);
        Board c = b;
        c.playMove(m);
        r->bestMove(&c);
        if (-gr.value != gs.value) {
            bad++;
            printf("  repeat %d: value %d, move %s reaches %d\n", i, gs.value, m.name(), -gr.value);
        }
    }
    printf("move: %d of %d repeats return a move which does not reach the value\n", bad, reps);
    return bad ? 1 : 0;
}

int main(int argc, char** argv)
{
    if (argc >= 3 && !strcmp(argv[1], "zobrist")) return zobrist(atoi(argv[2]));
    if (argc >= 3 && !strcmp(argv[1], "table"))   return table(atoi(argv[2]));
    if (argc >= 2 && !strcmp(argv[1], "pick"))    return pick();
    if (argc >= 6 && !strcmp(argv[1], "move"))
        return move(argv[2], argv[3], atoi(argv[4]), atoi(argv[5]));
    fprintf(stderr, "usage: %s zobrist <games> | table <pairs> | "
                    "pick | move <strategy> <position> <depth> <repeats>\n", argv[0]);
    return 2;
}
