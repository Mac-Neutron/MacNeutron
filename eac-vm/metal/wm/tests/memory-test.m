/* Milestone 4b-1, Task 5: memory the guest touches, on the Mac: the guest winemetal.so's code and the host's in one
 * process (loopback.c), against MacNeutron's real winemetal.so and real Metal. Usage: memory-test <metal-host dir>.
 *   galloc                the allocator's cases (galloc-cases.c)
 *   buffer-outlives-free  a buffer over blob memory, still used by an uncompleted command buffer, keeps the memory
 *                         mapped after the guest frees the buffer and the blob; it goes once Metal is done with it
 *   update-bounds         slot 107 past the buffer's end is refused, and nothing past it is written
 *   region-bounds         slots 45 and 145 outside the texture's level are refused; inside, the bytes round-trip
 *   heap                  a buffer placed in an emulated (Shared) heap lies at the heap's memory plus its offset, and
 *                         joins the residency set the heap was added to
 *   wmt-memory            DXMT 0071's WMTMemoryAlloc (slot 153) gives page-aligned memory a buffer can be made
 *                         over; WMTMemoryFree (154) while the buffer lives keeps the memory from a new allocation
 *                         until the host says the buffer is gone; neither reaches the host
 *   sequence              the 4b-1 sequence, as far as the gate runs it */
#import <Metal/Metal.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../guest/galloc.h"
#include "../host/loader.h"
#include "galloc-cases.h"
#include "loopback.h"
#include "sequence.h"
#include "winemetal_thunks.h"

#define SEQUENCE_STEPS 0 /* as metal-wm.sh's STEPS: all */

typedef NTSTATUS_T (*entry_t)(void *params);
extern const entry_t __wine_unix_call_funcs[MN_WM_SLOTS];
int mn_wm_stats(struct mn_wm_stats *st);

static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __func__, __LINE__, #c);                                             \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static NTSTATUS_T W(unsigned slot, void *p) { return __wine_unix_call_funcs[slot](p); }
static void print(const char *line) { printf("  %s\n", line); }
static id host_obj(obj_handle_t h) {
    int ok;
    return (id)mn_ht_get(loop_ctx.ht, h, MN_HK_OBJC, &ok);
}
static void sweep(void) {
    struct mn_wm_stats st;
    mn_wm_stats(&st);
}
static void release(obj_handle_t h) { W(1, &h); }

static obj_handle_t devices, device;

static obj_handle_t buffer(uint64_t len, void **ptr) {
    struct WMTBufferInfo info = {.length = len, .options = WMTResourceStorageModeShared};
    struct unixcall_mtldevice_newbuffer b = {device, {&info}, 0};
    if (W(18, &b)) return 0;
    *ptr = info.memory.ptr;
    return b.ret;
}

static void galloc(void) {
    struct mn_galloc_env env = {W, mn_wm_stats, print};
    CHECK(mn_galloc_cases(&env) == 0);
}

static void buffer_outlives_free(void) {
    void *p = NULL;
    uint64_t region = 0, blob, off;
    @autoreleasepool {
        obj_handle_t b = buffer(65u << 20, &p); /* past 64 MiB: a blob of its own */
        CHECK(b && p);
        if (!b) return;
        CHECK(mn_glookup(p, 1, &region, &blob, &off) == 0 && mn_gholders(region) == 1);
        id<MTLBuffer> buf = host_obj(b);
        CHECK([buf contents] == p); /* in loopback the guest's address is the host's */
        id<MTLDevice> dev = [buf device];
        id<MTLCommandQueue> q = [dev newCommandQueue];
        id<MTLSharedEvent> gate = [dev newSharedEvent];
        id<MTLBuffer> dst = [dev newBufferWithLength:16384 options:0];
        id<MTLCommandBuffer> cb = [q commandBuffer];
        [cb encodeWaitForEvent:gate value:1]; /* the GPU holds the buffer until the test opens the gate */
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        [blit copyFromBuffer:buf sourceOffset:0 toBuffer:dst destinationOffset:0 size:16384];
        [blit endEncoding];
        [cb commit];
        release(b);
        sweep(); /* the host table's reference goes; the command buffer's stays */
        CHECK(loop_blob_free_of(p) == 0);
        CHECK(loop_mapped(p));
        mn_gdrain();
        CHECK(mn_gholders(region) == 1); /* no MN_NOTIFY_REGION_FREE while Metal has it */
        gate.signaledValue = 1;
        [cb waitUntilCompleted];
        [dst release];
        [gate release];
        [q release];
    } /* the command buffer goes with the pool, and the buffer with it, once Metal's completion thread lets go */
    for (int i = 0; i < 1000 && (mn_gdrain(), mn_gholders(region)); i++) usleep(1000);
    CHECK(mn_gholders(region) == 0);
    CHECK(!loop_mapped(p));
}

