//! Implements the `proton` verbs Steam calls on a compatibility tool. Ported from `Launcher.swift`.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use crate::compat_context::CompatContext;
use crate::file_lock::with_file_lock;
use crate::game_settings::{GameSettings, GameSettingsStore};
use crate::graphics_backend::GraphicsBackend;
use crate::install_scripts::InstallScripts;
use crate::launch_environment::LaunchEnvironment;
use crate::launcher_log::{iso8601_now, LauncherLog};
use crate::micro_vm::{MicroVM, UserShare};
use crate::pe_image;
use crate::preflight::{AppleScriptNotifier, Notifier, Preflight};
use crate::prefix_manager::{Prepared, PrefixManager};
use crate::process_runner::{ProcessRunner, SystemProcessRunner};
use crate::profile_links::{ProfileLinkReport, ProfileLinks};
use crate::shader_precache::ShaderPrecache;
use crate::steam_bridge::SteamBridge;
use crate::steam_location::SteamLocation;
use crate::tool_layout::ToolLayout;
use crate::verb::{LaunchRequest, Verb};
use crate::vm_crash::VMCrash;

type Error = Box<dyn std::error::Error>;

/// 128 + SIGTERM: what Steam's Stop leaves a launch that started nothing more.
const STOPPED: i32 = 128 + libc::SIGTERM;

/// How often a shader replay's progress is read (ShaderPrecache.swift's default).
const REPLAY_POLL: Duration = Duration::from_secs(1);

/// The microVM launch in progress, which Steam's Stop ends (Launcher::stop_vm): its run-guest.sh's process group once
/// it runs (lockf, the guest's script, macneutron-vm; its helpers end with it).
#[derive(Default)]
pub(crate) struct VMRun {
    state: Mutex<VMRunState>,
}

#[derive(Default)]
struct VMRunState {
    active: bool,
    stopped: bool,
    group: i32,
}

impl VMRun {
    #[cfg(test)]
    pub(crate) fn running(&self) -> bool {
        self.state.lock().unwrap().group > 0
    }

    fn begin(&self) {
        self.state.lock().unwrap().active = true;
    }

    fn end(&self) {
        let mut state = self.state.lock().unwrap();
        state.active = false;
        state.group = 0;
    }

    /// run-guest.sh runs: a stop that came first ends it now.
    fn started(&self, pid: i32) {
        let stopped = {
            let mut state = self.state.lock().unwrap();
            state.group = pid;
            state.stopped
        };
        // ponytail: pid > 1 never signals this process's own group (0) or every process (-1).
        if stopped && pid > 1 {
            unsafe { libc::kill(-pid, libc::SIGTERM) };
        }
    }

    /// Ends the VM now, or as it starts. False when no microVM launch is in progress.
    fn stop(&self) -> bool {
        let group = {
            let mut state = self.state.lock().unwrap();
            if !state.active {
                return false;
            }
            state.stopped = true;
            state.group
        };
        if group > 1 {
            unsafe { libc::kill(-group, libc::SIGTERM) };
        }
        true
    }
}

/// Runs its closure when dropped: Swift's `defer`.
struct Defer<F: FnMut()>(F);

impl<F: FnMut()> Drop for Defer<F> {
    fn drop(&mut self) {
        (self.0)()
    }
}

/// Implements the `proton` verbs Steam calls on a compatibility tool.
pub struct Launcher {
    pub layout: ToolLayout,
    pub runner: Arc<dyn ProcessRunner>,
    pub log: Arc<LauncherLog>,
    pub notifier: Arc<dyn Notifier>,
    pub preflight: Preflight,
    pub settings: GameSettingsStore,
    pub steam: SteamLocation,
    /// Where the microVM is installed (micro_vm.rs): every game runs in it when it is.
    pub micro_vm_root: PathBuf,
    /// The Mac's display in pixels (MicroVM::main_display): the guest's screen, unless a launch option sets MN_SCREEN.
    pub display: Box<dyn Fn() -> Option<(usize, usize)> + Send + Sync>,
    /// The Mac user's home: its Documents go to a VM game (MicroVM::user_shares).
    pub home: String,
    /// Set by `terminate` (Steam's Stop): the launch in progress starts nothing more, as a shader replay can run for
    /// minutes before the game.
    pub(crate) stop_requested: AtomicBool,
    pub(crate) vm_run: VMRun,
}

impl Launcher {
    /// The real machine's: the system runner, `~/Library/Logs/MacNeutron`, osascript notifications without toasts,
    /// MacNeutron's VM folder and the main display.
    pub fn new(layout: ToolLayout) -> Self {
        Launcher {
            layout,
            runner: Arc::new(SystemProcessRunner),
            log: Arc::new(LauncherLog::standard()),
            notifier: Arc::new(AppleScriptNotifier::new(None)),
            preflight: Preflight::default(),
            settings: GameSettingsStore::default(),
            steam: SteamLocation::default(),
            micro_vm_root: MicroVM::standard_root(),
            display: Box::new(MicroVM::main_display),
            home: std::env::var("HOME").unwrap_or_else(|_| "/".to_string()),
            stop_requested: AtomicBool::new(false),
            vm_run: VMRun::default(),
        }
    }

    fn stopped(&self) -> bool {
        self.stop_requested.load(Ordering::SeqCst)
    }

    /// Returns the exit code for Steam. Never fails: every failure is logged and becomes exit 1.
    pub fn launch(&self, argv: &[String], steam_environment: &BTreeMap<String, String>) -> i32 {
        let mut request = match LaunchRequest::parse(argv) {
            Ok(request) => request,
            Err(error) => return self.fail(&error.to_string(), argv, false, None),
        };
        let context = match CompatContext::new(steam_environment) {
            Ok(context) => context,
            Err(error) => return self.fail(&error.to_string(), argv, false, None),
        };
        let game_settings = self.settings.load(&context.app_id).unwrap_or_else(|error| {
            self.log.append(&format!("note: ignoring unreadable game settings for {}: {error}", context.app_id));
            // The defaults still apply (16x anisotropic filtering is a default, not an empty setting).
            GameSettings::default()
        });
        let mut environment = game_settings.environment();
        environment.extend(steam_environment.clone());
        // The launch options field's arguments follow the game's own when Steam starts the game, not when its install
        // scripts start a redistributable's installer (`run`).
        if request.verb == Verb::WaitForExitAndRun {
            request.arguments.extend(game_settings.launch_arguments());
        }
        let logging = matches!(environment.get("MACNEUTRON_LOG").map(String::as_str), Some("1" | "2"));
        // A performance build (X.Y.Zp) keeps one launcher.log line a launch unless MACNEUTRON_LOG asks for more.
        self.log.set_quiet(self.layout.is_performance_build() && !logging);
        let game_log = logging.then(|| self.log.game_log(&context.app_id));
        let game_log = game_log.as_deref();
        let vm_wanted = MicroVM::wanted(&environment, request.verb);
        // A game in a Steam library: in the VM.
        let vm_target = if vm_wanted { MicroVM::guest_path(&request.target) } else { None };
        let identity = match self.preflight.check(&self.layout, &request, vm_target.is_some()) {
            Ok(identity) => identity,
            Err(error) => return self.fail(&error.to_string(), argv, true, game_log),
        };
        // Steam's install scripts start redistributable installers with `run`; a 32-bit one can't run on macOS. In the
        // VM it runs, as Proton runs them.
        if vm_target.is_none()
            && request.verb == Verb::Run
            && pe_image::machine(Path::new(&request.target)) == Some(pe_image::I386)
        {
            let name = Path::new(&request.target).file_name().unwrap_or_default().to_string_lossy();
            self.log.append(&format!("skipped 32-bit installer {name}"));
            return 0;
        }

        if let Some((library, guest)) = vm_target {
            // In the VM or not at all: a launch before the app's setup finishes is refused, never run on macOS.
            let Some(vm) = MicroVM::find(&self.micro_vm_root, &environment, request.verb) else {
                return self.fail(
                    "MacNeutron's game VM isn't ready yet: open MacNeutron, and launch the game again once its Settings \
                     say \"Game VM: ready\".",
                    argv,
                    true,
                    game_log,
                );
            };
            return self.run_in_micro_vm(&vm, &request, &context, &library, &guest, environment, game_log, argv);
        }
        if vm_wanted {
            self.log.append(&format!("note: {} is not in a Steam library: it runs on macOS", request.target));
        }

        let (backend, note) = GraphicsBackend::select(environment.get("MACNEUTRON_GRAPHICS").map(String::as_str));
        let mut env = LaunchEnvironment::build(&environment, &context, backend, logging);
        // mtld3d (Direct3D 9) writes its logs to mtld3d-logs/ beside the game's exe unless told otherwise: ours go to
        // the log folder. The user's MTLD3D_CONFIG comes after it: mtld3d applies the entries in order, so theirs win.
        let mut mtld3d = format!("log.dir=Z:{}", self.log.directory.join("mtld3d").to_string_lossy());
        if let Some(theirs) = env.get("MTLD3D_CONFIG") {
            mtld3d = format!("{mtld3d};{theirs}");
        }
        env.insert("MTLD3D_CONFIG".into(), mtld3d);
        // MACNEUTRON_PERF=1: DXMT's frame statistics, for vm/tests/perf-compare.sh. The user's own settings win.
        // DXMT keeps stats.txt in its DXIL dump folder, which also turns on its capture mode: every new shader, root
        // signature and pipeline is saved there too (a little CPU, and files that build up in the folder): the
        // folder is emptied at each launch, unless the user named another. When their settings or the backend keep
        // stats.txt out of the folder, the log says so (the record's fps would come out empty).
        // A performance build has no DXMT stats or capture to turn on (compiled out).
        if environment.get("MACNEUTRON_PERF").map(String::as_str) == Some("1") && !self.layout.is_performance_build() {
            let mut why = None;
            if backend != GraphicsBackend::Dxmt {
                why = Some("needs the DXMT backend");
            } else if env.contains_key("DXMT_STATS") {
                why = Some("DXMT_STATS is set");
            } else {
                let folder = self.perf_folder(&context.app_id);
                if env.contains_key("DXMT_DXIL_DUMP") {
                    why = Some("DXMT_DXIL_DUMP is set");
                } else {
                    let _ = crate::posix::remove_item(&folder);
                }
                let _ = std::fs::create_dir_all(&folder);
                env.insert("DXMT_STATS".into(), "1".into());
                env.entry("DXMT_DXIL_DUMP".into()).or_insert_with(|| directory_path(&folder));
            }
            if let Some(why) = why {
                self.log.append(&format!("note: MACNEUTRON_PERF=1: no frame statistics in the perf folder: {why}"));
            }
        }
        let steam_bridge = self.uses_steam_bridge(request.verb, &env);
        if steam_bridge {
            self.add_steam_client(&mut env);
        }
        // The MetalFX presenter inside wine.app reads this; only the game's verbs ask for it. With MetalFX off it still
        // runs for post-AA, and reads MACNEUTRON_NO_METALFX itself to skip only its upscaling.
        let value = |key: &str| env.get(key).map(String::as_str);
        if (request.verb == Verb::Run || request.verb == Verb::WaitForExitAndRun)
            && (value("MACNEUTRON_NO_METALFX") != Some("1") || value("MACNEUTRON_POST_AA") == Some("cmaa2"))
            && value("MACNEUTRON_PRESENT").is_none()
        {
            env.insert("MACNEUTRON_PRESENT".into(), "1".into());
        }
        if let Some(game_log) = game_log {
            self.write_header(game_log, &request, &env);
        }
        let mut prefix = PrefixManager::new(
            context.clone(),
            self.layout.clone(),
            identity.clone(),
            self.runner.clone(),
            self.log.clone(),
        );
        let (notifier, (title, message)) = (self.notifier.clone(), Self::setup_toast(&request.target));
        prefix.on_prepare = Box::new(move || {
            let toast = notifier.progress(&title, &message, None);
            Box::new(move || (toast.close)()) as Prepared
        });

        match self.run_on_macos(&request, &context, &prefix, &env, backend, game_log, steam_bridge) {
            Ok(status) => {
                let label = ToolLayout::runtime_label(self.layout.runtime_version().as_deref(), Some(&identity));
                let mut line = format!(
                    "verb={} appid={} backend={} runtime={label} exit={status}",
                    request.verb.raw_value(),
                    context.app_id,
                    backend.raw_value()
                );
                if let Some(note) = note {
                    line += &format!(" note={note}");
                }
                self.log.summary(&line);
                status
            }
            Err(error) => self.fail(&error.to_string(), argv, true, game_log),
        }
    }

    #[allow(clippy::too_many_arguments)] // the Swift launch's locals, as one `do` block had them
    fn run_on_macos(
        &self,
        request: &LaunchRequest,
        context: &CompatContext,
        prefix: &PrefixManager,
        env: &BTreeMap<String, String>,
        backend: GraphicsBackend,
        game_log: Option<&Path>,
        steam_bridge: bool,
    ) -> Result<i32, Error> {
        Ok(match request.verb {
            Verb::RunInPrefix => self.run_game(request, env, game_log, false)?,
            Verb::Run => {
                self.notify_about_cloud_conflicts(&prefix.prepare(env, steam_bridge)?);
                if !steam_bridge {
                    prefix.remove_steam_bridge()?;
                }
                self.run_game(request, env, game_log, steam_bridge)?
            }
            Verb::WaitForExitAndRun => {
                // Prepare first (Proton's order): a launch queued on the prefix lock behind `run iscriptevaluator.exe`
                // then finds that session's wineserver alive, and `-w` waits for the redistributable installers to
                // finish.
                self.notify_about_cloud_conflicts(&prefix.prepare(env, steam_bridge)?);
                if !steam_bridge {
                    prefix.remove_steam_bridge()?;
                }
                self.runner.run(&self.layout.wineserver(), &["-w".to_string()], env, None)?;
                // Shader pre-caching: after a DXMT or macOS update, rebuild the recorded pipelines before the game.
                let precache = ShaderPrecache::enabled(backend, env)
                    .then(|| ShaderPrecache::new(context, &self.layout, &ShaderPrecache::macos_build()));
                if let Some(precache) = precache.as_ref().filter(|p| p.needs_replay()) {
                    let toast = self.shaders_toast(&request.target);
                    let lines = precache.replay(
                        &self.layout,
                        self.runner.as_ref(),
                        env,
                        &|| self.stopped(),
                        &|line| (toast.update)(line),
                        REPLAY_POLL,
                    );
                    (toast.close)();
                    for line in lines {
                        self.log.append(&line);
                    }
                    if !self.stopped() {
                        precache.write_stamp();
                    }
                }
                if self.stopped() {
                    // Steam's Stop during the replay: start nothing; with the stamp unchanged, the next launch replays.
                    STOPPED
                } else {
                    let status = self.run_game(request, env, game_log, steam_bridge)?;
                    // Keep Steam's "running" state until every process in the prefix is gone (covers launchers that
                    // start the real game and exit).
                    self.runner.run(&self.layout.wineserver(), &["-w".to_string()], env, None)?;
                    if let Some(precache) = &precache {
                        precache.write_stamp_if_missing();
                    }
                    status
                }
            }
            Verb::GetCompatPath | Verb::GetNativePath => {
                prefix.prepare(env, false)?;
                let flag = if request.verb == Verb::GetCompatPath { "-w" } else { "-u" };
                let arguments = ["winepath.exe".to_string(), flag.to_string(), request.target.clone()];
                self.runner.run(&self.layout.wine(), &arguments, env, None)?
            }
        })
    }

