//! Upstream test suites built from the submodules under `upstream/`: musl's libc-test, the Open
//! POSIX Test Suite and LTP. Each module discovers its tests, builds them with wasixcc and maps
//! the suite's own result convention onto pass, fail or skip.
//!
//! Tests that cannot make sense on wasixv2 are excluded, never registered, and counted by reason.
//! Everything else runs, and what does not work yet is tracked in `expected-failures/`.

pub mod libc_test;
pub mod ltp;
pub mod posix;

use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::OnceLock;

use anyhow::{Context, Result, ensure};

use crate::{Failure, Suite, run};

/// Where the submodules live.
pub fn upstream_dir() -> PathBuf {
    crate::root().join("upstream")
}

/// One test of an upstream suite.
pub enum Case {
    LibcTest(libc_test::Test),
    Posix(posix::Test),
    Ltp(ltp::Test),
}

impl Case {
    pub fn run(&self, suite: &Suite) -> Result<Outcome, Failure> {
        match self {
            Case::LibcTest(t) => t.run(suite),
            Case::Posix(t) => t.run(suite),
            Case::Ltp(t) => t.run(suite),
        }
    }
}

/// How a test that did not fail ended.
pub enum Outcome {
    Passed,
    Skipped(String),
}

/// Tests that cannot make sense on wasixv2, by reason.
#[derive(Default)]
pub struct Excluded(pub BTreeMap<String, usize>);

impl Excluded {
    pub fn add(&mut self, reason: impl Into<String>) {
        *self.0.entry(reason.into()).or_default() += 1;
    }
}

/// `excluded.txt` or `flaky.txt`: names, or prefixes ending in `*`, each with a `# reason`.
pub struct ExcludeList(Vec<(String, String)>);

impl ExcludeList {
    pub fn load(file: &str) -> Result<Self> {
        let path = crate::root().join(file);
        let text = std::fs::read_to_string(&path)
            .with_context(|| format!("reading {}", path.display()))?;
        let mut entries = Vec::new();
        for (i, line) in text.lines().enumerate() {
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            let (pattern, reason) = line
                .split_once('#')
                .with_context(|| format!("{}:{}: no `# reason`", path.display(), i + 1))?;
            entries.push((pattern.trim().to_owned(), reason.trim().to_owned()));
        }
        Ok(Self(entries))
    }

    /// Entries that match none of `names`.
    pub fn stale<'a>(&'a self, names: &'a BTreeSet<String>) -> impl Iterator<Item = &'a str> {
        self.0
            .iter()
            .map(|(pattern, _)| pattern.as_str())
            .filter(|p| {
                !names.iter().any(|n| match p.strip_suffix('*') {
                    Some(prefix) => n.starts_with(prefix),
                    None => n == p,
                })
            })
    }

    /// The reason `name` is listed, if it is.
    pub fn reason(&self, name: &str) -> Option<&str> {
        self.0.iter().find_map(|(pattern, reason)| {
            let hit = match pattern.strip_suffix('*') {
                Some(prefix) => name.starts_with(prefix),
                None => name == pattern,
            };
            hit.then_some(reason.as_str())
        })
    }
}

/// A clean copy of submodule `name` at its checked-out commit, under the build directory, so
/// builds never touch the submodule. Made once per commit.
pub fn source_copy(suite: &Suite, name: &str) -> Result<PathBuf> {
    source_copy_as(suite, name, "")
}

/// `source_copy`, another copy for each `variant`, such as a second build configuration.
pub fn source_copy_as(suite: &Suite, name: &str, variant: &str) -> Result<PathBuf> {
    // Two suites share LTP's copy; only one may create it.
    static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
    let _guard = LOCK.lock().unwrap();
    let repo = upstream_dir().join(name);
    let commit = git(&repo, &["rev-parse", "HEAD"]).with_context(|| {
        format!("upstream/{name} is not checked out: run `git submodule update --init`")
    })?;
    let suffix = if variant.is_empty() {
        String::new()
    } else {
        format!("-{variant}")
    };
    let dest = suite
        .build_root
        .join("upstream")
        .join(format!("{name}-{}{suffix}", &commit[..12]));
    let ready = dest.join(".wasixv2-tests-ready");
    if ready.exists() {
        return Ok(dest);
    }
    if dest.exists() {
        std::fs::remove_dir_all(&dest)?;
    }
    std::fs::create_dir_all(&dest)?;
    let archive = Command::new("git")
        .arg("-C")
        .arg(&repo)
        .args(["archive", "--format=tar", "HEAD"])
        .output()?;
    ensure!(
        archive.status.success(),
        "git archive in upstream/{name} failed"
    );
    let mut tar = Command::new("tar")
        .arg("-x")
        .arg("-C")
        .arg(&dest)
        .stdin(std::process::Stdio::piped())
        .spawn()?;
    use std::io::Write;
    tar.stdin.take().unwrap().write_all(&archive.stdout)?;
    ensure!(tar.wait()?.success(), "extracting upstream/{name} failed");
    std::fs::write(&ready, &commit)?;
    Ok(dest)
}

