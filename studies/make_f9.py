#!/usr/bin/env python3
"""
F9: how the node visits of one search spread over the 48 threads, split into the
three categories the task asks for.

!!! every panel is divided by its own mean thread, so the y axis is the same
everywhere and the shape of the imbalance is comparable by eye. plotting the
raw counts instead puts 1500 thousand nodes next to 10 and the panels cannot be
read against each other, which is the whole point of the figure

!!! the representative run is the repeat whose wall time is the median of its
five, never an average over repeats, because averaging distributions from
different nondeterministic runs flattens exactly the spikes this figure shows
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

COLS = [("start", "start position"),
        ("position-midgame1", "midgame position 1"),
        ("position-midgame2", "midgame position 2"),
        ("position-endgame", "endgame position")]
# rows are cumulative, the abbreviations are spelled out in the caption
ROWS = [("AB Parallel", "parallel " + r"$\alpha\beta$"),
        ("AB Par TT ID Asp Ord", "+ TT ID AW MO"),
        ("AB Par TT ID Asp Ord YBW", "+ YBWC")]
# !!! leaf is 60 to 95 percent of every bar so it takes the lightest value, and
# the table share of 2 to 14 percent takes the darkest one, otherwise it
# disappears, it sits on top of the stack and needs contrast against white too
CAT = [("interior", "interior, expanded", "0.45"),
       ("leaf", "leaf, evaluated", "0.88"),
       ("ttcut", "answered by the table", "0.05")]


def representative(runs):
    med = st.median([r["secs"] for r in runs])
    return min(runs, key=lambda r: abs(r["secs"] - med))


def main():
    FIG.mkdir(exist_ok=True)
    f = rows(newest("F_ladder"))
    have_cached = any(q["ttcut"] for r in f for q in r["per"])

    fig, axes = plt.subplots(3, 4, figsize=(14 * CM, 8.4 * CM),
                             sharex=True, sharey=True)
    for row, (strat, rlab) in enumerate(ROWS):
        for col, (pos, plab) in enumerate(COLS):
            ax = axes[row][col]
            runs = [r for r in f if r["strategy"] == strat and r["pos"] == pos
                    and r["depth"] == 5 and r["threads"] == 48]
            r = representative(runs)
            per = sorted(r["per"],
                         key=lambda q: -(q["interior"] + q["leaf"] + (q["ttcut"] or 0)))
            n = len(per)
            mean = sum(q["interior"] + q["leaf"] + (q["ttcut"] or 0)
                       for q in per) / n

            bottom = [0.0] * n
            for key, _lab, colr in CAT:
                vals = [(q[key] or 0) / mean for q in per]
                if not any(vals):
                    continue
                ax.bar(range(1, n + 1), vals, bottom=bottom, width=1.0,
                       color=colr, edgecolor="none")
                bottom = [b + v for b, v in zip(bottom, vals)]
            ax.axhline(1.0, color="black", lw=0.8, ls="--")

            ax.set_xlim(0.5, n + 0.5)
            ax.set_xticks([1, 16, 32, 48])
            ax.set_ylim(0, 7)
            ax.set_yticks([0, 1, 2, 4, 7])
            ax.grid(False)
            for sp in ("top", "right"):
                ax.spines[sp].set_visible(False)
            if row == 0:
                ax.set_title(plab, fontsize=8, pad=4)
            if col == 0:
                ax.set_ylabel(rlab)

    fig.supylabel("node visits per thread, relative to the average thread",
                  fontsize=8, x=0.005)
    fig.tight_layout(pad=0.4, rect=[0.045, 0.13, 1, 1])
    fig.text(0.55, 0.085, "the 48 threads, ordered from busiest to idlest",
             ha="center", fontsize=8)
    labels = [lab for key, lab, _ in CAT if have_cached or key != "ttcut"]
    handles = [plt.Rectangle((0, 0), 1, 1, color=c)
               for key, _l, c in CAT if have_cached or key != "ttcut"]
    fig.legend(handles, labels, loc="lower center", ncol=len(labels),
               frameon=False, bbox_to_anchor=(0.55, 0.0))
    fig.savefig(FIG / "F9.png", bbox_inches="tight")
    plt.close(fig)
    print("written: figures/F9.png")

    print(f"\n{'version':26s}{'position':20s}{'idle':>6s}"
          f"{'busiest/mean':>14s}{'busiest/idlest':>16s}")
    for strat, _ in ROWS:
        for pos, plab in COLS:
            runs = [r for r in f if r["strategy"] == strat and r["pos"] == pos
                    and r["depth"] == 5 and r["threads"] == 48]
            r = representative(runs)
            per = [q["interior"] + q["leaf"] + (q["ttcut"] or 0)
                   for q in r["per"]]
            nz = [v for v in per if v > 0]
            mean = sum(per) / len(per)
            print(f"{strat:26s}{plab:20s}{len(per)-len(nz):6d}"
                  f"{max(per)/mean:14.1f}{max(nz)/min(nz):16.1f}")

    print(
        "\nFig. 9. Node visits per thread of one search at 48 threads, depth "
        "5, split into interior nodes, leaf nodes given to the evaluator and "
        "nodes the transposition table answered outright. O4 denotes the four "
        "optimizations acting on the search itself, transposition table + "
        "iterative deepening + aspiration window + move ordering, and YBWC is "
        "the Young Brothers Wait Concept. Every bar is divided by the "
        "average thread of its own panel, which is that search's node visits "
        "over all 48 threads and counts the idle ones, so the dashed line at "
        "one is a perfectly balanced search and a bar at four means the "
        "thread did four times what an average thread did. Each panel uses "
        "the average of its own search, because the question here is how "
        "evenly that search spread its work; one common reference would mix "
        "the imbalance together with the speedup, and since the versions "
        "differ by a factor of fifty in total work the shapes could then no "
        "longer be compared. The "
        "absolute counts, which differ by two orders of magnitude between the "
        "rows, are in Table VII. Threads are ordered from the busiest on the "
        "left to the idlest on the right, since which core took which subtree carries "
        "no meaning of its own, so the horizontal position is a place in that "
        "order and not a thread number. Root splitting leaves threads without any work where a position "
        "has fewer root moves than threads, 41 root moves against 48 threads "
        "on the endgame position. Move ordering concentrates the remaining "
        "work into the few subtrees which do not cut at once, and the wait "
        "concept flattens the distribution again by splitting below the root. "
        "The search is nondeterministic, so the panel shows the repeat whose "
        "wall time is the median of five. Xeon Platinum 8174, "
        "OMP_PLACES=cores, OMP_PROC_BIND=close."
    )


if __name__ == "__main__":
    main()
