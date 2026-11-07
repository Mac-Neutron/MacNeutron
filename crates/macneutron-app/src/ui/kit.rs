//! The two Objective-C classes the AppKit layer defines (a control's target and the one delegate), and the small
//! builders the windows are drawn with.

use std::cell::RefCell;

use objc2::rc::Retained;
use objc2::runtime::{AnyObject, NSObject, NSObjectProtocol};
use objc2::{define_class, msg_send, sel, DefinedClass, MainThreadMarker, MainThreadOnly, Message};
use objc2_app_kit::{
    NSApplicationDelegate, NSBox, NSBoxType, NSButton, NSColor, NSControlSize, NSControlTextEditingDelegate, NSFont,
    NSFontTextStyle, NSImage, NSImageSymbolConfiguration, NSImageView, NSLayoutAttribute, NSLayoutConstraintOrientation,
    NSMenu, NSMenuDelegate, NSPopUpButton, NSProgressIndicator, NSProgressIndicatorStyle,
    NSStackView, NSSwitch, NSTableColumn, NSTableView, NSTableViewDataSource, NSTableViewDelegate, NSTextField,
    NSTitlePosition, NSToolbar, NSToolbarDelegate, NSToolbarItem, NSToolbarItemIdentifier, NSUserInterfaceLayoutOrientation,
    NSView, NSWindowDelegate,
};
use objc2_foundation::{NSArray, NSDictionary, NSEdgeInsets, NSInteger, NSNotification, NSRect, NSString};

use crate::views::{Act, Tone};

define_class!(
    /// A control's target: performs its `Act` with the control as the sender.
    #[unsafe(super = NSObject)]
    #[thread_kind = MainThreadOnly]
    #[ivars = Act]
    pub struct Target;

    impl Target {
        #[unsafe(method(fire:))]
        fn fire(&self, sender: &AnyObject) {
            // Kept alive through the action: a redraw it causes drops the region's targets, this one among them.
            let _alive = self.retain();
            super::perform(&self.ivars().clone(), sender);
        }
    }
);

impl Target {
    fn new(act: Act, mtm: MainThreadMarker) -> Retained<Self> {
        let this = Self::alloc(mtm).set_ivars(act);
        unsafe { msg_send![super(this), init] }
    }
}

define_class!(
    /// The application's, status menu's, games table's, toolbar's and windows' delegate: it hands each call to the
    /// app (`ui::App`).
    #[unsafe(super = NSObject)]
    #[thread_kind = MainThreadOnly]
    pub struct Delegate;

    unsafe impl NSObjectProtocol for Delegate {}

    unsafe impl NSApplicationDelegate for Delegate {
        #[unsafe(method(applicationDidFinishLaunching:))]
        fn did_finish_launching(&self, _notification: &NSNotification) {
            super::with_app(|app| app.did_finish_launching());
        }

        #[unsafe(method(applicationWillTerminate:))]
        fn will_terminate(&self, _notification: &NSNotification) {
            super::with_app(|app| app.model.will_terminate());
        }
    }

    unsafe impl NSMenuDelegate for Delegate {
        #[unsafe(method(menuNeedsUpdate:))]
        fn menu_needs_update(&self, menu: &NSMenu) {
            super::with_app(|app| app.fill_menu(menu));
        }
    }

    unsafe impl NSControlTextEditingDelegate for Delegate {}

    unsafe impl NSTableViewDataSource for Delegate {
        #[unsafe(method(numberOfRowsInTableView:))]
        fn number_of_rows(&self, _table: &NSTableView) -> NSInteger {
            super::with_app(|app| app.games.rows.borrow().len() as NSInteger).unwrap_or(0)
        }
    }

    unsafe impl NSTableViewDelegate for Delegate {
        #[unsafe(method_id(tableView:viewForTableColumn:row:))]
        fn view_for(&self, _table: &NSTableView, column: Option<&NSTableColumn>, row: NSInteger) -> Option<Retained<NSView>> {
            let column = column.map(|column| column.identifier().to_string()).unwrap_or_default();
            super::with_app(|app| app.games_cell(&column, row as usize)).flatten()
        }

        #[unsafe(method(tableViewSelectionDidChange:))]
        fn selection_did_change(&self, _notification: &NSNotification) {
            super::with_app(|app| app.games_selection_changed());
        }
    }

    unsafe impl NSToolbarDelegate for Delegate {
        #[unsafe(method_id(toolbar:itemForItemIdentifier:willBeInsertedIntoToolbar:))]
        fn toolbar_item(&self, _toolbar: &NSToolbar, identifier: &NSToolbarItemIdentifier, _inserted: bool) -> Option<Retained<NSToolbarItem>> {
            super::with_app(|app| app.games_toolbar_item(identifier)).flatten()
        }

        #[unsafe(method_id(toolbarDefaultItemIdentifiers:))]
        fn default_items(&self, _toolbar: &NSToolbar) -> Retained<NSArray<NSToolbarItemIdentifier>> {
            super::games_toolbar_items()
        }

        #[unsafe(method_id(toolbarAllowedItemIdentifiers:))]
        fn allowed_items(&self, _toolbar: &NSToolbar) -> Retained<NSArray<NSToolbarItemIdentifier>> {
            super::games_toolbar_items()
        }
    }

    unsafe impl NSWindowDelegate for Delegate {
        #[unsafe(method(windowWillClose:))]
        fn window_will_close(&self, notification: &NSNotification) {
            super::with_app(|app| app.window_will_close(notification));
        }
    }
);

