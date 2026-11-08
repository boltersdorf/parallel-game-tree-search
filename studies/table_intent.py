#!/usr/bin/env python3
"""
one row per optimization, its intent, the counter which tests that intent
measured on the version before and after adding it, and the net wall time
against AB Parallel.

!!! before and after are the same counter on two versions which differ in this
one optimization only, so every row is a controlled comparison. the counter
differs per row because each intent predicts a different quantity, and a row
can reach its intent without paying in wall time, which is why the net column
is separate
"""

import statistics as st
from collections import defaultdict

from parse_results import rows, newest, AN

POS = "position-midgame1"
BASE = "AB Parallel"


def main():
    f = newest("F_ladder")
    rs = rows(f)
    cut, asp = defaultdict(list), defaultdict(list)
    for line in open(f):
        p = line.rstrip("\n").split(",")
        if len(p) < 6 or p[2] != POS or p[3] != "5" or p[4] != "48":
            continue
        if line.startswith("CUTOFFAT,"):
            cut[p[1]].append([int(x) for x in p[6:]])
        if line.startswith("ASPSTATS,"):
            asp[p[1]].append([int(x) for x in p[6:10]])

    def med(s, field):
        v = [r[field] for r in rs if r["strategy"] == s and r["pos"] == POS
             and r["depth"] == 5 and r["threads"] == 48]
        return st.median(v) if v else None

    def nodes(s):
        return med(s, "nodes") / 1e6

    def firstmove(s):
        return st.median([100 * r[0] / sum(r) for r in cut[s]])

    # !!! busiest over the average over all 48 threads, idle ones included, not
    # busiest over idlest: the latter is set by whichever thread got least and
    # drops idle threads entirely, so it reads mild exactly where threads starve
    def imbalance(s):
        out = []
        for r in rs:
            if (r["strategy"] == s and r["pos"] == POS and r["depth"] == 5
                    and r["threads"] == 48):
                w = [q["interior"] + q["leaf"] + (q["ttcut"] or 0)
                     for q in r["per"]]
                out.append(max(w) / (sum(w) / len(w)))
        return st.median(out)

    def net(s):
        return med(BASE, "secs") / med(s, "secs")

    # optimization, intent, counter, before version, after version, formatter
    SPEC = [
        ("transposition table",
         "answer a position which was already searched",
         "node visits [$10^6$]",
         "AB Parallel", "AB Par TT", nodes, "{:.2f}"),
        ("iterative deepening",
         "let the previous depth name the move to search first",
         "cutoffs on first move [\\%]",
         "AB Par TT", "AB Par TT ID", firstmove, "{:.1f}"),
        ("aspiration window",
         "search a narrow window, so more subtrees fall outside it",
         "node visits [$10^6$]",
         "AB Par TT ID", "AB Par TT ID Asp", nodes, "{:.2f}"),
        ("move ordering",
         "search the move most likely to cut before the others",
         "cutoffs on first move [\\%]",
         "AB Par TT ID Asp", "AB Par TT ID Asp Ord", firstmove, "{:.1f}"),
        ("young brothers wait",
         "split below the root, so threads stay busy",
         "busiest over average thread",
         "AB Par TT ID Asp Ord", "AB Par TT ID Asp Ord YBW", imbalance,
         "{:.1f}"),
    ]

    body = []
    for name, intent, counter, before, after, fn, fmt in SPEC:
        body.append((name, intent, counter, fmt.format(fn(before)),
                     fmt.format(fn(after)), net(after)))

    w = max(len(r[2]) for r in body)
    print(f"{'optimization':22s}{'counter':{w+2}s}{'before':>9s}{'after':>9s}"
          f"{'net':>8s}")
    for name, intent, counter, b, a, n in body:
        c = counter.replace("\\%", "%").replace("$10^6$", "10^6")
        print(f"{name:22s}{c:{w+2}s}{b:>9s}{a:>9s}{n:7.1f}x")

    a = [st.median(c) for c in zip(*asp["AB Par TT ID Asp"])]
    print(f"\naspiration re-searches: {a[1] + a[2]:.0f} of {a[0]:.0f} "
          f"iterations, {a[3]:.0f} nodes")

    tex = [r"\begin{table}[htbp]", r"\centering", r"\footnotesize",
           r"\setlength{\tabcolsep}{5pt}",
           r"\begin{tabular}{@{}llrrr@{}}", r"\toprule",
           r"Optimization & Counter testing the intent & Before & After & "
           r"Net \\",
           r"\midrule"]
    for name, intent, counter, b, a_, n in body:
        tex.append(f"{name} & {counter} & {b} & {a_} & {n:.1f}$\\times$ \\\\")
    tex += [r"\bottomrule", r"\end{tabular}", r"\end{table}"]

    out = AN / "results/intent.tex"
    out.write_text("\n".join(tex) + "\n")
    print(f"\nLaTeX: {out.relative_to(AN)}")


if __name__ == "__main__":
    main()
