//! Browser export uses the native fixture parser and compiler, never native xfail baselines.

use std::collections::{BTreeMap, BTreeSet};
use std::path::{Component, Path, PathBuf};
use std::process::{Command, ExitCode};

use anyhow::{Context, Result, bail, ensure};
use serde_json::{Value, json};
use walkdir::WalkDir;

use crate::{Suite, build, fixture, root};
use fixture::{Config, Host};

pub(crate) fn try_main() -> Result<Option<ExitCode>> {
    let args: Vec<_> = std::env::args_os().skip(1).collect();
    let Some(mode) = args.first().and_then(|s| s.to_str()) else {
        return Ok(None);
    };
    if !matches!(
        mode,
        "--browser-accept-export"
            | "--browser-accept-run"
            | "--browser-export"
            | "--browser-run"
            | "--browser-v1-export"
            | "--browser-v1-run"
            | "--browser-net-export"
            | "--browser-net-run"
    ) {
        return Ok(None);
    }
    let v1 = mode.starts_with("--browser-v1-");
    let acceptance = mode.starts_with("--browser-accept-");
    let net = mode.starts_with("--browser-net-");
    let run = mode.ends_with("-run");
    ensure!(args.len() >= 2, "{mode} needs an export directory");
    ensure!(
        run || args.len() == 2,
        "usage: --browser-export DIR (selection is browser/allowlist.txt)"
    );
    let out = std::path::absolute(PathBuf::from(&args[1]))?;
    let suite = Suite::with_mode(true)?;
    let ok = export(&suite, &out, v1, acceptance, net)?;
    if !run {
        return Ok(Some(if ok {
            ExitCode::SUCCESS
        } else {
            ExitCode::FAILURE
        }));
    }
    let status = Command::new("node")
        .arg(root().join("browser/run.mjs"))
        .arg("--manifest")
        .arg(out.join("manifest.json"))
        .args(&args[2..])
        .status()
        .context("starting the browser runner; run npm ci in browser/ first")?;
    Ok(Some(if ok && status.success() {
        ExitCode::SUCCESS
    } else {
        ExitCode::FAILURE
    }))
}

fn select(configs: Vec<Config>, allowlist: &str) -> Result<Vec<Config>> {
    let mut by_name = BTreeMap::new();
    for config in configs {
        let name = config.test_name();
        ensure!(
            by_name.insert(name.clone(), config).is_none(),
            "duplicate fixture configuration: {name}"
        );
    }
    let mut seen = BTreeSet::new();
    let mut selected = Vec::new();
    for line in allowlist.lines() {
        let name = line.split('#').next().unwrap().trim();
        if name.is_empty() {
            continue;
        }
        ensure!(seen.insert(name), "duplicate browser test {name}");
        selected.push(
            by_name.remove(name).with_context(|| {
                format!("browser allowlist names a missing configuration: {name}")
            })?,
        );
    }
    ensure!(!selected.is_empty(), "empty browser allowlist");
    Ok(selected)
}

/// Only paths inside a fixture's build tree can be exported as browser assets.
fn relative(path: &Path) -> Result<()> {
    ensure!(
        !path.as_os_str().is_empty()
            && path
                .components()
                .all(|c| matches!(c, Component::Normal(_) | Component::CurDir)),
        "browser asset path must stay inside the fixture: {}",
        path.display()
    );
    Ok(())
}

fn validate(config: &Config) -> Result<()> {
    ensure!(config.ignored.is_none(), "Ignored: {:?}", config.ignored);
    ensure!(
        !config.unix_only && !config.ignore_macos,
        "host-only fixture metadata"
    );
    ensure!(
        config.skipped_engines.is_empty(),
        "SkipEngine needs browser review"
    );
    ensure!(
        config.browser_unsupported.is_empty(),
        "unsupported browser directives: {:?}",
        config.browser_unsupported
    );
    for (host, guest) in &config.mapped_directories {
        if let Host::Path(path) = host {
            relative(path)?;
        }
        ensure!(
            guest.starts_with('/') && !guest.split('/').any(|c| c == ".."),
            "mount needs an absolute guest path: {guest}"
        );
    }
    for (path, _) in config.prefilled_files.iter().chain(&config.expected_files) {
        relative(path)?;
    }
    if let Some(user) = &config.user {
        let (uid, gid) = user.split_once(':').context("User needs UID:GID")?;
        uid.parse::<u32>()?;
        gid.parse::<u32>()?;
    }
    Ok(())
}