    fn shaders_toast(&self, target: &str) -> crate::preflight::ToastHandle {
        self.notifier.progress(
            &format!("Preparing {}'s shaders", Self::game_name(target)),
            "DXMT or macOS changed: they're rebuilt once, before it starts.",
            None,
        )
    }

    /// The game in the microVM (micro_vm.rs): run-guest.sh boots it with the game's state disk and Steam library,
    /// Steam forwarded to the Mac's for the game's app ID, and returns the game's status once every process in its
    /// prefix is gone (run-game.sh). A VM per game, side by side: the same game twice is refused (75).
    /// ponytail: a fixed 16 GiB of RAM, until milestone 5's sizing.
    #[allow(clippy::too_many_arguments)] // Swift's runInMicroVM
    fn run_in_micro_vm(
        &self,
        vm: &MicroVM,
        request: &LaunchRequest,
        context: &CompatContext,
        library: &str,
        guest: &str,
        mut environment: BTreeMap<String, String>,
        game_log: Option<&Path>,
        argv: &[String],
    ) -> i32 {
        // Direct3D forwarding by default (M5 passed, 2026-10-19): native DXMT on the Mac draws; =guest keeps DXMT in the
        // guest.
        environment.entry("MACNEUTRON_VM_D3D".into()).or_insert_with(|| "forward".into());
        let debug = LaunchEnvironment::wine_debug(&environment, game_log.is_some());
        environment.insert("WINEDEBUG".into(), debug);
        // The guest's screen is the Mac's display, so a game is offered its resolutions up to the display's own.
        if !environment.get("MN_SCREEN").is_some_and(|s| MicroVM::is_screen_size(s)) {
            match (self.display)() {
                Some((width, height)) => environment.insert("MN_SCREEN".into(), format!("{width}x{height}")),
                None => environment.remove("MN_SCREEN"),
            };
        }
        // MACNEUTRON_PERF=1: the guest profiler (run-game.sh); frames are counted on the host (MN_PRESENT_STATS below),
        // where DXMT_STATS's timing would cost the guest a clock read under FEX for every D3D12 call.
        let perf = environment.get("MACNEUTRON_PERF").map(String::as_str) == Some("1");
        if perf && !self.layout.is_performance_build() {
            environment.insert("MACNEUTRON_VM_PROFILE".into(), "1".into());
        }
        // A performance build without MACNEUTRON_LOG: no guest console, and DXMT (native and the guest's) logs nothing.
        let quiet = self.layout.is_performance_build() && game_log.is_none();
        if quiet {
            environment.entry("DXMT_LOG_LEVEL".into()).or_insert_with(|| "none".into());
        }
        self.vm_run.begin();
        let _end = Defer(|| self.vm_run.end());
        match self.boot(vm, request, context, library, guest, environment, game_log, perf, quiet) {
            Ok(status) => status,
            Err(error) => self.fail(&error.to_string(), argv, true, game_log),
        }
    }

    #[allow(clippy::too_many_arguments)] // runInMicroVM's `do` block
    fn boot(
        &self,
        vm: &MicroVM,
        request: &LaunchRequest,
        context: &CompatContext,
        library: &str,
        guest: &str,
        mut environment: BTreeMap<String, String>,
        game_log: Option<&Path>,
        perf: bool,
        quiet: bool,
    ) -> Result<i32, Error> {
        let app_id = context.app_id.as_str();
        // The folders Steam Cloud syncs, read-write in the guest (MicroVM::user_shares): mn-runner mounts the run
        // share's `mounts` before the game, or leaves traces/mounts.failed and powers off without it; run-game.sh
        // leaves it too when the profile isn't mounted (images older than these tools).
        let mut shares = MicroVM::user_shares(&context.prefix(), library, guest, &self.home)?;
        // The Proton EasyAntiCheat Runtime, as Linux Steam passes it to every game under Proton: read-only in the
        // game's own VM, wherever Steam put it (an EAC game's processes load its libraries).
        if let Some(runtime) = MicroVM::eac_runtime(&self.steam) {
            shares.push(UserShare {
                read_only: true,
                ..UserShare::new("eac-runtime", &runtime, MicroVM::GUEST_EAC_RUNTIME)
            });
            environment.insert("PROTON_EAC_RUNTIME".into(), MicroVM::GUEST_EAC_RUNTIME.into());
        } else {
            environment.remove("PROTON_EAC_RUNTIME");
        }
        // Steam's Auto-Cloud roots are the profile's XP names (ProfileLinks): a game no macOS launch prepared has
        // none, so they're made here, under the prefix's lock as PrefixManager makes them, merging what Steam's
        // launch download put in real XP-name folders.
        let profile_name = Path::new(&shares[0].host).file_name().unwrap_or_default().to_string_lossy().into_owned();
        let links = with_file_lock(&context.lock_file(), || ProfileLinks::apply(&context.prefix(), &profile_name, true))?;
        for note in &links.notes {
            self.log.append(&format!("note: {note}"));
        }
        environment.insert("MN_MAC_HOME".into(), self.home.clone()); // run-game.sh's XDG folders: the Mac's
        let share = vm.share(app_id);
        std::fs::create_dir_all(&share)?;
        // Where the game ran from: the app deletes the drive of a game Steam uninstalled only while Steam still lists
        // this library (UninstalledDrives), not when Steam's list was reset.
        let steamapps = Path::new(library).parent().unwrap_or(Path::new("/"));
        std::fs::write(share.join("library"), steamapps.to_string_lossy().as_bytes())?;
        std::fs::write(share.join("mounts"), MicroVM::mounts_file(&shares))?;
        let mounts_failed = share.join("traces/mounts.failed");
        let _ = std::fs::remove_file(&mounts_failed); // an earlier run's
        // A crash reporter's marker and report from an earlier run: this launch's own, if any, are the next.
        let _ = crate::posix::remove_item(&share.join(VMCrash::MARKER));
        let _ = crate::posix::remove_item(&share.join(VMCrash::DETAILS));
        // Steam's install scripts for the game (spec 2026-10-24-macneutron-install-scripts-design): Mac Steam never
        // runs them, so run-game.sh does, in the game's prefix before the game, and says how each step went.
        let install_scripts = if request.verb == Verb::WaitForExitAndRun {
            InstallScripts::plan(steamapps, app_id, MicroVM::game_folder(guest).as_deref())
        } else {
            InstallScripts::default()
        };
        for note in &install_scripts.notes {
            self.log.append(&format!("install script {app_id}: {note}"));
        }
        // Their Registry entries, which run-game.sh imports when they changed: none from an earlier run.
        let registry = share.join("installscript.reg");
        if install_scripts.registry.is_empty() {
            let _ = std::fs::remove_file(&registry);
        } else {
            std::fs::write(&registry, install_scripts.registry_file())?;
        }
        let install_results = share.join("traces/installscript.log");
        let _ = std::fs::remove_file(&install_results); // an earlier run's
        let script = vm.script(
            request.verb,
            app_id,
            guest,
            &request.arguments,
            &environment,
            self.layout.is_performance_build(),
            &install_scripts.shell(),
        )?;
        let file = std::env::temp_dir().join(format!("macneutron-vm-{app_id}-{}.sh", unique()));
        std::fs::write(&file, script)?;
        let _remove = Defer(|| {
            let _ = std::fs::remove_file(&file);
        });
        // The guest boots, prepares its prefix (a first run makes it) and loads the game behind no window: a toast
        // until the game's window shows on the Mac (macneutron-vm's MN_WINDOW_SHOWN) or the run ends, run-game.sh's
        // stages its message (the last run's word on both removed first).
        let traces = share.join("traces");
        let (window_shown, stage) = (traces.join("window"), traces.join("stage"));
        for marker in [&window_shown, &stage] {
            let _ = std::fs::remove_file(marker);
        }
        let start = if !vm.state(app_id).exists() {
            Self::setup_toast(&request.target)
        } else {
            (format!("Starting {}", Self::game_name(&request.target)), "Starting its game VM.".to_string())
        };
        let toast = Arc::new(self.notifier.progress(&start.0, &start.1, Some(window_shown.clone())));
        let launched = Arc::new(AtomicBool::new(false));
        {
            // The share is the guest's: polled.
            let (toast, launched, stage) = (toast.clone(), launched.clone(), stage.clone());
            std::thread::spawn(move || {
                let mut last = String::new();
                while !launched.load(Ordering::SeqCst) {
                    let line = std::fs::read_to_string(&stage).unwrap_or_default().trim().to_string();
                    if !line.is_empty() && line != last {
                        (toast.update)(&line);
                        last = line;
                    }
                    std::thread::sleep(Duration::from_millis(500));
                }
            });
        }
        let _close = Defer(|| {
            launched.store(true, Ordering::SeqCst);
            (toast.close)();
        });
        let mut env = environment.clone();
        env.insert("MN_WINDOW_SHOWN".into(), window_shown.to_string_lossy().into_owned());
        // The game's name on that window and its .exe's icon on the Dock tile (vm/metal/wm/host/winemac.m), not a bare
        // executable's.
        env.insert("MN_APP_NAME".into(), Self::game_name(&request.target));
        let icon = share.join("icon.ico");
        if let Some(data) = pe_image::icon(Path::new(&request.target)) {
            if std::fs::write(&icon, data).is_ok() {
                env.insert("MN_APP_ICON".into(), icon.to_string_lossy().into_owned());
            }
        }
        env.insert("MN_VM_DIR".into(), directory_path(&vm.root));
        env.insert("MN_STATE".into(), vm.state(app_id).to_string_lossy().into_owned());
        env.insert("MN_SHARE".into(), share.to_string_lossy().into_owned());
        // vm/host/tpm.c's, while macOS asks
        env.insert("MN_TOAST".into(), self.layout.launcher_binary().to_string_lossy().into_owned());
        // DLSS's NVIDIA extension in native DXMT too (Direct3D forwarding: it reads macneutron-vm's environment)
        if environment.get("MACNEUTRON_DLSS").map(String::as_str) == Some("1") {
            env.entry("DXMT_ENABLE_NVEXT".into()).or_insert_with(|| "1".into());
        }
        // Forwarded, native DXMT's shader cache and recordings are the game's, in its VM state folder (macneutron-vm
        // points DXMT at them); after a translator or macOS update its recordings replay natively before the boot.
        let mut precache = None;
        if environment.get("MACNEUTRON_VM_D3D").map(String::as_str) == Some("forward") {
            let state = vm.dxmt_state(app_id);
            env.insert("MN_D3D_STATE".into(), state.to_string_lossy().into_owned());
            if ShaderPrecache::enabled(GraphicsBackend::Dxmt, &environment) {
                let key = vm.dxmt_translator().or_else(|| self.layout.dxmt_translator());
                let builds = format!("{} {}", key.as_deref().unwrap_or("none"), ShaderPrecache::macos_build());
                precache = Some(ShaderPrecache::with_folder(state.join("pipelines"), builds));
            }
            if let Some(precache) = precache.as_ref().filter(|p| p.needs_replay()) {
                let toast = self.shaders_toast(&request.target);
                let lines = precache.replay_native(
                    &vm.dxmt_replay(),
                    &state,
                    self.runner.as_ref(),
                    &env,
                    &|| self.stopped(),
                    &|line| (toast.update)(line),
                    REPLAY_POLL,
                );
                (toast.close)();
                for line in lines {
                    self.log.append(&line);
                }
                if self.stopped() {
                    return Ok(STOPPED); // Steam's Stop: no boot; the next launch replays
                }
                precache.write_stamp();
            }
        }
        let _stamp = Defer(|| {
            if let Some(precache) = &precache {
                precache.write_stamp_if_missing();
            }
        });
        if perf {
            env.insert("MN_PRESENT_STATS".into(), share.join("traces/frames.txt").to_string_lossy().into_owned());
        }
        env.entry("MN_RUN_TIMEOUT".into()).or_insert_with(|| "86400".into());
        let mut arguments: Vec<String> = vec![
            file.to_string_lossy().into_owned(),
            "--steam-appid".into(),
            app_id.into(),
            "--ram-mib".into(),
            "16384".into(),
            "--steam-map".into(),
            format!("{library}=/mnt/games"),
        ];
        for share in &shares {
            arguments.push(if share.read_only { "--ro-share" } else { "--user-share" }.into());
            arguments.push(format!("{}={}", share.tag, share.host));
        }
        if quiet {
            arguments.extend(["--console".to_string(), "/dev/null".to_string()]);
        }
        let mut status =
            self.runner.run_with_started(&vm.run_guest(), &arguments, &env, game_log, &mut |pid| self.vm_run.started(pid))?;
        // run-game.sh ended the VM because a game's crash reporter was waiting on the invisible Xvfb (Unreal's, and its
        // kin): keep the report where the player can find it and say so. A crash is never a success.
        if let Some(crash) = VMCrash::read(&share, &context.prefix(), library) {
            if status == 0 {
                status = 1;
            }
            self.report(&crash, app_id, &request.target, game_log);
        }
        self.log.summary(&format!(
            "verb={} appid={app_id} backend=vm vm={} exit={status}",
            request.verb.raw_value(),
            directory_path(&vm.root)
        ));
        // A step that failed is retried at the next launch, as Steam retries it: a performance build keeps its line.
        let results = std::fs::read_to_string(&install_results).unwrap_or_default();
        for line in results.split('\n').filter(|l| !l.is_empty()) {
            let entry = format!("install script {app_id}: {line}");
            if line.ends_with(" exit 0") {
                self.log.append(&entry);
            } else {
                self.log.summary(&entry);
            }
            // Timed out (run-game.sh's MN_INSTALL_TIMEOUT): likely waiting for a click, and it will again.
            if let Some(step) = line.strip_suffix(" exit 124") {
                self.notifier.post(
                    "MacNeutron",
                    &format!(
                        "{}'s install step \"{step}\" was stopped after waiting too long, maybe for a click in a window \
                         the game VM can't show. It runs again at the next launch.",
                        Self::game_name(&request.target)
                    ),
                );
            }
        }
        if let Ok(reason) = std::fs::read_to_string(&mounts_failed) {
            let first = reason.split('\n').find(|l| !l.is_empty()).unwrap_or("");
            let message = format!("MacNeutron couldn't share this game's saves with the game VM: {first}");
            self.log.summary(&format!("error: {message}"));
            self.notifier.post("MacNeutron", &message);
        }
        // A logged game's log ends with the guest's console (kernel, services): the one log a player sends.
        if let (Some(game_log), Ok(console)) = (game_log, std::fs::read_to_string(share.join("console.log"))) {
            append(&format!("--- the VM's console ---\n{console}"), game_log);
        }
        // macneutron-vm's 7: the game's TPM didn't start, so the guest never ran (run-guest.sh removed the last
        // run.status first).
        if status == 7 && !share.join("run.status").exists() {
            self.notifier.post(
                "MacNeutron",
                "This game's TPM didn't start: the login keychain refused its key, or its files couldn't be read. The \
                 game's log has the reason.",
            );
        }
        Ok(status)
    }

    /// `MACNEUTRON_PERF=1`'s folder for a game's DXMT frame statistics on macOS (perf-compare.sh reads its stats.txt).
    pub fn perf_folder(&self, app_id: &str) -> PathBuf {
        self.log.directory.join(format!("perf/{app_id}"))
    }

    /// The toast while a game's prefix is prepared.
    pub(crate) fn setup_toast(target: &str) -> (String, String) {
        (
            format!("Setting up {}", Self::game_name(target)),
            "Preparing its Windows environment. The first launch takes a minute or two.".to_string(),
        )
    }

