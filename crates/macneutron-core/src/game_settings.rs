//! Per-game choices from the app's games window. Every field is optional; nil means default.

use std::collections::{BTreeMap, HashMap};
use std::path::PathBuf;

use serde::{Deserialize, Serialize};

use crate::launch_options::LaunchOptions;
use crate::mapping_planner::RunAs;
use crate::paths::MacNeutronPaths;

/// MACNEUTRON_UPSCALE's presets, XeSS's ratios: the back buffer keeps this fraction of the window's pixels.
pub const UPSCALE_PRESETS: [&str; 5] = [
    "ultra-quality",
    "quality",
    "balanced",
    "performance",
    "ultra-performance",
];

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default)]
pub struct GameSettings {
    pub graphics: Option<String>,
    pub log: Option<bool>,
    pub msync: Option<bool>,
    pub run_as: Option<RunAs>,
    #[serde(rename = "metalFX")]
    pub metal_fx: Option<bool>,
    /// Post-process anti-aliasing in the presenter: "off" or "cmaa2" (any other value is off).
    #[serde(rename = "postAA")]
    pub post_aa: Option<String>,
    /// Anisotropic filtering: nil (16x), "game" (the game's own samplers), "4", "8" or "16".
    pub anisotropy: Option<String>,
    /// Upscaling (the launcher's MACNEUTRON_UPSCALE): the game renders into a back buffer this much smaller than its
    /// window and the presenter upscales it with MetalFX. nil (off) or one of `UPSCALE_PRESETS`.
    pub upscale: Option<String>,
    /// Apple's Metal performance HUD over the game (MTL_HUD_ENABLED): FPS, frame times, and the labels DXMT and the
    /// presenter publish. nil/false: off.
    #[serde(rename = "metalHUD")]
    pub metal_hud: Option<bool>,
    /// DLSS answered by MetalFX (MACNEUTRON_DLSS): the game sees an NVIDIA GPU, and its DLSS option upscales with
    /// MetalFX's temporal upscaler. nil/false: off.
    pub dlss: Option<bool>,
    /// Frame generation on MetalFX (MACNEUTRON_FRAMEGEN=1, adaptive in DXMT): a frame interpolated between each two the
    /// game renders, when it runs DLSS on MetalFX and generating doesn't hold it back. nil/false: off.
    #[serde(rename = "frameGen")]
    pub frame_gen: Option<bool>,
    /// The Games window's launch options (`LaunchOptions::parse`): variables over the controls' and game arguments.
    /// Steam's own launch options still win over both. nil: none.
    #[serde(rename = "launchOptions")]
    pub launch_options: Option<String>,
}

impl GameSettings {
    /// The launch-option variables these settings stand for (`run_as` is for the mapping planner only).
    pub fn environment(&self) -> BTreeMap<String, String> {
        let mut env = BTreeMap::new();
        if let Some(graphics) = &self.graphics {
            env.insert("MACNEUTRON_GRAPHICS".to_string(), graphics.clone());
        }
        if self.log == Some(true) {
            env.insert("MACNEUTRON_LOG".to_string(), "1".to_string());
        }
        if self.msync == Some(false) {
            env.insert("MACNEUTRON_NO_MSYNC".to_string(), "1".to_string());
        }
        if self.metal_fx == Some(false) {
            env.insert("MACNEUTRON_NO_METALFX".to_string(), "1".to_string());
        }
        if self.post_aa.as_deref() == Some("cmaa2") {
            env.insert("MACNEUTRON_POST_AA".to_string(), "cmaa2".to_string());
        }
        // DXMT's floor on trilinear samplers' anisotropy (DXMT 0030): Metal's anisotropic filter cancels a negative
        // LOD bias (an upscaler's) past the anisotropy ratio, so 16x by default; unknown values get it too.
        match self.anisotropy.as_deref() {
            Some("game") => {}
            Some(v @ ("4" | "8")) => {
                env.insert("DXMT_MAX_ANISOTROPY".to_string(), v.to_string());
            }
            _ => {
                env.insert("DXMT_MAX_ANISOTROPY".to_string(), "16".to_string());
            }
        }
        // The upscaler reports the desktop smaller than it is (its preset picks how much smaller) so the game renders
        // fewer pixels; the launcher turns the preset into MACNEUTRON_DISPLAY_SCALE and drops it when MetalFX is off.
        // Unknown values are off.
        if let Some(upscale) = &self.upscale {
            if UPSCALE_PRESETS.contains(&upscale.as_str()) {
                env.insert("MACNEUTRON_UPSCALE".to_string(), upscale.clone());
            }
        }
        // Apple's Metal HUD, the same variable a launch option sets (a user's own value wins where the launcher merges
        // environments); its log (MTL_HUD_LOG_ENABLED) and layout variables stay launch options.
        if self.metal_hud == Some(true) {
            env.insert("MTL_HUD_ENABLED".to_string(), "1".to_string());
        }
        if self.dlss == Some(true) {
            env.insert("MACNEUTRON_DLSS".to_string(), "1".to_string());
        }
        if self.frame_gen == Some(true) {
            env.insert("MACNEUTRON_FRAMEGEN".to_string(), "1".to_string());
        }
        for (k, v) in LaunchOptions::parse(self.launch_options.as_deref().unwrap_or("")).0 {
            env.insert(k, v);
        }
        env
    }

