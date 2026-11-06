//! The menu bar app in AppKit (MacNeutronApp.swift, MenuContent.swift, SetupView.swift, SettingsView.swift,
//! GamesView.swift): a status item whose menu is built as it opens, and four windows, each redrawn from `views` when
//! what it shows changes. Everything here runs on the main thread; the models' listeners hop to it, and actions that
//! block run on a thread of their own.

mod games;
mod kit;
pub mod system;

use std::cell::{OnceCell, RefCell};
use std::collections::{BTreeMap, BTreeSet};
use std::path::Path;
use std::rc::Rc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use block2::RcBlock;
use dispatch2::DispatchQueue;
use objc2::rc::Retained;
use objc2::runtime::{AnyObject, ProtocolObject};
use objc2::{MainThreadMarker, MainThreadOnly};
use objc2_app_kit::{
    NSAlert, NSAlertFirstButtonReturn, NSApplication, NSApplicationActivationPolicy, NSBackingStoreType, NSColor,
    NSFontTextStyleCallout, NSFontTextStyleHeadline, NSFontTextStyleTitle2, NSFontTextStyleTitle3, NSGridView, NSMenu,
    NSMenuItem, NSModalResponse, NSStatusBar, NSStatusItem, NSVariableStatusItemLength, NSView, NSWindow,
    NSWindowCollectionBehavior, NSWindowStyleMask, NSWorkspace,
};
use objc2_foundation::{NSArray, NSEdgeInsets, NSNotification, NSPoint, NSRect, NSSize, NSURL};

use crate::app_model::{AppModel, StatusDisplay};
use crate::update_model::UpdateModel;
use crate::views::{self, Act, Confirmation, Control, Inputs, MenuEntry, Row, StepAction, Tone};
use kit::{font, insets, ns, upcast, Delegate, Kit, Targets};

pub(crate) use games::games_toolbar_items;

/// The app's windows (`Window(id:)` scenes).
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum Scene {
    Setup,
    Games,
    Cleanup,
    Settings,
}

impl Scene {
    pub const ALL: [Scene; 4] = [Scene::Setup, Scene::Games, Scene::Cleanup, Scene::Settings];

    pub fn title(self) -> &'static str {
        match self {
            Scene::Setup => "Set up MacNeutron",
            Scene::Games => "Games",
            Scene::Cleanup => "Free up space",
            Scene::Settings => "MacNeutron Settings",
        }
    }

    /// `.frame(width:)`: the content-sized windows' fixed widths.
    fn width(self) -> f64 {
        match self {
            Scene::Setup => 560.0,
            Scene::Games => 640.0,
            Scene::Cleanup => 420.0,
            Scene::Settings => 460.0,
        }
    }
}

/// What a content-sized window last drew, to redraw only on a change.
#[derive(PartialEq)]
enum Drawn {
    Setup(views::Setup),
    Cleanup(views::Cleanup),
    Settings(Vec<Row>),
}

struct Pane {
    window: Retained<NSWindow>,
    targets: Targets,
    drawn: RefCell<Option<Drawn>>,
}

pub struct App {
    mtm: MainThreadMarker,
    pub model: Arc<AppModel>,
    pub updates: Arc<UpdateModel>,
    delegate: Retained<Delegate>,
    status_item: OnceCell<Retained<NSStatusItem>>,
    menu_targets: Targets,
    inputs: RefCell<Inputs>,
    panes: RefCell<BTreeMap<Scene, Rc<Pane>>>,
    /// Cleanup's ticked app IDs (`@State selected`).
    selected: RefCell<BTreeSet<String>>,
    pub(crate) games: games::Games,
    /// Whether setup was complete at launch: Swift's `defaultLaunchBehavior` reads it as the scene is made.
    open_setup: std::cell::Cell<bool>,
}

thread_local! {
    static APP: OnceCell<Rc<App>> = const { OnceCell::new() };
}

/// The app, on the main thread (None before `run`, or off it).
pub(crate) fn with_app<R>(work: impl FnOnce(&App) -> R) -> Option<R> {
    let app = APP.with(|cell| cell.get().cloned())?;
    Some(work(&app))
}