    /// A game's folder in steamapps/common (Steam's name for it, near enough), or "the game".
    pub(crate) fn game_name(target: &str) -> String {
        let marker = "/steamapps/common/";
        target
            .find(marker)
            .and_then(|at| target[at + marker.len()..].split('/').find(|s| !s.is_empty()))
            .unwrap_or("the game")
            .to_string()
    }

    /// Steam's Stop button sends SIGTERM: kill every Wine process in the game's prefix, in the environment the launch
    /// built (the server only talks to a client in its own msync mode).
    pub fn terminate(&self, environment: &BTreeMap<String, String>) {
        self.stop_requested.store(true, Ordering::SeqCst);
        let _ = self.vm_run.stop(); // not left running after the launcher
        let Ok(context) = CompatContext::new(environment) else { return };
        // Per-game settings from the app sit underneath; variables from Steam launch options win.
        let merged = match self.settings.load(&context.app_id) {
            Ok(settings) => {
                let mut merged = settings.environment();
                merged.extend(environment.clone());
                merged
            }
            Err(_) => environment.clone(),
        };
        let backend = GraphicsBackend::select(merged.get("MACNEUTRON_GRAPHICS").map(String::as_str)).0;
        let env = LaunchEnvironment::build(&merged, &context, backend, false);
        let _ = self.runner.run(&self.layout.wineserver(), &["-k".to_string()], &env, None);
    }

    /// Steam's Stop on macOS: it asks the processes it tracks to quit, as apps (the launcher is one while it runs). A
    /// microVM launch ends its VM and returns; one on macOS is left to the game, whose Wine is an app Steam asks too.
    /// True for a microVM launch.
    pub fn stop_vm(&self) -> bool {
        if !self.vm_run.stop() {
            return false;
        }
        self.stop_requested.store(true, Ordering::SeqCst); // before the boot: a shader replay stops, and nothing boots
        true
    }

    /// A crash reporter's report beside the launcher's logs, and the player told where it is. Never fails: a report
    /// that can't be copied must not lose the notification that the game crashed.
    fn report(&self, crash: &VMCrash, app_id: &str, game: &str, game_log: Option<&Path>) {
        let kept = self.keep(crash, app_id);
        if let Some(game_log) = game_log {
            append(&format!("--- the game's crash reporter ---\n{}\n", crash.raw), game_log);
        }
        let mut message = format!("{} crashed, so MacNeutron ended its game VM. ", Self::game_name(game));
        if let Some(kept) = &kept {
            message += &format!("Its crash report is in {}.", directory_path(kept));
        } else if let Some(first) = crash.reports.first() {
            message += &format!("Its crash report is in {first}.");
        } else {
            message += "No crash report was found; the game log may say more.";
        }
        let kept_text = kept.as_deref().map(directory_path).unwrap_or_else(|| "none".to_string());
        self.log.summary(&format!("crash: appid={app_id} {} kept={kept_text}", crash.summary()));
        self.notifier.post("MacNeutron", &message);
    }

    /// Copies the share's `traces/crash-report` (the reporter's `ps`, command line and the game's report files) to
    /// `~/Library/Logs/MacNeutron/crashes/<appid>/<time>`, a folder the player can open after the VM's share is gone.
    fn keep(&self, crash: &VMCrash, app_id: &str) -> Option<PathBuf> {
        let source = &crash.details_url;
        if !source.exists() {
            return None;
        }
        let when = if crash.when.is_empty() { iso8601_now() } else { crash.when.clone() };
        let stamp = when.replace(':', "-");
        let root = self.log.directory.join(format!("crashes/{app_id}"));
        let mut folder = root.join(&stamp);
        let mut serial = 1;
        while folder.exists() && serial < 100 {
            serial += 1;
            folder = root.join(format!("{stamp}-{serial}"));
        }
        let copied = std::fs::create_dir_all(&folder).and_then(|_| copy_item(source, &folder.join("report")));
        match copied {
            Ok(()) => Some(folder),
            Err(error) => {
                self.log.append(&format!("note: couldn't keep {app_id}'s crash report: {error}"));
                None
            }
        }
    }

    fn run_game(
        &self,
        request: &LaunchRequest,
        env: &BTreeMap<String, String>,
        game_log: Option<&Path>,
        through_steam: bool,
    ) -> std::io::Result<i32> {
        // The macOS path as Steam gave it: steam.exe converts it (Proton's run_process), to S: for a game in a library.
        let mut command = Vec::with_capacity(request.arguments.len() + 2);
        if through_steam {
            command.push(SteamBridge::steam_exe(env.get("SteamGameId").map(String::as_str)).to_string());
        }
        command.push(request.target.clone());
        command.extend(request.arguments.iter().cloned());
        self.runner.run(&self.layout.wine(), &command, env, game_log)
    }

    /// Spec D2: Steam's copy and the Mac's were both kept, and only the player can choose between them.
    fn notify_about_cloud_conflicts(&self, links: &ProfileLinkReport) {
        if links.conflicts > 0 {
            self.notifier.post(
                "MacNeutron",
                "Steam Cloud saves for this game differ from the ones on this Mac, so MacNeutron kept both. Details are \
                 in launcher.log.",
            );
        }
    }

    /// `run` and `waitforexitandrun` start the game through `steam.exe` when the bridge is installed, as Proton does;
    /// the log says why when they don't.
    fn uses_steam_bridge(&self, verb: Verb, environment: &BTreeMap<String, String>) -> bool {
        if verb != Verb::Run && verb != Verb::WaitForExitAndRun {
            return false;
        }
        if environment.get("MACNEUTRON_NO_STEAM_BRIDGE").map(String::as_str) == Some("1") {
            self.log.append("note: Steam bridge disabled by launch option");
            return false;
        }
        if !self.layout.steam_bridge_installed() {
            self.log.append("note: Steam bridge not installed");
            return false;
        }
        true
    }

    /// Tells the runtime's lsteamclient where macOS Steam's client library is, and steam.exe Steam's language.
    fn add_steam_client(&self, env: &mut BTreeMap<String, String>) {
        let passed = env.get("STEAM_COMPAT_CLIENT_INSTALL_PATH").cloned();
        let client = SteamBridge::client_directory(passed.as_deref(), &self.steam);
        env.insert("STEAM_COMPAT_CLIENT_INSTALL_PATH".into(), client.clone());
        self.log.append(&format!(
            "note: Steam client folder {client} (Steam passed {})",
            passed.as_deref().unwrap_or("nothing")
        ));
        if !Path::new(&format!("{client}/steamclient.dylib")).exists() {
            self.log.append(&format!("note: steamclient.dylib not found in {client}"));
        }
        if !env.contains_key("MACNEUTRON_STEAM_LANGUAGE") {
            if let Some(language) = self.steam.ui_language() {
                env.insert("MACNEUTRON_STEAM_LANGUAGE".into(), language);
            }
        }
    }

    /// People post game logs in bug reports: Steam passes the account's login name as SteamUser and SteamAppUser.
    pub(crate) const REDACTED_KEYS: [&'static str; 2] = ["SteamUser", "SteamAppUser"];

    /// The rest of the environment comes from whatever started Steam (a terminal's shell can carry API tokens): any
    /// name that reads like a secret is hidden too.
    pub(crate) fn is_redacted(key: &str) -> bool {
        let name = key.to_uppercase();
        Self::REDACTED_KEYS.contains(&key)
            || ["TOKEN", "SECRET", "PASSWORD", "PASSWD", "CREDENTIAL", "APIKEY", "API_KEY", "AUTH"]
                .iter()
                .any(|word| name.contains(word))
            || name.ends_with("_KEY")
    }

    fn write_header(&self, game_log: &Path, request: &LaunchRequest, environment: &BTreeMap<String, String>) {
        let mut text = format!(
            "=== {} {} {} {:?}\n",
            iso8601_now(),
            request.verb.raw_value(),
            request.target,
            request.arguments
        );
        for (key, value) in environment {
            let value = if Self::is_redacted(key) { "<redacted>" } else { value };
            text += &format!("{key}={value}\n");
        }
        append(&text, game_log);
    }

    fn fail(&self, message: &str, argv: &[String], notify: bool, game_log: Option<&Path>) -> i32 {
        self.log.summary(&format!("error: {message} argv={argv:?}"));
        if let Some(game_log) = game_log {
            append(&format!("macneutron: {message}\n"), game_log);
        }
        eprintln!("macneutron: {message}");
        if notify {
            self.notifier.post("MacNeutron", message);
        }
        1
    }
}

fn append(text: &str, game_log: &Path) {
    use std::io::Write;
    if let Ok(mut file) = std::fs::OpenOptions::new().create(true).append(true).open(game_log) {
        let _ = file.write_all(text.as_bytes());
    }
}

/// A folder's path as a directory URL's: with its trailing slash.
fn directory_path(path: &Path) -> String {
    let text = path.to_string_lossy().into_owned();
    if text.ends_with('/') {
        text
    } else {
        text + "/"
    }
}

/// `FileManager.copyItem`: a file, or a folder with everything in it.
fn copy_item(source: &Path, destination: &Path) -> std::io::Result<()> {
    let (Ok(from), Ok(to)) = (
        std::ffi::CString::new(source.as_os_str().as_encoded_bytes()),
        std::ffi::CString::new(destination.as_os_str().as_encoded_bytes()),
    ) else {
        return Err(std::io::Error::from(std::io::ErrorKind::InvalidInput));
    };
    let flags = libc::COPYFILE_DATA | libc::COPYFILE_METADATA | libc::COPYFILE_RECURSIVE;
    if unsafe { libc::copyfile(from.as_ptr(), to.as_ptr(), std::ptr::null_mut(), flags) } == 0 {
        Ok(())
    } else {
        Err(std::io::Error::last_os_error())
    }
}

/// A name no other launch uses: this process, the time, and a counter (Swift's UUID).
fn unique() -> String {
    use std::sync::atomic::AtomicU64;
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let nanos = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_nanos()).unwrap_or(0);
    format!("{}-{nanos}-{}", std::process::id(), COUNTER.fetch_add(1, Ordering::Relaxed))
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use crate::test_support::{
        install_fake_steam_bridge, make_steam_location, make_temp_dir, make_tool_layout, pe_bytes, registry_vdf,
        set_runtime_version, steam_environment, wineboot_creating_prefix, write, Call, FakeRunner, RecordingNotifier,
        TEST_IDENTITY,
    };

    pub(crate) fn args(words: &[&str]) -> Vec<String> {
        words.iter().map(|w| w.to_string()).collect()
    }

    /// A launcher on `layout` with fakes everywhere: logs, settings and Steam in temp folders, `identity` for the
    /// runtime's, no display, the real home.
    pub(crate) fn test_launcher(
        layout: ToolLayout,
        runner: Arc<dyn ProcessRunner>,
        notifier: Arc<RecordingNotifier>,
        identity: Option<&str>,
        micro_vm_root: PathBuf,
    ) -> Launcher {
        let identity = identity.map(str::to_string);
        Launcher {
            runner,
            log: Arc::new(LauncherLog::new(make_temp_dir().join("Logs"))),
            notifier,
            preflight: Preflight::new(Box::new(|| true), Box::new(move |_| identity.clone()), Box::new(|_| true)),
            settings: GameSettingsStore::new(make_temp_dir().join("games")),
            steam: make_steam_location(true),
            micro_vm_root,
            display: Box::new(|| None),
            ..Launcher::new(layout)
        }
    }

    pub(crate) fn read(path: &Path) -> String {
        std::fs::read_to_string(path).unwrap_or_default()
    }

    struct Fixture {
        launcher: Launcher,
        runner: Arc<FakeRunner>,
        notifier: Arc<RecordingNotifier>,
        env: BTreeMap<String, String>,
    }

    impl Fixture {
        fn launch(&self, argv: &[&str], env: &BTreeMap<String, String>) -> i32 {
            self.launcher.launch(&args(argv), env)
        }

        fn launcher_log(&self) -> String {
            read(&self.launcher.log.launcher_log())
        }

        fn calls(&self) -> Vec<Call> {
            self.runner.calls()
        }

        fn last(&self) -> Call {
            self.calls().last().cloned().unwrap()
        }

        fn last_env(&self, key: &str) -> Option<String> {
            self.last().environment.get(key).cloned()
        }

        fn prefix(&self) -> PathBuf {
            CompatContext::new(&self.env).unwrap().prefix()
        }

        fn save(&self, settings: GameSettings) {
            self.launcher.settings.save(&settings, "42").unwrap();
        }
    }

    fn make_fixture_with(runner: FakeRunner, identity: Option<&str>, bridge: bool) -> Fixture {
        let notifier = Arc::new(RecordingNotifier::default());
        let layout = make_tool_layout();
        if bridge {
            install_fake_steam_bridge(&layout);
        }
        let runner = Arc::new(runner);
        let launcher = test_launcher(layout, runner.clone(), notifier.clone(), identity, make_temp_dir().join("no-vm"));
        let env = steam_environment(&make_temp_dir().join("compatdata/42"), "42");
        Fixture { launcher, runner, notifier, env }
    }

    fn make_fixture() -> Fixture {
        make_fixture_with(wineboot_creating_prefix(0, 0.0), Some(TEST_IDENTITY), false)
    }

    fn make_bridge_fixture() -> Fixture {
        make_fixture_with(wineboot_creating_prefix(0, 0.0), Some(TEST_IDENTITY), true)
    }

    fn with(env: &BTreeMap<String, String>, pairs: &[(&str, &str)]) -> BTreeMap<String, String> {
        let mut env = env.clone();
        for (key, value) in pairs {
            env.insert(key.to_string(), value.to_string());
        }
        env
    }

    fn command(call: &Call) -> Vec<String> {
        std::iter::once(call.tool.clone()).chain(call.arguments.iter().cloned()).collect()
    }

    const DXMT_OVERRIDES: &str = "dxgi=n,b;d3d10core=n,b;d3d11=n,b";

    fn creates_prefix(call: &Call) {
        if call.arguments.first().map(String::as_str) == Some("wineboot") {
            if let Some(prefix) = call.environment.get("WINEPREFIX") {
                let _ = std::fs::create_dir_all(prefix);
            }
        }
    }

    #[test]
    fn wait_for_exit_and_run_prepares_waits_runs_then_waits() {
        // Proton's order: preparing first means a launch that queued on the prefix lock behind
        // `run iscriptevaluator.exe` finds that session's wineserver alive and waits it out.
        let runner = FakeRunner::new(|call| {
            creates_prefix(call);
            if call.arguments.first().map(String::as_str) == Some("/g/Game.exe") { 7 } else { 0 }
        });
        let f = make_fixture_with(runner, Some(TEST_IDENTITY), false);
        assert_eq!(f.launch(&["waitforexitandrun", "/g/Game.exe", "-windowed"], &f.env), 7);
        let commands: Vec<Vec<String>> = f.calls().iter().map(command).collect();
        assert_eq!(
            commands,
            vec![
                args(&["wine", "wineboot", "-u"]),
                args(&["wine", "reg", "add", r"HKLM\Software\Microsoft\Wow64\amd64", "/ve", "/d", "libarm64ecfex.dll", "/f"]),
                args(&["wine", "reg", "add", r"HKCU\Software\Wine\WineDbg", "/v", "ShowCrashDialog", "/t", "REG_DWORD", "/d", "0", "/f"]),
                args(&["wine", "reg", "add", r"HKCU\Software\Wine\X11 Driver", "/v", "EmulateModeset", "/d", "Y", "/f"]),
                args(&["wineserver", "-w"]),
                args(&["wineserver", "-w"]),
                args(&["wine", "/g/Game.exe", "-windowed"]),
                args(&["wineserver", "-w"]),
            ]
        );
    }

