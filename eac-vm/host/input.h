/* The guest's keyboard and mouse (input.c). */
#ifndef MN_HOST_INPUT_H
#define MN_HOST_INPUT_H
#include <stdint.h>
#include "libkrun.h"
#include "libkrun_input.h"

#define MN_INPUT_ABS_MAX 32767

/* The virtio-input device, wired to the game window's events. NULL with *err set if libkrun refuses it. */
KrunInputDevice mn_input_device(KrunError *err);
/* An evdev event (type, code, value) for the guest; EV_SYN/SYN_REPORT ends each batch. Any thread. */
void mn_input_post(uint16_t type, uint16_t code, int32_t value);
/* libkrun's event provider: the next queued event, 1; or 0 when there is none. */
int32_t mn_input_next(void *instance, struct krun_input_event *out);

#endif
