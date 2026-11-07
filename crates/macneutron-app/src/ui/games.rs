//! The games window (GamesView.swift): a table of games with a search field in the toolbar, the selected game's options
//! under it, and the error and restart bars. Unlike the other windows it isn't rebuilt whole: the table reloads when
//! its rows change, the options pane when the selected game's settings do, and the launch options field only when
//! another game is selected, so a draft being typed survives the redraws around it.

use std::cell::{Cell, OnceCell, RefCell};

use objc2::rc::Retained;
use objc2::runtime::ProtocolObject;
use objc2::{MainThreadOnly, Message};
use objc2_app_kit::{
    NSFontTextStyleCaption1, NSFontTextStyleTitle2, NSLayoutConstraintOrientation, NSScrollView, NSSearchToolbarItem,
    NSStackView, NSTableColumn, NSTableView, NSTableViewStyle, NSTextField, NSToolbar, NSToolbarFlexibleSpaceItemIdentifier,
    NSToolbarItem, NSToolbarItemIdentifier, NSView, NSWindow, NSWindowToolbarStyle,
};
use objc2_foundation::{NSArray, NSIndexSet, NSSize, NSString};

use super::kit::{font, insets, ns, upcast, Kit, Targets};
use super::App;
use crate::app_model::GameRow;
use crate::views::{self, Act, Cell as TableCell, GamePicker, GameToggle, Inputs, Tone};

const SEARCH: &str = "search";

pub(crate) fn games_toolbar_items() -> Retained<NSArray<NSToolbarItemIdentifier>> {
    let flexible: &NSString = unsafe { NSToolbarFlexibleSpaceItemIdentifier };
    NSArray::from_retained_slice(&[ns(&flexible.to_string()), ns(SEARCH)])
}

/// What the launch options block shows: the game's ID and the lines under the field.
type LaunchKey = (u32, Vec<(String, Tone)>);

struct Views {
    window: Retained<NSWindow>,
    content: Retained<NSView>,
    table: Retained<NSTableView>,
    detail: Retained<NSStackView>,
    options: Retained<NSStackView>,
    launch: Retained<NSStackView>,
    error: Retained<NSStackView>,
    restart: Retained<NSStackView>,
    overlay: Retained<NSStackView>,
    overlay_text: Retained<NSTextField>,
}

#[derive(Default)]
pub(crate) struct Games {
    pub search: RefCell<String>,
    selection: Cell<Option<u32>>,
    pub rows: RefCell<Vec<GameRow>>,
    views: OnceCell<Views>,
    drawn_detail: RefCell<Option<Option<GameRow>>>,
    drawn_launch: RefCell<Option<LaunchKey>>,
    drawn_bars: RefCell<Option<[Option<String>; 3]>>,
    table_targets: Targets,
    options_targets: Targets,
    launch_targets: Targets,
    bar_targets: Targets,
    search_targets: Targets,
    /// Set while the table reloads, so its selection changes aren't the player's.
    reloading: Cell<bool>,
}

/// Makes `child` as wide as the vertical `stack` (less its insets).
fn fill_width(stack: &NSStackView, child: &NSView) {
    let insets = stack.edgeInsets();
    child.setTranslatesAutoresizingMaskIntoConstraints(false);
    child.widthAnchor().constraintEqualToAnchor_constant(&stack.widthAnchor(), -(insets.left + insets.right)).setActive(true);
}

/// Replaces a stack's views.
fn replace(stack: &NSStackView, views: &[Retained<NSView>]) {
    for view in stack.arrangedSubviews().iter() {
        stack.removeArrangedSubview(&view);
        view.removeFromSuperview();
    }
    for view in views {
        stack.addArrangedSubview(view);
    }
}

