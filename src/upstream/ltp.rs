//! The Linux Test Project (`upstream/ltp`, wasix-org's fork with the wasixv2 patches): the tests
//! of LTP's `runtest/` lists, named `ltp/<list>/<tag>` and run with the list's command line.
//!
//! LTP is built once per commit: its own configure and makefiles with wasixcc, against Linux's
//! UAPI headers: the sysroot's, or, for a sysroot without them, ones generated for LTP's build
//! only; and with libcap (`upstream/libcap`), built for each configuration first, so the tests
//! that use it (`HAVE_LIBCAP`) are built whole. Tests whose metadata (or `excluded.txt`) shows they
//! cannot make sense on wasixv2 are excluded. Tests that need root run as uid 0.
//!
//! Shell tests (LTP's shell scripts, and runtest lines that need a shell) run as
//! `bash -c '<line>'` with a v2 userland at `/bin`, from `WASIXV2_TESTS_USERLAND`; without one
//! they are skipped.
//!
//! Other tests run as a child of `ltp-launch.c`, in a process group of its own, as LTP's own
//! runners run them: the program `wasmer run` starts leads a session, which a test must not (see
//! there).
//!
//! A test that needs kernel options (`needs_kconfigs`) runs only if wasixv2's kernel has every
//! one ([`KCONFIGS`]); LTP checks them against a kernel configuration that lists those
//! (`KCONFIG_PATH`).

use std::collections::{BTreeMap, HashMap};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::OnceLock;

use anyhow::{Context, Result, bail, ensure};
use walkdir::WalkDir;

use super::{
    BuildConfig, Case, ExcludeList, Excluded, Invocation, Outcome, run_wasm, wasixcc_env,
    wasixcc_env_for,
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
    "commands",
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

/// The kernel options currently advertised to LTP: pid, IPC, UTS, network and user
/// namespaces, dnotify, and AIO. Mount namespaces have no option of their own.
const KCONFIGS: &[&str] = &[
    "CONFIG_PID_NS",
    "CONFIG_IPC_NS",
    "CONFIG_UTS_NS",
    "CONFIG_NET_NS",
    "CONFIG_DNOTIFY",
    "CONFIG_USER_NS",
    "CONFIG_AIO",
];

/// The kernel configuration LTP reads for the tests that need options, in the install.
const KCONFIG_FILE: &str = "wasixv2-kconfig";

/// The version of what the runner builds in LTP's tree. A tree built by an older runner, with
/// another version, is built again. 2: trunk directories too. 3: with libcap. 4: the network
/// tests' data files. 5: with libaio.
const BUILD_VERSION: u32 = 5;

/// The version of which programs `links_fork` finds forking. An install prepared with another
/// version is completed in place: the programs it finds now are built for asyncify too. 2: raw
/// `clone`/`clone3` syscalls. 3: libc2's `clone` wrapper.
const FORK_DETECTION: u32 = 3;

/// Linux release whose UAPI headers LTP builds against, the newest release covering wasixv2's
/// syscall table but for `fchroot`.
const LINUX_UAPI_TAG: &str = "v7.2";

pub struct Test {
    name: String,
    command: String,
    args: Vec<String>,
    /// The runtest line after the tag, which shell tests run.
    line: String,
    /// The test needs root (`needs_root`, `TST_NEEDS_ROOT=1`, or `tst_require_root` in the old
    /// APIs, or a `.caps` declaration): it runs as uid 0, which holds every capability.
    root: bool,
    budget: std::time::Duration,
}

struct Prepared {
    /// `opt/ltp` of the installed build.
    ltproot: PathBuf,
    /// `ltp-launch.c`, built.
    launcher: PathBuf,
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
    let scripts = script_index(&src());
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
        for Entry {
            tag,
            command,
            args,
            line,
        } in entries
        {
            let name = format!("ltp/{list}/{tag}");
            let scripts = script_texts(&line, &scripts);
            let declared = declared(&metadata, &sources, &command, &scripts);
            let reason = excludes
                .reason(&name)
                .map(str::to_owned)
                .or_else(|| scripts.iter().find_map(|text| script_reason(text)))
                .or_else(|| declared.iter().find_map(metadata_reason));
            if let Some(reason) = reason {
                excluded.add(reason);
                continue;
            }
            let test = Test {
                name: name.clone(),
                root: needs_root(&declared),
                budget: declared_budget(&declared, std::time::Duration::from_secs(30)),
                command,
                args,
                line,
            };
            tests.push((name, Case::Ltp(test)));
        }
    }
    Ok(tests)
}

/// A `tag command args...` line of a runtest list.
struct Entry {
    tag: String,
    command: String,
    args: Vec<String>,
    /// Everything after the tag, as written.
    line: String,
}

/// The entries of a runtest list.
fn runtest_entries(path: &Path) -> Result<Vec<Entry>> {
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
        entries.push(Entry {
            tag: tag.to_owned(),
            command: command.to_owned(),
            args: words.map(str::to_owned).collect(),
            line: line[tag.len()..].trim().to_owned(),
        });
    }
    Ok(entries)
}

/// The metadata key of a runtest command: its binary, without the `_64` of the
/// `_FILE_OFFSET_BITS=64` variants.
fn metadata_key(command: &str) -> &str {
    command.strip_suffix("_64").unwrap_or(command)
}

