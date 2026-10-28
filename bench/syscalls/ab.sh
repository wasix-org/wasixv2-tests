#!/usr/bin/env bash
# ab.sh <wasmer-a> <wasmer-b> <abi> <call> [N] [RUNS]: runs one call type (getpid, read, write,
# clock_gettime, dup_close, thread, spawn) of one ABI (v2 or v1) with two wasmer binaries
# alternately, RUNS times each (default 11), pinned like run.sh, and prints the medians.
# Interleaving keeps drifting machine load out of the comparison.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
out=${BENCH_OUT:-$here/../../target/bench/syscalls}
a=$(realpath "$1"); b=$(realpath "$2"); abi=$3; t=$4; n=${5:-1000000}; runs=${6:-11}
cd "$out"
declare -A v
for r in $(seq "$runs"); do
  for w in "$a" "$b"; do
    x=$(nice -n 19 taskset -c "${CPUS:-6,7}" "$w" run --volume "$PWD:/bench" sysbench-$abi.wasm -- "$n" "$t" /bench/sysbench-$abi.wasm | awk '{print $2}')
    v[$w]="${v[$w]:-} $x"
  done
done
echo "# $(date -Is) load: $(cut -d' ' -f1-3 /proc/loadavg)"
for w in "$a" "$b"; do
  python3 -I -c 'import sys,statistics as s; v=[float(x) for x in sys.argv[2:]]; print(f"{sys.argv[1]}\n  median {s.median(v):10.1f}  min {min(v):10.1f}  max {max(v):10.1f}  ({len(v)} runs)")' "$w" ${v[$w]}
done
