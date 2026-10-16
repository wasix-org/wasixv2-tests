//! musl's libc-test, at `external/libc-test`.
//!
//! Tests are discovered and built the way its `Makefile` does it, from `src/functional`,
//! `src/regression` and `src/math`, and each runs with `wasmer run` from a directory laid out like
//! the suite's tree, as `make run` runs it through `src/common/runtest.c`. A test passes when it
//! exits with status 0.

use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::{Arc, OnceLock};
use std::time::Duration;

use anyhow::{Context, Result, ensure};

use super::{ExternalSuite, ExternalTest};
use crate::{Failure, Outcome, Suite, build, run};

pub struct LibcTest;

/// The directories with tests the suite builds into programs and runs.
///
/// `src/api` is compile-only and `src/musl` tests musl internals.
const GROUPS: &[&str] = &["functional", "regression", "math"];

/// The suite's own timeout is `runtest`'s 5 seconds. This leaves room for wasmer compiling the
/// module and for the tests that fill memory, such as `regression/malloc-oom`, which take seconds
/// to exhaust wasm32's 4 GiB, while the whole suite runs in parallel.
const DEFAULT_TIMEOUT: Duration = Duration::from_secs(60);

/// Where the test's directory appears in the guest, and its working directory: the top of a tree
/// like the suite's, so paths such as `src/functional/tls_init_dso.so` resolve as in `make run`.
const GUEST_DIR: &str = "/t";

/// The archiver that comes with wasixcc.
const AR: &str = "wasixar";

/// The line of `config.mak.def` where the settings that every libc apply end.
const GLIBC_SETTINGS: &str = "# glibc specific settings";

impl ExternalSuite for LibcTest {
    fn name(&self) -> &'static str {
        "libc-test"
    }

    fn submodule(&self) -> &'static str {
        "external/libc-test"
    }

    fn discover(&self, checkout: &Path) -> Result<Vec<Box<dyn ExternalTest>>> {
        let shared = Arc::new(Shared {
            top: checkout.to_path_buf(),
            flags: OnceLock::new(),
        });
        let mut tests: Vec<Box<dyn ExternalTest>> = Vec::new();
        for group in GROUPS {
            let dir = checkout.join("src").join(group);
            let mut sources: Vec<PathBuf> = std::fs::read_dir(&dir)
                .with_context(|| format!("reading {}", dir.display()))?
                .map(|e| e.map(|e| e.path()))
                .collect::<Result<_, _>>()?;
            sources.retain(|p| p.extension().is_some_and(|e| e == "c"));
            sources.sort();
            for source in sources {
                let stem = source.file_stem().unwrap().to_string_lossy().into_owned();
                let mk = Mk::read(&source.with_extension("mk"))?;
                let link = match &mk.bins {
                    // A shared object other tests load, such as `dlopen_dso`.
                    Some(bins) if bins.is_empty() => continue,
                    // Only `$(N).exe`, which loads a shared object.
                    Some(_) => Link::Dynamic,
                    // The default: `math` builds `bin.exe`, the others also `bin-static.exe`.
                    None if *group == "math" => Link::Default,
                    None => Link::Static,
                };
                tests.push(Box::new(Test {
                    name: format!("{}/{group}/{stem}", self.name()),
                    group,
                    stem,
                    link,
                    mk,
                    source,
                    shared: shared.clone(),
                }));
            }
        }
        ensure!(!tests.is_empty(), "no tests under {}", checkout.display());
        Ok(tests)
    }
}

/// Which of the suite's programs for a test is built and run.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Link {
    /// `bin-static.exe`, linked with `-static`.
    Static,
    /// `bin.exe`, the only program the suite builds for `math`.
    Default,
    /// `bin.exe` of a test whose `.mk` builds only that, because it loads a shared object.
    Dynamic,
}

