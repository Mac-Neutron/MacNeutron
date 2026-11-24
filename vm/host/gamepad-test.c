/* Host unit & mock-pipe roundtrip tests for MacNeutron gamepad bridge (clean-room MIT). */
#include "gamepad.h"
#include "gamepad_hid.h"
#include <CoreFoundation/CoreFoundation.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "gamepad-test FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while (0)

static void test_line_encoders(void) {
    char buf[1024];

    int n = mn_pad_encode_create(MN_PAD_PROFILE_XBOX360, buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(strstr(buf, "create 0003 045e 028e 0114 304,305,307,308,310,311,314,315,316,317,318 ") == buf);
    CHECK(strstr(buf, " Microsoft X-Box 360 pad\n") != NULL);

    n = mn_pad_encode_create(MN_PAD_PROFILE_DUALSENSE, buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(strstr(buf, "create 0003 054c 0ce6 8111 304,305,307,308,310,311,312,313,314,315,316,317,318 ") == buf);
    CHECK(strstr(buf, " Sony Interactive Entertainment DualSense Wireless Controller\n") != NULL);

    n = mn_pad_encode_create(MN_PAD_PROFILE_DUALSHOCK4, buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(strstr(buf, "create 0003 054c 09cc 8111 ") == buf);
    CHECK(strstr(buf, " Sony Interactive Entertainment Wireless Controller\n") != NULL);

    mn_pad_ev_state_t s0, s1;
    memset(&s0, 0, sizeof(s0));
    s1 = s0;
    CHECK(mn_pad_encode_ev_diff(MN_PAD_PROFILE_XBOX360, &s0, &s1, buf, sizeof(buf)) == 0);

    s1.keys[0] = 1; /* 304 */
    s1.keys[6] = 1; /* 312 (ignored on Xbox 360) */
    s1.abs[0] = 16384;
    s1.hat_y = -1;
    n = mn_pad_encode_ev_diff(MN_PAD_PROFILE_XBOX360, &s0, &s1, buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(strcmp(buf, "ev 1:304:1 3:0:16384 3:17:-1\n") == 0);

    n = mn_pad_encode_ev_diff(MN_PAD_PROFILE_DUALSENSE, &s0, &s1, buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(strcmp(buf, "ev 1:304:1 1:312:1 3:0:16384 3:17:-1\n") == 0);

    n = mn_pad_encode_remove(buf, sizeof(buf));
    CHECK(n == 7 && strcmp(buf, "remove\n") == 0);

    const uint8_t desc[4] = {0x05, 0x01, 0x09, 0x05};
    n = mn_pad_encode_hid_create(0x0003, 0x054c, 0x0ce6, 0x0100, 0x21, desc, 4, "Pad\nTest", buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(strcmp(buf, "hid-create 0003 054c 0ce6 0100 21 05010905 Pad Test\n") == 0);
    CHECK(mn_pad_encode_hid_create(0x0003, 1, 2, 3, 0, desc, 0, "x", buf, sizeof(buf)) == -1);

    const uint8_t rep[3] = {0x01, 0xab, 0xcd};
    n = mn_pad_encode_hid_input(rep, 3, buf, sizeof(buf));
    CHECK(n > 0 && strcmp(buf, "hid-input 01abcd\n") == 0);

    n = mn_pad_encode_hid_get_reply(7, 0, rep, 3, buf, sizeof(buf));
    CHECK(n > 0 && strcmp(buf, "hid-get-reply 7 0 01abcd\n") == 0);

    n = mn_pad_encode_hid_get_reply(7, 5, NULL, 0, buf, sizeof(buf));
    CHECK(n > 0 && strcmp(buf, "hid-get-reply 7 5\n") == 0);

    n = mn_pad_encode_hid_set_reply(9, 19, buf, sizeof(buf));
    CHECK(n > 0 && strcmp(buf, "hid-set-reply 9 19\n") == 0);
}

static void test_mapping_math(void) {
    float ox = 1.0f, oy = 1.0f;
    mn_pad_apply_radial_deadzone(0.05f, 0.05f, 0.08f, &ox, &oy);
    CHECK(ox == 0.0f && oy == 0.0f);

    mn_pad_apply_radial_deadzone(0.54f, 0.0f, 0.08f, &ox, &oy);
    CHECK(fabsf(ox - 0.5f) < 1e-4f && fabsf(oy) < 1e-4f);

    mn_pad_apply_radial_deadzone(1.0f, 1.0f, 0.08f, &ox, &oy);
    CHECK(fabsf(ox * ox + oy * oy - 1.0f) < 1e-4f);

    mn_pad_input_t in;
    memset(&in, 0, sizeof(in));
    in.lx = 1.0f;
    in.ly = 1.0f;   /* +Y up in GCController -> negative ABS_Y in Linux evdev */
    in.rx = 0.0f;
    in.ry = -1.0f;  /* -Y down in GCController -> positive ABS_RY in Linux evdev */
    in.lt = 1.0f;
    in.rt = 0.5f;
    in.dpad_x = -1.0f;
    in.dpad_y = 1.0f; /* +Y up -> HAT0Y = -1 */
    in.x = 1;
    in.y = 0;

    mn_pad_ev_state_t ev_xbox, ev_sony;
    mn_pad_map_input(MN_PAD_PROFILE_XBOX360, &in, 0.0f, &ev_xbox);
    mn_pad_map_input(MN_PAD_PROFILE_DUALSENSE, &in, 0.0f, &ev_sony);

    /* Radial deadzone clamps (1,1) onto unit circle (~0.7071). */
    CHECK(ev_xbox.abs[0] > 23000 && ev_xbox.abs[0] < 23300);
    CHECK(ev_xbox.abs[1] < -23000 && ev_xbox.abs[1] > -23300);
    CHECK(ev_xbox.abs[2] == 255);
    CHECK(ev_xbox.abs[4] == 32767);
    CHECK(ev_xbox.abs[5] == 128);
    CHECK(ev_xbox.hat_x == -1 && ev_xbox.hat_y == -1);

    /* Face button X (left): xpad uses 307 (keys[2]=1, keys[3]=0); Sony uses 308 (keys[2]=0, keys[3]=1). */
    CHECK(ev_xbox.keys[2] == 1 && ev_xbox.keys[3] == 0);
    CHECK(ev_sony.keys[2] == 0 && ev_sony.keys[3] == 1);
    CHECK(ev_xbox.keys[6] == 0 && ev_sony.keys[6] == 1);
}

static void test_txq_coalescing_and_cap(void) {
    int pfd[2];
    CHECK(pipe(pfd) == 0);
    CHECK(fcntl(pfd[0], F_SETFL, O_NONBLOCK) == 0);
    CHECK(fcntl(pfd[1], F_SETFL, O_NONBLOCK) == 0);

    mn_pad_txq_t q;
    mn_pad_txq_init(&q);

    /* Saturate the pipe so txq holds non-empty pending data under backpressure. */
    char fill[4096];
    memset(fill, 'X', sizeof(fill) - 1);
    fill[sizeof(fill) - 1] = '\n';
    int err = 0;
    for (int i = 0; i < 64; i++) {
        CHECK(mn_pad_txq_push_control(&q, fill, sizeof(fill)) == 0);
        ssize_t w = mn_pad_txq_flush_fd(&q, pfd[1], &err);
        if (w >= 0 && mn_pad_txq_has_pending(&q)) {
            break;
        }
    }
    CHECK(mn_pad_txq_has_pending(&q));

    const char *a1 = "hid-input 01aaaa\n";
    const char *b1 = "hid-input 02bbbb\n";
    const char *a2 = "hid-input 01cccc\n";
    const char *a3 = "hid-input 01dddd\n";
    const char *c1 = "hid-input 03eeee\n";
    CHECK(mn_pad_txq_push_hid_input(&q, 1, a1, strlen(a1)) == 0);
    CHECK(mn_pad_txq_push_hid_input(&q, 2, b1, strlen(b1)) == 0);
    CHECK(mn_pad_txq_push_hid_input(&q, 1, a2, strlen(a2)) == 0);
    CHECK(mn_pad_txq_push_hid_input(&q, 1, a3, strlen(a3)) == 0);
    CHECK(mn_pad_txq_push_hid_input(&q, 3, c1, strlen(c1)) == 0);

    CHECK(q.fifo_len == 3);
    CHECK(q.fifo[0] == 1 && q.fifo[1] == 2 && q.fifo[2] == 3);
    CHECK(strcmp(q.latest[1].line, a3) == 0);
    CHECK(strcmp(q.latest[2].line, b1) == 0);
    CHECK(strcmp(q.latest[3].line, c1) == 0);

    /* Control message acts as ordering barrier: promotes latest in FIFO order before remove\n. */
    CHECK(mn_pad_txq_push_control(&q, "remove\n", 7) == 0);
    CHECK(q.fifo_len == 0);

    /* Drain pipe and flush remaining txq; verify exact tail order. */
    char *drained = (char *)malloc(512 * 1024);
    CHECK(drained != NULL);
    size_t dlen = 0;
    for (;;) {
        char tmp[8192];
        ssize_t r = read(pfd[0], tmp, sizeof(tmp));
        if (r > 0) {
            memcpy(drained + dlen, tmp, (size_t)r);
            dlen += (size_t)r;
        }
        (void)mn_pad_txq_flush_fd(&q, pfd[1], &err);
        if (!mn_pad_txq_has_pending(&q) && r < 0 && errno == EAGAIN) {
            break;
        }
    }
    drained[dlen] = '\0';
    const char *expected_tail = "hid-input 01dddd\nhid-input 02bbbb\nhid-input 03eeee\nremove\n";
    size_t et_len = strlen(expected_tail);
    CHECK(dlen >= et_len);
    CHECK(strcmp(drained + dlen - et_len, expected_tail) == 0);

    free(drained);
    mn_pad_txq_destroy(&q);
    close(pfd[0]);
    close(pfd[1]);

    /* 256 KiB cap enforcement & control-barrier eviction of stale hid-input latest entries. */
    mn_pad_txq_init(&q);
    char chunk[1024];
    memset(chunk, 'A', sizeof(chunk) - 1);
    chunk[sizeof(chunk) - 1] = '\n';
    for (int i = 0; i < 255; i++) {
        CHECK(mn_pad_txq_push_control(&q, chunk, sizeof(chunk)) == 0);
    }
    /* pending = 255 KiB; push 1 KiB hid-input into latest[1] -> total = 256 KiB. */
    char hid_chunk[1024];
    memcpy(hid_chunk, "hid-input ", 10);
    memset(hid_chunk + 10, '0', sizeof(hid_chunk) - 11);
    hid_chunk[sizeof(hid_chunk) - 1] = '\n';
    CHECK(mn_pad_txq_push_hid_input(&q, 1, hid_chunk, sizeof(hid_chunk)) == 0);
    CHECK(mn_pad_txq_bytes(&q) == MN_PAD_TX_CAP);
    /* Pushing a 1 KiB control message evicts latest[1] so the critical control barrier still fits. */
    CHECK(mn_pad_txq_push_control(&q, chunk, sizeof(chunk)) == 0);
    CHECK(mn_pad_txq_bytes(&q) == MN_PAD_TX_CAP);
    CHECK(mn_pad_txq_push_control(&q, "remove\n", 7) == -ENOSPC);
    CHECK(mn_pad_txq_push_hid_input(&q, 1, "hid-input 0100\n", 15) == -ENOSPC);
    mn_pad_txq_destroy(&q);
}

/* --- IOHIDLib report-buffer lifetime invariant test --- */

struct mock_hid_hw {
    int open_calls;
    int sched_calls;
    int reg_calls;
    int unsched_calls;
    int close_calls;
    int release_calls;
    char order[64];
    uint8_t *reg_buf;
    size_t reg_len;
    void *reg_ctx;
    const struct mn_hid_device *dev_for_check;
    bool buf_alive_at_release;
};

static void append_step(struct mock_hid_hw *hw, char c) {
    size_t n = strlen(hw->order);
    if (n + 1 < sizeof(hw->order)) {
        hw->order[n] = c;
        hw->order[n + 1] = '\0';
    }
}

static int mock_open(void *raw_ref) {
    struct mock_hid_hw *hw = (struct mock_hid_hw *)raw_ref;
    hw->open_calls++;
    append_step(hw, 'O');
    return 0;
}

static void mock_sched(void *raw_ref) {
    struct mock_hid_hw *hw = (struct mock_hid_hw *)raw_ref;
    hw->sched_calls++;
    append_step(hw, 'S');
}

static void mock_reg(void *raw_ref, uint8_t *buf, size_t len, void *dev_ctx) {
    struct mock_hid_hw *hw = (struct mock_hid_hw *)raw_ref;
    hw->reg_calls++;
    hw->reg_buf = buf;
    hw->reg_len = len;
    hw->reg_ctx = dev_ctx;
    append_step(hw, 'R');
}

static void mock_unsched(void *raw_ref) {
    struct mock_hid_hw *hw = (struct mock_hid_hw *)raw_ref;
    hw->unsched_calls++;
    append_step(hw, 'U');
    CHECK(mn_hid_device_input_buf(hw->dev_for_check, NULL) == hw->reg_buf);
}

static void mock_close(void *raw_ref) {
    struct mock_hid_hw *hw = (struct mock_hid_hw *)raw_ref;
    hw->close_calls++;
    append_step(hw, 'C');
    CHECK(mn_hid_device_input_buf(hw->dev_for_check, NULL) == hw->reg_buf);
}

static void mock_release(void *raw_ref) {
    struct mock_hid_hw *hw = (struct mock_hid_hw *)raw_ref;
    hw->release_calls++;
    append_step(hw, 'F');
    /* Buffer must STILL be allocated until AFTER unschedule + close + release complete. */
    size_t cap = 0;
    const uint8_t *b = mn_hid_device_input_buf(hw->dev_for_check, &cap);
    hw->buf_alive_at_release = (b != NULL && b == hw->reg_buf && cap == hw->reg_len);
}

struct mock_rx_log {
    int count;
    int last_slot;
    uint8_t last_bytes[64];
    size_t last_len;
};

static void mock_on_input(int slot, const uint8_t *report, size_t len, void *ctx) {
    struct mock_rx_log *log = (struct mock_rx_log *)ctx;
    log->count++;
    log->last_slot = slot;
    log->last_len = len < sizeof(log->last_bytes) ? len : sizeof(log->last_bytes);
    memcpy(log->last_bytes, report, log->last_len);
}

static void test_iohid_report_buffer_lifetime(void) {
    const uint8_t numbered_desc[] = {0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x01, 0xC0};
    const uint8_t plain_desc[] = {0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0xC0};
    CHECK(mn_hid_desc_uses_report_ids(numbered_desc, sizeof(numbered_desc)));
    CHECK(!mn_hid_desc_uses_report_ids(plain_desc, sizeof(plain_desc)));

    mn_hid_dev_info_t ds_info = {.kind = MN_HID_KIND_DUALSENSE, .is_ble_steam = false, .max_feature = 64};
    mn_hid_dev_info_t ble_info = {.kind = MN_HID_KIND_STEAM, .is_ble_steam = true, .max_feature = 64};
    CHECK(mn_hid_get_report_buf_size(&ds_info, MN_HID_REPORT_FEATURE) == 64);
    CHECK(mn_hid_get_report_buf_size(&ble_info, MN_HID_REPORT_FEATURE) == 128);

    struct mock_hid_hw hw;
    memset(&hw, 0, sizeof(hw));
    const mn_hid_backend_ops_t ops = {
        .open_dev = mock_open,
        .schedule = mock_sched,
        .register_input_cb = mock_reg,
        .unschedule = mock_unsched,
        .close_dev = mock_close,
        .release_dev = mock_release,
    };
    mn_hid_dev_info_t info;
    memset(&info, 0, sizeof(info));
    info.kind = MN_HID_KIND_STEAM;
    info.max_input = 64;

    int err = 0;
    struct mn_hid_device *dev = mn_hid_device_create_with_ops(&hw, &info, &ops, &err);
    CHECK(dev != NULL && err == 0);
    hw.dev_for_check = dev;
    CHECK(hw.open_calls == 1 && hw.sched_calls == 1 && hw.reg_calls == 1);
    CHECK(hw.reg_buf != NULL && hw.reg_len == 64);

    struct mock_rx_log rx = {0};
    mn_hid_device_set_active(dev, 0, mock_on_input, &rx);
    int active_slot = -1;
    CHECK(mn_hid_device_is_active(dev, &active_slot) && active_slot == 0);

    hw.reg_buf[0] = 0x01;
    hw.reg_buf[1] = 0x42;
    mn_hid_device_deliver_input(dev, hw.reg_buf, 2);
    CHECK(rx.count == 1 && rx.last_slot == 0 && rx.last_bytes[1] == 0x42);

    /* Deactivate: buffer MUST stay live and registered, never freed or re-registered. */
    mn_hid_device_set_active(dev, -1, NULL, NULL);
    CHECK(!mn_hid_device_is_active(dev, &active_slot) && active_slot == -1);
    CHECK(hw.reg_calls == 1 && hw.unsched_calls == 0 && hw.close_calls == 0 && hw.release_calls == 0);
    CHECK(mn_hid_device_input_buf(dev, NULL) == hw.reg_buf);

    /* Simulated IOHIDLib write into registered buffer while deactivated: safe, no UAF, ignored. */
    hw.reg_buf[0] = 0x01;
    hw.reg_buf[1] = 0x99;
    mn_hid_device_deliver_input(dev, hw.reg_buf, 2);
    CHECK(rx.count == 1);

    /* Reactivate on slot 2: still same single registration. */
    mn_hid_device_set_active(dev, 2, mock_on_input, &rx);
    hw.reg_buf[1] = 0x77;
    mn_hid_device_deliver_input(dev, hw.reg_buf, 2);
    CHECK(rx.count == 2 && rx.last_slot == 2 && rx.last_bytes[1] == 0x77);
    CHECK(hw.reg_calls == 1);

    mn_hid_device_destroy(dev);
    CHECK(strcmp(hw.order, "OSRUCF") == 0);
    CHECK(hw.buf_alive_at_release);
}

/* --- Live mock-pipe roundtrip test (MN_PAD_SELFTEST=1) --- */

struct pipe_guest_ctx {
    int rd_fd;
    int wr_fd;
    volatile int done;
    volatile int failed;
};

static int read_line_timeout(int fd, char *out, size_t cap, int timeout_ms) {
    size_t pos = 0;
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        char c = 0;
        ssize_t n = read(fd, &c, 1);
        if (n == 1) {
            if (c == '\n') {
                out[pos] = '\0';
                return 0;
            }
            if (pos + 1 < cap) {
                out[pos++] = c;
            }
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            usleep(2000);
            elapsed += 2;
            continue;
        }
        return -1;
    }
    return -1;
}

static void write_str(int fd, const char *s) {
    size_t len = strlen(s);
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, s + off, len - off);
        if (n > 0) {
            off += (size_t)n;
        } else if (errno == EAGAIN || errno == EINTR) {
            usleep(1000);
        } else {
            break;
        }
    }
}

static void *guest_sim_thread(void *arg) {
    struct pipe_guest_ctx *g = (struct pipe_guest_ctx *)arg;
    char line[2048];

    write_str(g->wr_fd, "hello\ncaps hid\n");

    if (read_line_timeout(g->rd_fd, line, sizeof(line), 2000) != 0 ||
        strncmp(line, "create 0003 045e 028e 0114 ", 27) != 0) {
        fprintf(stderr, "guest_sim: expected Xbox 360 create, got: %s\n", line);
        g->failed = 1;
        g->done = 1;
        return NULL;
    }

    int got_ev = 0;
    for (int i = 0; i < 10; i++) {
        if (read_line_timeout(g->rd_fd, line, sizeof(line), 1000) != 0) {
            break;
        }
        if (strcmp(line, "ev 1:304:1 3:0:16384") == 0) {
            got_ev = 1;
            break;
        }
    }
    if (!got_ev) {
        fprintf(stderr, "guest_sim: missing ev 1:304:1 3:0:16384\n");
        g->failed = 1;
        g->done = 1;
        return NULL;
    }

    write_str(g->wr_fd, "rumble 49152 16384\n");
    usleep(30000);
    write_str(g->wr_fd, "rumble 0 0\n");

    int got_hid_create = 0, got_hid_input = 0;
    for (int i = 0; i < 20; i++) {
        if (read_line_timeout(g->rd_fd, line, sizeof(line), 1500) != 0) {
            break;
        }
        if (strncmp(line, "hid-create ", 11) == 0) {
            got_hid_create = 1;
        } else if (strcmp(line, "hid-input 01054080") == 0) {
            got_hid_input = 1;
            break;
        }
    }
    if (!got_hid_create || !got_hid_input) {
        fprintf(stderr, "guest_sim: missing hid-create (%d) or hid-input (%d)\n",
                got_hid_create, got_hid_input);
        g->failed = 1;
        g->done = 1;
        return NULL;
    }

    write_str(g->wr_fd, "hid-output output 01aabb\n");
    write_str(g->wr_fd, "hid-get 42 feature 1\n");
    write_str(g->wr_fd, "hid-set 43 feature 011234\n");

    int got_get_reply = 0, got_set_reply = 0;
    for (int i = 0; i < 20 && (!got_get_reply || !got_set_reply); i++) {
        if (read_line_timeout(g->rd_fd, line, sizeof(line), 1500) != 0) {
            break;
        }
        if (strcmp(line, "hid-get-reply 42 0 01cafe1234") == 0) {
            got_get_reply = 1;
        } else if (strcmp(line, "hid-set-reply 43 0") == 0) {
            got_set_reply = 1;
        }
    }
    if (!got_get_reply || !got_set_reply) {
        fprintf(stderr, "guest_sim: missing get_reply (%d) or set_reply (%d)\n",
                got_get_reply, got_set_reply);
        g->failed = 1;
    }
    g->done = 1;
    return NULL;
}

static void test_live_mock_pipe_roundtrip(void) {
    int to_guest[2], from_guest[2];
    CHECK(pipe(to_guest) == 0);
    CHECK(pipe(from_guest) == 0);
    CHECK(fcntl(to_guest[0], F_SETFL, O_NONBLOCK) == 0);

    setenv("MN_PAD_SELFTEST", "1", 1);
    int wr_fds[1] = {to_guest[1]};
    int rd_fds[1] = {from_guest[0]};
    CHECK(mn_gamepad_init(wr_fds, rd_fds, 1) == 0);

    struct pipe_guest_ctx g = {
        .rd_fd = to_guest[0],
        .wr_fd = from_guest[1],
        .done = 0,
        .failed = 0,
    };
    pthread_t th;
    CHECK(pthread_create(&th, NULL, guest_sim_thread, &g) == 0);

    for (int i = 0; i < 500 && !g.done; i++) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
    }
    pthread_join(th, NULL);
    CHECK(g.done && !g.failed);

    mn_gamepad_set_paused(1);
    mn_gamepad_set_paused(0);
    mn_gamepad_shutdown();
    for (int i = 0; i < 10; i++) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.005, true);
    }
    close(to_guest[0]);
    close(from_guest[1]);
    unsetenv("MN_PAD_SELFTEST");
}

