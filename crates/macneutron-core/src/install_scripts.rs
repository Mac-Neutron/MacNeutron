//! Steam's install scripts for one game in
//! the VM (spec 2026-10-24-macneutron-install-scripts-design §4.1): the scripts Steam's app manifests list for the game
//! and the shared redistributables it uses, as run-game.sh runs them in the game's prefix before the game. Steam's rules
//! are the Steamworks installscripts documentation's; Mac Steam never runs them itself (bridge spec D-16).
//! ponytail: no language blocks, `process 2+`, `%ROOTDRIVE%` or other Requirement_OS keys: each is a note, until a game
//! needs it.

use std::path::Path;

use crate::key_values::{node_at, KVNode, KeyValues};
use crate::micro_vm::MicroVM;

#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct InstallScripts {
    /// The Registry entries as a "Windows Registry Editor Version 5.00" text, "" when none.
    pub registry: String,
    pub runs: Vec<Run>,
    /// One per thing skipped, for launcher.log.
    pub notes: Vec<String>,
}

/// A "Run Process" entry: run-game.sh's mn_run runs `process` in `folder` unless `key`'s DWORD `name` is at least
/// `minimum`, and writes `minimum` there after exit 0.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Run {
    pub key: String,
    pub name: String,
    pub minimum: i64,
    /// The process's folder in the guest, a Unix path: its working directory.
    pub folder: String,
    /// Its Windows path, ending .exe, .cmd, .bat or .msi.
    pub process: String,
    pub arguments: Vec<String>,
}

impl Run {
    /// The prefix's registry file that keeps `key`, and `key` (from its hive down) and `name` as Wine writes them
    /// there: what run-game.sh's mn_hasrun looks for, with fixed strings.
    pub fn file(&self) -> String {
        if self.key.starts_with("HKEY_LOCAL_MACHINE\\") { "system.reg".to_string() } else { "user.reg".to_string() }
    }

    pub fn file_key(&self) -> String {
        let rest = self.key.split_once('\\').map(|(_, rest)| rest).unwrap_or("");
        InstallScripts::wine_escaped(rest, "[]")
    }

    pub fn file_name(&self) -> String {
        format!("\"{}\"", InstallScripts::wine_escaped(&self.name, "\""))
    }
}

impl InstallScripts {
    /// `registry` as run-game.sh imports it (the run share's installscript.reg): UTF-16LE with its byte-order mark,
    /// which Wine's reg import reads whatever the characters (a REGEDIT4 file it reads in the ANSI code page).
    pub fn registry_file(&self) -> Vec<u8> {
        let mut out = vec![0xFF, 0xFE];
        for unit in self.registry.encode_utf16() {
            out.push((unit & 0xFF) as u8);
            out.push((unit >> 8) as u8);
        }
        out
    }

    /// The runs as the guest's launch script has them, in front of run-game.sh: mn_install_script's mn_run calls,
    /// every word single-quoted (nothing from a script is parsed as shell). "" without runs (an empty function is a
    /// syntax error); the registry goes as a file (`registry_file`).
    pub fn shell(&self) -> String {
        if self.runs.is_empty() {
            return String::new();
        }
        let mut out = String::from("mn_install_script() {\n");
        for run in &self.runs {
            let mut words: Vec<String> = vec![
                run.key.clone(),
                run.name.clone(),
                run.minimum.to_string(),
                run.file(),
                run.file_key(),
                run.file_name(),
                run.folder.clone(),
                run.process.clone(),
            ];
            words.extend(run.arguments.iter().cloned());
            let quoted: Vec<String> = words.iter().map(|word| MicroVM::quoted(word)).collect();
            out += &format!("  mn_run {}\n", quoted.join(" "));
        }
        out + "}\n"
    }

    /// The game's scripts and those of the shared depots it uses (dependencies first; Steam's order is undocumented).
    /// Nothing for a game without an app manifest in `steamapps` (a launch from outside Steam). `game_folder`, the
    /// folder the game was launched from, is its own scripts' folder and %INSTALLDIR% (the share that's writable in the
    /// VM), whatever its manifest's `installdir` says.
    pub fn plan(steamapps: &Path, app_id: &str, game_folder: Option<&str>) -> InstallScripts {
        let mut result = InstallScripts::default();
        let Some(mut game) = Manifest::new(steamapps, app_id) else { return result };
        if let Some(folder) = game_folder {
            game.installdir = folder.to_string();
        }
        let mut scripts: Vec<(Manifest, String)> = Vec::new();
        let mut owners: std::collections::HashMap<String, Option<Manifest>> = std::collections::HashMap::new();
        for (depot, owner) in &game.shared {
            if !owners.contains_key(owner) {
                owners.insert(owner.clone(), Manifest::new(steamapps, owner));
            }
            let Some(Some(app)) = owners.get(owner) else {
                result.notes.push(format!("shared depot {depot}: app {owner} isn't installed in this library"));
                continue;
            };
            if let Some(script) = app.scripts.iter().find(|(d, _)| d == depot) {
                scripts.push((app.clone(), script.1.clone()));
            }
        }
        scripts.extend(game.scripts.iter().map(|s| (game.clone(), s.1.clone())));
        let mut blocks: Vec<String> = Vec::new();
        let mut seen: std::collections::HashSet<String> = std::collections::HashSet::new();
        for (app, path) in &scripts {
            result.add(path, app, steamapps, &mut blocks, &mut seen);
        }
        if !blocks.is_empty() {
            result.registry = "Windows Registry Editor Version 5.00\n\n".to_string() + &blocks.join("\n");
        }
        result
    }