static void update_bounds(void) {
    void *p;
    obj_handle_t b = buffer(16384, &p);
    CHECK(b && p);
    if (!b) return;
    uint8_t *bytes = p;
    uint8_t after[8];
    memcpy(after, bytes + 16384, 8); /* the page after the buffer's, in the same blob */
    uint64_t before = loop_ctx.failures;
    struct unixcall_mtlbuffer_updatecontents u = {b, 16380, {"abcdefgh"}, 8};
    CHECK(W(107, &u) == 0); /* queued */
    CHECK(loop_ctx.failures == before + 1);
    CHECK(!memcmp(bytes + 16384, after, 8) && bytes[16380] == 0);
    u.offset = 8;
    CHECK(W(107, &u) == 0 && loop_ctx.failures == before + 1 && !memcmp(bytes + 8, "abcdefgh", 8));
    release(b);
}

static void region_bounds(void) {
    struct WMTTextureInfo ti = {.pixel_format = WMTPixelFormatRGBA8Unorm, .width = 64, .height = 64, .depth = 1,
                                .array_length = 1, .type = WMTTextureType2D, .mipmap_level_count = 1,
                                .sample_count = 1, .usage = WMTTextureUsageShaderRead};
    struct unixcall_mtldevice_newtexture nt = {device, {&ti}, 0};
    CHECK(W(21, &nt) == 0 && nt.ret);
    static uint8_t in[64 * 64 * 4], out[64 * 64 * 4];
    for (size_t i = 0; i < sizeof in; i++) in[i] = (uint8_t)(i * 7);
    uint64_t before = loop_ctx.failures;
    struct unixcall_mtltexture_replaceregion r = {nt.ret, {60, 0, 0}, {8, 8, 1}, 0, 0, {in}, 256, 0};
    CHECK(W(45, &r) == 0 && loop_ctx.failures == before + 1); /* x 60 + 8 is past 64 */
    r = (struct unixcall_mtltexture_replaceregion){nt.ret, {0, 0, 0}, {64, 64, 1}, 1, 0, {in}, 256, 0};
    CHECK(W(45, &r) == 0 && loop_ctx.failures == before + 2); /* level 1 of a single-level texture */
    r.level = 0;
    CHECK(W(45, &r) == 0 && loop_ctx.failures == before + 2);
    struct unixcall_mtltexture_replaceregion g = {nt.ret, {0, 0, 0}, {64, 64, 1}, 0, 0, {out}, 256, 0};
    CHECK(W(145, &g) == 0 && !memcmp(in, out, sizeof in));
    g.size.height = 65;
    CHECK(W(145, &g) == MN_STATUS_INVALID_PARAMETER);
    release(nt.ret);
}

static void heap(void) {
    struct WMTHeapInfo hi = {.size = 1u << 20, .options = WMTResourceStorageModeShared, .type = WMTHeapTypePlacement,
                             .sparse_page_size = WMTSparsePageSize16};
    struct unixcall_mtldevice_newheap nh = {device, {&hi}, 0};
    CHECK(W(138, &nh) == 0 && nh.ret);
    struct unixcall_mtldevice_newresidencyset rs = {device, 16, 0, 0};
    CHECK(W(132, &rs) == 0 && rs.ret_set);
    struct unixcall_mtlresidencyset_addallocations add = {rs.ret_set, {&nh.ret}, 1};
    CHECK(W(133, &add) == 0);
    struct WMTBufferInfo bi = {.length = 65536, .options = WMTResourceStorageModeShared};
    bi.memory.ptr = (void *)0x1234; /* uninitialised in DXMT's placed buffers: an output only */
    struct unixcall_mtlheap_newbuffer nb = {nh.ret, {&bi}, 65536, 0};
    CHECK(W(141, &nb) == 0 && nb.ret && bi.memory.ptr && bi.gpu_address);
    uint64_t region, blob, off;
    CHECK(mn_glookup(bi.memory.ptr, 65536, &region, &blob, &off) == 0);
    CHECK((char *)bi.memory.ptr == (char *)mn_gptr(region) + 65536);
    id<MTLBuffer> buf = host_obj(nb.ret);
    id<MTLResidencySet> set = host_obj(rs.ret_set);
    CHECK([buf contents] == bi.memory.ptr && [set containsAllocation:buf] && [set allocationCount] == 1);
    struct unixcall_mtlheap_newbuffer outside = {nh.ret, {&bi}, (1u << 20) - 16384, 0};
    CHECK(W(141, &outside) == MN_STATUS_INVALID_PARAMETER); /* 64 KiB from 16 KiB before the end */
    struct unixcall_mtlheap_newtexture tex = {nh.ret, {&(struct WMTTextureInfo){0}}, 0, 0};
    CHECK(W(142, &tex) == MN_STATUS_NOT_SUPPORTED);
    mn_gdrain(); /* the refused calls' holds came back at once */
    CHECK(mn_gholders(region) == 2); /* the heap and its buffer */
    release(nb.ret);
    sweep();
    mn_gdrain();
    CHECK(mn_gholders(region) == 1 && [set allocationCount] == 0); /* buf is gone: not touched again */
    release(rs.ret_set);
    release(nh.ret);
    sweep();
    mn_gdrain();
    CHECK(mn_gholders(region) == 0);
}