pub(crate) fn perform(act: &Act, sender: &AnyObject) {
    with_app(|app| app.perform(act, sender));
}

static REDRAW_PENDING: AtomicBool = AtomicBool::new(false);

/// The models' listener: one redraw on the main thread, however many changes arrive before it runs.
fn request_redraw() {
    if !REDRAW_PENDING.swap(true, Ordering::AcqRel) {
        DispatchQueue::main().exec_async(|| {
            REDRAW_PENDING.store(false, Ordering::Release);
            with_app(App::redraw);
        });
    }
}

/// Runs the menu bar app; never returns. `start` is the model's `start`, called once the listeners are set.
pub fn run(model: Arc<AppModel>, updates: Arc<UpdateModel>) -> ! {
    let mtm = MainThreadMarker::new().expect("the app runs on the main thread");
    let application = NSApplication::sharedApplication(mtm);
    application.setActivationPolicy(NSApplicationActivationPolicy::Accessory); // a menu-bar app, even when run unbundled
    let app = install(mtm, model, updates);
    app.open_setup.set(!app.inputs.borrow().setup_complete());

    let item = NSStatusBar::systemStatusBar().statusItemWithLength(NSVariableStatusItemLength);
    let menu = NSMenu::new(mtm);
    menu.setDelegate(Some(ProtocolObject::from_ref(&*app.delegate)));
    item.setMenu(Some(&menu));
    let _ = app.status_item.set(item);
    app.draw_status_icon();

    application.setDelegate(Some(ProtocolObject::from_ref(&*app.delegate)));
    app.model.set_on_change(request_redraw);
    app.updates.set_on_change(request_redraw);
    app.model.start();
    application.run();
    std::process::exit(0)
}

/// Makes the app (no status item, nothing shown) and keeps it for the main thread's callbacks.
pub fn install(mtm: MainThreadMarker, model: Arc<AppModel>, updates: Arc<UpdateModel>) -> Rc<App> {
    let app = Rc::new(App {
        mtm,
        inputs: RefCell::new(Inputs::read(&model, &updates)),
        model,
        updates,
        delegate: Delegate::new(mtm),
        status_item: OnceCell::new(),
        menu_targets: Targets::default(),
        panes: RefCell::default(),
        selected: RefCell::default(),
        games: games::Games::default(),
        open_setup: std::cell::Cell::new(false),
    });
    APP.with(|cell| {
        let _ = cell.set(app.clone());
    });
    app
}

