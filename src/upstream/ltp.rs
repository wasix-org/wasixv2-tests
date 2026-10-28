//! The Linux Test Project (`upstream/ltp`, wasix-org's fork with the wasixv2 patches): the tests
//! of LTP's `runtest/` lists, named `ltp/<list>/<tag>` and run with the list's command line.
//!
//! LTP is built once per commit: its own configure and makefiles with wasixcc, against Linux's
//! UAPI headers, which only LTP's build sees. Tests whose metadata (or `excluded.txt`) shows they
//! cannot make sense on wasixv2 are excluded. Tests that need root run as uid 0.

use std::collections::{BTreeMap, HashMap};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::OnceLock;

use anyhow::{Context, Result, bail, ensure};
use walkdir::WalkDir;

use super::{
    BuildConfig, Case, ExcludeList, Excluded, Invocation, Outcome, run_wasm, wasixcc_env_for,
};
use crate::{Failure, Suite};

/// The `runtest/` lists that test the system interface wasixv2 provides.
const LISTS: &[&str] = &[
    "syscalls",
    "syscalls-ipc",
    "mm",
    "fs",
    "fs_perms_simple",
    "fs_bind",
    "pty",
    "sched",
    "math",
    "nptl",
    "fcntl-locktests",
    "containers",
    "cve",
    "crypto",
    "watchqueue",
    "dio",
    "dma_thread_diotest",
    "ltp-aiodio.part1",
    "ltp-aiodio.part2",
    "ltp-aiodio.part3",
    "ltp-aiodio.part4",
    "ltp-aio-stress",
    "capability",
    "net.features",
    "net.ipv6",
    "net.ipv6_lib",
    "net.multicast",
    "net.sctp",
    "net.tcp_cmds",
];

/// The other lists, which test hardware, kernel internals or things wasixv2 has no counterpart
/// for, and why. A `*` suffix matches a prefix.
const EXCLUDED_LISTS: &[(&str, &str)] = &[
    ("can", "CAN bus devices"),
    (
        "commands",
        "host userspace commands, not the system interface",
    ),
    ("controllers", "cgroups"),
    ("cpuhotplug", "CPU hotplug"),
    ("crashme", "executes random machine code"),
    ("fs_readonly", "block devices"),
    ("hugetlb", "huge pages"),
    ("hyperthreading", "CPU topology"),
    ("ima", "kernel integrity measurement"),
    ("input", "input devices"),
    ("kernel_misc", "kernel internals and modules"),
    ("kvm", "KVM"),
    ("net.nfs", "NFS over a second host"),
    ("net_stress.*", "network stress between two hosts"),
    ("net.rpc_tests", "SunRPC (no libtirpc)"),
    ("net.tirpc_tests", "SunRPC (no libtirpc)"),
    ("numa", "NUMA"),
    ("power_management_tests", "power management"),
    ("scsi_debug.part1", "SCSI devices"),
    ("smack", "the Smack LSM"),
    ("smoketest", "duplicates the other lists"),
    ("staging", "unmerged kernel features"),
    ("sysfs", "sysfs device objects"),
    ("thermal", "thermal devices"),
    ("tpm_tools", "TPM devices"),
    ("tracing", "kernel tracing"),
    ("uevent", "device events"),
];

/// Linux release whose UAPI headers LTP builds against, the newest release covering wasixv2's
/// syscall table but for `fchroot`.
const LINUX_UAPI_TAG: &str = "v7.2";

pub struct Test {
    name: String,
    command: String,
    args: Vec<String>,
    /// The test needs root (`needs_root`, or `tst_require_root()` in the old API): it runs as
    /// uid 0, which holds every capability.
    root: bool,
}

struct Prepared {
    /// `opt/ltp` of the installed build.
    ltproot: PathBuf,
    /// Leaf directory of each source file stem, and its build log.
    logs: HashMap<String, PathBuf>,
}

static PREPARED: OnceLock<Result<Prepared, String>> = OnceLock::new();

