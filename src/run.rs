//! Running a built module with `wasmer run` and checking the result against the fixture's
//! expectations.

use std::collections::{BTreeMap, VecDeque};
use std::io::{Read, Write};
use std::path::Path;
use std::process::{Command, Stdio};
use std::time::Instant;

use crate::{Failure, Suite, fixture::Config, fixture::Host};

/// How many of the last syscalls a failure report lists.
const RECENT_SYSCALLS: usize = 30;

/// How much of each output stream of a run is kept, from its start and from its end. A guest
/// that spins in a syscall writes gigabytes of trace in a minute, which would exhaust the
/// host's memory with three tests at a time.
const KEEP_HEAD: usize = 4 << 20;
const KEEP_TAIL: usize = 4 << 20;

pub(crate) struct Output {
    /// `None` when the process was killed after the timeout.
    pub(crate) status: Option<i32>,
    pub(crate) stdout: String,
    pub(crate) stderr: String,
}

/// The output of a run, for failure reports.
pub(crate) fn captured(output: &Output) -> String {
    format!(
        "--- stdout\n{}\n--- stderr\n{}\n",
        output.stdout.trim_end(),
        output.stderr.trim_end()
    )
}

/// Reruns `cmd` with syscall tracing to name what the kernel does not implement yet: the cause
/// becomes the ENOSYS syscalls, unless it was a timeout, and the details get the last syscalls.
pub(crate) fn diagnose(
    suite: &Suite,
    cmd: &mut Command,
    stdin: Option<&[u8]>,
    failure: &mut Failure,
) {
    cmd.env("RUST_LOG", "wasixv2_trace=trace");
    if let Ok(traced) = execute(suite, cmd, stdin) {
        let syscalls = Syscalls::parse(&traced.stderr);
        if !syscalls.enosys.is_empty() && failure.cause != "timeout" {
            let names: Vec<_> = syscalls.enosys.keys().map(String::as_str).collect();
            failure.cause = format!("ENOSYS: {}", names.join(", "));
        }
        failure.details.push_str(&syscalls.report());
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

    let output = execute(suite, &mut cmd, config.stdin.as_deref())?;
    match check(config, &output, dir) {
        Ok(()) => Ok(()),
        Err(mut failure) => {
            diagnose(suite, &mut cmd, config.stdin.as_deref(), &mut failure);
            Err(failure)
        }
    }
}

pub(crate) fn execute(
    suite: &Suite,
    cmd: &mut Command,
    stdin: Option<&[u8]>,
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
    let stdout = std::thread::spawn(move || read_bounded(&mut out));
    let stderr = std::thread::spawn(move || read_bounded(&mut err));

    let deadline = Instant::now() + suite.timeout;
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

/// Reads `stream` to its end, keeping its first [`KEEP_HEAD`] and last [`KEEP_TAIL`] bytes, with
/// a line in between that says how much was left out.
fn read_bounded(stream: &mut impl Read) -> Vec<u8> {
    let mut head = Vec::new();
    let mut tail = Vec::new();
    let mut dropped = 0usize;
    let mut buf = vec![0; 64 << 10];
    loop {
        let n = match stream.read(&mut buf) {
            Ok(0) | Err(_) => break,
            Ok(n) => n,
        };
        let mut chunk = &buf[..n];
        if head.len() < KEEP_HEAD {
            let take = chunk.len().min(KEEP_HEAD - head.len());
            head.extend_from_slice(&chunk[..take]);
            chunk = &chunk[take..];
        }
        tail.extend_from_slice(chunk);
        if tail.len() > 2 * KEEP_TAIL {
            let excess = tail.len() - KEEP_TAIL;
            dropped += excess;
            tail.drain(..excess);
        }
    }
    if tail.len() > KEEP_TAIL {
        let excess = tail.len() - KEEP_TAIL;
        dropped += excess;
        tail.drain(..excess);
    }
    if dropped > 0 {
        // Start the kept end at a line.
        let start = tail.iter().position(|&b| b == b'\n').map_or(0, |i| i + 1);
        dropped += start;
        tail.drain(..start);
        head.extend_from_slice(format!("\n[... {dropped} bytes left out ...]\n").as_bytes());
    }
    head.extend_from_slice(&tail);
    head
}

fn check(config: &Config, output: &Output, dir: &Path) -> Result<(), Failure> {
    let captured = || captured(output);
    let Some(status) = output.status else {
        return Err(Failure::new("timeout", captured()));
    };
    // wasmer's own refusal, e.g. an option the wasixv2 stack does not support yet.
    if let Some(line) = output
        .stderr
        .lines()
        .find(|l| l.contains("which does not support"))
    {
        return Err(Failure::new(
            format!("wasmer: {}", line.trim().trim_start_matches("error: ")),
            captured(),
        ));
    }
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
        let mut recent = VecDeque::new();
        for line in log.lines().filter(|l| l.contains("wasixv2_trace")) {
            let Some(call) = line.split_once("call=").map(|(_, rest)| rest) else {
                continue;
            };
            let call = call.split(" ret=").next().unwrap_or(call);
            if line.contains("ENOSYS") {
                let name = call.split([' ', '{', '(']).next().unwrap_or(call);
                *enosys.entry(name.to_owned()).or_default() += 1;
            }
            if recent.len() == RECENT_SYSCALLS {
                recent.pop_front();
            }
            recent.push_back(line[line.find("call=").unwrap()..].to_owned());
        }
        let recent = recent.into();
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn read_bounded_keeps_both_ends_of_a_long_stream() {
        let lines: String = (0..2_000_000).map(|i| format!("line {i}\n")).collect();
        let kept = read_bounded(&mut lines.as_bytes());
        let kept = String::from_utf8(kept).unwrap();
        assert!(kept.len() < KEEP_HEAD + KEEP_TAIL + 100);
        assert!(kept.starts_with("line 0\n"));
        assert!(kept.ends_with("line 1999999\n"));
        assert!(kept.contains(" bytes left out ...]\nline "));
        let short = read_bounded(&mut "a\nb\n".as_bytes());
        assert_eq!(short, b"a\nb\n");
    }
}
