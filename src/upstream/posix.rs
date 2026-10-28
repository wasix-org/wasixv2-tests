//! The Open POSIX Test Suite (`testcases/open_posix_testsuite` in `upstream/ltp`): one program per
//! assertion, reporting through its exit status. `-buildonly` tests only have to compile. Tests
//! that fork are built for the asyncify configuration, the rest for exception handling.

use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::OnceLock;

use anyhow::{Result, ensure};
use walkdir::WalkDir;

use super::{
    BuildConfig, Case, ExcludeList, Excluded, Invocation, Outcome, build_program, run_build,
    run_wasm, wasixcc_env, wasixcc_env_for,
};
use crate::{Failure, Suite};

/// `posixtest.h`'s exit statuses.
const PTS_PASS: i32 = 0;
const PTS_FAIL: i32 = 1;
const PTS_UNRESOLVED: i32 = 2;
const PTS_UNSUPPORTED: i32 = 4;
const PTS_UNTESTED: i32 = 5;

/// The parts of the suite that are tests; `stress/` runs for far longer than a test timeout.
const DIRS: &[&str] = &["conformance", "functional"];

const SUITE_DIR: &str = "testcases/open_posix_testsuite";

pub struct Test {
    name: String,
    /// Path under the suite directory.
    file: String,
    build_only: bool,
}

struct Prepared {
    root: PathBuf,
    /// `lib/common.c`, built once for exception handling and once for asyncify.
    common: PathBuf,
    common_asyncify: PathBuf,
}

impl Prepared {
    fn common(&self, config: BuildConfig) -> &Path {
        match config {
            BuildConfig::ExceptionHandling => &self.common,
            BuildConfig::Asyncify => &self.common_asyncify,
        }
    }
}

static PREPARED: OnceLock<Result<Prepared, String>> = OnceLock::new();

fn is_test_file(name: &str) -> bool {
    let Some(stem) = name.strip_suffix(".c") else {
        return false;
    };
    let stem = stem.strip_suffix("-buildonly").unwrap_or(stem);
    let mut parts = stem.split('-');
    let numeric =
        |p: Option<&str>| p.is_some_and(|p| !p.is_empty() && p.bytes().all(|b| b.is_ascii_digit()));
    numeric(parts.next())
        && match parts.next() {
            None => name.ends_with("-buildonly.c"),
            p => numeric(p) && parts.next().is_none(),
        }
}

pub fn discover(excludes: &ExcludeList, excluded: &mut Excluded) -> Result<Vec<(String, Case)>> {
    let root = super::upstream_dir().join("ltp").join(SUITE_DIR);
    ensure!(
        root.is_dir(),
        "upstream/ltp is not checked out: run `git submodule update --init --recursive`"
    );
    let mut tests = Vec::new();
    for dir in DIRS {
        let mut files: Vec<String> = WalkDir::new(root.join(dir))
            .into_iter()
            .filter_map(Result::ok)
            .filter(|e| e.file_type().is_file() && is_test_file(&e.file_name().to_string_lossy()))
            .map(|e| {
                e.path()
                    .strip_prefix(&root)
                    .unwrap()
                    .to_string_lossy()
                    .into_owned()
            })
            .collect();
        files.sort();
        for file in files {
            let name = format!("posix/{}", file.trim_end_matches(".c"));
            if let Some(reason) = excludes.reason(&name) {
                excluded.add(reason);
                continue;
            }
            let test = Test {
                name: name.clone(),
                build_only: file.ends_with("-buildonly.c"),
                file,
            };
            tests.push((name, Case::Posix(test)));
        }
    }
    Ok(tests)
}

fn prepare(suite: &Suite) -> Result<Prepared> {
    let root = super::source_copy(suite, "ltp")?.join(SUITE_DIR);
    let common = suite.build_root.join("posix-common.o");
    let common_asyncify = suite.build_root.join("posix-common-asyncify.o");
    for (config, object) in [
        (BuildConfig::ExceptionHandling, &common),
        (BuildConfig::Asyncify, &common_asyncify),
    ] {
        let mut cmd = Command::new(&suite.cc);
        wasixcc_env_for(&mut cmd, config)
            .arg("-I")
            .arg(root.join("include"))
            .arg("-c")
            .arg(root.join("lib/common.c"))
            .arg("-o")
            .arg(object);
        run_build(&mut cmd).map_err(|f| anyhow::anyhow!("{}\n{}", f.cause, f.details))?;
    }
    Ok(Prepared {
        root,
        common,
        common_asyncify,
    })
}

