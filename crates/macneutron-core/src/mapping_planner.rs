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

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::{BTreeMap, HashMap};

    fn app(id: u32, oslist: &[&str], ty: &str) -> AppInfo {
        AppInfo {
            app_id: id,
            name: format!("App {id}"),
            r#type: ty.to_string(),
            oslist: oslist.iter().map(|s| s.to_string()).collect(),
        }
    }

    fn entry(key: &str, name: &str, with_config: bool) -> KVNode {
        let mut c = vec![KVNode::string("name", name)];
        if with_config {
            c.push(KVNode::string("config", ""));
        }
        c.push(KVNode::string("priority", "250"));
        KVNode::block(key, c)
    }

    fn name_at(nodes: &[KVNode], path: &[&str]) -> Option<String> {
        node_at(nodes, path).and_then(|n| n.string_value())
    }

    #[test]
    fn routes_by_platform() {
        let cases: [(&[&str], &str); 6] = [
            (&["macos"], "macneutron-native"),
            (&["windows"], "macneutron-proton"),
            (&["windows", "macos"], "macneutron-native"),
            (&["macos", "linux"], "macneutron-native"),
            (&["windows", "linux"], "macneutron-proton"),
            (&["windows", "macos", "linux"], "macneutron-native"),
        ];
        for (oslist, tool) in cases {
            let plan = MappingPlanner::plan(&[app(10, oslist, "game")], &HashMap::new());
            assert_eq!(plan["10"], ToolMapping::new(tool, 250), "{oslist:?}");
        }
    }

    #[test]
    fn linux_only_and_tools_are_not_mapped() {
        let plan = MappingPlanner::plan(
            &[app(1, &["linux"], "game"), app(2, &["windows"], "tool"), app(3, &["windows"], "dlc")],
            &HashMap::new(),
        );
        assert_eq!(plan.keys().cloned().collect::<Vec<_>>(), vec!["0".to_string()]);
    }

    #[test]
    fn global_entry_always_points_at_the_runtime() {
        assert_eq!(MappingPlanner::plan(&[], &HashMap::new())["0"], ToolMapping::new("macneutron-proton", 75));
    }

    #[test]
    fn run_as_windows_only_applies_to_dual_platform_games() {
        let plan = MappingPlanner::plan(
            &[app(1, &["windows", "macos"], "game"), app(2, &["macos"], "game")],
            &HashMap::from([(1, RunAs::Windows), (2, RunAs::Windows)]),
        );
        assert_eq!(plan["1"].tool, "macneutron-proton");
        assert_eq!(plan["2"].tool, "macneutron-native");
    }

    #[test]
    fn merge_keeps_other_tools_and_replaces_ours() {
        let existing = vec![entry("440", "proton_9", true), entry("99", "macneutron", true)];
        let plan = BTreeMap::from([
            ("0".to_string(), ToolMapping::new("macneutron", 75)),
            ("20".to_string(), ToolMapping::new("macneutron-native", 250)),
        ]);
        let merged = MappingPlanner::merged(&existing, &plan);
        assert_eq!(merged.iter().map(|n| n.key.clone()).collect::<Vec<_>>(), vec!["0", "20", "440"]);
        assert_eq!(MappingPlanner::current(&merged), plan);
        assert_eq!(name_at(&merged, &["440", "name"]).as_deref(), Some("proton_9"));
    }

    #[test]
    fn another_tools_entry_is_never_replaced() {
        // Spec §5: entries naming another tool are the user's explicit choice and are left alone.
        let existing = vec![entry("20", "proton_9", false)];
        let plan = BTreeMap::from([("20".to_string(), ToolMapping::new("macneutron", 250))]);
        let merged = MappingPlanner::merged(&existing, &plan);
        assert_eq!(merged.len(), 1);
        assert_eq!(name_at(&merged, &["20", "name"]).as_deref(), Some("proton_9"));
    }

    #[test]
    fn legacy_and_native_entries_are_ours() {
        let existing = vec![entry("1", "macneutron", true), entry("2", "macneutron-native", true)];
        assert!(MappingPlanner::claimed_by_other_tools(&existing).is_empty());
        assert_eq!(MappingPlanner::current(&existing).len(), 2);
        let plan = BTreeMap::from([("1".to_string(), ToolMapping::new("macneutron-proton", 250))]);
        let merged = MappingPlanner::merged(&existing, &plan);
        assert_eq!(name_at(&merged, &["1", "name"]).as_deref(), Some("macneutron-proton"));
        assert!(node_at(&merged, &["2"]).is_none());
    }
}
