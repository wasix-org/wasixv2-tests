//! Runs the wasixv2 test suite: builds every fixture under `suite/`, and every test of the external
//! suites checked out under `external/`, with wasixcc for `wasm32-wasixv2` and runs it with a
//! `wasmer` binary. See the README for the environment it needs.
//!
//! Most tests fail until wasixv2 is complete. `expected-failures.txt` lists them; a listed test
//! that fails passes the run, and one that passes fails it so the list keeps shrinking.

mod build;
mod external;
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

/// Everything a test needs from the environment.
pub struct Suite {
    pub wasmer: PathBuf,
    pub cc: String,
    pub cxx: String,
    pub build_root: PathBuf,
    /// `WASIXV2_TESTS_TIMEOUT_SECS`, for every test; each source has its own default.
    timeout: Option<Duration>,
}

/// How a test that did not fail ended.
pub enum Outcome {
    Passed,
    Ignored(String),
    /// The test needs an optional feature that is missing, such as Open POSIX's
    /// `PTS_UNSUPPORTED`.
    Unsupported(String),
    /// The test could not check what it tests, such as Open POSIX's `PTS_UNTESTED`.
    Untested(String),
}

/// A fixture, or a test of an external suite.
enum Test {
    Fixture(Box<Config>),
    External(Box<dyn external::ExternalTest>),
}

impl Test {
    fn name(&self) -> String {
        match self {
            Self::Fixture(config) => config.test_name(),
            Self::External(test) => test.name().to_owned(),
        }
    }
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
            Ok(secs) => Some(Duration::from_secs(
                secs.parse().context("WASIXV2_TESTS_TIMEOUT_SECS")?,
            )),
            Err(_) => None,
        };
        let suite = Self {
            wasmer,
            cc: "wasixcc".to_owned(),
            cxx: "wasix++".to_owned(),
            build_root: env_path("WASIXV2_TESTS_BUILD_DIR")
                .unwrap_or_else(|| root().join("target/suite")),
            timeout,
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

    pub fn timeout_or(&self, default: Duration) -> Duration {
        self.timeout.unwrap_or(default)
    }

    fn run(&self, test: &Test) -> Result<Outcome, Failure> {
        match test {
            Test::Fixture(config) => self.run_fixture(config),
            Test::External(test) => test.run(self),
        }
    }

    fn run_fixture(&self, config: &Config) -> Result<Outcome, Failure> {
        if let Some(reason) = &config.ignored {
            return Ok(Outcome::Ignored(reason.clone()));
        }
        if config.unix_only && !cfg!(unix) {
            return Ok(Outcome::Ignored("Unix only".to_owned()));
        }
        if config.ignore_macos && cfg!(target_os = "macos") {
            return Ok(Outcome::Ignored("ignored on macOS".to_owned()));
        }
        // The wasmer under test runs on its default backend, Cranelift.
        if let Some((_, reason)) = config
            .skipped_engines
            .iter()
            .find(|(e, _)| e == "cranelift")
        {
            return Ok(Outcome::Ignored(reason.clone()));
        }
        let dir = self.build_root.join(config.test_name());
        let main = build::build(self, config, &dir)?;
        run::run_and_check(self, config, &main, &dir)?;
        Ok(Outcome::Passed)
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
    unsupported: usize,
    untested: usize,
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
    let mut tests: Vec<Test> = fixture::discover(&root().join("suite"))?
        .into_iter()
        .map(|config| Test::Fixture(Box::new(config)))
        .collect();
    let external::Discovered {
        tests: external,
        missing,
    } = external::discover(&root())?;
    tests.extend(external.into_iter().map(Test::External));
    if !missing.is_empty() {
        eprintln!(
            "note: not checked out, so their tests do not run: {}; \
             run `git submodule update --init` in {}",
            missing.join(", "),
            root().display()
        );
    }
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

    let names: BTreeSet<String> = tests.iter().map(Test::name).collect();
    ensure!(names.len() == tests.len(), "two tests share a name");
    // Without its submodule, an external suite's entries cannot be checked, so they stay.
    let known = |name: &str| {
        names.contains(name)
            || missing
                .iter()
                .any(|suite| name.split('/').next() == Some(*suite))
    };
    let mut trials = vec![stale_entries_trial(&expected, &known)];
    for test in tests {
        let name = test.name();
        let listed = expected.tests.contains_key(&name);
        let (kind, opt_in) = match &test {
            Test::External(t) => (t.kind(), t.opt_in()),
            Test::Fixture(_) => ("", false),
        };
        let suite = suite.clone();
        let tally = tally.clone();
        let trial = Trial::ignorable_test(name.clone(), move || {
            let suite = suite.as_ref().as_ref().expect("a toolchain outside --list");
            let result = suite.run(&test);
            let mut tally = tally.lock().unwrap();
            let not_tested = match result {
                Ok(Outcome::Ignored(reason)) => {
                    tally.ignored += 1;
                    return Ok(Completion::ignored_with(reason));
                }
                Ok(Outcome::Passed) if !listed => {
                    tally.passed += 1;
                    return Ok(Completion::Completed);
                }
                Ok(Outcome::Unsupported(detail)) if !listed => {
                    tally.unsupported += 1;
                    return Ok(Completion::ignored_with(format!(
                        "PTS_UNSUPPORTED: {detail}"
                    )));
                }
                Ok(Outcome::Untested(detail)) if !listed => {
                    tally.untested += 1;
                    return Ok(Completion::ignored_with(format!("PTS_UNTESTED: {detail}")));
                }
                Ok(Outcome::Passed) => "passed",
                Ok(Outcome::Unsupported(_)) => "reported PTS_UNSUPPORTED",
                Ok(Outcome::Untested(_)) => "reported PTS_UNTESTED",
                Err(failure) => {
                    tally.failures.insert(name, failure.cause.clone());
                    if listed || bless {
                        tally.expected_failures += usize::from(listed);
                        tally.unexpected_failures += usize::from(!listed);
                        return Ok(Completion::Completed);
                    }
                    tally.unexpected_failures += 1;
                    return Err(Failed::from(format!(
                        "{}\n{}",
                        failure.cause, failure.details
                    )));
                }
            };
            // Listed, but it no longer fails.
            tally.unexpected_passes += 1;
            if bless {
                return Ok(Completion::Completed);
            }
            Err(Failed::from(format!(
                "{not_tested}, but expected-failures.txt lists it: take it off the list"
            )))
        });
        let kind = if listed { "xfail" } else { kind };
        trials.push(trial.with_kind(kind).with_ignored_flag(opt_in));
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
         {} passed unexpectedly, {} ignored, {} unsupported, {} untested",
        tally.passed,
        tally.expected_failures,
        tally.unexpected_failures,
        tally.unexpected_passes,
        tally.ignored,
        tally.unsupported,
        tally.untested
    );
    if bless {
        // Tests outside this run keep their entries.
        let mut tests: BTreeMap<String, String> = expected
            .tests
            .iter()
            .filter(|(name, _)| !ran.contains(*name) && known(name))
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
fn stale_entries_trial(expected: &ExpectedFailures, known: &impl Fn(&str) -> bool) -> Trial {
    let stale: Vec<String> = expected
        .tests
        .keys()
        .filter(|name| !known(name))
        .cloned()
        .collect();
    let path = expected.path.clone();
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
