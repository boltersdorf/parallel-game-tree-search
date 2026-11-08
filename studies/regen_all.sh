#!/bin/bash
# regenerates every figure and table from the raw logs in results/
set -e
cd "$(dirname "$0")"
for f in make_f8.py make_f9.py make_f10.py make_t7.py make_t8.py; do
  python3 "$f" >/dev/null
  echo "ran $f"
done
python3 render_tables.py
echo "all figures and tables regenerated"
