//! A POSIX pax tar of recipe entries, for `mkfs.erofs --tar=f`: a pax record for a path or link over 100 bytes, for
//! owners or sizes too big for ustar's octal fields, and for a file capability (`SCHILY.xattr.security.capability`).
//! Times are 0: images compare by content. Paths are written relative ("usr/bin/x", the root as "./").

use std::io::Write;
use std::path::Path;

use crate::posix::Sha256;
use crate::recipe::{Kind, RecipeEntry};
use crate::vm_setup_error::VMSetupError;

pub struct TarWriter<W: Write> {
    out: W,
}

impl<W: Write> TarWriter<W> {
    pub fn new(out: W) -> Self {
        TarWriter { out }
    }

    /// `contents` for a file: its bytes, whose size goes in the header.
    pub fn add(&mut self, entry: &RecipeEntry, contents: Option<&Path>) -> Result<(), VMSetupError> {
        let mut name = if entry.path == "/" {
            "./".to_string()
        } else {
            entry.path.trim_start_matches('/').to_string()
        };
        let mut type_byte = b'0';
        let mut link = String::new();
        let mut size: i64 = 0;
        let mut device: (u32, u32) = (0, 0);
        match &entry.kind {
            Kind::Directory => {
                type_byte = b'5';
                if !name.ends_with('/') {
                    name.push('/');
                }
            }
            Kind::Symlink(target) => {
                type_byte = b'2';
                link = target.clone();
            }
            Kind::Device { block, major, minor } => {
                type_byte = if *block { b'4' } else { b'3' };
                device = (*major, *minor);
            }
            Kind::File { .. } => {
                let Some(contents) = contents else {
                    return Err(VMSetupError::Unsupported("a file entry with no contents".into()));
                };
                size = std::fs::metadata(contents).map(|m| m.len() as i64).unwrap_or(0);
            }
        }
        let mut records: Vec<(String, Vec<u8>)> = Vec::new();
        if name.len() > 100 {
            records.push(("path".into(), name.as_bytes().to_vec()));
        }
        if link.len() > 100 {
            records.push(("linkpath".into(), link.as_bytes().to_vec()));
        }
        if entry.uid > 0o7777777 {
            records.push(("uid".into(), entry.uid.to_string().into_bytes()));
        }
        if entry.gid > 0o7777777 {
            records.push(("gid".into(), entry.gid.to_string().into_bytes()));
        }
        if size > 0o77777777777 {
            records.push(("size".into(), size.to_string().into_bytes()));
        }
        if let Some(capability) = &entry.capability {
            records.push(("SCHILY.xattr.security.capability".into(), capability.clone()));
        }
        if !records.is_empty() {
            let mut body = Vec::new();
            for item in &records {
                body.extend_from_slice(&record(item));
            }
            let pax_name = format!("PaxHeaders/{}", tail(&name, 80));
            let header = header(&pax_name, 0o644, 0, 0, body.len() as i64, b'x', "", (0, 0));
            self.write(&header)?;
            let padded = padded(&body);
            self.write(&padded)?;
        }
        let header = header(&name, entry.mode, entry.uid, entry.gid, size, type_byte, &link, device);
        self.write(&header)?;
        if let (Some(contents), Kind::File { .. }) = (contents, &entry.kind) {
            use std::io::Read as _;
            let mut file = std::fs::File::open(contents).map_err(|_| posix_err(contents))?;
            let mut written: i64 = 0;
            let mut hash = Sha256::new();
            let mut buffer = vec![0u8; 1 << 20];
            loop {
                let read = file.read(&mut buffer).map_err(|_| posix_err(contents))?;
                if read == 0 {
                    break;
                }
                self.write(&buffer[..read])?;
                hash.update(&buffer[..read]);
                written += read as i64;
            }
            let padding = vec![0u8; ((512 - written % 512) % 512) as usize];
            self.write(&padding)?;
            Self::check(entry, hash.finalize())?;
        }
        Ok(())
    }

    pub fn finish(&mut self) -> Result<(), VMSetupError> {
        self.write(&vec![0u8; 1024])
    }

    /// A recipe file's bytes are the ones it names (a store file left by an earlier run could have changed since).
    pub fn check(entry: &RecipeEntry, digest: String) -> Result<(), VMSetupError> {
        match &entry.kind {
            Kind::File { sha256, .. } if sha256.len() == 64 => {
                if digest == *sha256 {
                    Ok(())
                } else {
                    Err(VMSetupError::Mismatch { file: entry.path.clone() })
                }
            }
            _ => Ok(()),
        }
    }