impl Games {
    /// Builds the window's views (once, when the window is made).
    pub(crate) fn make(&self, app: &App, window: &NSWindow) {
        let mtm = app.mtm;
        let kit = app.kit(&self.bar_targets);

        let table = NSTableView::new(mtm);
        for (identifier, title) in [("game", "Game"), ("runsAs", "Runs as"), ("graphics", "Graphics")] {
            let column = NSTableColumn::initWithIdentifier(NSTableColumn::alloc(mtm), &ns(identifier));
            column.setTitle(&ns(title));
            column.setWidth(if identifier == "game" { 260.0 } else { 160.0 });
            table.addTableColumn(&column);
        }
        table.setStyle(NSTableViewStyle::Inset);
        table.setUsesAlternatingRowBackgroundColors(true);
        table.setRowHeight(24.0);
        // The delegate lives as long as the app.
        unsafe {
            table.setDataSource(Some(ProtocolObject::from_ref(&*app.delegate)));
            table.setDelegate(Some(ProtocolObject::from_ref(&*app.delegate)));
        }
        let scroll = NSScrollView::new(mtm);
        scroll.setDocumentView(Some(&table));
        scroll.setHasVerticalScroller(true);
        scroll.setContentHuggingPriority_forOrientation(1.0, NSLayoutConstraintOrientation::Vertical);

        let options = kit.vstack(&[], 6.0);
        let launch = kit.vstack(&[], 4.0);
        let detail = kit.stack(&[upcast(&options), upcast(&launch)], true, 6.0, insets(10.0));
        fill_width(&detail, &launch);
        let error = kit.stack(&[], false, 8.0, insets(10.0));
        let restart = kit.stack(&[], false, 8.0, insets(10.0));
        let root = kit.vstack(&[upcast(&scroll), upcast(&detail), upcast(&error), upcast(&restart)], 0.0);
        for child in [upcast(&scroll), upcast(&detail), upcast(&error), upcast(&restart)] {
            fill_width(&root, &child);
        }

        // ContentUnavailableView, over everything.
        let title2 = unsafe { NSFontTextStyleTitle2 };
        let symbol = kit.symbol("exclamationmark.triangle", title2, Some(&objc2_app_kit::NSColor::secondaryLabelColor()));
        let title = kit.text("Steam's app list couldn't be read", None);
        title.setFont(Some(&objc2_app_kit::NSFont::boldSystemFontOfSize(font(title2).pointSize())));
        let overlay_text = kit.wrapping("", Some(Tone::Secondary), 400.0, false);
        overlay_text.setAlignment(objc2_app_kit::NSTextAlignment::Center);
        let overlay = kit.vstack(&[upcast(&symbol), upcast(&title), upcast(&overlay_text)], 8.0);
        overlay.setAlignment(objc2_app_kit::NSLayoutAttribute::CenterX);
        overlay.setHidden(true);

        let content = NSView::new(mtm);
        content.addSubview(&root);
        content.addSubview(&overlay);
        root.setTranslatesAutoresizingMaskIntoConstraints(false);
        overlay.setTranslatesAutoresizingMaskIntoConstraints(false);
        for constraint in [
            root.leadingAnchor().constraintEqualToAnchor(&content.leadingAnchor()),
            root.trailingAnchor().constraintEqualToAnchor(&content.trailingAnchor()),
            root.topAnchor().constraintEqualToAnchor(&content.topAnchor()),
            root.bottomAnchor().constraintEqualToAnchor(&content.bottomAnchor()),
            overlay.centerXAnchor().constraintEqualToAnchor(&content.centerXAnchor()),
            overlay.centerYAnchor().constraintEqualToAnchor(&content.centerYAnchor()),
        ] {
            constraint.setActive(true);
        }
        window.setContentView(Some(&content));
        window.setContentMinSize(NSSize::new(640.0, 360.0));
        window.setContentSize(NSSize::new(640.0, 360.0));

        let toolbar = NSToolbar::initWithIdentifier(NSToolbar::alloc(mtm), &ns("games"));
        toolbar.setDelegate(Some(ProtocolObject::from_ref(&*app.delegate)));
        toolbar.setDisplayMode(objc2_app_kit::NSToolbarDisplayMode::IconOnly);
        window.setToolbar(Some(&toolbar));
        window.setToolbarStyle(NSWindowToolbarStyle::Unified);

        let _ = self.views.set(Views { window: window.retain(), content, table, detail, options, launch, error, restart, overlay, overlay_text });
    }

    /// `.searchable`: the toolbar's search field.
    pub(crate) fn toolbar_item(&self, app: &App, identifier: &NSString) -> Option<Retained<NSToolbarItem>> {
        if identifier.to_string() != SEARCH {
            return None;
        }
        let item = NSSearchToolbarItem::initWithItemIdentifier(NSSearchToolbarItem::alloc(app.mtm), identifier);
        let field = item.searchField();
        field.setStringValue(&ns(&self.search.borrow()));
        app.kit(&self.search_targets).bind(&field, Act::Search);
        Some(Retained::into_super(item))
    }

