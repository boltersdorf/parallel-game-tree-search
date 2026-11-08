// node counters, merged after the search and read by abtest

#ifndef COUNTERS_H
#define COUNTERS_H

#include <vector>

// cutoffs counted by the index of the move that caused them, index 0 means the
// first move tried already cut, which is what good ordering looks like
enum { CUTOFF_BUCKETS = 8 };

struct ParStats
{
    int threads = 0;
    std::vector<long> interior;   // inner nodes expanded, per thread
    std::vector<long> leaf;       // nodes given to the eval function, per thread
    std::vector<long> ttcut;      // nodes the table answered, per thread
    std::vector<double> secs;     // busy time per thread

    // nodes under each root move, the largest is the speedup ceiling of root
    // splitting, known before anything runs in parallel
    std::vector<long> rootNodes;

    std::vector<long> cutoffAt;
    long cutoffs = 0;             // nodes that cut at all
    long searched = 0;            // nodes that ran the whole move list

    // a hit deep enough to answer, against one only good for ordering
    long ttProbes = 0;
    long ttHitsCut = 0;
    long ttHitsOrder = 0;
    long ttMisses = 0;
    long ttStores = 0;
    long ttExact = 0, ttLower = 0, ttUpper = 0;
    long ttCrossThread = 0;       // entry was written by a different thread

    long aspFailHigh = 0, aspFailLow = 0, aspInWindow = 0;
    long aspResearchNodes = 0;

    long pvsNullWindow = 0, pvsResearch = 0;

    void reset(int n) {
        threads = n;
        interior.assign(n, 0);
        leaf.assign(n, 0);
        ttcut.assign(n, 0);
        secs.assign(n, 0.0);
        rootNodes.clear();
        cutoffAt.assign(CUTOFF_BUCKETS, 0);
        cutoffs = searched = 0;
        ttProbes = ttHitsCut = ttHitsOrder = ttMisses = ttStores = 0;
        ttExact = ttLower = ttUpper = ttCrossThread = 0;
        aspFailHigh = aspFailLow = aspInWindow = aspResearchNodes = 0;
        pvsNullWindow = pvsResearch = 0;
    }

    long totalInterior() const {
        long s = 0; for (long v: interior) s += v; return s;
    }
    long totalLeaf() const {
        long s = 0; for (long v: leaf) s += v; return s;
    }
    long totalTTCut() const {
        long s = 0; for (long v: ttcut) s += v; return s;
    }

    double splitCeiling() const {
        long sum = 0, mx = 0;
        for (long v: rootNodes) { sum += v; if (v > mx) mx = v; }
        return (mx > 0) ? (double) sum / (double) mx : 0.0;
    }
};

extern ParStats parStats;

#endif
