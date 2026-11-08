#!/bin/bash
# builds abtest into studies/bin
#   studies/build.sh                                  -> bin/abtest
#   studies/build.sh -eq -DTT_DEPTH_EQ                -> bin/abtest-eq
#   studies/build.sh -tsan "-fsanitize=thread -O1 -g" -> bin/abtest-tsan
# !!! compiles with icpx directly, make is not on the compute nodes
# counting and timing are the same binary, counters are thread local and
# merged only after the search

set -uo pipefail

AN="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$AN")"
BIN="$AN/bin"
mkdir -p "$BIN"

SUFFIX="${1:-}"
EXTRA="${2:-}"

# icpx on the cluster, CXX and CF are overridable so the script can be tried out
# on a local machine, where whatever it produces is not a result
CXX="${CXX:-icpx}"
command -v "$CXX" >/dev/null || { echo "ERROR: $CXX not in PATH, run: module load intel"; exit 1; }

CF="${CF:--O3 -fno-omit-frame-pointer -qopenmp -std=c++20}"
CF="$CF -Isrc/framework -Isrc/search"
LDF="${LDF:-}"
OUT="$BIN/abtest$SUFFIX"
OBJ=$(mktemp -d)
trap 'rm -rf "$OBJ"' EXIT

# every strategy in one binary, so a comparison differs in the strategy and
# nothing else
SOURCES="src/framework/move.cpp src/framework/board.cpp src/framework/network.cpp
         src/framework/search.cpp src/framework/eval.cpp
         src/search/counters.cpp src/search/table.cpp src/search/zobrist.cpp
         src/sequential/minimax.cpp src/sequential/alpha_beta.cpp
         src/parallel/root_splitting.cpp
         src/parallel/cumulative/tt.cpp
         src/parallel/cumulative/tt_id.cpp
         src/parallel/cumulative/tt_id_aw.cpp
         src/parallel/cumulative/tt_id_aw_mo.cpp
         src/parallel/cumulative/tt_id_aw_mo_ybwc.cpp
         src/parallel/isolated/mo.cpp
         src/parallel/isolated/ybwc.cpp
         src/driver/abtest.cpp"

cd "$ROOT" || exit 1
echo "=== $(hostname), $($CXX --version | head -1)"
echo "flags: $CF $EXTRA"

for s in $SOURCES; do
  [ -f "$s" ] || { echo "ERROR: $s missing"; exit 1; }
  o=$(echo "${s%.cpp}" | tr / _).o
  if ! "$CXX" $CF $EXTRA -c "$s" -o "$OBJ/$o"; then
    echo "ERROR: compiling $s failed"
    exit 1
  fi
done

if ! "$CXX" $CF $EXTRA $LDF -o "$OUT" "$OBJ"/*.o; then
  echo "ERROR: link failed"
  exit 1
fi

echo "built $OUT"

# !!! abtest with no arguments exits 1 on purpose and with pipefail that would
# become the exit status of the whole script, so a good build would read as a
# failure to every caller
"$OUT" 2>&1 | sed -n '/available strategies/,$p' || true
exit 0