/// What a test's `src/<group>/<test>.mk` sets, in the forms the suite uses.
#[derive(Debug, Default)]
struct Mk {
    /// `$(N).BINS`, when set: empty for a shared object that is no test itself.
    bins: Option<String>,
    /// `$(N).CFLAGS`.
    cflags: Vec<String>,
    /// `$(N).LDFLAGS`, for `bin.exe`.
    ldflags: Vec<String>,
    /// `$(N).LDLIBS`, for `bin.exe`.
    ldlibs: Vec<String>,
    /// `$(N)-static.LDLIBS`, for `bin-static.exe`.
    static_ldlibs: Vec<String>,
    /// The shared objects `$(N).err` (the run) depends on, as `$(B)/$(D)/<name>.so`.
    run_deps: Vec<String>,
}

impl Mk {
    fn read(path: &Path) -> Result<Self> {
        let text = match std::fs::read_to_string(path) {
            Ok(text) => text,
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Ok(Self::default()),
            Err(e) => return Err(e).with_context(|| format!("reading {}", path.display())),
        };
        let mut mk = Self::default();
        for line in text.lines().map(str::trim) {
            if let Some((var, value)) = line.split_once(":=") {
                let value = value.trim();
                let words = || value.split_whitespace().map(str::to_owned).collect();
                match var.trim() {
                    "$(N).BINS" => mk.bins = Some(value.to_owned()),
                    "$(N).CFLAGS" => mk.cflags = words(),
                    "$(N).LDFLAGS" => mk.ldflags = words(),
                    "$(N).LDLIBS" => mk.ldlibs = words(),
                    "$(N)-static.LDLIBS" => mk.static_ldlibs = words(),
                    "$(N).LIBS" => {}
                    other => anyhow::bail!("{}: unknown variable {other}", path.display()),
                }
            } else if let Some(deps) = line.strip_prefix("$(B)/$(N).err:") {
                mk.run_deps
                    .extend(deps.split_whitespace().map(str::to_owned));
            }
        }
        Ok(mk)
    }
}

/// One test of the suite.
struct Test {
    /// `libc-test/<group>/<test>`, e.g. `libc-test/functional/pthread_cond`.
    name: String,
    group: &'static str,
    stem: String,
    link: Link,
    mk: Mk,
    source: PathBuf,
    shared: Arc<Shared>,
}

/// What every test of one checkout shares.
struct Shared {
    /// The checkout.
    top: PathBuf,
    /// Built by the first test that needs them.
    flags: OnceLock<Result<Flags, String>>,
}

/// The flags and library the suite's build uses for every test.
struct Flags {
    /// The `Makefile`'s `-Isrc/common`, then `CFLAGS` from `config.mak.def`.
    cflags: Vec<String>,
    ldflags: Vec<String>,
    ldlibs: Vec<String>,
    /// `src/common/libtest.a`, built once.
    libtest: Result<PathBuf, String>,
}

impl ExternalTest for Test {
    fn name(&self) -> &str {
        &self.name
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
        let program = self.build(suite, flags, &dir)?;
        self.execute(suite, &program, &dir)
    }
}

impl Test {
    /// Where the suite's tree has this test's programs, in the build directory `dir`:
    /// `$(B)/$(D)`.
    fn out_dir(&self, dir: &Path) -> PathBuf {
        dir.join("src").join(self.group)
    }

    /// Expands a word from the test's `.mk` as make and the recipe's shell would: `$(B)/$(D)/`,
    /// `$$`, and quotes.
    fn expand(&self, word: &str, dir: &Path) -> String {
        word.replace("$(B)/$(D)/", &format!("{}/", self.out_dir(dir).display()))
            .replace("$$", "$")
            .replace('\'', "")
    }