/* The guest's NtSetEvent (Wine's, in the guest): the listener loop calls it for each event the host posts. */
static pthread_mutex_t ev_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ev_cond = PTHREAD_COND_INITIALIZER;
static uint64_t events[16];
static int nevents;
unsigned int NtSetEvent(void *handle, int *prev) {
    if (prev) *prev = 0;
    pthread_mutex_lock(&ev_lock);
    if (nevents < 16) events[nevents++] = (uint64_t)(uintptr_t)handle;
    pthread_cond_broadcast(&ev_cond);
    pthread_mutex_unlock(&ev_lock);
    return 0;
}
static int signalled(uint64_t h, int timeout_ms) {
    int found = 0;
    pthread_mutex_lock(&ev_lock);
    for (int waited = 0; !found && waited <= timeout_ms; waited += 10) {
        for (int i = 0; i < nevents; i++) found |= events[i] == h;
        if (!found) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 10000000;
            if (ts.tv_nsec >= 1000000000) ts.tv_sec++, ts.tv_nsec -= 1000000000;
            pthread_cond_timedwait(&ev_cond, &ev_lock, &ts);
        }
    }
    pthread_mutex_unlock(&ev_lock);
    return found;
}

static void wmt_memory(void) {
    CHECK(__wine_unix_call_funcs[153] && __wine_unix_call_funcs[154]);
    if (!__wine_unix_call_funcs[153] || !__wine_unix_call_funcs[154]) return;
    uint64_t before = loop_ctx.failures, region, blob, off;
    struct unixcall_wmtmemory m = {65536, {NULL}};
    CHECK(W(153, &m) == 0 && m.ptr.ptr && ((uintptr_t)m.ptr.ptr & 16383) == 0);
    void *p = m.ptr.ptr;
    if (!p) return;
    CHECK(mn_glookup(p, 65536, &region, &blob, &off) == 0 && mn_gptr(region) == p);
    @autoreleasepool {
        struct WMTBufferInfo info = {.length = 65536, .options = WMTResourceStorageModeShared, .memory = {p}};
        struct unixcall_mtldevice_newbuffer b = {device, {&info}, 0};
        CHECK(W(18, &b) == 0 && b.ret && info.memory.ptr == p && mn_gholders(region) == 1);
        CHECK([(id<MTLBuffer>)host_obj(b.ret) contents] == p);
        struct unixcall_wmtmemory f = {0, {p}};
        CHECK(W(154, &f) == 0);
        CHECK(mn_glookup(p, 1, &region, &blob, &off) == 0); /* still reserved: the buffer holds it */
        struct unixcall_wmtmemory n = {65536, {NULL}};
        CHECK(W(153, &n) == 0 && n.ptr.ptr && ((uint8_t *)n.ptr.ptr >= (uint8_t *)p + 65536 ||
                                                (uint8_t *)n.ptr.ptr + 65536 <= (uint8_t *)p));
        struct unixcall_wmtmemory inside = {0, {(uint8_t *)n.ptr.ptr + 16384}};
        CHECK(W(154, &inside) == MN_STATUS_INVALID_PARAMETER); /* not an allocation's start */
        struct unixcall_wmtmemory none = {0, {NULL}};
        CHECK(W(154, &none) == 0);
        W(154, &n);
        release(b.ret);
    }
    for (int i = 0; i < 1000 && (sweep(), mn_gdrain(), mn_glookup(p, 1, &region, &blob, &off) == 0); i++) usleep(1000);
    CHECK(mn_glookup(p, 1, &region, &blob, &off) != 0); /* back for reuse */
    CHECK(loop_ctx.failures == before);
}

static void sequence(void) {
    struct mn_wm_env env = {W, print, signalled};
    CHECK(mn_wm_sequence(&env, SEQUENCE_STEPS) == 0);
}

int main(int argc, char **argv) {
    if (argc != 2 || mn_wm_load(argv[1])) {
        fprintf(stderr, "memory-test: FAIL loading winemetal.so\n");
        return 1;
    }
    struct { const char *name; void (*fn)(void); } tests[] = {
        {"galloc", galloc},           {"buffer-outlives-free", buffer_outlives_free},
        {"update-bounds", update_bounds}, {"region-bounds", region_bounds},
        {"heap", heap},               {"wmt-memory", wmt_memory}, {"sequence", sequence},
    };
    for (size_t i = 0; i < sizeof tests / sizeof *tests; i++) {
        @autoreleasepool {
            if (i == 1) { /* the galloc cases make their own device; the rest share one */
                struct unixcall_generic_obj_ret all = {0};
                W(4, &all);
                devices = all.ret;
                struct unixcall_generic_obj_uint64_obj_ret first = {devices, 0, 0};
                W(2, &first);
                device = first.ret;
            }
            int before = failures;
            tests[i].fn();
            printf("%s %s\n", failures == before ? "ok  " : "FAIL", tests[i].name);
        }
    }
    struct mn_wm_stats st;
    mn_wm_stats(&st);
    CHECK(st.failures == loop_ctx.failures);
    printf("memory-test: %s\n", failures ? "FAIL" : "all passed");
    return failures != 0;
}
