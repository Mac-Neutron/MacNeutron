// Main-thread owned CoreHaptics dual-motor rumble implementation (clean-room MIT).
#import "gamepad_rumble.h"
#import <CoreHaptics/CoreHaptics.h>
#import <Foundation/Foundation.h>
#import <GameController/GameController.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MN_RUMBLE_MAX_SLOTS 4
#define MN_RUMBLE_LOOP_SECONDS 30.0

#if !__has_feature(objc_arc)
#define MN_RETAIN(obj) [(obj) retain]
#define MN_RELEASE(obj) do { if ((obj) != nil) { [(obj) release]; (obj) = nil; } } while (0)
#else
#define MN_RETAIN(obj) (obj)
#define MN_RELEASE(obj) do { (obj) = nil; } while (0)
#endif

typedef struct {
    CHHapticEngine *engine;
    id<CHHapticAdvancedPatternPlayer> player;
    GCHapticsLocality locality;
    float sharpness;
    bool engine_running;
    bool playing;
    uint64_t gen;
} mn_rumble_motor_t;

typedef struct {
    GCController *controller;
    bool initialized;
    bool dual;
    mn_rumble_motor_t left;
    mn_rumble_motor_t right;
} mn_rumble_slot_t;

static mn_rumble_slot_t g_rumble_slots[MN_RUMBLE_MAX_SLOTS];

static void motor_release_player(mn_rumble_motor_t *m) {
    if (m->player != nil) {
        [m->player stopAtTime:CHHapticTimeImmediate error:nil];
        MN_RELEASE(m->player);
    }
    m->playing = false;
}

static void motor_stop_engine(mn_rumble_motor_t *m, bool release_engine) {
    m->gen++;
    motor_release_player(m);
    if (m->engine != nil) {
        if (m->engine_running) {
            [m->engine stopWithCompletionHandler:nil];
        }
        m->engine_running = false;
        if (release_engine) {
            MN_RELEASE(m->engine);
        }
    }
}

static void handle_engine_stopped_or_reset(int slot_idx, int motor_idx, uint64_t gen) {
    if (slot_idx < 0 || slot_idx >= MN_RUMBLE_MAX_SLOTS) {
        return;
    }
    mn_rumble_slot_t *s = &g_rumble_slots[slot_idx];
    mn_rumble_motor_t *m = (motor_idx == 0) ? &s->left : &s->right;
    if (m->gen != gen) {
        return;
    }
    m->engine_running = false;
    m->playing = false;
    MN_RELEASE(m->player);
}

static bool motor_ensure_engine(int slot_idx, int motor_idx, mn_rumble_motor_t *m, GCController *controller) {
    if (controller == nil || controller.haptics == nil) {
        return false;
    }
    if (m->engine == nil) {
        CHHapticEngine *eng = [controller.haptics createEngineWithLocality:m->locality];
        if (eng == nil) {
            return false;
        }
        m->engine = MN_RETAIN(eng);
    }
    if (!m->engine_running) {
        /* Re-install handlers with current m->gen before starting so mn_rumble_stop() cannot leave stale gen blocks. */
        uint64_t gen = m->gen;
        m->engine.stoppedHandler = ^(CHHapticEngineStoppedReason reason) {
            (void)reason;
            dispatch_async(dispatch_get_main_queue(), ^{
                handle_engine_stopped_or_reset(slot_idx, motor_idx, gen);
            });
        };
        m->engine.resetHandler = ^{
            dispatch_async(dispatch_get_main_queue(), ^{
                handle_engine_stopped_or_reset(slot_idx, motor_idx, gen);
            });
        };
        NSError *err = nil;
        if (![m->engine startAndReturnError:&err]) {
            MN_RELEASE(m->player);
            m->playing = false;
            return false;
        }
        m->engine_running = true;
    }
    if (m->player == nil) {
        CHHapticEventParameter *p_int =
            [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity
                                                          value:1.0f];
        CHHapticEventParameter *p_sharp =
            [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness
                                                          value:m->sharpness];
        CHHapticEvent *evt =
            [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous
                                          parameters:@[p_int, p_sharp]
                                        relativeTime:0.0
                                            duration:MN_RUMBLE_LOOP_SECONDS];
        NSError *err = nil;
        CHHapticPattern *pat = [[CHHapticPattern alloc] initWithEvents:@[evt] parameters:@[] error:&err];
        id<CHHapticAdvancedPatternPlayer> player = nil;
        if (pat != nil) {
            player = [m->engine createAdvancedPlayerWithPattern:pat error:&err];
        }
#if !__has_feature(objc_arc)
        [p_int release];
        [p_sharp release];
        [evt release];
        [pat release];
#endif
        if (player == nil) {
            return false;
        }
        player.loopEnabled = YES;
        player.loopEnd = MN_RUMBLE_LOOP_SECONDS;
        m->player = MN_RETAIN(player);
        m->playing = false;
    }
    return true;
}

