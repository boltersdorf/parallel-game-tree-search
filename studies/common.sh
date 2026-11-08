# sourced by every session, sets the paths and checks where we are running
# !!! the binary runs directly, never through srun, one node and one task, so
# srun only adds a step with its own cpuset and a cpu-bind that overrides
# OMP_PLACES without saying so, override with RUNNER= if it is ever needed

# !!! not SLURM_SUBMIT_DIR, salloc sets it to wherever salloc was called, the
# script's own path is the only thing that is right in every case
AN="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$AN")"
BIN="$AN/bin"
OUT="$AN/results"
mkdir -p "$OUT"

RUNNER=${RUNNER-}
MODE=$([ -n "${SLURM_JOB_ID:-}" ] && echo "job ${SLURM_JOB_ID}" || echo "no allocation")

export OMP_PLACES=${OMP_PLACES:-cores}
export OMP_PROC_BIND=${OMP_PROC_BIND:-close}
export OMP_DISPLAY_AFFINITY=TRUE
export OMP_AFFINITY_FORMAT="AFFINITY tid=%0.3n mask=%A"

# !!! not nproc, it returns OMP_NUM_THREADS when that is set and the site sets
# it to 1, so nproc says 1 on a full node, the affinity mask is the truth
CPUMASK=$(grep -m1 Cpus_allowed_list /proc/self/status 2>/dev/null | awk '{print $2}')
if [ -n "$CPUMASK" ]; then
    CPUS=0
    IFS=',' read -ra _r <<< "$CPUMASK"
    for _e in "${_r[@]}"; do
        case "$_e" in
            *-*) CPUS=$(( CPUS + ${_e#*-} - ${_e%-*} + 1 )) ;;
            *)   CPUS=$(( CPUS + 1 )) ;;
        esac
    done
else
    CPUMASK="unknown"
    CPUS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 0)
fi
if command -v lscpu >/dev/null 2>&1; then
    MODEL=$(lscpu | sed -n 's/^Model name: *//p')
else
    MODEL=$(uname -m)
fi

case "$MODEL" in
    *6148*)
        echo "ERROR: login node cpu, nothing measured here is valid"
        exit 1 ;;
esac

# !!! only the platinum 8174 gives results, everything else is a test run
case "$MODEL" in
    *8174*) ;;
    *) LOCAL_TEST=1 ;;
esac

# a thread sweep in a cpuset smaller than the node is noise, so stop early
if [ -z "${LOCAL_TEST:-}" ] && [ "$CPUS" -lt 48 ]; then
    echo "WARNING: only $CPUS cpus in the mask, expected at least 48"
    echo "  allocate the node exclusive with --cpus-per-task=48"
    if [ "${ALLOW_SMALL_CPUSET:-0}" != "1" ]; then
        echo "  ALLOW_SMALL_CPUSET=1 runs it anyway"
        exit 1
    fi
fi

# !!! 48 is the hard ceiling, one thread per physical core, above that a core
# carries two threads and the run is on hyperthreads, which no result uses
export OMP_THREAD_LIMIT=48
if [ "${OMP_NUM_THREADS:-0}" -gt 48 ] 2>/dev/null; then
    echo "ERROR: OMP_NUM_THREADS=$OMP_NUM_THREADS, more than the 48 physical cores"
    exit 1
fi

[ -x "$BIN/abtest" ] || { echo "ERROR: no $BIN/abtest, run studies/build.sh"; exit 1; }

# !!! every run writes its own file, a fixed name means the second run of a
# session silently destroys the first, and a job id alone is not enough because
# an interactive run has none, so session, wall clock, job and host together
SESSION="${SESSION:-unnamed}"
RUNID="${SESSION}_$(date +%Y%m%d-%H%M%S)_j${SLURM_JOB_ID:-none}_$(hostname -s)"
RESULT="$OUT/${RUNID}.out"
if [ -e "$RESULT" ]; then
    echo "ERROR: $RESULT already exists, refusing to overwrite"
    exit 1
fi
exec > >(tee "$RESULT") 2>&1
echo "run id:  $RUNID"
echo "results: $RESULT"

# after the tee, so the header is in the result file
echo "=== $(date) on $(hostname), $MODE, $CPUS cpus in the cpuset"
echo "cpu: $MODEL"
echo "mask: $CPUMASK, OMP_NUM_THREADS=${OMP_NUM_THREADS:-unset}"
# !!! we never run on hyperthreads, the cpuset is 96 logical cpus because the
# node is held exclusively, but a place is a core, so 48 threads land on 48
# physical cores, the AFFINITY lines in the log are the proof
echo "places: OMP_PLACES=$OMP_PLACES OMP_PROC_BIND=$OMP_PROC_BIND"
command -v lscpu >/dev/null 2>&1 && \
    lscpu | grep -E '^CPU\(s\)|Thread\(s\) per core|Core\(s\) per socket|^Socket'
if [ -n "${LOCAL_TEST:-}" ]; then
    echo "NOTE: not a platinum 8174, this is a test run and its numbers"
    echo "      are not results"
fi
echo

# positions by bare name, as in the logs
cd "$ROOT/positions" || exit 1
