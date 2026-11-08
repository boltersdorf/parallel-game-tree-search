#!/bin/bash
# runs the three kinds of shared state under ThreadSanitizer, a report whose
# SUMMARY line names one of our files fails, a report inside the openmp runtime
# does not
#   make PORTABLE=1 CXX=clang++ EXTRA_CXXFLAGS="-fsanitize=thread -g -O1" EXTRA_LDFLAGS=-fsanitize=thread
#   bash tests/race_check.sh build/portable
# !!! judge by the SUMMARY line, it names where the racing access is, our files
# turn up in every stack anyway because each worker runs through the parallel line
set -uo pipefail

B="${1:-build/portable}"
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

# archer tells the sanitizer about the runtime's own synchronisation
ARCHER=$(ls /usr/lib/llvm-*/lib/libarcher.so 2>/dev/null | tail -1)
[ -n "$ARCHER" ] && export OMP_TOOL_LIBRARIES="$ARCHER"
export TSAN_OPTIONS="halt_on_error=0 exitcode=0 history_size=7 ignore_noninstrumented_modules=1"

fail=0
# root window, lockless table slots, nested tasks with the shared history
for s in "AB Parallel" "AB Par TT" "AB Par TT ID Asp Ord YBW"; do
  for p in start positions/position-midgame1 positions/position-endgame; do
    for t in 2 4 8; do
      echo "--- $s $p threads=$t" >> "$LOG"
      if ! OMP_NUM_THREADS=$t "$B/abtest" "$s" "$p" 3 2 >> "$LOG" 2>&1; then
        echo "exit code not 0: $s $p threads=$t"
        fail=1
      fi
    done
  done
done

total=$(grep -c "WARNING: ThreadSanitizer" "$LOG")
ours=$(grep "^SUMMARY" "$LOG" | grep -cE "(^|[ /])src/(framework|search|sequential|parallel|driver)/")
echo "archer: ${ARCHER:-not found}"
echo "$total sanitizer reports, $ours of them with a SUMMARY in our sources"
grep "^SUMMARY" "$LOG" | sort | uniq -c
if [ "$ours" -gt 0 ]; then
  grep -B40 -E "^SUMMARY.*(^|[ /])src/" "$LOG" | head -200
  fail=1
fi
exit $fail
