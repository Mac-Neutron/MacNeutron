//! Install MacNeutron (spec 2026-10-25-macneutron-installer-design §3.2): asks once (not when the app's update opened
//! it with --from-app), then app_installer verifies its bundled MacNeutron.app, quits MacNeutron and puts the app in
//! /Applications, which it opens. Every failure is an alert, and /Applications/MacNeutron.app stays as it was.

use std::cell::RefCell;
use std::path::Path;
use std::time::{Duration, Instant};

use macneutron_core::app_installer::{self, AppInstallerError};
use macneutron_core::code_identity::CodeIdentity;
use macneutron_core::process_runner::SystemProcessRunner;
use macneutron_core::updater::{self, UpdateError};
use objc2::rc::Retained;
use objc2_app_kit::{
    NSAlert, NSAlertFirstButtonReturn, NSApplication, NSApplicationActivationPolicy, NSRunningApplication,
};
use objc2_foundation::{ns_string, MainThreadMarker, NSBundle, NSDate, NSRunLoop, NSString};

fn main() {
    let code = run();
    updater::remove_installer_folders(&std::env::temp_dir()); // the app's update unpacked this installer into one
    std::process::exit(code);
}

fn run() -> i32 {
    let mtm = MainThreadMarker::new().expect("the installer runs on the main thread");
    let app = NSApplication::sharedApplication(mtm);
    app.setActivationPolicy(NSApplicationActivationPolicy::Regular);
    app.activate();
    let bundle = NSBundle::mainBundle();
    let me = std::path::PathBuf::from(bundle.bundlePath().to_string());
    let version = bundle
        .objectForInfoDictionaryKey(ns_string!("CFBundleShortVersionString"))
        .and_then(|value| value.downcast::<NSString>().ok())
        .map_or_else(|| "dev".to_string(), |value| value.to_string());
    let from_app = std::env::args().any(|argument| argument == "--from-app");
    flow(
        CodeIdentity::team(&me).as_deref(),
        from_app,
        &version,
        &mut |message, informative, buttons| alert(mtm, message, informative, buttons),
        |team| install(&me, team, &version),
        || {
            let _ = std::process::Command::new("/usr/bin/open").arg(app_installer::target()).status();
        },
    )
}

/// The installer's decisions, its alerts answered by `alert` (the index of the button chosen): the exit code.
fn flow(
    team: Option<&str>,
    from_app: bool,
    version: &str,
    alert: &mut dyn FnMut(&str, &str, &[&str]) -> usize,
    install: impl FnOnce(&str) -> Result<(), String>,
    open: impl FnOnce(),
) -> i32 {
    let Some(team) = team else {
        alert("This installer isn't signed by MacNeutron's developer.", "", &["OK"]);
        return 1;
    };
    let question = format!("Install MacNeutron {version} in Applications?");
    if !from_app && alert(&question, "MacNeutron will quit.", &["Install", "Cancel"]) != 0 {
        return 0;
    }
    if let Err(message) = install(team) {
        alert(&message, "", &["OK"]);
        return 1;
    }
    open();
    0
}

/// app_installer::install with updater::verify's checks; the alert's text is the error's, the verify's UpdateError
/// included.
fn install(me: &Path, team: &str, version: &str) -> Result<(), String> {
    let bundled = me.join("Contents/Helpers/MacNeutron.app");
    let log = std::env::temp_dir().join("install-macneutron-spctl.txt");
    let runner = SystemProcessRunner::new();
    // ponytail: app_installer's verify returns AppInstallerError, which has no UpdateError case; the refusal is kept
    // here and shown in place of the stand-in error.
    let refused: RefCell<Option<UpdateError>> = RefCell::new(None);
    let verify = |bundle: &Path| {
        updater::verify(bundle, team, version, &log, &runner, &updater::identifiers()).map_err(|error| {
            *refused.borrow_mut() = Some(error);
            AppInstallerError::StillRunning
        })
    };
    let quit = quit_macneutron;
    app_installer::install(&bundled, &app_installer::target(), &runner, &verify, &quit)
        .map_err(|error| refused.take().map_or_else(|| error.to_string(), |refusal| refusal.to_string()))
}

/// Every running MacNeutron asked to quit, waited for up to 30 s; true when none is left (none running is true: the
/// app's update quits it right after opening the installer).
fn quit_macneutron() -> bool {
    let apps: Vec<Retained<NSRunningApplication>> = updater::identifiers()
        .iter()
        .flat_map(|id| NSRunningApplication::runningApplicationsWithBundleIdentifier(&NSString::from_str(id)).to_vec())
        .collect();
    for app in &apps {
        app.terminate();
    }
    let deadline = Instant::now() + Duration::from_secs(30);
    while apps.iter().any(|app| !app.isTerminated()) && Instant::now() < deadline {
        // isTerminated changes only as the run loop runs.
        NSRunLoop::currentRunLoop().runUntilDate(&NSDate::dateWithTimeIntervalSinceNow(0.2));
    }
    apps.iter().all(|app| app.isTerminated())
}

