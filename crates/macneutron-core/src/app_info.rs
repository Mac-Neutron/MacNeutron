//! Reads Steam's binary `appcache/appinfo.vdf` (v29) and sharded `appcache/appinfo/appinfo_*.vdf` (v30).

use std::collections::{HashMap, HashSet};
use std::fmt;
use std::path::{Path, PathBuf};

/// What MacNeutron needs to know about one Steam app.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AppInfo {
    pub app_id: u32,
    pub name: String,
    /// Lowercased `common/type`, e.g. "game", "tool", "dlc".
    pub r#type: String,
    /// `common/oslist`, e.g. ["windows", "macos"].
    pub oslist: HashSet<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum AppInfoError {
    UnsupportedFormat(u32),
    Truncated(usize),
    UnknownValueType(u8, usize),
}

impl fmt::Display for AppInfoError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            AppInfoError::UnsupportedFormat(magic) => write!(
                f,
                "Steam's app cache uses format 0x{magic:08x}, which MacNeutron doesn't understand yet"
            ),
            AppInfoError::Truncated(offset) => {
                write!(f, "Steam's app cache ends early (byte {offset})")
            }
            AppInfoError::UnknownValueType(t, offset) => {
                write!(f, "Steam's app cache has unknown value type {t} at byte {offset}")
            }
        }
    }
}

impl std::error::Error for AppInfoError {}

pub const MAGIC_V29: u32 = 0x0756_4429;
pub const MAGIC_V30: u32 = 0x0756_442A;

const HEADER_V30_SIZE: usize = 0x2C;
const MAX_KV_DEPTH: usize = 64;

pub fn read(path: &Path) -> Result<Vec<AppInfo>, AppInfoError> {
    if path.is_dir() {
        return read_sharded(path);
    }
    if !path.exists() && path.file_name().and_then(|n| n.to_str()) == Some("appinfo.vdf") {
        let sibling = path.with_file_name("appinfo");
        if sibling.is_dir() && !shard_files(&sibling)?.is_empty() {
            return read_sharded(&sibling);
        }
    }
    let data = std::fs::read(path).map_err(|_| AppInfoError::Truncated(0))?;
    parse(&data)
}

fn shard_sort_key(name: &str) -> (u8, u64, &str) {
    let num = name
        .strip_prefix("appinfo_")
        .and_then(|s| s.strip_suffix(".vdf"))
        .and_then(|s| s.parse::<u64>().ok());
    match num {
        Some(n) => (0, n, name),
        None => (1, 0, name),
    }
}

fn shard_files(dir: &Path) -> Result<Vec<PathBuf>, AppInfoError> {
    let entries = std::fs::read_dir(dir).map_err(|_| AppInfoError::Truncated(0))?;
    let mut shards: Vec<(String, PathBuf)> = entries
        .flatten()
        .filter_map(|entry| {
            let name = entry.file_name().into_string().ok()?;
            if name.starts_with("appinfo_") && name.ends_with(".vdf") && entry.path().is_file() {
                Some((name, entry.path()))
            } else {
                None
            }
        })
        .collect();
    shards.sort_by(|a, b| shard_sort_key(&a.0).cmp(&shard_sort_key(&b.0)));
    Ok(shards.into_iter().map(|(_, p)| p).collect())
}

fn read_sharded(dir: &Path) -> Result<Vec<AppInfo>, AppInfoError> {
    let shards = shard_files(dir)?;
    if shards.is_empty() {
        return Err(AppInfoError::Truncated(0));
    }
    let mut apps: Vec<AppInfo> = Vec::new();
    let mut index_by_id: HashMap<u32, usize> = HashMap::new();
    for shard in shards {
        let data = std::fs::read(&shard).map_err(|_| AppInfoError::Truncated(0))?;
        for app in parse(&data)? {
            if let Some(&idx) = index_by_id.get(&app.app_id) {
                apps[idx] = app;
            } else {
                index_by_id.insert(app.app_id, apps.len());
                apps.push(app);
            }
        }
    }
    Ok(apps)
}

pub fn parse(data: &[u8]) -> Result<Vec<AppInfo>, AppInfoError> {
    let mut cursor = Cursor::new(data);
    let magic = cursor.u32()?;
    match magic {
        MAGIC_V29 => parse_v29(data),
        MAGIC_V30 => parse_v30(data),
        other => Err(AppInfoError::UnsupportedFormat(other)),
    }
}

