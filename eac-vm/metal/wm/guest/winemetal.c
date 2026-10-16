/* The guest's winemetal.so (milestone 4b-1): winemetal's Unix-call table, each slot forwarded over the Metal transport
 * (one ring per thread) to the VM host, which runs MacNeutron's own winemetal.so. x86-64 Linux, under FEX; linked
 * against Wine's ntdll.so. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../guest/transport.h"
#include "../marshal.h"

struct buffers { uint8_t rec[MN_REC_MAX]; uint8_t reply[MN_REPLY_BYTES]; };
static pthread_key_t key;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static _Atomic uint64_t refused_logged[(MN_WM_SLOTS + 63) / 64];

static void make_key(void) { pthread_key_create(&key, free); }

static struct buffers *buffers(void) {
    pthread_once(&once, make_key);
    struct buffers *b = pthread_getspecific(key);
    if (!b && (b = malloc(sizeof *b))) pthread_setspecific(key, b);
    return b;
}

static NTSTATUS_T forward(unsigned slot, void *params) {
    struct buffers *b = buffers();
    if (!b || mn_open()) return MN_STATUS_UNSUCCESSFUL;
    struct mn_wm_out out = {.buf = b->rec, .cap = sizeof b->rec};
    NTSTATUS_T s = mn_wm_pack(slot, params, &out);
    if (s) {
        if (mn_wm_calls[slot].mode == MN_WM_REFUSED &&
            !(atomic_fetch_or(&refused_logged[slot / 64], 1ull << (slot % 64)) & (1ull << (slot % 64))))
            fprintf(stderr, "mn-winemetal: %s (slot %u) is refused: 0x%x\n", mn_wm_calls[slot].name, slot, s);
        return s;
    }
    if (!out.sync) {
        if (!mn_send(MN_OP_WM + slot, out.buf, out.len)) return 0;
        mn_wm_unwind(&out);
        return MN_STATUS_UNSUCCESSFUL;
    }
    uint32_t len = 0;
    int r = mn_call(MN_OP_WM + slot, out.buf, out.len, b->reply, sizeof b->reply, &len);
    if (r) {
        mn_wm_unwind(&out);
        return r > -4096 && r < 0 ? MN_STATUS_UNSUCCESSFUL : (NTSTATUS_T)r; /* a transport error, or the host's */
    }
    return mn_wm_unpack(slot, params, &out, b->reply, len);
}

/* The context's live handles, deferred releases and failed queued calls (the gates' check). */
int mn_wm_stats(struct mn_wm_stats *st) {
    if (mn_open()) return -1;
    uint32_t len = 0;
    int r = mn_call(MN_OP_WM + MN_WM_STATS, NULL, 0, st, sizeof *st, &len);
    return r || len != sizeof *st ? -1 : 0;
}

#define STUB(n) static NTSTATUS_T slot##n(void *p) { return forward(n, p); }
#define STUB10(t) STUB(t##0) STUB(t##1) STUB(t##2) STUB(t##3) STUB(t##4) STUB(t##5) STUB(t##6) STUB(t##7) STUB(t##8) STUB(t##9)
#define SLOT10(t) slot##t##0, slot##t##1, slot##t##2, slot##t##3, slot##t##4, slot##t##5, slot##t##6, slot##t##7, \
                  slot##t##8, slot##t##9,
STUB(0) STUB(1) STUB(2) STUB(3) STUB(4) STUB(5) STUB(6) STUB(7) STUB(8) STUB(9)
STUB10(1) STUB10(2) STUB10(3) STUB10(4) STUB10(5) STUB10(6) STUB10(7) STUB10(8) STUB10(9) STUB10(10) STUB10(11)
STUB10(12) STUB10(13) STUB10(14) STUB(150) STUB(151) STUB(152)

typedef NTSTATUS_T (*unixlib_entry_t)(void *params);
const unixlib_entry_t __wine_unix_call_funcs[MN_WM_SLOTS] = {
    slot0, slot1, slot2, slot3, slot4, slot5, slot6, slot7, slot8, slot9,
    SLOT10(1) SLOT10(2) SLOT10(3) SLOT10(4) SLOT10(5) SLOT10(6) SLOT10(7) SLOT10(8) SLOT10(9) SLOT10(10) SLOT10(11)
    SLOT10(12) SLOT10(13) SLOT10(14) slot150, slot151, slot152,
};