    fn add(
        &mut self,
        path: &str,
        app: &Manifest,
        steamapps: &Path,
        blocks: &mut Vec<String>,
        seen: &mut std::collections::HashSet<String>,
    ) {
        let name = app.installdir.clone() + "/" + &path.replace('\\', "/");
        let file = steamapps.join(format!("common/{name}"));
        if !file.exists() {
            self.notes.push(format!("{name}: not found"));
            return;
        }
        // UTF-8, else a Windows code page's (Steam's parser takes bytes as they come)
        let mut text = match read_utf8_or_cp1252(&file) {
            Some(text) => text,
            None => {
                self.notes.push(format!("{name}: unreadable (neither UTF-8 nor Windows-1252)"));
                return;
            }
        };
        if text.starts_with('\u{FEFF}') {
            text.remove(0);
        }
        let Some(root) = KeyValues::parse(&text).ok().and_then(|nodes| node_at(&nodes, &["InstallScript"])) else {
            self.notes.push(format!("{name}: not an install script Steam's parser reads"));
            return;
        };
        for section in root.children() {
            match section.key.to_lowercase().as_str() {
                "registry" => {
                    for key in section.children() {
                        self.add_registry(key, &name, &app.installdir, blocks);
                    }
                }
                "run process" => {
                    for entry in section.children() {
                        self.run(entry, &name, &app.installdir, seen);
                    }
                }
                "firewall" => self.notes.push(format!("{name}: {}: not applied (Wine has no firewall)", section.key)),
                _ => self.notes.push(format!("{name}: {}: not applied", section.key)),
            }
        }
    }

    fn run(
        &mut self,
        entry: &KVNode,
        script: &str,
        installdir: &str,
        seen: &mut std::collections::HashSet<String>,
    ) {
        let at = format!("{script}: {}", entry.key);
        let value = |key: &str| -> Option<String> { node_at(entry.children(), &[key]).and_then(|n| n.string_value()) };
        let Some(has_run_key) = value("HasRunKey") else {
            self.notes.push(format!("{at}: no HasRunKey"));
            return;
        };
        let Some(key) = Self::registry_key(&has_run_key) else {
            self.notes.push(format!("{at}: HasRunKey {has_run_key} isn't HKLM or HKCU"));
            return;
        };
        if value("process 2").is_some() {
            self.notes.push(format!("{at}: has a process 2"));
            return;
        }
        let Some(process1) = value("process 1") else {
            self.notes.push(format!("{at}: no process 1"));
            return;
        };
        if let Some(requirements) = node_at(entry.children(), &["Requirement_OS"]) {
            for requirement in requirements.children() {
                let wanted = requirement.string_value().unwrap_or_default();
                match requirement.key.to_lowercase().as_str() {
                    // the VM prefix is 64-bit
                    "is64bitwindows" => {
                        if wanted != "1" {
                            return;
                        }
                    }
                    // what our Wine reports
                    "ostype" => {
                        if wanted.to_lowercase() != "windows 10" {
                            return;
                        }
                    }
                    _ => {
                        self.notes.push(format!("{at}: Requirement_OS {} isn't understood", requirement.key));
                        return;
                    }
                }
            }
        }
        let (process, unknown) = Self::expand(&process1, installdir);
        let Some(process) = process else {
            self.notes.push(format!("{at}: unknown token {}", unknown.unwrap_or_default()));
            return;
        };
        let (command, unknown) = Self::expand(&value("command 1").unwrap_or_default(), installdir);
        let Some(command) = command else {
            self.notes.push(format!("{at}: unknown token {}", unknown.unwrap_or_default()));
            return;
        };
        let ext = ".".to_string()
            + &process.rsplit_once('.').map(|(_, last)| last.to_lowercase()).unwrap_or_default();
        if ![".exe", ".cmd", ".bat", ".msi"].contains(&ext.as_str()) {
            self.notes.push(format!("{at}: a {ext} isn't run (only .exe, .cmd, .bat and .msi)"));
            return;
        }
        let Some(folder) = Self::folder(&process) else {
            self.notes.push(format!("{at}: {process} isn't on C: or Z:"));
            return;
        };
        let name = KeyValues::unescape(&entry.key);
        // the first one counts
        if !seen.insert(key.to_lowercase() + "\n" + &name.to_lowercase()) {
            return;
        }
        let minimum = value("MinimumHasRunValue").and_then(|v| v.parse::<i64>().ok()).unwrap_or(1);
        self.runs.push(Run {
            key,
            name,
            minimum,
            folder,
            process,
            arguments: Self::arguments(&command),
        });
    }

    fn add_registry(&mut self, node: &KVNode, script: &str, installdir: &str, blocks: &mut Vec<String>) {
        let path = KeyValues::unescape(&node.key);
        if path.contains(['\n', '\r']) {
            self.notes.push(format!("{script}: a key on several lines isn't applied"));
            return;
        }
        let Some(key) = Self::registry_key(&path) else {
            self.notes.push(format!("{script}: {path} isn't HKLM or HKCU"));
            return;
        };
        let mut lines: Vec<String> = Vec::new();
        for value_type in node.children() {
            let kind = value_type.key.to_lowercase();
            if kind != "string" && kind != "dword" {
                self.notes.push(format!("{script}: {path}: {} values aren't applied", value_type.key));
                continue;
            }
            for item in value_type.children() {
                let at = format!("{script}: {path}: {}", item.key);
                let Some(raw) = item.string_value() else {
                    self.notes.push(format!("{at}: a language block isn't applied"));
                    continue;
                };
                let name = KeyValues::unescape(&item.key);
                let (value, unknown) = Self::expand(&raw, installdir);
                let Some(value) = value else {
                    self.notes.push(format!("{at}: unknown token {}", unknown.unwrap_or_default()));
                    continue;
                };
                if (name.clone() + &value).contains(['\n', '\r']) {
                    self.notes.push(format!("{at}: a value on several lines isn't applied"));
                    continue;
                }
                let left = if name == "(Default)" { "@".to_string() } else { Self::reg_quoted(&name) };
                if kind == "string" {
                    lines.push(left + "=" + &Self::reg_quoted(&value));
                } else if let Ok(number) = value.parse::<u32>() {
                    lines.push(format!("{left}=dword:{number:08x}"));
                } else {
                    self.notes.push(format!("{at}: {value} isn't a DWORD"));
                }
            }
        }
        if !lines.is_empty() {
            let body: String = lines.iter().map(|line| line.clone() + "\n").collect();
            blocks.push(format!("[{key}]\n{body}"));
        }
    }

