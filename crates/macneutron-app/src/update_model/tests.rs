//! UpdateModelTests.swift's 5 tests.

use super::*;
use macneutron_core::test_support::{make_temp_dir, releases_json, RecordingNotifier};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::time::UNIX_EPOCH;

struct Updates {
    model: Arc<UpdateModel>,
    /// GitHub's release list, answering every request with the same reply: how many were made.
    github_calls: Arc<AtomicUsize>,
    notifier: Arc<RecordingNotifier>,
    clock: Arc<Mutex<SystemTime>>,
}

impl Updates {
    fn advance(&self, hours: u64) {
        *self.clock.lock().unwrap() += Duration::from_secs(hours * 3600);
    }

    fn calls(&self) -> usize {
        self.github_calls.load(Ordering::SeqCst)
    }
}

/// A model whose defaults are a plist in a temp folder: the real domain would keep every run's choices.
fn make_updates(team: Option<&str>) -> Updates {
    let github_calls = Arc::new(AtomicUsize::new(0));
    let notifier = Arc::new(RecordingNotifier::default());
    let clock = Arc::new(Mutex::new(UNIX_EPOCH + Duration::from_secs(1_800_000_000)));
    let reply = releases_json(&[("v0.9.0", false, false, &["MacNeutron-0.9.0.zip", "SHA256SUMS"])], "https://example.invalid");
    let (counted, now) = (github_calls.clone(), clock.clone());
    let services = UpdateServices {
        defaults: Box::new(PlistDefaults::new(make_temp_dir().join("defaults.plist"))),
        notifier: notifier.clone(),
        get: Arc::new(move |_| {
            counted.fetch_add(1, Ordering::SeqCst);
            Ok(reply.clone())
        }),
        now: Box::new(move || *now.lock().unwrap()),
        open: Box::new(|_, _| panic!("no test installs")),
        terminate: Box::new(|| panic!("no test quits")),
        hourly: false,
    };
    let model = UpdateModel::new("0.8.0", PathBuf::from("/Applications/MacNeutron.app"), team.map(str::to_string), services);
    Updates { model, github_calls, notifier, clock }
}

#[test]
fn check_finds_a_release_and_notifies_once() {
    let updates = make_updates(Some("ABCDE12345"));
    updates.model.check();
    assert_eq!(updates.model.state().available.map(|release| release.version).as_deref(), Some("0.9.0"));
    assert_eq!(updates.notifier.posted(), ["MacNeutron 0.9.0 is available."]);
    updates.model.check();
    assert_eq!(updates.notifier.posted().len(), 1); // the same version isn't announced again
}

#[test]
fn check_if_due_waits_a_day_and_honours_the_switch() {
    let updates = make_updates(Some("ABCDE12345"));
    updates.model.check_if_due();
    assert_eq!(updates.calls(), 1);
    updates.advance(23);
    updates.model.check_if_due();
    assert_eq!(updates.calls(), 1);
    updates.advance(2);
    updates.model.check_if_due();
    assert_eq!(updates.calls(), 2);
    updates.model.set_automatic(false);
    updates.advance(50);
    updates.model.check_if_due();
    assert_eq!(updates.calls(), 2);
}

#[test]
fn dev_builds_never_check() {
    let updates = make_updates(None);
    assert!(!updates.model.enabled());
    updates.model.check();
    updates.model.check_if_due();
    assert_eq!(updates.calls(), 0);
}

#[test]
fn launch_checks_even_within_a_day() {
    let updates = make_updates(Some("ABCDE12345"));
    updates.model.check_if_due();
    updates.model.check_at_launch(); // relaunched within the day: the update found is offered again
    assert_eq!(updates.calls(), 2);
    updates.model.set_automatic(false);
    updates.model.check_at_launch();
    assert_eq!(updates.calls(), 2);
}

#[test]
fn the_installer_opens_as_a_new_copy_from_the_app() {
    // A running Install MacNeutron (one the player opened) mustn't be brought forward in its place, without --from-app.
    let configuration = UpdateModel::installer_configuration();
    assert_eq!(configuration.arguments, ["--from-app"]);
    assert!(!configuration.allows_running_application_substitution);
}
