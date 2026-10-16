# wasixv2-tests

The test suite for wasixv2: programs under `suite/`, and the external suites under `external/`,
built with
[wasixcc](https://github.com/wasix-org/wasixcc) for `wasm32-wasixv2` against
[libc2](https://github.com/wasix-org/libc2), and run with
[wasmer](https://github.com/wasmerio/wasmer)'s `wasmer run`.

This repository has no CI of its own. It is a submodule of wasmer (`tests/wasixv2`) and libc2
(`wasixv2/tests`), and each of them runs it against its own build:

- wasmer: `make test-wasixv2`, with the wasmer it builds
- libc2: `make wasixv2-test`, with the sysroot it builds

Most tests fail until wasixv2 is complete. `expected-failures.txt` tracks them, so a run is green
when nothing regressed and nothing started passing silently.

## Running

The runner builds and runs every test. It needs, from the environment:

| Variable | Meaning |
| --- | --- |
| `WASIXV2_TESTS_WASMER` | The `wasmer` binary under test (default: `wasmer` on `PATH`). |
| `PATH` | Must contain `wasixcc` and `wasix++` with wasixv2 support. The runner sets `WASIXCC_TARGET=wasm32-wasixv2`. |
| `WASIXCC_*` | wasixcc's own settings, such as `WASIXCC_LLVM_LOCATION` and `WASIXCC_SYSROOT`, apply as usual. Without them, wasixcc uses its downloaded defaults. |
| `WASIXV2_TESTS_BUILD_DIR` | Where tests are built (default `target/suite`). |
| `WASIXV2_TESTS_TIMEOUT_SECS` | Per-test timeout for every test (default 60 for fixtures; an external suite may have its own). |
| `WASIXV2_TESTS_EXPECTED_FAILURES` | An expected-failures file to use instead of `expected-failures.txt`. |
| `WASIXV2_TESTS_BLESS` | Rewrite the expected-failures file from this run's results. |

```sh
cargo run --release -- [FILTER] [--exact] [--list] ...
```

Arguments are those of the standard Rust test harness. A failure report names its cause, and for
a test that fails at run time, the syscalls the kernel answered with `ENOSYS` (from a rerun with
`RUST_LOG=wasixv2_trace=trace`) and the last syscalls made. When the guest reports `ENOSYS` that no
syscall returned, a libc stub such as `fork`'s, the cause ends in `ENOSYS from libc`.

Some tests run only on request, with `--include-ignored` (or `--ignored` for only them); see
[External suites](#external-suites).

## Expected failures

`expected-failures.txt` lists failing tests under `## <cause>` headings. A listed test that fails
passes the run, and is shown as `[xfail]`. A listed test that passes fails the run: take it off
the list. A name that matches no test fails the `expected-failures` check.

After changing wasmer or libc2, regenerate the list with `WASIXV2_TESTS_BLESS=1` and review the
diff. Tests that the run did not include keep their entries.

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

## External suites

Suites maintained elsewhere are git submodules under `external/`, pinned to a release, and are not
copied into this tree: they keep their own licenses. Each is a first-class test source of the
runner (`src/external/`): it discovers its tests the way the suite's own build does, builds each
with wasixcc and runs it with `wasmer run`, and judges the result by the suite's conventions. Test
names start with the suite's name, so filters and `expected-failures.txt` work as for fixtures.

```sh
git submodule update --init      # in this repository; parents need --recursive
```

wasmer and libc2 check this repository out without `--recursive`, so the external suites are
missing there until initialized. The runner then says so and skips them, and keeps their entries in
`expected-failures.txt` as they are.

Besides passing and failing, an external test can be **unsupported** (it needs an optional feature
that is missing) or **untested** (it could not check what it tests). Neither is a failure: the
harness shows them as ignored with the reason, and the summary counts them separately. A test in
`expected-failures.txt` that ends either way fails the run, like one that passes.

To add a suite, add its submodule and implement `ExternalSuite` and `ExternalTest` for it, next to
`src/external/openposix.rs`, and register it in `SUITES`.

| Submodule | Pinned at | License |
| --- | --- | --- |
| `external/ltp` | [LTP](https://github.com/linux-test-project/ltp) `20260930` (`2279d708c817`) | GPL-2.0-or-later |

### Open POSIX Test Suite

`openposix/...`: LTP's `testcases/open_posix_testsuite`, as its `scripts/locate-test`,
`scripts/generate-makefiles.sh` and `bin/run-tests.sh` define it.

- **Tests.** Under `conformance/` and `stress/`, every `NUMBER-NUMBER.c` (any `.c` ending in a
  digit); under `functional/`, every `.c`. Each `*-buildonly.c` only compiles, with `-c`, and is
  shown as `[buildonly]`. A test's name is its path without `.c`, e.g.
  `openposix/conformance/interfaces/pthread_mutex_lock/1-1`.
- **Default set.** `conformance/` (`interfaces`, `definitions`, `behavior`) and `functional/`.
  `stress/` and the `speculative/` tests, which the suite builds but its `make test` does not run,
  run only on request (`--include-ignored`).
- **Shell tests.** The suite's `NUMBER-NUMBER.sh` tests all print "tested implicitly via assertion
  N" and exit 0 on the host; they test nothing under wasmer and are not included. Discovery fails if
  a pin brings a real one.
- **Build.** As the generated Makefiles do: `CFLAGS` from `include/mk/config.mk.in` after
  autoconf's `-g -O2`, `-I include -I <test dir>`, and the test directory's `CFLAGS`, `LDFLAGS` and
  `LDLIBS` files (`-lpthread -lrt` and the like), expanded as the recipe's shell would. Every
  program links `lib/common.c`, the `main` that calls the test's `test_main`, built once per run.
- **Run.** The build directory is mapped at `/t` and is the working directory, as `run-tests.sh`
  runs each test in its own directory; `<program>.args` supplies arguments. The timeout is the
  suite's `TIMEOUT_VAL`, 300 seconds, unless `WASIXV2_TESTS_TIMEOUT_SECS` is set.
- **Results** (`include/posixtest.h`): `PTS_PASS` (0) passes; `PTS_FAIL` (1) and
  `PTS_UNRESOLVED` (2) fail with that cause; `PTS_UNSUPPORTED` (4) and `PTS_UNTESTED` (5) are
  unsupported and untested; a guest killed by a signal fails as `killed by SIG...`, and any other
  status as `exit status N`.
