# Browser adapter

The first lane is **SharedArrayBuffer + cross-origin isolation, no JSPI**. This is
an initial harness and fixture export, not browser parity signoff. The browser
executor and its debug artifact recipe belong to Wasmer B2; B8 supplies the
fixture embedding adapter. No executor or syscall is implemented here.

## Run

Use Node 22 or newer. `npm ci` pins Playwright and its Chromium revision via
`package-lock.json`; `npx playwright install --with-deps chromium` installs that
revision. `CHROME=/path/to/chromium` explicitly overrides the binary for local
work; capabilities.json records the actual browser version. CI uses the pinned
Playwright browser. These commands use new output directories to prevent stale
success artifacts from surviving failures.

```sh
cd browser
npm ci
npx playwright install --with-deps chromium
npm test
node run.mjs --smoke --artifacts artifacts/transport
```

`--smoke` compiles a tiny WAT source at runtime with wabt and executes it in a
worker against a shared guest memory. It proves transport, isolation, capability
reporting and results only. It is marked `infrastructureOnly: true`; it proves no
WASIX syscalls. No generated Wasm is committed.

Build B2's runtime using **its** Wasmer recipe, then reuse its assets unchanged:

```sh
# In Wasmer (after B2 merges); requires its pinned nightly + rust-src and a
# wasm-bindgen CLI matching Wasmer's Cargo.lock. The recipe is debug only.
CARGO_TARGET_DIR=/absolute/task-target bash lib/wasixv2/host/tests/browser/build.sh
# In this repository:
CHROME=/usr/bin/google-chrome node browser/run.mjs --smoke --b2-smoke \
  --runtime /absolute/task-target/browser --artifacts /absolute/new-b2-results
```

With wasixcc and the libc2 sysroots configured as for the native runner:

```sh
# Does not discover/build upstream suites, need wasmer, or consult native xfails.
cargo run -p wasixv2-tests --locked -- --browser-export /absolute/new-export
# The runtime directory contains B2 artifacts plus B8 fixture-adapter.js.
node browser/run.mjs --manifest /absolute/new-export/manifest.json \
  --runtime /absolute/browser-runtime --artifacts /absolute/new-results
# Combined build/export/run mode (arguments after export directory go to Node):
cargo run -p wasixv2-tests --locked -- --browser-run /absolute/new-export \
  --runtime /absolute/browser-runtime --artifacts /absolute/new-results
```

On the shared implementation machine prefix builds/tests with
`CARGO_INCREMENTAL=0 nice -n 19 taskset -c 4-7`, pass Cargo `-j3`, use a task-owned
`CARGO_TARGET_DIR`, and remove that target when done. Do not run the full suite.
The export is serial and uses the existing fixture compiler, including each
fixture's asyncify setting. `WASIXV2_TESTS_BUILD_DIR` and
`WASIXV2_TESTS_TIMEOUT_SECS` retain their native meanings. A build/metadata failure
is written to the selected test's `build.log` and manifest and exits nonzero;
combined mode still runs the adapter to collect all selected results.

## Reviewed selection

`allowlist.txt` names **21 exact configurations in 17 existing fixture families**.
No glob selection, expected-failure blessing or automatic capability filtering is
used. A stale/duplicate name, empty selection or unknown directive fails. Browser
export also rejects Ignored, host-only flags, SkipEngine and native-only
compatibility directives instead of silently discarding them.

| Fixtures | Why included / dependencies |
| --- | --- |
| base-args-env | argv/env/auxv; no host mount |
| base-exit-status (all five configs) | exit and output flushing; preserves nonzero expected statuses |
| base-getrandom | browser entropy, including large requests |
| base-clocks | clock/error coverage; CPU time uses runtime active-wall accounting, not native CPU precision |
| base-nanosleep, base-timerfd | off-worker timers, poll/readiness, no early success |
| fs-rw-pread, fs-readdir | memfs I/O, cookies, cwd; all files created under guest /tmp |
| fs-rename, fs-link-symlink, fs-openat2-in-root | VFS validation and containment, no native filesystem |
| mmap-file-private | copied private file mappings and growth |
| mmap-file-shared-msync | same-process copied mappings at msync/I/O/unmap synchronization points; does not require coherent separately mapped host pages |
| mmap-brk-layout | asyncify fork, independent private heap and pthread unwind buffers |
| thr-atomic-wait-signal | real instruction waits/futex interoperability, signals, join and blocked-thread exit |
| proc-fork | asyncify fork/wait/SIGCHLD, shared open-file offsets and pipe data |
| proc-execve | snapshotted `.:/t` mount includes its built main; exec replaces args/env and preserves pid |

These expectations stay those of the native source fixtures. Linux authorities
for the behaviors exercised are `kernel/exit.c:do_exit`,
`fs/exec.c:begin_new_exec`, `kernel/fork.c:copy_process`,
`kernel/futex/waitwake.c:futex_wait_setup,futex_wake`,
`kernel/time/hrtimer.c:hrtimer_nanosleep`, `fs/namei.c:link_path_walk`,
`fs/read_write.c:vfs_read,vfs_write`, and `mm/msync.c:SYSCALL_DEFINE3(msync)`.

