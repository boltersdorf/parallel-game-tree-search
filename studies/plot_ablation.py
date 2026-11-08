#!/usr/bin/env python3
"""
what is left of root splitting after each optimization is added,
node visits on the left and wall time on the right, both as a fraction of AB
Parallel so a falling curve means an improvement.

!!! the two panels are the two factors of time = node visits x time per node
and they are not the same curve, the wait concept raises the node count on some
positions and still lowers the wall time, because it keeps threads busy which
root splitting leaves idle
"""

import statistics as st

import matplotlib
matplotlib.use("Agg")
import fig_style
fig_style.apply()
import matplotlib.pyplot as plt

from parse_results import rows, newest, AN

FIG = AN / "figures"
CM = 1 / 2.54

# the ticks are cumulative, each one adds its optimization to the one left of it
LADDER = [("AB Parallel", "root splitting"),
          ("AB Par TT", "+ transposition table"),
          ("AB Par TT ID", "+ iterative deepening"),
          ("AB Par TT ID Asp", "+ aspiration window"),
          ("AB Par TT ID Asp Ord", "+ move ordering"),
          ("AB Par TT ID Asp Ord YBW", "+ YBWC")]
POS = [("start", "start position", "#000000", "o", "-"),
       ("position-midgame1", "midgame position 1", "#D55E00", "v", (0, (3, 1.5))),
       ("position-midgame2", "midgame position 2", "#009E73", "s", (0, (1, 1.5))),
       ("position-endgame", "endgame position", "#0072B2", "D", (0, (5, 1.5)))]


def main():
    FIG.mkdir(exist_ok=True)
    f = rows(newest("F_ladder"))

    def med(s, p, field):
        v = [r[field] for r in f if r["strategy"] == s and r["pos"] == p
             and r["depth"] == 5 and r["threads"] == 48]
        return st.median(v) if v else None

    fig, axes = plt.subplots(1, 2, figsize=(14 * CM, 7.8 * CM), sharex=True,
                             sharey=True)
    xs = range(len(LADDER))
    for ax, field, ylab in (
            (axes[0], "nodes", "node visits, relative to root splitting"),
            (axes[1], "secs", "wall time, relative to root splitting")):
        ax.axhline(1, color="0.75", lw=0.8)
        for pos, plab, c, mk, ls in POS:
            base = med(LADDER[0][0], pos, field)
            ys = [med(s, pos, field) / base for s, _ in LADDER]
            ax.plot(xs, ys, ls=ls, marker=mk, color=c, ms=4, lw=1.4,
                    markerfacecolor="white", markeredgewidth=1.0,
                    label=plab if field == "nodes" else None)
        ax.set_yscale("log")
        ax.set_yticks([0.005, 0.01, 0.05, 0.1, 0.5, 1])
        ax.set_yticklabels(["1/200", "1/100", "1/20", "1/10", "1/2", "1"])
        ax.minorticks_off()
        ax.set_ylim(0.003, 1.6)
        ax.set_xticks(list(xs))
        ax.set_xticklabels([lab for _, lab in LADDER], rotation=30,
                           ha="right", fontsize=7)
        ax.set_ylabel(ylab)
        for sp in ("top", "right"):
            ax.spines[sp].set_visible(False)

    fig.tight_layout(pad=0.6, rect=[0, 0.08, 1, 0.99])
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", ncol=4, frameon=False,
               handlelength=2.2, columnspacing=1.6, bbox_to_anchor=(0.5, 0.0))
    fig.savefig(FIG / "ablation_visits_time.png", bbox_inches="tight")
    plt.close(fig)
    print("written: figures/ablation_visits_time.png")

    print(f"\n{'optimization':16s}" + "".join(f"{p[1]:>26s}" for p in POS))
    for s, lab in LADDER:
        line = f"{lab.replace(chr(10), ' '):16s}"
        for pos, _, _, _, _ in POS:
            n = med(s, pos, "nodes") / med(LADDER[0][0], pos, "nodes")
            t = med(s, pos, "secs") / med(LADDER[0][0], pos, "secs")
            line += f"   {n:10.4f} n {t:8.4f} t"
        print(line)



if __name__ == "__main__":
    main()
