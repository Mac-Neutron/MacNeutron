// The microVM every game runs in (vm; docs/superpowers/specs/2026-10-15-macneutron-vm-route-design.md): one
// folder holding macneutron-vm, run-guest.sh, the guest's run-game.sh, the images every game shares (images/) and
// each game's own state disk (state/<appid>.img, run-guest.sh's APFS clone of images/state-template.img on first
// use), TPM (state/<appid>.tpm, sealed under a key in the login keychain) and share (runs/<appid>: the run's script,
// results and console), so games run side by side.
// MACNEUTRON_VM_DIR names the folder, else MacNeutron's own; MACNEUTRON_VM=0 runs a game on macOS as before. A game
// in a Steam library runs in the VM or not at all: the launcher refuses it until the VM is ready.

use std::collections::{BTreeMap, HashSet};
use std::path::{Path, PathBuf};

use crate::steam_location::SteamLocation;
use crate::verb::Verb;

pub struct MicroVM {
    pub root: PathBuf,
}

impl MicroVM {
    pub fn standard_root() -> PathBuf {
        home().join("Library/Application Support/MacNeutron/vm")
    }

    /// The game's verbs go to the VM (getcompatpath and getnativepath answer about the macOS prefix), unless
    /// MACNEUTRON_VM=0.
    pub fn wanted(environment: &BTreeMap<String, String>, verb: Verb) -> bool {
        (verb == Verb::Run || verb == Verb::WaitForExitAndRun)
            && environment.get("MACNEUTRON_VM").map(String::as_str) != Some("0")
    }

    /// The VM, once it's ready: macneutron-vm and a complete image set in the folder, no tools swap under way.
    pub fn find(root: &Path, environment: &BTreeMap<String, String>, verb: Verb) -> Option<MicroVM> {
        if !Self::wanted(environment, verb) {
            return None;
        }
        let folder = environment
            .get("MACNEUTRON_VM_DIR")
            .map(PathBuf::from)
            .unwrap_or_else(|| root.to_path_buf());
        if Self::is_ready(&folder) {
            Some(MicroVM { root: folder })
        } else {
            None
        }
    }

    /// A launch would run in the folder's VM: macneutron-vm and a complete image set there, no tools swap under way.
    pub fn is_ready(folder: &Path) -> bool {
        let vm = folder.join("macneutron-vm");
        let installing = folder.join(".installing"); // VMToolsInstaller's swap
        is_executable(&vm) && Self::images_complete(folder) && !installing.exists()
    }

    /// The setup (`macneutron vm-setup`) writes images/manifest.sha256 last, once the whole set is in place; the image
    /// build in vm writes `dev` there.
    pub fn images_complete(root: &Path) -> bool {
        root.join("images/manifest.sha256").exists()
    }

    pub fn run_guest(&self) -> PathBuf {
        self.root.join("run-guest.sh")
    }

    pub fn game_script(&self) -> PathBuf {
        self.root.join("run-game.sh")
    }

    pub fn state(&self, app_id: &str) -> PathBuf {
        self.root.join(format!("state/{app_id}.img"))
    }

    pub fn share(&self, app_id: &str) -> PathBuf {
        self.root.join(format!("runs/{app_id}"))
    }

    /// Native DXMT's own for the game, forwarded (MACNEUTRON_VM_D3D=forward): its shader cache and recorded pipelines.
    pub fn dxmt_state(&self, app_id: &str) -> PathBuf {
        self.root.join(format!("state/{app_id}.dxmt"))
    }

    /// The native replayer of recorded pipelines (vm/dxmt-native/build.sh), beside native DXMT.
    pub fn dxmt_replay(&self) -> PathBuf {
        self.root.join("dxmt/dxmt-replay")
    }

    /// Native DXMT's translator key (`dxmt-translator`, vm/dxmt-native/build.sh's), which keys its recordings.
    pub fn dxmt_translator(&self) -> Option<String> {
        std::fs::read_to_string(self.root.join("dxmt-translator"))
            .ok()
            .map(|s| s.trim().to_string())
    }

    /// A game under a Steam library's steamapps/common: that folder (the guest's read-only /mnt/games) and the game's
    /// path in the guest. Nil for a game anywhere else.
    pub fn guest_path(target: &str) -> Option<(String, String)> {
        let marker = "/steamapps/common/";
        let index = target.find(marker)?;
        let upper = index + marker.len();
        let library = target[..upper].trim_end_matches('/').to_string();
        Some((library, format!("/mnt/games/{}", &target[upper..])))
    }