    /// The launch options field's game arguments, after the game's own.
    pub fn launch_arguments(&self) -> Vec<String> {
        LaunchOptions::parse(self.launch_options.as_deref().unwrap_or("")).1
    }
}

/// `games/<appid>.json`, written atomically so the launcher never reads half a file.
pub struct GameSettingsStore {
    pub directory: PathBuf,
}

impl Default for GameSettingsStore {
    fn default() -> Self {
        GameSettingsStore { directory: MacNeutronPaths::games() }
    }
}

impl GameSettingsStore {
    pub fn new(directory: PathBuf) -> Self {
        GameSettingsStore { directory }
    }

    fn file(&self, app_id: &str) -> PathBuf {
        self.directory.join(format!("{app_id}.json"))
    }

    /// Missing file -> defaults. A file that exists but can't be decoded throws.
    pub fn load(&self, app_id: &str) -> Result<GameSettings, anyhow::Error> {
        let url = self.file(app_id);
        if !url.exists() {
            return Ok(GameSettings::default());
        }
        let data = std::fs::read(&url)?;
        Ok(serde_json::from_slice(&data)?)
    }

    pub fn save(&self, settings: &GameSettings, app_id: &str) -> Result<(), anyhow::Error> {
        std::fs::create_dir_all(&self.directory)?;
        let data = serde_json::to_vec(settings)?;
        write_atomic(&self.file(app_id), &data)?;
        Ok(())
    }

    /// Every readable settings file, keyed by app ID.
    pub fn all(&self) -> BTreeMap<String, GameSettings> {
        let mut result = BTreeMap::new();
        let Ok(entries) = std::fs::read_dir(&self.directory) else { return result };
        for entry in entries.flatten() {
            let Ok(name) = entry.file_name().into_string() else { continue };
            let Some(app_id) = name.strip_suffix(".json") else { continue };
            if let Ok(settings) = self.load(app_id) {
                result.insert(app_id.to_string(), settings);
            }
        }
        result
    }

    /// `run_as` choices for the mapping planner.
    pub fn run_as_overrides(&self) -> HashMap<u32, RunAs> {
        let mut result = HashMap::new();
        for (app_id, settings) in self.all() {
            if let (Ok(id), Some(run_as)) = (app_id.parse::<u32>(), settings.run_as) {
                result.insert(id, run_as);
            }
        }
        result
    }
}