pub(crate) fn git(dir: &Path, args: &[&str]) -> Result<String> {
    let out = Command::new("git").arg("-C").arg(dir).args(args).output()?;
    ensure!(
        out.status.success(),
        "git {args:?} failed in {}",
        dir.display()
    );
    Ok(String::from_utf8(out.stdout)?.trim().to_owned())
}

/// Environment for every wasixcc build: the v2 target.
pub fn wasixcc_env(cmd: &mut Command) -> &mut Command {
    cmd.env("WASIXCC_TARGET", "wasm32-wasixv2")
        .env("WASIXCC_DISCARD_UNSUPPORTED_FLAGS", "yes")
}

/// libc2's two build configurations, each with its own sysroot under `WASIXCC_SYSROOT_PREFIX`.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum BuildConfig {
    /// wasixcc's default, with wasm exception handling. fork fails with `ENOSYS`.
    ExceptionHandling,
    /// `WASIXCC_WASM_EXCEPTIONS=no`: wasixcc runs `wasm-opt --asyncify` on every program, so the
    /// host can unwind and rewind the stack, and fork returns twice.
    Asyncify,
}

/// `wasixcc_env`, for `config`.
pub fn wasixcc_env_for(cmd: &mut Command, config: BuildConfig) -> &mut Command {
    wasixcc_env(cmd);
    if config == BuildConfig::Asyncify {
        cmd.env("WASIXCC_WASM_EXCEPTIONS", "no");
    }
    cmd
}

/// libc2's numbers for clone and clone3, Linux's generic syscall table.
const SYS_CLONE: i32 = 220;
const SYS_CLONE3: i32 = 435;

/// Whether a module built for exception handling can fork, in one of the ways a program reaches
/// the `clone`/`clone3` imports that return twice in the asyncify configuration:
///
/// - It contains libc2's `_Fork`, which `fork`, `daemon`, `forkpty` and a `vfork` that bypasses
///   libc2's macro all go through.
/// - It contains libc2's `clone` wrapper, which forks when the flags lack `CLONE_VM` (LTP's
///   `ltp_clone`, clone01 to clone11, the namespace tests). The flags are seldom constants
///   there, so every caller counts; one that only vforks (`CLONE_VM | CLONE_VFORK`) works in
///   either configuration, and threads cannot start through it (`CLONE_THREAD` is `EINVAL`).
/// - It calls `syscall` with `SYS_clone` or `SYS_clone3` as the number (`raw_clone`): LTP's
///   `tst_clone` (`SAFE_CLONE`, `tst_ns_create`, `tst_ns_exec`) and its `clone3` tests. A raw
///   clone has no function to run on a new stack, so it forks. Threads never come this way:
///   libc2's `pthread_create` goes through `__clone` and the thread-spawn extension.
///
/// The linker drops functions nothing calls, so this follows what the program and every library
/// it links actually use. Names come from the name section; `None` for a module without one, or
/// whose code cannot be read.
pub fn links_fork(wasm: &[u8]) -> Option<bool> {
    let mut bodies = Vec::new();
    let mut syscall = None;
    let mut named = false;
    for payload in wasmparser::Parser::new(0).parse_all(wasm) {
        match payload.ok()? {
            wasmparser::Payload::CodeSectionEntry(body) => bodies.push(body),
            wasmparser::Payload::CustomSection(section) => {
                let wasmparser::KnownCustom::Name(names) = section.as_known() else {
                    continue;
                };
                for name in names {
                    let wasmparser::Name::Function(functions) = name.ok()? else {
                        continue;
                    };
                    named = true;
                    for naming in functions {
                        let naming = naming.ok()?;
                        match naming.name {
                            "_Fork" | "clone" => return Some(true),
                            "syscall" => syscall = Some(naming.index),
                            _ => {}
                        }
                    }
                }
            }
            _ => {}
        }
    }
    if !named {
        return None;
    }
    let Some(syscall) = syscall else {
        return Some(false);
    };
    for body in bodies {
        if raw_clone(body.get_operators_reader().ok()?, syscall)? {
            return Some(true);
        }
    }
    Some(false)
}

