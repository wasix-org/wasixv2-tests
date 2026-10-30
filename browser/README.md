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

## WASIX v1 compatibility

`--browser-v1-export DIR` and `--browser-v1-run DIR` select only `v1-allowlist.txt`
from `browser/v1/`. These source WAT fixtures use the ordinary shell compiler,
configuration parser, manifest and B8 facade. Binaryen `wasm-as`/`wasm-opt` must
be on PATH. Their explicit `BuildEnv: WASIXCC_TARGET=wasm32-wasix` permits legacy
imports without weakening the v2 build check. No prebuilt wasm is tracked.

The fourteen required cases cover static output, preopens, shared-memory threads/futexes,
detached tty defaults, spawn/file actions, vfork/exec, copied-memory fork, loopback
sockets, unavailable HTTP capability, active v1 console mode changes, file-identity
command defaults with argv/env, and HTTP through B8's independent I/O worker.
An explicit unreachable fixture must exit with SIGILL (132).

`BrowserConsole` and `BrowserHttp` are boolean fixture metadata;
`BrowserCommandDefaults` passes typed JSON annotations to the existing file-identity
API. `BrowserHttp` grants a Fetch provider and injects HTTP_BASE pointing at an
ephemeral cross-origin mock: binary GET/POST, readable status500, CORS denial,
connection failure and timeout/abort. The runner removes JSPI APIs from every
served module as in the v2 lane; no fixtures are skipped or expected failures
accepted. Linux behavior remains in the v2 kernel (`kernel/fork.c:copy_process`,
`drivers/tty/n_tty.c:n_tty_set_termios`, `net/socket.c:__sys_socket`); HTTP layouts
follow the historical WASIX v1 WAI binding.

## Browser matrix and fork stress

Select a required no-JSPI lane with `--browser chromium|firefox|webkit` (default
Chromium). Install the lockfile-pinned engines with
`npx playwright install chromium firefox webkit` from this directory. `CHROME`,
`FIREFOX` and `WEBKIT` optionally select explicit executables for their matching
engine. Browser name/version, page/worker capabilities and every failure remain
in each run's artifacts; unsupported capabilities fail the lane.

```sh
node browser/run.mjs --browser firefox --manifest /exports/manifest.json \
  --runtime /runtime/bundle --artifacts /results/firefox
node browser/run.mjs --browser webkit --manifest /exports/manifest.json \
  --runtime /runtime/bundle --artifacts /results/webkit
node browser/run.mjs --manifest /exports/manifest.json --runtime /runtime/bundle \
  --filter proc-fork/ --repeat 100 --artifacts /results/fork-stress
```

Filtering uses a literal name substring and an empty selection fails. Repetition
preserves all fixture metadata and expectations; each iteration gets a fresh
browser context/runtime heap and distinct artifacts. Stress therefore exercises
worker startup, fork and complete TLS/worker retirement each time. Use ordinary
same-runtime lifecycle tests separately to verify worker reuse.

`--fresh-browser` starts a distinct browser process for each case and records
`isolation: process-per-case` in capabilities.json. The default uses a distinct
context per case. This is useful when diagnosing engine process/context reuse:
run both modes and retain failing results rather than treating process isolation
as proof that repeated-context lifecycle works. Local Playwright WebKit 27.2
showed repeated-context crashes even in transport-only smoke; its WASIX lane is
therefore additionally exercised with explicit process isolation.

## R1/R3 package, filesystem and terminal acceptance

`--browser-accept-export DIR` selects the 15 exact configurations in
`accept-allowlist.txt`. It combines new source fixtures under `browser/accept`
with the existing `tty-pty`, `tty-pty-jobctl` and `tty-mini-shell` fixtures;
it does not build any other suite fixture. Run its manifest through `run.mjs`
with the B8/B12 facade artifact, using the same no-JSPI capability checks.
`--browser-accept-run DIR` exports then invokes that runner, like the other modes.

`BrowserAcceptance` carries explicit example-only inputs: `packageVersion` (2/3),
optional named `command`, `liveVfs`, and ordered `actions`. Each action waits for
its nonempty `after` output marker and may send bounded input bytes, resize the
console (`[rows, cols, xpixel, ypixel]`), hang up, close input, or perform the live
VFS scenario. Complete marker lines use one guest write; echo stays entirely in
the kernel. Every action must execute before EOF, otherwise the fixture fails.

