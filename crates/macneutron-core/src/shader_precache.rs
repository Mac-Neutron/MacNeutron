//! The environment a game launches with: Steam's, plus the compatibility tool's settings. Ported from
//! `ShaderPrecache.swift`; the recording/replay machinery comes with the capstone batch, but `LaunchEnvironment`
//! only needs the folder and the on/off decision.

use std::path::PathBuf;

use crate::compat_context::CompatContext;
use crate::graphics_backend::GraphicsBackend;

pub struct ShaderPrecache;

impl ShaderPrecache {
    /// Recordings live beside the prefix, in the game's VM state folder.
    pub fn folder(context: &CompatContext) -> PathBuf {
        context.data_path.join("dxmt-pipelines")
    }

    /// Recording and replay are DXMT's; `MACNEUTRON_PRECACHE=0` turns both off.
    pub fn enabled(backend: GraphicsBackend, environment: &std::collections::BTreeMap<String, String>) -> bool {
        backend == GraphicsBackend::Dxmt && environment.get("MACNEUTRON_PRECACHE").map(|v| v != "0").unwrap_or(true)
    }
}