/// Whether a function calls `syscall` (function `syscall`, `long syscall(long n, ...)`: on
/// wasm32 the number and a pointer to the variadic arguments) with `SYS_clone` or `SYS_clone3`
/// as a constant number. Follows the operand stack through the instructions that compute
/// arguments, constants, locals, arithmetic, loads and stores; any other instruction, a call or
/// control flow, forgets what is on it. `None` when the code cannot be read.
fn raw_clone(ops: wasmparser::OperatorsReader<'_>, syscall: u32) -> Option<bool> {
    use wasmparser::Operator as Op;
    // The top of the operand stack since the last instruction that is not followed: each entry
    // the constant it is, if it is an i32 constant.
    let mut stack: Vec<Option<i32>> = Vec::new();
    let pop = |stack: &mut Vec<Option<i32>>, n: usize| {
        stack.truncate(stack.len().saturating_sub(n));
    };
    for op in ops {
        match op.ok()? {
            Op::Call { function_index } if function_index == syscall => {
                if let [.., Some(SYS_CLONE | SYS_CLONE3), _] = stack[..] {
                    return Some(true);
                }
                stack.clear();
            }
            Op::I32Const { value } => stack.push(Some(value)),
            Op::I64Const { .. }
            | Op::F32Const { .. }
            | Op::F64Const { .. }
            | Op::LocalGet { .. }
            | Op::GlobalGet { .. } => stack.push(None),
            Op::LocalSet { .. } | Op::GlobalSet { .. } | Op::Drop => pop(&mut stack, 1),
            Op::LocalTee { .. }
            | Op::I32Load { .. }
            | Op::I32Load8S { .. }
            | Op::I32Load8U { .. }
            | Op::I32Load16S { .. }
            | Op::I32Load16U { .. }
            | Op::I64Load { .. }
            | Op::I64Load8S { .. }
            | Op::I64Load8U { .. }
            | Op::I64Load16S { .. }
            | Op::I64Load16U { .. }
            | Op::I64Load32S { .. }
            | Op::I64Load32U { .. }
            | Op::F32Load { .. }
            | Op::F64Load { .. }
            | Op::V128Load { .. }
            | Op::I32Eqz
            | Op::I64ExtendI32S
            | Op::I64ExtendI32U
            | Op::I32WrapI64 => {
                pop(&mut stack, 1);
                stack.push(None);
            }
            Op::I32Store { .. }
            | Op::I32Store8 { .. }
            | Op::I32Store16 { .. }
            | Op::I64Store { .. }
            | Op::I64Store8 { .. }
            | Op::I64Store16 { .. }
            | Op::I64Store32 { .. }
            | Op::F32Store { .. }
            | Op::F64Store { .. }
            | Op::V128Store { .. } => pop(&mut stack, 2),
            Op::I32Add
            | Op::I32Sub
            | Op::I32Mul
            | Op::I32And
            | Op::I32Or
            | Op::I32Xor
            | Op::I32Shl
            | Op::I32ShrS
            | Op::I32ShrU
            | Op::I64Add
            | Op::I64Sub
            | Op::I64Mul
            | Op::I64And
            | Op::I64Or
            | Op::I64Xor
            | Op::I64Shl
            | Op::I64ShrS
            | Op::I64ShrU => {
                pop(&mut stack, 2);
                stack.push(None);
            }
            Op::Select | Op::TypedSelect { .. } => {
                pop(&mut stack, 3);
                stack.push(None);
            }
            _ => stack.clear(),
        }
    }
    Some(false)
}