impl App {
    fn kit<'a>(&self, targets: &'a Targets) -> Kit<'a> {
        Kit { mtm: self.mtm, targets }
    }

    /// `applicationDidFinishLaunching`: the hot key, and the setup window when setup isn't done.
    pub(crate) fn did_finish_launching(&self) {
        system::install_menu_hot_key();
        if self.open_setup.get() {
            self.show(Scene::Setup);
        }
    }

    fn redraw(&self) {
        *self.inputs.borrow_mut() = Inputs::read(&self.model, &self.updates);
        self.draw_status_icon();
        for scene in Scene::ALL {
            let visible = self.panes.borrow().get(&scene).is_some_and(|pane| pane.window.isVisible());
            if visible {
                self.draw(scene, false);
            }
        }
    }

    fn draw_status_icon(&self) {
        let Some(item) = self.status_item.get() else { return };
        let symbol = self.inputs.borrow().status.symbol();
        let image = objc2_app_kit::NSImage::imageWithSystemSymbolName_accessibilityDescription(&ns(symbol), Some(&ns("MacNeutron")));
        if let (Some(button), Some(image)) = (item.button(self.mtm), image) {
            image.setTemplate(true);
            button.setImage(Some(&image));
        }
    }

    /// Opens the menu (the hot key): clicks the status item's button.
    pub(crate) fn open_menu(&self) {
        match self.status_item.get().and_then(|item| item.button(self.mtm)) {
            Some(button) => unsafe { button.performClick(None) },
            None => self.model.log.append("menu hotkey Shift-F9: no menu bar item to open\n"),
        }
    }

    // MARK: The menu

    /// `menuNeedsUpdate:`: the menu's items, made as it opens.
    pub(crate) fn fill_menu(&self, menu: &NSMenu) {
        *self.inputs.borrow_mut() = Inputs::read(&self.model, &self.updates);
        let entries = views::menu(&self.inputs.borrow());
        self.menu_targets.clear();
        let kit = self.kit(&self.menu_targets);
        menu.removeAllItems();
        for entry in entries {
            let item = match entry {
                MenuEntry::Divider => NSMenuItem::separatorItem(self.mtm),
                MenuEntry::Text(text) => {
                    let item = NSMenuItem::new(self.mtm);
                    item.setTitle(&ns(&text));
                    item.setEnabled(false);
                    item
                }
                MenuEntry::Button(title, act) => {
                    let item = NSMenuItem::new(self.mtm);
                    item.setTitle(&ns(&title));
                    if act == Act::Quit {
                        item.setKeyEquivalent(&ns("q")); // ⌘Q
                    }
                    kit.bind(&item, act);
                    item
                }
            };
            menu.addItem(&item);
        }
    }

    // MARK: Windows

    fn pane(&self, scene: Scene) -> Rc<Pane> {
        if let Some(pane) = self.panes.borrow().get(&scene) {
            return pane.clone();
        }
        let mut style = NSWindowStyleMask::Titled | NSWindowStyleMask::Closable | NSWindowStyleMask::Miniaturizable;
        if scene == Scene::Games {
            style |= NSWindowStyleMask::Resizable;
        }
        let window = unsafe {
            NSWindow::initWithContentRect_styleMask_backing_defer(
                NSWindow::alloc(self.mtm),
                NSRect::new(NSPoint::new(0.0, 0.0), NSSize::new(scene.width(), 360.0)),
                style,
                NSBackingStoreType::Buffered,
                false,
            )
        };
        unsafe { window.setReleasedWhenClosed(false) };
        window.setTitle(&ns(scene.title()));
        window.setDelegate(Some(ProtocolObject::from_ref(&*self.delegate)));
        if scene == Scene::Games {
            self.games.make(self, &window);
        }
        window.center();
        let pane = Rc::new(Pane { window, targets: Targets::default(), drawn: RefCell::new(None) });
        self.panes.borrow_mut().insert(scene, pane.clone());
        pane
    }

    pub fn window(&self, scene: Scene) -> Retained<NSWindow> {
        self.pane(scene).window.clone()
    }

    /// `show(_:with:)`: opens the scene's window, or brings it back, in front of other apps.
    pub fn show(&self, scene: Scene) {
        let pane = self.pane(scene);
        let appearing = !pane.window.isVisible();
        self.draw(scene, true);
        if scene == Scene::Setup && appearing {
            self.work(AppModel::refresh); // SetupView's onAppear
        }
        self.raise(&pane.window);
    }

    /// `raiseWindow`: brings the window to this Space and in front of other apps, then asks to activate.
    fn raise(&self, window: &NSWindow) {
        window.setCollectionBehavior(window.collectionBehavior() | NSWindowCollectionBehavior::MoveToActiveSpace);
        if window.isMiniaturized() {
            window.deminiaturize(None);
        }
        window.makeKeyAndOrderFront(None);
        window.orderFrontRegardless();
        // ponytail: activate() is refused for a menu-bar click (FB23508310, Apple forum 836619); this soft-deprecated
        // call, made inside the click's action, is the forum sample's working path. If a 27.x drops it, use a
        // temporary .regular activation policy while a window is open.
        #[allow(deprecated)]
        NSApplication::sharedApplication(self.mtm).activateIgnoringOtherApps(true);
    }

    pub(crate) fn window_will_close(&self, notification: &NSNotification) {
        let games = self.panes.borrow().get(&Scene::Games).map(|pane| pane.window.clone());
        let closing = notification.object();
        if let (Some(games), Some(closing)) = (games, closing) {
            if std::ptr::eq(&*games as *const NSWindow as *const AnyObject, &*closing as *const AnyObject) {
                games.makeFirstResponder(None); // ends the launch options' editing, which saves it
            }
        }
    }

    /// Redraws a scene's window when what it shows changed (always with `force`).
    pub fn draw(&self, scene: Scene, force: bool) {
        let pane = self.pane(scene);
        let inputs = self.inputs.borrow().clone();
        let drawn = match scene {
            Scene::Games => return self.games.draw(self, &inputs, force),
            Scene::Setup => Drawn::Setup(views::setup(&inputs)),
            Scene::Cleanup => Drawn::Cleanup(views::cleanup(&inputs.state, &self.selected.borrow())),
            Scene::Settings => Drawn::Settings(views::settings(&inputs)),
        };
        if !force && pane.drawn.borrow().as_ref() == Some(&drawn) {
            return;
        }
        pane.targets.clear();
        let kit = self.kit(&pane.targets);
        let content = match &drawn {
            Drawn::Setup(view) => setup_view(&kit, view),
            Drawn::Cleanup(view) => cleanup_view(&kit, view),
            Drawn::Settings(rows) => settings_view(&kit, rows, scene.width()),
        };
        *pane.drawn.borrow_mut() = Some(drawn);
        set_fitted_content(&pane.window, &content);
    }

    // MARK: Actions

    /// A blocking model call, off the main thread.
    fn work(&self, work: impl FnOnce(&AppModel) + Send + 'static) {
        let model = self.model.clone();
        std::thread::spawn(move || work(&model));
    }

    fn update_game(&self, app_id: u32, change: impl FnOnce(&mut macneutron_core::GameSettings) + Send + 'static) {
        self.work(move |model| model.update(app_id, change));
    }

    fn perform(&self, act: &Act, sender: &AnyObject) {
        match act {
            Act::DismissError => self.model.dismiss_error(),
            Act::RestartSteam => self.work(AppModel::restart_steam),
            Act::ShowSetup => self.show(Scene::Setup),
            Act::ShowGames => {
                self.work(AppModel::refresh);
                self.show(Scene::Games);
            }
            Act::OpenSteam => self.work(|model| {
                let _ = model.mode.process.launch();
            }),
            Act::ShowCleanup => self.show(Scene::Cleanup),
            Act::ShowLogs => open_path(&macneutron_core::LauncherLog::standard().directory),
            Act::ShowSettings => self.show(Scene::Settings),
            Act::Quit => NSApplication::sharedApplication(self.mtm).terminate(None),
            Act::SetLaunchAtLogin => {
                let on = kit::is_on(sender);
                self.work(move |model| model.set_launch_at_login(on));
            }
            Act::OpenLoginItems => system::open_login_items(),
            Act::SetAutomaticUpdates => self.updates.set_automatic(kit::is_on(sender)),
            Act::CheckForUpdates => {
                let updates = self.updates.clone();
                std::thread::spawn(move || updates.check());
            }
            Act::OpenURL(url) => {
                if let Some(url) = NSURL::URLWithString(&ns(url)) {
                    NSWorkspace::sharedWorkspace().openURL(&url);
                }
            }
            Act::InstallUpdate => {
                let updates = self.updates.clone();
                std::thread::spawn(move || updates.install());
            }
            Act::RepairRuntime => self.work(|model| model.install_runtime(true)),
            Act::DownloadValve => self.work(|model| model.update_valve_client(true)),
            Act::RemoveValve => self.work(AppModel::remove_valve_client),
            Act::RetryVM => self.work(AppModel::set_up_vm),
            Act::RebuildVM => {
                let confirmation = views::rebuild_confirmation(&self.inputs.borrow());
                let model = self.model.clone();
                self.confirm(Scene::Settings, confirmation, true, move || {
                    std::thread::spawn(move || model.rebuild_vm());
                });
            }
            Act::TurnOffSteamPlay => {
                let model = self.model.clone();
                self.confirm(Scene::Settings, views::turn_off_confirmation(), true, move || {
                    std::thread::spawn(move || model.disable_steam_play());
                });
            }
            Act::TurnOnSteamPlay => self.work(AppModel::enable_steam_play),
            Act::SelectOrphan(app_id) => {
                if kit::is_on(sender) {
                    self.selected.borrow_mut().insert(app_id.clone());
                } else {
                    self.selected.borrow_mut().remove(app_id);
                }
                // After the click returns: the redraw replaces the toggle that sent it.
                DispatchQueue::main().exec_async(|| {
                    with_app(|app| app.draw(Scene::Cleanup, false));
                });
            }
            Act::DeleteSelected => {
                let confirmation = views::delete_confirmation(&self.inputs.borrow().state, &self.selected.borrow());
                self.confirm(Scene::Cleanup, confirmation, true, || {
                    with_app(|app| {
                        let picked = views::chosen(&app.inputs.borrow().state, &app.selected.borrow());
                        app.work(move |model| model.clean_up(&picked));
                        app.selected.borrow_mut().clear();
                        app.draw(Scene::Cleanup, false);
                    });
                });
            }
            Act::Search => {
                *self.games.search.borrow_mut() = kit::string_value(sender);
                self.draw(Scene::Games, false);
            }
            Act::RunsAs(app_id) => {
                let tag = views::RUNS_AS[kit::selected_index(sender).min(views::RUNS_AS.len() - 1)].0;
                let (app_id, run_as) = (*app_id, views::run_as_setting(tag));
                let model = self.model.clone();
                self.confirm_then(
                    Scene::Games,
                    views::switch_confirmation(),
                    false,
                    move || {
                        std::thread::spawn(move || model.update(app_id, move |settings| settings.run_as = run_as));
                    },
                    || {
                        with_app(|app| app.games.reload(app)); // the picker goes back to the current version
                    },
                );
            }
            Act::Graphics(app_id) => {
                let tag = views::GRAPHICS[kit::selected_index(sender).min(views::GRAPHICS.len() - 1)].0;
                let graphics = (!tag.is_empty()).then(|| tag.to_string());
                self.update_game(*app_id, move |settings| settings.graphics = graphics);
            }
            Act::GameToggle(app_id, toggle) => {
                let (toggle, on) = (*toggle, kit::is_on(sender));
                self.update_game(*app_id, move |settings| toggle.set(settings, on));
            }
            Act::GamePicker(app_id, picker) => {
                let options = picker.options();
                let tag = options[kit::selected_index(sender).min(options.len() - 1)].0;
                let picker = *picker;
                self.update_game(*app_id, move |settings| picker.set(settings, tag));
            }
            Act::LaunchOptions(app_id) => {
                let draft = kit::string_value(sender);
                let change = self.inputs.borrow().state.games.iter().find(|row| row.id() == *app_id).and_then(|row| {
                    views::launch_options_change(&row.settings, &draft)
                });
                if let Some(value) = change {
                    self.update_game(*app_id, move |settings| settings.launch_options = value);
                }
            }
        }
    }

    fn confirm(&self, scene: Scene, confirmation: Confirmation, destructive: bool, confirmed: impl FnOnce() + 'static) {
        self.confirm_then(scene, confirmation, destructive, confirmed, || {});
    }

    /// `confirmationDialog`: a sheet on the scene's window with the confirming button and Cancel.
    fn confirm_then(
        &self,
        scene: Scene,
        confirmation: Confirmation,
        destructive: bool,
        confirmed: impl FnOnce() + 'static,
        cancelled: impl FnOnce() + 'static,
    ) {
        let alert = NSAlert::new(self.mtm);
        alert.setMessageText(&ns(&confirmation.title));
        alert.setInformativeText(&ns(&confirmation.message));
        alert.addButtonWithTitle(&ns(&confirmation.button)).setHasDestructiveAction(destructive);
        alert.addButtonWithTitle(&ns("Cancel"));
        let answers = RefCell::new(Some((confirmed, cancelled)));
        let handler = RcBlock::new(move |response: NSModalResponse| {
            if let Some((confirmed, cancelled)) = answers.take() {
                if response == NSAlertFirstButtonReturn {
                    confirmed();
                } else {
                    cancelled();
                }
            }
        });
        alert.beginSheetModalForWindow_completionHandler(&self.window(scene), Some(&handler));
    }
}

