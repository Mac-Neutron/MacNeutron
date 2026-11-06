//! How a game's Direct3D is rendered for one launch.

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum GraphicsBackend {
    /// DXMT: Direct3D 10-12 on Metal, MacNeutron's default for every game.
    Dxmt,
    /// Wine's built-in Direct3D 10-11 over OpenGL: an escape hatch for a Direct3D 11 game DXMT breaks. Direct3D 9 is
    /// mtld3d's (wine.app's builtin d3d9.dll) with either backend: Wine's own is no longer in wine.app.
    WineD3D,
}

impl GraphicsBackend {
    pub fn raw_value(self) -> &'static str {
        match self {
            GraphicsBackend::Dxmt => "dxmt",
            GraphicsBackend::WineD3D => "wined3d",
        }
    }

    #[allow(clippy::should_implement_trait)]
    pub fn from_str(s: &str) -> Option<GraphicsBackend> {
        Some(match s {
            "dxmt" => GraphicsBackend::Dxmt,
            "wined3d" => GraphicsBackend::WineD3D,
            _ => return None,
        })
    }

    pub fn all() -> [GraphicsBackend; 2] {
        [GraphicsBackend::Dxmt, GraphicsBackend::WineD3D]
    }

    /// Honors `MACNEUTRON_GRAPHICS` when valid; otherwise DXMT. `note` explains any fallback so the launcher can log it.
    pub fn select(requested: Option<&str>) -> (GraphicsBackend, Option<String>) {
        let fallback = GraphicsBackend::Dxmt;
        let raw = match requested {
            Some(r) => r.trim().to_lowercase(),
            None => return (fallback, None),
        };
        if raw.is_empty() {
            return (fallback, None);
        }
        // Settings files written before 0.1 can still name these.
        if raw == "d3dmetal" || raw == "dxvk" {
            return (fallback, Some(format!("'{raw}' was removed in 0.1, using {}", fallback.raw_value())));
        }
        match GraphicsBackend::from_str(&raw) {
            Some(backend) => (backend, None),
            None => (fallback, Some(format!("unknown MACNEUTRON_GRAPHICS '{raw}', using {}", fallback.raw_value()))),
        }
    }

    /// `WINEDLLOVERRIDES` for this backend. Every backend names every D3D DLL any backend manages, so DLLs a previous
    /// backend left in the prefix can never leak into this launch.
    pub fn dll_overrides(self) -> &'static str {
        match self {
            GraphicsBackend::Dxmt => "dxgi,d3d10core,d3d11,d3d12=n,b;d3d9,d3d10=b",
            GraphicsBackend::WineD3D => "dxgi,d3d9,d3d10,d3d10core,d3d11,d3d12=b",
        }
    }
}
