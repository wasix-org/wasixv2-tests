#!/usr/bin/env bash
# build.sh: builds sysbench.c for WASIX v2 (libc2) and WASIX v1 (the wasix-libc release sysroot)
# into $BENCH_OUT (default: target/bench/syscalls of this repository).
#   WASIXCC_SYSROOT_PREFIX  libc2's build-wasixv2 (holds sysroot-wasixv2)
#   WASIX1_SYSROOT_PREFIX   the v1 sysroot (default: ~/.wasixcc/sysroot-v2026-05-12.1, from
#                           `wasixccenv download-sysroot v2026-05-12.1`)
set -eu
here=$(cd "$(dirname "$0")" && pwd)
out=${BENCH_OUT:-$here/../../target/bench/syscalls}
mkdir -p "$out"
: "${WASIXCC_SYSROOT_PREFIX:?set it to the build-wasixv2 directory of libc2}"
v1=${WASIX1_SYSROOT_PREFIX:-$HOME/.wasixcc/sysroot-v2026-05-12.1}
[ -d "$v1" ] || { echo "no v1 sysroot at $v1 (set WASIX1_SYSROOT_PREFIX)" >&2; exit 1; }
WASIXCC_TARGET=wasm32-wasixv2 nice -n 19 wasixcc -O2 "$here/sysbench.c" -o "$out/sysbench-v2.wasm"
env -u WASIXCC_TARGET WASIXCC_SYSROOT_PREFIX="$v1" nice -n 19 wasixcc -O2 "$here/sysbench.c" -o "$out/sysbench-v1.wasm"
echo "built $out/sysbench-v2.wasm and $out/sysbench-v1.wasm"