    #[test]
    fn a_new_prefix_is_prepared_under_a_toast() {
        let f = make_fixture();
        let game = "/Steam/steamapps/common/Some Game/bin/Game.exe";
        let env = with(&f.env, &[("MACNEUTRON_VM", "0")]); // a game in a library runs on macOS only by opting out
        f.launch(&["waitforexitandrun", game], &env);
        assert_eq!(f.notifier.toasts(), ["Setting up Some Game"]);
        assert_eq!(f.notifier.toasts_closed(), 1);
        f.launch(&["waitforexitandrun", game], &env); // up to date: no toast
        assert_eq!(f.notifier.toasts().len(), 1);
    }

    #[test]
    fn run_does_not_wait_for_wineserver() {
        let f = make_fixture();
        f.launch(&["run", "/g/Game.exe"], &f.env); // prepares the prefix (its own `-w` included)
        let prepared = f.calls().len();
        assert_eq!(f.launch(&["run", "/g/iscriptevaluator.exe", "--get-current-step", "42"], &f.env), 0);
        let after: Vec<Vec<String>> = f.calls()[prepared..].iter().map(|c| c.arguments.clone()).collect();
        assert_eq!(after, vec![args(&["/g/iscriptevaluator.exe", "--get-current-step", "42"])]);
    }

    #[test]
    fn run_in_prefix_skips_preparation() {
        let f = make_fixture();
        assert_eq!(f.launch(&["runinprefix", "/g/tool.exe"], &f.env), 0);
        let all: Vec<Vec<String>> = f.calls().iter().map(|c| c.arguments.clone()).collect();
        assert_eq!(all, vec![args(&["/g/tool.exe"])]);
    }

    #[test]
    fn get_compat_path_converts_through_winepath() {
        let f = make_fixture();
        assert_eq!(f.launch(&["getcompatpath", "/g/save"], &f.env), 0);
        assert_eq!(f.last().arguments, args(&["winepath.exe", "-w", "/g/save"]));
    }

    #[test]
    fn game_arguments_pass_through_unchanged() {
        let f = make_fixture();
        let game = ["/Steam Library/My Game/Game.exe", "--name=\"Player One\"", "a b", "ünïcode", ""];
        let argv: Vec<&str> = std::iter::once("run").chain(game).collect();
        f.launch(&argv, &f.env);
        assert_eq!(f.last().arguments, args(&game));
    }

    #[test]
    fn unknown_verb_fails_without_running_anything() {
        let f = make_fixture();
        assert_eq!(f.launch(&["destroyprefix", "/g/Game.exe"], &f.env), 1);
        assert!(f.calls().is_empty());
    }

    #[test]
    fn launching_outside_steam_fails_cleanly() {
        let f = make_fixture();
        let env = BTreeMap::from([("PATH".to_string(), "/usr/bin".to_string())]);
        assert_eq!(f.launch(&["run", "/g/Game.exe"], &env), 1);
        assert!(f.calls().is_empty());
    }

    #[test]
    fn invalid_graphics_setting_still_launches_with_default() {
        let f = make_fixture();
        let env = with(&f.env, &[("MACNEUTRON_GRAPHICS", "vulkan")]);
        assert_eq!(f.launch(&["run", "/g/Game.exe"], &env), 0);
        assert!(f.last_env("WINEDLLOVERRIDES").unwrap().starts_with(DXMT_OVERRIDES));
        let logged = f.launcher_log();
        assert!(logged.contains("backend=dxmt"));
        assert!(logged.contains("note=unknown MACNEUTRON_GRAPHICS 'vulkan'"));
    }

    #[test]
    fn missing_runtime_notifies_and_fails() {
        let f = make_fixture_with(wineboot_creating_prefix(0, 0.0), None, false);
        assert_eq!(f.launch(&["run", "/g/Game.exe"], &f.env), 1);
        assert!(f.calls().is_empty());
        assert_eq!(f.notifier.posted(), ["MacNeutron's runtime is missing or damaged. Open MacNeutron to repair it."]);
        assert!(f.launcher.layout.runtime_damaged_marker().exists());
    }

    pub(crate) const THIRTY_TWO_BIT_TEXT: &str =
        "This game is 32-bit. On macOS MacNeutron runs 64-bit games only; 32-bit games in a Steam library run in its game VM.";

    fn i386_exe(name: &str) -> String {
        let path = make_temp_dir().join(name);
        std::fs::write(&path, pe_bytes(pe_image::I386, false)).unwrap();
        path.to_string_lossy().into_owned()
    }

    #[test]
    fn thirty_two_bit_game_is_refused_with_its_message() {
        let f = make_fixture();
        assert_eq!(f.launch(&["waitforexitandrun", &i386_exe("Game.exe")], &f.env), 1);
        assert!(f.calls().is_empty());
        assert_eq!(f.notifier.posted(), [THIRTY_TWO_BIT_TEXT]);
        assert!(f.launcher_log().contains(&format!("error: {THIRTY_TWO_BIT_TEXT}")));
    }

    #[test]
    fn run_skips_a_32bit_installer() {
        // Steam's install scripts start redistributable installers with `run`; a 32-bit one can't run, and isn't the
        // game.
        let f = make_fixture();
        assert_eq!(f.launch(&["run", &i386_exe("Setup.exe"), "/quiet"], &f.env), 0);
        assert!(f.calls().is_empty());
        assert!(f.notifier.posted().is_empty());
        assert!(f.launcher_log().contains("skipped 32-bit installer Setup.exe"));
    }

    #[test]
    fn failure_messages_reach_the_game_log() {
        let f = make_fixture();
        let env = with(&f.env, &[("MACNEUTRON_LOG", "1")]);
        assert_eq!(f.launch(&["waitforexitandrun", &i386_exe("Game.exe")], &env), 1);
        assert!(read(&f.launcher.log.game_log("42")).contains(THIRTY_TWO_BIT_TEXT));
    }

    #[test]
    fn failed_prefix_setup_notifies_and_skips_game() {
        let f = make_fixture_with(wineboot_creating_prefix(5, 0.0), Some(TEST_IDENTITY), false);
        assert_eq!(f.launch(&["waitforexitandrun", "/g/Game.exe"], &f.env), 1);
        assert!(!f.calls().iter().any(|c| c.arguments.first().map(String::as_str) == Some("/g/Game.exe")));
        assert_eq!(f.notifier.posted().len(), 1);
    }

    #[test]
    fn macneutron_log_sends_game_output_to_per_game_log() {
        let f = make_fixture();
        f.launch(&["run", "/g/Game.exe"], &with(&f.env, &[("MACNEUTRON_LOG", "1")]));
        let game_log = f.launcher.log.game_log("42");
        assert_eq!(f.last().output, Some(game_log.clone()));
        assert!(read(&game_log).contains("WINEDEBUG=warn+all,warn-seh,+loaddll,warn+steamclient\n"));
    }

    #[test]
    fn macneutron_log2_keeps_every_warning_and_the_steam_trace() {
        let f = make_fixture();
        f.launch(&["run", "/g/Game.exe"], &with(&f.env, &[("MACNEUTRON_LOG", "2")]));
        assert_eq!(f.last().output, Some(f.launcher.log.game_log("42")));
        assert_eq!(f.last_env("WINEDEBUG").as_deref(), Some("warn+all,+loaddll,+steamclient"));
    }

    #[test]
    fn anisotropic_filtering_reaches_the_game() {
        let f = make_fixture();
        f.launch(&["run", "/g/Game.exe"], &f.env);
        assert_eq!(f.last_env("DXMT_MAX_ANISOTROPY").as_deref(), Some("16")); // no settings file: the default
        f.save(GameSettings { anisotropy: Some("game".into()), ..Default::default() });
        f.launch(&["run", "/g/Game.exe"], &f.env);
        assert_eq!(f.last_env("DXMT_MAX_ANISOTROPY"), None);
    }

    #[test]
    fn mtld3d_logs_in_the_log_folder_not_the_games() {
        // mtld3d writes mtld3d-logs/ beside the game's exe unless told otherwise: ours go to ~/Library/Logs/MacNeutron.
        let f = make_fixture();
        let dir = f.launcher.log.directory.join("mtld3d").to_string_lossy().into_owned();
        f.launch(&["run", "/g/Game.exe"], &f.env);
        assert_eq!(f.last_env("MTLD3D_CONFIG"), Some(format!("log.dir=Z:{dir}")));
        // The user's MTLD3D_CONFIG comes after ours: mtld3d applies its entries in order, so theirs win.
        f.launch(&["run", "/g/Game.exe"], &with(&f.env, &[("MTLD3D_CONFIG", "log.dir=logs;present.maxFps=60")]));
        assert_eq!(f.last_env("MTLD3D_CONFIG"), Some(format!("log.dir=Z:{dir};log.dir=logs;present.maxFps=60")));
    }

    #[test]
    fn every_launch_is_logged_with_versions() {
        let f = make_fixture();
        f.launch(&["run", "/g/Game.exe"], &f.env);
        assert!(f.launcher_log().ends_with("verb=run appid=42 backend=dxmt runtime=test (0123456789ab) exit=0\n"));
    }

    #[test]
    fn terminate_kills_the_prefix_wineserver_under_the_games_msync() {
        // The server runs with the game's WINEMSYNC; a client in the other mode can't talk to it.
        let f = make_fixture();
        f.launcher.terminate(&f.env);
        assert_eq!(f.calls().iter().map(command).collect::<Vec<_>>(), vec![args(&["wineserver", "-k"])]);
        assert!(f.calls()[0].environment["WINEPREFIX"].ends_with("compatdata/42/pfx/"));
        assert_eq!(f.calls()[0].environment.get("WINEMSYNC").map(String::as_str), Some("1"));
        f.save(GameSettings { msync: Some(false), ..Default::default() });
        f.launcher.terminate(&f.env);
        assert_eq!(f.calls().len(), 2);
        assert_eq!(f.calls()[1].environment.get("WINEMSYNC"), None);
    }

    #[test]
    fn game_settings_apply_underneath_launch_options() {
        let f = make_fixture();
        f.save(GameSettings {
            graphics: Some("wined3d".into()),
            log: Some(true),
            msync: Some(false),
            ..Default::default()
        });
        // typed into Steam's launch options: wins
        f.launch(&["run", "/g/Game.exe"], &with(&f.env, &[("MACNEUTRON_GRAPHICS", "dxmt")]));
        let wine = f.last().environment;
        assert!(wine["WINEDLLOVERRIDES"].starts_with(DXMT_OVERRIDES)); // dxmt
        assert_eq!(wine["WINEDEBUG"], "warn+all,warn-seh,+loaddll,warn+steamclient");
        assert_eq!(wine.get("WINEMSYNC"), None);
    }

    #[test]
    fn launch_options_field_applies_under_steam_and_adds_arguments() {
        let f = make_fixture();
        f.save(GameSettings {
            launch_options: Some("MACNEUTRON_GRAPHICS=wined3d DXMT_D3D12_SM6=0 -dx12".into()),
            ..Default::default()
        });
        let env = with(&f.env, &[("MACNEUTRON_GRAPHICS", "dxmt")]); // Steam's launch options win over the field
        assert_eq!(f.launch(&["waitforexitandrun", "/g/Game.exe", "-windowed"], &env), 0);
        let game = f.calls().into_iter().find(|c| c.arguments.first().map(String::as_str) == Some("/g/Game.exe")).unwrap();
        assert_eq!(game.arguments, args(&["/g/Game.exe", "-windowed", "-dx12"]));
        assert_eq!(game.environment["DXMT_D3D12_SM6"], "0");
        assert!(game.environment["WINEDLLOVERRIDES"].starts_with(DXMT_OVERRIDES)); // dxmt
        f.launch(&["run", "/g/Setup.exe"], &env); // an install script's run
        let setup = f.calls().into_iter().rev().find(|c| c.arguments.first().map(String::as_str) == Some("/g/Setup.exe"));
        assert_eq!(setup.unwrap().arguments, args(&["/g/Setup.exe"]));
    }

    #[test]
    fn old_graphics_value_runs_dxmt_with_a_note() {
        let f = make_fixture();
        f.save(GameSettings { graphics: Some("dxvk".into()), ..Default::default() });
        assert_eq!(f.launch(&["run", "/g/Game.exe"], &f.env), 0);
        assert!(f.last_env("WINEDLLOVERRIDES").unwrap().starts_with(DXMT_OVERRIDES));
        let log = f.launcher_log();
        let line = log.split('\n').rev().find(|l| l.contains(" verb=run ")).unwrap();
        assert!(line.contains("backend=dxmt"));
        assert!(line.contains("'dxvk' was removed in 0.1, using dxmt"));
    }

    #[test]
    fn unreadable_game_settings_are_ignored() {
        let f = make_fixture();
        write("{ not json", &f.launcher.settings.directory.join("42.json"), false);
        assert_eq!(f.launch(&["run", "/g/Game.exe"], &f.env), 0);
        assert!(f.launcher_log().contains("ignoring unreadable game settings for 42"));
        assert_eq!(f.last_env("DXMT_MAX_ANISOTROPY").as_deref(), Some("16")); // the defaults still apply
    }

    #[test]
    fn game_goes_through_steam_exe_when_the_bridge_is_installed() {
        let f = make_bridge_fixture();
        assert_eq!(f.launch(&["waitforexitandrun", "/Steam Library/My Game/Game.exe", "-windowed"], &f.env), 0);
        let game = f
            .calls()
            .into_iter()
            .find(|c| c.arguments.first().map(String::as_str) == Some(SteamBridge::STEAM_EXE))
            .unwrap();
        // The game's macOS path, raw: steam.exe converts it (Proton's run_process), to S: in a Steam library.
        assert_eq!(game.arguments, args(&[r"C:\windows\system32\steam.exe", "/Steam Library/My Game/Game.exe", "-windowed"]));
        let bundle = f.launcher.steam.bundle_macos().to_string_lossy().trim_end_matches('/').to_string();
        assert_eq!(game.environment["STEAM_COMPAT_CLIENT_INSTALL_PATH"], bundle);
        assert_eq!(game.environment.get("MACNEUTRON_STEAM_ACCOUNT"), None); // Proton writes no ActiveUser (parity L2)
        assert!(f.prefix().join("drive_c/Program Files (x86)/Steam/steamclient64.dll").exists());
    }

    #[test]
    fn run_in_prefix_never_goes_through_steam_exe() {
        let f = make_bridge_fixture();
        f.launch(&["runinprefix", "/g/tool.exe"], &f.env);
        let all: Vec<Vec<String>> = f.calls().iter().map(|c| c.arguments.clone()).collect();
        assert_eq!(all, vec![args(&["/g/tool.exe"])]);
    }

    #[test]
    fn escape_hatch_starts_the_game_directly() {
        let f = make_bridge_fixture();
        f.launch(&["run", "/g/Game.exe"], &with(&f.env, &[("MACNEUTRON_NO_STEAM_BRIDGE", "1")]));
        assert_eq!(f.last().arguments, args(&["/g/Game.exe"]));
        assert!(f.launcher_log().contains("note: Steam bridge disabled by launch option"));
    }

    #[test]
    fn missing_bridge_starts_the_game_directly() {
        let f = make_fixture();
        f.launch(&["run", "/g/Game.exe"], &f.env);
        assert_eq!(f.last().arguments, args(&["/g/Game.exe"]));
        assert!(f.launcher_log().contains("note: Steam bridge not installed"));
    }

