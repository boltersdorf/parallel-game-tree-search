#!/usr/bin/env python3
"""
every number the top level README quotes, recomputed from the raw logs

medians over repeats everywhere, one thread runs are deterministic and have one
"""

import re
import statistics as st
from collections import defaultdict

from parse_results import rows, rootnodes, AN

R = AN / "results"
F = R / "F_ladder_20260831-015527_j5819721_i01r03c05s08.out"
G = R / "G_threads_20260831-020317_j5819721_i01r03c05s08.out"
H = R / "H_sweeps_20260831-013430_j5819681_i02r04c06s01.out"
A2 = R / "sA2A3.out"
D = sorted(R.glob("D_counters_*.out"))
C = R / "C_races_20260830-233133_j5819573_i01r05c06s11.out"

POS = ["start", "position-midgame1", "position-midgame2", "position-endgame"]
SHORT = {"start": "start", "position-midgame1": "mid1",
         "position-midgame2": "mid2", "position-endgame": "end"}
TOP = "AB Par TT ID Asp Ord YBW"


def med(rs, field, **want):
    v = [r[field] for r in rs if all(r[k] == x for k, x in want.items())]
    return st.median(v) if v else None


def extra(path, tag):
    """CUTOFFAT, TTSTATS or ASPSTATS lines keyed by strategy, position, depth, threads"""
    out = defaultdict(list)
    for line in open(path):
        if line.startswith(tag + ","):
            f = line.rstrip("\n").split(",")
            out[(f[1], f[2], int(f[3]), int(f[4]))].append([int(x) for x in f[6:]])
    return out


def section(t):
    print(f"\n== {t}")


def alpha_beta_vs_minimax():
    section("alpha-beta against minimax, one core, depth 4 (sA2A3, job 5819077)")
    leaves, secs, vals = {}, {}, {}
    for line in open(A2):
        f = line.split(",")
        if len(f) > 10 and f[0] in ("Minmax Seq Oracle", "AB Seq Oracle") and f[2] == "4":
            leaves[f[0], f[1]] = int(f[9])
            secs[f[0], f[1]] = float(f[7])
            vals[f[0], f[1]] = int(f[5])
    for p in POS:
        print(f"  {SHORT[p]:5s} leaf reduction {leaves['Minmax Seq Oracle', p] / leaves['AB Seq Oracle', p]:5.2f}x"
              f"  time {secs['Minmax Seq Oracle', p] / secs['AB Seq Oracle', p]:5.2f}x"
              f"  same value {vals['Minmax Seq Oracle', p] == vals['AB Seq Oracle', p]}")


def first_move_cutoffs(rs, cut):
    section("share of cutoffs on the first move, depth 5, one thread (F)")
    for s in ["AB Seq Oracle", "AB Par TT", "AB Par TT ID", "AB Par TT ID Asp", "AB Par TT ID Asp Ord"]:
        v = [100 * c[0][0] / sum(c[0]) for p in POS for c in [cut[(s, p, 5, 1)]] if c]
        print(f"  {s:26s} " + "  ".join(f"{x:5.1f}" for x in v))


def ladder(rs):
    section("48 threads, depth 5 (F): wall time of AB Parallel over each version")
    for s in ["AB Par TT", "AB Par TT ID", "AB Par TT ID Asp", "AB Par TT ID Asp Ord", TOP,
              "AB Par Ord", "AB Par YBW"]:
        v = [med(rs, "secs", strategy="AB Parallel", pos=p, depth=5, threads=48) /
             med(rs, "secs", strategy=s, pos=p, depth=5, threads=48) for p in POS]
        print(f"  {s:26s} " + "  ".join(f"{x:7.1f}x" for x in v))
    section("48 threads against one thread of the same version, depth 5 (F)")
    for s in ["AB Parallel", "AB Par TT", "AB Par TT ID", "AB Par TT ID Asp Ord", TOP, "AB Par YBW"]:
        v = [med(rs, "secs", strategy=s, pos=p, depth=5, threads=1) /
             med(rs, "secs", strategy=s, pos=p, depth=5, threads=48) for p in POS]
        print(f"  {s:26s} " + "  ".join(f"{x:6.1f}x" for x in v))
    section("node visits [10^6] at 48 threads, and the one thread tree (F)")
    for s in ["AB Seq Oracle", "AB Parallel", "AB Par TT ID Asp Ord", TOP, "AB Par Ord", "AB Par YBW"]:
        t = 1 if s == "AB Seq Oracle" else 48
        v = [med(rs, "nodes", strategy=s, pos=p, depth=5, threads=t) / 1e6 for p in POS]
        print(f"  {s:26s} t{t:<3d}" + "  ".join(f"{x:7.3f}" for x in v))
    s = "AB Par YBW"
    n1 = med(rs, "nodes", strategy=s, pos="start", depth=5, threads=1)
    n48 = med(rs, "nodes", strategy=s, pos="start", depth=5, threads=48)
    print(f"  YBWC alone, start: search overhead {100 * (n48 / n1 - 1):.0f} %")


def imbalance(rs):
    section("busiest thread over the average of all 48, depth 5, median of repeats (F)")
    for s in ["AB Parallel", "AB Par TT ID Asp Ord", TOP]:
        v = []
        for p in POS:
            q = []
            for r in rs:
                if r["strategy"] == s and r["pos"] == p and r["depth"] == 5 and r["threads"] == 48:
                    w = [x["interior"] + x["leaf"] + (x["ttcut"] or 0) for x in r["per"]]
                    q.append(max(w) / (sum(w) / len(w)))
            v.append(st.median(q))
        print(f"  {s:26s} " + "  ".join(f"{x:5.1f}" for x in v))
    rn = rootnodes(F)
    print("  root moves: " + "  ".join(
        f"{SHORT[p]} {len(rn[('AB Seq Oracle', p, 5, 1, 0)])}" for p in POS))


