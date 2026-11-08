#!/bin/bash
# regenerates every figure and table from the raw logs in results/
set -e
cd "$(dirname "$0")"
for f in plot_factor_vs_threads.py plot_per_thread_work.py plot_ablation.py table_ladder.py table_intent.py; do
  python3 "$f" >/dev/null
  echo "ran $f"
done
echo "all figures and tables regenerated"
