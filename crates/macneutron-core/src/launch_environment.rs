//! Wine's launch environment (LaunchEnvironment.swift): Steam's own variables plus our Wine/DXMT settings, with
//! anything the user set explicitly winning.

use std::collections::BTreeMap;

use crate::compat_context::CompatContext;
use crate::graphics_backend::GraphicsBackend;
use crate::shader_precache::ShaderPrecache;

pub struct LaunchEnvironment;

impl LaunchEnvironment {
    /// Darwin's URL(for:)-style path with a trailing slash, as Swift's `.path(percentEncoded: false)` gives for a
    /// directory URL.
    fn directory_path(path: &std::path::Path) -> String {
        let text = path.to_string_lossy().into_owned();
        if text.ends_with('/') {
            text
        } else {
            format!("{text}/")
        }
    }

    /// How much the presenter sharpens the frame it upscales: contra-adaptive, so edges gain while flat areas and
    /// noise are left alone. 1 is AMD CAS's maximum, 0 turns it off; MACNEUTRON_UPSCALE sets it unless the user did.
    pub const UPSCALE_SHARPNESS: &'static str = "0.5";

    /// Wine's environment: Steam's (including the user's launch-option variables) plus ours. Anything the user set
    /// explicitly wins over our defaults.
    pub fn build(
        base: &BTreeMap<String, String>,
        context: &CompatContext,
        backend: GraphicsBackend,
        logging: bool,
    ) -> BTreeMap<String, String> {
        let mut env = base.clone();
        env.insert(
            "WINEPREFIX".to_string(),
            Self::directory_path(&context.prefix()),
        );
        // Steam sets it for "proton" tools, and the steamclient our bridge loads reads it; the game keeps the
        // environment it had before the rename (spec D5).
        env.remove("STEAM_COMPAT_PROTON");
        // XeSS's path for non-Intel GPUs (DP4a) ran SMITE 2 at ~4 FPS: Wine's builtin libxess answers XeSS on D3D12
        // with MetalFX's upscaler (through DXMT; no device elsewhere, so games pick another upscaler).
        // MACNEUTRON_XESS=1 loads the game's own Intel XeSS.
        let xess = if base.get("MACNEUTRON_XESS").map(String::as_str) == Some("1") {
            ""
        } else {
            ";libxess,libxess_dx11=b"
        };
        let overrides = format!("{}{}", backend.dll_overrides(), xess);
        env.insert(
            "WINEDLLOVERRIDES".to_string(),
            Self::merge_overrides(&overrides, base.get("WINEDLLOVERRIDES").map(String::as_str)),
        );
        // MACNEUTRON_DLSS=1 (opt-in per game): DXMT's NVIDIA extension, so the game sees an NVIDIA adapter, finds
        // DXMT's nvapi64.dll and, through the NGX core path DXMT registers, its nvngx.dll, which answers DLSS super
        // resolution with MetalFX's temporal upscaler. A value the user set for DXMT_ENABLE_NVEXT wins.
        if base.get("MACNEUTRON_DLSS").map(String::as_str) == Some("1")
            && backend == GraphicsBackend::Dxmt
            && !base.contains_key("DXMT_ENABLE_NVEXT")
        {
            env.insert("DXMT_ENABLE_NVEXT".to_string(), "1".to_string());
        }
        // MACNEUTRON_UPSCALE=<preset> reports a smaller desktop to the game (XeSS's ratios), so it renders fewer
        // pixels and lays out its UI, which shares the back buffer, for that size; MetalFX upscales the window back
        // up: a large win for a GPU-bound game. Wine's Mac driver reads the ratio from MACNEUTRON_DISPLAY_SCALE; a
        // ratio the user set wins.
        //
        // MetalFX's spatial upscaler has no sharpness control, so the presenter sharpens the copy of the frame it
        // makes anyway (about free): MACNEUTRON_SHARPEN, which a value the user set wins over.
        if let Some(preset) = base.get("MACNEUTRON_UPSCALE") {
            if let Some(ratio) = Self::upscale_ratio(preset) {
                if base.get("MACNEUTRON_NO_METALFX").map(String::as_str) != Some("1") {
                    if !base.contains_key("MACNEUTRON_DISPLAY_SCALE") {
                        env.insert("MACNEUTRON_DISPLAY_SCALE".to_string(), ratio.to_string());
                    }
                    if !base.contains_key("MACNEUTRON_SHARPEN") {
                        env.insert("MACNEUTRON_SHARPEN".to_string(), Self::UPSCALE_SHARPNESS.to_string());
                    }
                }
            }
        }
        env.insert("WINEDEBUG".to_string(), Self::wine_debug(base, logging));
        // msync off means unset, whoever set it: Wine's client and server must agree on it.
        if base.get("MACNEUTRON_NO_MSYNC").map(String::as_str) == Some("1") {
            env.remove("WINEMSYNC");
        } else if !base.contains_key("WINEMSYNC") {
            env.insert("WINEMSYNC".to_string(), "1".to_string());
        }
        if ShaderPrecache::enabled(backend, base) && !base.contains_key("DXMT_PIPELINE_RECORD") {
            env.insert(
                "DXMT_PIPELINE_RECORD".to_string(),
                ShaderPrecache::folder(context).to_string_lossy().into_owned(),
            );
        }
        env
    }

