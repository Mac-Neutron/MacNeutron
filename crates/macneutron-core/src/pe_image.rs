//! The COFF machine type, signature and icon of a Windows executable, read from its headers and resources only (game
//! exes can be many GB).

use std::path::Path;

pub const I386: u16 = 0x014c;
pub const AMD64: u16 = 0x8664;
pub const ARM64: u16 = 0xAA64;

/// The little-endian unsigned integer of `size` bytes at `offset`, or None past the end.
fn le(data: &[u8], offset: usize, size: usize) -> Option<usize> {
    if offset.checked_add(size)? > data.len() {
        return None;
    }
    Some((0..size).fold(0usize, |acc, i| acc | ((data[offset + i] as usize) << (8 * i))))
}

/// The COFF `Machine` of a PE file, or None when `url` isn't one (missing, unreadable, a script, truncated).
/// ARM64EC images report `AMD64`, ARM64X images `ARM64`.
pub fn machine(path: &Path) -> Option<u16> {
    use std::io::{Read, Seek, SeekFrom};
    // Header only: game exes can be many GB (Swift PEImage.swift:12-21).
    let mut file = std::fs::File::open(path).ok()?;
    let mut dos = [0u8; 64];
    file.read_exact(&mut dos).ok()?;
    if dos[0] != 0x4D || dos[1] != 0x5A {
        return None;
    }
    let lfanew = u32::from_le_bytes([dos[0x3C], dos[0x3D], dos[0x3E], dos[0x3F]]) as u64;
    file.seek(SeekFrom::Start(lfanew)).ok()?;
    let mut nt = [0u8; 6];
    file.read_exact(&mut nt).ok()?;
    if nt[..4] != [0x50, 0x45, 0, 0] {
        return None;
    }
    Some((nt[4] as u16) | ((nt[5] as u16) << 8))
}

/// The optional header's Certificate Table (data directory 4) has a non-zero size: the file carries an Authenticode
/// signature. Not a validity check (Wine's WinVerifyTrust, and SteamStub, do that).
pub fn has_certificate_table(path: &Path) -> bool {
    let Ok(data) = std::fs::read(path) else { return false };
    let Some(lfanew) = le(&data, 0x3C, 4) else { return false };
    if le(&data, lfanew, 4) != Some(0x4550) {
        return false;
    }
    let Some(magic) = le(&data, lfanew + 24, 2) else { return false };
    if magic != 0x10B && magic != 0x20B {
        return false;
    }
    let optional = lfanew + 24;
    let pe32 = magic == 0x10B;
    let Some(count) = le(&data, optional + if pe32 { 92 } else { 108 }, 4) else { return false };
    if count <= 4 {
        return false;
    }
    let Some(size) = le(&data, optional + if pe32 { 128 } else { 144 } + 4, 4) else { return false };
    size > 0
}

