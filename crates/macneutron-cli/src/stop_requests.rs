//! Steam's Stop on macOS asks the processes it tracks to quit, as apps (NSRunningApplication's terminate, which reaches
//! only one whose event loop runs): the launcher waits as one, an accessory (no Dock icon), its launch on another
//! thread. A quit request ends a microVM launch's VM ([`crate::quit_requested`]); the launcher exits when the launch
//! returns. (StopRequests.swift)

use objc2::rc::Retained;
use objc2::runtime::ProtocolObject;
use objc2::{define_class, msg_send, MainThreadOnly};
use objc2_app_kit::{NSApplication, NSApplicationActivationPolicy, NSApplicationDelegate, NSApplicationTerminateReply};
use objc2_foundation::{MainThreadMarker, NSObject, NSObjectProtocol};

define_class!(
    // SAFETY: NSObject has no subclassing requirements and Delegate has no Drop.
    #[unsafe(super = NSObject)]
    #[thread_kind = MainThreadOnly]
    struct Delegate;

    unsafe impl NSObjectProtocol for Delegate {}

    unsafe impl NSApplicationDelegate for Delegate {
        #[unsafe(method(applicationShouldTerminate:))]
        fn should_terminate(&self, _sender: &NSApplication) -> NSApplicationTerminateReply {
            crate::quit_requested();
            NSApplicationTerminateReply::TerminateCancel
        }
    }
);

pub fn run(launch: impl FnOnce() -> i32 + Send + 'static) -> ! {
    let mtm = MainThreadMarker::new().expect("the launcher's event loop runs on the main thread");
    // Before any other thread starts, so every thread (the main one included) leaves Steam's SIGTERM and Ctrl-C to
    // the launch's sigwait thread.
    crate::block_termination_signals();
    let app = NSApplication::sharedApplication(mtm);
    app.setActivationPolicy(NSApplicationActivationPolicy::Accessory);
    let delegate: Retained<Delegate> = unsafe { msg_send![super(Delegate::alloc(mtm).set_ivars(())), init] };
    app.setDelegate(Some(ProtocolObject::from_ref(&*delegate)));
    std::thread::spawn(move || std::process::exit(launch()));
    app.run();
    std::process::exit(0)
}