    /// A key or HasRunKey as the VM prefix has it: the hive's full name, and HKLM\SOFTWARE in the 32-bit view
    /// (Wow6432Node), where Steam writes (Steamworks documentation) and Proton's prefix has its CommonRedist marks.
    /// None for another hive.
    pub fn registry_key(path: &str) -> Option<String> {
        let (hive, rest) = path.split_once('\\')?;
        let lower = rest.to_lowercase();
        match hive.to_uppercase().as_str() {
            "HKEY_CURRENT_USER" | "HKCU" => Some("HKEY_CURRENT_USER\\".to_string() + rest),
            "HKEY_LOCAL_MACHINE" | "HKLM" => {
                if lower.starts_with("software\\wow6432node\\") || !lower.starts_with("software\\") {
                    Some("HKEY_LOCAL_MACHINE\\".to_string() + rest)
                } else {
                    Some(
                        "HKEY_LOCAL_MACHINE\\Software\\Wow6432Node\\".to_string()
                            + &rest["software\\".len()..],
                    )
                }
            }
            _ => None,
        }
    }

    /// The documented tokens, as the VM prefix has them (spec §4.1); `%INSTALLDIR%` is the script's app's folder as
    /// the game itself sees it (run-game.sh's EXE: the VM prefix has no S: drive).
    fn tokens(installdir: &str) -> Vec<(&'static str, String)> {
        vec![
            ("%INSTALLDIR%", "Z:\\mnt\\games\\".to_string() + &installdir.replace('/', "\\")),
            ("%STEAMPATH%", "C:\\Program Files (x86)\\Steam".to_string()),
            ("%WinDir%", "C:\\windows".to_string()),
            ("%APPDATA%", "C:\\users\\steamuser\\AppData\\Roaming".to_string()),
            ("%LOCALAPPDATA%", "C:\\users\\steamuser\\AppData\\Local".to_string()),
            ("%USER_MYDOCS%", "C:\\users\\steamuser\\Documents".to_string()),
            ("%COMMON_MYDOCS%", "C:\\users\\Public\\Documents".to_string()),
        ]
    }

    /// `text` with its tokens replaced, or None and the first token the table lacks.
    fn expand(text: &str, installdir: &str) -> (Option<String>, Option<String>) {
        let mut out = text.to_string();
        for (token, value) in Self::tokens(installdir) {
            out = replace_case_insensitive(&out, token, &value);
        }
        match first_unknown_token(&out) {
            Some(unknown) => (None, Some(unknown)),
            None => (Some(out), None),
        }
    }

    /// A Windows path's folder in the guest: `Z:\a\b\x.exe` is /a/b, `C:\a\x.exe` the prefix's drive_c/a, and a
    /// bare program (msiexec.exe), which Windows finds in its system folder, system32.
    fn folder(process: &str) -> Option<String> {
        let Some(cut) = process.rfind('\\') else {
            return Some("/home/mn/pfx/drive_c/windows/system32".to_string());
        };
        let folder = &process[..cut];
        let rest = &folder[2.min(folder.len())..];
        let rest = rest.replace('\\', "/");
        let prefix = folder.get(..2).unwrap_or("").to_lowercase();
        match prefix.as_str() {
            "z:" => Some(if rest.is_empty() { "/".to_string() } else { rest }),
            "c:" => Some("/home/mn/pfx/drive_c".to_string() + &rest),
            _ => None,
        }
    }

    /// A Windows command line's arguments, as CommandLineToArgvW splits them: whitespace outside quotes separates,
    /// `"` toggles quoting; 2n backslashes before a `"` are n and a toggle, 2n+1 are n and a literal `"`.
    pub fn arguments(command: &str) -> Vec<String> {
        let mut result: Vec<String> = Vec::new();
        let mut current = String::new();
        let mut started = false;
        let mut quoted = false;
        let mut backslashes = 0usize;
        for character in command.chars() {
            if character == '\\' {
                backslashes += 1;
                started = true;
                continue;
            }
            if character == '"' {
                current += &"\\".repeat(backslashes / 2);
                if backslashes % 2 == 1 {
                    current.push('"');
                } else {
                    quoted = !quoted;
                }
                backslashes = 0;
                started = true;
                continue;
            }
            current += &"\\".repeat(backslashes);
            backslashes = 0;
            if (character == ' ' || character == '\t') && !quoted {
                if started {
                    result.push(current.clone());
                }
                current.clear();
                started = false;
            } else {
                current.push(character);
                started = true;
            }
        }
        current += &"\\".repeat(backslashes);
        if started {
            result.push(current);
        }
        result
    }

    /// A name as Wine's server writes it in the prefix's registry files (server/registry.c dump_strW): `\` and the
    /// `escaping` characters get a backslash, a control character its C or octal escape, anything past ASCII \x and
    /// its UTF-16 unit in hex, four digits when an ASCII hex digit follows.
    pub fn wine_escaped(text: &str, escaping: &str) -> String {
        let units: Vec<u16> = text.encode_utf16().collect();
        let controls: Vec<char> = ".......abtnvfr.............e....".chars().collect();
        let mut out = String::new();
        for (index, unit) in units.iter().enumerate() {
            let next = units.get(index + 1).copied();
            let is_next = |test: &dyn Fn(char) -> bool| -> bool {
                next.map(|n| n < 128 && test(char::from_u32(n as u32).unwrap_or('\0'))).unwrap_or(false)
            };
            if *unit > 127 {
                if is_next(&|c| c.is_ascii_hexdigit()) {
                    out += &format!("\\x{unit:04x}");
                } else {
                    out += &format!("\\x{unit:x}");
                }
            } else if *unit < 32 {
                let control = controls[*unit as usize];
                if control != '.' {
                    out.push('\\');
                    out.push(control);
                } else if is_next(&|c| ('0'..='7').contains(&c)) {
                    out += &format!("\\{:03o}", *unit);
                } else {
                    out += &format!("\\{:o}", *unit);
                }
            } else {
                let character = char::from_u32(*unit as u32).unwrap_or('\0');
                if character == '\\' || escaping.contains(character) {
                    out.push('\\');
                }
                out.push(character);
            }
        }
        out
    }