/// Builds the program `main` with `build`, for exception handling, and rebuilds it for asyncify
/// when it can fork (`links_fork`). Returns the configuration it was built for.
pub fn build_program(
    main: &Path,
    mut build: impl FnMut(BuildConfig) -> Result<(), Failure>,
) -> Result<BuildConfig, Failure> {
    build(BuildConfig::ExceptionHandling)?;
    let wasm = std::fs::read(main)
        .map_err(|e| Failure::harness(format!("reading {}: {e}", main.display())))?;
    match links_fork(&wasm) {
        Some(false) => Ok(BuildConfig::ExceptionHandling),
        Some(true) => {
            build(BuildConfig::Asyncify)?;
            Ok(BuildConfig::Asyncify)
        }
        None => Err(Failure::harness(format!(
            "{} has no name section, so whether it forks is unknown",
            main.display()
        ))),
    }
}

/// Runs `cmd` (a build step) and turns a failure into a `build:` failure named after its first
/// error line.
pub fn run_build(cmd: &mut Command) -> Result<(), Failure> {
    let output = cmd
        .output()
        .map_err(|e| Failure::harness(format!("cannot run {cmd:?}: {e}")))?;
    if output.status.success() {
        return Ok(());
    }
    let stderr = String::from_utf8_lossy(&output.stderr);
    let stdout = String::from_utf8_lossy(&output.stdout);
    Err(Failure::new(
        format!(
            "build: {}",
            crate::build::first_error_line(&stderr, &stdout)
        ),
        format!(
            "build failed ({})\n--- stdout\n{stdout}\n--- stderr\n{stderr}",
            output.status
        ),
    ))
}

/// A wasm module to run, with what its environment needs.
#[derive(Default)]
pub struct Invocation {
    pub module: PathBuf,
    pub args: Vec<String>,
    pub env: Vec<(String, String)>,
    /// Host directory to guest path.
    pub volumes: Vec<(PathBuf, String)>,
    pub cwd: Option<String>,
    /// `UID[:GID]` to run as, instead of `wasmer run`'s default user.
    pub user: Option<String>,
    /// An upstream runner can declare a new watchdog interval during setup.
    pub timeout_from_line: Option<run::TimeoutFromLine>,
}

/// The result of running an `Invocation`.
pub struct Ran {
    pub output: run::Output,
    cmd: Command,
}

impl Ran {
    /// The combined output, for failure reports.
    pub fn captured(&self) -> String {
        run::captured(&self.output)
    }

    /// Fails with `cause`, naming the syscalls the kernel answered with ENOSYS when there are any.
    pub fn fail(mut self, suite: &Suite, cause: impl Into<String>) -> Failure {
        let mut failure = Failure::new(cause, self.captured());
        if self.output.status.is_none() {
            failure.cause = "timeout".to_owned();
        }
        run::diagnose(suite, &mut self.cmd, None, &mut failure);
        failure
    }
}

/// The optional userland for upstream tests: `WASIXV2_TESTS_USERLAND`, a directory with GNU bash,
/// coreutils and the other commands upstream tests execute, built for wasixv2 (`out/bin` of a
/// userland build), which is mounted at `/bin`. `None` when the variable is unset.
pub fn userland() -> Result<Option<&'static Path>, Failure> {
    static USERLAND: OnceLock<Result<Option<PathBuf>, String>> = OnceLock::new();
    USERLAND
        .get_or_init(|| {
            let Some(bin) = crate::env_path("WASIXV2_TESTS_USERLAND") else {
                return Ok(None);
            };
            if !bin.join("bash").is_file() {
                return Err(format!(
                    "WASIXV2_TESTS_USERLAND: {} has no bash",
                    bin.display()
                ));
            }
            Ok(Some(bin))
        })
        .as_ref()
        .map(Option::as_deref)
        .map_err(|e| Failure::harness(e.clone()))
}

fn wasm_command(suite: &Suite, inv: &Invocation, dir: &Path, userland: Option<&Path>) -> Command {
    let mut cmd = suite.wasmer_command();
    cmd.arg("run");
    for (host, guest) in &inv.volumes {
        cmd.arg("--volume")
            .arg(format!("{}:{guest}", host.display()));
    }
    if let Some(bin) = userland {
        cmd.arg("--volume").arg(format!("{}:/bin", bin.display()));
        if !inv.env.iter().any(|(key, _)| key == "PATH") {
            cmd.args(["--env", "PATH=/bin"]);
        }
    }
    if let Some(cwd) = &inv.cwd {
        cmd.arg("--cwd").arg(cwd);
    }
    if let Some(user) = &inv.user {
        cmd.arg("--user").arg(user);
    }
    for (key, value) in &inv.env {
        cmd.arg("--env").arg(format!("{key}={value}"));
    }
    cmd.arg(&inv.module)
        .arg("--")
        .args(&inv.args)
        .current_dir(dir);
    cmd
}