The package case source-builds its guest, then the browser constructs and parses
real WEBC bytes (v3 or migrated v2), installs supplied dependencies and extra
packages, prepares a default/named command and executes its VFS path. It checks
annotations, explicit cwd/environment precedence, extra-command collision
precedence, subdirectory containment and writable package overlays. Missing
commands/dependencies and invalid bytes fail explicitly. Acquisition/auth/CORS,
persistent package caches and SDK JavaScript wrappers remain separate work.

Live VFS acceptance keeps a guest handle open while the embedder writes, renames,
unlinks and replaces its pathname using explicit VFS credentials. Both sides
check the old/new inode contents; foreign credentials return EACCES, a readonly
mount EROFS, and a test provider returns EIO for lookup/read. The provider wrapper
is test-only and uses the existing Provider protocol and memory driver.

Console fixtures check canonical echo/erase, Ctrl-D followed by more input,
raw control bytes without echo, blocked/busy SIGWINCH with every winsize field,
background SIGTTIN then foreground Ctrl-C, caught hangup and default SIGHUP under
read/busy guests. Input close is persistent hangup, distinct from terminal Ctrl-D.
Linux `drivers/tty/tty_io.c:__tty_hangup` changes file operations before signalling
the session, so an EOF racing ahead of SIGHUP must still wait for the signal.
The reused PTY/shell fixtures add TOSTOP/SIGTTOU, stop/continue and foreground swaps.
EOF, output and guest exit expectations use the existing strict result checker;
there are no browser expected failures or skips.

## Deterministic browser networking

`--browser-net-export DIR` / `--browser-net-run DIR` reuse the fixture compiler,
metadata checker and ordinary B8 facade for `net-allowlist.txt` (28 cases under
`browser/net`). Set the usual source compiler/sysroot environment, then run:

```sh
cargo run -p wasixv2-tests -- --browser-net-export /exports/network
CHROME=/usr/bin/google-chrome node browser/run.mjs --manifest /exports/network/manifest.json \
  --runtime /runtime/bundle --artifacts /results/network
node browser/run.mjs --browser firefox --manifest /exports/network/manifest.json \
  --runtime /runtime/bundle --artifacts /results/network-firefox
```

`BrowserNetwork` is an explicit profile (`classic`, `fidelity`, `absent`,
`refused`, `disconnect`, `overflow`, `ingress`, `peer`, `ephemeral`, `switch4`,
`switch6`). The runner starts fresh local services for relay cases, injects
ephemeral service ports and a local relay URL,
and retains service counts/maxima in each result. Unknown profiles fail export
and validation. The required lane still removes JSPI in page and all workers.
No external DNS or internet is used. Linux and Python 3 provide the deterministic
urgent host service; its two localhost TCP sockets use real MSG_OOB/SO_OOBINLINE
and SIOCATMARK. There are no prebuilt Wasm fixtures.

Coverage: IPv4/IPv6 classic TCP and UDP (including empty datagrams, peek and
truncation), deterministic A/AAAA/PTR/raw TXT DNS, FIN with a writable reverse
half and confirmed remote delivery, reset, actual host UDP-send error with a
usable endpoint afterward, urgent mark/peek/POLLPRI, bounded nonblocking writes
and ingress overflow, no provider, peer refusal, transport refusal/disconnect,
TCP+UDP publication with reply source-port verification and two distinct
Machines/Nets exchanging peer bytes via an explicit relay publication. Four
switch cases instead create two separate Machines on one in-runtime switch,
with no relay/host network service: IPv4/IPv6 TCP+UDP, exact guest source tuples,
same guest ports, loopback privacy, half-close and next-I/O UDP refusal.
The ephemeral case requests TCP+UDP port zero and verifies real ingress/reply
source ports after classic GetAddrLocal completion. Six ICMP cases use actual
Linux connected UDP host sockets to closed loopback ports: connected refusal on
next send/recv, unconnected suppression and RECVERR quotes on both IP families.
Refusal before flow creation returns ENETDOWN; a flow already accepted by a
failed transport returns ECONNABORTED. Neither is silently treated as peer RST.