fn needs_root(declared: &[serde_json::Value]) -> bool {
    declared
        .iter()
        .any(|test| has(test, "needs_root") || has(test, "caps"))
}

fn declared_budget(
    declared: &[serde_json::Value],
    default: std::time::Duration,
) -> std::time::Duration {
    // LTP lib/tst_test.c set_overall_timeout: 30 seconds plus the timeout
    // adjustment and runtime, once per test variant. Leave 30 seconds for the
    // launcher/compilation and use the suite's configured budget as a floor.
    let seconds = |test: &serde_json::Value, key: &str| {
        test.get(key)
            .and_then(|v| v.as_u64().or_else(|| v.as_str()?.parse().ok()))
            .unwrap_or(0)
    };
    let declared = declared
        .iter()
        .map(|test| {
            let runtime = seconds(test, "runtime").max(seconds(test, "min_runtime"));
            (30u64
                .saturating_add(seconds(test, "timeout"))
                .saturating_add(runtime))
            .max(seconds(test, "overall_timeout"))
            .saturating_mul(seconds(test, "test_variants").max(1))
        })
        .max()
        .unwrap_or(30);
    default.max(std::time::Duration::from_secs(declared.saturating_add(30)))
}

/// Whether a test's metadata sets `key`.
fn has(test: &serde_json::Value, key: &str) -> bool {
    test.get(key)
        .is_some_and(|v| !v.is_null() && v != &serde_json::Value::from(0))
}

/// What a test declares it needs, in the shape of an entry of LTP's metadata: one for its program
/// and one for each LTP shell script (`scripts`, their text) its runtest line runs.
fn declared(
    metadata: &serde_json::Value,
    sources: &HashMap<String, PathBuf>,
    command: &str,
    scripts: &[String],
) -> Vec<serde_json::Value> {
    let key = metadata_key(command);
    let mut program = metadata
        .get("tests")
        .and_then(|t| t.get(key))
        .cloned()
        .unwrap_or_else(|| serde_json::json!({}));
    // Old-API tests have no metadata; they call tst_require_root().
    let old_api_root = sources
        .get(key)
        .and_then(|path| std::fs::read_to_string(path).ok())
        .is_some_and(|text| {
            text.contains("tst_require_root()") && !text.contains("struct tst_test")
        });
    if old_api_root && let Some(program) = program.as_object_mut() {
        program.insert("needs_root".to_owned(), true.into());
    }
    let mut declared = vec![program];
    declared.extend(scripts.iter().map(|text| script_metadata(text)));
    declared
}

/// The metadata of a shell test on LTP's shell library (`tst_test.sh`), which declares what it
/// needs in `TST_*` variables, or of an old-API one (`test.sh`), which calls `tst_require_root`.
/// A network test (`tst_net.sh`) needs root.
fn script_metadata(text: &str) -> serde_json::Value {
    const VARIABLES: &[(&str, &str)] = &[
        ("TST_NEEDS_ROOT", "needs_root"),
        ("TST_NEEDS_DEVICE", "needs_device"),
        ("TST_FORMAT_DEVICE", "format_device"),
        ("TST_MOUNT_DEVICE", "mount_device"),
        ("TST_ALL_FILESYSTEMS", "all_filesystems"),
        ("TST_NEEDS_KCONFIGS", "needs_kconfigs"),
    ];
    let mut test = serde_json::Map::new();
    let mut timeout = None;
    let mut timed_shell = false;
    for line in text.lines().map(str::trim) {
        if line.starts_with('#') {
            continue;
        }
        if line.contains("tst_require_root") && !line.contains("tst_require_root()") {
            test.insert("needs_root".to_owned(), true.into());
        }
        // The network library makes the test's second host, a network namespace joined by a
        // veth pair, as root (`init_ltp_netspace` calls `tst_require_root`); the library is not
        // among the scripts read here.
        if line == ". tst_net.sh" {
            test.insert("needs_root".to_owned(), true.into());
        }
        // testcases/lib/tst_test.sh _tst_setup_timer defaults to 300 seconds;
        // tst_net.sh sources that library too, including for its legacy API.
        timed_shell |= matches!(line, ". tst_test.sh" | ". tst_net.sh");
        let assignment = line.strip_prefix("export ").unwrap_or(line);
        if let Some(value) = assignment.strip_prefix("TST_TIMEOUT=") {
            let value = value
                .split_whitespace()
                .next()
                .unwrap_or_default()
                .trim_matches(['\'', '"']);
            timeout = match value {
                "-1" => Some(0), // LTP disables its timer; retain the runner's configured floor.
                value => value.parse::<u64>().ok(),
            };
        }
        for (variable, key) in VARIABLES {
            let Some(value) = assignment
                .strip_prefix(variable)
                .and_then(|rest| rest.strip_prefix('='))
            else {
                continue;
            };
            let value = value.trim_matches(|c: char| c == '"' || c == '\'' || c.is_whitespace());
            if !value.is_empty() && value != "0" {
                test.insert((*key).to_owned(), true.into());
            }
        }
    }
    if timed_shell || timeout.is_some() {
        test.insert("overall_timeout".to_owned(), timeout.unwrap_or(300).into());
    }
    test.into()
}

