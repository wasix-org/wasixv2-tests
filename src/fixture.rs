//! Discovering fixtures under `suite/` and parsing their directives into runnable configurations.
//!
//! The directive syntax is the one wasmer's WASIX v1 suite uses, so fixtures can move between the
//! two unchanged. Directives that only mean something to that suite (engines, filesystems, libc
//! versions, Rust toolchains) are accepted and ignored.

use std::collections::HashMap;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result, anyhow, bail, ensure};
use walkdir::WalkDir;

/// The file a fixture's directives live in, which also decides how it is built.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Source {
    C(String),
    Cpp(String),
    /// A shell script that builds `main`; `build.sh` is the default name.
    Shell(String),
    /// A single-file Rust program; each `.rs` file is its own test.
    Rust(String),
    Cargo,
}

impl Source {
    pub fn filename(&self) -> &str {
        match self {
            Self::C(f) | Self::Cpp(f) | Self::Shell(f) | Self::Rust(f) => f,
            Self::Cargo => "Cargo.toml",
        }
    }

    /// The extra name component a non-default source adds to the test name.
    fn name_component(&self) -> Option<String> {
        match self {
            Self::C(f) | Self::Cpp(f) if matches!(f.as_str(), "main.c" | "main.cpp") => None,
            Self::Shell(f) if f == "build.sh" => None,
            Self::Cargo => None,
            Self::C(f) | Self::Cpp(f) | Self::Shell(f) | Self::Rust(f) => Some(
                Path::new(f)
                    .file_stem()
                    .expect("source files have a stem")
                    .to_string_lossy()
                    .into_owned(),
            ),
        }
    }

    fn directive<'a>(&self, line: &'a str) -> Option<&'a str> {
        let line = line.trim();
        match self {
            Self::Shell(_) => line.strip_prefix("##"),
            Self::Cargo => line
                .strip_prefix("##")
                .map(str::trim)
                .filter(|rest| rest.contains(':')),
            Self::C(_) | Self::Cpp(_) | Self::Rust(_) => line.strip_prefix("//#"),
        }
    }
}

/// A host directory mapped into the guest.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Host {
    /// Relative paths are resolved against the test's build directory.
    Path(PathBuf),
    /// `$temp`: a fresh temporary directory.
    Temp,
}

/// Explicit browser embedding inputs for the existing v1 command annotation API.
#[derive(Clone, Debug, PartialEq, serde::Serialize, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct BrowserCommandDefaults {
    pub name: String,
    pub argv0: Option<String>,
    pub args: Vec<String>,
    pub env: Vec<String>,
}

/// One runnable configuration of a fixture.
#[derive(Debug, Clone, PartialEq)]
pub struct Config {
    pub source: Source,
    pub src_dir: PathBuf,
    /// The fixture's path under `suite/`.
    pub fixture: String,
    pub name: String,
    is_abstract: bool,
    pub args: Vec<String>,
    pub build_env: Vec<(String, String)>,
    pub env: Vec<(String, String)>,
    pub stdin: Option<Vec<u8>>,
    pub expected_stdout: Vec<String>,
    pub expected_stderr: Vec<String>,
    pub must_fail: bool,
    pub expected_exit_code: i32,
    pub ignored: Option<String>,
    pub unix_only: bool,
    pub ignore_macos: bool,
    /// `SkipEngine` reasons by lowercase engine name.
    pub skipped_engines: Vec<(String, String)>,
    pub mapped_directories: Vec<(Host, String)>,
    pub current_directory: Option<String>,
    pub user: Option<String>,
    pub prefilled_files: Vec<(PathBuf, String)>,
    pub expected_files: Vec<(PathBuf, String)>,
    pub program_name: Option<String>,
    /// Native compatibility directives that browser export must not silently discard.
    pub browser_unsupported: Vec<String>,
    pub browser_console: bool,
    pub browser_http: bool,
    pub browser_network: Option<String>,
    pub browser_command_defaults: Vec<BrowserCommandDefaults>,
}

