#!/usr/bin/env bash
# run.sh <wasmer> <label> [N] [RUNS]: runs both ABIs RUNS times (default 5) at nice 19, pinned to
# CPUS (default 6,7, away from the test suite's 0-3), and writes $BENCH_OUT/results/<label>.txt
# with the median and the min..max spread of ns/call per call type. Build first with build.sh.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
out=${BENCH_OUT:-$here/../../target/bench/syscalls}
W=$(realpath "$1"); label=$2; n=${3:-200000}; runs=${4:-5}
cd "$out"
mkdir -p results raw
raw=raw/$label.txt; : > "$raw"
# Concurrent heavy processes skew timing: record them.
echo "# $(date -Is) load: $(cut -d' ' -f1-3 /proc/loadavg) cargo/rustc procs: $(pgrep -c -x 'rustc|cargo' || true)" >> "$raw"
for abi in v2 v1; do
  # Warm the module cache so compilation stays out of the measured runs.
  nice -n 19 "$W" run --volume "$PWD:/bench" sysbench-$abi.wasm -- 1000 all /bench/sysbench-$abi.wasm > /dev/null
  for r in $(seq "$runs"); do
    nice -n 19 taskset -c "${CPUS:-6,7}" "$W" run --volume "$PWD:/bench" sysbench-$abi.wasm -- "$n" all /bench/sysbench-$abi.wasm | sed "s/^/$abi /" >> "$raw"
  done
done
echo "# $(date -Is) load: $(cut -d' ' -f1-3 /proc/loadavg) cargo/rustc procs: $(pgrep -c -x 'rustc|cargo' || true)" >> "$raw"
python3 -I - "$raw" > results/$label.txt <<'PY'
import sys, statistics, collections
d = collections.defaultdict(list); notes = []
for line in open(sys.argv[1]):
    if line.startswith('#'): notes.append(line.strip()); continue
    abi, name, ns, _ = line.split()
    d[(abi, name)].append(float(ns))
for n in notes: print(n)
print(f"{'abi':4} {'call':14} {'median':>9} {'min':>9} {'max':>9}  (ns/call, {len(next(iter(d.values())))} runs)")
for (abi, name), v in d.items():
    print(f"{abi:4} {name:14} {statistics.median(v):9.1f} {min(v):9.1f} {max(v):9.1f}")
PY
cat results/$label.txt