/// The icon Windows shows for the file, its first RT_GROUP_ICON's largest image (the deepest on a tie), as a
/// one-image .ico; None when it has none. Only the headers and that resource's pages are read.
pub fn icon(path: &Path) -> Option<Vec<u8>> {
    let data = std::fs::read(path).ok()?;
    let lfanew = le(&data, 0x3C, 4)?;
    if le(&data, lfanew, 4) != Some(0x4550) {
        return None;
    }
    let sections = le(&data, lfanew + 6, 2)?;
    let optional_size = le(&data, lfanew + 20, 2)?;
    let magic = le(&data, lfanew + 24, 2)?;
    if magic != 0x10B && magic != 0x20B {
        return None;
    }
    let optional = lfanew + 24;
    let directories = optional + if magic == 0x10B { 96 } else { 112 };
    let count = le(&data, directories - 4, 4)?;
    if count <= 2 {
        return None;
    }
    let rsrc_rva = le(&data, directories + 16, 4)?;
    if rsrc_rva == 0 {
        return None;
    }

    // Through the section table.
    let file = |rva: usize| -> Option<usize> {
        for i in 0..sections {
            let header = optional + optional_size + 40 * i;
            if let (Some(va), Some(raw), Some(pointer)) =
                (le(&data, header + 12, 4), le(&data, header + 16, 4), le(&data, header + 20, 4))
            {
                if rva >= va && rva < va + raw {
                    return Some(pointer + rva - va);
                }
            }
        }
        None
    };
    let rsrc = file(rsrc_rva)?;

    // A resource directory's entry for `id`, or its first entry (a name or an ID) when None.
    let entry = |directory: usize, id: Option<usize>| -> Option<usize> {
        let named = le(&data, directory + 12, 2)?;
        let ids = le(&data, directory + 14, 2)?;
        for i in 0..(named + ids) {
            let name = le(&data, directory + 16 + 8 * i, 4)?;
            let offset = le(&data, directory + 20 + 8 * i, 4)?;
            if id.is_none() || Some(name) == id {
                return Some(offset);
            }
        }
        None
    };

    // Type, then name, then the first language: the data's file range.
    let resource = |ty: usize, id: Option<usize>| -> Option<(usize, usize)> {
        const SUB: usize = 0x8000_0000;
        let names = entry(rsrc, Some(ty))?;
        if names & SUB == 0 {
            return None;
        }
        let languages = entry(rsrc + (names & !SUB), id)?;
        if languages & SUB == 0 {
            return None;
        }
        let leaf = entry(rsrc + (languages & !SUB), None)?;
        if leaf & SUB != 0 {
            return None;
        }
        let rva = le(&data, rsrc + leaf, 4)?;
        let size = le(&data, rsrc + leaf + 4, 4)?;
        let offset = file(rva)?;
        if offset + size > data.len() {
            return None;
        }
        Some((offset, size))
    };

    let group = resource(14, None)?;
    let n = le(&data, group.0 + 4, 2)?;
    if n == 0 || 6 + 14 * n > group.1 {
        return None;
    }
    // A width byte of 0 is 256.
    let rank = |e: usize| -> (usize, usize) {
        (((data[e] as usize) + 255) % 256, le(&data, e + 6, 2).unwrap_or(0))
    };
    let mut best = group.0 + 6;
    for i in 1..n {
        let candidate = group.0 + 6 + 14 * i;
        if rank(candidate) > rank(best) {
            best = candidate;
        }
    }
    let id = le(&data, best + 12, 2)?;
    let image = resource(3, Some(id))?;

    let mut out = vec![0, 0, 1, 0, 1, 0];
    out.extend_from_slice(&data[best..best + 8]);
    out.extend_from_slice(&(image.1 as u32).to_le_bytes());
    out.extend_from_slice(&22u32.to_le_bytes());
    out.extend_from_slice(&data[image.0..image.0 + image.1]);
    Some(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_support::{make_temp_dir, pe_bytes, pe_bytes_icons, write};

    fn temp(name: &str) -> std::path::PathBuf {
        crate::rpm_fixture::temp(name).join("x.exe")
    }

    #[test]
    fn reads_the_machine_of_a_minimal_pe() {
        let path = temp("machine");
        let mut data = vec![0u8; 0x80];
        data[0] = 0x4D;
        data[1] = 0x5A;
        data[0x3C] = 0x40;
        data[0x40] = 0x50;
        data[0x41] = 0x45;
        data[0x44] = 0x64;
        data[0x45] = 0x86;
        std::fs::write(&path, &data).unwrap();
        assert_eq!(machine(&path), Some(AMD64));
        let _ = std::fs::remove_dir_all(path.parent().unwrap());
    }

    #[test]
    fn rejects_non_pe_and_missing() {
        let path = temp("reject");
        std::fs::write(&path, b"not a pe").unwrap();
        assert_eq!(machine(&path), None);
        assert_eq!(machine(&path.with_extension("missing")), None);
        let _ = std::fs::remove_dir_all(path.parent().unwrap());
    }

    #[test]
    fn amd64_and_arm64_and_i386_machines_are_read() {
        let dir = make_temp_dir();
        for m in [AMD64, ARM64, I386, 0x01c4] {
            let exe = dir.join(format!("{m}.exe"));
            std::fs::write(&exe, pe_bytes(m, false)).unwrap();
            assert_eq!(machine(&exe), Some(m));
        }
    }

    #[test]
    fn a_script_is_not_pe() {
        let script = make_temp_dir().join("run.sh");
        write(&format!("#!/bin/sh\necho MZ PE\n{}", "x".repeat(200)), &script, true);
        assert_eq!(machine(&script), None);
    }

    #[test]
    fn a_truncated_mz_is_not_pe() {
        let dir = make_temp_dir();
        let short = dir.join("short.exe");
        std::fs::write(&short, [0x4D, 0x5A, 0, 0]).unwrap();
        assert_eq!(machine(&short), None);
        let no_nt = dir.join("nont.exe");
        std::fs::write(&no_nt, &pe_bytes(AMD64, false)[..0x83]).unwrap();
        assert_eq!(machine(&no_nt), None);
    }

    #[test]
    fn a_missing_file_is_not_pe() {
        assert_eq!(machine(&make_temp_dir().join("missing.exe")), None);
    }

    #[test]
    fn machine_reads_a_path_with_spaces() {
        let exe = make_temp_dir().join("game \u{e9}/Game.exe");
        std::fs::create_dir_all(exe.parent().unwrap()).unwrap();
        std::fs::write(&exe, pe_bytes(ARM64, false)).unwrap();
        assert_eq!(machine(&exe), Some(ARM64));
    }

    #[test]
    fn machine_reads_only_the_header_of_a_huge_file() {
        let exe = make_temp_dir().join("Huge.exe");
        std::fs::write(&exe, pe_bytes(AMD64, false)).unwrap();
        std::fs::OpenOptions::new().write(true).open(&exe).unwrap().set_len(4 << 30).unwrap(); // sparse, 4 GiB
        let start = std::time::Instant::now();
        assert_eq!(machine(&exe), Some(AMD64));
        assert!(start.elapsed() < std::time::Duration::from_secs(1));
    }

    #[test]
    fn certificate_table_is_seen_in_pe32_and_pe32_plus() {
        let dir = make_temp_dir();
        for m in [I386, AMD64] {
            for signed in [true, false] {
                let file = dir.join(format!("{m}-{signed}.dll"));
                std::fs::write(&file, pe_bytes(m, signed)).unwrap();
                assert_eq!(has_certificate_table(&file), signed);
            }
        }
        let short = dir.join("short.dll");
        std::fs::write(&short, &pe_bytes(AMD64, true)[..0x100]).unwrap();
        assert!(!has_certificate_table(&short));
    }

    #[test]
    fn icon_is_the_first_groups_largest_image_as_an_ico() {
        let small = vec![0x11u8; 40];
        let big = vec![0x89u8, 0x50, 0x4E, 0x47, 1, 2, 3];
        let exe = make_temp_dir().join("Game.exe");
        std::fs::write(&exe, pe_bytes_icons(&[(32, 32, small.clone()), (0, 32, big.clone()), (48, 8, small)])).unwrap();
        let mut ico: Vec<u8> = vec![0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 32, 0, 7, 0, 0, 0, 22, 0, 0, 0];
        ico.extend(&big);
        assert_eq!(icon(&exe), Some(ico));
    }

    #[test]
    fn an_image_without_resources_has_no_icon() {
        let exe = make_temp_dir().join("Game.exe");
        std::fs::write(&exe, pe_bytes(AMD64, false)).unwrap();
        assert_eq!(icon(&exe), None);
    }
}