fn src() -> PathBuf {
    super::upstream_dir().join("ltp")
}

pub fn discover(
    suite_build_root: &Path,
    excludes: &ExcludeList,
    excluded: &mut Excluded,
) -> Result<Vec<(String, Case)>> {
    ensure!(
        src().join("runtest").is_dir(),
        "upstream/ltp is not checked out: run `git submodule update --init --recursive`"
    );
    let metadata = metadata(suite_build_root)?;
    let sources = source_index(&src());
    let mut tests = Vec::new();
    let mut all_lists: Vec<String> = std::fs::read_dir(src().join("runtest"))?
        .filter_map(Result::ok)
        .map(|e| e.file_name().to_string_lossy().into_owned())
        .filter(|n| n != "Makefile")
        .collect();
    all_lists.sort();
    for list in all_lists {
        let entries = runtest_entries(&src().join("runtest").join(&list))?;
        let excluded_list = EXCLUDED_LISTS
            .iter()
            .find(|(l, _)| match l.strip_suffix('*') {
                Some(prefix) => list.starts_with(prefix),
                None => *l == list,
            });
        if let Some((_, reason)) = excluded_list {
            for _ in &entries {
                excluded.add(format!("LTP list `{list}`: {reason}"));
            }
            continue;
        }
        if !LISTS.contains(&list.as_str()) {
            bail!("runtest/{list} is neither in LISTS nor EXCLUDED_LISTS");
        }
        for (tag, command, args) in entries {
            let name = format!("ltp/{list}/{tag}");
            let reason = excludes
                .reason(&name)
                .map(str::to_owned)
                .or_else(|| metadata_reason(&metadata, &command));
            if let Some(reason) = reason {
                excluded.add(reason);
                continue;
            }
            let test = Test {
                name: name.clone(),
                root: needs_root(&metadata, &sources, &command),
                command,
                args,
            };
            tests.push((name, Case::Ltp(test)));
        }
    }
    Ok(tests)
}

/// `tag command args...` lines of a runtest list.
fn runtest_entries(path: &Path) -> Result<Vec<(String, String, Vec<String>)>> {
    let text =
        std::fs::read_to_string(path).with_context(|| format!("reading {}", path.display()))?;
    let mut entries = Vec::new();
    for line in text.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let mut words = line.split_whitespace();
        let (Some(tag), Some(command)) = (words.next(), words.next()) else {
            continue;
        };
        entries.push((
            tag.to_owned(),
            command.to_owned(),
            words.map(str::to_owned).collect(),
        ));
    }
    Ok(entries)
}

/// The metadata key of a runtest command: its binary, without the `_64` of the
/// `_FILE_OFFSET_BITS=64` variants.
fn metadata_key(command: &str) -> &str {
    command.strip_suffix("_64").unwrap_or(command)
}

/// Why a test's declared requirements cannot make sense on wasixv2.
fn metadata_reason(metadata: &serde_json::Value, command: &str) -> Option<String> {
    let test = metadata.get("tests")?.get(metadata_key(command))?;
    let has = |key: &str| {
        test.get(key)
            .is_some_and(|v| !v.is_null() && v != &serde_json::Value::from(0))
    };
    let reason = if has("needs_device")
        || has("format_device")
        || has("mount_device")
        || has("all_filesystems")
        || has("dev_min_size")
        || has("needs_rofs")
        || has("needs_overlay")
        || has("needs_devfs")
        || has("filesystems")
    {
        "needs block devices or real filesystems; the module owns its whole filesystem"
    } else if has("taint_check") {
        "checks the kernel's taint flags"
    } else if has("needs_hugetlbfs") || has("hugepages") {
        "needs huge pages, which wasm memory does not have"
    } else if has("needs_cgroup_ctrls") || has("needs_cgroup_ver") || has("needs_cgroup_nsdelegate")
    {
        "needs cgroups"
    } else if has("needs_kconfigs") {
        "reads the kernel's build configuration"
    } else if has("min_swap_avail") {
        "needs swap"
    } else if has("needs_cpu_vendor") {
        "needs a particular CPU"
    } else if test
        .get("supported_archs")
        .and_then(|a| a.as_array())
        .is_some_and(|a| !a.iter().any(|v| v.as_str() == Some("wasm32")))
    {
        "architecture-specific"
    } else if test
        .get("needs_abi_bits")
        .is_some_and(|b| b.as_u64() == Some(64) || b.as_str() == Some("64"))
    {
        "needs a 64-bit ABI; wasixv2 is wasm32"
    } else {
        return None;
    };
    Some(format!("LTP: {reason}"))
}

