//! What stops a VM setup (`macneutron vm-setup`), each case naming what it was about.

use std::fmt;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum VMSetupError {
    /// A payload compressor or format this setup doesn't read.
    Unsupported(String),
    /// A compressed stream that ends before its end.
    Truncated,
    /// A compressed stream that isn't one.
    Corrupt,
    /// A recipe line (1-based) that isn't one.
    BadRecipe { line: usize },
    /// A recipe file whose bytes no package and no overlay holds.
    Missing { path: String },
    /// A file whose bytes aren't the ones the manifest or recipe names.
    Mismatch { file: String },
    /// A tool (mkfs.erofs, tar) that exited with a status other than 0.
    Tool(String, i32),
    /// A download no source served right, three times over.
    Download { name: String },
    /// Less free space than the setup needs, in GB.
    NoSpace { needed: i64 },
    /// The caller asked the setup to stop (the launcher's cancel button).
    Cancelled,
}

impl fmt::Display for VMSetupError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            VMSetupError::Unsupported(what) => write!(f, "vm-setup: {what} isn't supported"),
            VMSetupError::Truncated => write!(f, "vm-setup: a package's payload is cut short"),
            VMSetupError::Corrupt => write!(f, "vm-setup: a package's payload doesn't decompress"),
            VMSetupError::BadRecipe { line } => {
                write!(f, "vm-setup: the parts' recipe is damaged at line {line}")
            }
            VMSetupError::Missing { path } => write!(f, "vm-setup: no package holds {path}"),
            VMSetupError::Mismatch { file } => {
                write!(f, "vm-setup: {file} isn't what the manifest names")
            }
            VMSetupError::Tool(name, status) => write!(f, "vm-setup: {name} exited with {status}"),
            VMSetupError::Download { name } => write!(f, "vm-setup: couldn't download {name}"),
            VMSetupError::NoSpace { needed } => write!(f, "vm-setup: it needs {needed} GB free"),
            VMSetupError::Cancelled => write!(f, "vm-setup: cancelled"),
        }
    }
}

impl std::error::Error for VMSetupError {}