static void motor_start_with_retry(int slot_idx, int motor_idx, mn_rumble_motor_t *m,
                                   GCController *controller, CHHapticDynamicParameter *dyn) {
    NSError *err = nil;
    if ([m->player startAtTime:CHHapticTimeImmediate error:&err]) {
        m->playing = true;
        [m->player sendParameters:@[dyn] atTime:CHHapticTimeImmediate error:nil];
        return;
    }
    m->engine_running = false;
    motor_release_player(m);
    if (motor_ensure_engine(slot_idx, motor_idx, m, controller) &&
        [m->player startAtTime:CHHapticTimeImmediate error:nil]) {
        m->playing = true;
        [m->player sendParameters:@[dyn] atTime:CHHapticTimeImmediate error:nil];
    }
}

static void motor_apply(int slot_idx, int motor_idx, mn_rumble_motor_t *m, GCController *controller, float intensity) {
    if (intensity <= 0.0f) {
        if (m->playing && m->player != nil) {
            [m->player stopAtTime:CHHapticTimeImmediate error:nil];
            m->playing = false;
        }
        return;
    }
    if (intensity > 1.0f) {
        intensity = 1.0f;
    }
    if (!motor_ensure_engine(slot_idx, motor_idx, m, controller)) {
        return;
    }

    CHHapticDynamicParameter *dyn =
        [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticIntensityControl
                                                        value:intensity
                                                 relativeTime:0.0];
    if (!m->playing) {
        motor_start_with_retry(slot_idx, motor_idx, m, controller, dyn);
    } else {
        NSError *err = nil;
        if (![m->player sendParameters:@[dyn] atTime:CHHapticTimeImmediate error:&err]) {
            m->playing = false;
            motor_start_with_retry(slot_idx, motor_idx, m, controller, dyn);
        }
    }
#if !__has_feature(objc_arc)
    [dyn release];
#endif
}

static void slot_configure_localities(mn_rumble_slot_t *s) {
    if (s->initialized || s->controller == nil || s->controller.haptics == nil) {
        return;
    }
    NSSet<GCHapticsLocality> *locs = s->controller.haptics.supportedLocalities;
    if ([locs containsObject:GCHapticsLocalityLeftHandle] &&
        [locs containsObject:GCHapticsLocalityRightHandle]) {
        s->dual = true;
        s->left.locality = GCHapticsLocalityLeftHandle;
        s->left.sharpness = 0.2f;
        s->right.locality = GCHapticsLocalityRightHandle;
        s->right.sharpness = 0.8f;
    } else {
        s->dual = false;
        s->left.locality = GCHapticsLocalityDefault;
        s->left.sharpness = 0.5f;
        s->right.locality = nil;
        s->right.sharpness = 0.0f;
    }
    s->initialized = true;
}

void mn_rumble_attach(int slot, GCController *controller) {
    if (slot < 0 || slot >= MN_RUMBLE_MAX_SLOTS) {
        return;
    }
    mn_rumble_detach(slot);
    if (controller == nil) {
        return;
    }
    @autoreleasepool {
        mn_rumble_slot_t *s = &g_rumble_slots[slot];
        s->controller = MN_RETAIN(controller);
        s->initialized = false;
        s->dual = false;
    }
}

void mn_rumble_detach(int slot) {
    if (slot < 0 || slot >= MN_RUMBLE_MAX_SLOTS) {
        return;
    }
    @autoreleasepool {
        mn_rumble_slot_t *s = &g_rumble_slots[slot];
        motor_stop_engine(&s->left, true);
        motor_stop_engine(&s->right, true);
        MN_RELEASE(s->controller);
        s->initialized = false;
        s->dual = false;
    }
}

void mn_rumble_set(int slot, uint16_t strong_mag, uint16_t weak_mag) {
    if (slot < 0 || slot >= MN_RUMBLE_MAX_SLOTS) {
        return;
    }
    mn_rumble_slot_t *s = &g_rumble_slots[slot];
    if (s->controller == nil || s->controller.haptics == nil) {
        return;
    }
    @autoreleasepool {
        float strong = (float)strong_mag / 65535.0f;
        float weak = (float)weak_mag / 65535.0f;
        if (strong_mag == 0 && weak_mag == 0) {
            if (!s->initialized) {
                return;
            }
            motor_apply(slot, 0, &s->left, s->controller, 0.0f);
            if (s->dual) {
                motor_apply(slot, 1, &s->right, s->controller, 0.0f);
            }
            return;
        }
        slot_configure_localities(s);
        if (!s->initialized) {
            return;
        }
        if (s->dual) {
            motor_apply(slot, 0, &s->left, s->controller, strong);
            motor_apply(slot, 1, &s->right, s->controller, weak);
        } else {
            motor_apply(slot, 0, &s->left, s->controller, fmaxf(strong, weak));
        }
    }
}

void mn_rumble_stop(int slot) {
    if (slot < 0 || slot >= MN_RUMBLE_MAX_SLOTS) {
        return;
    }
    @autoreleasepool {
        mn_rumble_slot_t *s = &g_rumble_slots[slot];
        motor_stop_engine(&s->left, false);
        motor_stop_engine(&s->right, false);
    }
}

void mn_rumble_stop_all(void) {
    for (int i = 0; i < MN_RUMBLE_MAX_SLOTS; i++) {
        mn_rumble_stop(i);
    }
}