    /// Wine's WINEDEBUG, the user's if they set one (the microVM's guest gets it too). MACNEUTRON_LOG=1 leaves out
    /// seh warnings (Unreal logs through OutputDebugString, an exception each line) and steamclient's per-frame
    /// trace, which filled game logs by the gigabyte; MACNEUTRON_LOG=2 keeps them.
    pub fn wine_debug(base: &BTreeMap<String, String>, logging: bool) -> String {
        if let Some(value) = base.get("WINEDEBUG") {
            return value.clone();
        }
        if !logging {
            return "-all".to_string();
        }
        if base.get("MACNEUTRON_LOG").map(String::as_str) == Some("2") {
            "warn+all,+loaddll,+steamclient".to_string()
        } else {
            "warn+all,warn-seh,+loaddll,warn+steamclient".to_string()
        }
    }

    /// MACNEUTRON_UPSCALE's presets, as the fraction of its real size the desktop is reported at: XeSS's ratios, so
    /// "quality" reports 2/3 of the pixels and MetalFX upscales them to the window. None for an unknown preset.
    pub fn upscale_ratio(preset: &str) -> Option<&'static str> {
        match preset {
            "ultra-quality" => Some("0.7692"),
            "quality" => Some("0.6667"),
            "balanced" => Some("0.5882"),
            "performance" => Some("0.5"),
            "ultra-performance" => Some("0.3333"),
            _ => None,
        }
    }

    /// Merges two `WINEDLLOVERRIDES` strings by DLL name, `user` winning. Wine looks entries up in a sorted list, so
    /// a repeated name would make the winner arbitrary.
    pub fn merge_overrides(ours: &str, user: Option<&str>) -> String {
        let mut order: Vec<String> = Vec::new();
        let mut modes: BTreeMap<String, String> = BTreeMap::new();
        for spec in [ours, user.unwrap_or("")] {
            for entry in spec.split(';') {
                let mut parts = entry.splitn(2, '=');
                let names = parts.next().unwrap_or("");
                let mode = parts.next().unwrap_or("");
                for name in names.split(',') {
                    let key = name.trim();
                    if key.is_empty() {
                        continue;
                    }
                    if !modes.contains_key(key) {
                        order.push(key.to_string());
                    }
                    modes.insert(key.to_string(), mode.to_string());
                }
            }
        }
        order
            .iter()
            .map(|name| format!("{name}={}", modes[name]))
            .collect::<Vec<_>>()
            .join(";")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn context() -> CompatContext {
        CompatContext::new(&BTreeMap::from([
            ("STEAM_COMPAT_DATA_PATH".to_string(), "/c/42".to_string()),
            ("SteamAppId".to_string(), "42".to_string()),
        ]))
        .unwrap()
    }

    fn base(pairs: &[(&str, &str)]) -> BTreeMap<String, String> {
        pairs.iter().map(|(k, v)| (k.to_string(), v.to_string())).collect()
    }

    #[test]
    fn sets_prefix_overrides_and_defaults() {
        let env = LaunchEnvironment::build(&base(&[("PATH", "/usr/bin")]), &context(), GraphicsBackend::Dxmt, false);
        assert_eq!(env["WINEPREFIX"], "/c/42/pfx/");
        assert_eq!(
            env["WINEDLLOVERRIDES"],
            "dxgi=n,b;d3d10core=n,b;d3d11=n,b;d3d12=n,b;d3d9=b;d3d10=b;libxess=b;libxess_dx11=b"
        );
        assert_eq!(env["WINEDEBUG"], "-all");
        assert!(!env.contains_key("ROSETTA_ADVERTISE_AVX"));
        assert_eq!(env["WINEMSYNC"], "1");
        assert_eq!(env["PATH"], "/usr/bin");
    }

    #[test]
    fn wined3d_overrides_use_builtins() {
        let env = LaunchEnvironment::build(&BTreeMap::new(), &context(), GraphicsBackend::WineD3D, false);
        assert_eq!(
            env["WINEDLLOVERRIDES"],
            "dxgi=b;d3d9=b;d3d10=b;d3d10core=b;d3d11=b;d3d12=b;libxess=b;libxess_dx11=b"
        );
    }

    #[test]
    fn logging_turns_on_wine_debug_channels() {
        let env = LaunchEnvironment::build(&base(&[("MACNEUTRON_LOG", "1")]), &context(), GraphicsBackend::Dxmt, true);
        assert_eq!(env["WINEDEBUG"], "warn+all,warn-seh,+loaddll,warn+steamclient");
        let full = LaunchEnvironment::build(&base(&[("MACNEUTRON_LOG", "2")]), &context(), GraphicsBackend::Dxmt, true);
        assert_eq!(full["WINEDEBUG"], "warn+all,+loaddll,+steamclient");
    }

    #[test]
    fn user_settings_win() {
        let env = LaunchEnvironment::build(
            &base(&[("WINEDEBUG", "+seh"), ("WINEDLLOVERRIDES", "d3d11=b;xinput1_3=n")]),
            &context(),
            GraphicsBackend::Dxmt,
            true,
        );
        assert_eq!(env["WINEDEBUG"], "+seh");
        assert_eq!(
            env["WINEDLLOVERRIDES"],
            "dxgi=n,b;d3d10core=n,b;d3d11=b;d3d12=n,b;d3d9=b;d3d10=b;libxess=b;libxess_dx11=b;xinput1_3=n"
        );
    }

    #[test]
    fn opt_outs_drop_defaults() {
        let env = LaunchEnvironment::build(&base(&[("MACNEUTRON_NO_MSYNC", "1")]), &context(), GraphicsBackend::Dxmt, false);
        assert!(!env.contains_key("WINEMSYNC"));
        let set = LaunchEnvironment::build(
            &base(&[("MACNEUTRON_NO_MSYNC", "1"), ("WINEMSYNC", "1")]),
            &context(),
            GraphicsBackend::Dxmt,
            false,
        );
        assert!(!set.contains_key("WINEMSYNC"));
    }

    #[test]
    fn merge_keeps_disabled_entries() {
        assert_eq!(LaunchEnvironment::merge_overrides("a=b", Some("c=")), "a=b;c=");
    }

    #[test]
    fn user_overrides_win() {
        let env = LaunchEnvironment::build(&base(&[("WINEDLLOVERRIDES", "d3d12=b")]), &context(), GraphicsBackend::Dxmt, false);
        assert_eq!(
            env["WINEDLLOVERRIDES"],
            "dxgi=n,b;d3d10core=n,b;d3d11=n,b;d3d12=b;d3d9=b;d3d10=b;libxess=b;libxess_dx11=b"
        );
    }

    #[test]
    fn xess_bridge_is_the_default_unless_opted_out() {
        let overrides = |b: &BTreeMap<String, String>| {
            LaunchEnvironment::build(b, &context(), GraphicsBackend::Dxmt, false)
                .get("WINEDLLOVERRIDES")
                .cloned()
        };
        assert!(overrides(&BTreeMap::new()).unwrap().ends_with(";libxess=b;libxess_dx11=b"));
        assert_eq!(
            overrides(&base(&[("MACNEUTRON_XESS", "1")])).unwrap(),
            "dxgi=n,b;d3d10core=n,b;d3d11=n,b;d3d12=n,b;d3d9=b;d3d10=b"
        );
        assert!(overrides(&base(&[("WINEDLLOVERRIDES", "libxess=n")])).unwrap().ends_with(";libxess=n;libxess_dx11=b"));
    }

    #[test]
    fn records_pipelines_for_dxmt_only() {
        let record = |b: &BTreeMap<String, String>, backend| {
            LaunchEnvironment::build(b, &context(), backend, false)
                .get("DXMT_PIPELINE_RECORD")
                .cloned()
        };
        assert_eq!(record(&BTreeMap::new(), GraphicsBackend::Dxmt).as_deref(), Some("/c/42/dxmt-pipelines"));
        assert_eq!(record(&BTreeMap::new(), GraphicsBackend::WineD3D), None);
        assert_eq!(record(&base(&[("MACNEUTRON_PRECACHE", "0")]), GraphicsBackend::Dxmt), None);
        assert_eq!(record(&base(&[("DXMT_PIPELINE_RECORD", "/mine")]), GraphicsBackend::Dxmt).as_deref(), Some("/mine"));
    }

    #[test]
    fn upscale_presets_report_a_smaller_desktop() {
        let ratio = |b: &BTreeMap<String, String>| {
            LaunchEnvironment::build(b, &context(), GraphicsBackend::Dxmt, false)
                .get("MACNEUTRON_DISPLAY_SCALE")
                .cloned()
        };
        assert_eq!(ratio(&base(&[("MACNEUTRON_UPSCALE", "ultra-quality")])).as_deref(), Some("0.7692"));
        assert_eq!(ratio(&base(&[("MACNEUTRON_UPSCALE", "quality")])).as_deref(), Some("0.6667"));
        assert_eq!(ratio(&base(&[("MACNEUTRON_UPSCALE", "balanced")])).as_deref(), Some("0.5882"));
        assert_eq!(ratio(&base(&[("MACNEUTRON_UPSCALE", "performance")])).as_deref(), Some("0.5"));
        assert_eq!(ratio(&base(&[("MACNEUTRON_UPSCALE", "ultra-performance")])).as_deref(), Some("0.3333"));
        assert_eq!(ratio(&BTreeMap::new()), None);
        assert_eq!(ratio(&base(&[("MACNEUTRON_UPSCALE", "nonsense")])), None);
        assert_eq!(ratio(&base(&[("MACNEUTRON_UPSCALE", "quality"), ("MACNEUTRON_DISPLAY_SCALE", "0.9")])).as_deref(), Some("0.9"));
        assert_eq!(ratio(&base(&[("MACNEUTRON_UPSCALE", "quality"), ("MACNEUTRON_NO_METALFX", "1")])), None);

        let sharpen = |b: &BTreeMap<String, String>| {
            LaunchEnvironment::build(b, &context(), GraphicsBackend::Dxmt, false)
                .get("MACNEUTRON_SHARPEN")
                .cloned()
        };
        assert_eq!(sharpen(&base(&[("MACNEUTRON_UPSCALE", "quality")])).as_deref(), Some(LaunchEnvironment::UPSCALE_SHARPNESS));
        assert_eq!(sharpen(&base(&[("MACNEUTRON_UPSCALE", "quality"), ("MACNEUTRON_SHARPEN", "0.9")])).as_deref(), Some("0.9"));
        assert_eq!(sharpen(&base(&[("MACNEUTRON_UPSCALE", "quality"), ("MACNEUTRON_SHARPEN", "0")])).as_deref(), Some("0"));
        assert_eq!(sharpen(&BTreeMap::new()), None);
        assert_eq!(sharpen(&base(&[("MACNEUTRON_UPSCALE", "quality"), ("MACNEUTRON_NO_METALFX", "1")])), None);
    }

    #[test]
    fn dlss_turns_on_dxmts_nvidia_extension() {
        let nvext = |b: &BTreeMap<String, String>, backend| {
            LaunchEnvironment::build(b, &context(), backend, false).get("DXMT_ENABLE_NVEXT").cloned()
        };
        assert_eq!(nvext(&BTreeMap::new(), GraphicsBackend::Dxmt), None);
        assert_eq!(nvext(&base(&[("MACNEUTRON_DLSS", "1")]), GraphicsBackend::Dxmt).as_deref(), Some("1"));
        assert_eq!(nvext(&base(&[("MACNEUTRON_DLSS", "0")]), GraphicsBackend::Dxmt), None);
        assert_eq!(nvext(&base(&[("MACNEUTRON_DLSS", "1")]), GraphicsBackend::WineD3D), None);
        assert_eq!(nvext(&base(&[("MACNEUTRON_DLSS", "1"), ("DXMT_ENABLE_NVEXT", "0")]), GraphicsBackend::Dxmt).as_deref(), Some("0"));
    }

    #[test]
    fn steams_proton_flag_is_removed() {
        let env = LaunchEnvironment::build(&base(&[("STEAM_COMPAT_PROTON", "1")]), &context(), GraphicsBackend::Dxmt, false);
        assert!(!env.contains_key("STEAM_COMPAT_PROTON"));
    }

    #[test]
    fn post_anti_aliasing_reaches_wine() {
        use crate::game_settings::GameSettings;
        let settings = GameSettings { post_aa: Some("cmaa2".into()), ..Default::default() };
        let env = LaunchEnvironment::build(&settings.environment(), &context(), GraphicsBackend::Dxmt, false);
        assert_eq!(env.get("MACNEUTRON_POST_AA").map(String::as_str), Some("cmaa2"));
    }

    #[test]
    fn anisotropic_filtering_reaches_wine() {
        use crate::game_settings::GameSettings;
        let forced = |settings: GameSettings, options: &[(&str, &str)]| {
            let mut merged = settings.environment();
            merged.extend(base(options)); // a launch option wins
            LaunchEnvironment::build(&merged, &context(), GraphicsBackend::Dxmt, false)
                .get("DXMT_MAX_ANISOTROPY")
                .cloned()
        };
        assert_eq!(forced(GameSettings::default(), &[]).as_deref(), Some("16"));
        let with = |a: &str| GameSettings { anisotropy: Some(a.into()), ..Default::default() };
        assert_eq!(forced(with("8"), &[]).as_deref(), Some("8"));
        assert_eq!(forced(with("game"), &[]), None);
        assert_eq!(forced(GameSettings::default(), &[("DXMT_MAX_ANISOTROPY", "0")]).as_deref(), Some("0"));
    }
}
