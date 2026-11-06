//! A signed bundle's identity: its main executable's CDHash, as `codesign -dvvv` prints it (`CDHash=`).
//!
//! The Swift original read `kSecCodeInfoUnique` through the Security framework. The CDHash is exactly what
//! `codesign -dvvv` prints, so we ask the tool MacNeutron already requires for signing, keeping this crate
//! free of a Security/CF binding.

use std::path::Path;
use std::process::Command;

pub struct CodeIdentity;

impl CodeIdentity {
    /// 40 lowercase hex digits (the CDHash), or None when `bundle` is missing or unsigned.
    pub fn of(bundle: &Path) -> Option<String> {
        Self::run(bundle, "CDHash")
    }

    /// The Developer ID team that signed `bundle`; None when it is ad hoc signed, unsigned or missing.
    pub fn team(bundle: &Path) -> Option<String> {
        Self::run(bundle, "TeamIdentifier")
    }

    fn run(bundle: &Path, field: &str) -> Option<String> {
        let output = Command::new("/usr/bin/codesign")
            .arg("-dvvv")
            .arg(bundle)
            .output()
            .ok()?;
        // codesign writes its detail lines to stderr.
        let text = String::from_utf8_lossy(&output.stderr);
        for line in text.lines() {
            if let Some(rest) = line.strip_prefix(&format!("{field}=")) {
                let value = rest.trim();
                if !value.is_empty() {
                    return Some(value.to_string());
                }
            }
        }
        None
    }
}