fn open_path(path: &Path) {
    let url = NSURL::fileURLWithPath(&ns(&path.to_string_lossy()));
    NSWorkspace::sharedWorkspace().openURL(&url);
}

/// Sizes a content-sized window to `content` (keeping its top-left corner), then shows it.
fn set_fitted_content(window: &NSWindow, content: &NSView) {
    content.layoutSubtreeIfNeeded();
    let size = content.fittingSize();
    let frame = window.frame();
    let fitted = window.frameRectForContentRect(NSRect::new(NSPoint::new(0.0, 0.0), size));
    let top = frame.origin.y + frame.size.height;
    window.setFrame_display(NSRect::new(NSPoint::new(frame.origin.x, top - fitted.size.height), fitted.size), true);
    window.setContentView(Some(content));
}

fn width(view: &NSView, points: f64) {
    view.setTranslatesAutoresizingMaskIntoConstraints(false);
    view.widthAnchor().constraintEqualToConstant(points).setActive(true);
}

// MARK: Settings (SettingsView.swift)

/// The grouped form: one rounded section of rows, 460 points wide.
fn settings_view(kit: &Kit, rows: &[Row], total: f64) -> Retained<NSView> {
    let inner = total - 40.0;
    let mut views: Vec<Retained<NSView>> = Vec::new();
    for (index, row) in rows.iter().enumerate() {
        if index > 0 {
            let line = kit.separator();
            views.push(upcast(&line));
        }
        let row = form_row(kit, row, inner - 20.0);
        let padded = kit.stack(&[row], false, 0.0, NSEdgeInsets { top: 8.0, left: 10.0, bottom: 8.0, right: 10.0 });
        width(&padded, inner);
        views.push(upcast(&padded));
    }
    let section = kit.vstack(&views, 0.0);
    let fill = NSColor::quaternarySystemFillColor();
    let grouped = kit.group(&section, &NSColor::separatorColor(), Some(&fill), 6.0);
    let root = kit.stack(&[upcast(&grouped)], true, 0.0, insets(20.0));
    width(&root, total);
    upcast(&root)
}

