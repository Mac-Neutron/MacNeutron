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
        // MACNEUTRON_DLSS=1 (opt-in per game): DXMT's NVIDIA extension, so the game sees an NVIDIA adapter, finds
        // DXMT's nvapi64.dll and, through the NGX core path DXMT registers, its nvngx.dll, which answers DLSS super
        // resolution with MetalFX's temporal upscaler. A value the user set for DXMT_ENABLE_NVEXT wins.
        if base["MACNEUTRON_DLSS"] == "1", backend == .dxmt, base["DXMT_ENABLE_NVEXT"] == nil {
            env["DXMT_ENABLE_NVEXT"] = "1"
        }
        // MACNEUTRON_UPSCALE=<preset> reports a smaller desktop to the game (XeSS's ratios), so it renders fewer
        // pixels and lays out its UI, which shares the back buffer, for that size; MetalFX upscales the window back
        // up: a large win for a GPU-bound game. Wine's Mac driver reads the ratio from MACNEUTRON_DISPLAY_SCALE; a
        // ratio the user set wins.
        //
        // MetalFX's spatial upscaler has no sharpness control, so the presenter sharpens the copy of the frame it
        // makes anyway (about free): MACNEUTRON_SHARPEN, which a value the user set wins over.
        if let preset = base["MACNEUTRON_UPSCALE"], let ratio = upscaleRatio(preset),
           base["MACNEUTRON_NO_METALFX"] != "1" {
            if base["MACNEUTRON_DISPLAY_SCALE"] == nil { env["MACNEUTRON_DISPLAY_SCALE"] = ratio }
            if base["MACNEUTRON_SHARPEN"] == nil { env["MACNEUTRON_SHARPEN"] = upscaleSharpness }
        }
        env["WINEDEBUG"] = wineDebug(base, logging: logging)
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

    /// Wine's WINEDEBUG, the user's if they set one (the microVM's guest gets it too). MACNEUTRON_LOG=1 leaves out seh
    /// warnings (Unreal logs through OutputDebugString, an exception each line) and steamclient's per-frame trace,
    /// which filled game logs by the gigabyte; MACNEUTRON_LOG=2 keeps them.
    static func wineDebug(_ base: [String: String], logging: Bool) -> String {
        base["WINEDEBUG"] ?? (!logging ? "-all"
            : base["MACNEUTRON_LOG"] == "2" ? "warn+all,+loaddll,+steamclient"
            : "warn+all,warn-seh,+loaddll,warn+steamclient")
    }

    /// How much the presenter sharpens the frame it upscales: contra-adaptive, so edges gain and flat areas and noise
    /// are left alone.  1 is AMD's CAS maximum, 0 turns it off; MACNEUTRON_UPSCALE sets it unless the user set their own.
    public static let upscaleSharpness = "0.5"

    /// MACNEUTRON_UPSCALE's presets, as the fraction of its real size the desktop is reported at: XeSS's ratios, so
    /// "quality" reports 2/3 of the pixels and MetalFX upscales them to the window. Nil for an unknown preset.
    public static func upscaleRatio(_ preset: String) -> String? {
        switch preset {
        case "ultra-quality": return "0.7692"       // 1.3x
        case "quality": return "0.6667"             // 1.5x
        case "balanced": return "0.5882"            // 1.7x
        case "performance": return "0.5"            // 2.0x
        case "ultra-performance": return "0.3333"   // 3.0x
        default: return nil
        }
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