    /// A name or string as a .reg file writes it.
    fn reg_quoted(text: &str) -> String {
        format!("\"{}\"", text.replace('\\', "\\\\").replace('"', "\\\""))
    }
}

/// An app's `appmanifest_<id>.acf`: its folder in steamapps/common, its scripts by depot, its shared depots' owners.
#[derive(Debug, Clone, PartialEq, Eq)]
struct Manifest {
    installdir: String,
    scripts: Vec<(String, String)>,
    shared: Vec<(String, String)>,
}

impl Manifest {
    fn new(steamapps: &Path, app_id: &str) -> Option<Manifest> {
        let text = std::fs::read_to_string(steamapps.join(format!("appmanifest_{app_id}.acf"))).ok()?;
        let state = KeyValues::parse(&text).ok().and_then(|nodes| node_at(&nodes, &["AppState"]))?;
        let installdir = node_at(state.children(), &["installdir"])?.string_value()?;
        let pairs = |block: &str| -> Vec<(String, String)> {
            node_at(state.children(), &[block])
                .map(|node| {
                    node.children()
                        .iter()
                        .filter_map(|child| {
                            child.string_value().map(|value| (KeyValues::unescape(&child.key), value))
                        })
                        .collect()
                })
                .unwrap_or_default()
        };
        let scripts = pairs("InstallScripts");
        let shared = pairs("SharedDepots");
        Some(Manifest { installdir, scripts, shared })
    }
}

/// UTF-8, else Windows-1252, mirroring Steam taking bytes as they come. None when neither decode holds.
fn read_utf8_or_cp1252(path: &Path) -> Option<String> {
    let bytes = std::fs::read(path).ok()?;
    if let Ok(text) = std::str::from_utf8(&bytes) {
        return Some(text.to_string());
    }
    bytes.iter().map(|&b| cp1252_char(b)).collect()
}

/// Windows-1252's single-byte mapping: 0x80..0x9F differ from Latin-1, the rest are U+0000..=U+00FF. None for the
/// five bytes the code page leaves undefined (0x81, 0x8D, 0x8F, 0x90, 0x9D), which Foundation's decoder refuses.
fn cp1252_char(byte: u8) -> Option<char> {
    const HIGH: [u16; 32] = [
        0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
        0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E,
        0x0178,
    ];
    match byte {
        0x80..=0x9F => Some(HIGH[(byte - 0x80) as usize]).filter(|&c| c != 0).and_then(|c| char::from_u32(c.into())),
        _ => Some(char::from(byte)),
    }
}

/// Case-insensitive replace, as Swift's `replacingOccurrences(of:options:.caseInsensitive)` for ASCII tokens.
fn replace_case_insensitive(text: &str, from: &str, to: &str) -> String {
    if from.is_empty() {
        return text.to_string();
    }
    let haystack = text.to_lowercase();
    let needle = from.to_lowercase();
    let mut out = String::new();
    let mut index = 0usize;
    let hay_bytes = haystack.as_bytes();
    let needle_bytes = needle.as_bytes();
    while index <= haystack.len() {
        if index + needle_bytes.len() <= hay_bytes.len()
            && &hay_bytes[index..index + needle_bytes.len()] == needle_bytes
            && text.is_char_boundary(index)
            && text.is_char_boundary(index + from.len())
        {
            out.push_str(to);
            index += from.len();
        } else {
            match text[index..].chars().next() {
                Some(c) => {
                    out.push(c);
                    index += c.len_utf8();
                }
                None => break,
            }
        }
    }
    out
}

