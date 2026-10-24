import Foundation

/// Steam's install scripts for one game in the VM (spec 2026-10-24-macneutron-install-scripts-design §4.1): the
/// scripts Steam's app manifests list for the game and the shared redistributables it uses, as run-game.sh runs them
/// in the game's prefix before the game. Steam's rules are the Steamworks installscripts documentation's; Mac Steam
/// never runs them itself (bridge spec D-16).
/// ponytail: no language blocks, `process 2+`, `%ROOTDRIVE%` or other Requirement_OS keys: each is a note, until a game
/// needs it.
struct InstallScripts: Equatable {
    /// A "Run Process" entry: run-game.sh's mn_run runs `process` in `folder` unless `key`'s DWORD `name` is at least
    /// `minimum`, and writes `minimum` there after exit 0.
    struct Run: Equatable {
        let key: String, name: String, minimum: Int
        /// The process's folder in the guest, a Unix path: its working directory.
        let folder: String
        /// Its Windows path, ending .exe, .cmd, .bat or .msi.
        let process: String
        let arguments: [String]
    }

    /// REGEDIT4 text of every Registry entry, "" when none.
    var registry = ""
    var runs: [Run] = []
    /// One per thing skipped, for launcher.log.
    var notes: [String] = []

    /// The game's scripts and those of the shared depots it uses (dependencies first; Steam's order is undocumented).
    /// Nothing for a game without an app manifest in `steamapps` (a launch from outside Steam).
    static func plan(steamapps: URL, appID: String) -> InstallScripts {
        var result = InstallScripts()
        guard let game = Manifest(steamapps: steamapps, appID: appID) else { return result }
        var scripts: [(app: Manifest, path: String)] = []
        var owners: [String: Manifest?] = [:]
        for (depot, owner) in game.shared {
            if owners[owner] == nil { owners[owner] = Manifest(steamapps: steamapps, appID: owner) }
            guard let app = owners[owner] ?? nil else {
                result.notes.append("shared depot \(depot): app \(owner) isn't installed in this library")
                continue
            }
            if let path = app.scripts.first(where: { $0.depot == depot })?.path { scripts.append((app, path)) }
        }
        scripts += game.scripts.map { (game, $0.path) }
        var blocks: [String] = []
        var seen = Set<String>()
        for (app, path) in scripts {
            result.add(script: path, of: app, steamapps: steamapps, blocks: &blocks, seen: &seen)
        }
        if !blocks.isEmpty { result.registry = "REGEDIT4\n\n" + blocks.joined(separator: "\n") }
        return result
    }

    /// An app's `appmanifest_<id>.acf`: its folder in steamapps/common, its scripts by depot, its shared depots' owners.
    private struct Manifest {
        let installdir: String
        let scripts: [(depot: String, path: String)]
        let shared: [(depot: String, owner: String)]

        init?(steamapps: URL, appID: String) {
            guard let text = try? String(contentsOf: steamapps.appending(path: "appmanifest_\(appID).acf"), encoding: .utf8),
                  let state = (try? KeyValues.parse(text))?.node(at: ["AppState"]),
                  let installdir = state.children.node(at: ["installdir"])?.stringValue else { return nil }
            func pairs(_ block: String) -> [(String, String)] {
                (state.children.node(at: [block])?.children ?? []).compactMap { node in
                    node.stringValue.map { (KeyValues.unescape(node.key), $0) }
                }
            }
            self.installdir = installdir
            scripts = pairs("InstallScripts").map { (depot: $0.0, path: $0.1) }
            shared = pairs("SharedDepots").map { (depot: $0.0, owner: $0.1) }
        }
    }

    private mutating func add(script path: String, of app: Manifest, steamapps: URL, blocks: inout [String],
                              seen: inout Set<String>) {
        let name = app.installdir + "/" + path.replacingOccurrences(of: "\\", with: "/")
        let file = steamapps.appending(path: "common/" + name)
        guard var text = try? String(contentsOf: file, encoding: .utf8) else {
            notes.append("\(name): not found")
            return
        }
        if text.hasPrefix("\u{FEFF}") { text.removeFirst() }
        guard let root = (try? KeyValues.parse(text))?.node(at: ["InstallScript"]) else {
            notes.append("\(name): not an install script Steam's parser reads")
            return
        }
        for section in root.children {
            switch section.key.lowercased() {
            case "registry":
                for key in section.children { addRegistry(key, script: name, installdir: app.installdir, blocks: &blocks) }
            case "run process":
                for entry in section.children { run(entry, script: name, installdir: app.installdir, seen: &seen) }
            case "firewall":
                notes.append("\(name): \(section.key): not applied (Wine has no firewall)")
            default:
                notes.append("\(name): \(section.key): not applied")
            }
        }
    }

