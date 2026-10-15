//! Runs the wasixv2 test suite: builds every fixture under `suite/` with wasixcc for
//! `wasm32-wasixv2` and runs it with a `wasmer` binary. See the README for the environment it
//! needs.
//!
//! Most fixtures fail until wasixv2 is complete. `expected-failures.txt` lists them; a listed test
//! that fails passes the run, and one that passes fails it so the list keeps shrinking.

mod build;
mod fixture;
mod run;

use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};
use std::process::ExitCode;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use anyhow::{Context, Result, ensure};
use libtest_mimic::{Arguments, Completion, Failed, Trial};

use crate::fixture::Config;

const DEFAULT_TIMEOUT_SECS: u64 = 60;

/// Everything a test needs from the environment.
pub struct Suite {
    pub wasmer: PathBuf,
    pub cc: String,
    pub cxx: String,
    build_root: PathBuf,
    pub timeout: Duration,
}

/// Why a test failed. `cause` is a short, fixture-independent summary that groups failures in
/// `expected-failures.txt`; `details` is the full report.
#[derive(Debug)]
pub struct Failure {
    pub cause: String,
    pub details: String,
}

impl Failure {
    pub fn new(cause: impl Into<String>, details: impl Into<String>) -> Self {
        Self {
            cause: cause.into(),
            details: details.into(),
        }
    }

    /// A problem with the runner or the host, not with the code under test.
    pub fn harness(details: impl Into<String>) -> Self {
        Self::new("runner error", details)
    }
}

fn root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
}

fn find_on_path(name: &str) -> Option<PathBuf> {
    std::env::split_paths(&std::env::var_os("PATH")?)
        .map(|dir| dir.join(name))
        .find(|path| path.is_file())
}

fn env_path(var: &str) -> Option<PathBuf> {
    std::env::var_os(var)
        .filter(|v| !v.is_empty())
        .map(PathBuf::from)
}

impl Suite {
    fn from_env() -> Result<Self> {
        let wasmer = match env_path("WASIXV2_TESTS_WASMER") {
            Some(wasmer) => wasmer,
            None => find_on_path("wasmer").context(
                "no `wasmer` on PATH; set WASIXV2_TESTS_WASMER to the wasmer binary under test",
            )?,
        };
        ensure!(wasmer.is_file(), "{} is not a file", wasmer.display());
        let timeout = match std::env::var("WASIXV2_TESTS_TIMEOUT_SECS") {
            Ok(secs) => secs.parse().context("WASIXV2_TESTS_TIMEOUT_SECS")?,
            Err(_) => DEFAULT_TIMEOUT_SECS,
        };
        let suite = Self {
            wasmer,
            cc: "wasixcc".to_owned(),
            cxx: "wasix++".to_owned(),
            build_root: env_path("WASIXV2_TESTS_BUILD_DIR")
                .unwrap_or_else(|| root().join("target/suite")),
            timeout: Duration::from_secs(timeout),
        };
        let probe = std::process::Command::new(&suite.cc)
            .arg("--version")
            .output();
        ensure!(
            probe.is_ok_and(|o| o.status.success()),
            "`{}` is not on PATH; install a wasixcc with wasm32-wasixv2 support",
            suite.cc
        );
        Ok(suite)
    }

    fn run(&self, config: &Config) -> Result<Completion, Failure> {
        if let Some(reason) = &config.ignored {
            return Ok(Completion::ignored_with(reason));
        }
        if config.unix_only && !cfg!(unix) {
            return Ok(Completion::ignored_with("Unix only"));
        }
        if config.ignore_macos && cfg!(target_os = "macos") {
            return Ok(Completion::ignored_with("ignored on macOS"));
        }
        // The wasmer under test runs on its default backend, Cranelift.
        if let Some((_, reason)) = config
            .skipped_engines
            .iter()
            .find(|(e, _)| e == "cranelift")
        {
            return Ok(Completion::ignored_with(reason));
        }
        let dir = self.build_root.join(config.test_name());
        let main = build::build(self, config, &dir)?;
        run::run_and_check(self, config, &main, &dir)?;
        Ok(Completion::Completed)
    }
}

/// `expected-failures.txt`: test names, grouped under `# <cause>` comment lines.
struct ExpectedFailures {
    path: PathBuf,
    /// Test name to the cause it is listed under.
    tests: BTreeMap<String, String>,
}

impl ExpectedFailures {
    const HEADER: &str = "\
# Tests of the wasixv2 suite that are expected to fail, grouped by the cause the runner reported.
#
# A listed test that fails passes the run; a listed test that passes fails it, so take it off the
# list. A name that matches no test fails the `expected-failures` check. Regenerate the whole file
# with `WASIXV2_TESTS_BLESS=1`.
";

    fn load(path: PathBuf) -> Result<Self> {
        let mut tests = BTreeMap::new();
        if path.exists() {
            let text = std::fs::read_to_string(&path)
                .with_context(|| format!("reading {}", path.display()))?;
            let mut cause = String::new();
            for (i, line) in text.lines().enumerate() {
                let line = line.trim();
                if let Some(comment) = line.strip_prefix("## ") {
                    cause = comment.to_owned();
                } else if !line.is_empty() && !line.starts_with('#') {
                    ensure!(
                        tests.insert(line.to_owned(), cause.clone()).is_none(),
                        "{}:{}: `{line}` is listed twice",
                        path.display(),
                        i + 1
                    );
                }
            }
        }
        Ok(Self { path, tests })
    }

