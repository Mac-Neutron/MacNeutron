//! The app's own updates, as the menu and Settings show them: a check at launch and then daily while "Check
//! automatically" is on, and the install the player asks for (spec §5). Only a Developer ID build has a team, so a
//! `make app` build never checks (UpdateModel.swift).

use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex, Weak};
use std::time::{Duration, SystemTime};

use macneutron_core::{app_installer, updater, AppleScriptNotifier, Notifier, Release, SystemProcessRunner, UpdateOutcome};

use crate::OnChange;

/// `UserDefaults`: the few preferences the update check keeps.
pub trait Defaults: Send + Sync {
    fn value(&self, key: &str) -> Option<plist::Value>;
    fn set(&self, key: &str, value: plist::Value);
}

/// Defaults kept in one plist file (CFPreferences takes an absolute path as a domain, which Swift's tests use).
pub struct PlistDefaults {
    path: PathBuf,
    lock: Mutex<()>,
}

impl PlistDefaults {
    pub fn new(path: PathBuf) -> Self {
        PlistDefaults { path, lock: Mutex::new(()) }
    }

    /// The app's own domain. ponytail: cfprefsd caches this file; the AppKit layer supplies an NSUserDefaults-backed
    /// `Defaults` if the app and this file ever disagree.
    pub fn standard() -> Self {
        let home = std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"));
        Self::new(home.join(format!("Library/Preferences/{}.plist", updater::IDENTIFIER)))
    }

    fn read(&self) -> plist::Dictionary {
        plist::Value::from_file(&self.path).ok().and_then(plist::Value::into_dictionary).unwrap_or_default()
    }
}

impl Defaults for PlistDefaults {
    fn value(&self, key: &str) -> Option<plist::Value> {
        let _held = self.lock.lock().unwrap();
        self.read().get(key).cloned()
    }

    fn set(&self, key: &str, value: plist::Value) {
        let _held = self.lock.lock().unwrap();
        let mut dictionary = self.read();
        dictionary.insert(key.to_string(), value);
        if let Some(folder) = self.path.parent() {
            let _ = std::fs::create_dir_all(folder);
        }
        let _ = plist::Value::Dictionary(dictionary).to_file_binary(&self.path);
    }
}

/// GitHub's release list at a URL.
pub type Get = Arc<dyn Fn(&str) -> Result<Vec<u8>, String> + Send + Sync>;
/// `NSWorkspace.openApplication(at:configuration:)`.
pub type Open = Box<dyn Fn(&Path, &InstallerConfiguration) -> Result<(), String> + Send + Sync>;

/// `NSWorkspace.OpenConfiguration` as far as `install` sets it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InstallerConfiguration {
    pub arguments: Vec<String>,
    pub allows_running_application_substitution: bool,
}

/// `UpdateModel`'s services: Swift's init defaults, and the two AppKit calls `install` makes.
pub struct UpdateServices {
    pub defaults: Box<dyn Defaults>,
    pub notifier: Arc<dyn Notifier>,
    pub get: Get,
    pub now: Box<dyn Fn() -> SystemTime + Send + Sync>,
    pub open: Open,
    /// `NSApplication.terminate`.
    pub terminate: Box<dyn Fn() + Send + Sync>,
    /// Check at launch, then hourly whether a day has passed (only with a team).
    pub hourly: bool,
}

impl UpdateServices {
    pub fn standard() -> Self {
        UpdateServices {
            defaults: Box::new(PlistDefaults::standard()),
            notifier: Arc::new(AppleScriptNotifier::new(None)),
            get: Arc::new(fetch),
            now: Box::new(SystemTime::now),
            open: Box::new(open),
            // ponytail: the AppKit layer supplies NSApplication.terminate (it runs AppModel::will_terminate first).
            terminate: Box::new(|| std::process::exit(0)),
            hourly: true,
        }
    }
}

/// GitHub's release list, through curl (Swift: an ephemeral URLSession); anything but HTTP 200 fails.
pub fn fetch(url: &str) -> Result<Vec<u8>, String> {
    let output = std::process::Command::new("/usr/bin/curl")
        .args(["-fsSL", "-H", "Accept: application/vnd.github+json", "--"])
        .arg(url)
        .output()
        .map_err(|error| error.to_string())?;
    if !output.status.success() {
        return Err(format!("the request failed (curl exit {})", output.status.code().unwrap_or(-1)));
    }
    Ok(output.stdout)
}

/// `open(1)`: `-n` opens a new copy rather than bringing a running one forward.
pub fn open(bundle: &Path, configuration: &InstallerConfiguration) -> Result<(), String> {
    let mut command = std::process::Command::new("/usr/bin/open");
    if !configuration.allows_running_application_substitution {
        command.arg("-n");
    }
    command.arg(bundle).arg("--args").args(&configuration.arguments);
    match command.status() {
        Ok(status) if status.success() => Ok(()),
        Ok(status) => Err(format!("Couldn't open {} (open exit {}).", bundle.display(), status.code().unwrap_or(-1))),
        Err(error) => Err(error.to_string()),
    }
}

/// What Settings and the menu show.
#[derive(Debug, Clone, Default)]
pub struct UpdateState {
    /// The newer release of this build's channel; None when none, or not checked yet.
    pub available: Option<Release>,
    /// What the last check or install said; None when there's nothing to say.
    pub notice: Option<String>,
    pub working: bool,
    pub automatic: bool,
}