/// Whether a test needs root: `needs_root` in its metadata, or, for old-API tests, which have no
/// metadata, `tst_require_root()` in its source.
fn needs_root(
    metadata: &serde_json::Value,
    sources: &HashMap<String, PathBuf>,
    command: &str,
) -> bool {
    let key = metadata_key(command);
    let declared = metadata
        .get("tests")
        .and_then(|t| t.get(key))
        .and_then(|t| t.get("needs_root"))
        .is_some_and(|v| !v.is_null() && v != &serde_json::Value::from(0));
    declared
        || sources
            .get(key)
            .and_then(|path| std::fs::read_to_string(path).ok())
            .is_some_and(|text| {
                text.contains("tst_require_root()") && !text.contains("struct tst_test")
            })
}

/// Every `.c` under `testcases/` by stem, for finding a test's source and build log.
fn source_index(root: &Path) -> HashMap<String, PathBuf> {
    WalkDir::new(root.join("testcases"))
        .into_iter()
        .filter_map(Result::ok)
        .filter(|e| e.file_type().is_file() && e.path().extension().is_some_and(|x| x == "c"))
        .map(|e| {
            let stem = e.path().file_stem().unwrap().to_string_lossy().into_owned();
            (stem, e.into_path())
        })
        .collect()
}

/// LTP's `metadata/ltp.json`, generated with its host-side `metaparse` and cached per commit.
fn metadata(build_root: &Path) -> Result<serde_json::Value> {
    let commit = super::git(&src(), &["rev-parse", "HEAD"])?;
    let dir = build_root.join("ltp-metadata").join(&commit[..12]);
    let json = dir.join("ltp.json");
    if !json.exists() {
        let tools = dir.join("metadata");
        std::fs::create_dir_all(&tools)?;
        for tool in ["metaparse", "metaparse-sh"] {
            let status = Command::new(std::env::var("HOSTCC").unwrap_or_else(|_| "cc".to_owned()))
                .arg("-O2")
                .arg("-I")
                .arg(src().join("include"))
                .arg(src().join(format!("metadata/{tool}.c")))
                .arg("-o")
                .arg(tools.join(tool))
                .status()
                .context("building LTP's metaparse needs a host C compiler (`cc` or HOSTCC)")?;
            ensure!(status.success(), "building LTP's {tool} failed");
        }
        let out = Command::new("sh")
            .arg(src().join("metadata/parse.sh"))
            .current_dir(&tools)
            .output()?;
        ensure!(
            out.status.success(),
            "LTP's metadata/parse.sh failed: {}",
            String::from_utf8_lossy(&out.stderr)
        );
        std::fs::write(&json, &out.stdout)?;
    }
    let text = std::fs::read_to_string(&json)?;
    serde_json::from_str(&text).context("parsing LTP's ltp.json")
}

