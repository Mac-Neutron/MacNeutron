//! Turns periodic "is Steam running?" checks into launch and quit events.

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SteamEvent {
    Launched,
    Quit,
}

#[derive(Debug, Default, Clone)]
pub struct SteamWatcher {
    was_running: Option<bool>,
}

impl SteamWatcher {
    pub fn new() -> Self {
        SteamWatcher { was_running: None }
    }

    /// The first observation only records the state; later ones report changes.
    pub fn observe(&mut self, running: bool) -> Option<SteamEvent> {
        let previous = self.was_running;
        self.was_running = Some(running);
        match previous {
            Some(was) if was != running => {
                if running {
                    Some(SteamEvent::Launched)
                } else {
                    Some(SteamEvent::Quit)
                }
            }
            _ => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reports_only_transitions() {
        let mut watcher = SteamWatcher::new();
        assert_eq!(watcher.observe(true), None);
        assert_eq!(watcher.observe(true), None);
        assert_eq!(watcher.observe(false), Some(SteamEvent::Quit));
        assert_eq!(watcher.observe(false), None);
        assert_eq!(watcher.observe(true), Some(SteamEvent::Launched));
    }
}
