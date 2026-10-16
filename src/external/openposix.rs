//! The Open POSIX Test Suite, `testcases/open_posix_testsuite` in the LTP submodule.
//!
//! Tests are discovered and built the way the suite's own build does it
//! (`scripts/locate-test` and `scripts/generate-makefiles.sh`), and each runs with `wasmer run`
//! in its own directory, as `bin/run-tests.sh` runs it. Its exit status is a result code from
//! `include/posixtest.h`.

use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::{Arc, OnceLock};
use std::time::Duration;

use anyhow::{Context, Result, bail, ensure};
use walkdir::WalkDir;

use super::{ExternalSuite, ExternalTest};
use crate::{Failure, Outcome, Suite, build, run};

pub struct OpenPosix;

/// The suite inside the LTP checkout.
const SUITE_DIR: &str = "testcases/open_posix_testsuite";

/// `bin/run-tests.sh`'s default `TIMEOUT_VAL`.
const DEFAULT_TIMEOUT: Duration = Duration::from_secs(300);

/// Where the test's directory appears in the guest, and its working directory.
const GUEST_DIR: &str = "/t";

impl ExternalSuite for OpenPosix {
    fn name(&self) -> &'static str {
        "openposix"
    }

    fn submodule(&self) -> &'static str {
        "external/ltp"
    }

    fn discover(&self, checkout: &Path) -> Result<Vec<Box<dyn ExternalTest>>> {
        let top = checkout.join(SUITE_DIR);
        let shared = Arc::new(Shared {
            top: top.clone(),
            flags: OnceLock::new(),
        });
        let mut tests: Vec<Box<dyn ExternalTest>> = Vec::new();
        for group in ["conformance", "functional", "stress"] {
            let mut files: Vec<PathBuf> = WalkDir::new(top.join(group))
                .into_iter()
                .filter_map(Result::ok)
                .filter(|e| e.file_type().is_file())
                .map(|e| e.into_path())
                .collect();
            files.sort();
            for path in files {
                let rel = path
                    .strip_prefix(&top)?
                    .to_string_lossy()
                    .replace('\\', "/");
                let file = path.file_name().unwrap().to_string_lossy();
                let (stem, ext) = file.rsplit_once('.').unwrap_or((&file, ""));
                let kind = match ext {
                    "c" if stem.contains("buildonly") => Kind::BuildOnly,
                    "c" if group == "functional"
                        || stem.ends_with(|c: char| c.is_ascii_digit()) =>
                    {
                        Kind::Run
                    }
                    "sh" if is_numbered(stem) => {
                        // The suite's shell tests all stand in for assertions that other tests
                        // cover, and run on the host, so there is nothing to run under wasmer.
                        let text = std::fs::read_to_string(&path)?;
                        ensure!(
                            text.contains("implicitly"),
                            "{rel} is a shell test that is not a placeholder; \
                             the runner cannot run it"
                        );
                        continue;
                    }
                    _ => continue,
                };
                tests.push(Box::new(Test {
                    name: format!("{}/{}", self.name(), rel.strip_suffix(".c").unwrap()),
                    kind,
                    opt_in: group == "stress" || rel.contains("/speculative/"),
                    source: path,
                    shared: shared.clone(),
                }));
            }
        }
        ensure!(!tests.is_empty(), "no tests under {}", top.display());
        Ok(tests)
    }
}

/// What the suite's build does with a test.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Kind {
    /// Built and run: `NUMBER-NUMBER.c` under `conformance` and `stress`, any `.c` under
    /// `functional`.
    Run,
    /// Only compiled (`-c`): `*-buildonly.c`.
    BuildOnly,
}

/// One test of the suite.
struct Test {
    /// `openposix/<path without .c>`, e.g. `openposix/conformance/interfaces/sem_wait/1-1`.
    name: String,
    kind: Kind,
    /// The `stress` tests, and the `speculative` ones, which the suite builds but its
    /// `make test` does not run.
    opt_in: bool,
    source: PathBuf,
    shared: Arc<Shared>,
}

/// What every test of one checkout shares.
struct Shared {
    /// The suite's top directory.
    top: PathBuf,
    /// Built by the first test that needs them.
    flags: OnceLock<Result<Flags, String>>,
}

/// `NUMBER-NUMBER`, as `locate-test` names shell tests.
fn is_numbered(stem: &str) -> bool {
    stem.split_once('-').is_some_and(|(a, b)| {
        a.starts_with(|c: char| c.is_ascii_digit()) && b.starts_with(|c: char| c.is_ascii_digit())
    })
}

/// The flags the suite's build passes to every compile.
struct Flags {
    /// `CFLAGS` from `include/mk/config.mk.in`, after autoconf's default.
    cflags: Vec<String>,
    /// `lib/common.c`, the `main` that calls each test's `test_main`, built once.
    common: Result<PathBuf, String>,
}

impl ExternalTest for Test {
    fn name(&self) -> &str {
        &self.name
    }

