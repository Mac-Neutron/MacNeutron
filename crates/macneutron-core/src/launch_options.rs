//! A game's launch options as one line: the Games window's field, or Steam's own Launch Options.

use std::collections::{BTreeMap, BTreeSet};
use std::path::Path;

use crate::key_values::node_at;
use crate::KeyValues;

pub struct LaunchOptions;

impl LaunchOptions {
    /// The leading `NAME=value` words are variables (a later one of a name wins); every word from the first other one
    /// on is a game argument. A first `/usr/bin/env` and every `%command%` are dropped, so a line pasted from Steam or
    /// the README reads the same.
    ///
    /// ponytail: no quoting; add it when someone needs a value with a space.
    pub fn parse(line: &str) -> (BTreeMap<String, String>, Vec<String>) {
        let mut words: Vec<&str> = line.split_whitespace().collect();
        if words.first() == Some(&"/usr/bin/env") {
            words.remove(0);
        }
        words.retain(|w| *w != "%command%");
        let mut environment = BTreeMap::new();
        let mut index = 0;
        while index < words.len() && Self::is_assignment(words[index]) {
            let word = words[index];
            let equals = word.find('=').unwrap();
            environment.insert(word[..equals].to_string(), word[equals + 1..].to_string());
            index += 1;
        }
        (environment, words[index..].iter().map(|w| w.to_string()).collect())
    }

    /// Each app's non-empty Launch Options in Steam's `localconfig.vdf`, read only (Steam saves the file when it quits,
    /// so an edit made in Steam can show late). An unreadable file gives none.
    pub fn steam_lines(local_config: &Path) -> BTreeMap<u32, String> {
        let mut lines = BTreeMap::new();
        let Ok(text) = std::fs::read_to_string(local_config) else { return lines };
        let Ok(nodes) = KeyValues::parse(&text) else { return lines };
        let Some(apps) = node_at(
            &nodes,
            &["UserLocalConfigStore", "Software", "Valve", "Steam", "apps"],
        ) else {
            return lines;
        };
        for app in apps.children() {
            let Some(id) = KeyValues::unescape(&app.key).parse::<u32>().ok() else { continue };
            let Some(line) = node_at(app.children(), &["LaunchOptions"]).and_then(|n| n.string_value()) else {
                continue;
            };
            if !line.is_empty() {
                lines.insert(id, line);
            }
        }
        lines
    }

    /// What's wrong with a game's Steam launch line, for the Games window: macOS Steam runs it without a shell, so a
    /// first `NAME=value` word is run as the program ("OS Error 260"); and its variables win over the app's.
    pub fn problems(steam_line: &str, app_environment: &BTreeMap<String, String>) -> Vec<String> {
        let mut problems = Vec::new();
        if let Some(first) = steam_line.split_whitespace().next() {
            if Self::is_assignment(first) {
                problems.push("Steam can't start the game with this line: begin it with /usr/bin/env".to_string());
            }
        }
        let overridden: Vec<String> = Self::parse(steam_line)
            .0
            .into_keys()
            .filter(|k| app_environment.contains_key(k))
            .collect::<BTreeSet<String>>()
            .into_iter()
            .collect();
        if !overridden.is_empty() {
            problems.push(format!("Steam's launch options override: {}", overridden.join(", ")));
        }
        problems
    }

    /// `NAME=value`, `NAME` a shell name.
    pub fn is_assignment(word: &str) -> bool {
        let Some(equals) = word.find('=') else { return false };
        let Some(first) = word.chars().next() else { return false };
        if !(first == '_' || first.is_ascii_alphabetic()) {
            return false;
        }
        word[..equals]
            .chars()
            .all(|c| c == '_' || c.is_ascii_alphabetic() || c.is_ascii_digit())
    }
}

#[cfg(test)]
mod tests {
    use super::LaunchOptions as LO;
    use std::collections::BTreeMap;

    fn env(pairs: &[(&str, &str)]) -> BTreeMap<String, String> {
        pairs.iter().map(|(k, v)| (k.to_string(), v.to_string())).collect()
    }

    #[test]
    fn split_into_variables_and_arguments() {
        let plain = LO::parse("DXMT_D3D12_SM6=0 MACNEUTRON_LOG=1 -dx12 -windowed");
        assert_eq!(plain.0, env(&[("DXMT_D3D12_SM6", "0"), ("MACNEUTRON_LOG", "1")]));
        assert_eq!(plain.1, vec!["-dx12", "-windowed"]);

        let pasted = LO::parse("/usr/bin/env A=1 -foo %command% -bar");
        assert_eq!(pasted.0, env(&[("A", "1")]));
        assert_eq!(pasted.1, vec!["-foo", "-bar"]);

        assert_eq!(LO::parse("A=1 A=2").0, env(&[("A", "2")]));
        assert!(LO::parse("-x B=2").0.is_empty());
        assert_eq!(LO::parse("-x B=2").1, vec!["-x", "B=2"]);
        assert_eq!(LO::parse("1A=2").1, vec!["1A=2"]);
        assert!(LO::parse("  ").0.is_empty());
        assert!(LO::parse("").1.is_empty());
    }

    #[test]
    fn steam_line_problems() {
        assert_eq!(
            LO::problems("MACNEUTRON_LOG=1 %command%", &BTreeMap::new()),
            vec!["Steam can't start the game with this line: begin it with /usr/bin/env"]
        );
        assert_eq!(
            LO::problems(
                "/usr/bin/env MACNEUTRON_LOG=1 DXMT_MAX_ANISOTROPY=4 %command%",
                &env(&[("DXMT_MAX_ANISOTROPY", "16"), ("MACNEUTRON_LOG", "1"), ("X", "1")]),
            ),
            vec!["Steam's launch options override: DXMT_MAX_ANISOTROPY, MACNEUTRON_LOG"]
        );
        assert!(LO::problems("/usr/bin/env %command% -dx12", &env(&[("A", "1")])).is_empty());
    }
}