    fn write(&mut self, data: &[u8]) -> Result<(), VMSetupError> {
        self.out.write_all(data).map_err(|e| VMSetupError::Tool(format!("write: {e}"), -1))
    }
}

/// "<length> <key>=<value>\n", the length counting itself.
fn record(item: &(String, Vec<u8>)) -> Vec<u8> {
    let mut rest = Vec::new();
    rest.push(b' ');
    rest.extend_from_slice(item.0.as_bytes());
    rest.push(b'=');
    rest.extend_from_slice(&item.1);
    rest.push(b'\n');
    let mut length = rest.len() + 1;
    while length.to_string().len() + rest.len() != length {
        length = length.to_string().len() + rest.len();
    }
    let mut out = length.to_string().into_bytes();
    out.extend_from_slice(&rest);
    out
}

fn padded(data: &[u8]) -> Vec<u8> {
    let mut out = data.to_vec();
    out.extend(std::iter::repeat_n(0u8, (512 - data.len() % 512) % 512));
    out
}

fn tail(text: &str, count: usize) -> String {
    let chars: Vec<char> = text.chars().collect();
    if chars.len() <= count {
        text.to_string()
    } else {
        chars[chars.len() - count..].iter().collect()
    }
}

fn octal(value: i64, at: usize, width: usize, h: &mut [u8]) {
    let digits = format!("{:o}", value);
    let pad = width.saturating_sub(1).saturating_sub(digits.len());
    let text = format!("{}{}", "0".repeat(pad), digits);
    put(&text, at, width - 1, h);
}

fn put(text: &str, at: usize, width: usize, h: &mut [u8]) {
    let bytes = text.as_bytes();
    let count = bytes.len().min(width);
    h[at..at + count].copy_from_slice(&bytes[..count]);
}

#[allow(clippy::too_many_arguments)]
fn header(
    name: &str,
    mode: u16,
    uid: u32,
    gid: u32,
    size: i64,
    type_byte: u8,
    link: &str,
    device: (u32, u32),
) -> Vec<u8> {
    let mut h = vec![0u8; 512];
    put(name, 0, 100, &mut h);
    octal(mode as i64, 100, 8, &mut h);
    octal(uid.min(0o7777777) as i64, 108, 8, &mut h);
    octal(gid.min(0o7777777) as i64, 116, 8, &mut h);
    octal(size.min(0o77777777777), 124, 12, &mut h);
    octal(0, 136, 12, &mut h);
    h[156] = type_byte;
    put(link, 157, 100, &mut h);
    put("ustar", 257, 6, &mut h);
    put("00", 263, 2, &mut h);
    octal(device.0 as i64, 329, 8, &mut h);
    octal(device.1 as i64, 337, 8, &mut h);
    put("        ", 148, 8, &mut h);
    let sum: i64 = h.iter().map(|b| *b as i64).sum();
    octal(sum, 148, 7, &mut h);
    h[155] = b' ';
    h
}