The matching B16 runtime must expose the switch, resolved publication API and
opt-in destination-qualified classic-fidelity ICMP record. The deterministic
relay reports actual native ECONNREFUSED with the quoted destination/payload;
local send failures remain a separate event. These tests do not implement SDK
service-worker HTTP/Ports/WISP wrappers or change stock relay deployments.
Exact provider records and production follow-ups are documented in Wasmer's
vnet-web/RELAY-FIDELITY.md. Both Chromium and Firefox run these 28 configurations
with JSPI absent; no native expected-failure baseline is imported.

## Persistent runtime lifetime

`lifetime.mjs` uses the same B11 capability checks, isolated server, source export
and expectation checker as `run.mjs`, while keeping **one** B8 runtime heap and
coordinator alive across every iteration. It prewarms a bounded eight-task pool,
records worker count, copy-service retirements, shared heap size and UI heartbeat,
and requires all workers to disappear after normal or injected fatal shutdown.
Chromium on Linux also records renderer RSS and virtual address reservations via
its process IDs. RSS collection dips are reported separately from later peak
growth; these numbers include browser/JIT costs, not just the shared Rust heap.
After iteration 25 the debug-artifact gates allow a 16 MiB shared-heap range
(one additional dense fork-copy staging allocation), require a fixed worker
pool, cap RSS at 2 GiB, and bound the
virtual-address range to 16 GiB. Dense browser fork copies include the 8 MiB
guest stack and asyncify/data; shared-memory allocator high-water cannot shrink.
These explicit allowances cover the large debug module and JIT while rejecting
per-run heap growth. Preserve reports from failed budgets when comparing runs.
RSS later-peak growth remains diagnostic: a previous 512 MiB allowance relative
to two warmup samples failed during bounded collection cycles; those failed
reports are retained. The fixed 2 GiB ceiling has not been raised.
Actual worker targets are also bounded by the Rust pool plus one retiring realm
per task slot, the coordinator and up to two copy realms. A 500-run fork lane
requires both copy and guarded task retirements to have actually occurred.

The Chromium lane counts browser-wide CDP worker targets, including unattached
targets, and checks a positive live inventory before requiring zero after shutdown.
Detached Playwright handles never establish reclamation. A separate raw-CDP
main-frame-only control still reproduced the lingering coordinator without any
attached worker debugger; this is not classified as a debugger-only failure.
Keep failed shutdown reports visible. Firefox retains its worker lifecycle inventory.

Reduce shutdown without executing runtime Rust using the same source-built assets:

```sh
node browser/worker-close.mjs /runtime /new/close-control chromium
node browser/worker-close.mjs /runtime /new/close-control-firefox firefox
```

Both empty and large compiled-Module controls require zero remaining workers;
the latter only transfers the Module to fake boot/retire workers. A failing
control remains a failure and does not establish a kernel fault or an engine fix.

```sh
CHROME=/usr/bin/google-chrome node browser/lifetime.mjs \
  /runtime /export/manifest.json /new/results 500 chromium
node browser/lifetime.mjs /runtime /export/manifest.json /new/firefox 60 firefox
CHROME=/usr/bin/google-chrome node browser/lifetime.mjs \
  /runtime /export/manifest.json /new/continuous 1 chromium proc-fork-lifetime/default
```

The source `proc-fork-lifetime` fixture also forks and reaps 500 children inside
one continuing guest parent, checking memory isolation and `ECHILD` after reap.
The existing `proc-fork/default` fixture creates ten children/grandchildren per
iteration; the 500-iteration lane exercises 5,000 copied address spaces.

With the matching facade example, append `proc-fork/default lifetime_smoke` to
exercise 2,048 canceled waits, 1,000 timer cancellations, 1,000 notify-before-wait
parks and 16 canceled/deadline/concurrent kills per iteration. Use
`proc-fork/default admission_smoke` for coordinator capacity rejection and exact
closure destruction. Append `bootstrap`, `task` or `timer` as the final argument
for real worker failure injection; only the named injected error plus complete
worker reclamation counts as success. `throttled` delays timer callbacks by
50 ms and must pass normal deadline/no-early-completion assertions.

The embedding repository's `lib/wasixv2/api/tests/browser/relay-lifetime.mjs`
reuses its existing deterministic relay fixture with this runner's `relay_smoke`
entry and explicit arguments. It checks 50 repeated connections/sends in one
runtime, exact empty process outputs and zero workers after I/O retirement.

No GC API or browser GC flag is used in these acceptance lanes. Keep the JSON
from failed resource gates; completion alone does not establish bounded memory.