fn form_row(kit: &Kit, row: &Row, room: f64) -> Retained<NSView> {
    match row {
        Row::Switch { title, on, act, enabled } => {
            let switch = kit.switch(*on, act.clone(), *enabled);
            let label = kit.wrapping(title, None, room - 50.0, false);
            upcast(&kit.hstack(&[upcast(&label), kit.spacer(), upcast(&switch)], 8.0))
        }
        Row::Labeled { label, tone, controls } => {
            let controls: Vec<Retained<NSView>> = controls.iter().map(|control| form_control(kit, control)).collect();
            let trailing = kit.hstack(&controls, 8.0);
            let used = trailing.fittingSize().width;
            let label = kit.wrapping(label, *tone, (room - used - 8.0).max(84.0), false);
            upcast(&kit.hstack(&[upcast(&label), kit.spacer(), upcast(&trailing)], 4.0))
        }
        Row::Note(text, tone) => upcast(&kit.wrapping(text, Some(*tone), room, true)),
        Row::Progress(text) => {
            let label = kit.wrapping(text, None, room - 30.0, false);
            upcast(&kit.hstack(&[upcast(&label), kit.spacer(), upcast(&kit.spinner())], 8.0))
        }
    }
}

fn form_control(kit: &Kit, control: &Control) -> Retained<NSView> {
    match control {
        Control::Button { title, act, enabled, destructive } => {
            let button = kit.button(title, act.clone(), *enabled);
            button.setHasDestructiveAction(*destructive);
            upcast(&button)
        }
        Control::Check { title, on, act, enabled } => upcast(&kit.checkbox(title, *on, act.clone(), *enabled)),
        Control::Text(text, tone) => upcast(&kit.text(text, Some(*tone))),
        Control::Link { title, url, enabled } => {
            let link = kit.button(title, Act::OpenURL(url.clone()), *enabled);
            link.setBordered(false);
            link.setContentTintColor(Some(&NSColor::linkColor()));
            upcast(&link)
        }
    }
}

