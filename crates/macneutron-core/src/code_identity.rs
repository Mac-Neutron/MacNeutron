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
                // codesign prints "TeamIdentifier=not set" for ad hoc signatures, where Swift's
                // kSecCodeInfoTeamIdentifier is absent (CodeIdentity.swift:16-18).
                if !value.is_empty() && value != "not set" {
                    return Some(value.to_string());
                }
            }
        }
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_support::{make_signed_wine_app, make_temp_dir, write};

    const TRUE: &str = "/usr/bin/true";

    fn signed(dir: &Path) -> std::path::PathBuf {
        make_signed_wine_app(dir, Path::new(TRUE), "test", "1")
    }

    #[test]
    fn identity_is_40_hex_and_matches_codesign() {
        let bundle = signed(&make_temp_dir());
        let identity = CodeIdentity::of(&bundle).expect("signed");
        assert_eq!(identity.len(), 40);
        assert!(identity.chars().all(|c| "0123456789abcdef".contains(c)));
        let out = Command::new("/usr/bin/codesign").arg("-dvvv").arg(&bundle).output().unwrap();
        let text = String::from_utf8_lossy(&out.stderr).into_owned();
        let hash = text.lines().find_map(|l| l.strip_prefix("CDHash=")).map(str::to_string);
        assert_eq!(Some(identity), hash);
    }

    #[test]
    fn bundles_differing_only_in_the_loader_differ() {
        let a = signed(&make_temp_dir());
        let b = make_signed_wine_app(&make_temp_dir(), Path::new("/usr/bin/false"), "test", "1");
        assert!(CodeIdentity::of(&a).is_some());
        assert_ne!(CodeIdentity::of(&a), CodeIdentity::of(&b));
    }

    #[test]
    fn bundles_differing_only_in_info_plist_differ() {
        let a = signed(&make_temp_dir());
        let b = make_signed_wine_app(&make_temp_dir(), Path::new(TRUE), "test", "2");
        assert!(CodeIdentity::of(&a).is_some());
        assert_ne!(CodeIdentity::of(&a), CodeIdentity::of(&b));
    }

    #[test]
    fn resigning_identical_bits_keeps_the_identity() {
        let bundle = signed(&make_temp_dir());
        let before = CodeIdentity::of(&bundle).expect("signed");
        let status = Command::new("/usr/bin/codesign").args(["-s", "-", "-f"]).arg(&bundle).output().unwrap().status;
        assert!(status.success());
        assert_eq!(CodeIdentity::of(&bundle), Some(before));
    }

    #[test]
    fn unsigned_or_missing_bundle_has_no_identity() {
        let dir = make_temp_dir();
        assert_eq!(CodeIdentity::of(&dir.join("missing.app")), None);
        let unsigned = dir.join("unsigned.app");
        let mut plist = plist::Dictionary::new();
        plist.insert("CFBundleExecutable".into(), plist::Value::from("wine"));
        std::fs::create_dir_all(unsigned.join("Contents/MacOS")).unwrap();
        plist::Value::Dictionary(plist).to_file_xml(unsigned.join("Contents/Info.plist")).unwrap();
        write("#!/bin/sh\n", &unsigned.join("Contents/MacOS/wine"), true);
        assert_eq!(CodeIdentity::of(&unsigned), None);
    }

    #[test]
    fn ad_hoc_signed_apps_have_no_team() {
        assert_eq!(CodeIdentity::team(&signed(&make_temp_dir())), None);
    }
}
