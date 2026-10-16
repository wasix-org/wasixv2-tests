//! musl's libc-test (`upstream/libc-test`): functional, regression and math tests, each a program
//! that exits 0 and prints nothing on success, and api tests that only have to compile.

use std::path::PathBuf;
use std::process::Command;
use std::sync::OnceLock;

use anyhow::{Result, ensure};

use super::{Case, ExcludeList, Excluded, Invocation, Outcome, run_build, run_wasm, wasixcc_env};
use crate::{Failure, Suite};

/// The flags of libc-test's `config.mak.def`, without `-frounding-math`: wasm has only
/// round-to-nearest, and clang rejects the flag for it.
const CFLAGS: &[&str] = &[
    "-std=c99",
    "-D_POSIX_C_SOURCE=200809L",
    "-Wno-unused-function",
    "-Wno-missing-braces",
    "-Wno-unused",
    "-Wno-overflow",
    "-Wno-unknown-pragmas",
    "-fno-builtin",
    "-Werror=implicit-function-declaration",
    "-Werror=implicit-int",
    "-Werror=pointer-sign",
    "-Werror=pointer-arith",
    "-D_FILE_OFFSET_BITS=64",
];
/// The api tests' flags, plus `-Wno-strict-prototypes`: their sources declare `f()`, which newer
/// clang rejects under `-Werror`.
const API_CFLAGS: &[&str] = &[
    "-pedantic-errors",
    "-Werror",
    "-Wno-unused",
    "-Wno-strict-prototypes",
    "-D_XOPEN_SOURCE=700",
];
const LDLIBS: &[&str] = &[
    "-lpthread",
    "-lm",
    "-lrt",
    "-lcrypt",
    "-ldl",
    "-lresolv",
    "-lutil",
];
const DIRS: &[&str] = &["functional", "regression", "math", "api", "musl"];

pub struct Test {
    name: String,
    /// `functional`, `regression`, ...
    dir: String,
    /// Path under `src/`.
    file: String,
}

/// libc-test's sources and its built helper library, shared by every test.
struct Prepared {
    src: PathBuf,
    /// `common/*.o` and the generated `options.h`.
    common: PathBuf,
    objects: Vec<PathBuf>,
}

static PREPARED: OnceLock<Result<Prepared, String>> = OnceLock::new();

pub fn discover(excludes: &ExcludeList, excluded: &mut Excluded) -> Result<Vec<(String, Case)>> {
    let src = super::upstream_dir().join("libc-test/src");
    ensure!(
        src.is_dir(),
        "upstream/libc-test is not checked out: run `git submodule update --init --recursive`"
    );
    let mut tests = Vec::new();
    for dir in DIRS {
        let mut files: Vec<_> = std::fs::read_dir(src.join(dir))?
            .filter_map(Result::ok)
            .map(|e| e.file_name().to_string_lossy().into_owned())
            .filter(|f| f.ends_with(".c") && !f.ends_with("_dso.c"))
            .collect();
        files.sort();
        for file in files {
            let name = format!("libc-test/{dir}/{}", file.trim_end_matches(".c"));
            if let Some(reason) = excludes.reason(&name) {
                excluded.add(reason);
                continue;
            }
            let test = Test {
                name: name.clone(),
                dir: dir.to_string(),
                file: format!("{dir}/{file}"),
            };
            tests.push((name, Case::LibcTest(test)));
        }
    }
    Ok(tests)
}

fn prepare(suite: &Suite) -> Result<Prepared> {
    let src = super::source_copy(suite, "libc-test")?;
    let common = suite.build_root.join("libc-test-common");
    std::fs::create_dir_all(&common)?;
    // `options.h` as libc-test's Makefile generates it.
    let pre = Command::new(&suite.cc)
        .arg("-E")
        .arg("-")
        .stdin(std::fs::File::open(src.join("src/common/options.h.in"))?)
        .env("WASIXCC_TARGET", "wasm32-wasixv2")
        .output()?;
    ensure!(pre.status.success(), "preprocessing options.h.in failed");
    let mut options = String::new();
    let (mut started, mut pending) = (false, None::<String>);
    for line in String::from_utf8_lossy(&pre.stdout).lines() {
        let fields: Vec<&str> = line.split_whitespace().collect();
        if line.contains("optiongroups_unistd_end") {
            started = true;
            continue;
        }
        if !started || fields.is_empty() || line.starts_with('#') {
            continue;
        }
        match pending.take() {
            None if fields.len() == 1 => pending = Some(fields[0].to_owned()),
            None => options.push_str(&format!(
                "#define {} {}\n",
                fields[0],
                fields[fields.len() - 1]
            )),
            Some(a) => options.push_str(&format!("#define {a} {}\n", fields[fields.len() - 1])),
        }
    }
    std::fs::write(common.join("options.h"), options)?;

    let mut objects = Vec::new();
    for entry in std::fs::read_dir(src.join("src/common"))? {
        let path = entry?.path();
        let stem = path.file_stem().unwrap().to_string_lossy().into_owned();
        // runtest.c is libc-test's own runner, not part of the test library.
        if path.extension().is_none_or(|e| e != "c") || stem == "runtest" {
            continue;
        }
        let object = common.join(format!("{stem}.o"));
        let mut cmd = Command::new(&suite.cc);
        wasixcc_env(&mut cmd)
            .args(CFLAGS)
            .arg("-I")
            .arg(&common)
            .arg("-I")
            .arg(src.join("src/common"))
            .arg("-c")
            .arg(&path)
            .arg("-o")
            .arg(&object);
        run_build(&mut cmd).map_err(|f| anyhow::anyhow!("{}\n{}", f.cause, f.details))?;
        objects.push(object);
    }
    Ok(Prepared {
        src,
        common,
        objects,
    })
}

impl Test {
    pub fn run(&self, suite: &Suite) -> Result<Outcome, Failure> {
        let prepared = PREPARED
            .get_or_init(|| prepare(suite).map_err(|e| format!("{e:#}")))
            .as_ref()
            .map_err(|e| Failure::harness(format!("preparing libc-test: {e}")))?;
        let dir = suite.build_root.join(&self.name);
        std::fs::create_dir_all(&dir).map_err(|e| Failure::harness(e.to_string()))?;
        let main = dir.join("main");
        let mut cmd = Command::new(&suite.cc);
        wasixcc_env(&mut cmd)
            .args(CFLAGS)
            .arg("-I")
            .arg(&prepared.common)
            .arg("-I")
            .arg(prepared.src.join("src/common"))
            .arg(prepared.src.join("src").join(&self.file));
        if self.dir == "api" {
            cmd.args(API_CFLAGS)
                .arg("-c")
                .arg("-o")
                .arg(dir.join("main.o"));
            run_build(&mut cmd)?;
            return Ok(Outcome::Passed);
        }
        cmd.args(&prepared.objects)
            .arg("-o")
            .arg(&main)
            .args(LDLIBS);
        run_build(&mut cmd)?;
        let ran = run_wasm(
            suite,
            &Invocation {
                module: main,
                ..Default::default()
            },
            &dir,
        )?;
        if ran.output.status == Some(0) {
            return Ok(Outcome::Passed);
        }
        // libc-test prints `file:line: message` for each failed check.
        let cause = super::first_line(&ran.output.stdout, |l| !l.trim().is_empty())
            .map(|l| super::cause_from_line("", l))
            .unwrap_or_else(|| exit_cause(ran.output.status));
        Err(ran.fail(suite, cause))
    }
}

pub fn exit_cause(status: Option<i32>) -> String {
    match status {
        Some(s) => format!("exit status {s}"),
        None => "timeout".to_owned(),
    }
}