// MARK: Free up space (CleanupView)

fn cleanup_view(kit: &Kit, view: &views::Cleanup) -> Retained<NSView> {
    let room = Scene::Cleanup.width() - 40.0;
    let title = kit.text("Leftover data from games you've uninstalled", None);
    title.setFont(Some(&font(unsafe { NSFontTextStyleHeadline })));
    let mut views: Vec<Retained<NSView>> = vec![
        upcast(&title),
        upcast(&kit.wrapping(
            "These folders hold games' Windows settings and saves. Saves that aren't in Steam Cloud exist nowhere else.",
            Some(Tone::Secondary),
            room,
            false,
        )),
    ];
    if view.rows.is_empty() {
        views.push(upcast(&kit.text("Nothing to clean up.", Some(Tone::Secondary))));
    }
    for row in &view.rows {
        let check = kit.checkbox(&row.name, row.selected, Act::SelectOrphan(row.app_id.clone()), true);
        let line = kit.hstack(&[upcast(&check), kit.spacer(), upcast(&kit.text(&row.size, Some(Tone::Secondary)))], 8.0);
        width(&line, room);
        views.push(upcast(&line));
    }
    let delete = kit.button("Delete selected", Act::DeleteSelected, view.delete_enabled);
    delete.setHasDestructiveAction(true);
    let bar = kit.hstack(&[kit.spacer(), upcast(&delete)], 8.0);
    width(&bar, room);
    views.push(upcast(&bar));
    let root = kit.stack(&views, true, 12.0, insets(20.0));
    width(&root, Scene::Cleanup.width());
    upcast(&root)
}

