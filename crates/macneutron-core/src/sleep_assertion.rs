//! IOKit display sleep assertion so macOS does not dim or sleep the display during a game launch.

use std::ffi::{c_char, c_void, CString};

const CF_STRING_ENCODING_UTF8: u32 = 0x0800_0100;
const IOPM_ASSERTION_LEVEL_ON: u32 = 255;
const IO_RETURN_SUCCESS: i32 = 0;

#[link(name = "CoreFoundation", kind = "framework")]
extern "C" {
    fn CFStringCreateWithCString(alloc: *const c_void, c_str: *const c_char, encoding: u32) -> *const c_void;
    fn CFRelease(cf: *const c_void);
}

#[link(name = "IOKit", kind = "framework")]
extern "C" {
    fn IOPMAssertionCreateWithName(
        assertion_type: *const c_void,
        level: u32,
        name: *const c_void,
        assertion_id: *mut u32,
    ) -> i32;
    fn IOPMAssertionRelease(assertion_id: u32) -> i32;
}

/// RAII guard holding an IOKit `PreventUserIdleDisplaySleep` power assertion.
#[derive(Debug)]
pub struct DisplaySleepAssertion {
    id: u32,
}

impl DisplaySleepAssertion {
    /// Acquires a `PreventUserIdleDisplaySleep` assertion for `reason`, returning `None` if IOKit refuses it.
    #[must_use]
    pub fn acquire(reason: &str) -> Option<Self> {
        let c_type = CString::new("PreventUserIdleDisplaySleep").ok()?;
        let c_reason = CString::new(reason).ok()?;
        unsafe {
            let cf_type = CFStringCreateWithCString(std::ptr::null(), c_type.as_ptr(), CF_STRING_ENCODING_UTF8);
            if cf_type.is_null() {
                return None;
            }
            let cf_reason = CFStringCreateWithCString(std::ptr::null(), c_reason.as_ptr(), CF_STRING_ENCODING_UTF8);
            if cf_reason.is_null() {
                CFRelease(cf_type);
                return None;
            }
            let mut id: u32 = 0;
            let ret = IOPMAssertionCreateWithName(cf_type, IOPM_ASSERTION_LEVEL_ON, cf_reason, &mut id);
            CFRelease(cf_reason);
            CFRelease(cf_type);
            (ret == IO_RETURN_SUCCESS).then_some(Self { id })
        }
    }

    /// Returns the underlying `IOPMAssertionID` (primarily for tests).
    pub fn id(&self) -> u32 {
        self.id
    }
}

impl Drop for DisplaySleepAssertion {
    fn drop(&mut self) {
        unsafe {
            let _ = IOPMAssertionRelease(self.id);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn acquires_and_releases_display_sleep_assertion() {
        let guard = DisplaySleepAssertion::acquire("MacNeutron unit test").expect("display sleep assertion");
        assert_ne!(guard.id(), 0);
        drop(guard);
    }
}