/// The flags in a test directory's `CFLAGS`, `LDFLAGS` or `LDLIBS` file, as the suite's
/// generated makefiles use them: through the shell, since some probe the compiler.
fn dir_flags(suite: &Suite, dir: &Path, file: &str) -> Vec<String> {
    let Ok(text) = std::fs::read_to_string(dir.join(file)) else {
        return Vec::new();
    };
    if !text.contains('`') {
        return text.split_whitespace().map(str::to_owned).collect();
    }
    let cc = &suite.cc;
    // The probes compile into the working directory; keep that out of the tree.
    let Ok(scratch) = tempfile::tempdir() else {
        return Vec::new();
    };
    let mut cmd = Command::new("sh");
    wasixcc_env(&mut cmd)
        .current_dir(scratch.path())
        .arg("-c")
        .arg(format!("echo {}", text.trim().replace("$(CC)", cc)));
    cmd.output()
        .map(|o| {
            String::from_utf8_lossy(&o.stdout)
                .split_whitespace()
                .map(str::to_owned)
                .collect()
        })
        .unwrap_or_default()
}

impl Test {
    pub fn run(&self, suite: &Suite) -> Result<Outcome, Failure> {
        let prepared = PREPARED
            .get_or_init(|| prepare(suite).map_err(|e| format!("{e:#}")))
            .as_ref()
            .map_err(|e| Failure::harness(format!("preparing the Open POSIX suite: {e}")))?;
        let source = prepared.root.join(&self.file);
        let src_dir = source.parent().unwrap();
        let dir = suite.build_root.join(&self.name);
        std::fs::create_dir_all(&dir).map_err(|e| Failure::harness(e.to_string()))?;
        let compile = |config| {
            let mut cmd = Command::new(&suite.cc);
            wasixcc_env_for(&mut cmd, config)
                .arg("-I")
                .arg(prepared.root.join("include"))
                .arg("-I")
                .arg(src_dir)
                .args(dir_flags(suite, src_dir, "CFLAGS"))
                .arg(&source);
            cmd
        };
        if self.build_only {
            let mut cmd = compile(BuildConfig::ExceptionHandling);
            cmd.arg("-c").arg("-o").arg(dir.join("main.o"));
            run_build(&mut cmd)?;
            return Ok(Outcome::Passed);
        }
        let main = dir.join("main");
        build_program(&main, |config| {
            let mut cmd = compile(config);
            cmd.arg(prepared.common(config))
                .args(dir_flags(suite, src_dir, "LDFLAGS"))
                .arg("-o")
                .arg(&main)
                .args(dir_flags(suite, src_dir, "LDLIBS"))
                .args(["-lpthread", "-lrt", "-lm"]);
            run_build(&mut cmd)
        })?;
        let ran = run_wasm(
            suite,
            &Invocation {
                module: main,
                ..Default::default()
            },
            &dir,
        )?;
        let cause = match ran.output.status {
            Some(PTS_PASS) => return Ok(Outcome::Passed),
            Some(PTS_UNSUPPORTED) => return Ok(Outcome::Skipped("UNSUPPORTED".to_owned())),
            Some(PTS_UNTESTED) => return Ok(Outcome::Skipped("UNTESTED".to_owned())),
            Some(PTS_FAIL) => "FAIL",
            Some(PTS_UNRESOLVED) => "UNRESOLVED",
            other => return Err(ran.fail(suite, super::libc_test::exit_cause(other))),
        };
        // The suite's tests print what failed, usually on the last line of stdout; stderr is
        // wasmer's, a trap and its backtrace.
        let last = |text: &str| {
            text.lines()
                .rev()
                .find(|l| !l.trim().is_empty())
                .map(str::to_owned)
        };
        let line = last(&ran.output.stdout)
            .or_else(|| {
                super::first_line(&ran.output.stderr, |l| !l.trim().is_empty()).map(str::to_owned)
            })
            .unwrap_or_default();
        let cause = super::cause_from_line(&format!("{cause}: "), &line);
        Err(ran.fail(suite, cause))
    }
}