// MARK: Setup (SetupView.swift)

fn setup_view(kit: &Kit, view: &views::Setup) -> Retained<NSView> {
    let room = Scene::Setup.width() - 40.0;
    let title_style = unsafe { NSFontTextStyleTitle2 };
    let heading = kit.text("Set up MacNeutron", None);
    heading.setFont(Some(&font(title_style)));
    let mut views: Vec<Retained<NSView>> =
        vec![upcast(&kit.hstack(&[upcast(&kit.symbol("atom", title_style, None)), upcast(&heading)], 6.0))];
    for step in &view.steps {
        views.push(step_view(kit, step, room, view.enabled));
    }
    if let Some([native, windows, native_names, windows_names]) = &view.games {
        let callout = unsafe { NSFontTextStyleCallout };
        let heading = |symbol: &str, text: &str| {
            let label = kit.text(text, Some(Tone::Secondary));
            label.setFont(Some(&font(callout)));
            let icon = kit.symbol(symbol, callout, Some(&NSColor::secondaryLabelColor()));
            upcast(&kit.hstack(&[upcast(&icon), upcast(&label)], 4.0))
        };
        let names = |text: &str| {
            let label = kit.wrapping(text, None, (room - 30.0 - 24.0) / 2.0, false);
            label.setFont(Some(&font(callout)));
            upcast(&label)
        };
        let rows = NSArray::from_retained_slice(&[
            NSArray::from_retained_slice(&[heading("apple.logo", native), heading("square.grid.2x2", windows)]),
            NSArray::from_retained_slice(&[names(native_names), names(windows_names)]),
        ]);
        let grid = NSGridView::gridViewWithViews(&rows, kit.mtm);
        grid.setColumnSpacing(24.0);
        grid.setRowSpacing(4.0);
        let indented = kit.stack(&[upcast(&grid)], false, 0.0, NSEdgeInsets { top: 0.0, left: 30.0, bottom: 0.0, right: 0.0 });
        views.push(upcast(&indented));
    }
    if let Some(busy) = &view.busy {
        views.push(upcast(&kit.hstack(&[upcast(&kit.spinner()), upcast(&kit.text(busy, None))], 6.0)));
    }
    if let Some(error) = &view.error {
        views.push(upcast(&kit.wrapping(error, Some(Tone::Red), room, true)));
    }
    let root = kit.stack(&views, true, 12.0, insets(20.0));
    width(&root, Scene::Setup.width());
    upcast(&root)
}

