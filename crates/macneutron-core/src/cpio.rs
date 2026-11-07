//! A newc ("070701") cpio archive, as RPM payloads are, and a writer of the initramfs.

use std::io::Write;
use std::path::Path;

use crate::posix::Sha256;
use crate::recipe::{Kind, RecipeEntry};
use crate::tar_writer::TarWriter;
use crate::vm_setup_error::VMSetupError;

/// Bytes read in order: a file's, or a payload's as it decompresses.
pub trait ByteSource {
    /// Up to `count` bytes; empty only at the end.
    fn read(&mut self, up_to: usize) -> Result<Vec<u8>, VMSetupError>;
}

/// Exactly `count` bytes; `.Truncated` if the source ends first.
pub fn read_exactly(source: &mut dyn ByteSource, count: usize) -> Result<Vec<u8>, VMSetupError> {
    let mut out = Vec::with_capacity(count);
    while out.len() < count {
        let chunk = source.read(count - out.len())?;
        if chunk.is_empty() {
            return Err(VMSetupError::Truncated);
        }
        out.extend_from_slice(&chunk);
    }
    Ok(out)
}

/// Reads the files of a newc cpio archive, calling `body` for each set of paths that share its bytes.
pub struct CPIOReader;

impl CPIOReader {
    pub fn read<F>(source: &mut dyn ByteSource, mut body: F) -> Result<(), VMSetupError>
    where
        F: FnMut(&[String], &mut dyn FnMut() -> Result<Option<Vec<u8>>, VMSetupError>) -> Result<(), VMSetupError>,
    {
        let mut held: std::collections::BTreeMap<u32, Vec<String>> = std::collections::BTreeMap::new();
        loop {
            let header = read_exactly(source, 110)?;
            if &header[..6] != b"070701" {
                return Err(VMSetupError::Corrupt);
            }
            let field = |i: usize| -> Result<u32, VMSetupError> {
                let slice = &header[6 + i * 8..14 + i * 8];
                let text = std::str::from_utf8(slice).map_err(|_| VMSetupError::Corrupt)?;
                u32::from_str_radix(text, 16).map_err(|_| VMSetupError::Corrupt)
            };
            let inode = field(0)?;
            let mode = field(1)?;
            let nlink = field(4)?;
            let size = field(6)? as usize;
            let name_size = field(11)? as usize;
            let raw = read_exactly(source, name_size)?;
            let name_bytes: Vec<u8> = raw.into_iter().take_while(|b| *b != 0).collect();
            let name = String::from_utf8_lossy(&name_bytes).into_owned();
            let _ = read_exactly(source, (4 - (110 + name_size) % 4) % 4)?;
            if name == "TRAILER!!!" {
                break;
            }
            let path = if let Some(rest) = name.strip_prefix('.') {
                rest.to_string()
            } else if name.starts_with('/') {
                name.clone()
            } else {
                format!("/{name}")
            };
            let mut paths = vec![path.clone()];
            if mode & 0o170000 != 0o100000 {
                paths = Vec::new();
            } else if nlink > 1 {
                let mut combined = held.get(&inode).cloned().unwrap_or_default();
                combined.push(path);
                if size == 0 && (combined.len() as u32) < nlink {
                    held.insert(inode, combined);
                    paths = Vec::new();
                } else {
                    held.remove(&inode);
                    paths = combined;
                }
            }
            emit(&paths, size, source, &mut body)?;
        }
        let held_paths: Vec<Vec<String>> = held.into_values().collect();
        for paths in held_paths {
            body(&paths, &mut || Ok(None))?;
        }
        Ok(())
    }
}

