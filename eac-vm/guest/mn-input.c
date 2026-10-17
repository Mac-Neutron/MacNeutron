/* mn-input: the guest's half of the game window's input (spec §5.7). Reads the host's virtio-input device ("MacNeutron
 * VM input", eac-vm/host/input.c) and replays it into the X server (Xvfb, which reads no input devices) with XTEST:
 * keys as X key codes (evdev + 8, the evdev rules' mapping), buttons as 1-3 and 8-9, the wheels as buttons 4-7, and
 * motion. An absolute position (the host's 0..32767 scaled to the root window) goes as the relative move from where the
 * pointer is: XTEST's absolute motion gives raw input (what Wine hands a game's mouse look) the position as if it were a
 * delta, which spun cameras at thousands of pixels a move. aarch64, libxcb only (base.erofs has no
 * libXtst).   mn-input [display [capture-file]]   Waits up to 60 s for the device and the display.
 * With a capture file (run-game.sh's /mnt/share/traces/capture) it tells the host when the game takes the mouse: the
 * file is there while the displayed cursor is hidden (XFixes), as Wine hides it for a game's mouse look, and the host
 * then captures the Mac's mouse (eac-vm/host/input.c, the game window's capture).
 * It also stands in for a window manager's focus, which Xvfb has none of: the top-level window under the pointer gets
 * the X input focus (on a click, and on motion at most every 250 ms). Wine turns raw mouse input (XInput2's, what a
 * game's mouse look reads) on only when one of its windows gets FocusIn, which the X server's default focus, PointerRoot,
 * never sends. */
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xfixes.h>
#include <xcb/xtest.h>

#define NAME "MacNeutron VM input"
#define ABS_RANGE 32767

static int open_device(void) {
    for (int tries = 0; tries < 60; tries++, sleep(1))
        for (int i = 0; i < 32; i++) {
            char path[32], name[64] = "";
            snprintf(path, sizeof path, "/dev/input/event%d", i);
            int fd = open(path, O_RDONLY);
            if (fd < 0) continue;
            if (ioctl(fd, EVIOCGNAME(sizeof name - 1), name) >= 0 && !strcmp(name, NAME)) return fd;
            close(fd);
        }
    return -1;
}

static xcb_connection_t *open_display(const char *display) {
    for (int tries = 0; tries < 60; tries++, sleep(1)) {
        xcb_connection_t *c = xcb_connect(display, NULL);
        if (!xcb_connection_has_error(c)) return c;
        xcb_disconnect(c);
    }
    return NULL;
}

static int button_of(int code) {
    switch (code) {
    case BTN_LEFT: return 1;
    case BTN_MIDDLE: return 2;
    case BTN_RIGHT: return 3;
    case BTN_SIDE: return 8;
    case BTN_EXTRA: return 9;
    default: return 0;
    }
}

/* Focus to the top-level window under the pointer, if it isn't there already. */
static void focus_under_pointer(xcb_connection_t *c, xcb_window_t root, xcb_window_t *focused) {
    xcb_query_pointer_reply_t *p = xcb_query_pointer_reply(c, xcb_query_pointer(c, root), NULL);
    if (p && p->child != XCB_NONE && p->child != *focused) {
        xcb_set_input_focus(c, XCB_INPUT_FOCUS_POINTER_ROOT, p->child, XCB_CURRENT_TIME);
        *focused = p->child;
    }
    free(p);
}

/* Whether the cursor on screen shows anything: Wine's hidden cursor is one with no opaque pixel. */
static int cursor_hidden(xcb_connection_t *c) {
    xcb_xfixes_get_cursor_image_reply_t *r = xcb_xfixes_get_cursor_image_reply(c, xcb_xfixes_get_cursor_image(c), NULL);
    if (!r) return 0;
    const uint32_t *px = xcb_xfixes_get_cursor_image_cursor_image(r);
    int n = xcb_xfixes_get_cursor_image_cursor_image_length(r), hidden = 1;
    for (int i = 0; i < n && hidden; i++) hidden = (px[i] >> 24) == 0;
    free(r);
    return hidden;
}

/* The capture file, there while the cursor is hidden; a change goes to the log. */
static void tell_capture(const char *file, int hidden, int *told) {
    if (!file || hidden == *told) return;
    *told = hidden;
    if (hidden) close(open(file, O_WRONLY | O_CREAT, 0644));
    else unlink(file);
    fprintf(stderr, "mn-input: the cursor is %s\n", hidden ? "hidden: capture" : "shown: no capture");
}

static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000;
}

