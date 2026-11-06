//! What the app supplies the models from macOS: the menu's global hot key (MenuHotKey.swift), the login item
//! (`SMAppService.mainApp`), `UserDefaults.standard`, and quitting through NSApplication.

use std::ffi::c_void;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use dispatch2::DispatchQueue;
use objc2::rc::Retained;
use objc2::runtime::AnyObject;
use objc2::MainThreadMarker;
use objc2_app_kit::NSApplication;
use objc2_foundation::{NSDate, NSNumber, NSString, NSUserDefaults};
use objc2_service_management::{SMAppService, SMAppServiceStatus};

use crate::app_model::{LoginItem, LoginItemStatus};
use crate::update_model::Defaults;

// MARK: The menu hot key

/// The key combo that opens MacNeutron's menu: a global Shift-F9, so a fullscreen game can bring the menu up without
/// reaching for the menu bar. A Carbon hot key rather than an `NSEvent` global monitor: it needs no Accessibility grant.
mod carbon {
    use std::ffi::c_void;

    pub type OSStatus = i32;
    pub type EventTargetRef = *mut c_void;
    pub type EventRef = *mut c_void;
    pub type EventHandlerCallRef = *mut c_void;
    pub type EventHandlerRef = *mut c_void;
    pub type EventHotKeyRef = *mut c_void;
    pub type Handler = extern "C" fn(EventHandlerCallRef, EventRef, *mut c_void) -> OSStatus;

    #[repr(C)]
    pub struct EventTypeSpec {
        pub event_class: u32,
        pub event_kind: u32,
    }

    #[repr(C)]
    #[derive(Default)]
    pub struct EventHotKeyID {
        pub signature: u32,
        pub id: u32,
    }

    pub const NO_ERR: OSStatus = 0;
    /// `kVK_F9` (Events.h).
    pub const KVK_F9: u32 = 0x65;
    /// `shiftKey` (Events.h): 1 << shiftKeyBit (9).
    pub const SHIFT_KEY: u32 = 1 << 9;
    /// `kEventClassKeyboard`: 'keyb'.
    pub const EVENT_CLASS_KEYBOARD: u32 = u32::from_be_bytes(*b"keyb");
    /// `kEventHotKeyPressed`.
    pub const EVENT_HOT_KEY_PRESSED: u32 = 5;
    /// `kEventParamDirectObject`: '----'.
    pub const PARAM_DIRECT_OBJECT: u32 = u32::from_be_bytes(*b"----");
    /// `typeEventHotKeyID`: 'hkid'.
    pub const TYPE_EVENT_HOT_KEY_ID: u32 = u32::from_be_bytes(*b"hkid");

    #[link(name = "Carbon", kind = "framework")]
    extern "C" {
        pub fn GetApplicationEventTarget() -> EventTargetRef;
        pub fn InstallEventHandler(
            target: EventTargetRef,
            handler: Handler,
            num_types: usize,
            list: *const EventTypeSpec,
            user_data: *mut c_void,
            out_ref: *mut EventHandlerRef,
        ) -> OSStatus;
        pub fn RegisterEventHotKey(
            key_code: u32,
            modifiers: u32,
            id: EventHotKeyID,
            target: EventTargetRef,
            options: u32,
            out_ref: *mut EventHotKeyRef,
        ) -> OSStatus;
        pub fn GetEventParameter(
            event: EventRef,
            name: u32,
            desired_type: u32,
            actual_type: *mut u32,
            buffer_size: usize,
            actual_size: *mut usize,
            data: *mut c_void,
        ) -> OSStatus;
    }
}

/// 'MNRG', to tell our event from another handler on the application target.
const MENU_HOT_KEY_SIGNATURE: u32 = 0x4D4E5247;

extern "C" fn hot_key_pressed(_call: carbon::EventHandlerCallRef, event: carbon::EventRef, _data: *mut c_void) -> carbon::OSStatus {
    let mut id = carbon::EventHotKeyID::default();
    let found = !event.is_null()
        && unsafe {
            carbon::GetEventParameter(
                event,
                carbon::PARAM_DIRECT_OBJECT,
                carbon::TYPE_EVENT_HOT_KEY_ID,
                std::ptr::null_mut(),
                std::mem::size_of::<carbon::EventHotKeyID>(),
                std::ptr::null_mut(),
                (&mut id as *mut carbon::EventHotKeyID).cast(),
            )
        } == carbon::NO_ERR;
    if found && id.signature == MENU_HOT_KEY_SIGNATURE {
        super::with_app(|app| app.open_menu());
    }
    carbon::NO_ERR
}