    /// Builds the program as the `Makefile` does: the object with `$(CFLAGS) $($*.CFLAGS) -c`,
    /// then `$(CC) [-static] $(LDFLAGS) $($*.LDFLAGS) -o $@ $< $(LDLIBS) $($*.LDLIBS)`, after the
    /// objects and shared objects the test's `.mk` makes it depend on.
    fn build(&self, suite: &Suite, flags: &Flags, dir: &Path) -> Result<PathBuf, Failure> {
        let out = self.out_dir(dir);
        std::fs::create_dir_all(&out)
            .map_err(|e| Failure::harness(format!("creating {}: {e}", out.display())))?;
        let libtest = flags.libtest.as_ref().map_err(|e| {
            Failure::new(
                "build: src/common/libtest.a",
                format!("building libtest.a: {e}"),
            )
        })?;
        let (suffix, ldlibs) = match self.link {
            Link::Static => ("-static.exe", &self.mk.static_ldlibs),
            Link::Default | Link::Dynamic => (".exe", &self.mk.ldlibs),
        };
        let deps = ldlibs.iter().chain(match self.link {
            Link::Dynamic => &self.mk.run_deps,
            Link::Static | Link::Default => &[][..],
        });
        for dep in deps {
            self.build_dep(suite, flags, &self.expand(dep, dir), libtest)?;
        }

        let object = out.join(format!("{}.o", self.stem));
        let mut cmd = Command::new(&suite.cc);
        cmd.args(&flags.cflags)
            .args(&self.mk.cflags)
            .arg("-c")
            .arg("-o")
            .arg(&object)
            .arg(&self.source);
        build::target_wasixv2(&mut cmd).current_dir(dir);
        build::run_build(&mut cmd)?;

        let program = out.join(format!("{}{suffix}", self.stem));
        let mut cmd = Command::new(&suite.cc);
        if self.link == Link::Static {
            cmd.arg("-static");
        }
        cmd.args(&flags.ldflags);
        if self.link != Link::Static {
            cmd.args(self.mk.ldflags.iter().map(|w| self.expand(w, dir)));
        }
        cmd.arg("-o")
            .arg(&program)
            .arg(&object)
            .arg(libtest)
            .args(&flags.ldlibs)
            .args(ldlibs.iter().map(|w| self.expand(w, dir)));
        build::target_wasixv2(&mut cmd).current_dir(dir);
        build::run_build(&mut cmd)?;
        build::check_module(&program)?;
        Ok(program)
    }

    /// Builds `path`, an object (`$(B)/%.o`) or a shared object (`$(B)/%.so`, from `%.lo`, built
    /// with `-fPIC -DSHARED`) of another source in the test's directory.
    fn build_dep(
        &self,
        suite: &Suite,
        flags: &Flags,
        path: &str,
        libtest: &Path,
    ) -> Result<(), Failure> {
        let path = Path::new(path);
        let stem = path.file_stem().unwrap().to_string_lossy();
        let source = self.source.with_file_name(format!("{stem}.c"));
        let dir = path.parent().unwrap();
        let shared = path.extension().is_some_and(|e| e == "so");
        let object = if shared {
            path.with_extension("lo")
        } else {
            path.to_path_buf()
        };
        let mut cmd = Command::new(&suite.cc);
        cmd.args(&flags.cflags);
        if shared {
            cmd.args(["-fPIC", "-DSHARED"]);
        }
        cmd.arg("-c").arg("-o").arg(&object).arg(&source);
        build::target_wasixv2(&mut cmd).current_dir(dir);
        build::run_build(&mut cmd)?;
        if shared {
            let mut cmd = Command::new(&suite.cc);
            cmd.arg("-shared")
                .args(&flags.ldflags)
                .arg("-o")
                .arg(path)
                .arg(&object)
                .arg(libtest)
                .args(&flags.ldlibs);
            build::target_wasixv2(&mut cmd).current_dir(dir);
            build::run_build(&mut cmd)?;
        }
        Ok(())
    }

