//! The Proton verbs Steam uses when it launches a game through a compatibility tool.

use std::fmt;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Verb {
    Run,
    WaitForExitAndRun,
    RunInPrefix,
    GetCompatPath,
    GetNativePath,
}

impl Verb {
    pub fn raw_value(self) -> &'static str {
        match self {
            Verb::Run => "run",
            Verb::WaitForExitAndRun => "waitforexitandrun",
            Verb::RunInPrefix => "runinprefix",
            Verb::GetCompatPath => "getcompatpath",
            Verb::GetNativePath => "getnativepath",
        }
    }

    #[allow(clippy::should_implement_trait)]
    pub fn from_str(s: &str) -> Option<Verb> {
        Some(match s {
            "run" => Verb::Run,
            "waitforexitandrun" => Verb::WaitForExitAndRun,
            "runinprefix" => Verb::RunInPrefix,
            "getcompatpath" => Verb::GetCompatPath,
            "getnativepath" => Verb::GetNativePath,
            _ => return None,
        })
    }

    pub fn all() -> [Verb; 5] {
        [Verb::Run, Verb::WaitForExitAndRun, Verb::RunInPrefix, Verb::GetCompatPath, Verb::GetNativePath]
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum LaunchRequestError {
    MissingVerb,
    UnknownVerb(String),
    MissingTarget(Verb),
}

impl fmt::Display for LaunchRequestError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            LaunchRequestError::MissingVerb => write!(f, "no verb given"),
            LaunchRequestError::UnknownVerb(verb) => write!(f, "unknown verb '{verb}'"),
            LaunchRequestError::MissingTarget(verb) => {
                write!(f, "verb '{}' needs a target path", verb.raw_value())
            }
        }
    }
}

impl std::error::Error for LaunchRequestError {}

/// `proton <verb> <target> [args...]` exactly as Steam invokes it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LaunchRequest {
    pub verb: Verb,
    /// The executable to run, or the path to convert for the get*path verbs.
    pub target: String,
    /// Everything after the target, passed to the game untouched.
    pub arguments: Vec<String>,
}

impl LaunchRequest {
    pub fn parse(argv: &[String]) -> Result<LaunchRequest, LaunchRequestError> {
        let first = argv.first().ok_or(LaunchRequestError::MissingVerb)?;
        let verb = Verb::from_str(first).ok_or_else(|| LaunchRequestError::UnknownVerb(first.clone()))?;
        if argv.len() < 2 {
            return Err(LaunchRequestError::MissingTarget(verb));
        }
        Ok(LaunchRequest {
            verb,
            target: argv[1].clone(),
            arguments: argv[2..].to_vec(),
        })
    }
}
