#!/usr/bin/env python3
"""
every version at the working point, nodes and wall time on the four test
positions.

!!! nodes and time never move by the same ratio, the node rate falls as rungs
add work per node, which is why both columns exist and neither is redundant
"""

import statistics as st

from parse_results import rows, newest, AN

VERSIONS = [
    ("AB Seq Oracle", r"$\alpha\beta$ sequential, 1 core", 1),
    ("AB Parallel", "root splitting, 48 threads", 48),
    ("AB Par TT", r"+ transposition table", 48),
    ("AB Par TT ID", r"+ iterative deepening", 48),
    ("AB Par TT ID Asp", r"+ aspiration window", 48),
    ("AB Par TT ID Asp Ord", r"+ move ordering", 48),
    ("AB Par TT ID Asp Ord YBW", r"+ YBWC", 48),
    ("AB Par Ord", r"move ordering alone", 48),
    ("AB Par YBW", r"YBWC alone", 48),
]
POS = [("start", "start"), ("position-midgame1", "midgame 1"),
       ("position-midgame2", "midgame 2"), ("position-endgame", "endgame")]


def fmt3(v):
    if v >= 100:
        return f"{v:.0f}"
    if v >= 10:
        return f"{v:.1f}"
    if v >= 1:
        return f"{v:.2f}"
    return f"{v:.3f}"


def main():
    f = rows(newest("F_ladder"))

    def cell(s, p, t):
        runs = [r for r in f if r["strategy"] == s and r["pos"] == p
                and r["depth"] == 5 and r["threads"] == t]
        if not runs:
            return None
        return (st.median([r["nodes"] for r in runs]) / 1e6,
                st.median([r["secs"] for r in runs]))

    print(f"{'':32s}" + "".join(f"{lab:>24s}" for _, lab in POS))
    print(f"{'version':32s}" + "".join(f"{'Mnodes':>16s}{'s':>8s}" for _ in POS))
    body = []
    for s, lab, t in VERSIONS:
        vals = [cell(s, p, t) for p, _ in POS]
        body.append((lab, vals))
        print(f"{lab:32s}" + "".join(f"{fmt3(n):>16s}{fmt3(sec):>8s}"
                                     for n, sec in vals))

    ncol = len(POS)
    heads = " & ".join(
        r"\multicolumn{2}{c}{" + lab + "}" for _, lab in POS)
    cmid = " ".join(
        r"\cmidrule(lr){" + f"{2+2*k}-{3+2*k}" + "}" for k in range(ncol))
    tex = [r"\begin{table}[htbp]", r"\centering", r"\scriptsize",
           r"\setlength{\tabcolsep}{3pt}",
           r"\begin{tabular}{@{}l" + "rr" * ncol + r"@{}}", r"\toprule",
           " & " + heads + r" \\",
           cmid,
           "Version" + r" & Node visits & Time" * ncol + r" \\",
           "" + r" & [$10^6$] & [sec]" * ncol + r" \\",
           r"\midrule"]
    for i, (lab, vals) in enumerate(body):
        cells = " & ".join(f"{fmt3(n)} & {fmt3(sec)}" for n, sec in vals)
        tex.append(f"{lab} & {cells} \\\\")
        if i in (0, 6):
            tex.append(r"\midrule")
    tex += [r"\bottomrule", r"\end{tabular}", r"\end{table}"]

    out = AN / "results/ladder.tex"
    out.write_text("\n".join(tex) + "\n")
    print(f"\nLaTeX: {out.relative_to(AN)}")


if __name__ == "__main__":
    main()