fn parse_v29(data: &[u8]) -> Result<Vec<AppInfo>, AppInfoError> {
    let mut cursor = Cursor::new(data);
    let magic = cursor.u32()?;
    if magic != MAGIC_V29 {
        return Err(AppInfoError::UnsupportedFormat(magic));
    }
    let _universe = cursor.u32()?;
    let table_offset = usize::try_from(cursor.u64()?).map_err(|_| AppInfoError::Truncated(cursor.offset))?;
    let strings = string_table(data, table_offset)?;

    let mut apps = Vec::new();
    loop {
        let app_id = cursor.u32()?;
        if app_id == 0 {
            break;
        }
        let size = cursor.u32()? as usize;
        let end = cursor
            .offset
            .checked_add(size)
            .ok_or(AppInfoError::Truncated(cursor.offset))?;
        if end > cursor.bytes.len() {
            return Err(AppInfoError::Truncated(cursor.offset));
        }
        // info state, last updated, PICS token, text SHA-1, change number, binary SHA-1
        cursor.skip(4 + 4 + 8 + 20 + 4 + 20)?;
        apps.push(parse_kv(app_id, data, cursor.offset, end, &strings)?);
        cursor.offset = end;
    }
    Ok(apps)
}

pub fn parse_v30(data: &[u8]) -> Result<Vec<AppInfo>, AppInfoError> {
    let mut cursor = Cursor::new(data);
    let magic = cursor.u32()?;
    if magic != MAGIC_V30 {
        return Err(AppInfoError::UnsupportedFormat(magic));
    }
    let _universe = cursor.u32()?;
    let _reserved0 = cursor.u32()?;
    let _reserved1 = cursor.u32()?;
    let count = cursor.u32()? as usize;
    let index_size = cursor.u32()? as usize;
    let string_count = cursor.u32()? as usize;
    let strings_size = cursor.u32()? as usize;
    let data_size = cursor.u32()? as usize;
    // 8 bytes checksum/reserved at 0x24..0x2C
    cursor.skip(8)?;

    let strings_start = HEADER_V30_SIZE
        .checked_add(index_size)
        .ok_or(AppInfoError::Truncated(HEADER_V30_SIZE))?;
    let data_start = strings_start
        .checked_add(strings_size)
        .ok_or(AppInfoError::Truncated(strings_start))?;
    let total_len = data_start
        .checked_add(data_size)
        .ok_or(AppInfoError::Truncated(data_start))?;
    if total_len != data.len() {
        return Err(AppInfoError::Truncated(total_len.min(data.len())));
    }

    let offset_err = |base: usize| {
        move |err: AppInfoError| match err {
            AppInfoError::Truncated(off) => AppInfoError::Truncated(base + off),
            other => other,
        }
    };

    // Parse string table at [0x2C + index_size .. +strings_size].
    let mut str_cursor = Cursor::new(&data[strings_start..data_start]);
    let mut strings = Vec::with_capacity(string_count.min(strings_size));
    for _ in 0..string_count {
        let s = str_cursor.c_string().map_err(offset_err(strings_start))?;
        strings.push(s);
    }
    if str_cursor.offset != strings_size {
        return Err(AppInfoError::Truncated(strings_start + str_cursor.offset));
    }

    // Parse index at [0x2C .. +index_size].
    let mut idx_cursor = Cursor::new(&data[HEADER_V30_SIZE..strings_start]);
    let mut entries = Vec::with_capacity(count.min(index_size / 16));
    for _ in 0..count {
        let app_id = idx_cursor.u32().map_err(offset_err(HEADER_V30_SIZE))?;
        let offset = idx_cursor.u32().map_err(offset_err(HEADER_V30_SIZE))? as usize;
        let size = idx_cursor.u32().map_err(offset_err(HEADER_V30_SIZE))? as usize;
        let meta_size = idx_cursor.u32().map_err(offset_err(HEADER_V30_SIZE))? as usize;
        idx_cursor.skip(meta_size).map_err(offset_err(HEADER_V30_SIZE))?;

        if size < 20 || offset > data_size || size > data_size - offset {
            return Err(AppInfoError::Truncated(HEADER_V30_SIZE + idx_cursor.offset));
        }
        entries.push((app_id, offset, size));
    }
    if idx_cursor.offset != index_size {
        return Err(AppInfoError::Truncated(HEADER_V30_SIZE + idx_cursor.offset));
    }

    let mut apps = Vec::with_capacity(entries.len());
    for (app_id, offset, size) in entries {
        let kv_start = data_start + offset + 20;
        let kv_end = data_start + offset + size;
        apps.push(parse_kv(app_id, data, kv_start, kv_end, &strings)?);
    }
    Ok(apps)
}

