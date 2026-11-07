//! `macneutron toast <title> <message> [file]` (Toast.swift): the launcher's "please wait" while a game's prefix is
//! prepared or its shaders replayed (AppleScriptNotifier's progress starts it), in place of Wine's old dialog. A glass
//! panel at the top of the screen, over full-screen apps too, that takes no clicks and no focus. Each line on stdin is
//! its new message (the replay's progress); it goes when stdin closes (the launcher is done, or gone) or the file
//! appears (the microVM's guest wrote it).

use std::cell::{OnceCell, RefCell};
use std::io::BufRead;
use std::path::PathBuf;
use std::time::Duration;

use dispatch2::DispatchQueue;
use objc2::rc::Retained;
use objc2::runtime::AnyObject;
use objc2::MainThreadOnly;
use objc2_app_kit::{
    NSAccessibilityAnnouncementKey, NSAccessibilityAnnouncementRequestedNotification,
    NSAccessibilityPostNotificationWithUserInfo, NSAnimatablePropertyContainer, NSAnimationContext, NSApplication,
    NSApplicationActivationPolicy, NSBackingStoreType, NSColor, NSControlSize, NSFont, NSFontWeightSemibold,
    NSGlassEffectView, NSPanel, NSProgressIndicator, NSProgressIndicatorStyle, NSScreen, NSShadow,
    NSStatusWindowLevel, NSTextField, NSView, NSWindowCollectionBehavior, NSWindowStyleMask,
};
use objc2_foundation::{MainThreadMarker, NSDictionary, NSPoint, NSRect, NSSize, NSString};

/// `toast <title> <message> [until]`, as main.swift gates it: title and message required, arguments past `until`
/// ignored. None falls through to the usage error.
pub fn arguments(rest: &[String]) -> Option<(&str, &str, Option<&str>)> {
    match rest {
        [title, message, until @ ..] => Some((title, message, until.first().map(String::as_str))),
        _ => None,
    }
}

/// Each `\n`-terminated line of `input`, as lossy UTF-8; an unterminated last line is dropped, as Swift's read loop
/// dropped it.
pub fn messages(mut input: impl BufRead) -> impl Iterator<Item = String> {
    std::iter::from_fn(move || {
        let mut line = Vec::new();
        match input.read_until(b'\n', &mut line) {
            Ok(_) if line.last() == Some(&b'\n') => {
                line.pop();
                Some(String::from_utf8_lossy(&line).into_owned())
            }
            _ => None,
        }
    })
}

// ToastView's measures.
const CARD_WIDTH: f64 = 340.0;
const INSET_X: f64 = 18.0;
const INSET_Y: f64 = 14.0;
const SPINNER_BOX: f64 = 22.0;
const SPACING: f64 = 14.0;
const LINE_SPACING: f64 = 2.0;
const MARGIN_X: f64 = 24.0;
const MARGIN_TOP: f64 = 10.0;
const MARGIN_BOTTOM: f64 = 30.0; // room for the shadow
const FADE: f64 = 0.3;

struct Views {
    panel: Retained<NSPanel>,
    root: Retained<NSView>,
    card: Retained<NSGlassEffectView>,
    spinner: Retained<NSProgressIndicator>,
    title: Retained<NSTextField>,
    message: Retained<NSTextField>,
}

thread_local! {
    // ponytail: the process's one toast, main thread only (Swift's static panel).
    static VIEWS: OnceCell<Views> = const { OnceCell::new() };
    /// Taken by the first finish; a second one exits at once.
    static SHOWN: RefCell<Option<Retained<NSPanel>>> = const { RefCell::new(None) };
}