    fn write(&self) -> Result<()> {
        let mut by_cause: BTreeMap<&str, Vec<&str>> = BTreeMap::new();
        for (test, cause) in &self.tests {
            by_cause.entry(cause).or_default().push(test);
        }
        let mut out = String::from(Self::HEADER);
        for (cause, tests) in by_cause {
            out.push_str(&format!("\n## {cause}\n"));
            for test in tests {
                out.push_str(test);
                out.push('\n');
            }
        }
        std::fs::write(&self.path, out).with_context(|| format!("writing {}", self.path.display()))
    }
}

#[derive(Default)]
struct Tally {
    passed: usize,
    expected_failures: usize,
    unexpected_failures: usize,
    unexpected_passes: usize,
    ignored: usize,
    /// Every failure's cause, by test name, for blessing.
    failures: BTreeMap<String, String>,
}

fn main() -> ExitCode {
    match try_main() {
        Ok(code) => code,
        Err(e) => {
            eprintln!("error: {e:#}");
            ExitCode::from(2)
        }
    }
}

fn try_main() -> Result<ExitCode> {
    let args = Arguments::from_args();
    let configs = fixture::discover(&root().join("suite"))?;
    let expected_path = env_path("WASIXV2_TESTS_EXPECTED_FAILURES")
        .unwrap_or_else(|| root().join("expected-failures.txt"));
    let expected = ExpectedFailures::load(expected_path)?;
    let bless = std::env::var_os("WASIXV2_TESTS_BLESS").is_some();
    // Listing tests needs no toolchain.
    let suite = Arc::new(if args.list {
        None
    } else {
        Some(Suite::from_env()?)
    });
    let tally = Arc::new(Mutex::new(Tally::default()));

    let names: BTreeSet<String> = configs.iter().map(Config::test_name).collect();
    ensure!(
        names.len() == configs.len(),
        "two configurations share a test name"
    );
    let mut trials = vec![stale_entries_trial(&expected, &names)];
    for config in configs {
        let name = config.test_name();
        let listed = expected.tests.contains_key(&name);
        let suite = suite.clone();
        let tally = tally.clone();
        let trial = Trial::ignorable_test(name.clone(), move || {
            let suite = suite.as_ref().as_ref().expect("a toolchain outside --list");
            let result = suite.run(&config);
            let mut tally = tally.lock().unwrap();
            match (result, listed) {
                (Ok(Completion::Ignored { reason }), _) => {
                    tally.ignored += 1;
                    Ok(Completion::Ignored { reason })
                }
                (Ok(Completion::Completed), false) => {
                    tally.passed += 1;
                    Ok(Completion::Completed)
                }
                (Ok(Completion::Completed), true) => {
                    tally.unexpected_passes += 1;
                    if bless {
                        return Ok(Completion::Completed);
                    }
                    Err(Failed::from(
                        "passed, but expected-failures.txt lists it: take it off the list",
                    ))
                }
                (Err(failure), listed) => {
                    tally.failures.insert(name, failure.cause.clone());
                    if listed || bless {
                        tally.expected_failures += usize::from(listed);
                        tally.unexpected_failures += usize::from(!listed);
                        return Ok(Completion::Completed);
                    }
                    tally.unexpected_failures += 1;
                    Err(Failed::from(format!(
                        "{}\n{}",
                        failure.cause, failure.details
                    )))
                }
            }
        });
        trials.push(if listed {
            trial.with_kind("xfail")
        } else {
            trial
        });
    }

    let ran: BTreeSet<String> = trials
        .iter()
        .filter(|t| !args.is_filtered_out(t) && !args.is_ignored(t))
        .map(|t| t.name().to_owned())
        .collect();
    let conclusion = libtest_mimic::run(&args, trials);
    if args.list {
        return Ok(ExitCode::SUCCESS);
    }

    let tally = tally.lock().unwrap();
    println!(
        "wasixv2 suite: {} passed, {} failed as expected, {} failed unexpectedly, \
         {} passed unexpectedly, {} ignored",
        tally.passed,
        tally.expected_failures,
        tally.unexpected_failures,
        tally.unexpected_passes,
        tally.ignored
    );
    if bless {
        // Tests outside this run keep their entries.
        let mut tests: BTreeMap<String, String> = expected
            .tests
            .iter()
            .filter(|(name, _)| !ran.contains(*name) && names.contains(*name))
            .map(|(n, c)| (n.clone(), c.clone()))
            .collect();
        tests.extend(tally.failures.clone());
        let blessed = ExpectedFailures {
            path: expected.path.clone(),
            tests,
        };
        blessed.write()?;
        println!("wrote {}", blessed.path.display());
        return Ok(ExitCode::SUCCESS);
    }
    Ok(conclusion.exit_code())
}

/// Fails when `expected-failures.txt` names a test that does not exist.
fn stale_entries_trial(expected: &ExpectedFailures, names: &BTreeSet<String>) -> Trial {
    let stale: Vec<String> = expected
        .tests
        .keys()
        .filter(|name| !names.contains(*name))
        .cloned()
        .collect();
    let path: PathBuf = expected.path.clone();
    Trial::test("expected-failures", move || {
        if stale.is_empty() {
            return Ok(());
        }
        Err(Failed::from(format!(
            "{} lists tests that do not exist:\n  {}",
            display(&path),
            stale.join("\n  ")
        )))
    })
}

fn display(path: &Path) -> String {
    path.display().to_string()
}