/// Installs Shift-F9 once (the app delegate calls it at launch); logs when macOS refuses it.
pub fn install_menu_hot_key() {
    use std::sync::Once;
    static INSTALLED: Once = Once::new();
    INSTALLED.call_once(|| unsafe {
        let target = carbon::GetApplicationEventTarget();
        let spec = carbon::EventTypeSpec { event_class: carbon::EVENT_CLASS_KEYBOARD, event_kind: carbon::EVENT_HOT_KEY_PRESSED };
        let mut handler: carbon::EventHandlerRef = std::ptr::null_mut();
        let handler_err = carbon::InstallEventHandler(target, hot_key_pressed, 1, &spec, std::ptr::null_mut(), &mut handler);
        let mut hot_key: carbon::EventHotKeyRef = std::ptr::null_mut();
        let id = carbon::EventHotKeyID { signature: MENU_HOT_KEY_SIGNATURE, id: 1 };
        let hot_key_err = carbon::RegisterEventHotKey(carbon::KVK_F9, carbon::SHIFT_KEY, id, target, 0, &mut hot_key);
        if handler_err != carbon::NO_ERR || hot_key_err != carbon::NO_ERR {
            macneutron_core::LauncherLog::standard().append(&format!(
                "menu hotkey Shift-F9 could not be installed: handler {handler_err}, hot key {hot_key_err}\n"
            ));
        }
    });
}

// MARK: The login item

fn main_app_service() -> Retained<SMAppService> {
    unsafe { SMAppService::mainAppService() }
}

/// `SMAppService.mainApp`.
pub fn login_item() -> LoginItem {
    let describe = |error: Retained<objc2_foundation::NSError>| error.localizedDescription().to_string();
    LoginItem {
        status: Box::new(|| match unsafe { main_app_service().status() } {
            SMAppServiceStatus::Enabled => LoginItemStatus::Enabled,
            SMAppServiceStatus::RequiresApproval => LoginItemStatus::RequiresApproval,
            SMAppServiceStatus::NotFound => LoginItemStatus::NotFound,
            _ => LoginItemStatus::NotRegistered,
        }),
        register: Box::new(move || unsafe { main_app_service().registerAndReturnError() }.map_err(describe)),
        unregister: Box::new(move || unsafe { main_app_service().unregisterAndReturnError() }.map_err(describe)),
    }
}

pub fn open_login_items() {
    unsafe { SMAppService::openSystemSettingsLoginItems() };
}

// MARK: UserDefaults

/// `UserDefaults.standard`, as UpdateModel keeps its three keys there (a Bool, a Date, a String).
pub struct UserDefaults;

impl Defaults for UserDefaults {
    fn value(&self, key: &str) -> Option<plist::Value> {
        let object: Retained<AnyObject> = NSUserDefaults::standardUserDefaults().objectForKey(&NSString::from_str(key))?;
        if let Some(date) = object.downcast_ref::<NSDate>() {
            let seconds = date.timeIntervalSince1970();
            let time = if seconds >= 0.0 {
                UNIX_EPOCH + Duration::from_secs_f64(seconds)
            } else {
                UNIX_EPOCH - Duration::from_secs_f64(-seconds)
            };
            return Some(plist::Value::Date(time.into()));
        }
        if let Some(string) = object.downcast_ref::<NSString>() {
            return Some(plist::Value::String(string.to_string()));
        }
        // ponytail: every NSNumber reads as a Bool: the only number UpdateModel keeps is checkForUpdates.
        object.downcast_ref::<NSNumber>().map(|number| plist::Value::Boolean(number.boolValue()))
    }

    fn set(&self, key: &str, value: plist::Value) {
        let defaults = NSUserDefaults::standardUserDefaults();
        let key = NSString::from_str(key);
        match value {
            plist::Value::Boolean(on) => defaults.setBool_forKey(on, &key),
            plist::Value::String(string) => unsafe { defaults.setObject_forKey(Some(&NSString::from_str(&string)), &key) },
            plist::Value::Date(date) => {
                let seconds = match SystemTime::from(date).duration_since(UNIX_EPOCH) {
                    Ok(since) => since.as_secs_f64(),
                    Err(before) => -before.duration().as_secs_f64(),
                };
                let date = NSDate::dateWithTimeIntervalSince1970(seconds);
                unsafe { defaults.setObject_forKey(Some(&date), &key) };
            }
            _ => {}
        }
    }
}

/// `NSApplication.terminate`, from any thread (UpdateModel's install calls it off the main thread).
pub fn terminate() {
    DispatchQueue::main().exec_async(|| {
        if let Some(mtm) = MainThreadMarker::new() {
            NSApplication::sharedApplication(mtm).terminate(None);
        }
    });
}