    #[test]
    fn steams_language_reaches_steam_exe() {
        let f = make_bridge_fixture();
        write(&registry_vdf("french"), &f.launcher.steam.registry_vdf(), false);
        f.launch(&["run", "/g/Game.exe"], &f.env);
        assert_eq!(f.last_env("MACNEUTRON_STEAM_LANGUAGE").as_deref(), Some("french"));
        // a launch option wins
        f.launch(&["run", "/g/Game.exe"], &with(&f.env, &[("MACNEUTRON_STEAM_LANGUAGE", "latam")]));
        assert_eq!(f.last_env("MACNEUTRON_STEAM_LANGUAGE").as_deref(), Some("latam"));
    }

    #[test]
    fn steams_client_path_is_logged_and_checked_for_the_library() {
        let f = make_bridge_fixture();
        std::fs::remove_file(f.launcher.steam.bundle_macos().join("steamclient.dylib")).unwrap();
        f.launch(&["run", "/g/Game.exe"], &with(&f.env, &[("STEAM_COMPAT_CLIENT_INSTALL_PATH", "/nowhere")]));
        assert!(f.launcher_log().contains("(Steam passed /nowhere)"));
        assert!(f.launcher_log().contains("note: steamclient.dylib not found in"));
    }

    #[test]
    fn three_games_run_steam_exe_from_the_steam_folder() {
        // Proton runs C:\windows\system32\steam.exe, except for three games it runs from the Steam folder
        // (proton:2109-2115), keyed on SteamGameId.
        let expected = [
            ("311210", r"C:\Program Files (x86)\Steam\steam.exe"),
            ("1985810", r"C:\Program Files (x86)\Steam\steam.exe"),
            ("1549250", r"C:\Program Files (x86)\Steam\steam.exe"),
            ("480", r"C:\windows\system32\steam.exe"),
        ];
        for (id, path) in expected {
            let f = make_bridge_fixture();
            f.launch(&["waitforexitandrun", "/g/Game.exe"], &with(&f.env, &[("SteamGameId", id)]));
            let game = f.calls().into_iter().find(|c| c.arguments.first().is_some_and(|a| a.ends_with(r"\steam.exe")));
            assert_eq!(game.and_then(|g| g.arguments.first().cloned()).as_deref(), Some(path), "SteamGameId {id}");
        }
    }

    #[test]
    fn escape_hatch_keeps_steam_exe_for_steam_links() {
        // The game's steam:// links go through HKCR\steam to the Steam folder's steam.exe: without the bridge they
        // still reach Mac Steam (parity plan R10).
        let f = make_bridge_fixture();
        f.launch(&["run", "/g/Game.exe"], &f.env);
        f.launch(&["run", "/g/Game.exe"], &with(&f.env, &[("MACNEUTRON_NO_STEAM_BRIDGE", "1")]));
        for path in ["drive_c/Program Files (x86)/Steam/steam.exe", "drive_c/windows/system32/steam.exe"] {
            assert!(f.prefix().join(path).exists(), "{path}");
        }
    }

    #[test]
    fn escape_hatch_takes_the_bridge_out_of_the_prefix() {
        // Seen in acceptance: a game's steam_api loads the client DLL an earlier launch left (its registry values
        // persist), and without Steam's client path the bridge aborts the game. Without the files, the game just
        // finds no Steam.
        let f = make_bridge_fixture();
        f.launch(&["run", "/g/Game.exe"], &f.env);
        let folder = f.prefix().join("drive_c/Program Files (x86)/Steam");
        assert!(folder.join("steamclient64.dll").exists());
        f.launch(&["run", "/g/Game.exe"], &with(&f.env, &[("MACNEUTRON_NO_STEAM_BRIDGE", "1")]));
        for name in ["steamclient64.dll", "steamclient.dll"] {
            assert!(!folder.join(name).exists(), "{name}");
        }
    }

    #[test]
    fn any_direct_start_takes_leftover_bridge_files_out() {
        // Same abort as with the escape hatch when the runtime or tool folder loses a bridge file.
        let f = make_bridge_fixture();
        f.launch(&["run", "/g/Game.exe"], &f.env);
        std::fs::remove_file(f.launcher.layout.steam_helper()).unwrap();
        f.launch(&["run", "/g/Game.exe"], &f.env);
        assert!(!f.prefix().join("drive_c/Program Files (x86)/Steam/steamclient64.dll").exists());
    }

    #[test]
    fn game_logs_hide_the_steam_login_name() {
        // People post game logs in bug reports, and Steam passes the account's login name as SteamUser and
        // SteamAppUser. MacNeutron adds no account variable of its own. (Fake values.)
        let f = make_bridge_fixture();
        let env = with(&f.env, &[("MACNEUTRON_LOG", "1"), ("SteamUser", "fakelogin"), ("SteamAppUser", "fakeapplogin")]);
        f.launch(&["run", "/g/Game.exe"], &env);
        let log = read(&f.launcher.log.game_log("42"));
        assert!(log.contains("SteamUser=<redacted>\n"));
        assert!(log.contains("SteamAppUser=<redacted>\n"));
        assert!(!log.contains("fakelogin") && !log.contains("fakeapplogin"));
        assert_eq!(f.last_env("MACNEUTRON_STEAM_ACCOUNT"), None);
        // only the header hides them
        assert_eq!(f.last_env("SteamUser").as_deref(), Some("fakelogin"));
        assert_eq!(f.last_env("SteamAppUser").as_deref(), Some("fakeapplogin"));
    }

    #[test]
    fn game_logs_hide_secret_looking_variables() {
        // Steam started from a terminal passes that shell's environment on, API tokens included. (Fake values.)
        let f = make_fixture();
        let secrets = [
            ("GITHUB_TOKEN", "fake-gh"),
            ("SOME_API_KEY", "fake-api"),
            ("AWS_SECRET_ACCESS_KEY", "fake-aws"),
            ("DB_PASSWORD", "fake-pw"),
            ("SSH_AUTH_SOCK", "/tmp/fake-sock"),
            ("Session_Token", "fake-lower"),
        ];
        let env = with(&with(&f.env, &[("MACNEUTRON_LOG", "1")]), &secrets);
        f.launch(&["run", "/g/Game.exe"], &env);
        let log = read(&f.launcher.log.game_log("42"));
        for (key, value) in secrets {
            assert!(log.contains(&format!("{key}=<redacted>\n")), "{key}");
            assert!(!log.contains(value), "{key}");
        }
        assert!(log.contains("MACNEUTRON_LOG=1\n")); // ordinary variables stay readable
        assert_eq!(f.last_env("GITHUB_TOKEN").as_deref(), Some("fake-gh")); // only the header hides them
    }

    #[test]
    fn presenter_is_asked_for_by_default() {
        let f = make_bridge_fixture();
        f.launch(&["waitforexitandrun", "/g/Game.exe"], &f.env);
        let game = f
            .calls()
            .into_iter()
            .find(|c| c.arguments.first().map(String::as_str) == Some(SteamBridge::STEAM_EXE))
            .unwrap();
        assert_eq!(game.environment["MACNEUTRON_PRESENT"], "1");
        assert!(f.calls().iter().all(|c| !c.environment.contains_key("DYLD_INSERT_LIBRARIES")));
    }

    #[test]
    fn opting_out_leaves_the_presenter_off() {
        let f = make_fixture();
        f.launch(&["run", "/g/Game.exe"], &with(&f.env, &[("MACNEUTRON_NO_METALFX", "1")]));
        assert_eq!(f.last_env("MACNEUTRON_PRESENT"), None);
        f.save(GameSettings { metal_fx: Some(false), ..Default::default() });
        f.launch(&["run", "/g/Game.exe"], &f.env);
        assert_eq!(f.last_env("MACNEUTRON_PRESENT"), None);
    }

    #[test]
    fn post_anti_aliasing_keeps_the_presenter_with_metalfx_off() {
        // The presenter runs CMAA2 and reads MACNEUTRON_NO_METALFX itself to skip only its upscaling.
        let f = make_fixture();
        f.save(GameSettings { metal_fx: Some(false), post_aa: Some("cmaa2".into()), ..Default::default() });
        f.launch(&["run", "/g/Game.exe"], &f.env);
        let env = f.last().environment;
        assert_eq!(env["MACNEUTRON_PRESENT"], "1");
        assert_eq!(env["MACNEUTRON_NO_METALFX"], "1");
        assert_eq!(env["MACNEUTRON_POST_AA"], "cmaa2");
        // a launch option wins over the game's setting
        f.launch(&["run", "/g/Game.exe"], &with(&f.env, &[("MACNEUTRON_POST_AA", "off")]));
        assert_eq!(f.last_env("MACNEUTRON_PRESENT"), None);
    }

    #[test]
    fn tool_commands_get_no_presenter() {
        let f = make_fixture();
        f.launch(&["runinprefix", "/g/tool.exe"], &f.env);
        f.launch(&["getcompatpath", "/g/save"], &f.env);
        assert!(!f.calls().is_empty());
        assert!(f.calls().iter().all(|c| !c.environment.contains_key("MACNEUTRON_PRESENT")));
    }

    #[test]
    fn a_cloud_conflict_posts_a_notification() {
        // Spec D2: both copies are kept, and only the player can choose between them.
        let runner = FakeRunner::new(|call| {
            if call.arguments.first().map(String::as_str) == Some("wineboot") {
                if let Some(prefix) = call.environment.get("WINEPREFIX") {
                    write("local", &Path::new(prefix).join("drive_c/users/u/AppData/Local/G/s.sav"), false);
                }
            }
            0
        });
        let f = make_fixture_with(runner, Some(TEST_IDENTITY), false);
        let prefix = Path::new(&f.env["STEAM_COMPAT_DATA_PATH"]).join("pfx");
        write("cloud", &prefix.join("drive_c/users/steamuser/Local Settings/Application Data/G/s.sav"), false);
        f.launch(&["waitforexitandrun", "/g/Game.exe"], &with(&f.env, &[("USER", "u")]));
        assert_eq!(
            f.notifier.posted(),
            ["Steam Cloud saves for this game differ from the ones on this Mac, so MacNeutron kept both. Details are in launcher.log."]
        );
    }

    const LIBRARY_GAME: &str = "/Steam/steamapps/common/Some Game/bin/Game.exe";

    fn perf_env(f: &Fixture) -> BTreeMap<String, String> {
        with(&f.env, &[("MACNEUTRON_VM", "0"), ("MACNEUTRON_PERF", "1")])
    }

    fn game_run(f: &Fixture) -> Call {
        f.calls().into_iter().rev().find(|c| c.arguments.first().map(String::as_str) == Some(LIBRARY_GAME)).unwrap()
    }

    #[test]
    fn perf_turns_on_dxmt_stats_on_macos() {
        // MACNEUTRON_PERF=1 (vm/tests/perf-compare.sh): DXMT's frame statistics in the game's perf folder.
        let f = make_fixture();
        let mut env = perf_env(&f);
        f.launch(&["waitforexitandrun", LIBRARY_GAME], &env);
        let folder = f.launcher.perf_folder("42");
        let run = game_run(&f);
        assert_eq!(run.environment["DXMT_STATS"], "1");
        assert_eq!(run.environment["DXMT_DXIL_DUMP"], directory_path(&folder));
        assert!(folder.exists());
        // A folder the user named wins.
        env.insert("DXMT_DXIL_DUMP".into(), "/elsewhere".into());
        f.launch(&["waitforexitandrun", LIBRARY_GAME], &env);
        assert_eq!(game_run(&f).environment["DXMT_DXIL_DUMP"], "/elsewhere");
    }

    #[test]
    fn a_performance_runtime_ignores_perf() {
        // X.Y.Zp has no DXMT stats or capture to turn on (compiled out): MACNEUTRON_PERF=1 asks nothing of it.
        let f = make_fixture();
        set_runtime_version("0.7.1p", &f.launcher.layout);
        f.launch(&["waitforexitandrun", LIBRARY_GAME], &perf_env(&f));
        let run = game_run(&f);
        assert_eq!(run.environment.get("DXMT_STATS"), None);
        assert_eq!(run.environment.get("DXMT_DXIL_DUMP"), None);
    }

    #[test]
    fn perf_clears_the_last_runs_files() {
        // Left on, MACNEUTRON_PERF=1 would pile up stats.txt and the dump (every shader and pipeline) run after run.
        let f = make_fixture();
        let folder = f.launcher.perf_folder("42");
        write("# old window", &folder.join("stats.txt"), false);
        let mut env = perf_env(&f);
        f.launch(&["waitforexitandrun", LIBRARY_GAME], &env);
        assert!(folder.exists());
        assert!(!folder.join("stats.txt").exists());
        // A folder the user named is theirs: left alone.
        let theirs = folder.parent().unwrap().join("theirs");
        write("kept", &theirs.join("stats.txt"), false);
        env.insert("DXMT_DXIL_DUMP".into(), theirs.to_string_lossy().into_owned());
        f.launch(&["waitforexitandrun", LIBRARY_GAME], &env);
        assert!(theirs.join("stats.txt").exists());
    }

    #[test]
    fn perf_says_why_there_will_be_no_frame_statistics() {
        // perf-compare.sh reads the perf folder's stats.txt: when the user's own settings or the backend keep DXMT from
        // writing it, the launcher log says so, since the record's fps comes out empty.
        for (setting, value, said) in [
            ("MACNEUTRON_GRAPHICS", "wined3d", "needs the DXMT backend"),
            ("DXMT_STATS", "0", "DXMT_STATS is set"),
            ("DXMT_DXIL_DUMP", "/elsewhere", "DXMT_DXIL_DUMP is set"),
        ] {
            let f = make_fixture();
            f.launch(&["waitforexitandrun", LIBRARY_GAME], &with(&perf_env(&f), &[(setting, value)]));
            assert!(
                f.launcher_log().contains(&format!("MACNEUTRON_PERF=1: no frame statistics in the perf folder: {said}")),
                "{setting}"
            );
        }
        // Nothing in the way: no note.
        let f = make_fixture();
        f.launch(&["waitforexitandrun", LIBRARY_GAME], &perf_env(&f));
        assert!(!f.launcher_log().contains("MACNEUTRON_PERF=1"));
    }

    /// MicroVMTests.swift's tests that go through the launcher.
    mod vm {
        use super::*;
        use crate::micro_vm::UserShareError;
        use crate::test_support::pe_bytes_icons;

        /// A VM folder as vm/host/build.sh lays it out: an executable macneutron-vm, run-guest.sh and run-game.sh.
        fn make_vm_folder() -> PathBuf {
            let root = make_temp_dir().join("vm");
            write("#!/bin/sh\n", &root.join("macneutron-vm"), true);
            write("#!/bin/sh\n", &root.join("run-guest.sh"), true);
            write("# run-game.sh\nwine steam.exe \"$EXE\" \"$@\"\n", &root.join("run-game.sh"), false);
            write("dev\n", &root.join("images/manifest.sha256"), false);
            root
        }

        /// Written whole, as Swift's `atomically: true`: the launcher's stage poll never reads half a line.
        fn write_atomically(text: &str, to: &Path) {
            let partial = to.with_extension("partial");
            write(text, &partial, false);
            std::fs::rename(&partial, to).unwrap();
        }

        /// The guest scripts run-guest.sh was given, read while it "runs" (the launcher removes them after).
        #[derive(Default, Clone)]
        struct Scripts(Arc<Mutex<Vec<String>>>);

        impl Scripts {
            fn add(&self, path: Option<&String>) {
                if let Some(text) = path.and_then(|p| std::fs::read_to_string(p).ok()) {
                    self.0.lock().unwrap().push(text);
                }
            }

            fn all(&self) -> Vec<String> {
                self.0.lock().unwrap().clone()
            }

            fn first(&self) -> String {
                self.all().first().cloned().unwrap_or_default()
            }

