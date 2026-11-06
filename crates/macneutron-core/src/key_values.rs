//! Valve's text KeyValues (`config.vdf`, `libraryfolders.vdf`, `appmanifest_*.acf`).

use std::fmt;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum KeyValuesError {
    Unexpected(String, usize),
    UnterminatedString(usize),
    UnterminatedBlock(String),
}

impl fmt::Display for KeyValuesError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            KeyValuesError::Unexpected(what, offset) => write!(f, "{what} at byte {offset}"),
            KeyValuesError::UnterminatedString(offset) => {
                write!(f, "unterminated string starting at byte {offset}")
            }
            KeyValuesError::UnterminatedBlock(key) => write!(f, "block \"{key}\" is never closed"),
        }
    }
}

impl std::error::Error for KeyValuesError {}

/// One entry of Valve's text KeyValues.
/// Keys and strings are kept exactly as written, escapes included, so untouched parts of a file
/// round-trip byte for byte.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct KVNode {
    pub key: String,
    pub value: KVValue,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum KVValue {
    String(String),
    Block(Vec<KVNode>),
}

impl KVNode {
    pub fn new(key: impl Into<String>, value: KVValue) -> Self {
        KVNode { key: key.into(), value }
    }

    /// A key/value pair from plain strings; quotes and backslashes are escaped for you.
    pub fn string(key: &str, value: &str) -> KVNode {
        KVNode {
            key: KeyValues::escape(key),
            value: KVValue::String(KeyValues::escape(value)),
        }
    }

    pub fn block(key: &str, children: Vec<KVNode>) -> KVNode {
        KVNode {
            key: KeyValues::escape(key),
            value: KVValue::Block(children),
        }
    }

    /// The unescaped string value, or None for a block.
    pub fn string_value(&self) -> Option<String> {
        match &self.value {
            KVValue::String(raw) => Some(KeyValues::unescape(raw)),
            KVValue::Block(_) => None,
        }
    }

    pub fn children(&self) -> &[KVNode] {
        match &self.value {
            KVValue::Block(nodes) => nodes,
            KVValue::String(_) => &[],
        }
    }
}

pub struct KeyValues;

impl KeyValues {
    pub fn parse(text: &str) -> Result<Vec<KVNode>, KeyValuesError> {
        let mut parser = Parser::new(text.as_bytes());
        parser.block(None)
    }

    /// Steam's own layout: tab indentation, two tabs between key and value, braces on their own lines.
    pub fn serialize(nodes: &[KVNode]) -> String {
        let mut out = String::new();
        emit(&mut out, nodes, 0);
        out
    }

    pub fn escape(text: &str) -> String {
        text.replace('\\', "\\\\").replace('"', "\\\"")
    }

    pub fn unescape(raw: &str) -> String {
        let mut out = String::new();
        let mut escaping = false;
        for ch in raw.chars() {
            if escaping {
                out.push(ch);
                escaping = false;
            } else if ch == '\\' {
                escaping = true;
            } else {
                out.push(ch);
            }
        }
        out
    }
}

fn emit(out: &mut String, nodes: &[KVNode], depth: usize) {
    let indent: String = "\t".repeat(depth);
    for node in nodes {
        match &node.value {
            KVValue::String(raw) => {
                out.push_str(&format!("{indent}\"{}\"\t\t\"{}\"\n", node.key, raw));
            }
            KVValue::Block(children) => {
                out.push_str(&format!("{indent}\"{}\"\n{indent}{{\n", node.key));
                emit(out, children, depth + 1);
                out.push_str(&format!("{indent}}}\n"));
            }
        }
    }
}

struct Parser<'a> {
    bytes: &'a [u8],
    index: usize,
}

impl<'a> Parser<'a> {
    fn new(bytes: &'a [u8]) -> Self {
        Parser { bytes, index: 0 }
    }

    fn skip_whitespace(&mut self) {
        while self.index < self.bytes.len()
            && matches!(self.bytes[self.index], 0x20 | 0x09 | 0x0A | 0x0D)
        {
            self.index += 1;
        }
    }

    fn quoted(&mut self) -> Result<String, KeyValuesError> {
        if self.index >= self.bytes.len() || self.bytes[self.index] != b'"' {
            return Err(KeyValuesError::Unexpected("expected a quoted string".into(), self.index));
        }
        let start = self.index + 1;
        self.index = start;
        while self.index < self.bytes.len() {
            match self.bytes[self.index] {
                b'\\' => self.index += 2,
                b'"' => {
                    let slice = &self.bytes[start..self.index];
                    self.index += 1;
                    return Ok(String::from_utf8_lossy(slice).into_owned());
                }
                _ => self.index += 1,
            }
        }
        Err(KeyValuesError::UnterminatedString(start - 1))
    }