Mandatory parity coverage still to add after driver integration: vfork/exec
handoff, retained/foreign-memory lifetime, provider-backed shared directory
identity, relay TCP/UDP/DNS and deterministic disconnects, TTY Ctrl-C/resize/hangup,
worker admission/death, and stream backpressure/drain. Their absence is an open
acceptance item, not a skip or expected failure. Use existing proc/thr/sig/net/tty
fixtures when metadata review permits them. No source fixtures were duplicated.

Platform limits stay separate: native host directory paths need browser provider
mounts (`fs-mapped-dir`); host port publication/host OS proc require explicit
providers; native mprotect traps, coherent MAP_SHARED across separate guest
memories, and native shared-page AIO/io_uring rings are unavailable in this lane.
No native expected-failure lists are changed to disguise those limits.

## Manifest and B8 merge point

Version 1 manifest: `{version: 1, lane: "sab-isolated-no-jspi", tests: [...]}`.
Each test has name, export status, assets path, `main.wasm`, timeoutMs, args,
env (ordered key/value pairs), stdin bytes, cwd/user/programName (nullable),
buildEnv, mounts and expected exit/output/files. Null cwd/user/programName mean
the embedding's normal defaults; default credentials must match native v2
(uid/gid 1). Do not substitute worker completion for final process exit.

A mount is `{guest, entries}`. Each entry has relative UTF-8 path (empty for root),
kind (`directory`, `file`, `symlink`), mode bits, file data bytes or symlink target.
Symlinks are preserved, never followed on the export host. `$temp` becomes an
empty 0700 directory. Absolute host mounts, escaping paths, special files and
unmapped ExpectedFile paths fail export. Guest paths retain VFS semantics.
B8 materializes snapshots through its provider/VFS APIs, preserving files/modes
and mount isolation. This exports initial contents; it does not model shared
live host-directory identity.

The adapter imports `/runtime/fixture-adapter.js`, supplied by B8 alongside B2's
runtime assets. Its only required export is:

```js
export async function runFixture(test, assetsBaseUrl) {
  // Assemble a fresh Machine with the B2 executor; use test metadata without
  // changing guest expectations. All Promise-based I/O runs off task workers.
  // Await final process exit, output drain and normal worker/resource shutdown.
  return { exitCode, stdout: byteArray, stderr: byteArray,
           files: [{ path: "/guest/expected-file", data: byteArray }] };
}
```

Errors reject; `error` or `truncated` results fail. stdin is closed after its
bytes. Empty expected output means no output assertion, matching native checks;
otherwise compare trimmed lines. Expected files require exact bytes after exit.
An unavailable B8 module produces an explicit **B8 merge point** failure for each
selected case. Runtime code belongs to B2/B8; do not implement an alternate
executor in this adapter. The runner validates each real guest module before
invocation so unsupported Wasm features fail visibly.

The server binds only 127.0.0.1 and applies COOP same-origin, COEP require-corp and
CORP same-origin to success/error responses. It confines files to registered
roots. It removes JSPI APIs before every served JS module, including worker
modules, then probes actual API absence on the page and a worker. V8 feature flags
alone did not disable shipped JSPI on local Chromium 151. Capabilities include
secure context, isolation, SAB, shared Wasm memory, Atomics and actual worker
blocking wait; missing required capabilities fail. Each test gets a fresh browser
context, closed on success/failure/timeout to reclaim dedicated workers.

Artifacts: capabilities.json, aggregate results.json, numbered per-test JSON,
console/request errors, raw stdout/stderr, input manifest and source WAT for smoke.
Console capture is bounded with an explicit truncation marker; guest output is
bounded and truncation fails. Exported build logs live in the export bundle.
An empty run never passes; startup failure records every selected test as failed.

## CI integration

Wasmer's B8 facade now supplies `fixture-adapter.js` through
`lib/wasixv2/api/tests/browser/build.sh`, which reuses the B2 debug/std/TLS recipe.
The runtime bundle also includes B4's timer event worker and C1's pure-JS copy
worker. The adapter waits for final kernel exit, descendant shutdown, bounded
stdio drain and normal worker/TLS retirement. It uses byte-backed module sources,
B6 provider mounts and fixture args/env/cwd/credentials; no JSPI APIs are required.

The B8 workflow replaces dispatched, allowed-to-fail fixture consumption with an
automatic required `fixtures` job depending on a source-built runtime and a
source-built reviewed export. The exporter job uses Wasmer's pinned
`api/tests/browser/toolchain.env` and isolated debug toolchain producer (LLVM,
wasixcc, libc2 EH/asyncify C sysroots and Binaryen). The ABI pin is verified before
building. A cold source-toolchain job is intentionally separate from the faster
runtime/adapter checks; subsequent runs cache it by exact pins and recipe.
If any pinned source repository requires private access, configure
`WASIXV2_TOOLCHAIN_TOKEN` with read access to those repositories. Missing tools,
credentials, fixture builds and runtime capabilities fail the job; none are
converted to skips or expected failures. No prebuilt guest Wasm is checked in.
