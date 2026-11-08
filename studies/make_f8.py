#!/usr/bin/env python3
"""
F8: what the optimizations are worth against the unoptimized parallel search,
over the whole thread range.

!!! the reference is AB Parallel at the same thread count, the baseline every
optimization is measured against, so the flat line at one is AB Parallel
itself and every curve reads directly as a factor over it. scaling each version
against its own one thread time answers a different question
"""

import statistics as st

import matplotlib
matplotlib.use("Agg")
import fig_style
fig_style.apply()
import matplotlib.pyplot as plt

from parse_results import rows, AN

FIG = AN / "figures"
CM = 1 / 2.54
THREADS = [1, 2, 4, 8, 16, 24, 32, 48]
BASE = "AB Parallel"

COLS = [("start", "start position"),
        ("position-midgame1", "midgame position 1"),
        ("position-midgame2", "midgame position 2"),
        ("position-endgame", "endgame position")]
# O4 is the four sequential-side optimizations, defined in the caption, so the
# legend stays short. YBWC is Feldmann's own abbreviation for the wait concept
CURVES = [("AB Par TT ID Asp Ord YBW", "TT+ID+AW+MO + YBWC",
           "#CC79A7", "D", "-"),
          ("AB Par TT ID Asp Ord", "TT+ID+AW+MO",
           "#009E73", "s", (0, (3, 1.5))),
          ("AB Par YBW", "YBWC alone",
           "#0072B2", "o", (0, (1, 1.5)))]


def main():
    FIG.mkdir(exist_ok=True)
    g = []
    have = set()
    for f in sorted(AN.glob("results/G_threads_*.out"), reverse=True):
        fr = [r for r in rows(f) if r["bind"] is None]
        keys = {(r["strategy"], r["pos"], r["threads"]) for r in fr}
        fresh = keys - have
        g += [r for r in fr if (r["strategy"], r["pos"], r["threads"]) in fresh]
        have |= fresh
    if not g:
        raise SystemExit("no results/G_threads_*.out, run the session first")

    def sel(s, p, t):
        return [r["secs"] for r in g
                if r["strategy"] == s and r["pos"] == p and r["threads"] == t]

    cols = [(p, lab) for p, lab in COLS if sel(BASE, p, 48)]

    fig, axes = plt.subplots(1, len(cols), figsize=(14 * CM, 5.8 * CM),
                             sharex=True, sharey=True, squeeze=False)
    for col, (pos, plab) in enumerate(cols):
        ax = axes[0][col]
        ax.axhline(1, color="0.4", lw=1.0)
        for strat, lab, c, mk, ls in CURVES:
            xs = [t for t in THREADS if sel(strat, pos, t) and sel(BASE, pos, t)]
            med = [st.median(sel(BASE, pos, t)) / st.median(sel(strat, pos, t))
                   for t in xs]
            ax.plot(xs, med, ls=ls, marker=mk, color=c, ms=3.6, lw=1.4,
                    markerfacecolor="white", markeredgewidth=1.0,
                    label=lab if col == 0 else None)
        ax.set_xscale("log", base=2)
        ax.set_yscale("log")
        ax.set_xticks([1, 4, 16, 48])
        ax.set_xticklabels([1, 4, 16, 48])
        ax.set_yticks([0.5, 1, 3, 10, 30, 100, 300])
        ax.set_yticklabels(["0.5", "1", "3", "10", "30", "100", "300"])
        ax.set_ylim(0.45, 340)
        ax.minorticks_off()
        ax.set_xlim(0.85, 60)
        ax.set_xlabel("threads")
        ax.set_title(plab, fontsize=8, pad=4)
        for sp in ("top", "right"):
            ax.spines[sp].set_visible(False)
    axes[0][0].set_ylabel(r"speedup over parallel $\alpha\beta$")

    fig.tight_layout(pad=0.4, rect=[0, 0.16, 1, 1])
    handles, labels = axes[0][0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", ncol=3, frameon=False,
               handlelength=2.4, columnspacing=1.6, bbox_to_anchor=(0.5, 0.0))
    fig.savefig(FIG / "F8.png", bbox_inches="tight")
    plt.close(fig)
    print("written: figures/F8.png")

    print(f"\n{'version':27s}{'position':14s}" + "".join(f"{t:>8d}" for t in THREADS))
    for strat, lab, _, _, _ in CURVES:
        for pos, plab in cols:
            line = f"{lab[:26]:27s}{plab[:13]:14s}"
            for t in THREADS:
                if sel(strat, pos, t) and sel(BASE, pos, t):
                    v = st.median(sel(BASE, pos, t)) / st.median(sel(strat, pos, t))
                    line += f"{v:8.1f}"
                else:
                    line += f"{'-':>8s}"
            print(line)

    print(
        "\nFig. 8. Speedup of the optimized versions over the parallel "
        "alpha-beta at the same thread count, depth 5, log-log, identical axes "
        "in all panels. O4 denotes the four optimizations which act on the "
        "search itself, transposition table + iterative deepening + aspiration "
        "window + move ordering, and YBWC is the Young Brothers Wait Concept "
        "which splits the tree below the root. The reference is AB Parallel, "
        "the parallel alpha-beta without any of them, drawn as the horizontal "
        "line at one, so a curve reads directly as the factor the "
        "optimizations buy. The "
        "factor of O4 without the wait concept falls with thread "
        "count, because the optimizations remove the very node visits which "
        "root splitting would otherwise have distributed over the threads, so "
        "the two compete for the same headroom; the versions with the wait "
        "concept hold or extend their factor, up to 224x on the start "
        "position, because splitting below the root restores the parallelism "
        "the optimizations took. Points are medians of three repetitions. Xeon "
        "Platinum 8174, 48 physical cores, one thread per core, "
        "OMP_PLACES=cores, OMP_PROC_BIND=close."
    )


if __name__ == "__main__":
    main()