def root_bound():
    section("root splitting: total over the largest root subtree, and the speedup (G)")
    rs = rows(G)
    rn = rootnodes(G)
    for p in ["position-midgame1", "position-endgame"]:
        b = []
        for (s, pos, d, t, rep), v in rn.items():
            if s == "AB Parallel" and pos == p and d == 5 and t == 48 and v:
                b.append(sum(v) / max(v))
        b1 = [sum(v) / max(v) for (s, pos, d, t, rep), v in rn.items()
              if s == "AB Parallel" and pos == p and d == 5 and t == 1 and v]
        sp = {s: med(rs, "secs", strategy=s, pos=p, depth=5, threads=1) /
              med(rs, "secs", strategy=s, pos=p, depth=5, threads=48)
              for s in ["AB Parallel", "AB Par TT ID Asp Ord", TOP, "AB Par YBW"]}
        print(f"  {SHORT[p]}: bound at 48 threads {st.median(b):.1f}, at one thread "
              f"{st.median(b1):.1f}; speedup " +
              ", ".join(f"{s} {x:.1f}x" for s, x in sp.items()))
    print("  factor over AB Parallel at the same thread count:")
    for p in ["position-midgame1", "position-endgame"]:
        for s in ["AB Par TT ID Asp Ord", TOP]:
            v = [med(rs, "secs", strategy="AB Parallel", pos=p, depth=5, threads=t) /
                 med(rs, "secs", strategy=s, pos=p, depth=5, threads=t)
                 for t in (1, 2, 4, 8, 16, 24, 32, 48)]
            print(f"    {SHORT[p]} {s:26s} " + " ".join(f"{x:6.1f}" for x in v))
    section("placement at 24 threads, close over spread wall time (G)")
    for line_s in ["AB Parallel", "AB Par TT ID Asp Ord", TOP, "AB Par YBW"]:
        v = []
        for p in ["position-midgame1", "position-endgame"]:
            c = med(rs, "secs", strategy=line_s, pos=p, depth=5, threads=24, bind="BIND_close")
            sp = med(rs, "secs", strategy=line_s, pos=p, depth=5, threads=24, bind="BIND_spread")
            v.append(sp / c)
        print(f"  {line_s:26s} spread over close " + "  ".join(f"{x:.2f}" for x in v))


def table(rs, tt):
    section("table hits written by another thread, midgame 1, 48 threads (F)")
    for s in ["AB Par TT", "AB Par TT ID", "AB Par TT ID Asp", "AB Par TT ID Asp Ord", TOP]:
        q = [100 * x[8] / (x[1] + x[2]) for x in tt[(s, "position-midgame1", 5, 48)]]
        print(f"  {s:26s} {st.median(q):5.1f} %")
    hrows = []
    for line in open(H):
        m = re.match(r"^(YBWMIN|TTBITS|DELTA)_(\d+),(.*)$", line.rstrip("\n"))
        if m:
            f = m.group(3).split(",")
            if len(f) >= 14 and not f[0].isupper():
                hrows.append((m.group(1), int(m.group(2)), f[0], f[1], float(f[7]),
                              int(f[8]) + int(f[9]) + int(f[10])))
    section("table size sweep, midgame 1, 48 threads, 16 B entries (H)")
    for s in ["AB Par TT", "AB Par TT ID Asp Ord"]:
        for b in (14, 17, 20, 23, 26):
            x = [(t, n) for k, v, ss, p, t, n in hrows
                 if k == "TTBITS" and v == b and ss == s and p == "position-midgame1"]
            t = st.median([a for a, _ in x]); n = st.median([c for _, c in x])
            print(f"  {s:22s} {(1 << b) * 16 / 1e6:8.2f} MB  {n / 1e6:6.2f} M nodes"
                  f"  {t:.3f} s  {n / t / 1e6:5.2f} M nodes/s")
    section("wait threshold, 48 threads (H)")
    for p in ["position-midgame1", "position-endgame"]:
        for m in (2, 3, 4):
            t = st.median([t for k, v, ss, pp, t, n in hrows
                           if k == "YBWMIN" and v == m and ss == TOP and pp == p])
            print(f"  {SHORT[p]} threshold {m}: {t:.3f} s")


def counters():
    section("LIKWID, midgame 1, depth 5, per thread averages, three jobs (D)")
    for path in D:
        cur = None
        for line in open(path):
            if line.startswith("===== control"):
                break
            m = re.match(r"===== group=(\w+) threads=(\d+) mask=\S+ strategy=(\S+)", line)
            if m:
                cur = m.groups()
                continue
            if cur and cur[0] == "BRANCH":
                for name in ("CPI STAT", "Clock [MHz] STAT", "Branch rate STAT",
                             "Branch misprediction ratio STAT"):
                    if re.match(r"\|\s+" + re.escape(name) + r"\s+\|", line):
                        avg = float(line.split("|")[5])
                        print(f"  {path.name[11:30]} t{cur[1]:>2s} {cur[2]:12s} {name[:-5]:28s} {avg:.4f}")
    section("thread sanitizer (C)")
    for line in open(C):
        if line.startswith(("sanitizer warnings", "SUMMARY lines")) or "SUMMARY: Thread" in line:
            print("  " + line.strip()[:110])


def main():
    rs = rows(F)
    alpha_beta_vs_minimax()
    first_move_cutoffs(rs, extra(F, "CUTOFFAT"))
    ladder(rs)
    imbalance(rs)
    root_bound()
    table(rs, extra(F, "TTSTATS"))
    counters()


if __name__ == "__main__":
    main()