fn posix_err(path: &Path) -> VMSetupError {
    VMSetupError::Missing { path: path.to_string_lossy().into_owned() }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::recipe::{RecipeEntry, Source};
    use std::path::PathBuf;
    use std::process::Command;

    fn temp(name: &str) -> PathBuf {
        crate::rpm_fixture::temp(name)
    }

    fn long_path() -> String {
        format!("/usr/share/{}file.txt", "deep/".repeat(38))
    }

    fn sample<F: FnMut(&RecipeEntry, Option<&Path>)>(mut write: F) -> PathBuf {
        let dir = temp("sample");
        let contents = dir.join("hello");
        std::fs::write(&contents, b"hello\n").unwrap();
        let file = |path: &str, mode: u16, cap: Option<Vec<u8>>| RecipeEntry {
            path: path.into(),
            mode,
            uid: 0,
            gid: 0,
            kind: Kind::File { size: 6, sha256: "-".into(), source: Source::Overlay },
            capability: cap,
        };
        write(&RecipeEntry { path: "/".into(), mode: 0o755, uid: 0, gid: 0, kind: Kind::Directory, capability: None }, None);
        write(&RecipeEntry { path: "/usr".into(), mode: 0o755, uid: 0, gid: 0, kind: Kind::Directory, capability: None }, None);
        write(&file("/usr/hello", 0o644, None), Some(&contents));
        write(&file(&long_path(), 0o600, None), Some(&contents));
        write(
            &file("/usr/ping", 0o4755, Some(vec![1, 0, 0, 2, 0, 0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0])),
            Some(&contents),
        );
        write(
            &RecipeEntry { path: "/usr/link".into(), mode: 0o777, uid: 0, gid: 0, kind: Kind::Symlink("hello".into()), capability: None },
            None,
        );
        dir
    }

    fn listing(archive: &Path) -> String {
        let out = Command::new("/usr/bin/tar").arg("-tvf").arg(archive).output().unwrap();
        assert!(out.status.success());
        String::from_utf8_lossy(&out.stdout).into_owned()
    }

    #[test]
    fn tar_round_trips_through_bsdtar() {
        let dir = temp("tar");
        let archive = dir.join("t.tar");
        let file = std::fs::File::create(&archive).unwrap();
        let mut tar = TarWriter::new(file);
        sample(|entry, contents| tar.add(entry, contents).unwrap());
        tar.finish().unwrap();
        drop(tar);
        let text = listing(&archive);
        assert!(text.contains("-rw-r--r--  0 0      0           6 "), "{text}");
        assert!(text.contains(" usr/hello\n"), "{text}");
        assert!(text.contains(&long_path()[1..]), "{text}");
        assert!(text.contains("-rwsr-xr-x"), "{text}");
        assert!(text.contains("usr/link -> hello"), "{text}");
    }

    #[test]
    fn the_capability_is_a_pax_record() {
        let dir = temp("cap");
        let archive = dir.join("t.tar");
        let file = std::fs::File::create(&archive).unwrap();
        let mut tar = TarWriter::new(file);
        sample(|entry, contents| tar.add(entry, contents).unwrap());
        tar.finish().unwrap();
        drop(tar);
        let bytes = std::fs::read(&archive).unwrap();
        let mut record = b"SCHILY.xattr.security.capability=".to_vec();
        record.extend_from_slice(&[1, 0, 0, 2, 0, 0x20]);
        assert!(find(&bytes, &record).is_some());
    }

    #[test]
    fn a_device_node_is_written_with_its_numbers() {
        let console = RecipeEntry { path: "/dev/console".into(), mode: 0o600, uid: 0, gid: 5, kind: Kind::Device { block: false, major: 5, minor: 1 }, capability: None };
        let dir = temp("dev");
        let archive = dir.join("d.tar");
        let file = std::fs::File::create(&archive).unwrap();
        let mut tar = TarWriter::new(file);
        tar.add(&console, None).unwrap();
        tar.finish().unwrap();
        drop(tar);
        let text = listing(&archive);
        assert!(text.starts_with("crw-------"), "{text}");
        assert!(text.contains("5,1") || text.contains("5, 1"), "{text}");
        let _ = console;
    }

    fn find(haystack: &[u8], needle: &[u8]) -> Option<usize> {
        haystack.windows(needle.len()).position(|w| w == needle)
    }

    #[test]
    fn cpio_round_trips_through_bsdtar() {
        let dir = temp("cpio");
        let archive = dir.join("t.cpio");
        let file = std::fs::File::create(&archive).unwrap();
        let mut cpio = crate::cpio::CPIOWriter::new(file);
        sample(|entry, contents| cpio.add(entry, contents).unwrap());
        cpio.finish().unwrap();
        drop(cpio);
        let text = listing(&archive);
        assert!(text.contains(" usr/hello\n"), "{text}");
        assert!(text.contains("usr/link -> hello"), "{text}");
        assert_eq!(&std::fs::read(&archive).unwrap()[..6], b"070701");
    }

    #[test]
    fn a_cpio_device_node_is_written_with_its_numbers() {
        let console = RecipeEntry { path: "/dev/console".into(), mode: 0o600, uid: 0, gid: 5, kind: Kind::Device { block: false, major: 5, minor: 1 }, capability: None };
        let archive = temp("cpiodev").join("d.cpio");
        let mut cpio = crate::cpio::CPIOWriter::new(std::fs::File::create(&archive).unwrap());
        cpio.add(&console, None).unwrap();
        cpio.finish().unwrap();
        drop(cpio);
        let text = listing(&archive);
        assert!(text.starts_with("crw-------"), "{text}");
        assert!(text.contains("5,1") || text.contains("5, 1"), "{text}");
    }
}