pub struct UpdateModel {
    pub current: String,
    pub app: PathBuf,
    pub team: Option<String>,
    services: UpdateServices,
    state: Mutex<UpdateState>,
    on_change: OnChange,
}

impl UpdateModel {
    pub fn new(current: &str, app: PathBuf, team: Option<String>, services: UpdateServices) -> Arc<UpdateModel> {
        let automatic = services.defaults.value("checkForUpdates").and_then(|value| value.as_boolean()).unwrap_or(true);
        let hourly = services.hourly && team.is_some();
        let model = Arc::new(UpdateModel {
            current: current.to_string(),
            app,
            team,
            services,
            state: Mutex::new(UpdateState { automatic, ..UpdateState::default() }),
            on_change: OnChange::default(),
        });
        if hourly {
            let me = Arc::downgrade(&model);
            std::thread::spawn(move || Self::hourly(me));
        }
        model
    }

    fn hourly(me: Weak<UpdateModel>) {
        let Some(model) = me.upgrade() else { return };
        model.check_at_launch();
        drop(model);
        loop {
            std::thread::sleep(Duration::from_secs(3600));
            let Some(model) = me.upgrade() else { return };
            model.check_if_due();
        }
    }

    pub fn state(&self) -> UpdateState {
        self.state.lock().unwrap().clone()
    }

    pub fn set_on_change(&self, listener: impl Fn() + Send + Sync + 'static) {
        self.on_change.set(listener);
    }

    pub fn enabled(&self) -> bool {
        self.team.is_some()
    }

    pub fn set_automatic(&self, on: bool) {
        self.mutate(|state| state.automatic = on);
        self.services.defaults.set("checkForUpdates", plist::Value::Boolean(on));
    }

    /// Asks GitHub now. A version found for the first time posts one notification. A failed check is retried at the
    /// next hourly look, not a day later.
    pub fn check(&self) {
        if !self.enabled() {
            return;
        }
        match (self.services.get)(updater::RELEASES) {
            Ok(data) => {
                let defaults = &self.services.defaults;
                defaults.set("lastUpdateCheck", plist::Value::Date((self.services.now)().into()));
                let available = updater::pick(&data, &self.current);
                let current = &self.current;
                self.mutate(|state| {
                    state.notice = available.is_none().then(|| format!("MacNeutron {current} is up to date."));
                    state.available = available.clone();
                });
                if let Some(release) = available {
                    let notified = defaults.value("notifiedUpdate");
                    if notified.as_ref().and_then(plist::Value::as_string) != Some(&release.version) {
                        defaults.set("notifiedUpdate", plist::Value::String(release.version.clone()));
                        self.services.notifier.post("MacNeutron", &format!("MacNeutron {} is available.", release.version));
                    }
                }
            }
            Err(error) => self.mutate(|state| state.notice = Some(format!("Couldn't check for updates: {error}"))),
        }
    }

    /// At launch, `check` whenever automatic checks are on: what a check found isn't kept across launches, so an update
    /// found earlier the same day is offered again.
    pub fn check_at_launch(&self) {
        if self.state.lock().unwrap().automatic {
            self.check();
        }
    }

    /// `check`, when automatic checks are on and a day has passed since the last one.
    pub fn check_if_due(&self) {
        if !self.enabled() || !self.state.lock().unwrap().automatic {
            return;
        }
        let last = self.services.defaults.value("lastUpdateCheck").and_then(|value| value.as_date());
        if let Some(last) = last {
            let since = (self.services.now)().duration_since(last.into());
            if !matches!(since, Ok(elapsed) if elapsed >= Duration::from_secs(24 * 3600)) {
                return;
            }
        }
        self.check();
    }

    /// How `install` opens the downloaded installer: with --from-app, as a new process, never an Install MacNeutron
    /// already running (one the player opened, which would ask again and never get the argument).
    pub fn installer_configuration() -> InstallerConfiguration {
        InstallerConfiguration { arguments: vec!["--from-app".into()], allows_running_application_substitution: false }
    }

    /// Downloads, verifies and installs the available release over this app, then relaunches it; a copy it can't replace
    /// (opened from Downloads) opens the release's installer instead, which quits it and installs into Applications.
    pub fn install(&self) {
        let Some(team) = self.team.clone() else { return };
        let release = self.mutate(|state| {
            let release = state.available.clone().filter(|_| !state.working)?;
            state.working = true;
            state.notice = Some(format!("Downloading and checking MacNeutron {}…", release.version));
            Some(release)
        });
        let Some(release) = release else { return };
        let runner = SystemProcessRunner::new();
        let outcome = updater::update(&release, &self.app, &team, &app_installer::target(), &runner, &updater::default_fetch())
            .map_err(|error| error.to_string())
            .and_then(|outcome| match outcome {
                UpdateOutcome::Swapped(work) => updater::relaunch(&self.app, &work).map_err(|error| error.to_string()),
                UpdateOutcome::Installer(bundle) => (self.services.open)(&bundle, &Self::installer_configuration()),
            });
        match outcome {
            Ok(()) => (self.services.terminate)(),
            Err(error) => {
                updater::remove_installer_folders(&std::env::temp_dir()); // the installer downloaded, if it couldn't be opened
                self.mutate(|state| {
                    state.notice = Some(error);
                    state.working = false;
                });
            }
        }
    }

    fn mutate<R>(&self, change: impl FnOnce(&mut UpdateState) -> R) -> R {
        let result = change(&mut self.state.lock().unwrap());
        self.on_change.notify();
        result
    }
}

#[cfg(test)]
mod tests;
