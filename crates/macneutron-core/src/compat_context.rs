//! The per-game paths Steam hands a compatibility tool through its environment.

use std::collections::BTreeMap;
use std::fmt;
use std::path::PathBuf;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CompatContextError {
    Missing(String),
}

impl fmt::Display for CompatContextError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CompatContextError::Missing(name) => {
                write!(f, "{name} is not set; macneutron launch must be started by Steam")
            }
        }
    }
}

impl std::error::Error for CompatContextError {}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CompatContext {
    pub data_path: PathBuf,
    pub app_id: String,
}

impl CompatContext {
    pub fn new(env: &BTreeMap<String, String>) -> Result<Self, CompatContextError> {
        let data = env
            .get("STEAM_COMPAT_DATA_PATH")
            .filter(|d| !d.is_empty())
            .ok_or_else(|| CompatContextError::Missing("STEAM_COMPAT_DATA_PATH".to_string()))?;
        let app_id = env
            .get("SteamAppId")
            .filter(|s| !s.is_empty())
            .cloned()
            .unwrap_or_else(|| "0".to_string());
        Ok(CompatContext {
            data_path: PathBuf::from(data),
            app_id,
        })
    }

    pub fn prefix(&self) -> PathBuf {
        self.data_path.join("pfx")
    }

    pub fn version_file(&self) -> PathBuf {
        self.data_path.join("version")
    }

    pub fn lock_file(&self) -> PathBuf {
        self.data_path.join("macneutron.lock")
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeMap;

    fn env(pairs: &[(&str, &str)]) -> BTreeMap<String, String> {
        pairs.iter().map(|(k, v)| (k.to_string(), v.to_string())).collect()
    }

    #[test]
    fn reads_steam_compat_environment() {
        let context = CompatContext::new(&env(&[
            ("STEAM_COMPAT_DATA_PATH", "/Users/me/Library/Application Support/Steam/steamapps/compatdata/42"),
            ("SteamAppId", "42"),
        ]))
        .unwrap();
        assert_eq!(context.app_id, "42");
        assert_eq!(context.prefix(), PathBuf::from("/Users/me/Library/Application Support/Steam/steamapps/compatdata/42/pfx"));
        assert_eq!(context.version_file().file_name().unwrap(), "version");
        assert_eq!(context.lock_file().file_name().unwrap(), "macneutron.lock");
    }

    #[test]
    fn app_id_defaults_to_zero() {
        let context = CompatContext::new(&env(&[("STEAM_COMPAT_DATA_PATH", "/tmp/x"), ("SteamAppId", "")])).unwrap();
        assert_eq!(context.app_id, "0");
    }

    #[test]
    fn missing_data_path_is_an_error() {
        assert_eq!(
            CompatContext::new(&env(&[("SteamAppId", "42")])),
            Err(CompatContextError::Missing("STEAM_COMPAT_DATA_PATH".to_string()))
        );
    }
}
