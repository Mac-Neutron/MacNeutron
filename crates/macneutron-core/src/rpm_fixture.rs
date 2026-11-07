//! Writes a small RPM the way rpm lays one out, for the reader's and the assembler's tests: the lead, an empty
//! signature header (padded to 8 bytes), a header with the tags the reader reads, and an xz-compressed newc cpio
//! payload. Files sharing an inode are hardlinks: newc carries their bytes on the last one only.

use std::collections::BTreeMap;
use std::io::Write as _;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

pub struct File {
    pub path: String,
    pub bytes: Vec<u8>,
    pub inode: u32,
}

fn be(value: u32) -> Vec<u8> {
    value.to_be_bytes().to_vec()
}

fn header(tags: &[(u32, String)]) -> Vec<u8> {
    let mut index = Vec::new();
    let mut store = Vec::new();
    for (tag, value) in tags {
        for field in [*tag, 6, store.len() as u32, 1] {
            index.extend_from_slice(&be(field));
        }
        store.extend_from_slice(value.as_bytes());
        store.push(0);
    }
    let mut out = vec![0x8e, 0xad, 0xe8, 0x01, 0, 0, 0, 0];
    out.extend_from_slice(&be(tags.len() as u32));
    out.extend_from_slice(&be(store.len() as u32));
    out.extend_from_slice(&index);
    out.extend_from_slice(&store);
    out
}

fn entry(cpio: &mut Vec<u8>, name: &str, inode: u32, mode: u32, nlink: u32, bytes: &[u8]) {
    let fields = [inode, mode, 0, 0, nlink, 0, bytes.len() as u32, 0, 0, 0, 0, name.len() as u32 + 1, 0];
    let mut text = String::from("070701");
    for field in fields {
        text.push_str(&format!("{field:08X}"));
    }
    cpio.extend_from_slice(text.as_bytes());
    cpio.extend_from_slice(name.as_bytes());
    cpio.push(0);
    let pad = (4 - cpio.len() % 4) % 4;
    cpio.extend(std::iter::repeat_n(0u8, pad));
    cpio.extend_from_slice(bytes);
    let pad = (4 - cpio.len() % 4) % 4;
    cpio.extend(std::iter::repeat_n(0u8, pad));
}

/// xz, as `.lzma` from Apple's Compression: the payload decoder only decodes, so compress through the xz tool.
fn xz(bytes: &[u8]) -> Vec<u8> {
    let mut child = Command::new("/opt/homebrew/bin/xz")
        .args(["--format=xz", "-c"])
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .spawn()
        .expect("xz");
    child.stdin.as_mut().unwrap().write_all(bytes).unwrap();
    child.stdin.take();
    let out = child.wait_with_output().unwrap();
    assert!(out.status.success());
    out.stdout
}

/// The RPM's bytes, without a path (callers open or write them where they like).
pub fn rpm_bytes(name: &str, version: &str, release: &str, arch: &str, payload_format: &str, files: &[File]) -> Vec<u8> {
    let mut rpm = vec![0u8; 96];
    rpm[..4].copy_from_slice(&[0xed, 0xab, 0xee, 0xdb]);
    rpm.extend_from_slice(&header(&[]));
    rpm.extend_from_slice(&header(&[
        (1000, name.to_string()),
        (1001, version.to_string()),
        (1002, release.to_string()),
        (1022, arch.to_string()),
        (1124, payload_format.to_string()),
        (1125, "xz".to_string()),
    ]));
    let mut cpio = Vec::new();
    let mut links: BTreeMap<u32, u32> = BTreeMap::new();
    for file in files {
        *links.entry(file.inode).or_insert(0) += 1;
    }
    let mut seen: BTreeMap<u32, u32> = BTreeMap::new();
    for file in files {
        let count = seen.entry(file.inode).or_insert(0);
        *count += 1;
        let last = *count == links[&file.inode];
        let bytes = if last { file.bytes.as_slice() } else { &[] };
        entry(&mut cpio, &format!(".{}", file.path), file.inode, 0o100644, links[&file.inode], bytes);
    }
    entry(&mut cpio, "TRAILER!!!", 0, 0, 1, &[]);
    rpm.extend_from_slice(&xz(&cpio));
    rpm
}

/// Writes `bytes` to `path`.
pub fn write(path: &Path, bytes: &[u8]) {
    std::fs::write(path, bytes).unwrap();
}

/// A fresh empty directory for one test. Unique per call (pid + an atomic counter), so tests running in parallel
/// never share, and remove, each other's directory.
pub fn temp(name: &str) -> PathBuf {
    use std::sync::atomic::{AtomicU64, Ordering};
    static SEQ: AtomicU64 = AtomicU64::new(0);
    let dir = std::env::temp_dir().join(format!(
        "mn-fixture-{}-{}-{}",
        std::process::id(),
        SEQ.fetch_add(1, Ordering::Relaxed),
        name
    ));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    dir
}
