//! The COFF machine type, signature and icon of a Windows executable, read from its headers and resources only (game
//! exes can be many GB).

use std::fs::File;
use std::os::unix::fs::FileExt;
use std::path::Path;

pub const I386: u16 = 0x014c;
pub const AMD64: u16 = 0x8664;
pub const ARM64: u16 = 0xAA64;

const MAX_SECTIONS: usize = 96;
const MAX_ENTRIES: usize = 512;
const MAX_ICON_BYTES: usize = 16 * 1024 * 1024;

fn read_at(file: &File, file_len: u64, offset: u64, len: usize) -> Option<Vec<u8>> {
    let end = offset.checked_add(len as u64)?;
    if end > file_len {
        return None;
    }
    let mut buf = vec![0u8; len];
    file.read_exact_at(&mut buf, offset).ok()?;
    Some(buf)
}

fn le16(data: &[u8], offset: usize) -> Option<u16> {
    let end = offset.checked_add(2)?;
    let bytes: [u8; 2] = data.get(offset..end)?.try_into().ok()?;
    Some(u16::from_le_bytes(bytes))
}

fn le32(data: &[u8], offset: usize) -> Option<u32> {
    let end = offset.checked_add(4)?;
    let bytes: [u8; 4] = data.get(offset..end)?.try_into().ok()?;
    Some(u32::from_le_bytes(bytes))
}

/// The COFF `Machine` of a PE file, or None when `path` isn't one (missing, unreadable, a script, truncated).
/// ARM64EC images report `AMD64`, ARM64X images `ARM64`.
pub fn machine(path: &Path) -> Option<u16> {
    let file = File::open(path).ok()?;
    let file_len = file.metadata().ok()?.len();
    let dos = read_at(&file, file_len, 0, 64)?;
    if dos[0] != 0x4D || dos[1] != 0x5A {
        return None;
    }
    let lfanew = le32(&dos, 0x3C)? as u64;
    let nt = read_at(&file, file_len, lfanew, 6)?;
    if nt[..4] != [0x50, 0x45, 0, 0] {
        return None;
    }
    le16(&nt, 4)
}

/// The optional header's Certificate Table (data directory 4) has a non-zero size: the file carries an Authenticode
/// signature. Not a validity check (Wine's WinVerifyTrust, and SteamStub, do that).
pub fn has_certificate_table(path: &Path) -> bool {
    has_certificate_table_inner(path).unwrap_or(false)
}

fn has_certificate_table_inner(path: &Path) -> Option<bool> {
    let file = File::open(path).ok()?;
    let file_len = file.metadata().ok()?.len();
    let dos = read_at(&file, file_len, 0, 64)?;
    if dos[0] != 0x4D || dos[1] != 0x5A {
        return None;
    }
    let lfanew = le32(&dos, 0x3C)? as u64;
    let nt_magic = read_at(&file, file_len, lfanew, 26)?;
    if nt_magic[..4] != [0x50, 0x45, 0, 0] {
        return None;
    }
    let size_of_optional_header = le16(&nt_magic, 20)? as usize;
    let magic = le16(&nt_magic, 24)?;
    if magic != 0x10B && magic != 0x20B {
        return None;
    }
    let pe32plus = magic == 0x20B;
    let dirs_offset = if pe32plus { 112usize } else { 96usize };
    if size_of_optional_header < dirs_offset {
        return None;
    }
    let optional = read_at(&file, file_len, lfanew.checked_add(24)?, dirs_offset)?;
    let num_dirs = le32(&optional, dirs_offset - 4)?;
    if num_dirs <= 4 {
        return Some(false);
    }
    if size_of_optional_header < dirs_offset + 40 {
        return None;
    }
    let cert_dir = read_at(&file, file_len, lfanew.checked_add(24 + dirs_offset as u64 + 32)?, 8)?;
    let size = le32(&cert_dir, 4)?;
    Some(size > 0)
}

