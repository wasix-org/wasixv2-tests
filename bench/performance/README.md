# Performance workloads

These source-only benchmarks extend `../syscalls`. They are deliberately outside
the pass/fail suite. `micro.c` builds unchanged for native Linux, WASIX v1 and
WASIX v2. Every timed operation checks its result; an unsupported operation is
never recorded as a zero-cost success.

```sh
# Source the plan's env.sh, then select the libc2 build-wasixv2 sysroot prefix.
BENCH_OUT=/absolute/results bash bench/performance/build.sh
python3 bench/performance/run.py /absolute/release/wasmer --out /absolute/results
```

The runner pins CPUs 4–7 at nice 19, warms each module, alternates configuration
order, and records five measured runs. The JSONL contains commands, outputs,
return codes, load, active suite/compiler processes and wall time. Its summary
reports median/min/max **internal nanoseconds per operation**. CLI startup and
compilation are outside these intervals. Use `--backends cranelift` if LLVM was
not built. `--scale`, `--workloads`, `--runs`, `--label` and `--configs` permit
bounded repetitions and A/B runs. Preserve both raw JSONL and summaries.

| Workload | One operation |
| --- | --- |
| getpid, clock_gettime | One libc call; native clock may use the vDSO |
| pipe_small | One 1-byte write/read pair, same thread |
| file_small, file_bulk | One positioned write/read pair at offset zero, 1 byte / 64 KiB per direction |
| pipe_bulk, tcp_bulk | 64 KiB producer transfer to a validating receiver thread |
| fork_exit | fork, child exit, parent wait |
| fork_exec | fork, exec this binary in child mode, wait |
| vfork_exec | vfork, exec this binary in child mode, wait |
| thread | pthread_create + pthread_join |
| futex | One round trip of a two-thread atomic token, WAIT/WAKE_PRIVATE (v1's corresponding imports) |
| poll_many | poll 256 pipes, one readable, timeout zero |
| epoll_many | epoll_wait(maxevents=1) over 256 pipes, one readable |
| epoll_ready | epoll_wait(maxevents=1) over 256 readable pipes |
| tcp_connect | socket + loopback connect + one byte + close, peer accept/read/close |
| mmap | Map 64 KiB private anonymous, check zero endpoints, touch, unmap |

File setup and unlink happen before timing. Native files use `/dev/shm`; guests
use their `/tmp` tmpfs. File data remains resident and the same offset is reused:
this measures cached I/O, not disk or sequential file growth. Pipe throughput
may include different native/guest pipe capacities. Bulk timings include receiver
completion/join; setup and thread creation are outside the interval. Process
benchmarks include the loaded binary's image size; record it. EH cannot fork,
so those cases use asyncify. The runner measures both EH and asyncify for the
other v2 cases. Guest loopback stays within the guest network namespace.

`v1` means a v1-built program. When it runs on the v2 CLI, that is **v1 ABI on
the v2 kernel**, not the legacy v1 runtime; label these comparisons accurately.
For legacy-runtime comparisons run the same v1 binary with the old CLI separately.
No prebuilt Wasm belongs in this repository.