/// Linux's UAPI headers for LTP's build only, with `asm/unistd.h` replaced by libc2's syscall
/// numbers, which are the wasixv2 ABI's.
fn uapi_headers(build_root: &Path) -> Result<PathBuf> {
    let out = build_root.join(format!("linux-uapi-{LINUX_UAPI_TAG}"));
    let ready = out.join(".ready");
    if ready.exists() {
        return Ok(out.join("include"));
    }
    let linux = build_root.join(format!("linux-{LINUX_UAPI_TAG}"));
    if !linux.join("Makefile").exists() {
        if linux.exists() {
            std::fs::remove_dir_all(&linux)?;
        }
        let clone = Command::new("git")
            .args(["clone", "-q", "--depth", "1", "--branch", LINUX_UAPI_TAG])
            .args([
                "--filter=blob:none",
                "--sparse",
                "https://github.com/torvalds/linux.git",
            ])
            .arg(&linux)
            .status()?;
        ensure!(clone.success(), "cloning Linux {LINUX_UAPI_TAG} failed");
        let sparse = Command::new("git")
            .arg("-C")
            .arg(&linux)
            .args([
                "sparse-checkout",
                "set",
                "include",
                "arch/riscv",
                "scripts",
                "usr",
                "tools/include",
            ])
            .status()?;
        ensure!(sparse.success(), "sparse-checkout of Linux failed");
    }
    // riscv's UAPI: wasixv2's struct layouts and constants follow riscv32.
    let install = Command::new("make")
        .arg("-s")
        .arg("-C")
        .arg(&linux)
        .args(["ARCH=riscv", "headers_install"])
        .arg(format!("INSTALL_HDR_PATH={}", out.display()))
        .status()?;
    ensure!(install.success(), "Linux headers_install failed");
    std::fs::write(
        out.join("include/asm/unistd.h"),
        "/* wasixv2: syscall numbers are libc2's, which are the wasixv2 ABI's. riscv32's differ\n \
         * for lseek and mmap. Only LTP's build sees this directory. */\n#include <sys/syscall.h>\n",
    )?;
    std::fs::write(&ready, LINUX_UAPI_TAG)?;
    Ok(out.join("include"))
}

fn prepare(suite: &Suite) -> Result<Prepared> {
    let uapi = uapi_headers(&suite.build_root)?;
    let tree = super::source_copy(suite, "ltp")?;
    let ltproot = tree.join("install/opt/ltp");
    let logs_dir = tree.join("wasixv2-build-logs");
    let mut logs: HashMap<String, PathBuf> = source_index(&tree)
        .iter()
        .map(|(stem, path)| {
            let leaf = path.parent().unwrap().strip_prefix(&tree).unwrap();
            (
                stem.clone(),
                leaf_log(&logs_dir, leaf, BuildConfig::ExceptionHandling),
            )
        })
        .collect();
    // The exception-handling build of everything, then the asyncify build of the programs that
    // fork, which replaces theirs in the install.
    let built = tree.join(".wasixv2-ltp-built");
    let forking_list = tree.join("wasixv2-forking.txt");
    let ready = tree.join(".wasixv2-ltp-ready");
    if !ready.exists() {
        if !built.exists() {
            build_tree(
                &tree,
                BuildConfig::ExceptionHandling,
                &uapi,
                None,
                &logs_dir,
            )?;
            std::fs::write(&built, "")?;
        }
        if !forking_list.exists() {
            let forking = forking_programs(&tree, &ltproot)?;
            let text: String = forking
                .iter()
                .map(|(program, leaf)| format!("{program} {leaf}\n"))
                .collect();
            std::fs::write(&forking_list, text)?;
        }
        let forking = read_forking_list(&forking_list)?;
        if !forking.is_empty() {
            build_asyncify(suite, &uapi, &ltproot, &forking, &logs_dir)?;
        }
        std::fs::write(&ready, "")?;
    }
    for (program, leaf) in read_forking_list(&forking_list)? {
        if leaf == "-" {
            continue;
        }
        let name = program.rsplit('/').next().unwrap_or(&program);
        let log = leaf_log(&logs_dir, Path::new(&leaf), BuildConfig::Asyncify);
        logs.insert(metadata_key(name).to_owned(), log);
    }
    Ok(Prepared { ltproot, logs })
}

