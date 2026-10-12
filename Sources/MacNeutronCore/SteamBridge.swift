import Foundation

/// Where the Steam bridge lives inside a prefix, and the small decisions the launcher makes about it.
/// The bridge itself (Proton's lsteamclient) ships with the runtime; `steam.exe` ships with MacNeutron.
public enum SteamBridge {
    /// `steam.exe` inside every prefix.
    public static let steamExe = #"C:\Program Files (x86)\Steam\steam.exe"#

    /// `Z:` is Wine's mapping of `/`; Windows paths pass through.
    public static func windowsPath(_ path: String) -> String {
        path.hasPrefix("/") ? "Z:" + path.replacingOccurrences(of: "/", with: #"\"#) : path
    }

    /// The folder `steam.exe` names in the registry, relative to the prefix.
    static let prefixFolder = "drive_c/Program Files (x86)/Steam"

    /// Spike: a launch option naming Valve's signed steamclient64.dll (an absolute Mac path), which the prefix gets in
    /// place of lsteamclient.dll; Wine patch 0036 makes its exports jump to lsteamclient.dll's.
    static let steamClient64Variable = "MACNEUTRON_STEAMCLIENT64"

    /// What the launcher copies into a prefix: (source, destination relative to the prefix).
    static func prefixFiles(layout: ToolLayout, steamClient64: URL? = nil) -> [(URL, String)] {
        [(layout.steamHelper, "\(prefixFolder)/steam.exe"),
         (steamClient64 ?? layout.lsteamclient, "\(prefixFolder)/steamclient64.dll")]
    }

    /// `steamapps/libraryfolders.vdf` as Proton's steam.exe writes it at every launch (setup_steam_files): the 2K
    /// Launcher and mod managers find the games through it. One library per `STEAM_COMPAT_LIBRARY_PATHS` entry, a
    /// `steamapps` folder whose parent is the library, in Proton's spelling and layout. Proton lists
    /// `STEAM_COMPAT_CLIENT_INSTALL_PATH` first: on macOS that's the Steam bundle's MacOS folder, no library, so it's left
    /// out.
    static func libraryFolders(steamapps: String) -> String {
        let libraries = steamapps.split(separator: ":").compactMap { entry -> String? in
            let path = entry.hasSuffix("/") ? entry.dropLast() : entry
            guard let slash = path.lastIndex(of: "/"), slash > path.startIndex else { return nil }
            return windowsPath(String(path[..<slash]))
        }
        return KeyValues.serialize([.block("LibraryFolders", libraries.enumerated().map {
            .block(String($0.offset), [.string("path", $0.element)])
        })])
    }

    /// Steam's `STEAM_COMPAT_CLIENT_INSTALL_PATH` when that folder holds `steamclient.dylib`,
    /// otherwise the Steam bundle's MacOS folder, where macOS Steam keeps it.
    public static func clientDirectory(steamValue: String?, steam: SteamLocation) -> String {
        if let steamValue, FileManager.default.fileExists(atPath: steamValue + "/steamclient.dylib") { return steamValue }
        var bundle = steam.bundleMacOS.path(percentEncoded: false)
        if bundle.hasSuffix("/") { bundle.removeLast() }
        return bundle
    }
}