    pub fn quoted(word: &str) -> String {
        let escaped = word.replace('\'', "'\\''");
        format!("'{escaped}'")
    }

    /// The launch options the guest reads (run-game.sh), as the launcher's own environment has them.
    pub const PASSED: [&'static str; 19] = [
        "MACNEUTRON_DLSS", "MACNEUTRON_XESS", "DXMT_ENABLE_NVEXT", "MACNEUTRON_LOG", "MN_SCREEN",
        "MACNEUTRON_VM_PROFILE", "DXMT_STATS", "MACNEUTRON_VM_D3D", "MACNEUTRON_VM_D3D32",
        "MACNEUTRON_VM_D3D_TRACE", "DXMT_LOG_LEVEL", "MACNEUTRON_VM_WOW64", "PROTON_USE_WOW64",
        "MTLD3D_CONFIG", "WINEDEBUG", "MN_MAC_HOME", "MN_INSTALL_TIMEOUT", "PROTON_EAC_RUNTIME",
        "MN_CRASH_REPORTERS",
    ];

    /// What a performance build's guest doesn't get: its stats and profiler are compiled out or off.
    pub const NOT_PASSED_TO_PERFORMANCE: [&'static str; 2] = ["MACNEUTRON_VM_PROFILE", "DXMT_STATS"];

    /// "WxH", each side 1 to 16384 (Xvfb's screen; MN_SCREEN from a launch option).
    pub fn is_screen_size(text: &str) -> bool {
        let sides: Vec<&str> = text.split('x').collect();
        sides.len() == 2
            && sides.iter().all(|side| {
                side.bytes().all(|b| b.is_ascii())
                    && side.parse::<i64>().map(|n| (1..=16384).contains(&n)).unwrap_or(false)
            })
    }

    /// The guest's script for one launch: its facts as shell assignments, the game's install scripts
    /// (InstallScripts.shell), then run-game.sh. A performance build's (X.Y.Zp) says MACNEUTRON_PROFILE=p, which
    /// keeps run-game.sh quiet unless MACNEUTRON_LOG is set.
    #[allow(clippy::too_many_arguments)]
    pub fn script(
        &self,
        verb: Verb,
        app_id: &str,
        guest: &str,
        arguments: &[String],
        environment: &BTreeMap<String, String>,
        performance: bool,
        install_script: &str,
    ) -> std::io::Result<String> {
        let folder = parent_of(guest);
        let mut lines = vec![
            format!("VERB={}", Self::quoted(verb.raw_value())),
            format!("APPID={}", Self::quoted(app_id)),
            format!("GAME_DIR={}", Self::quoted(&folder)),
            format!("EXE={}", Self::quoted(&format!("Z:{}", guest.replace('/', "\\")))),
            std::iter::once("set --".to_string())
                .chain(arguments.iter().map(|a| Self::quoted(a)))
                .collect::<Vec<_>>()
                .join(" "),
        ];
        if performance {
            lines.push(format!("MACNEUTRON_PROFILE={}", Self::quoted("p")));
        }
        let not_passed: HashSet<&str> = Self::NOT_PASSED_TO_PERFORMANCE.into_iter().collect();
        let mut set: Vec<String> = Vec::new();
        for key in Self::PASSED {
            if performance && not_passed.contains(key) {
                continue;
            }
            if let Some(value) = environment.get(key) {
                lines.push(format!("{key}={}", Self::quoted(value)));
                set.push(key.to_string());
            }
        }
        // The game's environment too, as a launch option is under Proton: guest DXMT and the forwarder read some
        // (DXMT_LOG_LEVEL, DXMT_ENABLE_NVEXT, MACNEUTRON_VM_D3D_TRACE), which run-game.sh doesn't export itself.
        if !set.is_empty() {
            lines.push(format!("export {}", set.join(" ")));
        }
        let game = std::fs::read_to_string(self.game_script())?;
        Ok(lines.join("\n") + "\n" + install_script + &game)
    }
}

/// A Mac folder a game in the VM gets read-write (spec 2026-10-23-macneutron-vm-shared-saves §3): macneutron-vm's
/// `--user-share <tag>=<host>`, which the guest's mn-runner mounts at `guest` (the run share's `mounts` file).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct UserShare {
    pub tag: String,
    pub host: String,
    pub guest: String,
    /// A tool's files the guest only reads (macneutron-vm's --ro-share), not the game's folders it writes.
    pub read_only: bool,
}