/// The build log of a leaf directory, given relative to the tree.
fn leaf_log(logs_dir: &Path, leaf: &Path, config: BuildConfig) -> PathBuf {
    let suffix = match config {
        BuildConfig::ExceptionHandling => "",
        BuildConfig::Asyncify => ".asyncify",
    };
    logs_dir.join(format!(
        "{}{suffix}.log",
        leaf.display().to_string().replace('/', "_")
    ))
}

/// Configures LTP's `tree` for `config` and builds its library, then the leaf directories under
/// `testcases/`: each one's `targets` only, when given, else everything, installed.
fn build_tree(
    tree: &Path,
    config: BuildConfig,
    uapi: &Path,
    targets: Option<&BTreeMap<String, Vec<String>>>,
    logs_dir: &Path,
) -> Result<()> {
    let what = match config {
        BuildConfig::ExceptionHandling => "LTP",
        BuildConfig::Asyncify => "LTP (asyncify)",
    };
    let env = |cmd: &mut Command| {
        wasixcc_env_for(cmd, config).current_dir(tree);
    };
    let mut autotools = Command::new("make");
    env(&mut autotools);
    ensure!(
        autotools.arg("autotools").output()?.status.success(),
        "{what}: make autotools failed"
    );
    let mut configure = Command::new("./configure");
    env(&mut configure);
    let out = configure
        .args([
            "--host=wasm32-unknown-wasixv2",
            "CC=wasixcc",
            "AR=wasixar",
            "RANLIB=wasixranlib",
        ])
        .args(["NM=wasixnm", "HOSTCC=cc"])
        .arg(format!("CPPFLAGS=-isystem {}", uapi.display()))
        .env("WASIXCC_AUTOCONF_WORKAROUNDS", "yes")
        .output()?;
    ensure!(
        out.status.success(),
        "{what}: configure failed:\n{}",
        String::from_utf8_lossy(&out.stdout)
    );
    let mut lib = Command::new("make");
    env(&mut lib);
    let out = lib
        .args(["-C", "lib", "-j"])
        .arg(jobs().to_string())
        .output()?;
    ensure!(
        out.status.success(),
        "{what}: building lib failed:\n{}",
        String::from_utf8_lossy(&out.stderr)
    );

    // Leaf directories one by one: LTP's directory makefiles stop at the first failure.
    std::fs::create_dir_all(logs_dir)?;
    let leaves: Vec<PathBuf> = match targets {
        Some(targets) => targets.keys().map(|leaf| tree.join(leaf)).collect(),
        None => leaf_dirs(tree),
    };
    let next = std::sync::atomic::AtomicUsize::new(0);
    std::thread::scope(|s| {
        for _ in 0..jobs() {
            s.spawn(|| {
                loop {
                    let i = next.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
                    let Some(leaf) = leaves.get(i) else { break };
                    let rel = leaf.strip_prefix(tree).unwrap();
                    let mut text = String::new();
                    let steps: Vec<Vec<String>> = match targets {
                        Some(targets) => vec![targets[&rel.display().to_string()].clone()],
                        None => vec![
                            vec![],
                            vec![
                                "install".to_owned(),
                                format!("DESTDIR={}", tree.join("install").display()),
                            ],
                        ],
                    };
                    for args in steps {
                        let mut make = Command::new("make");
                        env(&mut make);
                        make.arg("-k").arg("-C").arg(leaf).args(args);
                        if let Ok(out) = make.output() {
                            text.push_str(&String::from_utf8_lossy(&out.stdout));
                            text.push_str(&String::from_utf8_lossy(&out.stderr));
                        }
                    }
                    let _ = std::fs::write(leaf_log(logs_dir, rel, config), text);
                }
            });
        }
    });
    Ok(())
}

