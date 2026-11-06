//!
//! Decides which compatibility tool every app should use (spec §5).

use crate::app_info::AppInfo;
use crate::key_values::{node_at, KVNode};
use crate::KeyValues;

/// Which build of a dual-platform game to run.
#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum RunAs {
    Mac,
    Windows,
}

/// A `CompatToolMapping` entry in Steam's config.vdf.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ToolMapping {
    pub tool: String,
    pub priority: i64,
}

impl ToolMapping {
    pub fn new(tool: impl Into<String>, priority: i64) -> Self {
        ToolMapping { tool: tool.into(), priority }
    }
}

pub struct MappingPlanner;

impl MappingPlanner {
    /// Steam maps Auto-Cloud's Windows roots into a tool's prefix only when its name contains "proton" (and ignores
    /// tools whose name contains "arm64").
    pub const RUNTIME_TOOL: &'static str = "macneutron-proton";
    /// The runtime's name before Steam Cloud: still registered until no mapping uses it.
    pub const LEGACY_RUNTIME_TOOL: &'static str = "macneutron";
    pub const NATIVE_TOOL: &'static str = "macneutron-native";
    /// Above Valve's automatic mappings (100), so explicit entries always win.
    pub const APP_PRIORITY: i64 = 250;
    pub const GLOBAL_PRIORITY: i64 = 75;

    /// Tools such as Steam Linux Runtime or Proton must never be mapped.
    pub fn is_mappable_type(t: &str) -> bool {
        matches!(t, "game" | "demo" | "application")
    }

    pub fn plan(apps: &[AppInfo], run_as: &std::collections::HashMap<u32, RunAs>) -> std::collections::BTreeMap<String, ToolMapping> {
        let mut plan = std::collections::BTreeMap::new();
        plan.insert("0".to_string(), ToolMapping::new(Self::RUNTIME_TOOL, Self::GLOBAL_PRIORITY));
        for app in apps {
            if !Self::is_mappable_type(&app.r#type) {
                continue;
            }
            let windows = app.oslist.contains("windows");
            let tool: Option<&str> = if app.oslist.contains("macos") {
                if run_as.get(&app.app_id) == Some(&RunAs::Windows) && windows {
                    Some(Self::RUNTIME_TOOL)
                } else {
                    Some(Self::NATIVE_TOOL)
                }
            } else if windows {
                Some(Self::RUNTIME_TOOL)
            } else {
                None
            };
            if let Some(tool) = tool {
                plan.insert(app.app_id.to_string(), ToolMapping::new(tool, Self::APP_PRIORITY));
            }
        }
        plan
    }

    /// MacNeutron's entries in an existing `CompatToolMapping` block.
    pub fn current(mapping_block: &[KVNode]) -> std::collections::BTreeMap<String, ToolMapping> {
        let mut result = std::collections::BTreeMap::new();
        for entry in mapping_block {
            let tool = match node_at(entry.children(), &["name"]).and_then(|n| n.string_value()) {
                Some(t) => t,
                None => continue,
            };
            if !Self::is_ours(&tool) {
                continue;
            }
            let priority = node_at(entry.children(), &["priority"])
                .and_then(|n| n.string_value())
                .and_then(|s| s.parse::<i64>().ok())
                .unwrap_or(0);
            result.insert(KeyValues::unescape(&entry.key), ToolMapping::new(tool, priority));
        }
        result
    }

    /// App IDs mapped to a tool other than MacNeutron's: the user's explicit choice, left alone (spec §5).
    pub fn claimed_by_other_tools(mapping_block: &[KVNode]) -> std::collections::HashSet<String> {
        mapping_block
            .iter()
            .filter_map(|entry| {
                let tool = node_at(entry.children(), &["name"])
                    .and_then(|n| n.string_value())
                    .unwrap_or_default();
                if tool.is_empty() || Self::is_ours(&tool) {
                    None
                } else {
                    Some(KeyValues::unescape(&entry.key))
                }
            })
            .collect()
    }

    /// The new `CompatToolMapping` block: other tools' entries kept untouched, ours replaced by `plan`
    /// (except for apps another tool claims), sorted by app ID for a stable file.
    pub fn merged(
        mapping_block: &[KVNode],
        plan: &std::collections::BTreeMap<String, ToolMapping>,
    ) -> Vec<KVNode> {
        let claimed = Self::claimed_by_other_tools(mapping_block);
        let mut others: Vec<KVNode> = mapping_block
            .iter()
            .filter(|entry| {
                let key = KeyValues::unescape(&entry.key);
                let tool = node_at(entry.children(), &["name"])
                    .and_then(|n| n.string_value())
                    .unwrap_or_default();
                claimed.contains(&key) || (!Self::is_ours(&tool) && !plan.contains_key(&key))
            })
            .cloned()
            .collect();
        let mut ours: Vec<KVNode> = plan
            .iter()
            .filter(|(app_id, _)| !claimed.contains(*app_id))
            .map(|(app_id, mapping)| {
                KVNode::block(
                    app_id,
                    vec![
                        KVNode::string("name", &mapping.tool),
                        KVNode::string("config", ""),
                        KVNode::string("priority", &mapping.priority.to_string()),
                    ],
                )
            })
            .collect();
        others.append(&mut ours);
        others.sort_by_key(|n| {
            KeyValues::unescape(&n.key).parse::<u64>().unwrap_or(u64::MAX)
        });
        others
    }

    /// Every MacNeutron tool, the legacy runtime name included, so its entries are migrated.
    pub fn is_ours(tool: &str) -> bool {
        tool.starts_with("macneutron")
    }
}