            fn last(&self) -> String {
                self.all().last().cloned().unwrap_or_default()
            }
        }

        struct Options {
            vm_installed: bool,
            status: i32,
            display: Option<(usize, usize)>,
            version: Option<&'static str>,
            console: Option<&'static str>,
            mounts_failed: Option<&'static str>,
            install_log: Option<&'static str>,
            crash: Option<&'static str>,
            stages: Vec<&'static str>,
            steam: Option<SteamLocation>,
        }

        impl Default for Options {
            fn default() -> Self {
                Options {
                    vm_installed: true,
                    status: 0,
                    display: None,
                    version: None,
                    console: None,
                    mounts_failed: None,
                    install_log: None,
                    crash: None,
                    stages: Vec::new(),
                    steam: None,
                }
            }
        }

        struct VMFixture {
            launcher: Launcher,
            runner: Arc<FakeRunner>,
            notifier: Arc<RecordingNotifier>,
            vm: PathBuf,
            env: BTreeMap<String, String>,
            /// The launcher's home (its Documents/ made) and the game's compatdata folder.
            home: String,
            data_path: PathBuf,
            scripts: Scripts,
        }

        impl VMFixture {
            fn launch(&self, argv: &[&str], env: &BTreeMap<String, String>) -> i32 {
                self.launcher.launch(&args(argv), env)
            }

            fn launcher_log(&self) -> String {
                read(&self.launcher.log.launcher_log())
            }

            fn calls(&self) -> Vec<Call> {
                self.runner.calls()
            }

            fn first(&self) -> Call {
                self.calls().first().cloned().unwrap()
            }

            fn last(&self) -> Call {
                self.calls().last().cloned().unwrap()
            }

            fn guest_call(&self) -> Call {
                self.calls().into_iter().rev().find(|c| c.tool == "run-guest.sh").unwrap()
            }

            fn profile(&self) -> PathBuf {
                self.data_path.join("pfx/drive_c/users/steamuser")
            }

            fn share(&self) -> PathBuf {
                self.vm.join("runs/2437170")
            }
        }

        fn make_vm_fixture(o: Options) -> VMFixture {
            let vm = make_vm_folder();
            let layout = make_tool_layout();
            if let Some(version) = o.version {
                set_runtime_version(version, &layout);
            }
            let scripts = Scripts::default();
            let seen = scripts.clone();
            let (status, stages) = (o.status, o.stages.clone());
            let (console, mounts_failed, install_log, crash) = (o.console, o.mounts_failed, o.install_log, o.crash);
            let runner = Arc::new(FakeRunner::new(move |call| {
                if call.tool != "run-guest.sh" {
                    return 0;
                }
                seen.add(call.arguments.first());
                let Some(share) = call.environment.get("MN_SHARE").map(PathBuf::from) else { return status };
                if let Some(console) = console {
                    write(console, &share.join("console.log"), false); // macneutron-vm's guest console
                }
                // mn-runner's word on a mount that failed, or run-game.sh's on a profile that isn't mounted
                if let Some(reason) = mounts_failed {
                    write(reason, &share.join("traces/mounts.failed"), false);
                }
                if let Some(log) = install_log {
                    write(log, &share.join("traces/installscript.log"), false); // the install-script steps it ran
                }
                // run-game.sh's crash reporter: its marker, the reporter's ps, and the copy of the game's report
                if let Some(crash) = crash {
                    let details = share.join("traces/crash-report");
                    write(crash, &share.join("traces/crash"), false);
                    write("CrashReportClie 1234\n", &details.join("ps.txt"), false);
                    write("report\n", &details.join("report/Crash/context.txt"), false);
                }
                // run-game.sh's stages as it goes, each long enough for the launcher's poll to see it
                for stage in &stages {
                    write_atomically(stage, &share.join("traces/stage"));
                    std::thread::sleep(Duration::from_secs(1));
                }
                status
            }));
            let notifier = Arc::new(RecordingNotifier::default());
            let home = make_temp_dir().join("home");
            std::fs::create_dir_all(home.join("Documents")).unwrap();
            let home = home.to_string_lossy().into_owned();
            let root = if o.vm_installed { vm.clone() } else { make_temp_dir().join("no-vm") };
            let mut launcher = test_launcher(layout, runner.clone(), notifier.clone(), Some(TEST_IDENTITY), root);
            let display = o.display;
            launcher.display = Box::new(move || display);
            launcher.home = home.clone();
            if let Some(steam) = o.steam {
                launcher.steam = steam;
            }
            let data_path = make_temp_dir().join("compatdata/2437170");
            let env = steam_environment(&data_path, "2437170");
            VMFixture { launcher, runner, notifier, vm, env, home, data_path, scripts }
        }

        fn fixture() -> VMFixture {
            make_vm_fixture(Options::default())
        }

        const GAME: &str =
            "/Users/someone/Library/Application Support/Steam/steamapps/common/SMITE 2/Windows/Hemingway.exe";

        fn exported(script: &str) -> Vec<String> {
            let line = script.lines().find(|l| l.starts_with("export ")).unwrap_or("");
            line.split(' ').map(str::to_string).collect()
        }

        #[test]
        fn every_game_runs_in_the_micro_vm() {
            let f = make_vm_fixture(Options { status: 3, ..Default::default() });
            assert_eq!(f.launch(&["waitforexitandrun", GAME, "-homedir=SMITE2Alpha", "-d3d12"], &f.env), 3);
            let call = f.first();
            assert_eq!(f.calls().len(), 1); // no host prefix, no host Wine
            assert_eq!(call.tool, "run-guest.sh");
            let profile = f.profile().to_string_lossy().into_owned();
            assert_eq!(
                call.arguments[1..],
                [
                    "--steam-appid".to_string(),
                    "2437170".into(),
                    "--ram-mib".into(),
                    "16384".into(),
                    "--steam-map".into(),
                    "/Users/someone/Library/Application Support/Steam/steamapps/common=/mnt/games".into(),
                    "--user-share".into(),
                    format!("profile={profile}"),
                    "--user-share".into(),
                    format!("documents={}/Documents", f.home),
                    "--user-share".into(),
                    "game=/Users/someone/Library/Application Support/Steam/steamapps/common/SMITE 2".into(),
                ]
            );
            assert_eq!(call.environment["MN_VM_DIR"], directory_path(&f.vm));
            assert_eq!(call.environment["MN_STATE"], f.vm.join("state/2437170.img").to_string_lossy());
            assert_eq!(call.environment["MN_SHARE"], f.share().to_string_lossy());
            // The toast macneutron-vm shows while macOS asks for the TPM key's password.
            assert_eq!(call.environment["MN_TOAST"], f.launcher.layout.launcher_binary().to_string_lossy());
            assert!(f.launcher_log().contains("verb=waitforexitandrun appid=2437170 backend=vm"));
            assert!(f.launcher_log().contains("exit=3"));
        }

        #[test]
        fn a_crashed_vm_game_keeps_its_report_and_says_where_it_is() {
            // run-game.sh ends a VM whose game left a crash reporter waiting on the invisible Xvfb. The launcher keeps
            // the report beside its logs, says where, and Steam sees a failure even if the guest's status came back 0.
            let marker = "when=2026-10-25T21:00:00Z\nreporter=CrashReportClie\ncommand=C:\\Game\\CrashReportClient.exe\n\
                          report=/mnt/games/SMITE 2/Saved/Crashes/ABC";
            let f = make_vm_fixture(Options { crash: Some(marker), ..Default::default() });
            assert_eq!(f.launch(&["waitforexitandrun", GAME], &f.env), 1);
            assert_eq!(f.notifier.posted().len(), 1);
            let message = f.notifier.posted()[0].clone();
            assert!(message.contains("SMITE 2 crashed"));
            assert!(message.contains("crash report is in"));
            let crashes = f.launcher.log.directory.join("crashes/2437170");
            let folders: Vec<_> = std::fs::read_dir(&crashes).unwrap().map(|e| e.unwrap().file_name()).collect();
            assert_eq!(folders.len(), 1);
            let kept = crashes.join(&folders[0]);
            assert!(kept.join("report/ps.txt").exists());
            assert!(message.contains(&*kept.to_string_lossy()));
            assert!(f.launcher_log().contains("crash: appid=2437170 reporter=CrashReportClie"));
            assert!(f
                .launcher_log()
                .contains("reports=/Users/someone/Library/Application Support/Steam/steamapps/common/SMITE 2/Saved/Crashes/ABC"));
        }

        #[test]
        fn an_earlier_runs_crash_marker_is_not_reported_again() {
            let f = fixture();
            let traces = f.share().join("traces");
            write("reporter=OldCrash\n", &traces.join("crash"), false);
            assert_eq!(f.launch(&["waitforexitandrun", GAME], &f.env), 0);
            assert!(f.notifier.posted().is_empty());
            assert!(!traces.join("crash").exists());
        }

        // A game in the VM uses the saves Steam Cloud syncs (spec 2026-10-23-macneutron-vm-shared-saves): its Mac
        // prefix's profile, ~/Documents and its own folder, shared read-write.

        #[test]
        fn a_vm_game_gets_its_profile_documents_and_folder() {
            let f = fixture();
            f.launch(&["waitforexitandrun", GAME], &f.env);
            let profile = f.profile().to_string_lossy().into_owned();
            // after the script, appid, RAM and --steam-map pairs
            assert_eq!(
                f.first().arguments[7..],
                [
                    "--user-share".to_string(),
                    format!("profile={profile}"),
                    "--user-share".into(),
                    format!("documents={}/Documents", f.home),
                    "--user-share".into(),
                    "game=/Users/someone/Library/Application Support/Steam/steamapps/common/SMITE 2".into(),
                ]
            );
            assert_eq!(
                read(&f.share().join("mounts")),
                format!(
                    "profile /home/mn/pfx/drive_c/users/steamuser\ndocuments {}/Documents\ngame /mnt/games/SMITE 2\n",
                    f.home
                )
            );
            assert!(f.scripts.first().contains(&format!("MN_MAC_HOME='{}'", f.home)));
            assert!(f.profile().exists()); // made: Steam's and Proton's layout
        }

        /// The `profile=` share run-guest.sh was given.
        fn profile_share(f: &VMFixture) -> Option<String> {
            f.first().arguments.iter().find_map(|a| a.strip_prefix("profile=").map(str::to_string))
        }

        #[test]
        fn a_vm_only_profile_gets_steams_xp_name_links() {
            // Steam's Auto-Cloud roots are the profile's XP names (ProfileLinks), which only a macOS launch made
            // before: a game never run on macOS kept its saves where Steam doesn't look.
            let f = fixture();
            f.launch(&["waitforexitandrun", GAME], &f.env);
            let link = |name: &str| std::fs::read_link(f.profile().join(name)).unwrap();
            assert_eq!(link("Local Settings/Application Data"), PathBuf::from("../AppData/Local"));
            assert_eq!(link("Application Data"), PathBuf::from("AppData/Roaming"));
            assert_eq!(link("My Documents"), PathBuf::from("Documents"));
        }

        #[test]
        fn cloud_files_steam_put_in_an_xp_folder_reach_the_game() {
            // Steam's launch download makes real XP-name folders in a profile no macOS launch prepared.
            let f = fixture();
            write("cloud\n", &f.profile().join("Local Settings/Application Data/Game/save.sav"), false);
            f.launch(&["waitforexitandrun", GAME], &f.env);
            assert_eq!(read(&f.profile().join("AppData/Local/Game/save.sav")), "cloud\n");
            assert_eq!(
                std::fs::read_link(f.profile().join("Local Settings/Application Data")).unwrap(),
                PathBuf::from("../AppData/Local")
            );
        }

        #[test]
        fn a_linked_profile_is_shared_as_its_target() {
            // macOS Wine's profile is named after the Mac user; users/steamuser links to it (ProfileLinks).
            let f = fixture();
            let users = f.data_path.join("pfx/drive_c/users");
            std::fs::create_dir_all(users.join("chad")).unwrap();
            std::os::unix::fs::symlink("chad", users.join("steamuser")).unwrap();
            f.launch(&["waitforexitandrun", GAME], &f.env);
            assert_eq!(profile_share(&f), Some(users.join("chad").to_string_lossy().into_owned()));
        }

        #[test]
        fn a_dangling_profile_link_gets_its_folder_back() {
            let f = fixture();
            let users = f.data_path.join("pfx/drive_c/users");
            std::fs::create_dir_all(&users).unwrap();
            std::os::unix::fs::symlink("chad", users.join("steamuser")).unwrap();
            f.launch(&["waitforexitandrun", GAME], &f.env);
            assert_eq!(std::fs::read_link(users.join("steamuser")).unwrap(), PathBuf::from("chad")); // the link stays
            assert!(std::fs::symlink_metadata(users.join("chad")).unwrap().is_dir());
            assert_eq!(profile_share(&f), Some(users.join("chad").to_string_lossy().into_owned()));
        }

        #[test]
        fn a_game_folder_with_an_apostrophe_stays_one_path() {
            let f = fixture();
            let bg3 = "/Users/someone/Library/Application Support/Steam/steamapps/common/Baldur's Gate 3/bin/bg3.exe";
            f.launch(&["waitforexitandrun", bg3], &f.env);
            assert!(f
                .first()
                .arguments
                .contains(&"game=/Users/someone/Library/Application Support/Steam/steamapps/common/Baldur's Gate 3".into()));
            assert!(read(&f.share().join("mounts")).ends_with("game /mnt/games/Baldur's Gate 3\n"));
        }

        #[test]
        fn a_library_on_another_drive_shares_that_librarys_game() {
            let f = fixture();
            f.launch(&["waitforexitandrun", "/Volumes/Games/SteamLibrary/steamapps/common/X/X.exe"], &f.env);
            assert!(f.first().arguments.contains(&"game=/Volumes/Games/SteamLibrary/steamapps/common/X".into()));
        }

        #[test]
        fn documents_that_cant_be_listed_refuse_the_launch() {
            // macOS's privacy settings refused Steam the Documents folder: never a run without it.
            use std::os::unix::fs::PermissionsExt;
            let f = fixture();
            let documents = format!("{}/Documents", f.home);
            std::fs::set_permissions(&documents, std::fs::Permissions::from_mode(0)).unwrap();
            let _restore = Defer(|| {
                let _ = std::fs::set_permissions(&documents, std::fs::Permissions::from_mode(0o755));
            });
            assert_ne!(f.launch(&["waitforexitandrun", GAME], &f.env), 0);
            assert!(f.calls().is_empty());
            assert!(f.notifier.posted().contains(&UserShareError::DocumentsUnreadable(documents.clone()).to_string()));
        }

        #[test]
        fn a_failed_mount_is_told_to_the_player() {
            let f = make_vm_fixture(Options { status: 1, mounts_failed: Some("no virtiofs tag game\n"), ..Default::default() });
            f.launch(&["waitforexitandrun", GAME], &f.env);
            let message = "MacNeutron couldn't share this game's saves with the game VM: no virtiofs tag game";
            assert!(f.notifier.posted().contains(&message.to_string()));
            assert!(f.launcher_log().contains(message));
        }

        #[test]
        fn an_old_mounts_failed_does_not_refuse_the_next_run() {
            let f = fixture();
            write("an earlier run's\n", &f.share().join("traces/mounts.failed"), false);
            assert_eq!(f.launch(&["waitforexitandrun", GAME], &f.env), 0);
            assert!(!f.notifier.posted().iter().any(|m| m.contains("couldn't share")));
        }