/// The text of each LTP shell script a runtest line runs, and of the scripts they source
/// (`. netns_lib.sh`), where tests share their setup and declarations.
fn script_texts(line: &str, scripts: &HashMap<String, PathBuf>) -> Vec<String> {
    let mut pending: Vec<String> = line.split_whitespace().map(str::to_owned).collect();
    let mut seen = std::collections::HashSet::new();
    let mut texts = Vec::new();
    while let Some(word) = pending.pop() {
        let Some(path) = scripts.get(&word) else {
            continue;
        };
        if !seen.insert(word) {
            continue;
        }
        let Ok(text) = std::fs::read_to_string(path) else {
            continue;
        };
        pending.extend(
            text.lines()
                .filter_map(|l| {
                    let l = l.trim_start();
                    l.strip_prefix(". ").or_else(|| l.strip_prefix("source "))
                })
                .filter_map(|rest| rest.split_whitespace().next())
                .map(str::to_owned),
        );
        texts.push(text);
    }
    texts
}

/// Why a shell script cannot run on wasixv2, beyond what its metadata declares.
fn script_reason(text: &str) -> Option<String> {
    // tst_loader.sh runs the script under tst_run_shell, a C test whose library shares its
    // results page with the shell (LTP_IPC_PATH, MAP_SHARED): forked processes on wasixv2 do not
    // share memory.
    text.lines()
        .any(|line| !line.trim_start().starts_with('#') && line.contains("tst_loader.sh"))
        .then(|| {
            "LTP: a shell test on the C test library (tst_loader.sh), which needs memory shared \
             between processes"
                .to_owned()
        })
}