impl Config {
    fn new(source: Source, src_dir: PathBuf, fixture: String) -> Self {
        Self {
            source,
            src_dir,
            fixture,
            name: "default".to_owned(),
            is_abstract: false,
            args: Vec::new(),
            build_env: Vec::new(),
            env: Vec::new(),
            stdin: None,
            expected_stdout: Vec::new(),
            expected_stderr: Vec::new(),
            must_fail: false,
            expected_exit_code: 0,
            ignored: None,
            unix_only: false,
            ignore_macos: false,
            skipped_engines: Vec::new(),
            mapped_directories: Vec::new(),
            current_directory: None,
            user: None,
            prefilled_files: Vec::new(),
            expected_files: Vec::new(),
            program_name: None,
            browser_unsupported: Vec::new(),
            browser_console: false,
            browser_http: false,
            browser_network: None,
            browser_command_defaults: Vec::new(),
        }
    }

    /// `<fixture>[/<source>]/<config>`, e.g. `basic/helloworld/default`.
    pub fn test_name(&self) -> String {
        let mut parts = vec![self.fixture.clone()];
        parts.extend(self.source.name_component());
        parts.push(self.name.clone());
        parts.join("/")
    }
}

/// Every configuration of every fixture under `suite`, in a stable order.
pub fn discover(suite: &Path) -> Result<Vec<Config>> {
    let mut dirs: Vec<PathBuf> = WalkDir::new(suite)
        .min_depth(1)
        .into_iter()
        .filter_map(Result::ok)
        .filter(|e| e.file_type().is_dir() && has_primary_source(e.path()))
        .map(|e| e.into_path())
        .collect();
    dirs.sort();

    let mut configs = Vec::new();
    for dir in dirs {
        let fixture = dir
            .strip_prefix(suite)?
            .to_string_lossy()
            .replace('\\', "/");
        for source in primary_sources(&dir)? {
            let default = Config::new(source, dir.clone(), fixture.clone());
            configs.extend(
                parse_configs(&default).with_context(|| format!("in fixture {}", dir.display()))?,
            );
        }
    }
    Ok(configs)
}

fn files_with_extension(dir: &Path, ext: &str) -> Vec<String> {
    let mut files: Vec<String> = std::fs::read_dir(dir)
        .into_iter()
        .flatten()
        .filter_map(Result::ok)
        .filter(|e| e.file_type().is_ok_and(|t| t.is_file()))
        .map(|e| e.path())
        .filter(|p| p.extension().and_then(|e| e.to_str()) == Some(ext))
        .map(|p| p.file_name().unwrap().to_string_lossy().into_owned())
        .collect();
    files.sort();
    files
}

fn has_primary_source(dir: &Path) -> bool {
    ["main.c", "main.cpp", "Cargo.toml"]
        .iter()
        .any(|f| dir.join(f).is_file())
        || !files_with_extension(dir, "sh").is_empty()
        || !files_with_extension(dir, "rs").is_empty()
}

/// Same precedence as the v1 suite: shell scripts, then a Cargo project, then `main.c` or
/// `main.cpp`, then each `.rs` file.
fn primary_sources(dir: &Path) -> Result<Vec<Source>> {
    let shell = files_with_extension(dir, "sh");
    if !shell.is_empty() {
        return Ok(shell.into_iter().map(Source::Shell).collect());
    }
    if dir.join("Cargo.toml").is_file() {
        return Ok(vec![Source::Cargo]);
    }
    if dir.join("main.c").is_file() {
        return Ok(vec![Source::C("main.c".to_owned())]);
    }
    if dir.join("main.cpp").is_file() {
        return Ok(vec![Source::Cpp("main.cpp".to_owned())]);
    }
    let rust = files_with_extension(dir, "rs");
    ensure!(!rust.is_empty(), "{} has no primary source", dir.display());
    Ok(rust.into_iter().map(Source::Rust).collect())
}

struct Parser<'a> {
    default: &'a Config,
    current: Config,
    done: Vec<Config>,
    by_name: HashMap<String, usize>,
    build_env: Vec<(String, String)>,
    browser_unsupported: Vec<String>,
}

