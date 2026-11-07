//! An RPM's header and payload files, streamed: the lead (96 bytes), the signature header padded to 8 bytes, the
//! header (big-endian index and store), then the compressed newc cpio payload. Nothing here checks signatures: the
//! release checked Fedora's when it made the manifest, and the setup checks each package's SHA-256 against it.

use std::fs::File;
use std::io::Read;
use std::path::Path;

use crate::cpio::{read_exactly, ByteSource, CPIOReader};
use crate::payload_decoder::PayloadDecoder;
use crate::vm_setup_error::VMSetupError;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RPMHeader {
    pub name: String,
    pub version: String,
    pub release: String,
    pub arch: String,
    pub payload_compressor: String,
}

pub struct RPMReader;

impl RPMReader {
    /// Calls `body` for each file in the payload, in archive order. `next` yields the file's bytes in order.
    pub fn read_files<F>(rpm: &Path, mut body: F) -> Result<(), VMSetupError>
    where
        F: FnMut(&RPMHeader, &[String], &mut dyn FnMut() -> Result<Option<Vec<u8>>, VMSetupError>) -> Result<(), VMSetupError>,
    {
        let mut file = FileSource::open(rpm)?;
        let _lead = read_exactly(&mut file, 96)?;
        let (sig_entries, sig_store) = intro(&mut file)?;
        let size = 16 + 16 * sig_entries + sig_store;
        let _sig_pad = read_exactly(&mut file, size - 16 + (8 - size % 8) % 8)?;
        let tags = strings(&mut file, &[1000, 1001, 1002, 1022, 1124, 1125])?;
        let format = tags.get(&1124).cloned().unwrap_or_default();
        let compressor = tags.get(&1125).cloned().unwrap_or_else(|| "gzip".into());
        if format != "cpio" {
            return Err(VMSetupError::Unsupported(format));
        }
        let header = RPMHeader {
            name: tags.get(&1000).cloned().unwrap_or_default(),
            version: tags.get(&1001).cloned().unwrap_or_default(),
            release: tags.get(&1002).cloned().unwrap_or_default(),
            arch: tags.get(&1022).cloned().unwrap_or_default(),
            payload_compressor: compressor.clone(),
        };
        let mut payload = PayloadSource::new(file, PayloadDecoder::new(&compressor)?);
        CPIOReader::read(&mut payload, |paths, next| body(&header, paths, next))
    }
}

/// The header's 16-byte intro: its index entries and store bytes, which follow.
fn intro(source: &mut dyn ByteSource) -> Result<(usize, usize), VMSetupError> {
    let intro = read_exactly(source, 16)?;
    if intro[..3] != [0x8e, 0xad, 0xe8] {
        return Err(VMSetupError::Corrupt);
    }
    Ok((be32(&intro, 8) as usize, be32(&intro, 12) as usize))
}

/// The header's STRING (6) and I18NSTRING (9) values for `wanted` tags.
fn strings(source: &mut dyn ByteSource, wanted: &[u32]) -> Result<std::collections::BTreeMap<u32, String>, VMSetupError> {
    let (entries, store_size) = intro(source)?;
    let index_size = 16 * entries;
    let rest = read_exactly(source, index_size + store_size)?;
    let store = &rest[index_size..];
    let mut out = std::collections::BTreeMap::new();
    for i in 0..entries {
        let tag = be32(&rest, 16 * i);
        let type_ = be32(&rest, 16 * i + 4);
        let offset = be32(&rest, 16 * i + 8) as usize;
        if !wanted.contains(&tag) || (type_ != 6 && type_ != 9) || offset >= store.len() {
            continue;
        }
        let end = store[offset..].iter().position(|b| *b == 0).map(|p| offset + p).unwrap_or(store.len());
        out.insert(tag, String::from_utf8_lossy(&store[offset..end]).into_owned());
    }
    Ok(out)
}

fn be32(data: &[u8], at: usize) -> u32 {
    u32::from_be_bytes([data[at], data[at + 1], data[at + 2], data[at + 3]])
}

/// A file's bytes, read 1 MiB at a time.
pub struct FileSource {
    handle: File,
    buffer: Vec<u8>,
    /// How much of `buffer` is still unread (`buffer[offset..]`).
    offset: usize,
}

impl FileSource {
    pub fn open(path: &Path) -> Result<Self, VMSetupError> {
        let handle = File::open(path).map_err(|_| VMSetupError::Missing { path: path.to_string_lossy().into_owned() })?;
        Ok(FileSource { handle, buffer: Vec::new(), offset: 0 })
    }
}

impl ByteSource for FileSource {
    fn read(&mut self, up_to: usize) -> Result<Vec<u8>, VMSetupError> {
        if self.offset == self.buffer.len() {
            let mut buf = vec![0u8; 1 << 20];
            let read = self.handle.read(&mut buf).map_err(|_| VMSetupError::Corrupt)?;
            buf.truncate(read);
            self.buffer = buf;
            self.offset = 0;
        }
        let end = (self.offset + up_to).min(self.buffer.len());
        let out = self.buffer[self.offset..end].to_vec();
        self.offset = end;
        Ok(out)
    }
}

/// A compressed stream's bytes as it decompresses.
pub struct PayloadSource {
    file: FileSource,
    decoder: PayloadDecoder,
    buffer: Vec<u8>,
    offset: usize,
    ended: bool,
}