impl Delegate {
    pub fn new(mtm: MainThreadMarker) -> Retained<Self> {
        let this = Self::alloc(mtm).set_ivars(());
        unsafe { msg_send![super(this), init] }
    }
}

/// The targets of one region's controls: AppKit holds targets weakly, so they live here until the region is redrawn.
#[derive(Default)]
pub struct Targets(RefCell<Vec<Retained<Target>>>);

impl Targets {
    pub fn clear(&self) {
        self.0.borrow_mut().clear();
    }
}

/// A toggle's state, a picker's choice, a field's text: what `perform` reads from the sender.
pub fn is_on(sender: &AnyObject) -> bool {
    let state: NSInteger = unsafe { msg_send![sender, state] };
    state != 0
}

pub fn selected_index(sender: &AnyObject) -> usize {
    let index: NSInteger = unsafe { msg_send![sender, indexOfSelectedItem] };
    index.max(0) as usize
}

pub fn string_value(sender: &AnyObject) -> String {
    let value: Retained<NSString> = unsafe { msg_send![sender, stringValue] };
    value.to_string()
}

pub fn ns(text: &str) -> Retained<NSString> {
    NSString::from_str(text)
}

/// `.foregroundStyle`'s colors.
pub fn color(tone: Tone) -> Retained<NSColor> {
    match tone {
        Tone::Secondary => NSColor::secondaryLabelColor(),
        Tone::Orange => NSColor::systemOrangeColor(),
        Tone::Red => NSColor::systemRedColor(),
    }
}

/// A SwiftUI font style (`.title2`, `.headline`, `.callout`…).
pub fn font(style: &NSFontTextStyle) -> Retained<NSFont> {
    unsafe { NSFont::preferredFontForTextStyle_options(style, &NSDictionary::new()) }
}

/// Builds controls whose targets go into `targets`.
pub struct Kit<'a> {
    pub mtm: MainThreadMarker,
    pub targets: &'a Targets,
}