/// The icon Windows shows for the file, its first RT_GROUP_ICON's largest image (the deepest on a tie), as a
/// one-image .ico; None when it has none. Only the headers and that resource's pages are read.
pub fn icon(path: &Path) -> Option<Vec<u8>> {
    let file = File::open(path).ok()?;
    let file_len = file.metadata().ok()?.len();
    let dos = read_at(&file, file_len, 0, 64)?;
    if dos[0] != 0x4D || dos[1] != 0x5A {
        return None;
    }
    let lfanew = le32(&dos, 0x3C)? as u64;
    let nt = read_at(&file, file_len, lfanew, 24)?;
    if nt[..4] != [0x50, 0x45, 0, 0] {
        return None;
    }
    let num_sections = (le16(&nt, 6)? as usize).min(MAX_SECTIONS);
    if num_sections == 0 {
        return None;
    }
    let optional_size = le16(&nt, 20)? as u64;
    let optional = lfanew.checked_add(24)?;
    let magic_bytes = read_at(&file, file_len, optional, 2)?;
    let magic = le16(&magic_bytes, 0)?;
    if magic != 0x10B && magic != 0x20B {
        return None;
    }
    let dirs_rel = if magic == 0x20B { 112usize } else { 96usize };
    if (optional_size as usize) < dirs_rel + 24 {
        return None;
    }
    let opt = read_at(&file, file_len, optional, dirs_rel + 24)?;
    let count = le32(&opt, dirs_rel - 4)?;
    if count <= 2 {
        return None;
    }
    let rsrc_rva = le32(&opt, dirs_rel + 16)?;
    if rsrc_rva == 0 {
        return None;
    }

    let section_bytes = read_at(&file, file_len, optional.checked_add(optional_size)?, num_sections * 40)?;
    let mut sections = Vec::with_capacity(num_sections);
    for chunk in section_bytes.as_chunks::<40>().0 {
        let va = le32(chunk, 12)?;
        let raw = le32(chunk, 16)?;
        let ptr = le32(chunk, 20)?;
        sections.push((va, raw, ptr));
    }

    let rva_to_offset = |rva: u32| -> Option<u64> {
        let rva64 = rva as u64;
        for &(va, raw, ptr) in &sections {
            let va64 = va as u64;
            let raw64 = raw as u64;
            if rva64 >= va64 && rva64 < va64 + raw64 {
                return (ptr as u64).checked_add(rva64 - va64);
            }
        }
        None
    };
    let rsrc = rva_to_offset(rsrc_rva)?;

    // A resource directory's entry for `id`, or its first entry (a name or an ID) when None.
    let find_entry = |dir_offset: u64, id: Option<u32>| -> Option<u32> {
        let hdr = read_at(&file, file_len, dir_offset, 16)?;
        let named = le16(&hdr, 12)? as usize;
        let ids = le16(&hdr, 14)? as usize;
        let total = named.checked_add(ids)?.min(MAX_ENTRIES);
        if total == 0 {
            return None;
        }
        let entries_bytes = read_at(&file, file_len, dir_offset.checked_add(16)?, total * 8)?;
        for chunk in entries_bytes.as_chunks::<8>().0 {
            let name = le32(chunk, 0)?;
            let offset = le32(chunk, 4)?;
            if id.is_none() || Some(name) == id {
                return Some(offset);
            }
        }
        None
    };

    // Type, then name, then the first language: the data's file range.
    let find_resource = |ty: u32, id: Option<u32>| -> Option<(u64, usize)> {
        const SUB: u32 = 0x8000_0000;
        let names = find_entry(rsrc, Some(ty))?;
        if names & SUB == 0 {
            return None;
        }
        let languages = find_entry(rsrc.checked_add((names & !SUB) as u64)?, id)?;
        if languages & SUB == 0 {
            return None;
        }
        let leaf = find_entry(rsrc.checked_add((languages & !SUB) as u64)?, None)?;
        if leaf & SUB != 0 {
            return None;
        }
        let data_entry = read_at(&file, file_len, rsrc.checked_add(leaf as u64)?, 8)?;
        let rva = le32(&data_entry, 0)?;
        let size = le32(&data_entry, 4)? as usize;
        if size == 0 || size > MAX_ICON_BYTES {
            return None;
        }
        let offset = rva_to_offset(rva)?;
        if offset.checked_add(size as u64)? > file_len {
            return None;
        }
        Some((offset, size))
    };

    let (group_off, group_size) = find_resource(14, None)?;
    if group_size < 6 {
        return None;
    }
    let group_bytes = read_at(&file, file_len, group_off, group_size)?;
    let n = le16(&group_bytes, 4)? as usize;
    if n == 0 || n > MAX_ENTRIES || 6 + 14 * n > group_bytes.len() {
        return None;
    }
    // A width byte of 0 is 256.
    let rank = |e: usize| -> (usize, usize) {
        (((group_bytes[e] as usize) + 255) % 256, le16(&group_bytes, e + 6).unwrap_or(0) as usize)
    };
    let mut best = 6usize;
    for i in 1..n {
        let candidate = 6 + 14 * i;
        if rank(candidate) > rank(best) {
            best = candidate;
        }
    }
    let id = le16(&group_bytes, best + 12)? as u32;
    let (img_off, img_size) = find_resource(3, Some(id))?;
    let img_bytes = read_at(&file, file_len, img_off, img_size)?;

    let mut out = Vec::with_capacity(22 + img_bytes.len());
    out.extend_from_slice(&[0, 0, 1, 0, 1, 0]);
    out.extend_from_slice(&group_bytes[best..best + 8]);
    out.extend_from_slice(&(img_size as u32).to_le_bytes());
    out.extend_from_slice(&22u32.to_le_bytes());
    out.extend_from_slice(&img_bytes);
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

    #[test]
    fn has_certificate_table_reads_only_the_header_of_a_huge_file() {
        let dir = make_temp_dir();
        for (name, signed) in [("signed.exe", true), ("unsigned.exe", false)] {
            let exe = dir.join(name);
            std::fs::write(&exe, pe_bytes(AMD64, signed)).unwrap();
            std::fs::OpenOptions::new()
                .write(true)
                .open(&exe)
                .unwrap()
                .set_len(4 << 30)
                .unwrap(); // sparse, 4 GiB
            let start = std::time::Instant::now();
            assert_eq!(has_certificate_table(&exe), signed);
            assert!(start.elapsed() < std::time::Duration::from_secs(1));
        }
    }

    #[test]
    fn icon_reads_only_headers_and_resources_of_a_huge_file() {
        let big = vec![0x89u8, 0x50, 0x4E, 0x47, 1, 2, 3];
        let exe = make_temp_dir().join("HugeIcon.exe");
        std::fs::write(&exe, pe_bytes_icons(&[(0, 32, big.clone())])).unwrap();
        std::fs::OpenOptions::new()
            .write(true)
            .open(&exe)
            .unwrap()
            .set_len(4 << 30)
            .unwrap(); // sparse, 4 GiB
        let mut expected: Vec<u8> = vec![0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 32, 0, 7, 0, 0, 0, 22, 0, 0, 0];
        expected.extend(&big);
        let start = std::time::Instant::now();
        assert_eq!(icon(&exe), Some(expected));
        assert!(start.elapsed() < std::time::Duration::from_secs(1));
    }

    #[test]
    fn truncated_size_of_optional_header_is_rejected() {
        let dir = make_temp_dir();
        let mut cert_pe = pe_bytes(AMD64, true);
        // Set SizeOfOptionalHeader (lfanew=0x80, +20 = 0x94) below dirs_offset + 40 (152).
        cert_pe[0x94..0x96].copy_from_slice(&120u16.to_le_bytes());
        let cert_path = dir.join("trunc_cert.exe");
        std::fs::write(&cert_path, &cert_pe).unwrap();
        assert!(!has_certificate_table(&cert_path));

        let mut icon_pe = pe_bytes_icons(&[(0, 32, vec![0x89, 0x50, 0x4E, 0x47])]);
        // Set SizeOfOptionalHeader below dirs_rel + 24 (136).
        icon_pe[0x94..0x96].copy_from_slice(&120u16.to_le_bytes());
        let icon_path = dir.join("trunc_icon.exe");
        std::fs::write(&icon_path, &icon_pe).unwrap();
        assert_eq!(icon(&icon_path), None);
    }
}
