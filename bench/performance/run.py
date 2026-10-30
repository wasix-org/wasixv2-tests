#!/usr/bin/env python3
"""Interleaved microbenchmarks; JSONL is the source of every summary statistic."""
import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess
import time

WORK = {
    "getpid": (200000, 1), "clock_gettime": (200000, 1),
    "pipe_small": (20000, 1), "file_small": (10000, 1),
    "pipe_bulk": (1000, 65536), "file_bulk": (1000, 65536),
    "fork_exit": (30, 1), "fork_exec": (10, 1), "vfork_exec": (20, 1),
    "thread": (100, 1), "futex": (2000, 1),
    "poll_many": (1000, 256), "epoll_many": (10000, 256),
    "epoll_ready": (10000, 256),
    "tcp_bulk": (1000, 65536), "tcp_connect": (200, 1), "mmap": (2000, 65536),
}


def activity():
    """Record machine contention, including suite processes, without a shell."""
    names = []
    for p in Path("/proc").glob("[0-9]*/comm"):
        try:
            name = p.read_text().strip()
            if name in ("cargo", "rustc", "wasixv2-tests", "wasmer"):
                names.append(name)
        except (OSError, ProcessLookupError):
            pass
    return {"load": os.getloadavg(), "processes": names}


def summarize(rows):
    groups = {}
    for row in rows:
        if row["status"] == "ok" and not row["warmup"]:
            groups.setdefault((row["config"], row["workload"]), []).append(row["ns"])
    return [{"config": c, "workload": w, "runs": len(v),
             "median_ns": statistics.median(v), "min_ns": min(v), "max_ns": max(v)}
            for (c, w), v in sorted(groups.items())]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("wasmer", type=Path)
    p.add_argument("--out", type=Path, required=True, help="directory containing built micro binaries")
    p.add_argument("--label", default="baseline")
    p.add_argument("--runs", type=int, default=5)
    p.add_argument("--backends", nargs="+", default=["cranelift", "llvm"])
    p.add_argument("--configs", nargs="+", default=["native", "v2-eh", "v2-async", "v1"])
    p.add_argument("--workloads", nargs="+", choices=WORK, default=list(WORK))
    p.add_argument("--cpus", default="4-7")
    p.add_argument("--scale", type=float, default=1.0)
    args = p.parse_args()
    assert args.runs > 0 and args.scale > 0
    out = args.out.resolve()
    wasmer = str(args.wasmer.resolve())
    configs = [("native", "native")] if "native" in args.configs else []
    configs += [(c, b) for b in args.backends for c in args.configs if c != "native"]
    rows = []
    with (out / (args.label + ".jsonl")).open("w") as raw:
        for name in args.workloads:
            count, size = WORK[name]
            for iteration in range(args.runs + 1):
                # Alternate order to distribute drift between configurations.
                for config, backend in configs[::1 if iteration % 2 == 0 else -1]:
                    if name.startswith("fork_") and config == "v2-eh":
                        continue  # fork requires asyncify, by design
                    binary = out / ("micro-native" if config == "native" else f"micro-{config}.wasm")
                    n = 1 if iteration == 0 else max(1, int(count * args.scale))
                    command = [str(binary), name, str(n), str(binary), str(size)]
                    env = dict(os.environ)
                    if config == "native":
                        env["BENCH_TMPDIR"] = "/dev/shm"
                    else:
                        command[3] = f"/bench/{binary.name}"
                        command = [wasmer, "run", "--net", f"--{backend}", "--compiler-threads", "3", "--no-tty",
                                   "--volume", f"{out}:/bench", *command[:1], "--", *command[1:]]
                    command = ["nice", "-n", "19", "taskset", "-c", args.cpus, *command]
                    row = {"config": f"{config}/{backend}", "workload": name,
                           "warmup": iteration == 0, "iteration": iteration,
                           "command": command, "before": activity(), "timestamp": time.time()}
                    start = time.monotonic()
                    try:
                        r = subprocess.run(command, env=env, capture_output=True, text=True, timeout=90)
                        row.update(returncode=r.returncode, stdout=r.stdout, stderr=r.stderr)
                        row["status"] = "ok" if r.returncode == 0 else "unsupported" if r.returncode == 77 else "failed"
                        if row["status"] == "ok":
                            fields = r.stdout.strip().split()
                            assert len(fields) == 4 and fields[0] == name, r.stdout
                            row.update(ns=float(fields[1]), bytes=int(fields[2]), operations=int(fields[3]))
                    except subprocess.TimeoutExpired:
                        row["status"] = "timeout"
                    row.update(wall_seconds=time.monotonic() - start, after=activity())
                    rows.append(row)
                    raw.write(json.dumps(row) + "\n")
                    raw.flush()
            print(name, {s: sum(r["status"] == s for r in rows if r["workload"] == name)
                         for s in ("ok", "failed", "timeout", "unsupported")}, flush=True)
    (out / (args.label + "-summary.json")).write_text(json.dumps(summarize(rows), indent=2) + "\n")


if __name__ == "__main__":
    main()