    fn kind(&self) -> &'static str {
        match self.kind {
            Kind::Run => "",
            Kind::BuildOnly => "buildonly",
        }
    }

    fn opt_in(&self) -> bool {
        self.opt_in
    }

    fn run(&self, suite: &Suite) -> Result<Outcome, Failure> {
        let top = &self.shared.top;
        let flags = self
            .shared
            .flags
            .get_or_init(|| Flags::new(suite, top).map_err(|e| format!("{e:#}")))
            .as_ref()
            .map_err(|e| Failure::harness(e.clone()))?;
        let dir = super::build_dir(suite, &self.name)?;
        let program = self.build(suite, top, flags, &dir)?;
        if self.kind == Kind::BuildOnly {
            return Ok(Outcome::Passed);
        }
        self.execute(suite, &program, &dir)
    }
}

impl Test {
    /// The directory the test's source and its `CFLAGS`, `LDFLAGS` and `LDLIBS` files live in.
    fn src_dir(&self) -> &Path {
        self.source.parent().unwrap()
    }

    /// The program's name as the suite's Makefiles call it: `<directory>_<test>.run-test`, or
    /// `.test` for an object of a build-only test.
    fn program_name(&self) -> String {
        let dir = self.src_dir();
        let mut prefix = dir.file_name().unwrap().to_string_lossy().into_owned();
        if prefix == "speculative" {
            let parent = dir.parent().unwrap().file_name().unwrap().to_string_lossy();
            prefix = format!("{parent}_speculative");
        }
        let stem = self.source.file_stem().unwrap().to_string_lossy();
        let suffix = match self.kind {
            Kind::Run => "run-test",
            Kind::BuildOnly => "test",
        };
        format!("{prefix}_{stem}.{suffix}")
    }

    /// Builds the test as its generated Makefile would:
    /// `$(CC) [-c] $(CFLAGS) $(LDFLAGS) -o $@ $(srcdir)/<test>.c [$(LDLIBS)]`.
    fn build(
        &self,
        suite: &Suite,
        top: &Path,
        flags: &Flags,
        dir: &Path,
    ) -> Result<PathBuf, Failure> {
        let src_dir = self.src_dir();
        let program = dir.join(self.program_name());
        let mut cmd = Command::new(&suite.cc);
        if self.kind == Kind::BuildOnly {
            cmd.arg("-c");
        }
        cmd.args(&flags.cflags)
            .arg("-I")
            .arg(top.join("include"))
            .arg("-I")
            .arg(src_dir);
        let local = |file: &str| {
            local_flags(suite, &src_dir.join(file), dir)
                .map_err(|e| Failure::harness(format!("{e:#}")))
        };
        cmd.args(local("CFLAGS")?)
            .args(local("LDFLAGS")?)
            .arg("-o")
            .arg(&program)
            .arg(&self.source);
        if self.kind == Kind::Run {
            let common = flags.common.as_ref().map_err(|e| {
                Failure::new("build: lib/common.c", format!("building lib/common.c: {e}"))
            })?;
            cmd.arg(common).args(local("LDLIBS")?);
        }
        build::target_wasixv2(&mut cmd).current_dir(dir);
        build::run_build(&mut cmd)?;
        if self.kind == Kind::Run {
            build::check_module(&program)?;
        }
        Ok(program)
    }

    /// Runs the program in its directory, as `bin/run-tests.sh` does, with the arguments from
    /// `<program>.args` if there is one, and maps its result code to an outcome.
    fn execute(&self, suite: &Suite, program: &Path, dir: &Path) -> Result<Outcome, Failure> {
        let name = self.program_name();
        let stem = name.split('.').next().unwrap();
        let args_file = self.src_dir().join(format!("{stem}.args"));
        let args = match std::fs::read_to_string(&args_file) {
            Ok(text) => text.split_whitespace().map(str::to_owned).collect(),
            Err(_) => Vec::new(),
        };
        let mut cmd = Command::new(&suite.wasmer);
        cmd.arg("run")
            .arg("--volume")
            .arg(format!("{}:{GUEST_DIR}", dir.display()))
            .arg("--cwd")
            .arg(GUEST_DIR)
            .arg(program)
            .arg("--")
            .args(args)
            .current_dir(dir);
        run::run_checked(
            suite,
            &mut cmd,
            None,
            suite.timeout_or(DEFAULT_TIMEOUT),
            result,
        )
    }
}

/// Maps a test's exit status, a result code from `include/posixtest.h`, to an outcome.
fn result(output: &run::Output) -> Result<Outcome, Failure> {
    let status = output.exit_status()?;
    let last_line = || {
        fn last(text: &str) -> Option<&str> {
            text.lines().map(str::trim).rfind(|l| !l.is_empty())
        }
        let line = last(&output.stdout)
            .or_else(|| last(&output.stderr))
            .unwrap_or("no output");
        line.chars().take(160).collect::<String>()
    };
    let cause = match status {
        0 => return Ok(Outcome::Passed),
        4 => return Ok(Outcome::Unsupported(last_line())),
        5 => return Ok(Outcome::Untested(last_line())),
        1 => "PTS_FAIL".to_owned(),
        2 => "PTS_UNRESOLVED".to_owned(),
        // wasmer exits with 128 + the number of the signal that killed the guest.
        status if status > 128 => format!("killed by {}", signal_name(status - 128)),
        status => format!("exit status {status}"),
    };
    Err(Failure::new(cause, output.captured()))
}