/// Why a test's declared requirements cannot make sense on wasixv2.
fn metadata_reason(test: &serde_json::Value) -> Option<String> {
    let has = |key: &str| has(test, key);
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
    } else if has("needs_hugetlbfs") || has("hugepages") {
        "needs huge pages, which wasm memory does not have"
    } else if has("needs_cgroup_ctrls") || has("needs_cgroup_ver") || has("needs_cgroup_nsdelegate")
    {
        "needs cgroups"
    } else if has("needs_kconfigs") && !kconfigs_supported(test) {
        "needs kernel options wasixv2's kernel does not have"
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

/// Whether the kernel has every option `test` needs: each of its `needs_kconfigs` is one of
/// [`KCONFIGS`], alone or `=y`. A shell test's, which are not parsed, are not.
fn kconfigs_supported(test: &serde_json::Value) -> bool {
    let Some(kconfigs) = test.get("needs_kconfigs").and_then(|v| v.as_array()) else {
        return false;
    };
    kconfigs.iter().all(|kconfig| {
        let kconfig = kconfig.as_str().unwrap_or_default().trim();
        let name = kconfig.strip_suffix("=y").unwrap_or(kconfig);
        KCONFIGS.contains(&name)
    })
}

/// The kernel configuration of [`KCONFIG_FILE`]: every option of [`KCONFIGS`] built in.
fn kconfig_text() -> String {
    KCONFIGS.iter().map(|name| format!("{name}=y\n")).collect()
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

/// Every shell script under `testcases/` but LTP's shell library, by file name, as runtest lines
/// name them.
fn script_index(root: &Path) -> HashMap<String, PathBuf> {
    WalkDir::new(root.join("testcases"))
        .into_iter()
        .filter_entry(|e| e.path() != root.join("testcases/lib"))
        .filter_map(Result::ok)
        .filter(|e| e.file_type().is_file() && e.path().extension().is_some_and(|x| x == "sh"))
        .map(|e| (e.file_name().to_string_lossy().into_owned(), e.into_path()))
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

/// Where LTP's build for `config` finds Linux's UAPI headers: in the sysroot (`None`), which
/// libc2's sysroots provide (`linux/`, `asm/`, `asm-generic/`), or else in headers generated for
/// it (`uapi_headers`).
fn uapi_for(suite: &Suite, config: BuildConfig) -> Result<Option<PathBuf>> {
    let scratch = tempfile::tempdir()?;
    let probe = scratch.path().join("uapi.c");
    // linux/types.h includes asm/types.h, which includes asm-generic/int-ll64.h.
    std::fs::write(&probe, "#include <linux/types.h>\n")?;
    let mut cmd = Command::new(&suite.cc);
    wasixcc_env_for(&mut cmd, config)
        .current_dir(scratch.path())
        .arg("-E")
        .arg(&probe)
        .arg("-o")
        .arg(scratch.path().join("uapi.i"));
    if cmd.output().context("running wasixcc")?.status.success() {
        Ok(None)
    } else {
        uapi_headers(&suite.build_root).map(Some)
    }
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

/// Builds `ltp-launch.c`, the program each test runs under, for this run.
fn launcher(suite: &Suite) -> Result<PathBuf> {
    let dir = suite.build_root.join("ltp-launch");
    std::fs::create_dir_all(&dir)?;
    let out = dir.join("ltp-launch.wasm");
    let partial = dir.join(format!("ltp-launch.{}.wasm", std::process::id()));
    let mut cc = Command::new(&suite.cc);
    wasixcc_env(&mut cc)
        .arg("-O2")
        .arg(crate::root().join("src/upstream/ltp-launch.c"))
        .arg("-o")
        .arg(&partial);
    let built = cc.output().context("building ltp-launch")?;
    ensure!(
        built.status.success(),
        "building ltp-launch failed:\n{}",
        String::from_utf8_lossy(&built.stderr)
    );
    std::fs::rename(&partial, &out)?;
    Ok(out)
}

fn prepare(suite: &Suite) -> Result<Prepared> {
    let launcher = launcher(suite)?;
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
    let built = tree.join(format!(".wasixv2-ltp-built-{BUILD_VERSION}"));
    let forking_list = tree.join(format!(
        "wasixv2-forking-{BUILD_VERSION}.{FORK_DETECTION}.txt"
    ));
    let ready = tree.join(format!(
        ".wasixv2-ltp-ready-{BUILD_VERSION}.{FORK_DETECTION}"
    ));
    if !ready.exists() {
        if !built.exists() {
            build_tree(
                &tree,
                BuildConfig::ExceptionHandling,
                uapi_for(suite, BuildConfig::ExceptionHandling)?.as_deref(),
                &libcap(suite, BuildConfig::ExceptionHandling)?,
                &libaio(suite, BuildConfig::ExceptionHandling)?,
                None,
                &logs_dir,
            )?;
            std::fs::write(&built, "")?;
        }
        if !forking_list.exists() {
            let forking = forking_programs(&tree, &ltproot, earlier_forking(&tree)?)?;
            let text: String = forking
                .iter()
                .map(|(program, leaf)| format!("{program} {leaf}\n"))
                .collect();
            std::fs::write(&forking_list, text)?;
        }
        let forking = read_forking_list(&forking_list)?;
        if !forking.is_empty() {
            build_asyncify(suite, &ltproot, &forking, &logs_dir)?;
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
    std::fs::write(ltproot.join(KCONFIG_FILE), kconfig_text())?;
    Ok(Prepared {
        ltproot,
        launcher,
        logs,
    })
}

/// The build log of a directory of `program_dirs`, given relative to the tree.
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

/// The headers and archive directory of a statically built dependency.
struct StaticLibrary {
    include: PathBuf,
    lib: PathBuf,
}

/// libcap's static library, built for `config` from `upstream/libcap` in a copy of its own
/// (`make -C libcap libcap.a`, its names table made by a host program, as its makefiles do):
/// the directory of its headers (`sys/capability.h`) and of `libcap.a`.
fn libcap(suite: &Suite, config: BuildConfig) -> Result<StaticLibrary> {
    let variant = match config {
        BuildConfig::ExceptionHandling => "",
        BuildConfig::Asyncify => "asyncify",
    };
    let tree = super::source_copy_as(suite, "libcap", variant)?;
    let dir = tree.join("libcap");
    let built = tree.join(".wasixv2-libcap-built");
    if !built.exists() {
        let mut make = Command::new("make");
        wasixcc_env_for(&mut make, config)
            .arg("-C")
            .arg(&dir)
            .arg(format!("CC={}", suite.cc))
            .args(["BUILD_CC=cc", "AR=wasixar", "RANLIB=wasixranlib"])
            // No shared library, no libpsx (pthreads), no Go bindings, no gperf table.
            .args(["SHARED=no", "PTHREADS=no", "GOLANG=no", "USE_GPERF=no"])
            .arg("libcap.a");
        let out = make.output().context("building libcap")?;
        ensure!(
            out.status.success(),
            "libcap: make failed:\n{}",
            String::from_utf8_lossy(&out.stderr)
        );
        std::fs::write(&built, "")?;
    }
    Ok(StaticLibrary {
        include: dir.join("include"),
        lib: dir,
    })
}

/// Build libaio's static archive using its libc syscall fallback, the time64-only syscall
/// table, and ordinary static aliases for the current API.
fn libaio(suite: &Suite, config: BuildConfig) -> Result<StaticLibrary> {
    let variant = match config {
        BuildConfig::ExceptionHandling => "",
        BuildConfig::Asyncify => "asyncify",
    };
    let tree = super::source_copy_as(suite, "libaio", variant)?;
    let dir = tree.join("src");
    let built = tree.join(".wasixv2-libaio-built");
    if !built.exists() {
        let patched = tree.join(".wasixv2-wasm-patched");
        if !patched.exists() {
            let mut patch = Command::new("patch");
            let out = patch
                .current_dir(&tree)
                .args(["-p1", "--forward", "-i"])
                .arg(crate::root().join("src/upstream/libaio-wasm.patch"))
                .output()
                .context("adapting libaio for WASM")?;
            ensure!(
                out.status.success(),
                "libaio: patch failed:\n{}\n{}",
                String::from_utf8_lossy(&out.stdout),
                String::from_utf8_lossy(&out.stderr)
            );
            std::fs::write(patched, "")?;
        }
        let mut make = Command::new("make");
        wasixcc_env_for(&mut make, config)
            .arg("-C")
            .arg(&dir)
            .arg(format!("CC={}", suite.cc))
            .args([
                "AR=wasixar",
                "RANLIB=wasixranlib",
                "ENABLE_SHARED=0",
                "libaio.a",
                // Upstream normally checks the shared build's ABI layout only.
                "struct_offsets.o",
            ]);
        let out = make.output().context("building libaio")?;
        ensure!(
            out.status.success(),
            "libaio: make failed:\n{}",
            String::from_utf8_lossy(&out.stderr)
        );
        std::fs::write(&built, "")?;
    }
    Ok(StaticLibrary {
        include: dir.clone(),
        lib: dir,
    })
}

/// Configures LTP's `tree` for `config` with libcap and libaio and builds its library, then the
/// directories under `testcases/` with programs (`program_dirs`): each one's `targets` only,
/// when given, else everything, installed.
fn build_tree(
    tree: &Path,
    config: BuildConfig,
    uapi: Option<&Path>,
    libcap: &StaticLibrary,
    libaio: &StaticLibrary,
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
    configure
        .args([
            "--host=wasm32-unknown-wasixv2",
            "CC=wasixcc",
            "AR=wasixar",
            "RANLIB=wasixranlib",
        ])
        .args(["NM=wasixnm", "HOSTCC=cc"])
        .env("WASIXCC_AUTOCONF_WORKAROUNDS", "yes");
    let mut cppflags = format!(
        "-isystem {} -isystem {}",
        libcap.include.display(),
        libaio.include.display()
    );
    if let Some(uapi) = uapi {
        cppflags.push_str(&format!(" -isystem {}", uapi.display()));
    }
    configure.arg(format!("CPPFLAGS={cppflags}")).arg(format!(
        "LDFLAGS=-L{} -L{}",
        libcap.lib.display(),
        libaio.lib.display()
    ));
    let out = configure.output()?;
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

    // Directories one by one: LTP's directory makefiles stop at the first failure.
    std::fs::create_dir_all(logs_dir)?;
    let leaves: Vec<PathBuf> = match targets {
        Some(targets) => targets.keys().map(|leaf| tree.join(leaf)).collect(),
        None => program_dirs(tree),
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
                    // A trunk directory's own programs and scripts, without its subdirectories,
                    // which are directories of their own here. LTP's makefiles define install
                    // rules only when `install` is a goal (env_post.mk), and a trunk's `install`
                    // recurses: ask for `trunk-install`, with the goals the rules look for.
                    let (all, install) = if is_trunk(leaf) {
                        // `testcases/network` installs `datafiles/ascii.*` (the network tests'
                        // `$TST_NET_DATAROOT`), which only its `generate` makes; its makefile
                        // looks for them as it is read, so they are made by the step before.
                        let generate =
                            (rel == Path::new("testcases/network")).then(|| "generate".to_owned());
                        (
                            ["trunk-all".to_owned()]
                                .into_iter()
                                .chain(generate)
                                .collect(),
                            vec![
                                "trunk-install".to_owned(),
                                "MAKECMDGOALS=install".to_owned(),
                            ],
                        )
                    } else {
                        (vec![], vec!["install".to_owned()])
                    };
                    let steps: Vec<Vec<String>> = match targets {
                        Some(targets) => vec![targets[&rel.display().to_string()].clone()],
                        None => vec![
                            all,
                            [
                                install,
                                vec![format!("DESTDIR={}", tree.join("install").display())],
                            ]
                            .concat(),
                        ],
                    };
                    for args in steps {
                        let mut make = Command::new("make");
                        env(&mut make);
                        // Each worker builds one leaf at a time; do not multiply MAKEFLAGS=-j3.
                        make.arg("-j1").arg("-k").arg("-C").arg(leaf).args(args);
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

/// The directories of `testcases/` that build or install programs: leaf directories, and trunk
/// directories, which can have programs and scripts of their own besides their subdirectories
/// (`testcases/lib`, LTP's shell library; `fs_bind_lib.sh`, ...).
fn program_dirs(tree: &Path) -> Vec<PathBuf> {
    WalkDir::new(tree.join("testcases"))
        .into_iter()
        .filter_map(Result::ok)
        .filter(|e| e.file_name() == "Makefile")
        .filter(|e| {
            std::fs::read_to_string(e.path()).is_ok_and(|m| {
                m.contains("generic_leaf_target") || m.contains("generic_trunk_target")
            })
        })
        .map(|e| e.path().parent().unwrap().to_path_buf())
        .collect()
}

/// Whether `dir`'s makefile is a trunk's, whose `all` and `install` recurse into its
/// subdirectories.
fn is_trunk(dir: &Path) -> bool {
    std::fs::read_to_string(dir.join("Makefile")).is_ok_and(|m| m.contains("generic_trunk_target"))
}

/// The installed programs that fork (`links_fork`), each under `ltproot` with the directory that
/// built it, relative to `tree`, or `-` when no directory has the same program. `earlier`, the
/// programs an earlier detection found, are installed from their asyncify builds already, and
/// stay.
fn forking_programs(
    tree: &Path,
    ltproot: &Path,
    earlier: BTreeMap<String, String>,
) -> Result<Vec<(String, String)>> {
    let mut forking = Vec::new();
    let mut unnamed = Vec::new();
    for entry in WalkDir::new(ltproot.join("testcases/bin"))
        .into_iter()
        .filter_map(Result::ok)
        .filter(|e| e.file_type().is_file())
    {
        let program = entry.path().strip_prefix(ltproot)?.to_owned();
        if earlier.contains_key(&program.display().to_string()) {
            continue;
        }
        let wasm = std::fs::read(entry.path())?;
        if !wasm.starts_with(b"\0asm") {
            continue;
        }
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
    // Installed programs are copies of what a directory built.
    let leaves = program_dirs(tree);
    let found = forking.into_iter().map(|(program, wasm)| {
        let name = program.file_name().unwrap();
        let leaf = leaves
            .iter()
            .find(|leaf| std::fs::read(leaf.join(name)).is_ok_and(|built| built == wasm))
            .map_or_else(
                || "-".to_owned(),
                |leaf| leaf.strip_prefix(tree).unwrap().display().to_string(),
            );
        (program.display().to_string(), leaf)
    });
    let mut all: Vec<_> = earlier.into_iter().chain(found).collect();
    all.sort();
    Ok(all)
}

/// The forking programs that earlier detections listed for this build of `tree`
/// (`FORK_DETECTION`), whose asyncify builds are installed.
fn earlier_forking(tree: &Path) -> Result<BTreeMap<String, String>> {
    let mut earlier = BTreeMap::new();
    for entry in std::fs::read_dir(tree)? {
        let name = entry?.file_name();
        let name = name.to_string_lossy();
        let Some(rest) = name.strip_prefix(&format!("wasixv2-forking-{BUILD_VERSION}")) else {
            continue;
        };
        if rest == ".txt" || (rest.starts_with('.') && rest.ends_with(".txt")) {
            earlier.extend(read_forking_list(&tree.join(&*name))?);
        }
    }
    Ok(earlier)
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
    let uapi = uapi_for(suite, BuildConfig::Asyncify)?;
    build_tree(
        &tree,
        BuildConfig::Asyncify,
        uapi.as_deref(),
        &libcap(suite, BuildConfig::Asyncify)?,
        &libaio(suite, BuildConfig::Asyncify)?,
        Some(&targets),
        logs_dir,
    )?;
    for (program, leaf) in forking {
        if leaf == "-" {
            continue;
        }
        let name = program.rsplit('/').next().unwrap_or(program);
        let installed = ltproot.join(program);
        match std::fs::read(tree.join(leaf).join(name)) {
            Ok(built) => std::fs::write(&installed, built)?,
            // Gone already when an earlier detection's asyncify build failed too.
            Err(_) => match std::fs::remove_file(&installed) {
                Err(e) if e.kind() != std::io::ErrorKind::NotFound => return Err(e.into()),
                _ => {}
            },
        }
    }
    Ok(())
}

fn jobs() -> usize {
    std::thread::available_parallelism().map_or(3, |n| n.get().min(3))
}

impl Test {
    fn environment(&self) -> Vec<(String, String)> {
        let mut env: Vec<(String, String)> = [
            ("LTPROOT", "/opt/ltp"),
            ("PATH", "/opt/ltp/testcases/bin:/usr/bin:/bin"),
            ("TMPDIR", "/tmp"),
            ("LTP_COLORIZE_OUTPUT", "0"),
        ]
        .iter()
        .map(|(k, v)| (k.to_string(), v.to_string()))
        .collect();
        // LTP helpers can inspect configuration from setup without needs_kconfigs
        // metadata (lib/tst_cgroup.c tst_check_rt_group_sched_support).
        env.push(("KCONFIG_PATH".into(), format!("/opt/ltp/{KCONFIG_FILE}")));
        env
    }

    pub fn run(&self, suite: &Suite) -> Result<Outcome, Failure> {
        let suite = &Suite {
            timeout: suite.timeout.max(self.budget),
            ..suite.clone()
        };
        let prepared = PREPARED
            .get_or_init(|| prepare(suite).map_err(|e| format!("{e:#}")))
            .as_ref()
            .map_err(|e| Failure::harness(format!("preparing LTP: {e}")))?;
        let binary = prepared.ltproot.join("testcases/bin").join(&self.command);
        let shell = self.needs_shell() || {
            let Ok(head) =
                std::fs::read(&binary).map(|b| b.into_iter().take(4).collect::<Vec<_>>())
            else {
                return Err(self.build_failure(prepared, &self.command));
            };
            head.starts_with(b"#!")
        };
        let env = self.environment();
        let mut invocation = Invocation {
            module: binary,
            args: self.args.clone(),
            env,
            volumes: vec![(prepared.ltproot.clone(), "/opt/ltp".to_owned())],
            cwd: None,
            user: self.root.then(|| "0:0".to_owned()),
        };
        if shell {
            // A script the runtest line names is LTP's to install.
            let bin = prepared.ltproot.join("testcases/bin");
            if let Some(script) = self.line.split_whitespace().find(|word| {
                word.ends_with(".sh") && !word.contains(['/', '$']) && !bin.join(word).is_file()
            }) {
                return Err(self.build_failure(prepared, script));
            }
            let Some(userland) = super::userland()? else {
                return Ok(Outcome::Skipped(
                    "a shell test; set WASIXV2_TESTS_USERLAND to run it".to_owned(),
                ));
            };
            self.in_shell(userland, &mut invocation);
        } else {
            self.launched(&prepared.launcher, &mut invocation);
        }
        let dir = suite.build_root.join(&self.name);
        std::fs::create_dir_all(&dir).map_err(|e| Failure::harness(e.to_string()))?;
        let ran = run_wasm(suite, &invocation, &dir)?;
        match ran.output.status {
            Some(0) => return Ok(Outcome::Passed),
            // Only TCONF results: the test found what it needs missing.
            Some(32) => {
                // The shell library reports on stderr.
                let output = format!("{}\n{}", ran.output.stdout, ran.output.stderr);
                let why = super::first_line(&output, |l| l.contains("TCONF"))
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

    /// Runs the runtest line with the userland's bash instead, as LTP's runners do (`sh -c`).
    ///
    /// The shell's job control (`set -m`) puts the test in a process group of its own, which
    /// LTP's shell library expects: its timer, `tst_timeout_kill`, signals the test script's
    /// group, and refuses pid 1, which this way is never the test. The trailing `exit` keeps bash
    /// from executing the last command in place of itself.
    fn in_shell(&self, userland: &Path, invocation: &mut Invocation) {
        invocation.module = userland.join("bash");
        invocation.args = vec!["-c".to_owned(), format!("set -m; {}; exit $?", self.line)];
    }

    /// Runs the test under `launcher` (`ltp-launch.c`), which starts it from `PATH` in a process
    /// group of its own and waits for it, as LTP's runners do: the test is not the program that
    /// `wasmer run` starts, which leads its session.
    fn launched(&self, launcher: &Path, invocation: &mut Invocation) {
        invocation.module = launcher.to_owned();
        invocation.args.insert(0, self.command.clone());
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
    fn build_failure(&self, prepared: &Prepared, program: &str) -> Failure {
        let log = prepared
            .logs
            .get(metadata_key(program))
            .and_then(|log| std::fs::read_to_string(log).ok())
            .unwrap_or_default();
        let cause = match crate::build::first_error_line(&log, "").as_str() {
            "failed without output" => {
                format!("`{program}` is not built by LTP's makefiles")
            }
            line => line.to_owned(),
        };
        Failure::new(
            format!("build: {cause}"),
            format!("`{program}` was not built\n--- build log\n{log}"),
        )
    }
}

/// The cause in an LTP result line. The old API prints `name  N  TFAIL  :  file.c:42: message`,
/// the new one `file.c:42: TFAIL: message`, the shell library `name N TFAIL: message`; all become
/// `TFAIL: message`.
fn old_api_line(line: &str) -> String {
    for kind in ["TFAIL", "TBROK", "TCONF"] {
        if let Some((_, rest)) = line.split_once(&format!("{kind}  :")) {
            return super::cause_from_line(&format!("{kind}: "), rest);
        }
    }
    // The shell library's `name N TFAIL: message`.
    let mut words = line.trim().splitn(3, ' ');
    if let (Some(_), Some(n), Some(rest)) = (words.next(), words.next(), words.next())
        && n.bytes().all(|b| b.is_ascii_digit())
        && let Some((kind @ ("TFAIL" | "TBROK" | "TCONF"), message)) = rest.split_once(": ")
    {
        return super::cause_from_line(&format!("{kind}: "), message);
    }
    super::cause_from_line("", line)
}

#[cfg(test)]
mod tests {
    use super::{
        has, kconfig_text, kconfigs_supported, metadata_reason, old_api_line, script_metadata,
        script_reason,
    };

    #[test]
    #[ignore = "requires the WASIX v2 toolchain, sysroots, and release wasmer"]
    fn libaio_build_and_syscalls() {
        let suite = crate::Suite::from_env().unwrap();
        for config in [
            super::BuildConfig::ExceptionHandling,
            super::BuildConfig::Asyncify,
        ] {
            let library = super::libaio(&suite, config).unwrap();
            let main = library.lib.join("wasixv2-libaio-check.wasm");
            let mut cc = std::process::Command::new(&suite.cc);
            super::wasixcc_env_for(&mut cc, config)
                .arg("-I")
                .arg(&library.include)
                .arg(crate::root().join("src/upstream/libaio-check.c"))
                .arg("-L")
                .arg(&library.lib)
                .arg("-laio")
                .arg("-o")
                .arg(&main);
            super::super::run_build(&mut cc).unwrap();
            let ran = super::run_wasm(
                &suite,
                &super::Invocation {
                    module: main,
                    ..Default::default()
                },
                &library.lib,
            )
            .unwrap();
            assert_eq!(ran.output.status, Some(0), "{}", ran.captured());
        }
    }

    #[test]
    fn capability_declarations_require_root() {
        assert!(super::needs_root(&[
            serde_json::json!({"caps": ["TST_CAP(TST_CAP_DROP, CAP_NET_RAW)"]})
        ]));
        assert!(super::needs_root(&[serde_json::json!({"needs_root": 1})]));
        assert!(!super::needs_root(&[serde_json::json!({})]));
    }

    #[test]
    fn declared_runtime_is_allowed_to_finish() {
        use std::time::Duration;
        let budget = |value| super::declared_budget(&[value], Duration::from_secs(60));
        assert_eq!(budget(serde_json::json!({})), Duration::from_secs(60));
        // LTP lib/tst_test.c set_overall_timeout adds DEFAULT_TIMEOUT (30).
        assert_eq!(
            budget(serde_json::json!({"runtime": 120})),
            Duration::from_secs(180)
        );
        assert_eq!(
            budget(serde_json::json!({"runtime": 600})),
            Duration::from_secs(660)
        );
        assert_eq!(
            budget(serde_json::json!({"runtime": 120, "timeout": 30, "test_variants": 2})),
            Duration::from_secs(390)
        );
        assert_eq!(
            budget(serde_json::json!({"min_runtime": 180})),
            Duration::from_secs(240)
        );
    }

    #[test]
    fn shell_timeout_is_an_overall_duration() {
        use std::time::Duration;
        let budget =
            |text| super::declared_budget(&[script_metadata(text)], Duration::from_secs(60));
        assert_eq!(
            budget("TST_TIMEOUT=120\n. tst_test.sh\n"),
            Duration::from_secs(150)
        );
        assert_eq!(budget(". tst_net.sh\n"), Duration::from_secs(330));
        assert_eq!(
            budget("TST_TIMEOUT=-1\n. tst_test.sh\n"),
            Duration::from_secs(60)
        );
    }

    #[test]
    fn build_parallelism_respects_the_shared_machine_limit() {
        assert!(super::jobs() <= 3);
    }

    #[test]
    fn runtime_config_queries_work_without_declared_requirements() {
        // LTP lib/tst_cgroup.c tst_check_rt_group_sched_support queries configuration
        // from setup without declaring needs_kconfigs on sched_rr_get_interval01-03.
        let test = super::Test {
            name: "ltp/syscalls/sched_rr_get_interval01".into(),
            command: "sched_rr_get_interval01".into(),
            args: Vec::new(),
            line: "sched_rr_get_interval01".into(),
            root: true,
            budget: std::time::Duration::from_secs(60),
        };
        assert!(
            test.environment()
                .iter()
                .any(|(key, value)| key == "KCONFIG_PATH" && value == "/opt/ltp/wasixv2-kconfig")
        );
    }

    #[test]
    fn tests_run_when_the_kernel_has_every_option_they_need() {
        let needs = |kconfigs: &[&str]| serde_json::json!({ "needs_kconfigs": kconfigs });
        assert!(kconfigs_supported(&needs(&["CONFIG_PID_NS"])));
        assert!(kconfigs_supported(&needs(&["CONFIG_AIO=y"])));
        assert!(kconfigs_supported(&needs(&[
            "CONFIG_NET_NS=y",
            "CONFIG_UTS_NS"
        ])));
        assert!(kconfigs_supported(&needs(&[
            "CONFIG_USER_NS",
            "CONFIG_AIO",
            "CONFIG_PID_NS"
        ])));
        assert!(!kconfigs_supported(&needs(&[
            "CONFIG_USER_NS",
            "CONFIG_AIO",
            "CONFIG_TIME_NS"
        ])));
        assert!(!kconfigs_supported(&needs(&["CONFIG_TIME_NS=y"])));
        assert!(!kconfigs_supported(&needs(&[
            "CONFIG_NET_NS=y",
            "CONFIG_TUN"
        ])));
        assert!(!kconfigs_supported(&needs(&["CONFIG_PID_NS=m"])));
        assert_eq!(metadata_reason(&needs(&["CONFIG_PID_NS"])), None);
        assert!(metadata_reason(&needs(&["CONFIG_KVM"])).is_some());
        // A shell test's options are not read.
        let shell = script_metadata("TST_NEEDS_KCONFIGS=\"CONFIG_PID_NS\"\n");
        assert!(metadata_reason(&shell).is_some());
        assert!(kconfig_text().lines().any(|line| line == "CONFIG_PID_NS=y"));
    }

    #[test]
    fn shell_scripts_declare_requirements_in_variables() {
        let test = script_metadata(
            "#!/bin/sh\n# TST_NEEDS_DEVICE=1\nTST_NEEDS_ROOT=1\nTST_NEEDS_TMPDIR=1\n\
             TST_NEEDS_KCONFIGS=\"CONFIG_SYSCTL=y\"\nTST_MOUNT_DEVICE=0\n. tst_test.sh\n",
        );
        assert!(has(&test, "needs_root"));
        assert!(has(&test, "needs_kconfigs"));
        assert!(!has(&test, "needs_device"));
        assert!(!has(&test, "mount_device"));
        assert!(has(
            &script_metadata("setup()\n{\n\ttst_require_root\n}\n"),
            "needs_root"
        ));
        assert!(has(
            &script_metadata("do_test()\n{\n}\n. tst_net.sh\n"),
            "needs_root"
        ));
        assert!(script_reason(". tst_loader.sh\n").is_some());
        assert!(script_reason("# not . tst_loader.sh\n. tst_test.sh\n").is_none());
    }

    #[test]
    fn shell_library_results_name_their_cause() {
        assert_eq!(
            old_api_line("du01 3 TFAIL: 'du -a' failed"),
            "TFAIL: 'du -a' failed"
        );
        assert_eq!(
            old_api_line("wc01 1 TBROK: tst_rod failed"),
            "TBROK: tst_rod failed"
        );
        assert_eq!(old_api_line("foo.c:42: TFAIL: bar"), "TFAIL: bar");
    }
}