impl Kit<'_> {
    fn target(&self, act: Act) -> Retained<Target> {
        let target = Target::new(act, self.mtm);
        self.targets.0.borrow_mut().push(target.clone());
        target
    }

    /// Hooks a control (or menu item) up to `act`.
    pub fn bind(&self, control: &AnyObject, act: Act) {
        let target = self.target(act);
        unsafe {
            let _: () = msg_send![control, setTarget: &*target];
            let _: () = msg_send![control, setAction: sel!(fire:)];
        }
    }

    /// A one-line label.
    pub fn text(&self, text: &str, tone: Option<Tone>) -> Retained<NSTextField> {
        let field = NSTextField::labelWithString(&ns(text), self.mtm);
        if let Some(tone) = tone {
            field.setTextColor(Some(&color(tone)));
        }
        field
    }

    /// A label that wraps at `width`; `selectable` for `.textSelection(.enabled)`.
    pub fn wrapping(&self, text: &str, tone: Option<Tone>, width: f64, selectable: bool) -> Retained<NSTextField> {
        let field = NSTextField::wrappingLabelWithString(&ns(text), self.mtm);
        if let Some(tone) = tone {
            field.setTextColor(Some(&color(tone)));
        }
        field.setSelectable(selectable);
        field.setPreferredMaxLayoutWidth(width);
        field
    }

    pub fn button(&self, title: &str, act: Act, enabled: bool) -> Retained<NSButton> {
        let target = self.target(act);
        let button = unsafe { NSButton::buttonWithTitle_target_action(&ns(title), Some(&target), Some(sel!(fire:)), self.mtm) };
        button.setEnabled(enabled);
        button
    }

    pub fn checkbox(&self, title: &str, on: bool, act: Act, enabled: bool) -> Retained<NSButton> {
        let target = self.target(act);
        let button =
            unsafe { NSButton::checkboxWithTitle_target_action(&ns(title), Some(&target), Some(sel!(fire:)), self.mtm) };
        button.setState(on as NSInteger);
        button.setEnabled(enabled);
        button
    }

    pub fn switch(&self, on: bool, act: Act, enabled: bool) -> Retained<NSSwitch> {
        let switch = NSSwitch::new(self.mtm);
        switch.setState(on as NSInteger);
        switch.setControlSize(NSControlSize::Small);
        switch.setEnabled(enabled);
        self.bind(&switch, act);
        switch
    }

    /// A pop-up button of `options` (tag, title) showing `selected`.
    pub fn popup(&self, options: &[(&str, &str)], selected: &str, act: Act, enabled: bool) -> Retained<NSPopUpButton> {
        let popup = NSPopUpButton::initWithFrame_pullsDown(NSPopUpButton::alloc(self.mtm), NSRect::ZERO, false);
        for (_, title) in options {
            popup.addItemWithTitle(&ns(title));
        }
        popup.selectItemAtIndex(options.iter().position(|(tag, _)| *tag == selected).unwrap_or(0) as NSInteger);
        popup.setEnabled(enabled);
        self.bind(&popup, act);
        popup
    }

    /// A small spinning `ProgressView`.
    pub fn spinner(&self) -> Retained<NSProgressIndicator> {
        let spinner = NSProgressIndicator::new(self.mtm);
        spinner.setStyle(NSProgressIndicatorStyle::Spinning);
        spinner.setControlSize(NSControlSize::Small);
        spinner.setIndeterminate(true);
        unsafe { spinner.startAnimation(None) };
        spinner
    }

    /// An SF Symbol at a font style's size, tinted.
    pub fn symbol(&self, name: &str, style: &NSFontTextStyle, tint: Option<&NSColor>) -> Retained<NSImageView> {
        let image = NSImage::imageWithSystemSymbolName_accessibilityDescription(&ns(name), None)
            .and_then(|image| {
                let configuration = NSImageSymbolConfiguration::configurationWithTextStyle_scale(
                    style,
                    objc2_app_kit::NSImageSymbolScale::Medium,
                );
                image.imageWithSymbolConfiguration(&configuration)
            })
            .unwrap_or_default();
        let view = NSImageView::imageViewWithImage(&image, self.mtm);
        view.setContentTintColor(tint);
        view.setContentHuggingPriority_forOrientation(750.0, NSLayoutConstraintOrientation::Horizontal);
        view
    }

    pub fn stack(&self, views: &[Retained<NSView>], vertical: bool, spacing: f64, insets: NSEdgeInsets) -> Retained<NSStackView> {
        let array = NSArray::from_retained_slice(views);
        let stack = NSStackView::stackViewWithViews(&array, self.mtm);
        if vertical {
            stack.setOrientation(NSUserInterfaceLayoutOrientation::Vertical);
            stack.setAlignment(NSLayoutAttribute::Leading);
        } else {
            stack.setOrientation(NSUserInterfaceLayoutOrientation::Horizontal);
            stack.setAlignment(NSLayoutAttribute::CenterY);
        }
        stack.setSpacing(spacing);
        stack.setEdgeInsets(insets);
        stack
    }

    pub fn hstack(&self, views: &[Retained<NSView>], spacing: f64) -> Retained<NSStackView> {
        self.stack(views, false, spacing, insets(0.0))
    }

    pub fn vstack(&self, views: &[Retained<NSView>], spacing: f64) -> Retained<NSStackView> {
        self.stack(views, true, spacing, insets(0.0))
    }

    /// `Spacer()`: takes the stack's extra room.
    pub fn spacer(&self) -> Retained<NSView> {
        let view = NSView::new(self.mtm);
        view.setContentHuggingPriority_forOrientation(1.0, NSLayoutConstraintOrientation::Horizontal);
        view.setContentCompressionResistancePriority_forOrientation(1.0, NSLayoutConstraintOrientation::Horizontal);
        view
    }

    /// A box around `content`: a rounded border (Setup's steps) or a grouped form's section.
    pub fn group(&self, content: &NSView, border: &NSColor, fill: Option<&NSColor>, radius: f64) -> Retained<NSBox> {
        let group = NSBox::new(self.mtm);
        group.setBoxType(NSBoxType::Custom);
        group.setTitlePosition(NSTitlePosition::NoTitle);
        group.setCornerRadius(radius);
        group.setBorderWidth(1.0);
        group.setBorderColor(border);
        group.setFillColor(fill.map_or_else(NSColor::clearColor, |fill| fill.retain()).as_ref());
        group.setContentViewMargins(objc2_foundation::NSSize::new(0.0, 0.0));
        group.setContentView(Some(content));
        // NSBox sizes its content view to itself; pinned instead, the box takes the content's size.
        content.setTranslatesAutoresizingMaskIntoConstraints(false);
        for constraint in [
            content.leadingAnchor().constraintEqualToAnchor(&group.leadingAnchor()),
            content.trailingAnchor().constraintEqualToAnchor(&group.trailingAnchor()),
            content.topAnchor().constraintEqualToAnchor(&group.topAnchor()),
            content.bottomAnchor().constraintEqualToAnchor(&group.bottomAnchor()),
        ] {
            constraint.setActive(true);
        }
        group
    }

    /// A one-point line between a grouped form's rows.
    pub fn separator(&self) -> Retained<NSBox> {
        let line = NSBox::new(self.mtm);
        line.setBoxType(NSBoxType::Separator);
        line
    }
}

pub fn insets(all: f64) -> NSEdgeInsets {
    NSEdgeInsets { top: all, left: all, bottom: all, right: all }
}

/// A view as an `NSView`, for the stacks.
pub fn upcast(view: &NSView) -> Retained<NSView> {
    view.retain()
}

