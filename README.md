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
the few patches LTP needs here: its test library runs each test in its own process instead of a
forked child sharing memory with the parent, it compares wasixv2 as newer than any kernel version,
and its `__linux__` checks also accept `__wasixv2__`. The upstream code keeps its own licenses
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
| `WASIXCC_*` | wasixcc's own settings, such as `WASIXCC_LLVM_LOCATION` and `WASIXCC_SYSROOT_PREFIX`, apply as usual. Without them, wasixcc uses its downloaded defaults. Tests that fork build for the asyncify configuration (`//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no`), which has its own sysroot, so give libc2's sysroots with `WASIXCC_SYSROOT_PREFIX` (its `build-wasixv2`) rather than one with `WASIXCC_SYSROOT`. |
| `WASIXV2_TESTS_BUILD_DIR` | Where tests are built (default `target/suite`). |
| `WASIXV2_TESTS_TIMEOUT_SECS` | Per-test timeout (default 60). |
| `WASIXV2_TESTS_EXPECTED_FAILURES` | A directory of expected-failures files to use instead of `expected-failures/`. |
| `WASIXV2_TESTS_BLESS` | Rewrite the expected-failures files from this run's results. |
| `HOSTCC` | The host C compiler that builds LTP's metadata parser (default `cc`). |

The first run also fetches Linux's UAPI headers (v7.2, a shallow sparse clone) into the build
directory: LTP's build needs `<linux/*.h>` and `<asm/*.h>`, and only its build sees them, with
`asm/unistd.h` replaced by libc2's syscall numbers. Building LTP takes a few minutes, once per
LTP commit.

```sh
cargo run --release -- [FILTER] [--exact] [--list] ...
```

Arguments are those of the standard Rust test harness. A failure report names its cause, and for
a test that fails at run time, the syscalls the kernel answered with `ENOSYS` (from a rerun with
`RUST_LOG=wasixv2_trace=trace`) and the last syscalls made.

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
  `taint_check`, ...), see `metadata_reason` in `src/upstream/ltp.rs`. Tests that need root
  (`needs_root`, or `tst_require_root()` in the old API) are not excluded: they run as uid 0.
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
| LTP | exit 0 | exit 32, only `TCONF` | the first `TFAIL` or `TBROK` line; shell scripts and shell command lines fail as needing a shell |

LTP tests run with LTP's install mounted at `/opt/ltp`, `LTPROOT=/opt/ltp` and its
`testcases/bin` on `PATH`, so tests can execute their helper programs. They run as `wasmer run`'s
default user, except tests that need root, which run as uid 0 (`wasmer run --user 0:0`). LTP
tests that are shell scripts fail until the guest has a shell.

## Fixtures

Each directory under `suite/` with a primary source is a fixture: shell scripts (`build.sh`, or
any `*.sh`, each a separate test), a `Cargo.toml`, `main.c`, `main.cpp`, or `*.rs` files (each a
separate test), in that order of precedence. Every build must produce `main`.

- `main.c` / `main.cpp` are built with `wasixcc` / `wasix++`.
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
