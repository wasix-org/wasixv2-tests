//! Running a built module with `wasmer run` and checking the result against the fixture's
//! expectations.

use std::collections::BTreeMap;
use std::io::{Read, Write};
use std::path::Path;
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

use crate::{Failure, Suite, fixture::Config, fixture::Host};

/// The per-test timeout unless `WASIXV2_TESTS_TIMEOUT_SECS` sets one.
const DEFAULT_TIMEOUT: Duration = Duration::from_secs(60);

/// libc's `strerror(ENOSYS)`.
const ENOSYS_MESSAGE: &str = "Function not implemented";

/// How many of the last syscalls a failure report lists.
const RECENT_SYSCALLS: usize = 30;

pub struct Output {
    /// `None` when the process was killed after the timeout.
    pub status: Option<i32>,
    pub stdout: String,
    pub stderr: String,
}

impl Output {
    /// Both streams, for a failure report.
    pub fn captured(&self) -> String {
        format!(
            "--- stdout\n{}\n--- stderr\n{}\n",
            self.stdout.trim_end(),
            self.stderr.trim_end()
        )
    }

    /// The guest's exit status, or why there is none: a timeout, or wasmer refusing to run it.
    pub fn exit_status(&self) -> Result<i32, Failure> {
        let Some(status) = self.status else {
            return Err(Failure::new("timeout", self.captured()));
        };
        // wasmer's own refusal, e.g. an option the wasixv2 stack does not support yet.
        if let Some(line) = self
            .stderr
            .lines()
            .find(|l| l.contains("which does not support"))
        {
            return Err(Failure::new(
                format!("wasmer: {}", line.trim().trim_start_matches("error: ")),
                self.captured(),
            ));
        }
        Ok(status)
    }
}

/// Runs `main` for `config` in `dir` and checks every expectation.
pub fn run_and_check(
    suite: &Suite,
    config: &Config,
    main: &Path,
    dir: &Path,
) -> Result<(), Failure> {
    if let Some(name) = &config.program_name {
        return Err(Failure::new(
            "runner: ProgramName is not supported",
            format!("`wasmer run` cannot set argv[0] (wanted `{name}`)"),
        ));
    }
    for (path, contents) in &config.prefilled_files {
        std::fs::write(dir.join(path), contents)
            .map_err(|e| Failure::harness(format!("writing {}: {e}", path.display())))?;
    }
    // Kept alive until the run is over.
    let mut temp_dirs = Vec::new();
    let mut cmd = Command::new(&suite.wasmer);
    cmd.arg("run");
    for (host, guest) in &config.mapped_directories {
        let host = match host {
            Host::Path(path) => dir.join(path),
            Host::Temp => {
                let temp = tempfile::tempdir().map_err(|e| Failure::harness(e.to_string()))?;
                let path = temp.path().to_path_buf();
                temp_dirs.push(temp);
                path
            }
        };
        cmd.arg("--volume")
            .arg(format!("{}:{guest}", host.display()));
    }
    if let Some(cwd) = &config.current_directory {
        cmd.arg("--cwd").arg(cwd);
    }
    for (key, value) in &config.env {
        cmd.arg("--env").arg(format!("{key}={value}"));
    }
    cmd.arg(main).arg("--").args(&config.args).current_dir(dir);

    run_checked(
        suite,
        &mut cmd,
        config.stdin.as_deref(),
        suite.timeout_or(DEFAULT_TIMEOUT),
        |output| check(config, output, dir),
    )
}

/// Runs a `wasmer run` command and judges its output with `check`. When the check fails, reruns
/// it with syscall tracing to name what the kernel does not implement yet.
pub fn run_checked<T>(
    suite: &Suite,
    cmd: &mut Command,
    stdin: Option<&[u8]>,
    timeout: Duration,
    check: impl Fn(&Output) -> Result<T, Failure>,
) -> Result<T, Failure> {
    let output = execute(suite, cmd, stdin, timeout)?;
    match check(&output) {
        Ok(result) => Ok(result),
        Err(mut failure) => {
            cmd.env("RUST_LOG", "wasixv2_trace=trace");
            if let Ok(traced) = execute(suite, cmd, stdin, timeout) {
                let syscalls = Syscalls::parse(&traced.stderr);
                let libc_enosys = [&output.stdout, &output.stderr]
                    .iter()
                    .any(|s| s.contains(ENOSYS_MESSAGE));
                if failure.cause == "timeout" {
                    // A hang is the cause, whatever else went wrong.
                } else if !syscalls.enosys.is_empty() {
                    let names: Vec<_> = syscalls.enosys.keys().map(String::as_str).collect();
                    failure.cause = format!("ENOSYS: {}", names.join(", "));
                } else if libc_enosys {
                    // ENOSYS that no syscall returned: a libc stub, such as fork's.
                    failure.cause.push_str(": ENOSYS from libc");
                }
                failure.details.push_str(&syscalls.report());
            }
            Err(failure)
        }
    }
}

