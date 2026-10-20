import Foundation

/// How a game's Direct3D is rendered for one launch.
public enum GraphicsBackend: String, Sendable, CaseIterable {
    /// DXMT: Direct3D 10-12 on Metal, MacNeutron's default for every game.
    case dxmt
    /// Wine's built-in Direct3D 10-11 over OpenGL: an escape hatch for a Direct3D 11 game DXMT breaks. Direct3D 9 is
    /// mtld3d's (wine.app's builtin d3d9.dll) with either backend: Wine's own is no longer in wine.app.
    case wined3d

    /// Honors `MACNEUTRON_GRAPHICS` when valid; otherwise DXMT. `note` explains any fallback so the launcher can log it.
    public static func select(requested: String?) -> (backend: GraphicsBackend, note: String?) {
        let fallback = GraphicsBackend.dxmt
        guard let raw = requested?.trimmingCharacters(in: .whitespaces).lowercased(), !raw.isEmpty else {
            return (fallback, nil)
        }
        // Settings files written before 0.1 can still name these.
        if raw == "d3dmetal" || raw == "dxvk" {
            return (fallback, "'\(raw)' was removed in 0.1, using \(fallback.rawValue)")
        }
        guard let backend = GraphicsBackend(rawValue: raw) else {
            return (fallback, "unknown MACNEUTRON_GRAPHICS '\(raw)', using \(fallback.rawValue)")
        }
        return (backend, nil)
    }

    /// `WINEDLLOVERRIDES` for this backend. Every backend names every D3D DLL any backend
    /// manages, so DLLs a previous backend left in the prefix can never leak into this launch.
    public var dllOverrides: String {
        switch self {
        case .dxmt: "dxgi,d3d10core,d3d11,d3d12=n,b;d3d9,d3d10=b"
        case .wined3d: "dxgi,d3d9,d3d10,d3d10core,d3d11,d3d12=b"
        }
    }
}