fn parse_configs(default: &Config) -> Result<Vec<Config>> {
    let path = default.src_dir.join(default.source.filename());
    let text =
        std::fs::read_to_string(&path).with_context(|| format!("reading {}", path.display()))?;
    let mut p = Parser {
        default,
        current: default.clone(),
        done: Vec::new(),
        by_name: HashMap::new(),
        build_env: Vec::new(),
        browser_unsupported: Vec::new(),
    };
    for (i, line) in text.lines().enumerate() {
        if let Some(rest) = default.source.directive(line) {
            p.directive(rest)
                .with_context(|| format!("{}:{}", path.display(), i + 1))?;
        }
    }
    let Parser {
        current,
        mut done,
        build_env,
        browser_unsupported,
        ..
    } = p;
    done.push(current);
    for config in &mut done {
        config.build_env = build_env.clone();
        config.browser_unsupported = browser_unsupported.clone();
    }
    done.retain(|c| !c.is_abstract);
    ensure!(!done.is_empty(), "no runnable Config");
    Ok(done)
}

fn relative(arg: &str, directive: &str) -> Result<PathBuf> {
    let path = PathBuf::from(arg);
    ensure!(path.is_relative(), "{directive} must be relative: {path:?}");
    Ok(path)
}

fn key_value(arg: &str, directive: &str) -> Result<(String, String)> {
    let (key, value) = arg
        .split_once('=')
        .ok_or_else(|| anyhow!("{directive} needs KEY=VALUE"))?;
    let key = key.trim();
    ensure!(!key.is_empty(), "{directive} key must not be empty");
    Ok((key.to_owned(), value.trim().to_owned()))
}