    /// Children until the closing brace of `closing` (or end of input at the top level).
    fn block(&mut self, closing: Option<&str>) -> Result<Vec<KVNode>, KeyValuesError> {
        let mut nodes = Vec::new();
        loop {
            self.skip_whitespace();
            if self.index >= self.bytes.len() {
                if let Some(c) = closing {
                    return Err(KeyValuesError::UnterminatedBlock(c.to_string()));
                }
                return Ok(nodes);
            }
            if self.bytes[self.index] == b'}' {
                if closing.is_none() {
                    return Err(KeyValuesError::Unexpected("unmatched }".into(), self.index));
                }
                self.index += 1;
                return Ok(nodes);
            }
            let key = self.quoted()?;
            self.skip_whitespace();
            if self.index >= self.bytes.len() {
                return Err(KeyValuesError::Unexpected(format!("missing value for \"{key}\""), self.index));
            }
            if self.bytes[self.index] == b'{' {
                self.index += 1;
                let children = self.block(Some(&key))?;
                nodes.push(KVNode { key, value: KVValue::Block(children) });
            } else {
                let value = self.quoted()?;
                nodes.push(KVNode { key, value: KVValue::String(value) });
            }
        }
    }
}

/// Case-insensitive lookup along a key path, the way Steam matches keys.
pub fn node_at(nodes: &[KVNode], path: &[&str]) -> Option<KVNode> {
    let head = path.first()?;
    let found = nodes.iter().find(|n| n.key.eq_ignore_ascii_case(head))?;
    if path.len() == 1 {
        Some(found.clone())
    } else {
        node_at(found.children(), &path[1..])
    }
}

/// Replaces the block at `path` with `children`, creating it and any missing parents at the end.
pub fn set_block_at(nodes: &mut Vec<KVNode>, path: &[&str], children: Vec<KVNode>) {
    let Some(head) = path.first() else { return };
    let index = nodes.iter().position(|n| n.key.eq_ignore_ascii_case(head));
    let replacement = if path.len() > 1 {
        let mut replacement = match index {
            Some(i) => nodes[i].children().to_vec(),
            None => Vec::new(),
        };
        set_block_at(&mut replacement, &path[1..], children);
        replacement
    } else {
        children
    };
    match index {
        Some(i) => nodes[i].value = KVValue::Block(replacement),
        None => nodes.push(KVNode::block(head, replacement)),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Shaped like Steam's config.vdf: tabs, nested blocks, escaped JSON inside a value.
    const STEAM_CONFIG: &str = "\"InstallConfigStore\"\n{\n\t\"Software\"\n\t{\n\t\t\"Valve\"\n\t\t{\n\t\t\t\"Steam\"\n\t\t\t{\n\t\t\t\t\"AutoUpdateWindowEnabled\"\t\t\"0\"\n\t\t\t\t\"Recent\"\t\t\"{\\\"version\\\":2,\\\"data\\\":[]}\"\n\t\t\t\t\"CompatToolMapping\"\n\t\t\t\t{\n\t\t\t\t\t\"440\"\n\t\t\t\t\t{\n\t\t\t\t\t\t\"name\"\t\t\"proton_9\"\n\t\t\t\t\t\t\"config\"\t\t\"\"\n\t\t\t\t\t\t\"priority\"\t\t\"250\"\n\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n";

    #[test]
    fn round_trips_steam_files_byte_for_byte() {
        assert_eq!(KeyValues::serialize(&KeyValues::parse(STEAM_CONFIG).unwrap()), STEAM_CONFIG);
    }

    #[test]
    fn looks_up_paths_case_insensitively() {
        let nodes = KeyValues::parse(STEAM_CONFIG).unwrap();
        let mapping = node_at(&nodes, &["InstallConfigStore", "software", "valve", "steam", "CompatToolMapping", "440"]);
        assert_eq!(node_at(mapping.as_ref().unwrap().children(), &["name"]).unwrap().string_value().as_deref(), Some("proton_9"));
        assert_eq!(
            node_at(&nodes, &["InstallConfigStore", "Software", "Valve", "Steam", "Recent"]).unwrap().string_value().as_deref(),
            Some(r#"{"version":2,"data":[]}"#)
        );
    }

    #[test]
    fn replacing_one_block_leaves_the_rest_untouched() {
        let mut nodes = KeyValues::parse(STEAM_CONFIG).unwrap();
        let path = ["InstallConfigStore", "Software", "Valve", "Steam", "CompatToolMapping"];
        set_block_at(&mut nodes, &path, vec![KVNode::block("0", vec![KVNode::string("name", "macneutron")])]);
        let out = KeyValues::serialize(&nodes);
        assert!(!out.contains("proton_9"));
        assert!(out.contains("\t\t\t\t\t\"0\"\n\t\t\t\t\t{\n\t\t\t\t\t\t\"name\"\t\t\"macneutron\"\n"));
        assert!(out.contains("\"Recent\"\t\t\"{\\\"version\\\":2,\\\"data\\\":[]}\""));
    }

    #[test]
    fn set_block_creates_missing_parents() {
        let mut nodes: Vec<KVNode> = Vec::new();
        set_block_at(&mut nodes, &["a", "b"], vec![KVNode::string("k", r#"say "hi" \o/"#)]);
        assert_eq!(node_at(&nodes, &["a", "b", "k"]).unwrap().string_value().as_deref(), Some(r#"say "hi" \o/"#));
        let expected = concat!("\"k\"\t\t\"say \\\"hi\\\" \\\\o/\"", "\n");
        assert!(KeyValues::serialize(&nodes).contains(expected));
    }

    #[test]
    fn rejects_broken_input() {
        assert!(KeyValues::parse("\"a\"\n{\n\"k\" \"v\"\n").is_err());
        assert!(KeyValues::parse("\"a\" \"unterminated").is_err());
        assert!(KeyValues::parse("}").is_err());
    }
}
