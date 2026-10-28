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

/// Whether a module built for exception handling can fork: whether it contains libc2's `_Fork`,
/// which `fork`, `daemon`, `forkpty` and a `vfork` that bypasses libc2's macro all go through.
/// The linker drops functions nothing calls, so this follows what the program and every library
/// it links actually use. Read from the name section; `None` for a module without one.
pub fn links_fork(wasm: &[u8]) -> Option<bool> {
    for payload in wasmparser::Parser::new(0).parse_all(wasm) {
        let Ok(wasmparser::Payload::CustomSection(section)) = payload else {
            continue;
        };
        let wasmparser::KnownCustom::Name(names) = section.as_known() else {
            continue;
        };
        for name in names.into_iter().flatten() {
            if let wasmparser::Name::Function(functions) = name {
                return Some(
                    functions
                        .into_iter()
                        .flatten()
                        .any(|naming| naming.name == "_Fork"),
                );
            }
        }
    }
    None
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

pub fn run_wasm(suite: &Suite, inv: &Invocation, dir: &Path) -> Result<Ran, Failure> {
    let mut cmd = Command::new(&suite.wasmer);
    cmd.arg("run");
    for (host, guest) in &inv.volumes {
        cmd.arg("--volume")
            .arg(format!("{}:{guest}", host.display()));
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
    let output = run::execute(suite, &mut cmd, None)?;
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
    use super::links_fork;

    /// A module with only a name section naming functions `names`.
    fn module_naming(names: &[&str]) -> Vec<u8> {
        let mut functions = vec![names.len() as u8];
        for (i, name) in names.iter().enumerate() {
            functions.extend([i as u8, name.len() as u8]);
            functions.extend(name.as_bytes());
        }
        let mut section = vec![4];
        section.extend(b"name");
        section.extend([1, functions.len() as u8]);
        section.extend(functions);
        let mut wasm = b"\0asm\x01\0\0\0".to_vec();
        wasm.extend([0, section.len() as u8]);
        wasm.extend(section);
        wasm
    }

    #[test]
    fn fork_is_found_by_name() {
        assert_eq!(links_fork(&module_naming(&["main", "_Fork"])), Some(true));
        assert_eq!(
            links_fork(&module_naming(&["main", "fork_testrun"])),
            Some(false)
        );
        assert_eq!(links_fork(b"\0asm\x01\0\0\0"), None);
    }
}