fn signal_name(signal: i32) -> String {
    const NAMES: [&str; 31] = [
        "SIGHUP",
        "SIGINT",
        "SIGQUIT",
        "SIGILL",
        "SIGTRAP",
        "SIGABRT",
        "SIGBUS",
        "SIGFPE",
        "SIGKILL",
        "SIGUSR1",
        "SIGSEGV",
        "SIGUSR2",
        "SIGPIPE",
        "SIGALRM",
        "SIGTERM",
        "SIGSTKFLT",
        "SIGCHLD",
        "SIGCONT",
        "SIGSTOP",
        "SIGTSTP",
        "SIGTTIN",
        "SIGTTOU",
        "SIGURG",
        "SIGXCPU",
        "SIGXFSZ",
        "SIGVTALRM",
        "SIGPROF",
        "SIGWINCH",
        "SIGIO",
        "SIGPWR",
        "SIGSYS",
    ];
    match usize::try_from(signal - 1).ok().and_then(|i| NAMES.get(i)) {
        Some(name) => (*name).to_owned(),
        None => format!("signal {signal}"),
    }
}

impl Flags {
    fn new(suite: &Suite, top: &Path) -> Result<Self> {
        // autoconf's default CFLAGS for gcc and clang, which `configure` puts first.
        let mut cflags = vec!["-g".to_owned(), "-O2".to_owned()];
        let config = top.join("include/mk/config.mk.in");
        let text = std::fs::read_to_string(&config)
            .with_context(|| format!("reading {}", config.display()))?;
        let fixed: Vec<&str> = text
            .lines()
            .filter_map(|l| l.strip_prefix("CFLAGS"))
            .filter_map(|l| l.trim_start().strip_prefix("+="))
            .filter(|l| !l.contains('@'))
            .collect();
        ensure!(!fixed.is_empty(), "no CFLAGS in {}", config.display());
        cflags.extend(
            fixed
                .iter()
                .flat_map(|l| l.split_whitespace().map(str::to_owned)),
        );

        let dir = suite.build_root.join(OpenPosix.name()).join("lib");
        let common = Self::build_common(suite, top, &cflags, &dir).map_err(|f| f.details);
        Ok(Self { cflags, common })
    }

    /// `lib/libcommon.a`'s one object, linked into every program in its place.
    fn build_common(
        suite: &Suite,
        top: &Path,
        cflags: &[String],
        dir: &Path,
    ) -> Result<PathBuf, Failure> {
        std::fs::create_dir_all(dir)
            .map_err(|e| Failure::harness(format!("creating {}: {e}", dir.display())))?;
        let object = dir.join("common.o");
        let mut cmd = Command::new(&suite.cc);
        cmd.arg("-c")
            .args(cflags)
            .arg("-I")
            .arg(top.join("include"))
            .arg("-o")
            .arg(&object)
            .arg(top.join("lib/common.c"));
        build::target_wasixv2(&mut cmd).current_dir(dir);
        build::run_build(&mut cmd)?;
        Ok(object)
    }
}

/// The words of a directory's `CFLAGS`, `LDFLAGS` or `LDLIBS` file, which the generated Makefile
/// appends to the variable of that name. make expands `$(VAR)` in them and the shell running the
/// recipe expands the rest, such as a command substitution that probes the compiler; it runs in
/// the build directory `dir`, as the recipe does.
fn local_flags(suite: &Suite, file: &Path, dir: &Path) -> Result<Vec<String>> {
    let text = match std::fs::read_to_string(file) {
        Ok(text) => text,
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Ok(Vec::new()),
        Err(e) => bail!("reading {}: {e}", file.display()),
    };
    let text = text.trim().replace("$(CC)", "\"$CC\"");
    if !text.contains(['$', '`', '"', '\'', '\\']) {
        return Ok(text.split_whitespace().map(str::to_owned).collect());
    }
    let mut cmd = Command::new("sh");
    cmd.arg("-c")
        .arg(format!("set -- {text}; printf '%s\\n' \"$@\""))
        .env("CC", &suite.cc)
        .current_dir(dir);
    build::target_wasixv2(&mut cmd);
    let output = cmd
        .output()
        .with_context(|| format!("expanding {}", file.display()))?;
    ensure!(
        output.status.success(),
        "expanding {}: {}",
        file.display(),
        String::from_utf8_lossy(&output.stderr)
    );
    Ok(String::from_utf8_lossy(&output.stdout)
        .lines()
        .filter(|l| !l.is_empty())
        .map(str::to_owned)
        .collect())
}