fn path_string(path: &Path) -> Result<String> {
    Ok(path
        .to_str()
        .context("browser assets need UTF-8 names")?
        .replace('\\', "/"))
}

fn snapshot(dir: &Path) -> Result<Vec<Value>> {
    let mut entries = Vec::new();
    for entry in WalkDir::new(dir).follow_links(false).sort_by_file_name() {
        let entry = entry?;
        let path = path_string(entry.path().strip_prefix(dir)?)?;
        let metadata = entry.path().symlink_metadata()?;
        #[cfg(unix)]
        let mode = {
            use std::os::unix::fs::PermissionsExt;
            metadata.permissions().mode() & 0o7777
        };
        #[cfg(not(unix))]
        let mode = if metadata.is_dir() { 0o755 } else { 0o644 };
        let mut record = json!({ "path": path, "mode": mode });
        if metadata.is_dir() {
            record["kind"] = json!("directory");
        } else if metadata.is_symlink() {
            record["kind"] = json!("symlink");
            record["target"] = json!(path_string(&std::fs::read_link(entry.path())?)?);
        } else if metadata.is_file() {
            record["kind"] = json!("file");
            record["data"] = json!(std::fs::read(entry.path())?);
        } else {
            bail!("cannot snapshot special file {}", entry.path().display());
        }
        entries.push(record);
    }
    Ok(entries)
}

fn descriptor(config: &Config, timeout_ms: u128) -> Value {
    json!({
        "name": config.test_name(), "status": "ready", "module": "main.wasm",
        "args": config.args, "env": config.env, "stdin": config.stdin.clone().unwrap_or_default(),
        "cwd": config.current_directory, "user": config.user, "programName": config.program_name,
        "network":config.browser_network, "acceptance": config.browser_acceptance, "buildEnv": config.build_env, "console":config.browser_console, "http":config.browser_http, "commandDefaults":config.browser_command_defaults, "timeoutMs": timeout_ms,
        "expected": { "exitCode": config.expected_exit_code, "mustFail": config.must_fail,
            "stdout": config.expected_stdout, "stderr": config.expected_stderr, "files": [] },
        "mounts": []
    })
}

fn export_case(suite: &Suite, config: &Config, out: &Path) -> Result<Value> {
    validate(config)?;
    let build_dir = suite.build_root.join(config.test_name());
    let main = build::build(suite, config, &build_dir)
        .map_err(|e| anyhow::anyhow!("{}\n{}", e.cause, e.details))?;
    std::fs::copy(main, out.join("main.wasm"))?;
    std::fs::copy(build_dir.join("build.log"), out.join("build.log"))?;
    for (path, contents) in &config.prefilled_files {
        // Follow the native runner's setup order: prefilled files precede mount creation.
        std::fs::write(build_dir.join(path), contents)?;
    }
    let mut case = descriptor(config, suite.timeout.as_millis());
    let mut mounts = Vec::new();
    for (host, guest) in &config.mapped_directories {
        let entries = match host {
            Host::Temp => vec![json!({ "path": "", "kind": "directory", "mode": 0o700 })],
            Host::Path(path) => snapshot(&build_dir.join(path))?,
        };
        mounts.push(json!({ "guest": guest, "entries": entries }));
    }
    case["mounts"] = json!(mounts);
    let mut expected = Vec::new();
    for (path, contents) in &config.expected_files {
        let guest = config
            .mapped_directories
            .iter()
            .find_map(|(host, guest)| {
                let Host::Path(host) = host else { return None };
                path.strip_prefix(host).ok().map(|tail| (guest, tail))
            })
            .with_context(|| {
                format!(
                    "ExpectedFile {} is outside every browser mount",
                    path.display()
                )
            })?;
        expected.push(json!({ "path": format!("{}/{}", guest.0.trim_end_matches('/'), path_string(guest.1)?), "data": contents.as_bytes() }));
    }
    case["expected"]["files"] = json!(expected);
    Ok(case)
}