    /// Runs the program from the top of the build directory, as `make run` runs
    /// `src/<group>/<program>`, with a fresh `/tmp`, and judges it as `runtest` does.
    fn execute(&self, suite: &Suite, program: &Path, dir: &Path) -> Result<Outcome, Failure> {
        let tmp = tempfile::tempdir().map_err(|e| Failure::harness(e.to_string()))?;
        let mut cmd = Command::new(&suite.wasmer);
        cmd.arg("run")
            .arg("--volume")
            .arg(format!("{}:{GUEST_DIR}", dir.display()))
            .arg("--volume")
            .arg(format!("{}:/tmp", tmp.path().display()))
            .arg("--cwd")
            .arg(GUEST_DIR)
            .arg(program)
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

/// Judges a run as `src/common/runtest.c` does: exit status 0 passes, and a failure is named in
/// its words.
fn result(output: &run::Output) -> Result<Outcome, Failure> {
    let cause = match output.exit_status()? {
        0 => return Ok(Outcome::Passed),
        // wasmer exits with 128 + the number of the signal that killed the guest.
        status if status > 128 => format!("FAIL [signal {}]", status - 128),
        status => format!("FAIL [status {status}]"),
    };
    Err(Failure::new(cause, output.captured()))
}

impl Flags {
    fn new(suite: &Suite, top: &Path) -> Result<Self> {
        let config = top.join("config.mak.def");
        let text = std::fs::read_to_string(&config)
            .with_context(|| format!("reading {}", config.display()))?;
        let mut flags = Self {
            cflags: vec![format!("-I{}", top.join("src/common").display())],
            ldflags: Vec::new(),
            ldlibs: Vec::new(),
            libtest: Err(String::new()),
        };
        for line in text.lines().take_while(|l| l.trim() != GLIBC_SETTINGS) {
            let Some((var, value)) = line.split_once("+=") else {
                continue;
            };
            let target = match var.trim() {
                "CFLAGS" => &mut flags.cflags,
                "LDFLAGS" => &mut flags.ldflags,
                "LDLIBS" => &mut flags.ldlibs,
                other => anyhow::bail!("{}: unknown variable {other}", config.display()),
            };
            target.extend(value.split_whitespace().map(str::to_owned));
        }
        ensure!(
            flags.ldlibs.iter().any(|l| l == "-lm"),
            "no LDLIBS in {}",
            config.display()
        );
        let dir = suite.build_root.join(LibcTest.name()).join("common");
        flags.libtest = flags.build_libtest(suite, top, &dir).map_err(|f| f.details);
        Ok(flags)
    }

    /// `src/common/libtest.a` without `runtest.o`: the runner takes the place of `runtest`, whose
    /// `main` would also be linked in, ahead of libc's, for a test whose `main` takes no
    /// arguments, because wasm names the two forms of `main` differently.
    fn build_libtest(&self, suite: &Suite, top: &Path, dir: &Path) -> Result<PathBuf, Failure> {
        std::fs::create_dir_all(dir)
            .map_err(|e| Failure::harness(format!("creating {}: {e}", dir.display())))?;
        let common = top.join("src/common");
        let mut sources: Vec<PathBuf> = std::fs::read_dir(&common)
            .and_then(|entries| entries.map(|e| e.map(|e| e.path())).collect())
            .map_err(|e| Failure::harness(format!("reading {}: {e}", common.display())))?;
        sources.retain(|p| {
            p.extension().is_some_and(|e| e == "c") && p.file_stem().unwrap() != "runtest"
        });
        sources.sort();
        let mut objects = Vec::new();
        for source in sources {
            let object = dir.join(source.file_stem().unwrap()).with_extension("o");
            let mut cmd = Command::new(&suite.cc);
            cmd.args(&self.cflags)
                .arg("-c")
                .arg("-o")
                .arg(&object)
                .arg(&source);
            build::target_wasixv2(&mut cmd).current_dir(dir);
            build::run_build(&mut cmd)?;
            objects.push(object);
        }
        let archive = dir.join("libtest.a");
        let _ = std::fs::remove_file(&archive);
        let mut cmd = Command::new(AR);
        cmd.arg("rcs").arg(&archive).args(&objects);
        build::target_wasixv2(&mut cmd).current_dir(dir);
        build::run_build(&mut cmd)?;
        Ok(archive)
    }
}
