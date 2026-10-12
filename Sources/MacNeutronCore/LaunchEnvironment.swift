import Foundation

public enum LaunchEnvironment {
    /// Wine's environment: Steam's (including the user's launch-option variables) plus ours.
    /// Anything the user set explicitly wins over our defaults.
    public static func build(base: [String: String], context: CompatContext, backend: GraphicsBackend,
                             logging: Bool) -> [String: String] {
        var env = base
        env["WINEPREFIX"] = context.prefix.path(percentEncoded: false)
        // Steam sets it for "proton" tools, and the steamclient our bridge loads reads it; the game keeps the
        // environment it had before the rename (spec D5).
        env.removeValue(forKey: "STEAM_COMPAT_PROTON")
        // XeSS's path for non-Intel GPUs (DP4a) ran SMITE 2 at ~4 FPS: Wine's builtin libxess answers XeSS on D3D12
        // with MetalFX's upscaler (through DXMT; no device elsewhere, so games pick another upscaler).
        // MACNEUTRON_XESS=1 loads the game's own Intel XeSS.
        let xess = base["MACNEUTRON_XESS"] == "1" ? "" : ";libxess,libxess_dx11=b"
        env["WINEDLLOVERRIDES"] = mergeOverrides(backend.dllOverrides + xess, user: base["WINEDLLOVERRIDES"])
        // MACNEUTRON_LOG=1 leaves out seh warnings (Unreal logs through OutputDebugString, an exception each line)
        // and steamclient's per-frame trace, which filled game logs by the gigabyte; MACNEUTRON_LOG=2 keeps them.
        if base["WINEDEBUG"] == nil {
            env["WINEDEBUG"] = !logging ? "-all"
                : base["MACNEUTRON_LOG"] == "2" ? "warn+all,+loaddll,+steamclient"
                : "warn+all,warn-seh,+loaddll,warn+steamclient"
        }
        // msync off means unset, whoever set it: Wine's client and server must agree on it.
        if base["MACNEUTRON_NO_MSYNC"] == "1" {
            env.removeValue(forKey: "WINEMSYNC")
        } else if base["WINEMSYNC"] == nil {
            env["WINEMSYNC"] = "1"
        }
        if ShaderPrecache.enabled(backend: backend, environment: base), base["DXMT_PIPELINE_RECORD"] == nil {
            env["DXMT_PIPELINE_RECORD"] = ShaderPrecache.folder(for: context).path(percentEncoded: false)
        }
        return env
    }

    /// Merges two `WINEDLLOVERRIDES` strings by DLL name, `user` winning. Wine looks entries up
    /// in a sorted list, so a repeated name would make the winner arbitrary.
    public static func mergeOverrides(_ ours: String, user: String?) -> String {
        var order: [String] = []
        var modes: [String: String] = [:]
        for spec in [ours, user ?? ""] {
            for entry in spec.split(separator: ";") {
                let parts = entry.split(separator: "=", maxSplits: 1, omittingEmptySubsequences: false)
                let mode = parts.count == 2 ? String(parts[1]) : ""
                for name in parts[0].split(separator: ",") {
                    let key = name.trimmingCharacters(in: .whitespaces)
                    guard !key.isEmpty else { continue }
                    if modes[key] == nil { order.append(key) }
                    modes[key] = mode
                }
            }
        }
        return order.map { "\($0)=\(modes[$0]!)" }.joined(separator: ";")
    }
}
