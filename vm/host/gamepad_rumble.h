// Clean-room MIT CoreHaptics dual-motor rumble controller for MacNeutron VM gamepads.
#ifndef MN_GAMEPAD_RUMBLE_H
#define MN_GAMEPAD_RUMBLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __OBJC__
@class GCController;
#else
typedef struct objc_object GCController;
#endif

void mn_rumble_attach(int slot, GCController *controller);
void mn_rumble_detach(int slot);
void mn_rumble_set(int slot, uint16_t strong_mag, uint16_t weak_mag);
void mn_rumble_stop(int slot);
void mn_rumble_stop_all(void);

#ifdef __cplusplus
}
#endif

#endif // MN_GAMEPAD_RUMBLE_H
