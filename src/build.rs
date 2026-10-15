//! Building a fixture configuration into a wasixv2 module with wasixcc.

use std::path::{Path, PathBuf};
use std::process::Command;

use anyhow::{Context, Result};
use walkdir::WalkDir;

use crate::{Failure, Suite, fixture::Config, fixture::Source};

/// Builds `config` in a fresh copy of its fixture directory and returns the path of `main`.
pub fn build(suite: &Suite, config: &Config, dir: &Path) -> Result<PathBuf, Failure> {
    let mut cmd = match &config.source {
        Source::Rust(_) | Source::Cargo => {
            return Err(Failure::new(
                "build: no Rust toolchain for wasm32-wasixv2",
                "Rust fixtures cannot be built for wasixv2 yet.",
            ));
        }
        Source::Shell(script) => {
            let mut cmd = Command::new("bash");
            cmd.arg(dir.join(script))
                .env("CC", &suite.cc)
                .env("CXX", &suite.cxx);
            cmd
        }
        Source::C(file) => {
            let mut cmd = Command::new(&suite.cc);
            cmd.arg(file).arg("-o").arg("main");
            cmd
        }
        Source::Cpp(file) => {
            let mut cmd = Command::new(&suite.cxx);
            cmd.arg(file).arg("-o").arg("main");
            cmd
        }
    };

    copy_tree(&config.src_dir, dir).map_err(|e| Failure::harness(format!("{e:#}")))?;
    cmd.current_dir(dir)
        .env("WASIXCC_TARGET", "wasm32-wasixv2")
        .env("WASIXCC_DISCARD_UNSUPPORTED_FLAGS", "yes")
        // Build scripts may tune compile-time parameters per backend and architecture.
        .env("WASMER_BACKEND", "cranelift")
        .env("WASMER_ARCH", std::env::consts::ARCH);
    for (key, value) in &config.build_env {
        cmd.env(key, value);
    }
    let output = cmd
        .output()
        .map_err(|e| Failure::harness(format!("cannot run {cmd:?}: {e}")))?;
    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr);
        let stdout = String::from_utf8_lossy(&output.stdout);
        return Err(Failure::new(
            format!("build: {}", first_error_line(&stderr, &stdout)),
            format!(
                "build failed ({})\n--- stdout\n{stdout}\n--- stderr\n{stderr}",
                output.status
            ),
        ));
    }

    let main = dir.join("main");
    let wasm = std::fs::read(&main).map_err(|e| {
        Failure::new(
            "build: no `main` produced",
            format!("the build succeeded but {} is missing: {e}", main.display()),
        )
    })?;
    if !imports_wasixv2(&wasm) {
        return Err(Failure::new(
            "build: not a wasixv2 module",
            "`main` imports nothing from `wasixv2`; does this wasixcc support \
             WASIXCC_TARGET=wasm32-wasixv2?",
        ));
    }
    Ok(main)
}

/// The line that best names a build failure, for grouping failures by cause: the first line that
/// mentions an error, without its source location or any machine-specific path.
fn first_error_line(stderr: &str, stdout: &str) -> String {
    let lines = || stderr.lines().chain(stdout.lines()).map(str::trim);
    let line = lines()
        .find(|l| l.to_lowercase().contains("error"))
        .or_else(|| lines().find(|l| !l.is_empty()))
        .unwrap_or("failed without output");
    let line = strip_location(line);
    let mut line = line.to_owned();
    for var in ["WASIXCC_SYSROOT", "WASIXCC_LLVM_LOCATION"] {
        if let Some(path) = std::env::var_os(var).filter(|p| !p.is_empty()) {
            line = line.replace(&*path.to_string_lossy(), &format!("${var}"));
        }
    }
    line.chars().take(160).collect()
}

/// Drops a leading `file:line:col: ` or `file:line: `, as compilers print it.
fn strip_location(line: &str) -> &str {
    let mut rest = line;
    let Some((_file, after)) = rest.split_once(':') else {
        return line;
    };
    rest = after;
    let mut numbers = 0;
    while let Some((num, after)) = rest.split_once(':') {
        if num.is_empty() || !num.bytes().all(|b| b.is_ascii_digit()) {
            break;
        }
        numbers += 1;
        rest = after;
    }
    if numbers == 0 {
        line
    } else {
        rest.trim_start()
    }
}

fn imports_wasixv2(wasm: &[u8]) -> bool {
    for payload in wasmparser::Parser::new(0).parse_all(wasm) {
        let Ok(wasmparser::Payload::ImportSection(reader)) = payload else {
            continue;
        };
        for import in reader.into_imports().flatten() {
            if import.module == "wasixv2" {
                return true;
            }
        }
    }
    false
}

fn copy_tree(from: &Path, to: &Path) -> Result<()> {
    if to.exists() {
        std::fs::remove_dir_all(to).with_context(|| format!("removing {}", to.display()))?;
    }
    for entry in WalkDir::new(from) {
        let entry = entry?;
        let target = to.join(entry.path().strip_prefix(from)?);
        let file_type = entry.file_type();
        if file_type.is_dir() {
            std::fs::create_dir_all(&target)?;
        } else if file_type.is_symlink() {
            let link = std::fs::read_link(entry.path())?;
            #[cfg(unix)]
            std::os::unix::fs::symlink(link, &target)?;
            #[cfg(not(unix))]
            anyhow::bail!("symlink {} needs a Unix host", link.display());
        } else {
            std::fs::copy(entry.path(), &target)
                .with_context(|| format!("copying {}", entry.path().display()))?;
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::strip_location;

    #[test]
    fn locations_are_stripped() {
        assert_eq!(
            strip_location("main.c:8:10: fatal error: 'wasix/context.h' file not found"),
            "fatal error: 'wasix/context.h' file not found"
        );
        assert_eq!(strip_location("a.c:3: error: x"), "error: x");
        assert_eq!(
            strip_location("Error: PIC is not supported"),
            "Error: PIC is not supported"
        );
    }
}
