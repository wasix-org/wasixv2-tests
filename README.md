# wasixv2-tests

The test suite for wasixv2: programs under `suite/`, and three upstream suites under `upstream/`,
built with [wasixcc](https://github.com/wasix-org/wasixcc) for `wasm32-wasixv2` against
[libc2](https://github.com/wasix-org/libc2), and run with
[wasmer](https://github.com/wasmerio/wasmer)'s `wasmer run`.

| Suite | Names | Source |
| --- | --- | --- |
| wasixv2's own tests | `<fixture>/<config>` | `suite/` |
| musl's [libc-test](https://repo.or.cz/libc-test.git) | `libc-test/<dir>/<test>` | `upstream/libc-test` |
| The Open POSIX Test Suite | `posix/<path>` | `testcases/open_posix_testsuite` in `upstream/ltp` |
| The [Linux Test Project](https://github.com/linux-test-project/ltp) | `ltp/<runtest list>/<tag>` | `upstream/ltp`: [wasix-org/ltp](https://github.com/wasix-org/ltp), branch `wasixv2` |

The upstream suites are submodules, so check out recursively:
`git submodule update --init --recursive`. LTP is wasix-org's fork, whose `wasixv2` branch adds
the few patches LTP needs here: it compares wasixv2 as newer than any kernel version, its
`__linux__` checks also accept `__wasixv2__`, and callbacks have prototypes matching their APIs.
The test library forks each test run as upstream does, sharing the IPC region with its parent. The upstream code keeps its own licenses
(LTP and the Open POSIX suite are GPLv2+), separate from this repository's.

This repository has no CI of its own. It is a submodule of wasmer (`tests/wasixv2`) and libc2
(`wasixv2/tests`), and each of them runs it against its own build:

- wasmer: `make test-wasixv2`, with the wasmer it builds
- libc2: `make wasixv2-test`, with the sysroot it builds

Many tests fail until wasixv2 is complete. `expected-failures/<suite>.txt` tracks them, so a run
is green when nothing regressed and nothing started passing silently. Upstream tests that cannot
make sense on wasixv2 are excluded instead; see below.

## Running

The runner builds and runs every test. It needs, from the environment:

| Variable | Meaning |
| --- | --- |
| `WASIXV2_TESTS_WASMER` | The `wasmer` binary under test (default: `wasmer` on `PATH`). |
| `PATH` | Must contain `wasixcc` and `wasix++` with wasixv2 support. The runner sets `WASIXCC_TARGET=wasm32-wasixv2`. |
| `WASIXCC_*` | wasixcc's own settings, such as `WASIXCC_LLVM_LOCATION` and `WASIXCC_SYSROOT_PREFIX`, apply as usual. Without them, wasixcc uses its downloaded defaults. Tests that fork build for the asyncify configuration (`WASIXCC_WASM_EXCEPTIONS=no`; see [Tests that fork](#tests-that-fork)), which has its own sysroot, so give libc2's sysroots with `WASIXCC_SYSROOT_PREFIX` (its `build-wasixv2`) rather than one with `WASIXCC_SYSROOT`. |
| `WASIXV2_TESTS_BUILD_DIR` | Where tests are built (default `target/suite`). |
| `WASIXV2_TESTS_TIMEOUT_SECS` | Baseline per-test timeout in seconds (default 60). LTP cases allow their declared runtime plus timeout, once per declared variant, with 30 seconds of launcher grace; the baseline remains a floor. |
| `WASIXV2_TESTS_WRAPPER` | A command line, split at whitespace, that every `wasmer` run goes through, e.g. `systemd-run --user --scope --quiet -p MemoryMax=4G` to cap each run's memory. Fork copies a process's whole linear memory, so tests that fork hundreds of processes (hackbench, `shm_open/23-1`) need many gigabytes; without a cap three of them at once can exhaust the host. |
| `WASIXV2_TESTS_EXPECTED_FAILURES` | A directory of expected-failures files to use instead of `expected-failures/`. |
| `WASIXV2_TESTS_BLESS` | Rewrite the expected-failures files from this run's results. |
| `HOSTCC` | The host C compiler that builds LTP's metadata parser (default `cc`). |
| `WASIXV2_TESTS_USERLAND` | A directory of v2 programs, mounted at `/bin` for every upstream test, with `/bin` on PATH. C tests use it for `system`, `popen`, and executed helpers too. Without it, LTP shell tests are skipped. |

LTP builds static libcap and libaio from the pinned `upstream/` sources for both compiler
configurations, enabling the capability and Linux AIO test bodies. libaio uses its libc syscall
fallback and ordinary static symbols on WebAssembly.

LTP's build needs Linux's UAPI headers (`<linux/*.h>`, `<asm/*.h>`, `<asm-generic/*.h>`). It
uses the sysroot's when the sysroot has them, as newer libc2 sysroots do. For an older sysroot,
the first run fetches them (v7.2, a shallow sparse clone) into the build directory, and only
LTP's build sees them, with `asm/unistd.h` replaced by libc2's syscall numbers. Building LTP
takes a few minutes, once per LTP commit, and its programs that fork once more for asyncify.

```sh
cargo run --release -- [FILTER] [--exact] [--list] ...
```

Arguments are those of the standard Rust test harness. A failure report names its cause, and for
a test that fails at run time, the syscalls the kernel answered with `ENOSYS` (from a rerun with
`RUST_LOG=wasixv2_trace=trace`) and the last syscalls made.

## Tests that fork

libc2 has two build configurations, each with its own sysroot: exception handling, wasixcc's
default, in which fork fails with `ENOSYS`, and asyncify (`WASIXCC_WASM_EXCEPTIONS=no`), in which
fork returns twice. Tests that fork build for asyncify and everything else for exception handling.

- Fixtures say so with `//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no`.
- Upstream tests are built for exception handling first. A program that forks is built again
  for asyncify: one that contains libc2's `_Fork`, which fork, daemon, forkpty and vfork without
  libc2's macro go through; libc2's `clone()`, which forks without `CLONE_VM` (LTP's
  `ltp_clone`); or that calls `syscall` with `SYS_clone` or `SYS_clone3` as the number, a raw
  clone, which forks (LTP's `SAFE_CLONE` and its namespace helpers `tst_ns_create` and
  `tst_ns_exec`). Threads take none of these ways: `pthread_create` starts them through libc2's
  internal `__clone`. The linker drops what nothing calls, so this catches forks in helper libraries (LTP's `SAFE_FORK`,
  `create_sig_proc`, ...) and nothing else; the name section tells, so builds must keep it (they
  do by default). libc-test and Open POSIX tests are rebuilt one by one. LTP's forking programs
  are listed in `wasixv2-forking-*.txt` in its build tree, built in a second tree configured for
  asyncify (`ltp-<commit>-asyncify`) and copied over their exception-handling builds in the
  install, so helpers that a test executes fork too. Their build logs end in `.asyncify.log`. A
  tree prepared before the detection changed is completed in place.

## Expected failures

`expected-failures/<suite>.txt` (`wasixv2`, `libc-test`, `posix`, `ltp`) lists failing tests under
`## <cause>` headings. A listed test that fails passes the run, and is shown as `[xfail]`. A listed
test that passes fails the run: take it off the list. A name that matches no test fails the
`expected-failures` check.

After changing wasmer or libc2, regenerate the lists with `WASIXV2_TESTS_BLESS=1` and review the
diff. Tests that the run did not include keep their entries.

## Excluded tests

Upstream tests that cannot make sense on wasixv2 are not registered at all, and a run prints how
many were excluded for each reason. Expected failures are for tests that make sense and do not
work yet; exclusions are for tests that never will, such as tests that need block devices or real
filesystems (the module owns its whole filesystem), huge pages, cgroups, kernel modules or a
particular CPU.

- LTP tests are excluded by the requirements their metadata declares (`needs_device`,
  `needs_hugetlbfs`, ...), see `metadata_reason` in `src/upstream/ltp.rs`. Shell tests declare theirs
  in `TST_*` variables (`TST_NEEDS_DEVICE=1`, ...), which count the same. Tests that need root
  (`needs_root`, a `.caps` declaration, `TST_NEEDS_ROOT=1`, or `tst_require_root` in the old APIs) are not excluded:
  they run as uid 0. Tests that need kernel options (`needs_kconfigs`) run when the kernel has
  every one, which only namespaces' options (`CONFIG_PID_NS`, `CONFIG_IPC_NS`, `CONFIG_UTS_NS`,
  `CONFIG_NET_NS`) and `CONFIG_DNOTIFY` can be; LTP reads them from a kernel configuration the
  runner gives it.
- LTP shell tests on the C test library (`tst_loader.sh`) are excluded: the library shares memory
  with the shell it runs, and processes on wasixv2 cannot share memory.
- Whole LTP runtest lists about hardware or kernel internals are excluded by `EXCLUDED_LISTS` in
  the same file; every list is either included or excluded with a reason.
- `excluded.txt` names the rest, each with a `# reason`; a pattern ending in `*` matches a prefix.

## Flaky tests

`flaky.txt`, in the same format, lists tests that pass or fail from run to run. They are reported
as ignored and not run, so they neither fail the run nor land in `expected-failures/`. Fix the
cause, then take the test off the list.

## Results of upstream tests

| Suite | Pass | Skip | Failure cause |
| --- | --- | --- | --- |
| libc-test | exit 0; api tests: compiles | | its first line of output |
| Open POSIX | `PTS_PASS`; `-buildonly` tests: compiles | `PTS_UNSUPPORTED`, `PTS_UNTESTED` | `FAIL` or `UNRESOLVED` and the last line of stdout |
| LTP | exit 0 | exit 32, only `TCONF`; shell tests without `WASIXV2_TESTS_USERLAND` | the first `TFAIL` or `TBROK` line |

LTP tests run with LTP's install mounted at `/opt/ltp`, `LTPROOT=/opt/ltp` and its
`testcases/bin` on `PATH`, so tests can execute their helper programs. They run as `wasmer run`'s
default user, except tests that need root, which run as uid 0 (`wasmer run --user 0:0`).
The Open POSIX suite always runs as uid 0, so its scheduling and resource-limit assertions have
the capabilities they require. LTP always gets `KCONFIG_PATH`, including when setup queries
configuration without declaring `needs_kconfigs`.

### LTP shell tests

A runtest line is a shell test when its program is a shell script, or when only a shell can run
it (`;`, pipes, `$VAR`, redirections). Shell tests run the whole line as
`bash -c 'set -m; <line>; exit $?'`, as LTP's runners run every line with `sh -c`, in a v2
userland mounted at `/bin`: GNU bash, coreutils, grep, sed, gawk, findutils, diffutils and which,
built for `wasm32-wasixv2`. LTP's shell library (`tst_test.sh`, ...) and the helper programs it
runs (`tst_timeout_kill`, ...) are part of LTP's install, from `testcases/lib`. `set -m` puts the
test in a process group of its own, as LTP's runners do: the shell library's timer signals the
test script's process group, and refuses to signal pid 1.

`WASIXV2_TESTS_USERLAND` is the userland's directory of programs. The runner does not build the
userland, which takes a while and needs host autotools, and this repository holds no binaries.
The build scripts are in wasmer's `lib/wasixv2/plan/userland` (branch `wasixv2/plan`); they
build against libc2's asyncify sysroot (`WASIXCC_SYSROOT_PREFIX` as above):

```sh
cp -r lib/wasixv2/plan/userland ~/userland   # the scripts build next to themselves
~/userland/build-all.sh bash coreutils grep sed gawk findutils diffutils which
WASIXV2_TESTS_USERLAND=~/userland/out/bin cargo run --release -- ltp/
```

## Fixtures

Each directory under `suite/` with a primary source is a fixture: shell scripts (`build.sh`, or
any `*.sh`, each a separate test), a `Cargo.toml`, `main.c`, `main.cpp`, or `*.rs` files (each a
separate test), in that order of precedence. Every build must produce `main`.

- `main.c` / `main.cpp` are built with `wasixcc` / `wasix++`. C++ needs libc2's sysroots with libc++
  (both have it; the asyncify one is built without exceptions). A fixture picks its language
  standard with `//#BuildEnv: WASIXCC_COMPILER_FLAGS_CXX=-std=c++20`.
- Shell scripts run with `CC=wasixcc`, `CXX=wasix++` and `WASIXCC_DISCARD_UNSUPPORTED_FLAGS=yes`.
- Rust fixtures fail: there is no Rust toolchain for `wasm32-wasixv2` yet.

Directives use the syntax of wasmer's WASIX v1 suite: `//#Directive: args` in C, C++ and Rust,
`##Directive: args` in shell scripts and `Cargo.toml`.

| Directive | Meaning |
| --- | --- |
| `Config:{name}[:{parent}]`, `AbstractConfig:...` | Starts a configuration, optionally inheriting from an earlier one. Abstract ones are not run. |
| `AbstractConfigFile:{path}` | Reads directives from a file. |
| `Args:{args}`, `Env:{k}={v}`, `Stdin:{text}`, `StdinFile:{path}` | Command line, environment and stdin. |
| `ExpectedStdout:{line}`, `ExpectedStderr:{line}`, `...File:{path}` | Expected output, compared line by line after trimming. |
| `ExpectedExitCode:{code}`, `MustFail:{bool}` | Expected exit status. |
| `BuildEnv:{k}={v}` | Environment for the build. |
| `MappedDirectory:{host}:{guest}`, `CurrentDirectory:{path}` | Passed to `wasmer run` as `--volume` and `--cwd`. A relative host path is resolved in the test's build directory; `$temp` is a fresh directory. |
| `PrefilledFile:{path}:{text}`, `ExpectedFile:{path}:{text}` | A file in the build directory written before the run, or checked after it. |
| `ProgramName:{name}` | argv[0]. `wasmer run` cannot set it, so these tests fail. |
| `Ignored:{reason}`, `UnixOnly:{bool}`, `IgnoreMacOS:{bool}`, `SkipEngine:cranelift:{reason}` | Skip the test. |
| `MinimalLibc`, `FileSystems`, `Toolchains`, `DefaultMappedDirectories` | Accepted for compatibility with the v1 suite's syntax, and ignored. |

The runner does not map the build directory into the guest: a test sees only what its directives
map.
