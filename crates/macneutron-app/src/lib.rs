//! MacNeutronApp's toolkit-independent half, in Rust: what the menu, setup, games and settings views show and every
//! action they take (`AppModel.swift`, `UpdateModel.swift`). No GUI crate: the AppKit layer that draws the views sits on
//! top of this.
//!
//! **Change notification.** Swift's `@Observable` becomes plain state behind a mutex, read whole through each model's
//! `state()` (a clone), plus one listener per model set with `set_on_change`. The model calls it after every change, on
//! whichever thread made it and with no lock held; the UI hops to its main thread and redraws from a fresh `state()`.
//!
//! **Concurrency.** Swift's `async` methods are blocking methods here, safe to call from any thread (the UI calls them
//! off its main thread). What Swift ran as `Task`s (the startup install, the Valve check, the VM setup a tools install
//! starts, the Steam watcher, the hourly update check) are threads; `wait_*` joins the ones Swift's tests await. The
//! one-at-a-time flags are tested and set under the state lock, and `generation` still drops a refresh read before a
//! change.

pub mod app_model;
pub mod update_model;

use std::sync::{Arc, Mutex};

/// The one change listener a model calls (see the crate docs).
#[derive(Default)]
pub struct OnChange(Mutex<Option<Arc<dyn Fn() + Send + Sync>>>);

impl OnChange {
    pub fn set(&self, listener: impl Fn() + Send + Sync + 'static) {
        *self.0.lock().unwrap() = Some(Arc::new(listener));
    }

    /// Calls the listener outside the lock, so it may set another or read the model.
    pub(crate) fn notify(&self) {
        let listener = self.0.lock().unwrap().clone();
        if let Some(listener) = listener {
            listener();
        }
    }
}
