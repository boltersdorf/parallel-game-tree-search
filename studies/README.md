# Studies

Measurement data and analysis of the search, as it ran on SuperMUC-NG batch nodes. The folder holds the SLURM sessions, the raw driver output, LIKWID counters and ThreadSanitizer logs, the scripts which turn them into tables and figures, and the figures themselves.

| Session | Question | Raw data |
|---|---|---|
| `session_a_oracle.scp` | every parallel value against the sequential search, minimax against alpha-beta, time per depth | `sA_oracle.out`, `sA2A3.out`, `A_oracle_*.out` |
| `session_c_races.scp` | ThreadSanitizer over the three kinds of shared state | `C_races_*.out` |
| `session_d_counters.scp` | LIKWID branch, cache and memory groups at 24 and 48 threads, with and without the table, and a depth-1 control for the setup | `D_counters_*.out` |
| `session_f_ladder.scp` | every version on the four positions at one and 48 threads | `F_ladder_*.out` |
| `session_g_threads.scp` | thread sweep from 1 to 48, close against spread placement | `G_threads_*.out` |
| `session_h_sweeps.scp` | split depth, table size and aspiration window width | `H_sweeps_*.out` |

The `sA` files are the first run of session A, before the optimized versions existed, and hold the check of root splitting, the minimax comparison and the time per depth. Session D ran on three nodes, whose times and node visits agree to the third digit. The NUMA control at the end of session D was set to 48 threads only after these runs. In the one log where it ran, it ran on one thread, and no result uses it. Earlier runs of the other sessions measured code which changed afterwards and are not included.

## Regenerating tables and figures

`bash studies/regen_all.sh` rebuilds every figure and table from the raw data. It needs Python 3 with matplotlib. Every committed figure and table regenerates byte-identically.

| Output | Content | Script |
|---|---|---|
| `figures/factor_vs_threads.png` | wall time of root splitting over the optimized versions, against thread count | `plot_factor_vs_threads.py` |
| `figures/per_thread_work.png` | node visits of each of the 48 threads, relative to the average thread | `plot_per_thread_work.py` |
| `figures/ablation_visits_time.png` | node visits and wall time left after each optimization | `plot_ablation.py` |
| `results/ladder.tex` | node visits and time of every version on every position | `table_ladder.py` |
| `results/intent.tex` | the counter which tests the intent of each optimization | `table_intent.py` |

`python3 studies/readme_numbers.py` recomputes every number the top-level README quotes, from the same raw files.

The parser keeps only lines with the full CSV layout, so headers, affinity lines and LIKWID tables pass through it unread. The sessions check their own data while they run: session A fails on any value which differs from the sequential search, session D fails when a wrapped search printed no result, and every session prints the affinity mask it was granted.

## Running a session

```bash
salloc --account=<project> -p test --nodes=1 --ntasks=1 --cpus-per-task=48 \
       --hint=nomultithread --exclusive --time=00:30:00 --ear=off
srun --cpus-per-task=48 --hint=nomultithread --pty bash -i
module load intel
studies/build.sh
DEPTH=5 studies/session_f_ladder.scp      # or: sbatch studies/session_f_ladder.scp
```

`common.sh` refuses to run on a login node, below 48 visible CPUs and above 48 threads, and never overwrites a result file. Each session carries a time budget and stops starting new work before the allocation ends. Every session was dry-run locally against stubs for `icpx`, `srun`, `likwid-perfctr`, `numactl` and `module`.

## Layout of the measured code

The sessions ran in a working repository whose layout differs from this one. One folder per version there, one file per version here, and all of them linked into the same driver in both.

| Working repository | This repository |
|---|---|
| `versions/4-ab-par/analysis/` | `studies/` |
| `versions/4-ab-par/search-ab-seq-oracle.cpp` | `src/sequential/alpha_beta.cpp` |
| `versions/4-ab-par/search-minmax-seq-oracle.cpp` | `src/sequential/minimax.cpp` |
| `versions/4-ab-par/search-ab-par.cpp` | `src/parallel/root_splitting.cpp` |
| `versions/5-ab-par-tt/` | `src/parallel/cumulative/tt.cpp`, `src/search/table.*`, `src/search/zobrist.*` |
| `versions/6-ab-par-tt-id/` | `src/parallel/cumulative/tt_id.cpp` |
| `versions/7-ab-par-tt-id-asp/` | `src/parallel/cumulative/tt_id_aw.cpp` |
| `versions/8-ab-par-tt-id-asp-ord/` | `src/parallel/cumulative/tt_id_aw_mo.cpp` |
| `versions/9-ab-par-tt-id-asp-ord-ybw/` | `src/parallel/cumulative/tt_id_aw_mo_ybwc.cpp` |
| `versions/s1-ab-par-ord/` | `src/parallel/isolated/mo.cpp` |
| `versions/s2-ab-par-ybw/` | `src/parallel/isolated/ybwc.cpp` |
| `versions/4-ab-par/abtest.cpp`, `parstats.*` | `src/driver/abtest.cpp`, `src/search/counters.*` |

The strategy names, and with them every name in the logs, are unchanged.

## Changes after the measurements

The sources here are the measured sources with shorter comments and three fixes. None of the versions was measured again.

1. A parallel split could return a correct value with a move which does not reach it. A move whose search failed low returns only an upper bound, and that bound can equal the exact maximum. The first maximum then decided, so the move depended on the order in which the tasks finished. Each split now records the window every move was searched with, and on a tie the move whose score lies inside its window wins (`src/search/pick_best.h`). The rule applies at the root of every root-splitting version and at every split node of the Young Brothers Wait Concept.
2. The root window was not clamped below beta in five of the root-splitting versions. A task which reads a shared alpha at or above beta searches an inverted window. A win scores beyond the evaluation range, so this can happen at a root which has a forced win.
3. The timed engine took the ply of a node as the depth limit minus its remaining depth. Started without a depth, the limit is 40, so every node with a remaining depth of 8 or less fell outside the 32-entry killer table and the engine searched without killers. The ply now counts from the root of each iteration. The engine is not part of any measurement.

The first fix only changes which of two tied moves a search returns. The second acts only at a root with a forced win, and none of the four positions has one at the measured depths. At one thread, built with clang and LLVM's OpenMP runtime, the values, moves and node counts of all nine strategies on all four positions at depth 5 equal the logs of job 5819721 exactly. The test suite pins the endgame counts of seven of them. The two YBWC versions still queue tasks at one thread, and the order in which a runtime runs queued tasks is its own, so GNU's runtime gives them a different tree.

Besides that, `common.sh` now prints its header after the output is redirected into the result file, so new result files also carry the CPU model and mask, and session H lists split depth 2 as the default, which its own result made the default for every later session. A node splits when its remaining depth is above the split depth. Home paths in the logs are replaced by `~` and the project account in the job scripts by `<project>`. Some comment lines and two echo lines in the sessions changed wording, so those two lines read differently in the logs, and the race filter of session C matches the new file names. The raw ThreadSanitizer log of session C is not included, and its summary lines are in `C_races_*.out`.
