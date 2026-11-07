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
            // Swift trims `.whitespaces` (spaces and tabs, not newlines): GraphicsBackend.swift:15.
            Some(r) => r.trim_matches(|c: char| c == '\t' || (c.is_whitespace() && !matches!(c, '\n' | '\r' | '\u{b}' | '\u{c}' | '\u{85}' | '\u{2028}' | '\u{2029}'))).to_lowercase(),
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn defaults_to_dxmt() {
        assert_eq!(GraphicsBackend::select(None), (GraphicsBackend::Dxmt, None));
    }

    #[test]
    fn removed_backends_read_as_dxmt_with_a_note() {
        for old in ["d3dmetal", "dxvk"] {
            let (backend, note) = GraphicsBackend::select(Some(old));
            assert_eq!(backend, GraphicsBackend::Dxmt);
            assert_eq!(note.as_deref(), Some(format!("'{old}' was removed in 0.1, using dxmt").as_str()));
        }
    }

    #[test]
    fn override_strings_per_backend() {
        assert_eq!(GraphicsBackend::Dxmt.dll_overrides(), "dxgi,d3d10core,d3d11,d3d12=n,b;d3d9,d3d10=b");
        assert_eq!(GraphicsBackend::WineD3D.dll_overrides(), "dxgi,d3d9,d3d10,d3d10core,d3d11,d3d12=b");
    }

    #[test]
    fn honors_request_case_insensitively() {
        assert_eq!(GraphicsBackend::select(Some(" WINED3D ")), (GraphicsBackend::WineD3D, None));
    }

    #[test]
    fn unknown_request_falls_back_with_note() {
        let (backend, note) = GraphicsBackend::select(Some("vulkan"));
        assert_eq!(backend, GraphicsBackend::Dxmt);
        assert_eq!(note.as_deref(), Some("unknown MACNEUTRON_GRAPHICS 'vulkan', using dxmt"));
    }

    #[test]
    fn every_backend_overrides_the_same_dll_set() {
        let managed: std::collections::BTreeSet<&str> =
            ["dxgi", "d3d9", "d3d10", "d3d10core", "d3d11", "d3d12"].into_iter().collect();
        for backend in GraphicsBackend::all() {
            let names: Vec<&str> = backend
                .dll_overrides()
                .split(';')
                .flat_map(|part| part.split('=').next().unwrap().split(','))
                .collect();
            assert_eq!(names.iter().copied().collect::<std::collections::BTreeSet<_>>(), managed, "{backend:?}");
            assert_eq!(names.len(), managed.len(), "{backend:?} repeats a DLL");
        }
    }
}
