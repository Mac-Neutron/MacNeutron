/* mn-input-check: an X client for eac-vm/tests/input.sh. A window over the whole screen, focused, printing the key,
 * button and motion events it gets for 40 s: what mn-input replays from the host (MN_INPUT_SELFTEST). */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <xcb/xcb.h>

int main(void) {
    xcb_connection_t *c = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(c)) return 1;
    xcb_screen_t *s = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
    xcb_window_t w = xcb_generate_id(c);
    uint32_t mask = XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS |
                    XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION;
    xcb_create_window(c, XCB_COPY_FROM_PARENT, w, s->root, 0, 0, s->width_in_pixels, s->height_in_pixels, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, s->root_visual, XCB_CW_EVENT_MASK, &mask);
    xcb_map_window(c, w);
    xcb_set_input_focus(c, XCB_INPUT_FOCUS_POINTER_ROOT, w, XCB_CURRENT_TIME);
    xcb_flush(c);
    printf("check: window up\n");
    fflush(stdout);
    time_t end = time(NULL) + 40;
    while (time(NULL) < end) {
        xcb_generic_event_t *e = xcb_poll_for_event(c);
        if (!e) { struct timespec t = {0, 10000000}; nanosleep(&t, NULL); continue; }
        switch (e->response_type & 0x7f) {
        case XCB_KEY_PRESS: printf("key press %u\n", ((xcb_key_press_event_t *)e)->detail); break;
        case XCB_KEY_RELEASE: printf("key release %u\n", ((xcb_key_release_event_t *)e)->detail); break;
        case XCB_BUTTON_PRESS: printf("button press %u\n", ((xcb_button_press_event_t *)e)->detail); break;
        case XCB_BUTTON_RELEASE: printf("button release %u\n", ((xcb_button_release_event_t *)e)->detail); break;
        case XCB_MOTION_NOTIFY: { xcb_motion_notify_event_t *m = (void *)e; printf("motion %d %d\n", m->root_x, m->root_y); break; }
        default: break;
        }
        fflush(stdout);
        free(e);
    }
    return 0;
}
