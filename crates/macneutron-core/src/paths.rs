//! Where MacNeutron keeps its own files.

use std::path::PathBuf;

pub struct MacNeutronPaths;

impl MacNeutronPaths {
    /// `~/Library/Application Support/MacNeutron`.
    pub fn root() -> PathBuf {
        home().join("Library/Application Support/MacNeutron")
    }

    pub fn tools() -> PathBuf {
        Self::root().join("compatibilitytools.d")
    }

    pub fn games() -> PathBuf {
        Self::root().join("games")
    }

    pub fn backups() -> PathBuf {
        Self::root().join("backups")
    }
}

fn home() -> PathBuf {
    std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"))
}