    pub(crate) fn draw(&self, app: &App, inputs: &Inputs, force: bool) {
        let Some(views) = self.views.get() else { return };
        let rows = views::filter(&inputs.state.games, &self.search.borrow());
        if force || rows != *self.rows.borrow() {
            *self.rows.borrow_mut() = rows.clone();
            self.reload(app);
        }

        let detail = views::detail(&rows, self.selection.get());
        if force || self.drawn_detail.borrow().as_ref() != Some(&detail) {
            self.draw_detail(app, views, detail.as_ref());
            *self.drawn_detail.borrow_mut() = Some(detail);
            // SwiftUI grows the window to the options' width; at least 640 by 360.
            let needed = views.content.fittingSize().width.max(640.0);
            views.window.setContentMinSize(NSSize::new(needed, 360.0));
            let current = views.content.frame().size;
            if needed > current.width {
                views.window.setContentSize(NSSize::new(needed, current.height.max(360.0)));
            }
        }

        let bars = [inputs.state.error_message.clone(), views::restart_bar(&inputs.status), inputs.state.app_info_error.clone()];
        if force || self.drawn_bars.borrow().as_ref() != Some(&bars) {
            self.bar_targets.clear();
            let kit = app.kit(&self.bar_targets);
            let room = views.table.frame().size.width.max(620.0) - 140.0;
            let [error, restart, unreadable] = &bars;
            views.error.setHidden(error.is_none());
            if let Some(error) = error {
                let text = kit.wrapping(error, Some(Tone::Red), room, true);
                replace(&views.error, &[upcast(&text), kit.spacer(), upcast(&kit.button("Dismiss", Act::DismissError, true))]);
            }
            views.restart.setHidden(restart.is_none());
            if let Some(restart) = restart {
                let text = kit.text(restart, Some(Tone::Orange));
                let button = kit.button("Restart Steam", Act::RestartSteam, true);
                replace(&views.restart, &[upcast(&text), kit.spacer(), upcast(&button)]);
            }
            views.overlay.setHidden(unreadable.is_none());
            views.overlay_text.setStringValue(&ns(unreadable.as_deref().unwrap_or("")));
            *self.drawn_bars.borrow_mut() = Some(bars);
        }
    }

    /// Reloads the table and selects the selected game's row again, when it's still shown.
    pub(crate) fn reload(&self, _app: &App) {
        let Some(views) = self.views.get() else { return };
        self.table_targets.clear();
        self.reloading.set(true);
        views.table.reloadData();
        let index = self.selection.get().and_then(|id| self.rows.borrow().iter().position(|row| row.id() == id));
        match index {
            Some(index) => views.table.selectRowIndexes_byExtendingSelection(&NSIndexSet::indexSetWithIndex(index), false),
            None => unsafe { views.table.deselectAll(None) },
        }
        self.reloading.set(false);
    }

    pub(crate) fn selection_changed(&self, app: &App) {
        let Some(views) = self.views.get() else { return };
        if self.reloading.get() {
            return;
        }
        let index = views.table.selectedRow();
        let selection = (index >= 0).then(|| self.rows.borrow().get(index as usize).map(GameRow::id)).flatten();
        self.selection.set(selection);
        let inputs = app.inputs.borrow().clone();
        self.draw(app, &inputs, false);
    }

    /// Selects a game (the preview).
    pub(crate) fn select(&self, app: &App, app_id: Option<u32>) {
        self.selection.set(app_id);
        let inputs = app.inputs.borrow().clone();
        self.draw(app, &inputs, true);
    }

    /// The selected game (the preview's checks).
    pub(crate) fn selected(&self) -> Option<u32> {
        self.selection.get()
    }

    /// Selects a row as a click would (the preview's checks).
    pub(crate) fn select_row(&self, index: usize) {
        if let Some(views) = self.views.get() {
            views.table.selectRowIndexes_byExtendingSelection(&NSIndexSet::indexSetWithIndex(index), false);
        }
    }

    pub(crate) fn cell(&self, app: &App, column: &str, index: usize) -> Option<Retained<NSView>> {
        let row = self.rows.borrow().get(index)?.clone();
        let kit = app.kit(&self.table_targets);
        let mut parts: Vec<Retained<NSView>> = Vec::new();
        match column {
            "game" => {
                parts.push(upcast(&kit.text(&row.name(), None)));
                if row.installed {
                    let installed = kit.text("Installed", Some(Tone::Secondary));
                    installed.setFont(Some(&font(unsafe { NSFontTextStyleCaption1 })));
                    parts.push(upcast(&installed));
                }
            }
            "runsAs" => parts.push(table_cell(&kit, views::runs_as_cell(&row), Act::RunsAs(row.id()))),
            "graphics" => parts.push(table_cell(&kit, views::graphics_cell(&row), Act::Graphics(row.id()))),
            _ => return None,
        }
        parts.push(kit.spacer());
        Some(upcast(&kit.hstack(&parts, 8.0)))
    }