/// The leaf directories of `testcases/`, which build programs.
fn leaf_dirs(tree: &Path) -> Vec<PathBuf> {
    WalkDir::new(tree.join("testcases"))
        .into_iter()
        .filter_map(Result::ok)
        .filter(|e| e.file_name() == "Makefile")
        .filter(|e| {
            std::fs::read_to_string(e.path()).is_ok_and(|m| m.contains("generic_leaf_target"))
        })
        .map(|e| e.path().parent().unwrap().to_path_buf())
        .collect()
}

/// The installed programs that fork (`links_fork`), each under `ltproot` with the leaf directory
/// that built it, relative to `tree`, or `-` when no leaf directory has the same program.
fn forking_programs(tree: &Path, ltproot: &Path) -> Result<Vec<(String, String)>> {
    let mut forking = Vec::new();
    let mut unnamed = Vec::new();
    for entry in WalkDir::new(ltproot.join("testcases/bin"))
        .into_iter()
        .filter_map(Result::ok)
        .filter(|e| e.file_type().is_file())
    {
        let wasm = std::fs::read(entry.path())?;
        if !wasm.starts_with(b"\0asm") {
            continue;
        }
        let program = entry.path().strip_prefix(ltproot)?.to_owned();
        match super::links_fork(&wasm) {
            Some(true) => forking.push((program, wasm)),
            Some(false) => {}
            None => unnamed.push(program.display().to_string()),
        }
    }
    ensure!(
        unnamed.is_empty(),
        "LTP: programs without a name section, so whether they fork is unknown: {unnamed:?}"
    );
    // Installed programs are copies of what a leaf directory built.
    let leaves = leaf_dirs(tree);
    Ok(forking
        .into_iter()
        .map(|(program, wasm)| {
            let name = program.file_name().unwrap();
            let leaf = leaves
                .iter()
                .find(|leaf| std::fs::read(leaf.join(name)).is_ok_and(|built| built == wasm))
                .map_or_else(
                    || "-".to_owned(),
                    |leaf| leaf.strip_prefix(tree).unwrap().display().to_string(),
                );
            (program.display().to_string(), leaf)
        })
        .collect())
}

/// `wasixv2-forking.txt`: `program leaf` lines, as `forking_programs` finds them.
fn read_forking_list(path: &Path) -> Result<Vec<(String, String)>> {
    let text = std::fs::read_to_string(path)?;
    Ok(text
        .lines()
        .filter_map(|line| line.split_once(' '))
        .map(|(program, leaf)| (program.to_owned(), leaf.to_owned()))
        .collect())
}

/// Builds the programs that fork for asyncify, in a tree of their own, and puts them in place of
/// their exception-handling builds under `ltproot`. A program whose asyncify build fails is
/// removed, so its test fails with the asyncify build's error rather than with ENOSYS.
fn build_asyncify(
    suite: &Suite,
    uapi: &Path,
    ltproot: &Path,
    forking: &[(String, String)],
    logs_dir: &Path,
) -> Result<()> {
    let tree = super::source_copy_as(suite, "ltp", "asyncify")?;
    let mut targets: BTreeMap<String, Vec<String>> = BTreeMap::new();
    for (program, leaf) in forking {
        if leaf != "-" {
            let name = program.rsplit('/').next().unwrap_or(program);
            targets
                .entry(leaf.clone())
                .or_default()
                .push(name.to_owned());
        }
    }
    build_tree(&tree, BuildConfig::Asyncify, uapi, Some(&targets), logs_dir)?;
    for (program, leaf) in forking {
        if leaf == "-" {
            continue;
        }
        let name = program.rsplit('/').next().unwrap_or(program);
        let installed = ltproot.join(program);
        match std::fs::read(tree.join(leaf).join(name)) {
            Ok(built) => std::fs::write(&installed, built)?,
            Err(_) => std::fs::remove_file(&installed)?,
        }
    }
    Ok(())
}

fn jobs() -> usize {
    std::thread::available_parallelism().map_or(4, |n| n.get())
}

