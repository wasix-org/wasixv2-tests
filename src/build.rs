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
    std::fs::write(
        dir.join("build.log"),
        format!(
            "command: {cmd:?}\nstatus: {}\n--- stdout\n{}\n--- stderr\n{}",
            output.status,
            String::from_utf8_lossy(&output.stdout),
            String::from_utf8_lossy(&output.stderr)
        ),
    )
    .map_err(|e| Failure::harness(format!("writing build.log: {e}")))?;
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
    let v1 = config
        .build_env
        .iter()
        .rev()
        .find(|(k, _)| k == "WASIXCC_TARGET")
        .is_some_and(|(_, v)| v == "wasm32-wasix");
    if !imports_target(&wasm, v1) {
        return Err(Failure::new(
            "build: module does not import the declared WASIX target",
            if v1 {
                "`main` has no recognized WASIX v1 imports"
            } else {
                "`main` imports nothing from `wasixv2`; does this wasixcc support WASIXCC_TARGET=wasm32-wasixv2?"
            },
        ));
    }
    Ok(main)
}

/// The line that best names a build failure, for grouping failures by cause: the first line that
/// mentions an error, without its source location or any machine-specific path.
pub(crate) fn first_error_line(stderr: &str, stdout: &str) -> String {
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
pub(crate) fn strip_location(line: &str) -> &str {
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

fn imports_target(wasm: &[u8], v1: bool) -> bool {
    for payload in wasmparser::Parser::new(0).parse_all(wasm) {
        let Ok(wasmparser::Payload::ImportSection(reader)) = payload else {
            continue;
        };
        for import in reader.into_imports().flatten() {
            if if v1 {
                matches!(
                    import.module,
                    "wasi_snapshot_preview1" | "wasix_32v1" | "wasix_http_client_v1"
                )
            } else {
                import.module == "wasixv2"
            } {
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
    fn failed_build_keeps_the_compiler_log() {
        let source = tempfile::tempdir().unwrap();
        let fixture = source.path().join("case");
        std::fs::create_dir(&fixture).unwrap();
        std::fs::write(fixture.join("main.c"), "int main(void) { return 0; }").unwrap();
        let config = crate::fixture::discover(source.path()).unwrap().remove(0);
        let suite = crate::Suite {
            wasmer: Default::default(),
            wrapper: Vec::new(),
            cc: "false".into(),
            cxx: "false".into(),
            build_root: Default::default(),
            timeout: std::time::Duration::from_secs(1),
        };
        let out = tempfile::tempdir().unwrap();
        let build = out.path().join("build");
        assert!(super::build(&suite, &config, &build).is_err());
        let log = std::fs::read_to_string(build.join("build.log")).unwrap();
        assert!(log.contains("status:") && log.contains("--- stderr"));
    }

    #[test]
    fn v1_namespace_requires_an_explicit_legacy_target() {
        for name in [
            "wasi_snapshot_preview1",
            "wasix_32v1",
            "wasix_http_client_v1",
            "wasixv2",
            "unknown",
        ] {
            // Source-built minimal import section; no guest artifact is checked in.
            let mut wasm = b"\0asm\x01\0\0\0".to_vec();
            wasm.extend_from_slice(&[1, 4, 1, 0x60, 0, 0]); // one () -> () type
            let mut section = vec![1, name.len() as u8];
            section.extend_from_slice(name.as_bytes());
            section.extend_from_slice(&[1, b'f', 0, 0]);
            wasm.extend_from_slice(&[2, section.len() as u8]);
            wasm.extend_from_slice(&section);
            assert_eq!(super::imports_target(&wasm, false), name == "wasixv2");
            assert_eq!(
                super::imports_target(&wasm, true),
                !matches!(name, "wasixv2" | "unknown")
            );
        }
    }

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