pub fn run(title: &str, message: &str, until: Option<&str>) -> ! {
    let mtm = MainThreadMarker::new().expect("the toast runs on the main thread");
    let app = NSApplication::sharedApplication(mtm);
    app.setActivationPolicy(NSApplicationActivationPolicy::Accessory);
    let views = make_views(mtm, title, message);
    let panel = views.panel.clone();
    VIEWS.with(|cell| {
        let _ = cell.set(views);
    });
    layout(mtm, message);
    panel.setAlphaValue(0.0);
    panel.orderFrontRegardless();
    SHOWN.with(|shown| *shown.borrow_mut() = Some(panel.clone()));
    fade(&panel, 1.0);
    announce(&panel, &format!("{title}. {message}"));
    std::thread::spawn(|| {
        for line in messages(std::io::stdin().lock()) {
            DispatchQueue::main().exec_async(move || layout(MainThreadMarker::new().unwrap(), &line));
        }
        DispatchQueue::main().exec_async(finish);
    });
    if let Some(until) = until.map(PathBuf::from) {
        std::thread::spawn(move || loop {
            std::thread::sleep(Duration::from_millis(500));
            if until.exists() {
                DispatchQueue::main().exec_async(finish);
                return;
            }
        });
    }
    app.run();
    std::process::exit(0)
}

fn finish() {
    let Some(panel) = SHOWN.with(|shown| shown.borrow_mut().take()) else { std::process::exit(0) };
    fade(&panel, 0.0);
    std::thread::spawn(|| {
        std::thread::sleep(Duration::from_secs_f64(FADE));
        std::process::exit(0)
    });
}

fn fade(panel: &NSPanel, to: f64) {
    NSAnimationContext::beginGrouping();
    NSAnimationContext::currentContext().setDuration(FADE);
    panel.animator().setAlphaValue(to);
    NSAnimationContext::endGrouping();
}

fn announce(panel: &NSPanel, text: &str) {
    let text = NSString::from_str(text);
    unsafe {
        let info = NSDictionary::<NSString, AnyObject>::from_slices(&[NSAccessibilityAnnouncementKey], &[&*text]);
        let notification = NSAccessibilityAnnouncementRequestedNotification;
        NSAccessibilityPostNotificationWithUserInfo(panel, notification, Some(&info));
    }
}

fn make_views(mtm: MainThreadMarker, title: &str, message: &str) -> Views {
    let panel = unsafe {
        let panel = NSPanel::initWithContentRect_styleMask_backing_defer(
            NSPanel::alloc(mtm),
            NSRect::ZERO,
            NSWindowStyleMask::Borderless | NSWindowStyleMask::NonactivatingPanel,
            NSBackingStoreType::Buffered,
            false,
        );
        panel.setReleasedWhenClosed(false);
        panel
    };
    panel.setOpaque(false);
    panel.setBackgroundColor(Some(&NSColor::clearColor()));
    panel.setHasShadow(false); // the card draws its own, softer
    panel.setLevel(NSStatusWindowLevel);
    panel.setIgnoresMouseEvents(true);
    panel.setCollectionBehavior(
        NSWindowCollectionBehavior::CanJoinAllSpaces
            | NSWindowCollectionBehavior::FullScreenAuxiliary
            | NSWindowCollectionBehavior::Stationary
            | NSWindowCollectionBehavior::IgnoresCycle,
    );

    let root = NSView::new(mtm);
    let shadow = NSShadow::new();
    shadow.setShadowColor(Some(&NSColor::blackColor().colorWithAlphaComponent(0.18)));
    shadow.setShadowBlurRadius(14.0);
    shadow.setShadowOffset(NSSize::new(0.0, -6.0)); // SwiftUI's y: 6 is down
    root.setWantsLayer(true);
    root.setShadow(Some(&shadow));

    let card = NSGlassEffectView::new(mtm);
    card.setCornerRadius(20.0);
    let content = NSView::new(mtm);
    card.setContentView(Some(&content));
    root.addSubview(&card);

    let spinner = NSProgressIndicator::new(mtm);
    spinner.setStyle(NSProgressIndicatorStyle::Spinning);
    spinner.setControlSize(NSControlSize::Small);
    spinner.setIndeterminate(true);
    unsafe { spinner.startAnimation(None) };
    let title = NSTextField::labelWithString(&NSString::from_str(title), mtm);
    title.setFont(Some(&NSFont::systemFontOfSize_weight(13.0, unsafe { NSFontWeightSemibold })));
    title.setTextColor(Some(&NSColor::labelColor()));
    let message = NSTextField::wrappingLabelWithString(&NSString::from_str(message), mtm);
    message.setFont(Some(&NSFont::systemFontOfSize(12.0)));
    message.setTextColor(Some(&NSColor::secondaryLabelColor()));
    for view in [&*spinner as &NSView, &title, &message] {
        content.addSubview(view);
    }
    panel.setContentView(Some(&root));
    Views { panel, root, card, spinner, title, message }
}