    private mutating func run(_ entry: KVNode, script: String, installdir: String, seen: inout Set<String>) {
        let at = "\(script): \(entry.key)"
        func value(_ key: String) -> String? { entry.children.node(at: [key])?.stringValue }
        guard let hasRunKey = value("HasRunKey") else { return notes.append("\(at): no HasRunKey") }
        guard let key = Self.registryKey(hasRunKey) else { return notes.append("\(at): HasRunKey \(hasRunKey) isn't HKLM or HKCU") }
        guard value("process 2") == nil else { return notes.append("\(at): has a process 2") }
        guard let process1 = value("process 1") else { return notes.append("\(at): no process 1") }
        for requirement in entry.children.node(at: ["Requirement_OS"])?.children ?? [] {
            let wanted = requirement.stringValue ?? ""
            switch requirement.key.lowercased() {
            case "is64bitwindows": if wanted != "1" { return }  // the VM prefix is 64-bit
            case "ostype": if wanted.lowercased() != "windows 10" { return }  // what our Wine reports
            default: return notes.append("\(at): Requirement_OS \(requirement.key) isn't understood")
            }
        }
        let (process, unknown) = Self.expand(process1, installdir: installdir)
        guard let process else { return notes.append("\(at): unknown token \(unknown ?? "")") }
        let (command, unknownInCommand) = Self.expand(value("command 1") ?? "", installdir: installdir)
        guard let command else { return notes.append("\(at): unknown token \(unknownInCommand ?? "")") }
        let ext = "." + (process.split(separator: ".").last.map { $0.lowercased() } ?? "")
        guard [".exe", ".cmd", ".bat", ".msi"].contains(ext) else {
            return notes.append("\(at): a \(ext) isn't run (only .exe, .cmd, .bat and .msi)")
        }
        guard let folder = Self.folder(of: process) else { return notes.append("\(at): \(process) isn't on C: or Z:") }
        let name = KeyValues.unescape(entry.key)
        guard seen.insert(key.lowercased() + "\n" + name.lowercased()).inserted else { return }  // the first one counts
        runs.append(Run(key: key, name: name, minimum: Int(value("MinimumHasRunValue") ?? "") ?? 1, folder: folder,
                        process: process, arguments: Self.arguments(command)))
    }

    private mutating func addRegistry(_ node: KVNode, script: String, installdir: String, blocks: inout [String]) {
        let path = KeyValues.unescape(node.key)
        guard let key = Self.registryKey(path) else { return notes.append("\(script): \(path) isn't HKLM or HKCU") }
        var lines: [String] = []
        for type in node.children {
            let kind = type.key.lowercased()
            guard kind == "string" || kind == "dword" else {
                notes.append("\(script): \(path): \(type.key) values aren't applied")
                continue
            }
            for item in type.children {
                let at = "\(script): \(path): \(item.key)"
                guard let raw = item.stringValue else {
                    notes.append("\(at): a language block isn't applied")
                    continue
                }
                let name = KeyValues.unescape(item.key)
                let (expanded, unknown) = Self.expand(raw, installdir: installdir)
                guard let value = expanded else {
                    notes.append("\(at): unknown token \(unknown ?? "")")
                    continue
                }
                guard !(name + value).contains(where: \.isNewline) else {
                    notes.append("\(at): a value on several lines isn't applied")
                    continue
                }
                let left = name == "(Default)" ? "@" : Self.regQuoted(name)
                if kind == "string" {
                    lines.append(left + "=" + Self.regQuoted(value))
                } else if let number = UInt32(value) {
                    lines.append(left + "=" + String(format: "dword:%08x", number))
                } else {
                    notes.append("\(at): \(value) isn't a DWORD")
                }
            }
        }
        if !lines.isEmpty { blocks.append("[\(key)]\n" + lines.map { $0 + "\n" }.joined()) }
    }