int main(int argc, char **argv) {
    int fd = open_device();
    if (fd < 0) {
        fprintf(stderr, "mn-input: no %s device\n", NAME);
        return 1;
    }
    xcb_connection_t *c = open_display(argc > 1 ? argv[1] : NULL);
    if (!c) {
        fprintf(stderr, "mn-input: no X display\n");
        return 1;
    }
    xcb_screen_t *s = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
    xcb_window_t root = s->root;
    fprintf(stderr, "mn-input: replaying %s into a %ux%u screen\n", NAME, s->width_in_pixels, s->height_in_pixels);
    const char *capture = argc > 2 ? argv[2] : NULL;
    int notify = -1, told = 0;
    if (capture) {
        unlink(capture); /* a run that ended captured */
        const xcb_query_extension_reply_t *x = xcb_get_extension_data(c, &xcb_xfixes_id);
        xcb_xfixes_query_version_reply_t *v = x && x->present
            ? xcb_xfixes_query_version_reply(c, xcb_xfixes_query_version(c, 4, 0), NULL) : NULL;
        if (v) {
            notify = x->first_event + XCB_XFIXES_CURSOR_NOTIFY;
            xcb_xfixes_select_cursor_input(c, root, XCB_XFIXES_CURSOR_NOTIFY_MASK_DISPLAY_CURSOR);
            tell_capture(capture, cursor_hidden(c), &told);
            xcb_flush(c);
        } else {
            fprintf(stderr, "mn-input: no XFixes: the host captures the mouse only on Control+Option\n");
        }
        free(v);
    }
    int ax = -1, ay = -1, dx = 0, dy = 0, abs_moved = 0, pressed = 0;
    xcb_window_t focused = XCB_NONE;
    long focus_checked = 0;
    for (;;) {
        /* The X server's events first (replies read below may have queued some), then wait for either side. */
        for (xcb_generic_event_t *xe; (xe = xcb_poll_for_event(c)); free(xe))
            if ((xe->response_type & 0x7f) == notify) tell_capture(capture, cursor_hidden(c), &told);
        struct pollfd w[2] = {{fd, POLLIN, 0}, {xcb_get_file_descriptor(c), POLLIN, 0}};
        if (poll(w, 2, -1) < 0 && errno != EINTR) break;
        if (!(w[0].revents & (POLLIN | POLLHUP | POLLERR))) {
            if (xcb_connection_has_error(c)) {
                fprintf(stderr, "mn-input: the X display went away\n");
                return 1;
            }
            continue;
        }
        struct input_event ev[64];
        ssize_t n = read(fd, ev, sizeof ev);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        for (size_t i = 0; i < (size_t)n / sizeof *ev; i++) {
            const struct input_event *e = &ev[i];
            if (e->type == EV_KEY) {
                if (e->value == 2) continue; /* the X server repeats keys itself */
                int b = button_of(e->code);
                uint8_t type = b ? (e->value ? XCB_BUTTON_PRESS : XCB_BUTTON_RELEASE)
                                 : (e->value ? XCB_KEY_PRESS : XCB_KEY_RELEASE);
                if (b && e->value) pressed = 1;
                if (b || e->code + 8 < 256)
                    xcb_test_fake_input(c, type, b ? b : e->code + 8, XCB_CURRENT_TIME, root, 0, 0, 0);
            } else if (e->type == EV_REL) {
                if (e->code == REL_X) dx += e->value;
                else if (e->code == REL_Y) dy += e->value;
                else if (e->code == REL_WHEEL || e->code == REL_HWHEEL) {
                    int b = e->code == REL_WHEEL ? (e->value > 0 ? 4 : 5) : (e->value > 0 ? 7 : 6);
                    for (int k = e->value > 0 ? e->value : -e->value; k > 0; k--) {
                        xcb_test_fake_input(c, XCB_BUTTON_PRESS, b, XCB_CURRENT_TIME, root, 0, 0, 0);
                        xcb_test_fake_input(c, XCB_BUTTON_RELEASE, b, XCB_CURRENT_TIME, root, 0, 0, 0);
                    }
                }
            } else if (e->type == EV_ABS) {
                if (e->code == ABS_X) ax = e->value, abs_moved = 1;
                else if (e->code == ABS_Y) ay = e->value, abs_moved = 1;
            } else if (e->type == EV_SYN && e->code == SYN_REPORT) {
                if (abs_moved && ax >= 0 && ay >= 0) {
                    int tx = (int)((long)ax * (s->width_in_pixels - 1) / ABS_RANGE);
                    int ty = (int)((long)ay * (s->height_in_pixels - 1) / ABS_RANGE);
                    xcb_query_pointer_reply_t *p = xcb_query_pointer_reply(c, xcb_query_pointer(c, root), NULL);
                    if (p) dx += tx - p->root_x, dy += ty - p->root_y;
                    else xcb_test_fake_input(c, XCB_MOTION_NOTIFY, 0, XCB_CURRENT_TIME, root, tx, ty, 0);
                    free(p);
                }
                if (dx || dy) xcb_test_fake_input(c, XCB_MOTION_NOTIFY, 1, XCB_CURRENT_TIME, XCB_NONE, dx, dy, 0);
                if (pressed || ((abs_moved || dx || dy) && now_ms() - focus_checked >= 250)) {
                    focus_under_pointer(c, root, &focused);
                    focus_checked = now_ms();
                }
                abs_moved = 0, dx = dy = 0, pressed = 0;
                xcb_flush(c);
            }
        }
        if (xcb_connection_has_error(c)) {
            fprintf(stderr, "mn-input: the X display went away\n");
            return 1;
        }
    }
    fprintf(stderr, "mn-input: the device went away\n");
    return 1;
}