fn execute(
    suite: &Suite,
    cmd: &mut Command,
    stdin: Option<&[u8]>,
    timeout: Duration,
) -> Result<Output, Failure> {
    let mut child = cmd
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|e| Failure::harness(format!("cannot run {}: {e}", suite.wasmer.display())))?;
    let mut input = child.stdin.take().unwrap();
    let stdin = stdin.unwrap_or_default().to_vec();
    // A guest may exit without reading its input, so a failed write is not an error.
    let writer = std::thread::spawn(move || {
        let _ = input.write_all(&stdin);
    });
    let mut out = child.stdout.take().unwrap();
    let mut err = child.stderr.take().unwrap();
    let stdout = std::thread::spawn(move || {
        let mut buf = Vec::new();
        let _ = out.read_to_end(&mut buf);
        buf
    });
    let stderr = std::thread::spawn(move || {
        let mut buf = Vec::new();
        let _ = err.read_to_end(&mut buf);
        buf
    });

    let deadline = Instant::now() + timeout;
    let status = loop {
        match child.try_wait() {
            Ok(Some(status)) => break status.code().or(Some(-1)),
            Ok(None) if Instant::now() >= deadline => {
                let _ = child.kill();
                let _ = child.wait();
                break None;
            }
            Ok(None) => std::thread::sleep(std::time::Duration::from_millis(10)),
            Err(e) => return Err(Failure::harness(format!("waiting for wasmer: {e}"))),
        }
    };
    let _ = writer.join();
    Ok(Output {
        status,
        stdout: String::from_utf8_lossy(&stdout.join().unwrap_or_default()).into_owned(),
        stderr: String::from_utf8_lossy(&stderr.join().unwrap_or_default()).into_owned(),
    })
}

fn check(config: &Config, output: &Output, dir: &Path) -> Result<(), Failure> {
    let captured = || output.captured();
    let status = output.exit_status()?;
    if config.must_fail {
        if status == 0 {
            return Err(Failure::new("exit status: expected non-zero", captured()));
        }
    } else if status != config.expected_exit_code {
        return Err(Failure::new(
            "exit status",
            format!(
                "expected exit status {}, got {status}\n{}",
                config.expected_exit_code,
                captured()
            ),
        ));
    }
    for (stream, got, expected) in [
        ("stdout", &output.stdout, &config.expected_stdout),
        ("stderr", &output.stderr, &config.expected_stderr),
    ] {
        let lines: Vec<&str> = got.trim().lines().collect();
        if !expected.is_empty() && lines != *expected {
            return Err(Failure::new(
                format!("{stream} mismatch"),
                format!(
                    "expected {stream} {expected:?}, got {lines:?}\n{}",
                    captured()
                ),
            ));
        }
    }
    for (path, expected) in &config.expected_files {
        let got = std::fs::read_to_string(dir.join(path)).unwrap_or_default();
        if got != *expected {
            return Err(Failure::new(
                "file mismatch",
                format!(
                    "expected {} to contain {expected:?}, got {got:?}\n{}",
                    path.display(),
                    captured()
                ),
            ));
        }
    }
    Ok(())
}

/// What the `wasixv2_trace` layer logged: one `syscall` event per call, with `call` and `ret`.
struct Syscalls {
    enosys: BTreeMap<String, usize>,
    recent: Vec<String>,
}

impl Syscalls {
    fn parse(log: &str) -> Self {
        let mut enosys = BTreeMap::new();
        let mut recent = Vec::new();
        for line in log.lines().filter(|l| l.contains("wasixv2_trace")) {
            let Some(call) = line.split_once("call=").map(|(_, rest)| rest) else {
                continue;
            };
            let call = call.split(" ret=").next().unwrap_or(call);
            if line.contains("ENOSYS") {
                let name = call.split([' ', '{', '(']).next().unwrap_or(call);
                *enosys.entry(name.to_owned()).or_default() += 1;
            }
            recent.push(line[line.find("call=").unwrap()..].to_owned());
        }
        let skip = recent.len().saturating_sub(RECENT_SYSCALLS);
        recent.drain(..skip);
        Self { enosys, recent }
    }

    fn report(&self) -> String {
        if self.recent.is_empty() {
            return String::new();
        }
        let enosys: Vec<_> = self
            .enosys
            .iter()
            .map(|(name, n)| format!("{name} x{n}"))
            .collect();
        let mut out = format!("--- ENOSYS: {}\n", enosys.join(", "));
        out.push_str(&format!("--- last {} syscalls\n", self.recent.len()));
        for line in &self.recent {
            out.push_str("  ");
            out.push_str(line);
            out.push('\n');
        }
        out
    }
}