/// Shows `message` and fits the panel around it, its top edge where it was (centred at the top of the screen at
/// first): a longer message grows the card downwards, as the SwiftUI view's height followed its text.
fn layout(mtm: MainThreadMarker, message: &str) {
    VIEWS.with(|cell| {
        let Some(v) = cell.get() else { return };
        v.message.setStringValue(&NSString::from_str(message));
        let text_width = CARD_WIDTH - 2.0 * INSET_X - SPINNER_BOX - SPACING;
        let title_height = v.title.intrinsicContentSize().height;
        let message_height = v
            .message
            .cell()
            .map(|cell| cell.cellSizeForBounds(NSRect::new(NSPoint::ZERO, NSSize::new(text_width, 10_000.0))).height)
            .unwrap_or(0.0)
            .ceil();
        let text_height = title_height + LINE_SPACING + message_height;
        let inner = text_height.max(SPINNER_BOX);
        let card_height = inner + 2.0 * INSET_Y;
        let size = NSSize::new(CARD_WIDTH + 2.0 * MARGIN_X, card_height + MARGIN_TOP + MARGIN_BOTTOM);

        let spinner = v.spinner.fittingSize();
        v.spinner.setFrame(NSRect::new(
            NSPoint::new(INSET_X + (SPINNER_BOX - spinner.width) / 2.0, INSET_Y + (inner - spinner.height) / 2.0),
            spinner,
        ));
        let x = INSET_X + SPINNER_BOX + SPACING;
        let bottom = INSET_Y + (inner - text_height) / 2.0;
        v.message.setFrame(NSRect::new(NSPoint::new(x, bottom), NSSize::new(text_width, message_height)));
        v.title.setFrame(NSRect::new(
            NSPoint::new(x, bottom + message_height + LINE_SPACING),
            NSSize::new(text_width, title_height),
        ));
        v.card.setFrame(NSRect::new(NSPoint::new(MARGIN_X, MARGIN_BOTTOM), NSSize::new(CARD_WIDTH, card_height)));
        v.root.setFrame(NSRect::new(NSPoint::ZERO, size));

        let old = v.panel.frame();
        let top_left = if old.size.width > 0.0 {
            NSPoint::new(old.origin.x, old.origin.y + old.size.height)
        } else {
            let area = NSScreen::mainScreen(mtm).map(|screen| screen.visibleFrame()).unwrap_or(NSRect::ZERO);
            NSPoint::new(area.origin.x + area.size.width / 2.0 - size.width / 2.0, area.origin.y + area.size.height)
        };
        v.panel.setFrame_display(NSRect::new(NSPoint::new(top_left.x, top_left.y - size.height), size), true);
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    fn strings(args: &[&str]) -> Vec<String> {
        args.iter().map(|s| s.to_string()).collect()
    }

    #[test]
    fn a_toast_needs_a_title_and_a_message() {
        assert_eq!(arguments(&strings(&[])), None);
        assert_eq!(arguments(&strings(&["T"])), None);
        assert_eq!(arguments(&strings(&["T", "M"])), Some(("T", "M", None)));
        assert_eq!(arguments(&strings(&["T", "M", "/tmp/shown", "extra"])), Some(("T", "M", Some("/tmp/shown"))));
        // main.rs leaves a short one to run: usage
        assert_eq!(crate::run(&strings(&["toast", "T"]), &Default::default(), std::path::Path::new("/x")), 2);
    }

    #[test]
    fn each_finished_line_is_a_message() {
        let all = |input: &[u8]| messages(input).collect::<Vec<_>>();
        assert_eq!(all(b"Loading\nAlmost there\n"), ["Loading", "Almost there"]);
        assert_eq!(all(b"Loading\nhalf a li"), ["Loading"]); // the unterminated tail is dropped
        assert_eq!(all(b"\n\n"), ["", ""]);
        assert_eq!(all(b"caf\xc3\xa9 \xff\n"), ["café \u{fffd}"]);
        assert!(all(b"").is_empty());
    }
}
