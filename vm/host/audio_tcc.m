/* Nonblocking macOS TCC microphone authorization and CoreAudio input device description helpers. */
#import "audio_tcc.h"
#import <AVFoundation/AVFoundation.h>
#import <CoreAudio/CoreAudio.h>
#import <Foundation/Foundation.h>
#include <stdatomic.h>
#include <stdio.h>

int mn_audio_mic_auth_status(void) {
    static _Atomic int requested = 0;
    @autoreleasepool {
        AVAuthorizationStatus st = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
        switch (st) {
        case AVAuthorizationStatusAuthorized:
            return MN_MIC_AUTHORIZED;
        case AVAuthorizationStatusNotDetermined:
            if (!atomic_exchange_explicit(&requested, 1, memory_order_acq_rel)) {
                [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                                         completionHandler:^(BOOL granted) {
                                             (void)granted;
                                         }];
            }
            return MN_MIC_NOT_DETERMINED;
        case AVAuthorizationStatusDenied:
        case AVAuthorizationStatusRestricted:
        default:
            return MN_MIC_DENIED;
        }
    }
}

static int copy_cfstring_prop(AudioDeviceID device_id, AudioObjectPropertySelector sel, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return 0;
    out[0] = '\0';
    CFStringRef str = NULL;
    UInt32 sz = sizeof(str);
    AudioObjectPropertyAddress addr = {
        sel,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    if (AudioObjectGetPropertyData((AudioObjectID)device_id, &addr, 0, NULL, &sz, &str) != noErr || !str) return 0;
    Boolean ok = CFStringGetCString(str, out, (CFIndex)out_sz, kCFStringEncodingUTF8);
    CFRelease(str);
    if (!ok) {
        out[0] = '\0';
        return 0;
    }
    return 1;
}

void mn_audio_describe_input_device(uint32_t device_id, char *buf, size_t len) {
    if (!buf || len == 0) return;
    if (device_id == kAudioObjectUnknown) {
        snprintf(buf, len, "unknown");
        return;
    }
    char name[128] = "";
    char uid[128] = "";
    int has_name = copy_cfstring_prop((AudioDeviceID)device_id, kAudioObjectPropertyName, name, sizeof(name));
    int has_uid = copy_cfstring_prop((AudioDeviceID)device_id, kAudioDevicePropertyDeviceUID, uid, sizeof(uid));
    if (has_name && has_uid) {
        snprintf(buf, len, "%s (%s)", name, uid);
    } else if (has_name) {
        snprintf(buf, len, "%s", name);
    } else if (has_uid) {
        snprintf(buf, len, "%s", uid);
    } else {
        snprintf(buf, len, "device-%u", (unsigned)device_id);
    }
}
