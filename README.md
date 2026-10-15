# wasixv2-tests

The test suite for wasixv2: programs under `suite/`, built with
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
| `WASIXV2_TESTS_TIMEOUT_SECS` | Per-test timeout (default 60). |
| `WASIXV2_TESTS_EXPECTED_FAILURES` | An expected-failures file to use instead of `expected-failures.txt`. |
| `WASIXV2_TESTS_BLESS` | Rewrite the expected-failures file from this run's results. |

```sh
cargo run --release -- [FILTER] [--exact] [--list] ...
```

Arguments are those of the standard Rust test harness. A failure report names its cause, and for
a test that fails at run time, the syscalls the kernel answered with `ENOSYS` (from a rerun with
`RUST_LOG=wasixv2_trace=trace`) and the last syscalls made.

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