impl UserShare {
    pub fn new(tag: &str, host: &str, guest: &str) -> Self {
        UserShare { tag: tag.to_string(), host: host.to_string(), guest: guest.to_string(), read_only: false }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum UserShareError {
    /// ~/Documents can't be listed: macOS's privacy settings refused it to Steam, or it's missing.
    DocumentsUnreadable(String),
}

impl std::fmt::Display for UserShareError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            UserShareError::DocumentsUnreadable(path) => write!(
                f,
                "MacNeutron can't open your Documents folder ({path}), so a game in the VM couldn't reach its saves there. Allow Steam in System Settings > Privacy & Security > Files and Folders, then launch the game again."
            ),
        }
    }
}

impl std::error::Error for UserShareError {}

impl MicroVM {
    /// The guest prefix's profile, Proton's name for it (run-game.sh runs Wine as steamuser).
    pub const GUEST_PROFILE: &'static str = "/home/mn/pfx/drive_c/users/steamuser";

    /// The folders Steam Cloud syncs for the game, as the guest gets them: the Mac prefix's profile (users/steamuser,
    /// or the profile it links to: macOS Wine's, named after the Mac user, ProfileLinks), made when missing as
    /// Steam's and Proton's layout has it; ~/Documents, which the profile's Documents link names; and the game's own
    /// folder in its library (`guest` is the game's path in the guest, MicroVM.guestPath's).
    pub fn user_shares(prefix: &Path, library: &str, guest: &str, home: &str) -> Result<Vec<UserShare>, UserShareError> {
        let users = prefix.join("drive_c/users");
        let mut profile = users.join("steamuser");
        // One level, as Steam and macOS Wine name it (resolvingSymlinksInPath would also turn /var into /private/var).
        if let Ok(target) = std::fs::read_link(&profile) {
            let target = target.to_string_lossy().into_owned();
            profile = if target.starts_with('/') {
                PathBuf::from(&target)
            } else {
                users.join(&target)
            };
        }
        let _ = std::fs::create_dir_all(&profile); // a dangling link's folder too
        let documents = format!("{home}/Documents");
        if std::fs::read_dir(&documents).is_err() {
            return Err(UserShareError::DocumentsUnreadable(documents));
        }
        let profile_string = path_string(&profile);
        let mut shares = vec![
            UserShare::new("profile", &profile_string, Self::GUEST_PROFILE),
            UserShare::new("documents", &documents, &documents),
        ];
        if let Some(folder) = Self::game_folder(guest) {
            shares.push(UserShare::new(
                "game",
                &format!("{library}/{folder}"),
                &format!("/mnt/games/{folder}"),
            ));
        }
        Ok(shares)
    }

    /// The game's own folder in its library (its writable share), from its path in the guest; nil for a file at the
    /// library's top.
    pub fn game_folder(guest: &str) -> Option<String> {
        let in_games = guest.strip_prefix("/mnt/games/").unwrap_or("");
        if in_games.contains('/') {
            in_games.split('/').next().map(|s| s.to_string())
        } else {
            None
        }
    }

    /// Where the guest mounts the Proton EasyAntiCheat Runtime (mn-runner allows /Volumes/...).
    pub const GUEST_EAC_RUNTIME: &'static str = "/Volumes/Proton EasyAntiCheat Runtime";

    /// The Proton EasyAntiCheat Runtime (Steam app 1826330), which Linux Steam passes to every game under Proton: a
    /// Steam library's, or the copy Steam's console downloads (`download_depot 1826330 1826331`: Mac Steam doesn't
    /// install the Linux-only tool). A runtime is a folder with v2/.
    pub fn eac_runtime(steam: &SteamLocation) -> Option<String> {
        let depot = "steamapps/content/app_1826330/depot_1826331";
        let mut places: Vec<PathBuf> = steam
            .libraries()
            .into_iter()
            .map(|library| library.join("common/Proton EasyAntiCheat Runtime"))
            .collect();
        places.push(steam.root.join(depot));
        places.push(steam.bundle_macos().join(depot));
        places.into_iter().map(|p| path_string(&p)).find(|p| Path::new(p).join("v2").is_dir())
    }

    /// The run share's `mounts`: a `<tag> <guest path>` line per share, read by mn-runner.
    pub fn mounts_file(shares: &[UserShare]) -> String {
        shares.iter().map(|s| format!("{} {}\n", s.tag, s.guest)).collect()
    }
}

fn home() -> PathBuf {
    std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"))
}