    fn draw_detail(&self, app: &App, views: &Views, row: Option<&GameRow>) {
        views.detail.setHidden(row.is_none());
        self.options_targets.clear();
        let kit = app.kit(&self.options_targets);
        let Some(row) = row else {
            replace(&views.options, &[]);
            replace(&views.launch, &[]);
            *self.drawn_launch.borrow_mut() = None; // a game selected again shows its saved launch options
            return;
        };
        let settings = &row.settings;
        let name = kit.text(&row.name(), None);
        name.setFont(Some(&objc2_app_kit::NSFont::boldSystemFontOfSize(objc2_app_kit::NSFont::systemFontSize())));
        let mut toggles = vec![upcast(&name)];
        for toggle in GameToggle::ALL {
            let check = kit.checkbox(
                toggle.title(),
                toggle.is_on(settings),
                Act::GameToggle(row.id(), toggle),
                toggle.is_enabled(settings),
            );
            if let Some(help) = toggle.help() {
                check.setToolTip(Some(&ns(help)));
            }
            toggles.push(upcast(&check));
        }
        toggles.push(kit.spacer());
        let mut pickers = Vec::new();
        for picker in GamePicker::ALL {
            let popup = kit.popup(
                picker.options(),
                &picker.selection(settings),
                Act::GamePicker(row.id(), picker),
                picker.is_enabled(settings),
            );
            let labeled = kit.hstack(&[upcast(&kit.text(picker.title(), None)), upcast(&popup)], 6.0);
            labeled.setToolTip(Some(&ns(picker.help())));
            popup.setToolTip(Some(&ns(picker.help())));
            pickers.push(upcast(&labeled));
        }
        pickers.push(kit.spacer());
        replace(&views.options, &[upcast(&kit.hstack(&toggles, 16.0)), upcast(&kit.hstack(&pickers, 16.0))]);

        let launch = (row.id(), views::steam_launch_lines(row));
        if self.drawn_launch.borrow().as_ref() != Some(&launch) {
            self.launch_targets.clear();
            let kit = app.kit(&self.launch_targets);
            let field = NSTextField::textFieldWithString(&ns(settings.launch_options.as_deref().unwrap_or("")), app.mtm);
            field.setPlaceholderString(Some(&ns(views::LAUNCH_OPTIONS_PROMPT)));
            field.setToolTip(Some(&ns(views::LAUNCH_OPTIONS_HELP)));
            if let Some(cell) = field.cell() {
                cell.setSendsActionOnEndEditing(true); // saved on Return and when the field is left
            }
            kit.bind(&field, Act::LaunchOptions(row.id()));
            field.setContentHuggingPriority_forOrientation(1.0, NSLayoutConstraintOrientation::Horizontal);
            let line = kit.hstack(&[upcast(&kit.text("Launch options", None)), upcast(&field)], 8.0);
            let room = views.table.frame().size.width.max(620.0) - 20.0;
            let mut parts = vec![upcast(&line)];
            for (text, tone) in &launch.1 {
                parts.push(upcast(&kit.wrapping(text, Some(*tone), room, *tone == Tone::Secondary)));
            }
            replace(&views.launch, &parts);
            fill_width(&views.launch, &line);
            *self.drawn_launch.borrow_mut() = Some(launch);
        }
    }
}

/// A "Runs as" or "Graphics" cell: a picker, or secondary text.
fn table_cell(kit: &Kit, cell: TableCell, act: Act) -> Retained<NSView> {
    match cell {
        TableCell::Picker { options, selected } => {
            let popup = kit.popup(options, &selected, act, true);
            popup.setControlSize(objc2_app_kit::NSControlSize::Small);
            popup.setFont(Some(&objc2_app_kit::NSFont::systemFontOfSize(objc2_app_kit::NSFont::smallSystemFontSize())));
            upcast(&popup)
        }
        TableCell::Text(text) => upcast(&kit.text(&text, Some(Tone::Secondary))),
    }
}

/// The delegate's games calls.
impl App {
    pub(crate) fn games_cell(&self, column: &str, row: usize) -> Option<Retained<NSView>> {
        self.games.cell(self, column, row)
    }

    pub(crate) fn games_selection_changed(&self) {
        self.games.selection_changed(self);
    }

    pub(crate) fn games_toolbar_item(&self, identifier: &NSString) -> Option<Retained<NSToolbarItem>> {
        self.games.toolbar_item(self, identifier)
    }
}