fn string_table(bytes: &[u8], at: usize) -> Result<Vec<String>, AppInfoError> {
    if at > bytes.len() {
        return Err(AppInfoError::Truncated(bytes.len()));
    }
    let mut cursor = Cursor { bytes, offset: at };
    let count = cursor.u32()? as usize;
    let remaining = bytes.len().saturating_sub(cursor.offset);
    let mut strings = Vec::with_capacity(count.min(remaining));
    for _ in 0..count {
        strings.push(cursor.c_string()?);
    }
    Ok(strings)
}

/// Walks the binary KeyValues tree, keeping only `<root>/common/{name,type,oslist}`.
fn parse_kv(
    app_id: u32,
    data: &[u8],
    start: usize,
    end: usize,
    strings: &[String],
) -> Result<AppInfo, AppInfoError> {
    if start > end {
        return Err(AppInfoError::Truncated(end));
    }
    let mut cursor = Cursor { bytes: &data[..end], offset: start };
    let mut path: Vec<String> = Vec::new();
    let (mut name, mut ty, mut oslist) = (String::new(), String::new(), String::new());
    skip_or_capture(&mut cursor, strings, &mut path, &mut name, &mut ty, &mut oslist)?;
    let platforms: HashSet<String> = oslist
        .split(',')
        .map(|s| s.trim().to_lowercase())
        .filter(|s| !s.is_empty())
        .collect();
    Ok(AppInfo { app_id, name, r#type: ty, oslist: platforms })
}

fn skip_or_capture(
    cursor: &mut Cursor,
    strings: &[String],
    path: &mut Vec<String>,
    name: &mut String,
    ty: &mut String,
    oslist: &mut String,
) -> Result<(), AppInfoError> {
    while cursor.offset < cursor.bytes.len() {
        let value_type = cursor.u8()?;
        if value_type == 0x08 || value_type == 0x0B {
            if path.is_empty() {
                break;
            }
            path.pop();
            continue;
        }
        let key_index = cursor.u32()? as usize;
        if key_index >= strings.len() {
            return Err(AppInfoError::Truncated(cursor.offset));
        }
        let key = &strings[key_index];
        match value_type {
            0x00 => {
                if path.len() + 1 >= MAX_KV_DEPTH {
                    return Err(AppInfoError::Truncated(cursor.offset));
                }
                path.push(key.clone());
            }
            0x01 | 0x05 => {
                let value = if value_type == 0x01 {
                    cursor.c_string()?
                } else {
                    cursor.utf16_le_string()?
                };
                if path.len() == 2 && path[1] == "common" {
                    match key.as_str() {
                        "name" => *name = value,
                        "type" => *ty = value.to_lowercase(),
                        "oslist" => *oslist = value,
                        _ => {}
                    }
                }
            }
            0x02 | 0x03 | 0x04 | 0x06 => cursor.skip(4)?,
            0x07 | 0x0A => cursor.skip(8)?,
            other => return Err(AppInfoError::UnknownValueType(other, cursor.offset - 5)),
        }
    }
    Ok(())
}

struct Cursor<'a> {
    bytes: &'a [u8],
    offset: usize,
}

impl<'a> Cursor<'a> {
    fn new(bytes: &'a [u8]) -> Self {
        Cursor { bytes, offset: 0 }
    }

    fn skip(&mut self, count: usize) -> Result<(), AppInfoError> {
        let next = self
            .offset
            .checked_add(count)
            .ok_or(AppInfoError::Truncated(self.offset))?;
        if next > self.bytes.len() {
            return Err(AppInfoError::Truncated(self.offset));
        }
        self.offset = next;
        Ok(())
    }

    fn u8(&mut self) -> Result<u8, AppInfoError> {
        self.skip(1)?;
        Ok(self.bytes[self.offset - 1])
    }

    fn u32(&mut self) -> Result<u32, AppInfoError> {
        self.skip(4)?;
        Ok(u32::from_le_bytes([
            self.bytes[self.offset - 4],
            self.bytes[self.offset - 3],
            self.bytes[self.offset - 2],
            self.bytes[self.offset - 1],
        ]))
    }

    fn u64(&mut self) -> Result<u64, AppInfoError> {
        self.skip(8)?;
        Ok(u64::from_le_bytes([
            self.bytes[self.offset - 8],
            self.bytes[self.offset - 7],
            self.bytes[self.offset - 6],
            self.bytes[self.offset - 5],
            self.bytes[self.offset - 4],
            self.bytes[self.offset - 3],
            self.bytes[self.offset - 2],
            self.bytes[self.offset - 1],
        ]))
    }

    fn c_string(&mut self) -> Result<String, AppInfoError> {
        let rest = &self.bytes[self.offset..];
        let terminator = rest
            .iter()
            .position(|&b| b == 0)
            .ok_or(AppInfoError::Truncated(self.offset))?;
        let s = String::from_utf8_lossy(&rest[..terminator]).into_owned();
        self.offset += terminator + 1;
        Ok(s)
    }

    fn utf16_le_string(&mut self) -> Result<String, AppInfoError> {
        let mut units = Vec::new();
        loop {
            if self.bytes.len() - self.offset < 2 {
                return Err(AppInfoError::Truncated(self.offset));
            }
            let unit = u16::from_le_bytes([self.bytes[self.offset], self.bytes[self.offset + 1]]);
            self.offset += 2;
            if unit == 0 {
                break;
            }
            units.push(unit);
        }
        Ok(String::from_utf16_lossy(&units))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_support::{make_app_info_v29, make_temp_dir};

    fn app(id: u32, name: &str, ty: &str, os: &[&str]) -> AppInfo {
        AppInfo {
            app_id: id,
            name: name.into(),
            r#type: ty.into(),
            oslist: os.iter().map(|s| s.to_string()).collect(),
        }
    }

    #[test]
    fn reads_name_type_and_platforms() {
        let apps = vec![
            app(1062090, "Timberborn", "game", &["windows", "macos"]),
            app(2977660, "Cats", "game", &["windows"]),
            app(1628350, "Steam Linux Runtime", "tool", &["linux"]),
        ];
        assert_eq!(parse(&make_app_info_v29(&apps, MAGIC_V29)).unwrap(), apps);
    }

    #[test]
    fn rejects_other_formats() {
        assert_eq!(
            parse(&make_app_info_v29(&[], 0x0756_4428)),
            Err(AppInfoError::UnsupportedFormat(0x0756_4428))
        );
    }

    #[test]
    fn rejects_truncated_files() {
        let data = make_app_info_v29(&[app(1, "A", "game", &["windows"])], MAGIC_V29);
        assert!(parse(&data[..40]).is_err());
    }

    // Skipped: readsTheRealAppCache (needs the real Steam folder, gated by MACNEUTRON_REAL_STEAM).

    #[test]
    fn reading_while_steam_rewrites_the_file_never_crashes() {
        // A short read may fail; it must not crash.
        let apps: Vec<AppInfo> = (0..3000).map(|i| app(i + 1, &format!("Game {i}"), "game", &["windows"])).collect();
        let big = make_app_info_v29(&apps, MAGIC_V29);
        let path = make_temp_dir().join("appinfo.vdf");
        std::fs::write(&path, &big).unwrap();
        let deadline = std::time::Instant::now() + std::time::Duration::from_secs(1);
        let (writer_path, writer_big) = (path.clone(), big.clone());
        let writer = std::thread::spawn(move || {
            while std::time::Instant::now() < deadline {
                let _ = std::fs::write(&writer_path, &writer_big[..64]);
                let _ = std::fs::write(&writer_path, &writer_big);
            }
        });
        while std::time::Instant::now() < deadline {
            let _ = read(&path);
        }
        writer.join().unwrap();
    }

    fn make_app_info_v30(apps: &[AppInfo], use_0b_end: bool, use_utf16_name: bool) -> Vec<u8> {
        fn index(strings: &mut Vec<String>, key: &str) -> u32 {
            if let Some(found) = strings.iter().position(|s| s == key) {
                return found as u32;
            }
            strings.push(key.to_string());
            (strings.len() - 1) as u32
        }

        let end_marker = if use_0b_end { 0x0B } else { 0x08 };
        let mut strings: Vec<String> = Vec::new();
        let mut index_bytes: Vec<u8> = Vec::new();
        let mut data_bytes: Vec<u8> = Vec::new();

        for app in apps {
            let mut kv: Vec<u8> = Vec::new();
            kv.push(0x00);
            kv.extend(index(&mut strings, "appinfo").to_le_bytes());
            kv.push(0x00);
            kv.extend(index(&mut strings, "common").to_le_bytes());

            if use_utf16_name {
                kv.push(0x05);
                kv.extend(index(&mut strings, "name").to_le_bytes());
                for unit in app.name.encode_utf16() {
                    kv.extend(unit.to_le_bytes());
                }
                kv.extend(0u16.to_le_bytes());
            } else {
                kv.push(0x01);
                kv.extend(index(&mut strings, "name").to_le_bytes());
                kv.extend(app.name.as_bytes());
                kv.push(0);
            }

            kv.push(0x01);
            kv.extend(index(&mut strings, "type").to_le_bytes());
            kv.extend(app.r#type.as_bytes());
            kv.push(0);

            let mut oslist: Vec<&str> = app.oslist.iter().map(String::as_str).collect();
            oslist.sort_unstable();
            let oslist = oslist.join(",");
            kv.push(0x01);
            kv.extend(index(&mut strings, "oslist").to_le_bytes());
            kv.extend(oslist.as_bytes());
            kv.push(0);

            kv.extend([end_marker, end_marker, end_marker]);

            let offset = data_bytes.len() as u32;
            data_bytes.extend([0xABu8; 20]); // 20-byte binary SHA-1 prefix
            data_bytes.extend(&kv);
            let size = (20 + kv.len()) as u32;

            let meta = [1u8, 2, 3, 4];
            index_bytes.extend(app.app_id.to_le_bytes());
            index_bytes.extend(offset.to_le_bytes());
            index_bytes.extend(size.to_le_bytes());
            index_bytes.extend((meta.len() as u32).to_le_bytes());
            index_bytes.extend(meta);
        }

        let mut string_bytes: Vec<u8> = Vec::new();
        for s in &strings {
            string_bytes.extend(s.as_bytes());
            string_bytes.push(0);
        }

        let mut out = Vec::new();
        out.extend(MAGIC_V30.to_le_bytes());
        out.extend(1u32.to_le_bytes()); // universe
        out.extend(0u32.to_le_bytes()); // reserved 0
        out.extend(0u32.to_le_bytes()); // reserved 1
        out.extend((apps.len() as u32).to_le_bytes());
        out.extend((index_bytes.len() as u32).to_le_bytes());
        out.extend((strings.len() as u32).to_le_bytes());
        out.extend((string_bytes.len() as u32).to_le_bytes());
        out.extend((data_bytes.len() as u32).to_le_bytes());
        out.extend([0u8; 8]); // checksum/reserved
        out.extend(index_bytes);
        out.extend(string_bytes);
        out.extend(data_bytes);
        out
    }

    #[test]
    fn parses_v30_single_file() {
        let apps = vec![
            app(1062090, "Timberborn", "game", &["windows", "macos"]),
            app(2977660, "Cats", "game", &["windows"]),
        ];
        let v30 = make_app_info_v30(&apps, false, false);
        assert_eq!(parse(&v30).unwrap(), apps);
    }

    #[test]
    fn reads_sharded_v30_directory_and_deduplicates_last_wins() {
        let dir = make_temp_dir().join("appcache/appinfo");
        std::fs::create_dir_all(&dir).unwrap();
        let shard0 = vec![
            app(10, "Old Name", "game", &["windows"]),
            app(20, "Second", "tool", &["linux"]),
        ];
        let shard1 = vec![
            app(10, "Updated Name", "game", &["windows", "macos"]),
            app(30, "Third", "dlc", &["windows"]),
        ];
        std::fs::write(dir.join("appinfo_0.vdf"), make_app_info_v30(&shard0, false, false)).unwrap();
        std::fs::write(dir.join("appinfo_1.vdf"), make_app_info_v30(&shard1, true, true)).unwrap();

        let parsed = read(&dir).unwrap();
        assert_eq!(
            parsed,
            vec![
                app(10, "Updated Name", "game", &["windows", "macos"]),
                app(20, "Second", "tool", &["linux"]),
                app(30, "Third", "dlc", &["windows"]),
            ]
        );

        let empty_dir = make_temp_dir().join("empty_appinfo");
        std::fs::create_dir_all(&empty_dir).unwrap();
        assert!(read(&empty_dir).is_err());
    }

    #[test]
    fn sharded_v30_sorts_shards_numerically() {
        let dir = make_temp_dir().join("appcache/appinfo");
        std::fs::create_dir_all(&dir).unwrap();
        let shard2 = vec![app(10, "From Shard 2", "game", &["windows"])];
        let shard10 = vec![app(10, "From Shard 10", "game", &["windows"])];
        std::fs::write(dir.join("appinfo_2.vdf"), make_app_info_v30(&shard2, false, false)).unwrap();
        std::fs::write(dir.join("appinfo_10.vdf"), make_app_info_v30(&shard10, false, false)).unwrap();

        let parsed = read(&dir).unwrap();
        assert_eq!(parsed, vec![app(10, "From Shard 10", "game", &["windows"])]);
    }

    #[test]
    fn read_missing_appinfo_vdf_falls_back_to_sibling_appinfo_dir() {
        let appcache = make_temp_dir().join("appcache");
        let shard_dir = appcache.join("appinfo");
        std::fs::create_dir_all(&shard_dir).unwrap();
        let apps = vec![app(440, "Team Fortress 2", "game", &["windows", "macos", "linux"])];
        std::fs::write(shard_dir.join("appinfo_0.vdf"), make_app_info_v30(&apps, false, false)).unwrap();

        let missing_vdf = appcache.join("appinfo.vdf");
        assert_eq!(read(&missing_vdf).unwrap(), apps);
    }

    #[test]
    fn parses_kv_utf16_le_values_and_0b_terminator() {
        let apps = vec![
            app(730, "Counter-Strike 🎮", "game", &["windows"]),
            app(570, "Dota 2", "game", &["windows", "macos"]),
        ];
        let v30 = make_app_info_v30(&apps, true, true);
        assert_eq!(parse(&v30).unwrap(), apps);

        // Odd-byte or unterminated 0x05 UTF-16LE string must fail cleanly with Truncated.
        let mut cursor = Cursor::new(&[b'A', 0x00, b'B']);
        assert!(matches!(cursor.utf16_le_string(), Err(AppInfoError::Truncated(_))));
    }

    #[test]
    fn rejects_corrupt_v30_headers_and_bounds() {
        let valid = make_app_info_v30(&[app(1, "A", "game", &["windows"])], false, false);

        // Wrong total length (extra trailing byte).
        let mut extra = valid.clone();
        extra.push(0);
        assert!(parse(&extra).is_err());

        // Truncated header.
        assert!(parse(&valid[..0x2B]).is_err());

        // Corrupt entry size < 20 (entry starts at 0x2C: app_id(4), offset(4), size(4)).
        let mut small_entry = valid.clone();
        small_entry[0x2C + 8..0x2C + 12].copy_from_slice(&19u32.to_le_bytes());
        assert!(parse(&small_entry).is_err());

        // Out-of-bounds offset in index entry.
        let mut bad_offset = valid.clone();
        bad_offset[0x2C + 4..0x2C + 8].copy_from_slice(&999_999u32.to_le_bytes());
        assert!(parse(&bad_offset).is_err());

        // String count mismatch (string table cursor does not reach end of string section).
        let mut bad_str_count = valid.clone();
        bad_str_count[0x18..0x1C].copy_from_slice(&1u32.to_le_bytes());
        assert!(parse(&bad_str_count).is_err());

        // Index size mismatch (index cursor does not reach end of index section).
        let mut bad_idx_count = valid;
        bad_idx_count[0x10..0x14].copy_from_slice(&0u32.to_le_bytes());
        assert!(parse(&bad_idx_count).is_err());
    }

    #[test]
    fn rejects_deeply_nested_kv_trees() {
        let mut kv = Vec::new();
        for _ in 0..70 {
            kv.push(0x00);
            kv.extend(0u32.to_le_bytes());
        }
        kv.resize(kv.len() + 70, 0x08);

        let strings = vec!["nested".to_string()];
        let mut cursor = Cursor::new(&kv);
        let mut path = Vec::new();
        let (mut name, mut ty, mut oslist) = (String::new(), String::new(), String::new());
        assert!(matches!(
            skip_or_capture(&mut cursor, &strings, &mut path, &mut name, &mut ty, &mut oslist),
            Err(AppInfoError::Truncated(_))
        ));
    }
}