fn is_executable(path: &Path) -> bool {
    use std::os::unix::fs::PermissionsExt;
    std::fs::metadata(path)
        .map(|m| m.is_file() && m.permissions().mode() & 0o111 != 0)
        .unwrap_or(false)
}

fn parent_of(path: &str) -> String {
    match path.rfind('/') {
        Some(0) => "/".to_string(),
        Some(index) => path[..index].to_string(),
        None => String::new(),
    }
}

fn path_string(path: &Path) -> String {
    path.to_string_lossy().into_owned()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(name: &str) -> PathBuf {
        crate::rpm_fixture::temp(name)
    }

    fn env(pairs: &[(&str, &str)]) -> BTreeMap<String, String> {
        pairs.iter().map(|(k, v)| (k.to_string(), v.to_string())).collect()
    }

    fn make_folder(name: &str) -> PathBuf {
        let root = temp(name).join("vm");
        std::fs::create_dir_all(&root).unwrap();
        let vm = root.join("macneutron-vm");
        std::fs::write(&vm, b"#!/bin/sh\n").unwrap();
        use std::os::unix::fs::PermissionsExt;
        let mut perms = std::fs::metadata(&vm).unwrap().permissions();
        perms.set_mode(0o755);
        std::fs::set_permissions(&vm, perms).unwrap();
        std::fs::write(root.join("run-guest.sh"), b"#!/bin/sh\n").unwrap();
        std::fs::write(root.join("run-game.sh"), b"# run-game.sh\nwine steam.exe \"$EXE\" \"$@\"\n").unwrap();
        std::fs::create_dir_all(root.join("images")).unwrap();
        std::fs::write(root.join("images/manifest.sha256"), b"dev\n").unwrap();
        root
    }

    #[test]
    fn only_run_verbs_want_the_vm() {
        assert!(MicroVM::wanted(&env(&[]), Verb::Run));
        assert!(MicroVM::wanted(&env(&[]), Verb::WaitForExitAndRun));
        assert!(!MicroVM::wanted(&env(&[]), Verb::GetCompatPath));
        assert!(!MicroVM::wanted(&env(&[("MACNEUTRON_VM", "0")]), Verb::Run));
    }

    #[test]
    fn find_needs_a_ready_folder() {
        let root = make_folder("ready");
        assert!(MicroVM::is_ready(&root));
        assert!(MicroVM::find(&root, &env(&[]), Verb::Run).is_some());
        assert!(MicroVM::find(&root, &env(&[]), Verb::GetNativePath).is_none());
        std::fs::write(root.join(".installing"), b"").unwrap();
        assert!(!MicroVM::is_ready(&root));
        std::fs::remove_file(root.join(".installing")).unwrap();
        std::fs::remove_file(root.join("images/manifest.sha256")).unwrap();
        assert!(!MicroVM::is_ready(&root));
    }

    #[test]
    fn vm_dir_overrides_the_root() {
        let root = make_folder("root");
        let other = make_folder("other");
        let found = MicroVM::find(&root, &env(&[("MACNEUTRON_VM_DIR", other.to_str().unwrap())]), Verb::Run).unwrap();
        assert_eq!(found.root, other);
    }

    #[test]
    fn guest_paths_come_from_steamapps_common() {
        let (library, guest) = MicroVM::guest_path(
            "/Users/someone/Library/Application Support/Steam/steamapps/common/SMITE 2/Windows/Hemingway.exe",
        )
        .unwrap();
        assert_eq!(library, "/Users/someone/Library/Application Support/Steam/steamapps/common");
        assert_eq!(guest, "/mnt/games/SMITE 2/Windows/Hemingway.exe");
        assert_eq!(MicroVM::guest_path("/tmp/elsewhere/game.exe"), None);
    }

    #[test]
    fn quotes_shell_words() {
        assert_eq!(MicroVM::quoted("ab"), "'ab'");
        assert_eq!(MicroVM::quoted("a'b"), "'a'\\''b'");
    }

    #[test]
    fn screen_sizes_are_checked() {
        assert!(MicroVM::is_screen_size("1920x1080"));
        assert!(MicroVM::is_screen_size("1x1"));
        assert!(MicroVM::is_screen_size("16384x16384"));
        assert!(!MicroVM::is_screen_size("0x1080"));
        assert!(!MicroVM::is_screen_size("1920x"));
        assert!(!MicroVM::is_screen_size("1920"));
        assert!(!MicroVM::is_screen_size("1920xabc"));
    }

    #[test]
    fn the_script_carries_the_environment() {
        let root = make_folder("script");
        let vm = MicroVM { root };
        let environment = env(&[("MACNEUTRON_LOG", "1"), ("OTHER", "x")]);
        let script = vm
            .script(
                Verb::WaitForExitAndRun,
                "2437170",
                "/mnt/games/SMITE 2/Windows/Hemingway.exe",
                &["-homedir=SMITE2Alpha".to_string()],
                &environment,
                false,
                "",
            )
            .unwrap();
        assert!(script.contains("VERB='waitforexitandrun'"));
        assert!(script.contains("APPID='2437170'"));
        assert!(script.contains("GAME_DIR='/mnt/games/SMITE 2/Windows'"));
        assert!(script.contains("EXE='Z:\\mnt\\games\\SMITE 2\\Windows\\Hemingway.exe'"));
        assert!(script.contains("set -- '-homedir=SMITE2Alpha'"));
        assert!(script.contains("MACNEUTRON_LOG='1'"));
        assert!(script.contains("export MACNEUTRON_LOG"));
        assert!(!script.contains("OTHER"));
        assert!(script.ends_with("wine steam.exe \"$EXE\" \"$@\"\n"));
    }

    #[test]
    fn a_performance_build_drops_stats_and_profile() {
        let vm = MicroVM { root: make_folder("perf") };
        let environment = env(&[("MACNEUTRON_VM_PROFILE", "1"), ("DXMT_STATS", "1"), ("MACNEUTRON_LOG", "1")]);
        let script = vm.script(Verb::Run, "1", "/mnt/games/Game/g.exe", &[], &environment, true, "").unwrap();
        assert!(script.contains("MACNEUTRON_PROFILE='p'"));
        assert!(!script.contains("MACNEUTRON_VM_PROFILE"));
        assert!(!script.contains("DXMT_STATS"));
        assert!(script.contains("MACNEUTRON_LOG='1'"));
    }

    #[test]
    fn user_shares_cover_profile_documents_and_the_game() {
        let home = temp("home");
        std::fs::create_dir_all(home.join("Documents")).unwrap();
        let prefix = temp("prefix");
        let shares = MicroVM::user_shares(
            &prefix,
            "/Users/someone/Library/Application Support/Steam/steamapps/common",
            "/mnt/games/SMITE 2/Windows/Hemingway.exe",
            home.to_str().unwrap(),
        )
        .unwrap();
        assert_eq!(shares[0].tag, "profile");
        assert_eq!(shares[0].guest, MicroVM::GUEST_PROFILE);
        assert_eq!(shares[1].tag, "documents");
        assert_eq!(shares[2].tag, "game");
        assert_eq!(shares[2].guest, "/mnt/games/SMITE 2");
        assert!(prefix.join("drive_c/users/steamuser").exists());
        let mounts = MicroVM::mounts_file(&shares);
        assert!(mounts.starts_with("profile /home/mn/pfx/drive_c/users/steamuser\n"));
        assert!(mounts.contains(&format!("documents {}/Documents\n", home.to_str().unwrap())));
        assert!(mounts.ends_with("game /mnt/games/SMITE 2\n"));
    }

    #[test]
    fn a_dangling_profile_link_gives_its_folder() {
        let home = temp("home2");
        std::fs::create_dir_all(home.join("Documents")).unwrap();
        let prefix = temp("prefix2");
        let users = prefix.join("drive_c/users");
        std::fs::create_dir_all(&users).unwrap();
        std::os::unix::fs::symlink("someoneelse", users.join("steamuser")).unwrap();
        let shares = MicroVM::user_shares(&prefix, "/lib/common", "/mnt/games/Game/g.exe", home.to_str().unwrap()).unwrap();
        assert_eq!(shares[0].host, users.join("someoneelse").to_string_lossy());
    }

    #[test]
    fn a_missing_documents_folder_is_reported() {
        let prefix = temp("prefix3");
        let err = MicroVM::user_shares(&prefix, "/lib/common", "/mnt/games/Game/g.exe", "/nonexistent-home-xyz");
        assert!(matches!(err, Err(UserShareError::DocumentsUnreadable(_))));
    }

    #[test]
    fn game_folder_is_the_top_of_the_library() {
        assert_eq!(MicroVM::game_folder("/mnt/games/SMITE 2/Windows/Hemingway.exe"), Some("SMITE 2".to_string()));
        assert_eq!(MicroVM::game_folder("/mnt/games/Game"), None);
        assert_eq!(MicroVM::game_folder("/tmp/x"), None);
    }
}