    /// A key or HasRunKey as the VM prefix has it: the hive's full name, and HKLM\SOFTWARE in the 32-bit view
    /// (Wow6432Node), where Steam writes (Steamworks documentation) and Proton's prefix has its CommonRedist marks.
    /// Nil for another hive.
    static func registryKey(_ path: String) -> String? {
        let parts = path.split(separator: "\\", maxSplits: 1, omittingEmptySubsequences: false)
        guard parts.count == 2 else { return nil }
        let rest = String(parts[1]), lower = rest.lowercased()
        switch parts[0].uppercased() {
        case "HKEY_CURRENT_USER", "HKCU":
            return #"HKEY_CURRENT_USER\"# + rest
        case "HKEY_LOCAL_MACHINE", "HKLM":
            if lower.hasPrefix(#"software\wow6432node\"#) || !lower.hasPrefix(#"software\"#) {
                return #"HKEY_LOCAL_MACHINE\"# + rest
            }
            return #"HKEY_LOCAL_MACHINE\Software\Wow6432Node\"# + rest.dropFirst(#"software\"#.count)
        default:
            return nil
        }
    }

    /// The documented tokens, as the VM prefix has them (spec §4.1); `%INSTALLDIR%` is the script's app's folder as
    /// the game itself sees it (run-game.sh's EXE: the VM prefix has no S: drive).
    private static func tokens(installdir: String) -> [(String, String)] {
        [("%INSTALLDIR%", #"Z:\mnt\games\"# + installdir.replacingOccurrences(of: "/", with: "\\")),
         ("%STEAMPATH%", #"C:\Program Files (x86)\Steam"#), ("%WinDir%", #"C:\windows"#),
         ("%APPDATA%", #"C:\users\steamuser\AppData\Roaming"#), ("%LOCALAPPDATA%", #"C:\users\steamuser\AppData\Local"#),
         ("%USER_MYDOCS%", #"C:\users\steamuser\Documents"#), ("%COMMON_MYDOCS%", #"C:\users\Public\Documents"#)]
    }

    /// `text` with its tokens replaced, or nil and the first token the table lacks.
    private static func expand(_ text: String, installdir: String) -> (String?, String?) {
        var out = text
        for (token, value) in tokens(installdir: installdir) {
            out = out.replacingOccurrences(of: token, with: value, options: .caseInsensitive)
        }
        if let unknown = out.firstMatch(of: #/%[A-Za-z_]+%/#) { return (nil, String(unknown.output)) }
        return (out, nil)
    }

    /// A Windows path's folder in the guest: `Z:\a\b\x.exe` is /a/b, `C:\a\x.exe` the prefix's drive_c/a.
    private static func folder(of process: String) -> String? {
        guard let cut = process.lastIndex(of: "\\") else { return nil }
        let folder = process[..<cut]
        let rest = folder.dropFirst(2).replacingOccurrences(of: "\\", with: "/")
        switch folder.prefix(2).lowercased() {
        case "z:": return rest.isEmpty ? "/" : rest
        case "c:": return "/home/mn/pfx/drive_c" + rest
        default: return nil
        }
    }

    /// A Windows command line's arguments, as CommandLineToArgvW splits them: whitespace outside quotes separates,
    /// `"` toggles quoting; 2n backslashes before a `"` are n and a toggle, 2n+1 are n and a literal `"`.
    static func arguments(_ command: String) -> [String] {
        var result: [String] = [], current = "", started = false, quoted = false, backslashes = 0
        for character in command {
            if character == "\\" {
                backslashes += 1
                started = true
                continue
            }
            if character == "\"" {
                current += String(repeating: "\\", count: backslashes / 2)
                if backslashes % 2 == 1 { current.append("\"") } else { quoted.toggle() }
                backslashes = 0
                started = true
                continue
            }
            current += String(repeating: "\\", count: backslashes)
            backslashes = 0
            if (character == " " || character == "\t") && !quoted {
                if started { result.append(current) }
                current = ""
                started = false
            } else {
                current.append(character)
                started = true
            }
        }
        current += String(repeating: "\\", count: backslashes)
        if started { result.append(current) }
        return result
    }

    /// A name or string as a .reg file writes it.
    private static func regQuoted(_ text: String) -> String {
        "\"" + text.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"") + "\""
    }
}