/// The button chosen, 0 for the first.
fn alert(mtm: MainThreadMarker, message: &str, informative: &str, buttons: &[&str]) -> usize {
    let alert = NSAlert::new(mtm);
    alert.setMessageText(&NSString::from_str(message));
    alert.setInformativeText(&NSString::from_str(informative));
    for button in buttons {
        alert.addButtonWithTitle(&NSString::from_str(button));
    }
    usize::try_from(alert.runModal() - NSAlertFirstButtonReturn).unwrap_or(usize::MAX)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::Cell;

    /// The alerts shown (message, informative, buttons), each answered in turn from `answers`.
    struct Front {
        answers: Vec<usize>,
        shown: Vec<(String, String, Vec<String>)>,
    }

    impl Front {
        fn new(answers: &[usize]) -> Self {
            Front { answers: answers.to_vec(), shown: Vec::new() }
        }

        fn alert(&mut self, message: &str, informative: &str, buttons: &[&str]) -> usize {
            self.shown.push((message.into(), informative.into(), buttons.iter().map(|b| b.to_string()).collect()));
            if self.answers.is_empty() { 0 } else { self.answers.remove(0) }
        }
    }

    /// The exit code, the team install was called with, and whether the app was opened.
    fn run_flow(
        team: Option<&str>,
        from_app: bool,
        front: &mut Front,
        result: Result<(), String>,
    ) -> (i32, Option<String>, bool) {
        let installed = RefCell::new(None);
        let opened = Cell::new(false);
        let code = flow(
            team,
            from_app,
            "1.2.3",
            &mut |m, i, b| front.alert(m, i, b),
            |team| {
                *installed.borrow_mut() = Some(team.to_string());
                result
            },
            || opened.set(true),
        );
        (code, installed.take(), opened.get())
    }

    fn alert_of(message: &str, informative: &str, buttons: &[&str]) -> (String, String, Vec<String>) {
        (message.into(), informative.into(), buttons.iter().map(|b| b.to_string()).collect())
    }

    #[test]
    fn an_unsigned_installer_installs_nothing() {
        let mut front = Front::new(&[]);
        assert_eq!(run_flow(None, false, &mut front, Ok(())), (1, None, false));
        assert_eq!(front.shown, [alert_of("This installer isn't signed by MacNeutron's developer.", "", &["OK"])]);
    }

    #[test]
    fn it_asks_once_and_installs_with_its_team() {
        let mut front = Front::new(&[0]);
        assert_eq!(run_flow(Some("TEAM"), false, &mut front, Ok(())), (0, Some("TEAM".into()), true));
        assert_eq!(
            front.shown,
            [alert_of("Install MacNeutron 1.2.3 in Applications?", "MacNeutron will quit.", &["Install", "Cancel"])]
        );
    }

    #[test]
    fn cancel_installs_nothing() {
        let mut front = Front::new(&[1]);
        assert_eq!(run_flow(Some("TEAM"), false, &mut front, Ok(())), (0, None, false));
        assert_eq!(front.shown.len(), 1);
    }

    #[test]
    fn the_apps_update_isnt_asked_again() {
        let mut front = Front::new(&[]);
        assert_eq!(run_flow(Some("TEAM"), true, &mut front, Ok(())), (0, Some("TEAM".into()), true));
        assert!(front.shown.is_empty());
    }

    #[test]
    fn a_refused_verify_is_the_alert() {
        // No Contents/Info.plist in the bundled app: updater::verify refuses it before app_installer looks at
        // /Applications or quits anything.
        let me = std::env::temp_dir().join(format!("macneutron-installer-test-{}", std::process::id()));
        std::fs::create_dir_all(me.join("Contents/Helpers/MacNeutron.app")).unwrap();
        let result = install(&me, "TEAM", "1.2.3");
        let _ = std::fs::remove_dir_all(&me);
        let refusal = "The download isn't signed by MacNeutron's developer: the download holds no MacNeutron.app";
        assert_eq!(result, Err(refusal.into()));
    }

    #[test]
    fn a_failure_is_an_alert_and_opens_nothing() {
        let mut front = Front::new(&[]);
        let error = AppInstallerError::StillRunning.to_string();
        assert_eq!(run_flow(Some("TEAM"), true, &mut front, Err(error.clone())), (1, Some("TEAM".into()), false));
        assert_eq!(front.shown, [alert_of(&error, "", &["OK"])]);
    }
}