impl Test {
    pub fn run(&self, suite: &Suite) -> Result<Outcome, Failure> {
        let prepared = PREPARED
            .get_or_init(|| prepare(suite).map_err(|e| format!("{e:#}")))
            .as_ref()
            .map_err(|e| Failure::harness(format!("preparing LTP: {e}")))?;
        if self.needs_shell() {
            return Err(Failure::new(
                "runner: a shell command; needs a shell in the guest",
                format!("{} {}", self.command, self.args.join(" ")),
            ));
        }
        let binary = prepared.ltproot.join("testcases/bin").join(&self.command);
        let Ok(head) = std::fs::read(&binary).map(|b| b.into_iter().take(4).collect::<Vec<_>>())
        else {
            return Err(self.build_failure(prepared));
        };
        if head.starts_with(b"#!") {
            return Err(Failure::new(
                "runner: a shell script; needs a shell in the guest",
                format!("{} is a script", binary.display()),
            ));
        }
        let dir = suite.build_root.join(&self.name);
        std::fs::create_dir_all(&dir).map_err(|e| Failure::harness(e.to_string()))?;
        let env = [
            ("LTPROOT", "/opt/ltp"),
            ("PATH", "/opt/ltp/testcases/bin:/usr/bin:/bin"),
            ("TMPDIR", "/tmp"),
            ("LTP_COLORIZE_OUTPUT", "0"),
        ];
        let ran = run_wasm(
            suite,
            &Invocation {
                module: binary,
                args: self.args.clone(),
                env: env
                    .iter()
                    .map(|(k, v)| (k.to_string(), v.to_string()))
                    .collect(),
                volumes: vec![(prepared.ltproot.clone(), "/opt/ltp".to_owned())],
                cwd: None,
                user: self.root.then(|| "0:0".to_owned()),
            },
            &dir,
        )?;
        match ran.output.status {
            Some(0) => return Ok(Outcome::Passed),
            // Only TCONF results: the test found what it needs missing.
            Some(32) => {
                let why = super::first_line(&ran.output.stdout, |l| l.contains("TCONF"))
                    .map(old_api_line)
                    .unwrap_or_else(|| "TCONF".to_owned());
                return Ok(Outcome::Skipped(why));
            }
            _ => {}
        }
        let output = format!("{}\n{}", ran.output.stdout, ran.output.stderr);
        let cause = super::first_line(&output, |l| l.contains("TFAIL") || l.contains("TBROK"))
            .map(old_api_line)
            .unwrap_or_else(|| super::libc_test::exit_cause(ran.output.status));
        Err(ran.fail(suite, cause))
    }

    /// A shell script, or a runtest line that only a shell can run (`;`, pipes, `$VAR`).
    fn needs_shell(&self) -> bool {
        self.command.ends_with(".sh")
            || self
                .args
                .iter()
                .any(|a| a.contains([';', '|', '&', '$', '>', '<']))
    }

    /// The binary was not installed: report the first error of its directory's build.
    fn build_failure(&self, prepared: &Prepared) -> Failure {
        let log = prepared
            .logs
            .get(metadata_key(&self.command))
            .and_then(|log| std::fs::read_to_string(log).ok())
            .unwrap_or_default();
        let cause = match crate::build::first_error_line(&log, "").as_str() {
            "failed without output" => {
                format!("`{}` is not built by LTP's makefiles", self.command)
            }
            line => line.to_owned(),
        };
        Failure::new(
            format!("build: {cause}"),
            format!("`{}` was not built\n--- build log\n{log}", self.command),
        )
    }
}

/// The cause in an LTP result line. The old API prints `name  N  TFAIL  :  file.c:42: message`,
/// the new one `file.c:42: TFAIL: message`; both become `TFAIL: message`.
fn old_api_line(line: &str) -> String {
    for kind in ["TFAIL", "TBROK", "TCONF"] {
        if let Some((_, rest)) = line.split_once(&format!("{kind}  :")) {
            return super::cause_from_line(&format!("{kind}: "), rest);
        }
    }
    super::cause_from_line("", line)
}