pub fn run_wasm(suite: &Suite, inv: &Invocation, dir: &Path) -> Result<Ran, Failure> {
    let mut cmd = wasm_command(suite, inv, dir, userland()?);
    let output = run::execute_with_timeout_updates(suite, &mut cmd, None, inv.timeout_from_line)?;
    Ok(Ran { output, cmd })
}

/// Normalizes an output line into a cause shared by tests that fail the same way: drops the
/// source location and the random part of LTP's temporary directory names.
pub fn cause_from_line(prefix: &str, line: &str) -> String {
    const TMP: &str = "/tmp/LTP_";
    let mut line = crate::build::strip_location(line.trim()).to_owned();
    // Host paths differ between machines; so do code offsets between builds.
    let build = crate::build_root().display().to_string();
    line = line.replace(&format!("{build}/"), "");
    if let Some(at) = line.find(" (main[") {
        line.truncate(at);
    }
    let mut from = 0;
    while let Some(at) = line[from..].find(TMP).map(|i| from + i + TMP.len()) {
        let random = line[at..]
            .chars()
            .take_while(char::is_ascii_alphanumeric)
            .count();
        line.replace_range(at..at + random, "*");
        from = at + 1;
    }
    format!("{prefix}{}", line.chars().take(160).collect::<String>())
}

/// The first line of `text` that satisfies `pred`.
pub fn first_line(text: &str, pred: impl Fn(&str) -> bool) -> Option<&str> {
    text.lines().find(|l| pred(l))
}

#[cfg(test)]
mod tests {
    use super::{Invocation, links_fork, wasm_command};
    use std::path::{Path, PathBuf};

    #[test]
    fn c_tests_get_the_userland_and_a_search_path() {
        let suite = crate::Suite {
            wasmer: "wasmer".into(),
            wrapper: vec![],
            cc: "cc".into(),
            cxx: "c++".into(),
            build_root: PathBuf::new(),
            timeout: std::time::Duration::from_secs(1),
        };
        for path in [None, Some("/opt/ltp/testcases/bin:/bin")] {
            let inv = Invocation {
                module: "main.wasm".into(),
                env: path
                    .map(|p| vec![("PATH".into(), p.into())])
                    .unwrap_or_default(),
                ..Default::default()
            };
            let command = wasm_command(&suite, &inv, Path::new("."), Some(Path::new("/userland")));
            let args: Vec<_> = command.get_args().map(|a| a.to_str().unwrap()).collect();
            assert!(args.windows(2).any(|a| a == ["--volume", "/userland:/bin"]));
            let expected = format!("PATH={}", path.unwrap_or("/bin"));
            assert!(args.windows(2).any(|a| a == ["--env", &expected]));
            assert_eq!(args.iter().filter(|a| a.starts_with("PATH=")).count(), 1);
        }
    }

    const I32_CONST: u8 = 0x41;
    const CALL: u8 = 0x10;
    const LOCAL_GET: u8 = 0x20;
    const I32_ADD: u8 = 0x6a;
    const DROP: u8 = 0x1a;
    const BLOCK: u8 = 0x02;
    const EMPTY: u8 = 0x40;
    const END: u8 = 0x0b;

    fn section(id: u8, contents: Vec<u8>) -> Vec<u8> {
        let mut section = vec![id];
        leb(&mut section, contents.len() as u64);
        section.extend(contents);
        section
    }

    fn leb(out: &mut Vec<u8>, mut value: u64) {
        loop {
            let byte = (value & 0x7f) as u8;
            value >>= 7;
            if value == 0 {
                out.push(byte);
                return;
            }
            out.push(byte | 0x80);
        }
    }

    /// `i32.const value`.
    fn i32_const(value: i32) -> Vec<u8> {
        let mut out = vec![I32_CONST];
        let mut value = i64::from(value);
        loop {
            let byte = (value & 0x7f) as u8;
            value >>= 7;
            if (value == 0 && byte & 0x40 == 0) || (value == -1 && byte & 0x40 != 0) {
                out.push(byte);
                return out;
            }
            out.push(byte | 0x80);
        }
    }