        /// A 32-bit PE in a Steam library (the VM's game folder).
        fn i386_game_in_library(name: &str) -> String {
            let path = make_temp_dir().join(format!("steamapps/common/Old Game/{name}"));
            std::fs::create_dir_all(path.parent().unwrap()).unwrap();
            std::fs::write(&path, pe_bytes(pe_image::I386, false)).unwrap();
            path.to_string_lossy().into_owned()
        }

        #[test]
        fn a_32bit_game_runs_in_the_micro_vm() {
            let f = fixture();
            assert_eq!(f.launch(&["waitforexitandrun", &i386_game_in_library("Game.exe")], &f.env), 0);
            assert_eq!(f.runner.tools(), ["run-guest.sh"]);
            assert!(f.notifier.posted().is_empty());
        }

        #[test]
        fn a_32bit_installer_runs_in_the_micro_vm() {
            // Proton runs a game's 32-bit redistributable installers; in the VM they run too.
            let f = fixture();
            assert_eq!(f.launch(&["run", &i386_game_in_library("Setup.exe"), "/quiet"], &f.env), 0);
            assert_eq!(f.runner.tools(), ["run-guest.sh"]);
            assert!(!f.launcher_log().contains("skipped 32-bit installer"));
        }

        #[test]
        fn a_32bit_game_waits_for_the_vm_like_any_other() {
            // Before the app's setup finishes a 32-bit game gets the VM's message, not the macOS refusal.
            let f = make_vm_fixture(Options { vm_installed: false, ..Default::default() });
            assert_eq!(f.launch(&["waitforexitandrun", &i386_game_in_library("Game.exe")], &f.env), 1);
            assert!(f.calls().is_empty());
            assert_eq!(f.notifier.posted().len(), 1);
            assert!(f.notifier.posted()[0].contains("game VM isn't ready"));
        }

        #[test]
        fn a_32bit_game_opted_out_of_the_vm_is_refused() {
            let f = fixture();
            let env = with(&f.env, &[("MACNEUTRON_VM", "0")]);
            assert_eq!(f.launch(&["waitforexitandrun", &i386_game_in_library("Game.exe")], &env), 1);
            assert!(f.calls().is_empty());
            assert_eq!(f.notifier.posted(), [THIRTY_TWO_BIT_TEXT]);
        }

        #[test]
        fn the_guest_script_carries_the_launch() {
            let f = fixture();
            let env = with(
                &f.env,
                &[
                    ("MACNEUTRON_DLSS", "1"),
                    ("MACNEUTRON_VM_D3D", "forward"),
                    ("MACNEUTRON_VM_D3D32", "forward"),
                    ("MACNEUTRON_VM_WOW64", "1"),
                    ("PROTON_USE_WOW64", "1"),
                    ("MTLD3D_CONFIG", "debug.capsAll=true"),
                ],
            );
            f.launch(&["waitforexitandrun", GAME, "-homedir=SMITE2Alpha", "it's quoted", ""], &env);
            let script = f.scripts.first();
            assert!(!Path::new(&f.first().arguments[0]).exists());
            assert!(script.contains("APPID='2437170'\n"));
            assert!(script.contains("GAME_DIR='/mnt/games/SMITE 2/Windows'\n"));
            assert!(script.contains("EXE='Z:\\mnt\\games\\SMITE 2\\Windows\\Hemingway.exe'\n"));
            assert!(script.contains("set -- '-homedir=SMITE2Alpha' 'it'\\''s quoted' ''\n"));
            for line in [
                "MACNEUTRON_DLSS='1'\n",
                "MACNEUTRON_VM_D3D='forward'\n",
                "MACNEUTRON_VM_D3D32='forward'\n",
                "MACNEUTRON_VM_WOW64='1'\n",
                "PROTON_USE_WOW64='1'\n",
                "MTLD3D_CONFIG='debug.capsAll=true'\n",
            ] {
                assert!(script.contains(line), "{line}");
            }
            // Exported, so Wine's processes get them (a forwarder trace, guest DXMT's NVIDIA extension or log level)
            let exported = exported(&script);
            for key in ["MACNEUTRON_DLSS", "MACNEUTRON_VM_D3D", "MTLD3D_CONFIG"] {
                assert!(exported.iter().any(|k| k == key), "{key}");
            }
            assert!(script.ends_with("# run-game.sh\nwine steam.exe \"$EXE\" \"$@\"\n"));
        }

        #[test]
        fn every_launch_shows_a_toast_until_the_games_window_shows() {
            let f = fixture();
            let window = f.share().join("traces/window");
            write("", &window, false); // the last run's
            let window_text = window.to_string_lossy().into_owned();
            f.launch(&["waitforexitandrun", GAME], &f.env);
            assert_eq!(f.notifier.toasts(), [format!("Setting up SMITE 2 until {window_text}")]);
            assert_eq!(f.notifier.toasts_closed(), 1);
            assert!(!window.exists());
            let call = f.guest_call();
            assert_eq!(call.environment["MN_WINDOW_SHOWN"], window_text); // macneutron-vm's word
            assert_eq!(call.environment["MN_APP_NAME"], "SMITE 2");
            assert_eq!(call.environment.get("MN_APP_ICON"), None); // no such exe here
            write("", &f.vm.join("state/2437170.img"), false); // run-guest.sh made it
            f.launch(&["waitforexitandrun", GAME], &f.env);
            assert_eq!(
                f.notifier.toasts(),
                [format!("Setting up SMITE 2 until {window_text}"), format!("Starting SMITE 2 until {window_text}")]
            );
            assert_eq!(f.notifier.toasts_closed(), 2);
        }

        #[test]
        fn the_guests_stages_are_the_toasts_message() {
            let f = make_vm_fixture(Options {
                stages: vec!["Updating its Windows environment\n", "Loading the game\n"],
                ..Default::default()
            });
            f.launch(&["waitforexitandrun", GAME], &f.env);
            assert_eq!(f.notifier.toast_updates(), ["Updating its Windows environment", "Loading the game"]);
        }

        #[test]
        fn the_games_exe_icon_goes_to_the_vms_window() {
            let f = fixture();
            let exe = make_temp_dir().join("steamapps/common/Some Game/Game.exe");
            std::fs::create_dir_all(exe.parent().unwrap()).unwrap();
            std::fs::write(&exe, pe_bytes_icons(&[(0, 32, vec![0x89, 0x50, 0x4E, 0x47])])).unwrap();
            f.launch(&["waitforexitandrun", &exe.to_string_lossy()], &f.env);
            let call = f.guest_call();
            assert_eq!(call.environment["MN_APP_NAME"], "Some Game");
            let icon = &call.environment["MN_APP_ICON"];
            assert_eq!(std::fs::read(icon).ok(), pe_image::icon(&exe));
        }

        #[test]
        fn the_eac_runtime_is_mounted_read_only_in_the_games_vm() {
            // Linux Steam passes the Proton EasyAntiCheat Runtime to every game under Proton; Mac Steam leaves it in a
            // library, or Steam's console downloads it (Mac Steam doesn't install the Linux-only tool).
            let steam_root = make_temp_dir().join("Steam");
            let places = [
                "steamapps/common/Proton EasyAntiCheat Runtime",
                "steamapps/content/app_1826330/depot_1826331",
                "Steam.AppBundle/Steam/Contents/MacOS/steamapps/content/app_1826330/depot_1826331",
            ];
            for place in places {
                let f = make_vm_fixture(Options { steam: Some(SteamLocation::new(steam_root.clone())), ..Default::default() });
                let runtime = steam_root.join(place);
                std::fs::create_dir_all(&runtime).unwrap(); // no v2/: not one
                f.launch(&["waitforexitandrun", GAME], &f.env);
                assert!(!f.last().arguments.contains(&"--ro-share".to_string()), "{place}");
                assert!(!f.scripts.last().contains("PROTON_EAC_RUNTIME"), "{place}");
                std::fs::create_dir_all(runtime.join("v2")).unwrap();
                f.launch(&["waitforexitandrun", GAME], &f.env);
                let call = f.guest_call();
                let at = call.arguments.iter().position(|a| a == "--ro-share").unwrap();
                assert_eq!(call.arguments[at + 1], format!("eac-runtime={}", runtime.to_string_lossy()));
                assert!(read(&f.share().join("mounts")).ends_with("eac-runtime /Volumes/Proton EasyAntiCheat Runtime\n"));
                let script = f.scripts.last();
                assert!(script.contains("PROTON_EAC_RUNTIME='/Volumes/Proton EasyAntiCheat Runtime'\n"));
                assert!(exported(&script).iter().any(|k| k == "PROTON_EAC_RUNTIME")); // Wine's processes get it
                std::fs::remove_dir_all(&runtime).unwrap();
            }
        }

        #[test]
        fn a_tpm_that_does_not_start_is_told_to_the_player() {
            // macneutron-vm's 7, with no run.status: its TPM refused to start, so the guest never ran.
            let f = make_vm_fixture(Options { status: 7, ..Default::default() });
            f.launch(&["waitforexitandrun", GAME], &f.env);
            assert!(f.notifier.posted().iter().any(|m| m.contains("TPM")));
            // A game that itself ends with 7 leaves run.status: nothing to tell.
            let g = make_vm_fixture(Options { status: 7, ..Default::default() });
            write("7", &g.share().join("run.status"), false);
            g.launch(&["waitforexitandrun", GAME], &g.env);
            assert!(!g.notifier.posted().iter().any(|m| m.contains("TPM")));
        }

        /// A game in a Steam library runs in the VM or not at all: never on macOS while the setup is still going.
        fn expect_refused_until_the_vm_is_ready(f: &VMFixture) {
            assert_ne!(f.launch(&["waitforexitandrun", GAME], &f.env), 0);
            assert!(f.calls().is_empty()); // no guest, no host prefix, no host Wine
            assert!(f.notifier.posted().iter().any(|m| m.contains("game VM isn't ready")));
            assert!(f.launcher_log().contains("game VM isn't ready"));
        }

        #[test]
        fn a_game_before_the_vm_setup_finishes_is_refused() {
            let f = fixture();
            std::fs::remove_file(f.vm.join("images/manifest.sha256")).unwrap();
            expect_refused_until_the_vm_is_ready(&f);
        }

        #[test]
        fn opting_out_runs_on_macos() {
            let f = fixture();
            f.launch(&["waitforexitandrun", GAME], &with(&f.env, &[("MACNEUTRON_VM", "0")]));
            assert!(!f.calls().iter().any(|c| c.tool == "run-guest.sh"));
            assert!(f.calls().iter().any(|c| c.tool == "wine" && c.arguments.first().map(String::as_str) == Some(GAME)));
        }

        #[test]
        fn a_game_before_the_vm_tools_are_installed_is_refused() {
            expect_refused_until_the_vm_is_ready(&make_vm_fixture(Options { vm_installed: false, ..Default::default() }));
        }

        #[test]
        fn a_game_while_the_vm_tools_swap_is_refused() {
            let f = fixture();
            write("", &f.vm.join(".installing"), false);
            expect_refused_until_the_vm_is_ready(&f);
        }

        #[test]
        fn path_queries_and_games_outside_a_library_stay_on_macos() {
            let f = fixture();
            f.launch(&["getcompatpath", GAME], &f.env);
            f.launch(&["waitforexitandrun", "/Applications/Other/Game.exe"], &f.env);
            assert!(!f.calls().iter().any(|c| c.tool == "run-guest.sh"));
            assert!(f.launcher_log().contains("not in a Steam library"));
        }

        #[test]
        fn forwarding_keeps_native_dxmts_state_with_the_game() {
            // Forwarded, native DXMT runs in macneutron-vm: its shader cache and recordings in the game's VM state
            // folder, and the game's launch options in its environment (M3 Task 8).
            let f = fixture();
            f.launch(&["waitforexitandrun", GAME], &with(&f.env, &[("MACNEUTRON_VM_D3D", "forward"), ("DXMT_D3D12_SM6", "0")]));
            let call = f.last();
            assert_eq!(call.environment["MN_D3D_STATE"], f.vm.join("state/2437170.dxmt").to_string_lossy());
            assert_eq!(call.environment["DXMT_D3D12_SM6"], "0");
            f.launch(&["waitforexitandrun", GAME], &with(&f.env, &[("MACNEUTRON_VM_D3D", "guest")]));
            assert_eq!(f.last().environment.get("MN_D3D_STATE"), None);
        }

        // Forwarding is the VM's default since M5 passed (2026-10-19, the D3D forwarding spec §8): native DXMT on the
        // Mac draws for every game; MACNEUTRON_VM_D3D=guest keeps DXMT in the guest.
        #[test]
        fn vm_games_forward_direct3d_by_default() {
            let f = fixture();
            f.launch(&["waitforexitandrun", GAME], &f.env);
            assert!(f.scripts.last().contains("\nMACNEUTRON_VM_D3D='forward'\n"));
            assert_eq!(f.last().environment["MACNEUTRON_VM_D3D"], "forward");
            assert!(f.last().environment.contains_key("MN_D3D_STATE"));
            f.launch(&["waitforexitandrun", GAME], &with(&f.env, &[("MACNEUTRON_VM_D3D", "guest")]));
            assert!(f.scripts.last().contains("\nMACNEUTRON_VM_D3D='guest'\n"));
            assert_eq!(f.last().environment.get("MN_D3D_STATE"), None);
        }

        #[test]
        fn the_vms_precache_replays_natively_before_the_boot() {
            let f = fixture();
            let pipelines = f.vm.join("state/2437170.dxmt/pipelines");
            write("recorded\n", &pipelines.join("game.pipelines"), false);
            write("an older build\n", &pipelines.join("replayed"), false);
            f.launch(&["waitforexitandrun", GAME], &with(&f.env, &[("MACNEUTRON_VM_D3D", "forward")]));
            assert_eq!(f.runner.tools(), ["dxmt-replay", "run-guest.sh"]);
            let replay = f.first();
            assert_eq!(replay.arguments, [pipelines.join("game.pipelines").to_string_lossy().into_owned()]);
            assert_eq!(
                replay.environment["DXMT_SHADER_CACHE_PATH"],
                format!("{}/", f.vm.join("state/2437170.dxmt").to_string_lossy())
            );
            assert_eq!(replay.environment.get("DXMT_PIPELINE_RECORD"), None);
            assert_ne!(read(&pipelines.join("replayed")), "an older build\n");
            assert!(f.notifier.toasts().iter().any(|t| t.contains("shaders")));
        }

        #[test]
        fn the_vms_precache_is_stamped_with_native_dxmts_key() {
            // The VM's recordings are native DXMT's: its translator key (dxmt-translator in the VM folder), not the
            // macOS runtime's, which a dev VM folder needn't match (M3's deferred minors).
            let f = fixture();
            write("native-key\n", &f.vm.join("dxmt-translator"), false);
            let pipelines = f.vm.join("state/2437170.dxmt/pipelines");
            write("recorded\n", &pipelines.join("game.pipelines"), false);
            write("an older build\n", &pipelines.join("replayed"), false);
            f.launch(&["waitforexitandrun", GAME], &with(&f.env, &[("MACNEUTRON_VM_D3D", "forward")]));
            assert!(read(&pipelines.join("replayed")).starts_with("native-key "));
        }

        #[test]
        fn dlss_turns_on_native_dxmts_nvidia_extension() {
            // Forwarded, native DXMT in macneutron-vm reads its own environment: DLSS's NVIDIA extension too (M3 Task 6).
            let f = fixture();
            f.launch(&["waitforexitandrun", GAME], &with(&f.env, &[("MACNEUTRON_DLSS", "1")]));
            assert_eq!(f.last().environment["DXMT_ENABLE_NVEXT"], "1");
            let g = fixture();
            g.launch(&["waitforexitandrun", GAME], &g.env);
            assert_eq!(g.last().environment.get("DXMT_ENABLE_NVEXT"), None);
        }

