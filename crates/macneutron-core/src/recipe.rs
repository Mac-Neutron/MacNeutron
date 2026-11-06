//! One entry of the tree an image is built from (vm/image/recipe.py wrote it from the builder's own tree).

use crate::vm_setup_error::VMSetupError;

/// One entry of the tree an image is built from.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RecipeEntry {
    pub path: String,
    /// Permission bits only (setuid, setgid and sticky included); the kind gives the file type.
    pub mode: u16,
    pub uid: u32,
    pub gid: u32,
    pub kind: Kind,
    /// The `security.capability` xattr's value.
    pub capability: Option<Vec<u8>>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Kind {
    Directory,
    File { size: i64, sha256: String, source: Source },
    Symlink(String),
    /// A character or block device node (an initramfs's /dev/console: the kernel opens it before /init).
    Device { block: bool, major: u32, minor: u32 },
}

/// Where a file's bytes come from: a Fedora package's payload, or the parts' overlay.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Source {
    Package(String),
    Overlay,
}

/// A recipe file: one entry a line, tab-separated, `type path mode uid gid size sha256 source link caps` (`-` for a
/// field the type has no use for; a tab, newline or backslash in a path or link written `\t`, `\n`, `\\`). Types: `d`,
/// `f`, `l`, and `c` or `b` for a device, whose link field is `major:minor`.
pub struct Recipe;

impl Recipe {
    pub fn parse(tsv: &str) -> Result<Vec<RecipeEntry>, VMSetupError> {
        let mut entries = Vec::new();
        for (number, line) in tsv.split('\n').enumerate() {
            if line.is_empty() {
                continue;
            }
            let fields: Vec<&str> = line.split('\t').collect();
            match Self::entry(&fields) {
                Some(entry) => entries.push(entry),
                None => return Err(VMSetupError::BadRecipe { line: number + 1 }),
            }
        }
        Ok(entries)
    }

    fn entry(f: &[&str]) -> Option<RecipeEntry> {
        if f.len() != 10 {
            return None;
        }
        let mode = u16::from_str_radix(f[2], 8).ok()?;
        let uid = f[3].parse::<u32>().ok()?;
        let gid = f[4].parse::<u32>().ok()?;
        let kind = match f[0] {
            "d" => Kind::Directory,
            "l" => Kind::Symlink(unescaped(f[8])),
            "c" | "b" => {
                let numbers: Vec<u32> = f[8].split(':').filter_map(|s| s.parse::<u32>().ok()).collect();
                if numbers.len() != 2 {
                    return None;
                }
                Kind::Device { block: f[0] == "b", major: numbers[0], minor: numbers[1] }
            }
            "f" => {
                let size = f[5].parse::<i64>().ok()?;
                if f[6].len() != 64 {
                    return None;
                }
                let source = match f[7] {
                    "overlay" => Source::Overlay,
                    other => Source::Package(other.strip_prefix("pkg:")?.to_string()),
                };
                Kind::File { size, sha256: f[6].to_string(), source }
            }
            _ => return None,
        };
        let mut capability = None;
        if f[9] != "-" {
            let digits = f[9].as_bytes();
            if !digits.len().is_multiple_of(2) {
                return None;
            }
            let mut bytes = Vec::with_capacity(digits.len() / 2);
            for pair in digits.chunks(2) {
                let text = std::str::from_utf8(pair).ok()?;
                bytes.push(u8::from_str_radix(text, 16).ok()?);
            }
            if bytes.len() != digits.len() / 2 {
                return None;
            }
            capability = Some(bytes);
        }
        Some(RecipeEntry { path: unescaped(f[1]), mode, uid, gid, kind, capability })
    }
}

fn unescaped(text: &str) -> String {
    if !text.contains('\\') {
        return text.to_string();
    }
    let mut out = String::new();
    let mut escape = false;
    for c in text.chars() {
        if escape {
            out.push(match c {
                't' => '\t',
                'n' => '\n',
                other => other,
            });
            escape = false;
        } else if c == '\\' {
            escape = true;
        } else {
            out.push(c);
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sha() -> String {
        "ab".repeat(32)
    }

    #[test]
    fn parses_each_kind_and_the_escapes() {
        let tsv = format!(
            "d\t/\t755\t0\t0\t-\t-\t-\t-\t-\n\
             f\t/usr/bin/ping\t755\t0\t0\t10\t{}\tpkg:iputils-1.fc44.aarch64.rpm\t-\t0100000200200000\n\
             f\t/etc/odd\\tname\t644\t0\t5\t3\t{}\toverlay\t-\t-\n\
             l\t/usr/lib/libz.so.1\t777\t0\t0\t-\t-\t-\tlibz.so.1.3\\\\x\t-\n\n",
            sha(),
            sha()
        );
        let entries = Recipe::parse(&tsv).unwrap();
        assert_eq!(
            entries,
            vec![
                RecipeEntry {
                    path: "/".into(),
                    mode: 0o755,
                    uid: 0,
                    gid: 0,
                    kind: Kind::Directory,
                    capability: None,
                },
                RecipeEntry {
                    path: "/usr/bin/ping".into(),
                    mode: 0o755,
                    uid: 0,
                    gid: 0,
                    kind: Kind::File {
                        size: 10,
                        sha256: sha(),
                        source: Source::Package("iputils-1.fc44.aarch64.rpm".into()),
                    },
                    capability: Some(vec![1, 0, 0, 2, 0, 0x20, 0, 0]),
                },
                RecipeEntry {
                    path: "/etc/odd\tname".into(),
                    mode: 0o644,
                    uid: 0,
                    gid: 5,
                    kind: Kind::File { size: 3, sha256: sha(), source: Source::Overlay },
                    capability: None,
                },
                RecipeEntry {
                    path: "/usr/lib/libz.so.1".into(),
                    mode: 0o777,
                    uid: 0,
                    gid: 0,
                    kind: Kind::Symlink("libz.so.1.3\\x".into()),
                    capability: None,
                },
            ]
        );
    }

    #[test]
    fn a_short_line_is_a_bad_recipe() {
        let err = Recipe::parse("d\t/\t755\t0\t0\t-\t-\t-\t-\t-\nf\t/x\t644\n").unwrap_err();
        assert_eq!(err, VMSetupError::BadRecipe { line: 2 });
    }

    #[test]
    fn device_nodes_carry_their_numbers() {
        let entries = Recipe::parse("c\t/dev/console\t600\t0\t5\t-\t-\t-\t5:1\t-\n").unwrap();
        assert_eq!(
            entries,
            vec![RecipeEntry {
                path: "/dev/console".into(),
                mode: 0o600,
                uid: 0,
                gid: 5,
                kind: Kind::Device { block: false, major: 5, minor: 1 },
                capability: None,
            }]
        );
    }
}