static void test_multi_slot_and_validation(void) {
    int dummy_w[1] = {0}, dummy_r[1] = {0};
    CHECK(mn_gamepad_init(NULL, dummy_r, 1) == -1);
    CHECK(mn_gamepad_init(dummy_w, NULL, 1) == -1);
    CHECK(mn_gamepad_init(dummy_w, dummy_r, 0) == -1);
    CHECK(mn_gamepad_init(dummy_w, dummy_r, 5) == -1);

    int to_g[MN_GAMEPAD_MAX_SLOTS][2];
    int from_g[MN_GAMEPAD_MAX_SLOTS][2];
    int wr_fds[MN_GAMEPAD_MAX_SLOTS];
    int rd_fds[MN_GAMEPAD_MAX_SLOTS];
    for (int i = 0; i < MN_GAMEPAD_MAX_SLOTS; i++) {
        CHECK(pipe(to_g[i]) == 0);
        CHECK(pipe(from_g[i]) == 0);
        CHECK(fcntl(to_g[i][0], F_SETFL, O_NONBLOCK) == 0);
        wr_fds[i] = to_g[i][1];
        rd_fds[i] = from_g[i][0];
    }

    setenv("MN_PAD_SELFTEST", "1", 1);
    CHECK(mn_gamepad_init(wr_fds, rd_fds, MN_GAMEPAD_MAX_SLOTS) == 0);
    for (int i = 0; i < MN_GAMEPAD_MAX_SLOTS; i++) {
        write_str(from_g[i][1], "hello\ncaps hid\nmalformed_ignored_line\n");
    }
    for (int i = 0; i < 30; i++) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.005, true);
    }
    char line[1024];
    CHECK(read_line_timeout(to_g[0][0], line, sizeof(line), 500) == 0);
    CHECK(strncmp(line, "create 0003 045e 028e 0114 ", 27) == 0);
    /* Slots 1..3 should ignore selftest and stay quiet when no physical controller is attached. */
    mn_gamepad_shutdown();
    for (int i = 0; i < 10; i++) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.005, true);
    }
    for (int i = 0; i < MN_GAMEPAD_MAX_SLOTS; i++) {
        close(to_g[i][0]);
        close(from_g[i][1]);
    }
    unsetenv("MN_PAD_SELFTEST");

    /* Partial failure in mn_gamepad_init must close all remaining fds cleanly without leaking. */
    int p0_w[2], p0_r[2], p2_w[2], p2_r[2];
    CHECK(pipe(p0_w) == 0 && pipe(p0_r) == 0);
    CHECK(pipe(p2_w) == 0 && pipe(p2_r) == 0);
    int fail_wr[3] = {p0_w[1], -1, p2_w[1]};
    int fail_rd[3] = {p0_r[0], -1, p2_r[0]};
    CHECK(mn_gamepad_init(fail_wr, fail_rd, 3) == -1);
    for (int i = 0; i < 10; i++) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.005, true);
    }
    CHECK(fcntl(p0_w[1], F_GETFD) == -1 && errno == EBADF);
    CHECK(fcntl(p0_r[0], F_GETFD) == -1 && errno == EBADF);
    CHECK(fcntl(p2_w[1], F_GETFD) == -1 && errno == EBADF);
    CHECK(fcntl(p2_r[0], F_GETFD) == -1 && errno == EBADF);
    close(p0_w[0]);
    close(p0_r[1]);
    close(p2_w[0]);
    close(p2_r[1]);

    /* Overlong incoming line must discard its tail up to '\n' rather than parsing the tail. */
    int ov_to_g[2], ov_from_g[2];
    CHECK(pipe(ov_to_g) == 0 && pipe(ov_from_g) == 0);
    CHECK(fcntl(ov_to_g[0], F_SETFL, O_NONBLOCK) == 0);
    setenv("MN_PAD_SELFTEST", "1", 1);
    int ov_wr[1] = {ov_to_g[1]};
    int ov_rd[1] = {ov_from_g[0]};
    CHECK(mn_gamepad_init(ov_wr, ov_rd, 1) == 0);
    write_str(ov_from_g[1], "hello\n");
    char *overlong = (char *)malloc(20050);
    CHECK(overlong != NULL);
    memset(overlong, 'X', 20000);
    memcpy(overlong + 20000, " hid-get 88 feature 1\n", 23);
    write_str(ov_from_g[1], overlong);
    free(overlong);
    write_str(ov_from_g[1], "hid-get 77 feature 1\nhid-get 78 invalid 999\n");
    for (int i = 0; i < 30; i++) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.005, true);
    }
    int saw_77 = 0, saw_88 = 0, saw_78_err = 0;
    while (read_line_timeout(ov_to_g[0], line, sizeof(line), 100) == 0) {
        if (strcmp(line, "hid-get-reply 77 0 01cafe1234") == 0) saw_77 = 1;
        if (strncmp(line, "hid-get-reply 88 ", 17) == 0) saw_88 = 1;
        if (strcmp(line, "hid-get-reply 78 22") == 0) saw_78_err = 1;
    }
    CHECK(saw_77 && !saw_88 && saw_78_err);
    mn_gamepad_shutdown();
    for (int i = 0; i < 10; i++) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.005, true);
    }
    close(ov_to_g[0]);
    close(ov_from_g[1]);
    unsetenv("MN_PAD_SELFTEST");
}

int main(void) {
    test_line_encoders();
    test_mapping_math();
    test_txq_coalescing_and_cap();
    test_iohid_report_buffer_lifetime();
    test_live_mock_pipe_roundtrip();
    test_multi_slot_and_validation();
    printf("gamepad-test: ok\n");
    return 0;
}