impl PayloadSource {
    pub fn new(file: FileSource, decoder: PayloadDecoder) -> Self {
        PayloadSource { file, decoder, buffer: Vec::new(), offset: 0, ended: false }
    }
}

impl ByteSource for PayloadSource {
    fn read(&mut self, up_to: usize) -> Result<Vec<u8>, VMSetupError> {
        while self.offset == self.buffer.len() && !self.ended {
            let compressed = self.file.read(1 << 20)?;
            if compressed.is_empty() {
                self.buffer = self.decoder.finish()?;
                self.offset = 0;
                self.ended = true;
            } else {
                self.buffer = self.decoder.decode(&compressed)?;
                self.offset = 0;
            }
        }
        let end = (self.offset + up_to).min(self.buffer.len());
        let out = self.buffer[self.offset..end].to_vec();
        self.offset = end;
        Ok(out)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::rpm_fixture::{self, File};
    use std::collections::BTreeMap;
    use std::path::Path;

    fn write_rpm(name: &str, version: &str, release: &str, arch: &str, payload_format: &str, files: &[File]) -> std::path::PathBuf {
        let dir = rpm_fixture::temp(name);
        let path = dir.join("demo.rpm");
        rpm_fixture::write(&path, &rpm_fixture::rpm_bytes(name, version, release, arch, payload_format, files));
        path
    }

    fn read(rpm: &Path) -> (Option<RPMHeader>, BTreeMap<Vec<String>, Vec<u8>>) {
        let mut header = None;
        let mut files = BTreeMap::new();
        RPMReader::read_files(rpm, |h, paths, next| {
            header = Some(h.clone());
            let mut bytes = Vec::new();
            while let Some(chunk) = next()? {
                bytes.extend_from_slice(&chunk);
            }
            files.insert(paths.to_vec(), bytes);
            Ok(())
        })
        .unwrap();
        (header, files)
    }

    #[test]
    fn reads_the_header_and_every_file() {
        let rpm = write_rpm("demo", "1.0", "1.fc44", "aarch64", "cpio", &[
            File { path: "/usr/bin/demo".into(), bytes: b"#!/bin/sh\n".to_vec(), inode: 1 },
            File { path: "/etc/demo.conf".into(), bytes: vec![7u8; 300_000], inode: 2 },
        ]);
        let (header, files) = read(&rpm);
        assert_eq!(
            header,
            Some(RPMHeader {
                name: "demo".into(),
                version: "1.0".into(),
                release: "1.fc44".into(),
                arch: "aarch64".into(),
                payload_compressor: "xz".into(),
            })
        );
        assert_eq!(files.len(), 2);
        assert_eq!(files[&vec!["/usr/bin/demo".to_string()]], b"#!/bin/sh\n");
        assert_eq!(files[&vec!["/etc/demo.conf".to_string()]], vec![7u8; 300_000]);
    }

    #[test]
    fn hardlinked_files_all_get_their_bytes() {
        let rpm = write_rpm("links", "1.0", "1.fc44", "aarch64", "cpio", &[
            File { path: "/usr/bin/a".into(), bytes: b"same\n".to_vec(), inode: 5 },
            File { path: "/usr/bin/b".into(), bytes: b"same\n".to_vec(), inode: 5 },
        ]);
        let (_, files) = read(&rpm);
        assert_eq!(files.len(), 1);
        assert_eq!(files[&vec!["/usr/bin/a".to_string(), "/usr/bin/b".to_string()]], b"same\n");
    }

    #[test]
    fn a_non_cpio_payload_is_refused() {
        let rpm = write_rpm("delta", "1.0", "1.fc44", "aarch64", "drpm", &[
            File { path: "/x".into(), bytes: Vec::new(), inode: 1 },
        ]);
        let err = RPMReader::read_files(&rpm, |_, _, _| Ok(())).unwrap_err();
        assert_eq!(err, VMSetupError::Unsupported("drpm".into()));
    }

    /// Real Fedora packages (kernel-core is xz, systemd zstd), when MN_RPM_SAMPLES names a folder holding them: the
    /// pinned kernel comes out with the bytes of the image build's own copy. Without it, nothing to check.
    #[test]
    fn real_fedora_packages_read() {
        let Ok(folder) = std::env::var("MN_RPM_SAMPLES") else { return };
        let mut rpms: Vec<_> = std::fs::read_dir(&folder)
            .unwrap()
            .map(|e| e.unwrap().path())
            .filter(|p| p.extension().is_some_and(|e| e == "rpm"))
            .collect();
        rpms.sort();
        assert!(rpms.len() >= 2);
        for rpm in rpms {
            let mut count = 0;
            RPMReader::read_files(&rpm, |_, paths, next| {
                count += 1;
                if paths.iter().any(|p| p == "/lib/modules/7.2.10-200.fc44.aarch64/vmlinuz") {
                    let mut hash = crate::posix::Sha256::new();
                    while let Some(chunk) = next()? {
                        hash.update(&chunk);
                    }
                    let expected = std::fs::read(std::env::var("MN_RPM_VMLINUZ").unwrap()).unwrap();
                    assert_eq!(hash.finalize(), crate::posix::sha256(&expected));
                } else {
                    while next()?.is_some() {}
                }
                Ok(())
            })
            .unwrap();
            assert!(count > 0);
        }
    }
}
