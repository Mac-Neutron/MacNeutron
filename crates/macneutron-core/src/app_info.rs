//! Reads Steam's binary `appcache/appinfo.vdf`, format v29 (keys are indices into a string table).

use std::collections::HashSet;
use std::fmt;
use std::path::Path;

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

pub fn read(path: &Path) -> Result<Vec<AppInfo>, AppInfoError> {
    let data = std::fs::read(path).map_err(|_| AppInfoError::Truncated(0))?;
    parse(&data)
}

pub fn parse(data: &[u8]) -> Result<Vec<AppInfo>, AppInfoError> {
    let mut cursor = Cursor::new(data);
    let magic = cursor.u32()?;
    if magic != MAGIC_V29 {
        return Err(AppInfoError::UnsupportedFormat(magic));
    }
    let _universe = cursor.u32()?;
    let table_offset = cursor.u64()? as usize;
    let strings = string_table(data, table_offset)?;

    let mut apps = Vec::new();
    loop {
        let app_id = cursor.u32()?;
        if app_id == 0 {
            break;
        }
        let size = cursor.u32()? as usize;
        let end = cursor.offset + size;
        if end > cursor.bytes.len() {
            return Err(AppInfoError::Truncated(cursor.offset));
        }
        // info state, last updated, PICS token, text SHA-1, change number, binary SHA-1
        cursor.skip(4 + 4 + 8 + 20 + 4 + 20)?;
        apps.push(app(app_id, &mut cursor, end, &strings)?);
        cursor.offset = end;
    }
    Ok(apps)
}

fn string_table(bytes: &[u8], at: usize) -> Result<Vec<String>, AppInfoError> {
    let mut cursor = Cursor { bytes, offset: at };
    let count = cursor.u32()? as usize;
    let mut strings = Vec::with_capacity(count);
    for _ in 0..count {
        strings.push(cursor.c_string()?);
    }
    Ok(strings)
}

/// Walks the binary KeyValues tree, keeping only `<root>/common/{name,type,oslist}`.
fn app(app_id: u32, cursor: &mut Cursor, end: usize, strings: &[String]) -> Result<AppInfo, AppInfoError> {
    let mut path: Vec<String> = Vec::new();
    let (mut name, mut ty, mut oslist) = (String::new(), String::new(), String::new());
    while cursor.offset < end {
        let value_type = cursor.u8()?;
        if value_type == 0x08 {
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
        let key = strings[key_index].clone();
        match value_type {
            0x00 => path.push(key),
            0x01 => {
                let value = cursor.c_string()?;
                if path.len() == 2 && path[1] == "common" {
                    match key.as_str() {
                        "name" => name = value,
                        "type" => ty = value.to_lowercase(),
                        "oslist" => oslist = value,
                        _ => {}
                    }
                }
            }
            0x02 | 0x03 | 0x04 | 0x06 => cursor.skip(4)?,
            0x07 | 0x0A => cursor.skip(8)?,
            other => return Err(AppInfoError::UnknownValueType(other, cursor.offset - 5)),
        }
    }
    let platforms: HashSet<String> = oslist
        .split(',')
        .map(|s| s.trim().to_lowercase())
        .filter(|s| !s.is_empty())
        .collect();
    Ok(AppInfo { app_id, name, r#type: ty, oslist: platforms })
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
        if self.offset + count > self.bytes.len() {
            return Err(AppInfoError::Truncated(self.offset));
        }
        self.offset += count;
        Ok(())
    }

    fn u8(&mut self) -> Result<u8, AppInfoError> {
        self.skip(1)?;
        Ok(self.bytes[self.offset - 1])
    }

    fn u32(&mut self) -> Result<u32, AppInfoError> {
        self.skip(4)?;
        Ok((0..4).fold(0u32, |acc, i| acc | (self.bytes[self.offset - 4 + i] as u32) << (8 * i)))
    }

    fn u64(&mut self) -> Result<u64, AppInfoError> {
        self.skip(8)?;
        Ok((0..8).fold(0u64, |acc, i| acc | (self.bytes[self.offset - 8 + i] as u64) << (8 * i)))
    }

    fn c_string(&mut self) -> Result<String, AppInfoError> {
        let rest = &self.bytes[self.offset..];
        let terminator = rest
            .iter()
            .position(|&b| b == 0)
            .ok_or(AppInfoError::Truncated(self.offset))?;
        let s = String::from_utf8_lossy(&rest[..terminator]).into_owned();
        self.offset = self.offset + terminator + 1;
        Ok(s)
    }
}
