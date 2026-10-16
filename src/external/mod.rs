//! External suites: test suites maintained elsewhere, checked out as submodules under
//! `external/`, whose tests run alongside the fixtures.
//!
//! An external suite discovers its tests the way its own build does, builds each with wasixcc and
//! runs it with `wasmer run`, judging the result by its own conventions. Its test names start with
//! its name, e.g. `openposix/conformance/interfaces/sem_wait/1-1`, so `FILTER` arguments and
//! `expected-failures.txt` treat them like any other test.

mod libc_test;
mod openposix;

use std::path::{Path, PathBuf};

use anyhow::Result;

use crate::{Failure, Outcome, Suite};

/// A suite maintained elsewhere.
pub trait ExternalSuite: Sync {
    /// The first component of its test names.
    fn name(&self) -> &'static str;

    /// The submodule it lives in, relative to the repository root.
    fn submodule(&self) -> &'static str;

    /// Every test in the checked-out submodule `checkout`, in a stable order.
    fn discover(&self, checkout: &Path) -> Result<Vec<Box<dyn ExternalTest>>>;
}

/// One test of an external suite.
pub trait ExternalTest: Send + Sync {
    /// The full test name, starting with the suite's name and a `/`.
    fn name(&self) -> &str;

    /// What the harness shows next to the name, such as `buildonly`, or `""`.
    fn kind(&self) -> &'static str {
        ""
    }

    /// Whether it runs only on request (`--ignored` or `--include-ignored`), like a test the
    /// suite's own default run leaves out.
    fn opt_in(&self) -> bool {
        false
    }

    /// Builds and runs the test.
    fn run(&self, suite: &Suite) -> Result<Outcome, Failure>;
}

/// Every external suite the runner knows.
pub const SUITES: &[&dyn ExternalSuite] = &[&openposix::OpenPosix, &libc_test::LibcTest];

/// What [`discover`] found.
pub struct Discovered {
    pub tests: Vec<Box<dyn ExternalTest>>,
    /// The names of the suites whose submodule is not checked out.
    pub missing: Vec<&'static str>,
}

/// The tests of every suite that is checked out.
pub fn discover(root: &Path) -> Result<Discovered> {
    let mut found = Discovered {
        tests: Vec::new(),
        missing: Vec::new(),
    };
    for suite in SUITES {
        let checkout = root.join(suite.submodule());
        if is_checked_out(&checkout) {
            found.tests.extend(suite.discover(&checkout)?);
        } else {
            found.missing.push(suite.name());
        }
    }
    Ok(found)
}

/// An uninitialized submodule is an empty directory.
fn is_checked_out(checkout: &Path) -> bool {
    std::fs::read_dir(checkout).is_ok_and(|mut entries| entries.next().is_some())
}

/// Where an external test is built: `<build root>/<test name>`, emptied first.
pub fn build_dir(suite: &Suite, name: &str) -> Result<PathBuf, Failure> {
    let dir = suite.build_root.join(name);
    if dir.exists() {
        std::fs::remove_dir_all(&dir)
            .map_err(|e| Failure::harness(format!("removing {}: {e}", dir.display())))?;
    }
    std::fs::create_dir_all(&dir)
        .map_err(|e| Failure::harness(format!("creating {}: {e}", dir.display())))?;
    Ok(dir)
}
