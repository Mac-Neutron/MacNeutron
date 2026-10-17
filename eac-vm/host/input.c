/* The guest's keyboard and mouse (spec §5.7): one virtio-input device, "MacNeutron VM input", whose events come from
 * the game window (metal-host's winemac.so, eac-vm/metal/wm/host/winemac.m, through mn_window_set_input_sink). Keys
 * and buttons, relative motion and wheels, and an absolute position (0..32767 on both axes, the window's whole client
 * area). The guest's agent (eac-vm/guest/mn-input.c) turns them into XTEST input for Xvfb. */
#include "input.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03
#define QUEUE 4096 /* events; a full queue drops new ones (the guest isn't reading) */

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct krun_input_event queue[QUEUE];
static unsigned head, tail; /* tail - head events queued */
static int wake[2] = {-1, -1};

void mn_input_post(uint16_t type, uint16_t code, int32_t value) {
    pthread_mutex_lock(&lock);
    int room = tail - head < QUEUE;
    if (room) queue[tail++ % QUEUE] = (struct krun_input_event){type, code, (uint32_t)value};
    pthread_mutex_unlock(&lock);
    if (room && wake[1] >= 0) (void)!write(wake[1], "", 1);
}

static int pop(struct krun_input_event *e) {
    pthread_mutex_lock(&lock);
    int got = tail != head;
    if (got) *e = queue[head++ % QUEUE];
    pthread_mutex_unlock(&lock);
    return got;
}

/* libkrun's worker polls the wake fd and then takes events until there are none; it never reads the fd. So the last
 * call drains it, and looks once more: an event posted meanwhile has its own byte still in the pipe, or is found. */
int32_t mn_input_next(void *instance, struct krun_input_event *out) {
    (void)instance;
    if (pop(out)) return 1;
    char buf[256];
    while (read(wake[0], buf, sizeof buf) > 0) {}
    return pop(out);
}

static int32_t create(void **instance, const void *userdata, const void *reserved) {
    (void)userdata, (void)reserved;
    *instance = NULL;
    return 0;
}

static int ready_fd(void *instance) {
    (void)instance;
    return wake[0];
}

static int32_t name(void *instance, uint8_t *buf, size_t len) {
    (void)instance;
    const char n[] = "MacNeutron VM input";
    size_t k = sizeof n - 1 < len ? sizeof n - 1 : len;
    memcpy(buf, n, k);
    return (int32_t)k;
}

static int32_t serial(void *instance, uint8_t *buf, size_t len) {
    (void)instance;
    if (!len) return 0;
    buf[0] = '0';
    return 1;
}

static int32_t ids(void *instance, struct krun_input_device_ids *out) {
    (void)instance;
    *out = (struct krun_input_device_ids){.bustype = 0x06 /* BUS_VIRTUAL */, .vendor = 0x4d4e, .product = 1, .version = 1};
    return 0;
}

static void set_bit(uint8_t *bits, size_t len, unsigned bit, size_t *used) {
    if (bit / 8 >= len) return;
    bits[bit / 8] |= 1u << (bit % 8);
    if (bit / 8 + 1 > *used) *used = bit / 8 + 1;
}

static int32_t capabilities(void *instance, uint8_t type, uint8_t *bits, size_t len) {
    (void)instance;
    size_t used = 0;
    memset(bits, 0, len);
    switch (type) {
    case EV_KEY:
        for (unsigned k = 1; k < 0xf8; k++) set_bit(bits, len, k, &used);         /* KEY_ESC to KEY_MICMUTE's block */
        for (unsigned b = 0x110; b <= 0x114; b++) set_bit(bits, len, b, &used);   /* BTN_LEFT to BTN_EXTRA */
        break;
    case EV_REL:
        for (unsigned r = 0; r <= 1; r++) set_bit(bits, len, r, &used);           /* REL_X, REL_Y */
        set_bit(bits, len, 6, &used);                                            /* REL_HWHEEL */
        set_bit(bits, len, 8, &used);                                            /* REL_WHEEL */
        break;
    case EV_ABS:
        set_bit(bits, len, 0, &used), set_bit(bits, len, 1, &used);              /* ABS_X, ABS_Y */
        break;
    default:
        break;
    }
    return (int32_t)used;
}

static int32_t abs_info(void *instance, uint8_t axis, struct krun_input_absinfo *out) {
    (void)instance;
    if (axis > 1) return KRUN_INPUT_ERR_INVALID_PARAM;
    *out = (struct krun_input_absinfo){.min = 0, .max = MN_INPUT_ABS_MAX};
    return 0;
}

static int32_t properties(void *instance, uint8_t *bits, size_t len) {
    (void)instance;
    memset(bits, 0, len);
    return 0;
}

/* MN_INPUT_SELFTEST=1 (eac-vm/tests/input.sh, input-wine.sh): a fixed sequence 20 s after boot, the agent's and the X
 * server's check: the pointer to the middle, key A, the left button; 2 s later (once a click's focus has reached Wine,
 * which only then asks for raw motion) a relative move of (10, 5) and one wheel notch up. */
static void *selftest(void *arg) {
    (void)arg;
    static const struct krun_input_event first[] = {
        {EV_ABS, 0, 16384}, {EV_ABS, 1, 16384}, {EV_SYN, 0, 0}, {EV_KEY, 30, 1}, {EV_SYN, 0, 0}, {EV_KEY, 30, 0},
        {EV_SYN, 0, 0}, {EV_KEY, 0x110, 1}, {EV_SYN, 0, 0}, {EV_KEY, 0x110, 0}, {EV_SYN, 0, 0},
    }, then[] = {
        {EV_REL, 0, 10}, {EV_REL, 1, 5}, {EV_SYN, 0, 0}, {EV_REL, 8, 1}, {EV_SYN, 0, 0},
    };
    sleep(20);
    for (size_t i = 0; i < sizeof first / sizeof *first; i++)
        mn_input_post(first[i].type, first[i].code, (int32_t)first[i].value);
    sleep(2);
    for (size_t i = 0; i < sizeof then / sizeof *then; i++)
        mn_input_post(then[i].type, then[i].code, (int32_t)then[i].value);
    return NULL;
}

KrunInputDevice mn_input_device(KrunError *err) {
    if (wake[0] < 0) {
        if (pipe(wake)) return NULL;
        fcntl(wake[0], F_SETFL, O_NONBLOCK);
        fcntl(wake[1], F_SETFL, O_NONBLOCK);
    }
    /* The game window lives in metal-host's winemac.so, which winemetal.so loaded: it posts here once it exists. */
    void (*set_sink)(void (*)(uint16_t, uint16_t, int32_t)) =
        (void (*)(void (*)(uint16_t, uint16_t, int32_t)))dlsym(RTLD_DEFAULT, "mn_window_set_input_sink");
    if (set_sink) set_sink(mn_input_post);
    else fprintf(stderr, "macneutron-vm: no game window code (metal-host's winemac.so): no input\n");
    if (getenv("MN_INPUT_SELFTEST")) {
        pthread_t t;
        pthread_create(&t, NULL, selftest, NULL);
    }
    struct krun_input_config config = {
        .features = KRUN_INPUT_CONFIG_FEATURE_QUERY,
        .create = create,
        .vtable = {NULL, name, serial, ids, capabilities, abs_info, properties},
    };
    struct krun_input_event_provider events = {
        .features = KRUN_INPUT_EVENT_PROVIDER_FEATURE_QUEUE,
        .create = create,
        .vtable = {NULL, ready_fd, mn_input_next},
    };
    return krun_input_device_new(&config, sizeof config, &events, sizeof events, err);
}