/// `Step`: a done mark, title and detail, and the step's control, in a rounded border.
fn step_view(kit: &Kit, step: &views::Step, room: f64, enabled: bool) -> Retained<NSView> {
    let title3 = unsafe { NSFontTextStyleTitle3 };
    let (symbol, tint) = if step.done {
        ("checkmark.circle.fill", NSColor::systemGreenColor())
    } else {
        ("circle.dashed", NSColor::secondaryLabelColor())
    };
    let action: Option<Retained<NSView>> = match &step.action {
        StepAction::None => None,
        StepAction::Spinner => Some(upcast(&kit.spinner())),
        StepAction::Button { title, act, enabled: on } => {
            let button = kit.button(title, act.clone(), *on && enabled);
            button.setKeyEquivalent(&ns("\r")); // .borderedProminent: the accent-colored button
            Some(upcast(&button))
        }
    };
    let action_width = action.as_ref().map_or(0.0, |view| view.fittingSize().width + 10.0);
    let detail = kit.wrapping(&step.detail, Some(Tone::Secondary), room - 20.0 - 40.0 - action_width, false);
    detail.setFont(Some(&font(unsafe { NSFontTextStyleCallout })));
    let text = kit.vstack(&[upcast(&kit.text(&step.title, None)), upcast(&detail)], 2.0);
    let mut parts = vec![upcast(&kit.symbol(symbol, title3, Some(&tint))), upcast(&text), kit.spacer()];
    parts.extend(action);
    let line = kit.stack(&parts, false, 10.0, insets(10.0));
    line.setAlignment(objc2_app_kit::NSLayoutAttribute::Top);
    width(&line, room);
    let group = kit.group(&line, &NSColor::quaternaryLabelColor(), None, 8.0);
    upcast(&group)
}

/// Shows each scene's window as a fresh launch would and writes a PNG of each, plus the menu's lines, into `folder`
/// (the preview example: a fake model, nothing installed, no Steam).
pub fn preview(mtm: MainThreadMarker, model: Arc<AppModel>, updates: Arc<UpdateModel>, folder: &Path, select: Option<u32>) {
    let app = install(mtm, model, updates);
    let lines: Vec<String> = views::menu(&app.inputs.borrow())
        .into_iter()
        .map(|entry| match entry {
            MenuEntry::Text(text) => format!("  {text}"),
            MenuEntry::Button(title, _) => format!("[{title}]"),
            MenuEntry::Divider => "-----".into(),
        })
        .collect();
    let _ = std::fs::write(folder.join("menu.txt"), lines.join("\n") + "\n");
    for scene in Scene::ALL {
        app.draw(scene, true);
        if scene == Scene::Games {
            app.games.select(&app, select);
        }
        let window = app.window(scene);
        window.orderFront(None);
        if let Some(view) = window.contentView() {
            view.layoutSubtreeIfNeeded();
            snapshot(&window, &folder.join(format!("{scene:?}.png")));
        }
    }
}

/// The window's content (toolbar and title bar excluded), as a PNG.
fn snapshot(window: &NSWindow, path: &Path) {
    use objc2_app_kit::{NSBitmapImageFileType, NSBitmapImageRep};
    // The frame view: the window's background and title bar, which the content view doesn't draw.
    let Some(view) = window.contentView().and_then(|content| unsafe { content.superview() }) else { return };
    let bounds = view.bounds();
    let Some(bitmap) = view.bitmapImageRepForCachingDisplayInRect(bounds) else { return };
    view.cacheDisplayInRect_toBitmapImageRep(bounds, &bitmap);
    let data = unsafe { bitmap.representationUsingType_properties(NSBitmapImageFileType::PNG, &objc2_foundation::NSDictionary::new()) };
    if let Some(data) = data {
        let _ = std::fs::write(path, data.to_vec());
    }
    let _: Option<&NSBitmapImageRep> = None;
}