/// The first `%TOKEN%` remaining in `text`, if any (only letters and underscores between the percent signs).
fn first_unknown_token(text: &str) -> Option<String> {
    let bytes = text.as_bytes();
    let mut index = 0usize;
    while index < bytes.len() {
        if bytes[index] == b'%' {
            let start = index;
            let mut end = index + 1;
            while end < bytes.len() && (bytes[end].is_ascii_alphabetic() || bytes[end] == b'_') {
                end += 1;
            }
            if end > start + 1 && end < bytes.len() && bytes[end] == b'%' {
                return Some(text[start..=end].to_string());
            }
        }
        index += 1;
    }
    None
}
#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_support::{make_temp_dir, write};

    const SHARED_MANIFEST: &str = r#""AppState"
{
    "appid"        "228980"
    "installdir"        "Steamworks Shared"
    "InstallScripts"
    {
        "228984"        "_CommonRedist\\vcredist\\2012\\installscript.vdf"
        "228988"        "_CommonRedist\\vcredist\\2019\\installscript.vdf"
        "228990"        "_CommonRedist\\DirectX\\Jun2010\\installscript.vdf"
        "229004"        "_CommonRedist\\DotNet\\4.5.2\\installscript.vdf"
    }
}"#;

    const VC2012: &str = r#""InstallScript"
{
    "Run Process"
    {
        "x86 Update 4"
        {
            "HasRunKey"        "HKEY_LOCAL_MACHINE\\Software\\Valve\\Steam\\Apps\\CommonRedist\\vcredist\\2012"
            "process 1"        "%INSTALLDIR%\\_CommonRedist\\vcredist\\2012\\vcredist_x86.exe"
            "command 1"        "/quiet /norestart"
            "NoCleanUp"        "1"
        }
        "x64 Update 4"
        {
            "HasRunKey"        "HKEY_LOCAL_MACHINE\\Software\\Valve\\Steam\\Apps\\CommonRedist\\vcredist\\2012"
            "process 1"        "%INSTALLDIR%\\_CommonRedist\\vcredist\\2012\\vcredist_x64.exe"
            "command 1"        "/quiet /norestart"
            "NoCleanUp"        "1"
            "Requirement_OS"
            {
                "Is64BitWindows"        "1"
            }
        }
    }
}
"kvsignatures"
{
    "InstallScript"        "579ba5cacfb9d7f10af81f98c13407ca"
}"#;

    const VC2019: &str = r#""InstallScript"
{
    "Run Process"
    {
        "x64 14.28.29334.0"
        {
            "HasRunKey"        "HKEY_LOCAL_MACHINE\\Software\\Valve\\Steam\\Apps\\CommonRedist\\vcredist\\2019"
            "process 1"        "%INSTALLDIR%\\_CommonRedist\\vcredist\\2019\\Microsoft Visual C++ 2019 x64.cmd"
        }
    }
}"#;

    const DIRECT_X: &str = r#""installscript"
{
    "Run Process"
    {
        "dxsetup"
        {
            "hasrunkey"        "HKEY_LOCAL_MACHINE\\Software\\Valve\\Steam\\Apps\\CommonRedist\\DirectX\\Jun2010"
            "process 1"        "%INSTALLDIR%\\_CommonRedist\\DirectX\\Jun2010\\DXSETUP.exe"
            "command 1"        "/silent"
            "nocleanup"        "1"
        }
    }
}"#;

    const DOT_NET_452: &str = r#""InstallScript"
{
    "Run Process"
    {
        "4.5.2"
        {
            "hasrunkey"        "HKEY_LOCAL_MACHINE\\Software\\Valve\\Steam\\Apps\\CommonRedist\\.NET\\4.5.2"
            "process 1"        "%INSTALLDIR%\\_CommonRedist\\DotNet\\4.5.2\\noop.cmd"
            "nocleanup"        "1"
            "requirement_os"
            {
                "OSType"        "Windows XP"
            }
        }
        "4.5.2"
        {
            "hasrunkey"        "HKEY_LOCAL_MACHINE\\Software\\Valve\\Steam\\Apps\\CommonRedist\\.NET\\4.5.2"
            "process 1"        "%INSTALLDIR%\\_CommonRedist\\DotNet\\4.5.2\\Microsoft .NET Framework 4.5.2.cmd"
            "nocleanup"        "1"
        }
    }
}"#;

    const THREE_D_MARK_REGISTRY: &str = r#""InstallScript"
{
    "Registry"
    {
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\Futuremark\\3DMark"
        {
            "string"
            {
                "Install_Path"        "%INSTALLDIR%"
            }
        }
    }
}
"kvsignatures"
{
    "InstallScript"        "579ba5cacfb9d7f10af81f98c13407ca"
}"#;

    const SYSTEM_INFO: &str = r#""InstallScript"
{
    "Run Process"
    {
        "FmSysInfo"
        {
            "HasRunKey"        "HKEY_CURRENT_USER\\SOFTWARE\\Valve\\Steam\\Apps\\231350"
            "Process 1"        "%INSTALLDIR%\\SystemInfoSetupAssistant.exe"
            "Command 1"        "Futuremark_SystemInfo_5_93_1517.msi"
            "MinimumHasRunValue"        "1517"
        }
    }
}"#;

    const UNSUPPORTED: &str = r#""InstallScript"
{
    "Firewall"
    {
        "Game"        "%INSTALLDIR%\\Game.exe"
    }
    "Run Process On Uninstall"
    {
        "x"
        {
            "process 1"        "%INSTALLDIR%\\uninstall.exe"
        }
    }
    "Run Process"
    {
        "nokey"
        {
            "process 1"        "%INSTALLDIR%\\a.exe"
        }
        "two"
        {
            "HasRunKey"        "HKEY_LOCAL_MACHINE\\Software\\T"
            "process 1"        "%INSTALLDIR%\\a.exe"
            "process 2"        "%INSTALLDIR%\\b.exe"
        }
        "drive"
        {
            "HasRunKey"        "HKEY_LOCAL_MACHINE\\Software\\T"
            "process 1"        "%ROOTDRIVE%\\a.exe"
        }
        "powershell"
        {
            "HasRunKey"        "HKEY_LOCAL_MACHINE\\Software\\T"
            "process 1"        "%INSTALLDIR%\\setup.ps1"
        }
    }
    "Registry"
    {
        "HKEY_LOCAL_MACHINE\\Software\\Game"
        {
            "string"
            {
                "english"
                {
                    "Language"        "en"
                }
            }
        }
    }
}
"#;

    const REG_ESCAPES: &str = r#""InstallScript"
{
    "Registry"
    {
        "HKEY_CURRENT_USER\\Software\\Game"
        {
            "string"
            {
                "v"        "a\"b\\c"
                "(Default)"        "x"
                "broken"        "two
nlines"
            }
            "dword"
            {
                "d"        "1517"
                "bad"        "x12"
            }
        }
    }
}
"#;

    const KEY_MULTI_LINE: &str = r#""InstallScript" { "Registry" { "HKEY_CURRENT_USER\\Software\\a