fn export(suite: &Suite, out: &Path, v1: bool, acceptance: bool, net: bool) -> Result<bool> {
    ensure!(!suite.timeout.is_zero(), "browser timeout must be positive");
    let mut candidates = fixture::discover(&root().join(if acceptance {
        "browser/accept"
    } else if net {
        "browser/net"
    } else if v1 {
        "browser/v1"
    } else {
        "suite"
    }))?;
    if acceptance {
        candidates.extend(fixture::discover(&root().join("suite"))?);
    }
    let configs = select(
        candidates,
        if acceptance {
            include_str!("../browser/accept-allowlist.txt")
        } else if v1 {
            include_str!("../browser/v1-allowlist.txt")
        } else if net {
            include_str!("../browser/net-allowlist.txt")
        } else {
            include_str!("../browser/allowlist.txt")
        },
    )?;
    // Create a new destination: a previous successful export must never survive a failed build.
    std::fs::create_dir_all(out.parent().context("export directory needs a parent")?)?;
    std::fs::create_dir(out).context("browser export directory must not already exist")?;
    let mut cases = Vec::new();
    let mut ok = true;
    for (index, config) in configs.iter().enumerate() {
        let asset_dir = format!("cases/{index}");
        let dir = out.join(&asset_dir);
        std::fs::create_dir_all(&dir)?;
        let mut case = match export_case(suite, config, &dir) {
            Ok(case) => case,
            Err(error) => {
                ok = false;
                let message = format!("{error:#}");
                std::fs::write(dir.join("build.log"), &message)?;
                let mut case = descriptor(config, suite.timeout.as_millis());
                case["status"] = json!("export-failed");
                case["error"] = json!(message);
                case
            }
        };
        case["assets"] = json!(asset_dir);
        cases.push(case);
    }
    let manifest = json!({ "version": 1, "lane": "sab-isolated-no-jspi", "tests": cases });
    std::fs::write(
        out.join("manifest.json"),
        serde_json::to_vec_pretty(&manifest)?,
    )?;
    println!(
        "exported {} browser cases to {} (success={ok})",
        configs.len(),
        out.display()
    );
    Ok(ok)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn configs(source: &str) -> Vec<Config> {
        let dir = tempfile::tempdir().unwrap();
        let fixture = dir.path().join("case");
        std::fs::create_dir(&fixture).unwrap();
        std::fs::write(fixture.join("main.c"), source).unwrap();
        fixture::discover(dir.path()).unwrap()
    }

    #[test]
    fn acceptance_metadata_survives_source_export() {
        let config = configs(r#"//#BrowserAcceptance: {"packageVersion":2,"command":"named","actions":[{"after":"ready","input":[4]}]}
"#).remove(0);
        let case = descriptor(&config, 1000);
        assert_eq!(case["acceptance"]["packageVersion"], 2);
        assert_eq!(case["acceptance"]["actions"][0]["input"], json!([4]));
    }

    #[test]
    fn exact_allowlist_rejects_stale_duplicate_and_empty_selection() {
        for text in ["case/missing", "case/default\ncase/default", "# empty"] {
            assert!(select(configs(""), text).is_err());
        }
        let config = configs("").remove(0);
        assert!(select(vec![config.clone(), config], "case/default").is_err());
        assert_eq!(
            select(configs(""), "case/default # reason").unwrap().len(),
            1
        );
    }

    #[test]
    fn ignored_or_native_compatibility_metadata_is_never_silently_exported() {
        for directive in [
            "Ignored: reason",
            "UnixOnly: true",
            "SkipEngine: js: reason",
            "FileSystems: memfs",
            "MappedDirectory: /etc:/etc",
            "PrefilledFile: ../escape:data",
        ] {
            let config = configs(&format!("//#{directive}\n"));
            assert!(validate(&config[0]).is_err(), "{directive}");
        }
        assert!(
            fixture::discover(Path::new("does-not-exist"))
                .unwrap()
                .is_empty()
        );
        assert!(
            configs("//#ExpectedStdout: ok")
                .iter()
                .all(|c| validate(c).is_ok())
        );
    }

    #[test]
    fn exports_inherited_expectations_build_flags_and_stdin_bytes() {
        let parsed = configs(
            "//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no\n//#AbstractConfig: parent\n//#Env: EQ=a=b\n//#ExpectedStdout: ok\n//#Stdin: input\n//#Config: child:parent\n//#ExpectedExitCode: 42\n",
        );
        let case = descriptor(&parsed[0], 60000);
        assert_eq!(case["expected"]["stdout"], json!(["ok"]));
        assert_eq!(case["expected"]["exitCode"], 42);
        assert_eq!(case["env"], json!([["EQ", "a=b"]]));
        assert_eq!(case["buildEnv"], json!([["WASIXCC_WASM_EXCEPTIONS", "no"]]));
        assert_eq!(case["stdin"], json!(b"input".to_vec()));
    }

    #[test]
    fn reviewed_allowlist_stays_resolvable_and_exportable() {
        let selected = select(
            fixture::discover(&root().join("suite")).unwrap(),
            include_str!("../browser/allowlist.txt"),
        )
        .unwrap();
        assert_eq!(selected.len(), 22);
        for config in selected {
            validate(&config).unwrap();
        }
    }

    #[test]
    fn failed_exports_keep_a_result_and_log_for_every_selected_case() {
        let temp = tempfile::tempdir().unwrap();
        let suite = Suite {
            wasmer: PathBuf::new(),
            wrapper: Vec::new(),
            cc: "false".into(),
            cxx: "false".into(),
            build_root: temp.path().join("build"),
            timeout: std::time::Duration::from_secs(1),
        };
        let out = temp.path().join("export");
        assert!(!export(&suite, &out, false, false, false).unwrap());
        let manifest: Value =
            serde_json::from_slice(&std::fs::read(out.join("manifest.json")).unwrap()).unwrap();
        let cases = manifest["tests"].as_array().unwrap();
        assert_eq!(cases.len(), 22);
        for case in cases {
            assert_eq!(case["status"], "export-failed");
            assert!(
                out.join(case["assets"].as_str().unwrap())
                    .join("build.log")
                    .is_file()
            );
        }
        assert!(
            export(&suite, &out, false, false, false).is_err(),
            "must not reuse a stale export"
        );
    }

    #[test]
    fn v1_console_http_metadata_and_allowlist_are_explicit() {
        let parsed = configs(
            r#"//#BrowserConsole: true
//#BrowserHttp: true
//#BrowserCommandDefaults: [{"name":"show","argv0":"shown","args":["--main"],"env":["B=default"]}]
"#,
        );
        let case = descriptor(&parsed[0], 1000);
        assert_eq!(case["console"], true);
        assert_eq!(case["http"], true);
        assert_eq!(case["commandDefaults"][0]["name"], "show");
        assert!(
            crate::fixture::discover(Path::new("does-not-exist"))
                .unwrap()
                .is_empty()
        );
        let selected = select(
            fixture::discover(&root().join("browser/v1")).unwrap(),
            include_str!("../browser/v1-allowlist.txt"),
        )
        .unwrap();
        assert_eq!(selected.len(), 14);
        for config in selected {
            validate(&config).unwrap();
        }
    }

    #[test]
    fn mount_snapshot_preserves_bytes_directories_and_symlinks() {
        let dir = tempfile::tempdir().unwrap();
        std::fs::create_dir(dir.path().join("nested")).unwrap();
        std::fs::write(dir.path().join("nested/file"), [0, 255, 10]).unwrap();
        #[cfg(unix)]
        std::os::unix::fs::symlink("nested/file", dir.path().join("link")).unwrap();
        let entries = snapshot(dir.path()).unwrap();
        assert!(
            entries
                .iter()
                .any(|e| e["path"] == "nested/file" && e["data"] == json!([0, 255, 10]))
        );
        #[cfg(unix)]
        assert!(
            entries
                .iter()
                .any(|e| e["kind"] == "symlink" && e["target"] == "nested/file")
        );
    }
    #[test]
    fn network_metadata_and_required_cases_are_explicit() {
        let config = configs("//#BrowserNetwork: fidelity");
        assert_eq!(descriptor(&config[0], 1000)["network"], "fidelity");
        for profile in ["switch4", "switch6", "ephemeral"] {
            let configs = configs(&format!("//#BrowserNetwork: {profile}"));
            assert_eq!(descriptor(&configs[0], 1000)["network"], profile);
        }
        assert!(std::panic::catch_unwind(|| configs("//#BrowserNetwork: unsupported")).is_err());
        let cases = select(
            fixture::discover(&root().join("browser/net")).unwrap(),
            include_str!("../browser/net-allowlist.txt"),
        )
        .unwrap();
        assert_eq!(cases.len(), 28);
        for config in cases {
            assert!(config.browser_network.is_some());
            validate(&config).unwrap();
        }
    }
}
