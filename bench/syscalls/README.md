# Syscall microbenchmark

`sysbench.c` times the syscall hot path of a guest: N `getpid`, N 1-byte reads from `/dev/zero`,
N 1-byte writes to `/dev/null`, N `clock_gettime`, N `dup`+`close`, N/20 `stat` and N/20
`open`+`close` of a file six path components deep on `/tmp` (`stat_tmp`, `open_tmp`) and under
the `--volume` host directory `/bench` (`stat_host`, `open_host`), N/1000
`pthread_create`+`pthread_join` and N/1000 `posix_spawn`+`waitpid` of itself doing nothing. It
prints ns per call for each. The path walks measure per-component lookup cost, which on a host
directory means asking the host for every component; they decide whether the VFS needs an
attribute cache. The same source builds against libc2 (WASIX v2) and wasix-libc
(WASIX v1), so one wasmer can be compared across ABIs, and two wasmers on one ABI.

It is not part of the pass/fail suite: the runner never builds or runs it.

## Running

It needs `wasixcc` with `wasm32-wasixv2` support on `PATH` and both sysroots:

- v2: libc2's `build-wasixv2`, in `WASIXCC_SYSROOT_PREFIX`, as for the suite;
- v1: the wasix-libc release sysroot, `wasixccenv download-sysroot v2026-05-12.1` (with
  `WASIXCC_SYSROOT_PREFIX` unset), in `WASIX1_SYSROOT_PREFIX` (default
  `~/.wasixcc/sysroot-v2026-05-12.1`).

```sh
bench/syscalls/build.sh                              # sysbench-v2.wasm, sysbench-v1.wasm
bench/syscalls/run.sh <wasmer> <label> [N] [RUNS]    # both ABIs, RUNS times (default 5)
bench/syscalls/ab.sh <wasmer-a> <wasmer-b> v2 getpid [N] [RUNS]   # one call, two wasmers
```

Modules and results go to `$BENCH_OUT`, by default `target/bench/syscalls` of this repository:
`run.sh` writes the raw runs to `raw/<label>.txt` and the median and min..max per call to
`results/<label>.txt`. Use release builds of wasmer.

Timings are only as good as the machine is quiet. The runs are pinned to `CPUS` (default `6,7`,
away from the suite's 0-3) at nice 19, and `run.sh` records the load average and the number of
running cargo/rustc processes before and after. `ab.sh` interleaves the two binaries so that
drifting load hits both alike.