impl Parser<'_> {
    fn directive(&mut self, rest: &str) -> Result<()> {
        let (directive, arg) = rest.split_once(':').context("missing `:`")?;
        let arg = arg.trim();
        let src_dir = self.current.src_dir.clone();
        let c = &mut self.current;
        match directive.trim() {
            "Config" | "AbstractConfig" => {
                if self.current != *self.default {
                    self.by_name
                        .insert(self.current.name.clone(), self.done.len());
                    self.done.push(self.current.clone());
                }
                let name = match arg.split_once(':') {
                    Some((name, parent)) => {
                        let index = self
                            .by_name
                            .get(parent)
                            .ok_or_else(|| anyhow!("`{name}` inherits unknown `{parent}`"))?;
                        self.current = self.done[*index].clone();
                        name
                    }
                    None => {
                        self.current = self.default.clone();
                        arg
                    }
                };
                ensure!(
                    !self.by_name.contains_key(name),
                    "duplicate config `{name}`"
                );
                self.current.is_abstract = directive.trim() == "AbstractConfig";
                self.current.name = name.to_owned();
            }
            "AbstractConfigFile" => {
                let path = src_dir.join(relative(arg, "AbstractConfigFile")?);
                let text = std::fs::read_to_string(&path)
                    .with_context(|| format!("reading {}", path.display()))?;
                for (i, line) in text.lines().enumerate() {
                    let line = line.trim();
                    if line.is_empty() || line.starts_with('#') {
                        continue;
                    }
                    self.directive(line)
                        .with_context(|| format!("{}:{}", path.display(), i + 1))?;
                }
            }
            "Args" => {
                c.args = arg
                    .split(' ')
                    .filter(|s| !s.is_empty())
                    .map(str::to_owned)
                    .collect()
            }
            "ExpectedStdout" => c.expected_stdout.push(arg.to_owned()),
            "ExpectedStderr" => c.expected_stderr.push(arg.to_owned()),
            "ExpectedStdoutFile" | "ExpectedStderrFile" => {
                let path = src_dir.join(relative(arg, directive)?);
                let text = std::fs::read_to_string(&path)
                    .with_context(|| format!("reading {}", path.display()))?;
                let lines = text.trim().lines().map(str::to_owned);
                if directive == "ExpectedStdoutFile" {
                    c.expected_stdout.extend(lines);
                } else {
                    c.expected_stderr.extend(lines);
                }
            }
            "BuildEnv" => self.build_env.push(key_value(arg, "BuildEnv")?),
            "BrowserConsole" => c.browser_console = arg.parse()?,
            "BrowserHttp" => c.browser_http = arg.parse()?,
            "BrowserNetwork" => {
                ensure!(
                    [
                        "classic",
                        "fidelity",
                        "absent",
                        "refused",
                        "disconnect",
                        "overflow",
                        "ingress",
                        "peer"
                    ]
                    .contains(&arg),
                    "unknown browser network profile"
                );
                c.browser_network = Some(arg.to_owned());
            }
            "BrowserCommandDefaults" => c.browser_command_defaults = serde_json::from_str(arg)?,
            "Env" => c.env.push(key_value(arg, "Env")?),
            "MustFail" => c.must_fail = arg.parse()?,
            "ExpectedExitCode" => c.expected_exit_code = arg.parse()?,
            "Ignored" => c.ignored = Some(arg.to_owned()),
            "SkipEngine" => {
                let (engine, reason) = arg
                    .split_once(':')
                    .ok_or_else(|| anyhow!("SkipEngine needs ENGINE:REASON"))?;
                c.skipped_engines
                    .push((engine.trim().to_lowercase(), reason.trim().to_owned()));
            }
            "UnixOnly" => c.unix_only = arg.parse()?,
            "IgnoreMacOS" => c.ignore_macos = arg.parse()?,
            "MappedDirectory" => {
                let (host, guest) = arg
                    .split_once(':')
                    .ok_or_else(|| anyhow!("MappedDirectory needs HOST:GUEST"))?;
                let host = match host {
                    "$temp" => Host::Temp,
                    host => Host::Path(PathBuf::from(host)),
                };
                c.mapped_directories.push((host, guest.to_owned()));
            }
            "CurrentDirectory" => c.current_directory = Some(arg.to_owned()),
            "User" => c.user = Some(arg.to_owned()),
            "PrefilledFile" | "ExpectedFile" => {
                let (path, contents) = arg
                    .split_once(':')
                    .ok_or_else(|| anyhow!("{directive} needs PATH:CONTENTS"))?;
                let entry = (relative(path, directive)?, contents.to_owned());
                if directive == "PrefilledFile" {
                    c.prefilled_files.push(entry);
                } else {
                    c.expected_files.push(entry);
                }
            }
            "Stdin" => c.stdin = Some(arg.as_bytes().to_vec()),
            "StdinFile" => {
                let path = src_dir.join(relative(arg, "StdinFile")?);
                c.stdin = Some(
                    std::fs::read(&path).with_context(|| format!("reading {}", path.display()))?,
                );
            }
            "ProgramName" => c.program_name = Some(arg.to_owned()),
            // Only meaningful to wasmer's WASIX v1 suite.
            "MinimalLibc" | "FileSystems" | "Toolchains" | "DefaultMappedDirectories" => {
                self.browser_unsupported.push(directive.trim().to_owned());
            }
            other => bail!("unknown directive `{other}`"),
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn compatibility_directives_do_not_create_an_extra_native_configuration() {
        let dir = tempfile::tempdir().unwrap();
        std::fs::write(
            dir.path().join("main.c"),
            "//#FileSystems: memfs\n//#Config: named\n//#ExpectedStdout: ok\n",
        )
        .unwrap();
        let config = Config::new(
            Source::C("main.c".into()),
            dir.path().into(),
            "compat".into(),
        );
        let parsed = parse_configs(&config).unwrap();
        assert_eq!(parsed.len(), 1);
        assert_eq!(parsed[0].name, "named");
        assert_eq!(parsed[0].browser_unsupported, ["FileSystems"]);
    }

    #[test]
    fn permission_fixtures_can_request_root() {
        let dir = tempfile::tempdir().unwrap();
        std::fs::write(dir.path().join("main.c"), "//#User: 0:0\n").unwrap();
        let config = Config::new(
            Source::C("main.c".into()),
            dir.path().into(),
            "perms".into(),
        );
        let parsed = parse_configs(&config).unwrap();
        assert_eq!(parsed.len(), 1);
        assert_eq!(parsed[0].user.as_deref(), Some("0:0"));
        assert_eq!(config.user, None);
    }
}