fn write_atomic(path: &std::path::Path, data: &[u8]) -> std::io::Result<()> {
    let tmp = path.with_extension("json.tmp");
    std::fs::write(&tmp, data)?;
    std::fs::rename(&tmp, path)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn settings(apply: impl FnOnce(&mut GameSettings)) -> GameSettings {
        let mut s = GameSettings::default();
        apply(&mut s);
        s
    }

    #[test]
    fn settings_become_launch_variables() {
        let s = settings(|s| {
            s.graphics = Some("dxmt".into());
            s.log = Some(true);
            s.msync = Some(false);
            s.run_as = Some(RunAs::Windows);
            s.metal_fx = Some(false);
        });
        let env = s.environment();
        assert_eq!(env["MACNEUTRON_GRAPHICS"], "dxmt");
        assert_eq!(env["MACNEUTRON_LOG"], "1");
        assert_eq!(env["MACNEUTRON_NO_MSYNC"], "1");
        assert_eq!(env["MACNEUTRON_NO_METALFX"], "1");
        assert_eq!(env["DXMT_MAX_ANISOTROPY"], "16");

        let s = settings(|s| {
            s.log = Some(false);
            s.msync = Some(true);
            s.metal_fx = Some(true);
            s.anisotropy = Some("game".into());
        });
        assert!(s.environment().is_empty());

        assert_eq!(settings(|s| s.post_aa = Some("cmaa2".into())).environment()["MACNEUTRON_POST_AA"], "cmaa2");
        assert!(!settings(|s| s.post_aa = Some("off".into())).environment().contains_key("MACNEUTRON_POST_AA"));
        assert!(!settings(|s| s.post_aa = Some("smaa".into())).environment().contains_key("MACNEUTRON_POST_AA"));
    }

    #[test]
    fn metal_hud_dlss_and_framegen() {
        assert_eq!(settings(|s| s.metal_hud = Some(true)).environment()["MTL_HUD_ENABLED"], "1");
        assert!(!settings(|s| s.metal_hud = Some(false)).environment().contains_key("MTL_HUD_ENABLED"));
        assert!(!GameSettings::default().environment().contains_key("MTL_HUD_ENABLED"));
        assert_eq!(settings(|s| s.dlss = Some(true)).environment()["MACNEUTRON_DLSS"], "1");
        assert!(!settings(|s| s.dlss = Some(false)).environment().contains_key("MACNEUTRON_DLSS"));
        assert_eq!(settings(|s| s.frame_gen = Some(true)).environment()["MACNEUTRON_FRAMEGEN"], "1");
        assert!(!settings(|s| s.frame_gen = Some(false)).environment().contains_key("MACNEUTRON_FRAMEGEN"));
    }

    #[test]
    fn upscaling_presets() {
        let preset = |value: Option<&str>| {
            settings(|s| s.upscale = value.map(|v| v.to_string()))
                .environment()
                .get("MACNEUTRON_UPSCALE")
                .cloned()
        };
        assert_eq!(preset(None), None);
        assert_eq!(preset(Some("ultra-quality")).as_deref(), Some("ultra-quality"));
        assert_eq!(preset(Some("ultra-performance")).as_deref(), Some("ultra-performance"));
        assert_eq!(preset(Some("off")), None);
        assert_eq!(preset(Some("dlss")), None);
    }

    #[test]
    fn anisotropic_filtering_defaults_to_16x() {
        let forced = |value: Option<&str>| {
            settings(|s| s.anisotropy = value.map(|v| v.to_string()))
                .environment()
                .get("DXMT_MAX_ANISOTROPY")
                .cloned()
        };
        assert_eq!(forced(None).as_deref(), Some("16"));
        assert_eq!(forced(Some("game")), None);
        assert_eq!(forced(Some("4")).as_deref(), Some("4"));
        assert_eq!(forced(Some("8")).as_deref(), Some("8"));
        assert_eq!(forced(Some("16")).as_deref(), Some("16"));
        assert_eq!(forced(Some("2")).as_deref(), Some("16"));
        assert_eq!(forced(Some("x")).as_deref(), Some("16"));
    }

    #[test]
    fn launch_options_field_sits_over_the_controls() {
        let s = settings(|s| {
            s.graphics = Some("dxmt".into());
            s.launch_options = Some("MACNEUTRON_GRAPHICS=wined3d DXMT_D3D12_SM6=0 -dx12".into());
        });
        assert_eq!(s.environment()["MACNEUTRON_GRAPHICS"], "wined3d");
        assert_eq!(s.environment()["DXMT_D3D12_SM6"], "0");
        assert_eq!(s.launch_arguments(), vec!["-dx12"]);
        assert!(GameSettings::default().launch_arguments().is_empty());
    }

    #[test]
    fn store_round_trips_and_defaults_when_missing() {
        let dir = std::env::temp_dir().join(format!("mn-gsettings-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        let store = GameSettingsStore::new(dir.clone());
        assert_eq!(store.load("7").unwrap(), GameSettings::default());
        let s = GameSettings { graphics: Some("wined3d".into()), run_as: Some(RunAs::Windows), ..Default::default() };
        store.save(&s, "7").unwrap();
        assert_eq!(store.load("7").unwrap(), s);
        assert_eq!(store.run_as_overrides().get(&7), Some(&RunAs::Windows));
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn old_settings_files_still_load() {
        let dir = std::env::temp_dir().join(format!("mn-gsettings-old-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let store = GameSettingsStore::new(dir.clone());
        std::fs::write(dir.join("9.json"), r#"{"avx":false,"graphics":"d3dmetal"}"#).unwrap();
        assert_eq!(store.load("9").unwrap().graphics.as_deref(), Some("d3dmetal"));
        std::fs::write(dir.join("10.json"), r#"{"metalFX":false,"postAA":"cmaa2"}"#).unwrap();
        assert_eq!(store.load("10").unwrap().environment()["DXMT_MAX_ANISOTROPY"], "16");
        let _ = std::fs::remove_dir_all(&dir);
    }
}