b" { "string" { "v" "1" } } } }"#;


    fn game_manifest(app_id: &str, installdir: &str, scripts: &[(&str, &str)], shared: &[&str]) -> String {
        let mut out = format!("\"AppState\"\n{{\n\t\"appid\"\t\t\"{app_id}\"\n\t\"installdir\"\t\t\"{installdir}\"\n");
        out += "\t\"InstallScripts\"\n\t{\n";
        for (depot, path) in scripts {
            out += &format!("\t\t\"{depot}\"\t\t\"{path}\"\n");
        }
        out += "\t}\n";
        out += "\t\"SharedDepots\"\n\t{\n";
        for depot in shared {
            out += &format!("\t\t\"{depot}\"\t\t\"228980\"\n");
        }
        out += "\t}\n}\n";
        out
    }

    fn three_d_mark_library(shared: bool) -> std::path::PathBuf {
        let steamapps = make_temp_dir().join("steamapps");
        write(
            &game_manifest("231350", "3DMark Demo", &[("223851", "installscript.vdf"), ("231351", "installscript_si.vdf")], &["228984", "228990", "229004"]),
            &steamapps.join("appmanifest_231350.acf"),
            false,
        );
        write(THREE_D_MARK_REGISTRY, &steamapps.join("common/3DMark Demo/installscript.vdf"), false);
        write(SYSTEM_INFO, &steamapps.join("common/3DMark Demo/installscript_si.vdf"), false);
        if shared {
            write(SHARED_MANIFEST, &steamapps.join("appmanifest_228980.acf"), false);
            let redist = steamapps.join("common/Steamworks Shared/_CommonRedist");
            write(VC2012, &redist.join("vcredist/2012/installscript.vdf"), false);
            write(VC2019, &redist.join("vcredist/2019/installscript.vdf"), false);
            write(DIRECT_X, &redist.join("DirectX/Jun2010/installscript.vdf"), false);
            write(DOT_NET_452, &redist.join("DotNet/4.5.2/installscript.vdf"), false);
        }
        steamapps
    }

    fn plan(script: &str) -> InstallScripts {
        let steamapps = make_temp_dir().join("steamapps");
        write(&game_manifest("42", "Game", &[("43", "installscript.vdf")], &[]), &steamapps.join("appmanifest_42.acf"), false);
        write(script, &steamapps.join("common/Game/installscript.vdf"), false);
        InstallScripts::plan(&steamapps, "42", None)
    }

    fn run_script(entries: &str) -> String {
        format!("\"InstallScript\"\n{{\n\"Run Process\"\n{{\n{entries}\n}}\n}}\n")
    }

    const REDIST: &str = r"Z:\mnt\games\Steamworks Shared\_CommonRedist";
    const COMMON_REDIST_KEY: &str = r"HKEY_LOCAL_MACHINE\Software\Wow6432Node\Valve\Steam\Apps\CommonRedist";

    #[test]
    fn three_d_marks_scripts_become_its_plan() {
        let result = InstallScripts::plan(&three_d_mark_library(true), "231350", None);
        let vc = "/mnt/games/Steamworks Shared/_CommonRedist/vcredist/2012";
        let vc_key = format!("{COMMON_REDIST_KEY}\\vcredist\\2012");
        let mut expected: Vec<Run> = Vec::new();
        expected.push(Run { key: vc_key.clone(), name: "x86 Update 4".into(), minimum: 1, folder: vc.into(), process: format!("{REDIST}\\vcredist\\2012\\vcredist_x86.exe"), arguments: vec!["/quiet".into(), "/norestart".into()] });
        expected.push(Run { key: vc_key, name: "x64 Update 4".into(), minimum: 1, folder: vc.into(), process: format!("{REDIST}\\vcredist\\2012\\vcredist_x64.exe"), arguments: vec!["/quiet".into(), "/norestart".into()] });
        expected.push(Run { key: format!("{COMMON_REDIST_KEY}\\DirectX\\Jun2010"), name: "dxsetup".into(), minimum: 1, folder: "/mnt/games/Steamworks Shared/_CommonRedist/DirectX/Jun2010".into(), process: format!("{REDIST}\\DirectX\\Jun2010\\DXSETUP.exe"), arguments: vec!["/silent".into()] });
        expected.push(Run { key: format!("{COMMON_REDIST_KEY}\\.NET\\4.5.2"), name: "4.5.2".into(), minimum: 1, folder: "/mnt/games/Steamworks Shared/_CommonRedist/DotNet/4.5.2".into(), process: format!("{REDIST}\\DotNet\\4.5.2\\Microsoft .NET Framework 4.5.2.cmd"), arguments: vec![] });
        expected.push(Run { key: r"HKEY_CURRENT_USER\SOFTWARE\Valve\Steam\Apps\231350".into(), name: "FmSysInfo".into(), minimum: 1517, folder: "/mnt/games/3DMark Demo".into(), process: r"Z:\mnt\games\3DMark Demo\SystemInfoSetupAssistant.exe".into(), arguments: vec!["Futuremark_SystemInfo_5_93_1517.msi".into()] });
        assert_eq!(result.runs, expected);
        assert_eq!(result.registry, "Windows Registry Editor Version 5.00\n\n[HKEY_LOCAL_MACHINE\\Software\\Wow6432Node\\Futuremark\\3DMark]\n\"Install_Path\"=\"Z:\\\\mnt\\\\games\\\\3DMark Demo\"\n");
        assert_eq!(result.notes, Vec::<String>::new());
    }
    #[test]
    fn keys_match_in_any_case() {
        let build = |keys: &[&str]| -> String {
            let mut out = String::new();
            out += &format!("\"{}\"\n{{\n", keys[0]);
            out += &format!("\"{}\"\n{{\n", keys[1]);
            out += "\"x64\"\n{\n";
            out += &format!("\"{}\"  \"HKEY_LOCAL_MACHINE\\\\Software\\\\T\"\n", keys[2]);
            out += &format!("\"{}\"  \"%INSTALLDIR%\\\\setup.exe\"\n", keys[3]);
            out += &format!("\"{}\"  \"/q\"\n", keys[4]);
            out += &format!("\"{}\"\n{{\n\"Is64BitWindows\"  \"1\"\n}}\n}}\n}}\n}}\n", keys[5]);
            out
        };
        let lower = plan(&build(&["installscript", "run process", "hasrunkey", "process 1", "command 1", "requirement_os"]));
        let upper = plan(&build(&["InstallScript", "Run Process", "HasRunKey", "Process 1", "Command 1", "Requirement_OS"]));
        assert_eq!(lower.runs.len(), 1);
        assert_eq!(lower.runs, upper.runs);
        assert_eq!(lower.runs.first().map(|r| r.process.as_str()), Some(r"Z:\mnt\games\Game\setup.exe"));
    }

    #[test]
    fn an_entry_for_another_windows_is_dropped() {
        let entry = |name: &str, requirement: &str| -> String {
            format!("\"{name}\"\n{{\n\"HasRunKey\" \"HKEY_LOCAL_MACHINE\\\\Software\\\\T\"\n\"process 1\" \"%INSTALLDIR%\\\\{name}.exe\"\n\"Requirement_OS\"\n{{\n{requirement}\n}}\n}}\n")
        };
        let script = run_script(&format!("{}{}{}{}",
            entry("bits32", "\"Is64BitWindows\" \"0\""),
            entry("xp", "\"OSType\" \"Windows XP\""),
            entry("server", "\"IsServer\" \"1\""),
            entry("ten", "\"OSType\" \"windows 10\"")));
        let result = plan(&script);
        assert_eq!(result.runs.iter().map(|r| r.name.as_str()).collect::<Vec<_>>(), vec!["ten"]);
        assert_eq!(result.notes.len(), 1);
        assert!(result.notes[0].contains("IsServer"));
    }

    #[test]
    fn the_first_entry_per_has_run_value_wins() {
        let entry = |exe: &str| -> String {
            format!("\"4.5.2\"\n{{\n\"hasrunkey\" \"HKEY_LOCAL_MACHINE\\\\Software\\\\T\"\n\"process 1\" \"%INSTALLDIR%\\\\{exe}\"\n}}\n")
        };
        let result = plan(&run_script(&format!("{}{}", entry("first.exe"), entry("second.exe"))));
        assert_eq!(result.runs.iter().map(|r| r.process.as_str()).collect::<Vec<_>>(), vec![r"Z:\mnt\games\Game\first.exe"]);
    }

    #[test]
    fn what_isnt_supported_is_skipped_with_a_note() {
        let result = plan(UNSUPPORTED);
        assert_eq!(result.runs, Vec::<Run>::new());
        assert_eq!(result.registry, "");
        for word in ["Firewall", "Run Process On Uninstall", "HasRunKey", "process 2", "%ROOTDRIVE%", ".ps1", "language"] {
            assert!(result.notes.iter().any(|n| n.contains(word)), "no note for {word}: {:?}", result.notes);
        }
    }

    #[test]
    fn missing_manifests_and_scripts_are_skipped() {
        assert_eq!(InstallScripts::plan(&make_temp_dir(), "231350", None), InstallScripts::default());
        let alone = InstallScripts::plan(&three_d_mark_library(false), "231350", None);
        assert_eq!(alone.runs.iter().map(|r| r.name.as_str()).collect::<Vec<_>>(), vec!["FmSysInfo"]);
        assert!(alone.registry.contains("Install_Path"));
        assert_eq!(alone.notes.len(), 3);
        assert!(alone.notes.iter().all(|n| n.contains("228980")));
        let steamapps = three_d_mark_library(true);
        std::fs::remove_file(steamapps.join("common/3DMark Demo/installscript_si.vdf")).unwrap();
        let missing = InstallScripts::plan(&steamapps, "231350", None);
        assert!(!missing.runs.iter().any(|r| r.name == "FmSysInfo"));
        assert_eq!(missing.notes.len(), 1);
        assert!(missing.notes[0].contains("installscript_si.vdf"));
    }

    #[test]
    fn a_bom_and_crlf_script_is_read() {
        let steamapps = three_d_mark_library(true);
        write(&format!("\u{FEFF}{}", SYSTEM_INFO.replace('\n', "\r\n")), &steamapps.join("common/3DMark Demo/installscript_si.vdf"), false);
        let result = InstallScripts::plan(&steamapps, "231350", None);
        assert_eq!(result.runs.last().map(|r| r.name.as_str()), Some("FmSysInfo"));
        assert_eq!(result.notes, Vec::<String>::new());
    }

    #[test]
    fn registry_text_escapes_as_reg_files_do() {
        let result = plan(REG_ESCAPES);
        assert_eq!(
            result.registry,
            "Windows Registry Editor Version 5.00\n\n[HKEY_CURRENT_USER\\Software\\Game]\n\"v\"=\"a\\\"b\\\\c\"\n@=\"x\"\n\"d\"=dword:000005ed\n"
        );
        assert_eq!(result.notes.len(), 2);
        assert!(result.notes.iter().any(|n| n.contains("broken")));
        assert!(result.notes.iter().any(|n| n.contains("bad")));
    }

    #[test]
    fn windows_command_lines_split_as_windows_does() {
        assert_eq!(InstallScripts::arguments("/quiet /norestart"), vec!["/quiet", "/norestart"]);
        assert_eq!(InstallScripts::arguments(r#"install "a b" c"#), vec!["install", "a b", "c"]);
        assert_eq!(InstallScripts::arguments(r#"a\"b"#), vec![r#"a"b"#]);
        assert_eq!(InstallScripts::arguments(r#"a\\"b c""#), vec![r#"a\b c"#]);
        assert_eq!(InstallScripts::arguments(r#"""""#), vec![""]);
        assert_eq!(InstallScripts::arguments("  "), Vec::<String>::new());
    }

    #[test]
    fn hklm_software_is_steams_thirty_two_bit_view() {
        assert_eq!(
            InstallScripts::registry_key(r"HKEY_LOCAL_MACHINE\SOFTWARE\Futuremark").as_deref(),
            Some(r"HKEY_LOCAL_MACHINE\Software\Wow6432Node\Futuremark")
        );
        assert_eq!(
            InstallScripts::registry_key(r"HKLM\Software\X").as_deref(),
            Some(r"HKEY_LOCAL_MACHINE\Software\Wow6432Node\X")
        );
        assert_eq!(
            InstallScripts::registry_key(r"HKEY_LOCAL_MACHINE\Software\Wow6432Node\X").as_deref(),
            Some(r"HKEY_LOCAL_MACHINE\Software\Wow6432Node\X")
        );
        assert_eq!(
            InstallScripts::registry_key(r"HKEY_LOCAL_MACHINE\SYSTEM\X").as_deref(),
            Some(r"HKEY_LOCAL_MACHINE\SYSTEM\X")
        );
        assert_eq!(
            InstallScripts::registry_key(r"HKCU\Software\X").as_deref(),
            Some(r"HKEY_CURRENT_USER\Software\X")
        );
        assert_eq!(InstallScripts::registry_key(r"HKEY_CLASSES_ROOT\X"), None);
    }

    #[test]
    fn the_shell_quotes_every_word() {
        let mut plan = InstallScripts::default();
        plan.registry = "Windows Registry Editor Version 5.00\n\n[X]\n".to_string();
        plan.runs = vec![Run {
            key: r"HKEY_CURRENT_USER\Software\T".to_string(),
            name: "n".to_string(),
            minimum: 1,
            folder: "/mnt/games/Jeu d'été".to_string(),
            process: r"Z:\mnt\games\Jeu d'été\setup.exe".to_string(),
            arguments: vec!["/S".to_string(), "it's".to_string()],
        }];
        assert_eq!(
            plan.shell(),
            "mn_install_script() {\n  mn_run 'HKEY_CURRENT_USER\\Software\\T' 'n' '1' 'user.reg' 'Software\\\\T' '\"n\"' '/mnt/games/Jeu d'\\''été' 'Z:\\mnt\\games\\Jeu d'\\''été\\setup.exe' '/S' 'it'\\''s'\n}\n"
        );
        plan.runs = Vec::new();
        assert_eq!(plan.shell(), "");
        assert_eq!(InstallScripts::default().shell(), "");
    }

    #[test]
    fn the_registry_goes_as_a_utf16_file() {
        let mut plan = InstallScripts::default();
        plan.registry = "Windows Registry Editor Version 5.00\n\n[X]\n\"v\"=\"Jeu d'été\"\n".to_string();
        let file = plan.registry_file();
        assert_eq!(&file[..2], &[0xFF, 0xFE]);
        let body: Vec<u16> = file[2..]
            .chunks_exact(2)
            .map(|c| u16::from_le_bytes([c[0], c[1]]))
            .collect();
        assert_eq!(String::from_utf16(&body).unwrap(), plan.registry);
    }

    #[test]
    fn a_has_run_value_is_looked_for_as_wine_writes_it() {
        let lm = Run {
            key: r"HKEY_LOCAL_MACHINE\Software\Wow6432Node\MNTest\K[x]é".to_string(),
            name: r#"j"[é]\y0"#.to_string(),
            minimum: 1,
            folder: "/".to_string(),
            process: r"Z:\a.exe".to_string(),
            arguments: Vec::new(),
        };
        assert_eq!(lm.file(), "system.reg");
        assert_eq!(lm.file_key(), r"Software\\Wow6432Node\\MNTest\\K\[x\]\xe9");
        assert_eq!(lm.file_name(), r#""j\"[\xe9]\\y0""#);
        let cu = Run {
            key: r"HKEY_CURRENT_USER\SOFTWARE\Valve\Steam\Apps\231350".to_string(),
            name: "é1\u{1}".to_string(),
            minimum: 1,
            folder: "/".to_string(),
            process: r"Z:\a.exe".to_string(),
            arguments: Vec::new(),
        };
        assert_eq!(cu.file(), "user.reg");
        assert_eq!(cu.file_key(), r"SOFTWARE\\Valve\\Steam\\Apps\\231350");
        assert_eq!(cu.file_name(), r#""\x00e91\1""#);
    }

    #[test]
    fn a_bare_program_runs_from_system32() {
        let result = plan(&run_script(r#""msi" { "HasRunKey" "HKEY_CURRENT_USER\\Software\\T" "process 1" "msiexec.exe" "command 1" "/i x.msi" }"#));
        assert_eq!(result.runs.first().map(|r| r.folder.as_str()), Some("/home/mn/pfx/drive_c/windows/system32"));
        assert_eq!(result.runs.first().map(|r| r.process.as_str()), Some("msiexec.exe"));
        assert_eq!(result.runs.first().map(|r| r.arguments.clone()), Some(vec!["/i".to_string(), "x.msi".to_string()]));
    }

    #[test]
    fn the_games_own_folder_is_the_one_it_was_launched_from() {
        let steamapps = make_temp_dir().join("steamapps");
        write(
            &game_manifest("42", "Old Name", &[("43", "installscript.vdf")], &[]),
            &steamapps.join("appmanifest_42.acf"),
            false,
        );
        write(
            &run_script(r#""s" { "HasRunKey" "HKEY_CURRENT_USER\\Software\\T" "process 1" "%INSTALLDIR%\\setup.exe" }"#),
            &steamapps.join("common/Game/installscript.vdf"),
            false,
        );
        let result = InstallScripts::plan(&steamapps, "42", Some("Game"));
        assert_eq!(result.runs.first().map(|r| r.process.as_str()), Some(r"Z:\mnt\games\Game\setup.exe"));
        assert_eq!(result.runs.first().map(|r| r.folder.as_str()), Some("/mnt/games/Game"));
    }

    #[test]
    fn a_windows_1252_script_is_read() {
        let steamapps = make_temp_dir().join("steamapps");
        write(
            &game_manifest("42", "Game", &[("43", "installscript.vdf")], &[]),
            &steamapps.join("appmanifest_42.acf"),
            false,
        );
        let script = r#""InstallScript" { "Registry" { "HKEY_CURRENT_USER\\Software\\Game" { "string" { "v" "Café" } } } }"#;
        let file = steamapps.join("common/Game/installscript.vdf");
        std::fs::create_dir_all(file.parent().unwrap()).unwrap();
        let mut bytes = Vec::new();
        for ch in script.chars() {
            if ch == 'é' {
                bytes.push(0xE9);
            } else {
                let mut buf = [0u8; 4];
                bytes.extend_from_slice(ch.encode_utf8(&mut buf).as_bytes());
            }
        }
        std::fs::write(&file, &bytes).unwrap();
        let result = InstallScripts::plan(&steamapps, "42", None);
        assert!(result.registry.contains(r#""v"="Café""#));
        assert_eq!(result.notes, Vec::<String>::new());
        std::fs::write(&file, [0x22u8, 0x81, 0x22]).unwrap();
        assert!(InstallScripts::plan(&steamapps, "42", None).notes.first().map(|n| n.contains("unreadable")).unwrap_or(false));
    }

    #[test]
    fn a_key_on_several_lines_is_skipped() {
        let result = plan(KEY_MULTI_LINE);
        assert_eq!(result.registry, "");
        assert_eq!(result.notes.len(), 1);
    }
}
