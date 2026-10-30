#!/usr/bin/env bash
# Same source, three guest configurations and native Linux. No binaries in git.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=${BENCH_OUT:-$here/../../target/bench/performance}
: "${WASIXCC_SYSROOT_PREFIX:?set libc2 build-wasixv2 directory}"
mkdir -p "$out"
export BINARYEN_CORES=3 OMP_NUM_THREADS=3
for config in v2-eh v2-async v1; do
  case $config in
    v2-eh) target=wasm32-wasixv2; exceptions=yes; prefix=$WASIXCC_SYSROOT_PREFIX ;;
    v2-async) target=wasm32-wasixv2; exceptions=no; prefix=$WASIXCC_SYSROOT_PREFIX ;;
    v1) target=wasm32-wasi; exceptions=no; prefix=${WASIX1_SYSROOT_PREFIX:-$HOME/.wasixcc/sysroot-v2026-05-12.1} ;;
  esac
  WASIXCC_TARGET=$target WASIXCC_WASM_EXCEPTIONS=$exceptions WASIXCC_SYSROOT_PREFIX=$prefix \
    nice -n 19 taskset -c "${CPUS:-4-7}" wasixcc -O2 -g "$here/micro.c" -o "$out/micro-$config.wasm"
done
nice -n 19 taskset -c "${CPUS:-4-7}" "${HOSTCC:-cc}" -O2 -g -pthread "$here/micro.c" -o "$out/micro-native"