fn emit<F>(
    paths: &[String],
    size: usize,
    source: &mut dyn ByteSource,
    body: &mut F,
) -> Result<(), VMSetupError>
where
    F: FnMut(&[String], &mut dyn FnMut() -> Result<Option<Vec<u8>>, VMSetupError>) -> Result<(), VMSetupError>,
{
    let mut left = size;
    let mut next = || -> Result<Option<Vec<u8>>, VMSetupError> {
        if left == 0 {
            return Ok(None);
        }
        let chunk = source.read(left.min(1 << 20))?;
        if chunk.is_empty() {
            return Err(VMSetupError::Truncated);
        }
        left -= chunk.len();
        Ok(Some(chunk))
    };
    if !paths.is_empty() {
        body(paths, &mut next)?;
    }
    while next()?.is_some() {}
    let _ = read_exactly(source, (4 - size % 4) % 4)?;
    Ok(())
}

/// A newc cpio of recipe entries, uncompressed: the initramfs (Fedora's kernel takes one as is). Paths are written
/// relative ("usr/bin/x", the root as "."); capabilities don't apply to an initramfs.
pub struct CPIOWriter<W: Write> {
    out: W,
    inode: u32,
    written: usize,
}

impl<W: Write> CPIOWriter<W> {
    pub fn new(out: W) -> Self {
        CPIOWriter { out, inode: 0, written: 0 }
    }

    pub fn add(&mut self, entry: &RecipeEntry, contents: Option<&Path>) -> Result<(), VMSetupError> {
        self.inode += 1;
        let name = if entry.path == "/" { ".".to_string() } else { entry.path.trim_start_matches('/').to_string() };
        match &entry.kind {
            Kind::Directory => self.write(&name, 0o040000 | entry.mode as u32, Some(entry), 2, &[], (0, 0)),
            Kind::Symlink(target) => {
                self.write(&name, 0o120000 | entry.mode as u32, Some(entry), 1, target.as_bytes(), (0, 0))
            }
            Kind::Device { block, major, minor } => self.write(
                &name,
                (if *block { 0o060000 } else { 0o020000 }) | entry.mode as u32,
                Some(entry),
                1,
                &[],
                (*major, *minor),
            ),
            Kind::File { .. } => {
                let Some(contents) = contents else {
                    return Err(VMSetupError::Unsupported("a file entry with no contents".into()));
                };
                let bytes = std::fs::read(contents).map_err(|_| VMSetupError::Missing {
                    path: contents.to_string_lossy().into_owned(),
                })?;
                let mut hash = Sha256::new();
                hash.update(&bytes);
                TarWriter::<Vec<u8>>::check(entry, hash.finalize())?;
                self.write(&name, 0o100000 | entry.mode as u32, Some(entry), 1, &bytes, (0, 0))
            }
        }
    }

    pub fn finish(&mut self) -> Result<(), VMSetupError> {
        self.inode = 0;
        self.write("TRAILER!!!", 0, None, 1, &[], (0, 0))
    }

    fn write(
        &mut self,
        name: &str,
        mode: u32,
        entry: Option<&RecipeEntry>,
        nlink: u32,
        bytes: &[u8],
        rdev: (u32, u32),
    ) -> Result<(), VMSetupError> {
        let fields: [u32; 13] = [
            self.inode,
            mode,
            entry.map(|e| e.uid).unwrap_or(0),
            entry.map(|e| e.gid).unwrap_or(0),
            nlink,
            0,
            bytes.len() as u32,
            0,
            0,
            rdev.0,
            rdev.1,
            name.len() as u32 + 1,
            0,
        ];
        let mut record = Vec::new();
        record.extend_from_slice(b"070701");
        for value in fields {
            record.extend_from_slice(format!("{value:08X}").as_bytes());
        }
        record.extend_from_slice(name.as_bytes());
        record.push(0);
        let pad = (4 - (self.written + record.len()) % 4) % 4;
        record.extend(std::iter::repeat_n(0u8, pad));
        record.extend_from_slice(bytes);
        let pad = (4 - (self.written + record.len()) % 4) % 4;
        record.extend(std::iter::repeat_n(0u8, pad));
        self.out.write_all(&record).map_err(|_| VMSetupError::Corrupt)?;
        self.written += record.len();
        Ok(())
    }
}