        #[test]
        fn vm_dir_from_the_environment() {
            let other = make_vm_folder();
            let f = make_vm_fixture(Options { vm_installed: false, ..Default::default() });
            f.launch(&["waitforexitandrun", GAME], &with(&f.env, &[("MACNEUTRON_VM_DIR", &other.to_string_lossy())]));
            assert_eq!(f.first().tool, "run-guest.sh");
            assert_eq!(f.first().environment["MN_VM_DIR"], directory_path(&other));
        }

        #[test]
        fn the_guests_screen_is_the_macs_display() {
            // Its pixels: the guest's X screen and the most a game is offered; the host window takes its shape.
            let f = make_vm_fixture(Options { display: Some((3456, 2234)), ..Default::default() });
            f.launch(&["waitforexitandrun", GAME], &f.env);
            assert!(f.scripts.last().contains("\nMN_SCREEN='3456x2234'\n"));
            assert_eq!(f.last().environment["MN_SCREEN"], "3456x2234");
            // A launch option's size wins; one that isn't WxH doesn't.
            f.launch(&["waitforexitandrun", GAME], &with(&f.env, &[("MN_SCREEN", "2560x1600")]));
            assert!(f.scripts.last().contains("\nMN_SCREEN='2560x1600'\n"));
            f.launch(&["waitforexitandrun", GAME], &with(&f.env, &[("MN_SCREEN", "big; reboot")]));
            assert!(f.scripts.last().contains("\nMN_SCREEN='3456x2234'\n"));
            assert_eq!(f.last().environment["MN_SCREEN"], "3456x2234");
        }

        #[test]
        fn without_a_display_the_guest_keeps_its_own_screen() {
            let f = fixture();
            f.launch(&["waitforexitandrun", GAME], &f.env);
            assert!(!f.scripts.last().contains("MN_SCREEN")); // run-game.sh's 1920x1080
        }

        #[test]
        fn perf_turns_on_the_guest_profiler() {
            let f = fixture();
            f.launch(&["waitforexitandrun", GAME], &f.env);
            assert!(!f.scripts.last().contains("MACNEUTRON_VM_PROFILE"));
            assert!(!f.scripts.last().contains("DXMT_STATS"));
            f.launch(&["waitforexitandrun", GAME], &with(&f.env, &[("MACNEUTRON_PERF", "1")]));
            assert!(f.scripts.last().contains("\nMACNEUTRON_VM_PROFILE='1'\n"));
            // Frames are counted on the host: DXMT_STATS's timing would cost the guest a clock read under FEX per D3D12
            // call.
            assert!(!f.scripts.last().contains("DXMT_STATS"));
            assert_eq!(f.last().environment["MN_PRESENT_STATS"], f.share().join("traces/frames.txt").to_string_lossy());
        }

        /// run-guest.sh as a real process group, a shell and its child (as lockf, the guest's script and macneutron-vm
        /// are).
        struct GroupRunner;

        impl ProcessRunner for GroupRunner {
            fn run(
                &self,
                executable: &Path,
                arguments: &[String],
                environment: &BTreeMap<String, String>,
                output: Option<&Path>,
            ) -> std::io::Result<i32> {
                self.run_with_started(executable, arguments, environment, output, &mut |_| {})
            }

            fn run_with_started(
                &self,
                executable: &Path,
                _arguments: &[String],
                _environment: &BTreeMap<String, String>,
                _output: Option<&Path>,
                started: &mut dyn FnMut(i32),
            ) -> std::io::Result<i32> {
                if executable.file_name().is_none_or(|n| n != "run-guest.sh") {
                    return Ok(0);
                }
                SystemProcessRunner.run_with_started(
                    Path::new("/bin/sh"),
                    &args(&["-c", "sleep 60 & wait"]),
                    &BTreeMap::new(),
                    None,
                    started,
                )
            }
        }

        // Steam's Stop on macOS asks the processes it tracks to quit, the launcher among them: a VM launch's VM ends,
        // the whole run-guest.sh group, and the launch returns.
        #[test]
        fn steams_stop_ends_the_vm() {
            let home = make_temp_dir().join("home");
            std::fs::create_dir_all(home.join("Documents")).unwrap();
            let mut launcher = test_launcher(
                make_tool_layout(),
                Arc::new(GroupRunner),
                Arc::new(RecordingNotifier::default()),
                Some(TEST_IDENTITY),
                make_vm_folder(),
            );
            launcher.home = home.to_string_lossy().into_owned();
            let launcher = Arc::new(launcher);
            let env = steam_environment(&make_temp_dir().join("compatdata/2437170"), "2437170");
            assert!(!launcher.stop_vm()); // nothing running yet
            let (sender, result) = std::sync::mpsc::channel();
            let running = launcher.clone();
            std::thread::spawn(move || {
                let _ = sender.send(running.launch(&args(&["waitforexitandrun", GAME]), &env));
            });
            let start = std::time::Instant::now();
            while !launcher.vm_run.running() && start.elapsed() < Duration::from_secs(10) {
                std::thread::sleep(Duration::from_millis(50));
            }
            assert!(launcher.vm_run.running());
            assert!(launcher.stop_vm());
            assert_eq!(result.recv_timeout(Duration::from_secs(20)), Ok(STOPPED));
        }

        #[test]
        fn a_performance_runtime_launches_quietly() {
            // X.Y.Zp without MACNEUTRON_LOG: no guest console, DXMT logs nothing, one launcher line, no game log.
            let f = make_vm_fixture(Options { version: Some("0.7.1p"), ..Default::default() });
            f.launch(&["waitforexitandrun", GAME], &f.env);
            let call = f.guest_call();
            assert_eq!(call.arguments[call.arguments.len() - 2..], ["--console".to_string(), "/dev/null".into()]);
            assert_eq!(call.environment["DXMT_LOG_LEVEL"], "none");
            let log = f.launcher_log();
            let lines: Vec<&str> = log.split('\n').filter(|l| !l.is_empty()).collect();
            assert_eq!(lines.len(), 1);
            assert!(lines[0].contains(" verb=waitforexitandrun appid=2437170 backend=vm"));
            assert!(!f.launcher.log.directory.join("steam-2437170.log").exists());
        }

        #[test]
        fn a_performance_runtime_logs_when_asked() {
            let f = make_vm_fixture(Options { version: Some("0.7.1p"), ..Default::default() });
            f.launch(&["waitforexitandrun", GAME], &with(&f.env, &[("MACNEUTRON_LOG", "1")]));
            let call = f.guest_call();
            assert!(!call.arguments.contains(&"--console".to_string()));
            assert_eq!(call.environment.get("DXMT_LOG_LEVEL"), None);
            // the VM's output is the game log
            assert_eq!(call.output, Some(f.launcher.log.directory.join("steam-2437170.log")));
        }

        #[test]
        fn a_performance_script_names_its_profile() {
            // The guest's run-game.sh keeps quiet for a p runtime; DXMT_STATS and the guest profiler aren't passed.
            let f = make_vm_fixture(Options { version: Some("0.7.1p"), ..Default::default() });
            f.launch(&["waitforexitandrun", GAME], &with(&f.env, &[("DXMT_STATS", "1"), ("MACNEUTRON_PERF", "1")]));
            let script = f.scripts.first();
            assert!(script.contains("MACNEUTRON_PROFILE='p'\n"));
            assert!(script.contains("DXMT_LOG_LEVEL='none'\n"));
            assert!(!script.contains("DXMT_STATS="));
            assert!(!script.contains("MACNEUTRON_VM_PROFILE="));
        }

        #[test]
        fn a_normal_runtime_keeps_its_console() {
            let f = fixture();
            f.launch(&["waitforexitandrun", GAME], &f.env);
            assert!(!f.guest_call().arguments.contains(&"--console".to_string()));
            assert!(!f.scripts.first().contains("MACNEUTRON_PROFILE="));
        }

        #[test]
        fn a_vm_game_gets_wine_debug_as_on_macos() {
            // LaunchEnvironment's WINEDEBUG: nothing unless logging, MACNEUTRON_LOG=1 and =2's channels, the user's own
            // wins.
            for (log, user, want) in [
                (None, None, "-all"),
                (Some("1"), None, "warn+all,warn-seh,+loaddll,warn+steamclient"),
                (Some("2"), None, "warn+all,+loaddll,+steamclient"),
                (Some("1"), Some("+relay"), "+relay"),
            ] {
                let f = make_vm_fixture(Options { version: Some("0.7.1p"), ..Default::default() });
                let mut env = f.env.clone();
                if let Some(log) = log {
                    env.insert("MACNEUTRON_LOG".into(), log.into());
                }
                if let Some(user) = user {
                    env.insert("WINEDEBUG".into(), user.into());
                }
                f.launch(&["waitforexitandrun", GAME], &env);
                assert!(f.scripts.first().contains(&format!("WINEDEBUG='{want}'\n")), "MACNEUTRON_LOG={log:?}");
            }
        }

        #[test]
        fn a_crash_reporter_override_reaches_the_guest() {
            // A reporter with another name is a launch option, so the launcher's environment must reach run-game.sh.
            let f = fixture();
            f.launch(&["waitforexitandrun", GAME], &with(&f.env, &[("MN_CRASH_REPORTERS", "mygamecrash")]));
            let script = f.scripts.first();
            assert!(script.contains("MN_CRASH_REPORTERS='mygamecrash'"));
            assert!(script.contains("export ") && script.contains(" MN_CRASH_REPORTERS"));
        }

        #[test]
        fn a_logged_vm_games_log_ends_with_the_guest_console() {
            let f = make_vm_fixture(Options { console: Some("[    0.1] Linux version\n"), ..Default::default() });
            f.launch(&["waitforexitandrun", GAME], &with(&f.env, &[("MACNEUTRON_LOG", "1")]));
            let log = read(&f.launcher.log.directory.join("steam-2437170.log"));
            assert!(log.ends_with("--- the VM's console ---\n[    0.1] Linux version\n"));
        }

        // Steam's install scripts (spec 2026-10-24-macneutron-install-scripts-design): the launcher reads them from
        // the game's app manifest and puts them in front of run-game.sh, which runs them and says how each step went.

        /// A game in a temp library (the fixture's app 2437170) whose app manifest lists `script`.
        fn game_with_install_script(script: &str) -> String {
            let steamapps = make_temp_dir().join("steamapps");
            write(
                "\"AppState\"\n{\n\"installdir\" \"Game\"\n\"InstallScripts\"\n{\n\"2437171\" \"installscript.vdf\"\n}\n}\n",
                &steamapps.join("appmanifest_2437170.acf"),
                false,
            );
            write(script, &steamapps.join("common/Game/installscript.vdf"), false);
            steamapps.join("common/Game/game.exe").to_string_lossy().into_owned()
        }

        #[test]
        fn a_games_install_scripts_reach_the_guest() {
            let f = fixture();
            let exe = game_with_install_script(
                r#""InstallScript"
{
    "Registry" { "HKEY_CURRENT_USER\\Software\\Game" { "string" { "v" "1" } } }
    "Run Process" { "setup" { "HasRunKey" "HKEY_CURRENT_USER\\Software\\Game" "process 1" "%INSTALLDIR%\\setup.exe" } }
}"#,
            );
            let registry = f.share().join("installscript.reg");
            f.launch(&["waitforexitandrun", &exe], &f.env);
            // The Registry entries go as a UTF-16 file in the run share (run-game.sh imports it when it changed).
            let data = std::fs::read(&registry).unwrap();
            assert_eq!(data[..2], [0xFF, 0xFE]);
            let units: Vec<u16> = data[2..].chunks(2).map(|c| u16::from_le_bytes([c[0], c[1]])).collect();
            assert!(String::from_utf16(&units).unwrap().contains(r#""v"="1""#));
            f.launch(&["run", &exe], &f.env);
            assert_eq!(f.scripts.all().len(), 2);
            let (game, run) = (f.scripts.first(), f.scripts.last());
            let body = game.find("# run-game.sh").unwrap();
            assert!(game.find("mn_install_script() {\n").unwrap() < body);
            assert!(game.contains(
                "  mn_run 'HKEY_CURRENT_USER\\Software\\Game' 'setup' '1' 'user.reg' 'Software\\\\Game' '\"setup\"' \
                 '/mnt/games/Game' 'Z:\\mnt\\games\\Game\\setup.exe'\n"
            ));
            // Mac Steam never sends an install script's `run`: Proton's generic verb gets none, and no earlier registry
            // file.
            assert!(!run.contains("mn_install_script"));
            assert!(!registry.exists());
        }

        #[test]
        fn install_script_results_reach_launcher_log() {
            let f = make_vm_fixture(Options { install_log: Some("x86 Update 4 exit 0\nFmSysInfo exit 1\n"), ..Default::default() });
            f.launch(&["waitforexitandrun", GAME], &f.env);
            assert!(f.launcher_log().contains("install script 2437170: x86 Update 4 exit 0\n"));
            assert!(f.launcher_log().contains("install script 2437170: FmSysInfo exit 1\n"));
        }

        #[test]
        fn a_performance_runtime_logs_only_the_install_scripts_that_failed() {
            let f = make_vm_fixture(Options {
                version: Some("0.7.1p"),
                install_log: Some("x86 Update 4 exit 0\nFmSysInfo exit 1\n"),
                ..Default::default()
            });
            f.launch(&["waitforexitandrun", GAME], &f.env);
            assert!(!f.launcher_log().contains("x86 Update 4"));
            assert!(f.launcher_log().contains("install script 2437170: FmSysInfo exit 1\n"));
        }

        #[test]
        fn an_earlier_runs_install_script_results_are_not_reported() {
            let f = fixture();
            write("FmSysInfo exit 1\n", &f.share().join("traces/installscript.log"), false);
            f.launch(&["waitforexitandrun", GAME], &f.env);
            assert!(!f.launcher_log().contains("install script"));
        }

        #[test]
        fn install_script_notes_reach_launcher_log() {
            let f = fixture();
            let exe = game_with_install_script(r#""InstallScript" { "Firewall" { "Game" "%INSTALLDIR%\\game.exe" } }"#);
            f.launch(&["waitforexitandrun", &exe], &f.env);
            assert!(f.launcher_log().split('\n').any(|l| l.contains("install script 2437170: ") && l.contains("Firewall")));
        }

        // Where a game ran from, for the app's cleanup of uninstalled games' drives (UninstalledDrives): a drive whose
        // library Steam's list no longer names is kept.
        #[test]
        fn a_vm_run_records_its_library() {
            let f = fixture();
            f.launch(&["waitforexitandrun", GAME], &f.env);
            assert_eq!(read(&f.share().join("library")), "/Users/someone/Library/Application Support/Steam/steamapps");
        }

        // An installer that waited for a click nobody could give costs the timeout at every launch: the player hears
        // of it, and a launch option sets the timeout.
        #[test]
        fn a_timed_out_install_script_is_told_to_the_player() {
            let f = make_vm_fixture(Options {
                install_log: Some("x86 Update 4 exit 0\nUE4 Prerequisites exit 124\n"),
                ..Default::default()
            });
            f.launch(&["waitforexitandrun", GAME], &with(&f.env, &[("MN_INSTALL_TIMEOUT", "120")]));
            assert_eq!(
                f.notifier.posted(),
                ["SMITE 2's install step \"UE4 Prerequisites\" was stopped after waiting too long, maybe for a click in a \
                  window the game VM can't show. It runs again at the next launch."]
            );
            assert!(f.scripts.first().contains("MN_INSTALL_TIMEOUT='120'\n"));
        }
    }
}