    /// A module of functions `(name, code)`, function `i` named `name` with body `code`, and a
    /// name section naming them.
    fn module(functions: &[(&str, Vec<u8>)]) -> Vec<u8> {
        let mut wasm = b"\0asm\x01\0\0\0".to_vec();
        wasm.extend(section(1, vec![1, 0x60, 0, 0]));
        let mut types = vec![functions.len() as u8];
        types.extend(std::iter::repeat_n(0, functions.len()));
        wasm.extend(section(3, types));
        let mut code = vec![functions.len() as u8];
        for (_, ops) in functions {
            let mut body = vec![0];
            body.extend(ops);
            body.push(END);
            leb(&mut code, body.len() as u64);
            code.extend(body);
        }
        wasm.extend(section(10, code));
        let mut names = vec![functions.len() as u8];
        for (i, (name, _)) in functions.iter().enumerate() {
            names.extend([i as u8, name.len() as u8]);
            names.extend(name.as_bytes());
        }
        let mut custom = vec![4];
        custom.extend(b"name");
        custom.extend(section(1, names));
        wasm.extend(section(0, custom));
        wasm
    }

    /// `main`, with `code`, and `syscall` as function 1.
    fn calling_syscall(code: Vec<u8>) -> Vec<u8> {
        module(&[("main", code), ("syscall", vec![])])
    }

    #[test]
    fn fork_is_found_by_name() {
        assert_eq!(
            links_fork(&module(&[("main", vec![]), ("_Fork", vec![])])),
            Some(true)
        );
        assert_eq!(
            links_fork(&module(&[("main", vec![]), ("clone", vec![])])),
            Some(true)
        );
        assert_eq!(
            links_fork(&module(&[("main", vec![]), ("fork_testrun", vec![])])),
            Some(false)
        );
        assert_eq!(links_fork(b"\0asm\x01\0\0\0"), None);
    }

    /// `syscall(SYS_clone3, &args, sizeof(args))` as clang emits it for LTP's `tst_clone`: the
    /// number, then the address of the variadic arguments on the stack.
    #[test]
    fn raw_clone3_forks() {
        let code = [
            i32_const(435),
            vec![LOCAL_GET, 0],
            i32_const(32),
            vec![I32_ADD, CALL, 1, DROP],
        ]
        .concat();
        assert_eq!(links_fork(&calling_syscall(code)), Some(true));
    }

    #[test]
    fn raw_clone_forks() {
        let code = [i32_const(220), vec![LOCAL_GET, 0, CALL, 1, DROP]].concat();
        assert_eq!(links_fork(&calling_syscall(code)), Some(true));
    }

    /// Another syscall whose variadic arguments happen to sit at 220 bytes into the frame.
    #[test]
    fn clone_number_as_another_argument_does_not_fork() {
        let code = [
            i32_const(39),
            vec![LOCAL_GET, 0],
            i32_const(220),
            vec![I32_ADD, CALL, 1, DROP],
        ]
        .concat();
        assert_eq!(links_fork(&calling_syscall(code)), Some(false));
    }

    /// A constant left behind by code the operand stack is not followed through.
    #[test]
    fn clone_number_before_control_flow_is_forgotten() {
        let code = [
            i32_const(435),
            vec![BLOCK, EMPTY, END],
            i32_const(39),
            vec![LOCAL_GET, 0, CALL, 1, DROP, DROP],
        ]
        .concat();
        assert_eq!(links_fork(&calling_syscall(code)), Some(false));
        let code = [i32_const(435), vec![DROP, LOCAL_GET, 0, CALL, 1, DROP]].concat();
        assert_eq!(links_fork(&calling_syscall(code)), Some(false));
    }

    /// The clone number passed to a function other than `syscall`.
    #[test]
    fn clone_number_to_another_function_does_not_fork() {
        let code = [i32_const(435), vec![LOCAL_GET, 0, CALL, 2, DROP]].concat();
        let wasm = module(&[("main", code), ("syscall", vec![]), ("other", vec![])]);
        assert_eq!(links_fork(&wasm), Some(false));
    }

    /// pthread_create reaches `__clone`, which spawns threads through the thread-spawn extension
    /// and never calls `syscall`.
    #[test]
    fn threads_do_not_fork() {
        let wasm = module(&[
            ("main", vec![CALL, 1]),
            ("pthread_create", vec![CALL, 2]),
            ("__clone", vec![]),
            ("syscall", vec![]),
        ]);
        assert_eq!(links_fork(&wasm), Some(false));
    }
}
