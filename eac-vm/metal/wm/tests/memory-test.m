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
 *                         until the host says the buffer is gone; neither reaches the host; a length too large to
 *                         round up to whole pages is refused
 *   big-blob              an allocation past 64 MiB, which gets a blob of its own, gives the blob back when freed
 *   sequence              the 4b-1 sequence, as far as the gate runs it
 * Milestone 4c, presentation, in the real window (the stand-in winemac.so): the main thread runs NSApp, as in
 * macneutron-vm, and the cases run on a worker thread. Each frame is the forwarded slots: nextDrawable (67), its
 * texture (66) cleared to (0, 0.3, 0) in a render pass, presentDrawable (47), commit.
 *   present               a frame for HWND 0x1234 counts one drawable presented; the view released (73) on another
 *                         thread before the frame's command buffer is committed: the present still runs, nothing
 *                         crashes, and the view is gone from the window (loopback runs each call at once, so this is
 *                         the drawable keeping its layer; deferral across rings is handles-test's)
 *   probe                 with MN_PRESENT_PROBE=1, 60 frames give one probe line: pixels 57600 green 100
 *   probe-10bit           the same with an RGB10A2 drawable (SMITE 2's): each channel's top 8 bits are read
 *   frames                with MN_PRESENT_STATS=<file> (and a 1 s period), presents give a window line in DXMT_STATS's
 *                         format: "# HH:MM:SS 1.0 s, N frames (F fps), every 1 s" with N > 0 (the last line written
 *                         during the case)
 *   props                 getProps (71) after setProps (70): the guest's device value comes back as it was, the host's
 *                         device pointer is nowhere in the reply, and the drawable size is 320x180
 *   nil-drawable          a layer with no device: nextDrawable gives handle 0 and status 0 (in a window, a 0x0
 *                         drawable takes the view's size instead)
 *   view-left             a context ending with a view still bound leaves no metal view in the window
 *   hud                   DeveloperHUDProperties_instance (62), which every swapchain asks for, answers nil here
 *                         without a failed call
 *   wait-uncommitted      a wait (13) on a buffer whose commit, from another thread (its own ring), the host hasn't
 *                         reached returns once that commit ran and the buffer completed: Metal's wait blocks for a
 *                         later commit, so a wait needs no order across rings (a commit does: a queue runs its
 *                         buffers in the order the host commits them) */
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
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
    struct unixcall_wmtmemory huge = {UINT64_MAX - 100, {NULL}};
    CHECK(W(153, &huge) == MN_STATUS_NO_MEMORY && !huge.ptr.ptr);
    CHECK(loop_ctx.failures == before);
}

static void big_blob(void) {
    struct unixcall_wmtmemory m = {65u << 20, {NULL}};
    CHECK(W(153, &m) == 0 && m.ptr.ptr && loop_mapped(m.ptr.ptr));
    void *p = m.ptr.ptr;
    CHECK(W(154, &m) == 0);
    CHECK(!loop_mapped(p)); /* nothing held it: its blob went with it */
}

static void sequence(void) {
    struct mn_wm_env env = {W, print, signalled};
    CHECK(mn_wm_sequence(&env, SEQUENCE_STEPS) == 0);
}

/* ---- presentation (4c) ---- */

static obj_handle_t pool_push(void) {
    struct unixcall_generic_obj_ret p = {0};
    CHECK(W(10, &p) == 0 && p.ret);
    return p.ret;
}

static obj_handle_t new_queue(void) {
    struct unixcall_generic_obj_uint64_obj_ret q = {device, 64, 0};
    CHECK(W(9, &q) == 0 && q.ret);
    return q.ret;
}

struct swapchain { obj_handle_t view, layer; };

/* A view for hwnd with a w x h drawable of format, as DXMT's presenter sets it up. */
static struct swapchain make_view_as(uint64_t hwnd, double w, double h, enum WMTPixelFormat format) {
    struct unixcall_create_metal_view_from_hwnd c = {hwnd, device, 0, 0};
    CHECK(W(72, &c) == 0 && c.ret_view && c.ret_layer);
    struct WMTLayerProps props = {.device = device, .contents_scale = 1, .drawable_width = w, .drawable_height = h,
                                  .opaque = 1, .framebuffer_only = 0, .pixel_format = format};
    struct unixcall_generic_obj_constptr_noret sp = {c.ret_layer, {&props}};
    CHECK(W(70, &sp) == 0);
    return (struct swapchain){c.ret_view, c.ret_layer};
}

static struct swapchain make_view(uint64_t hwnd, double w, double h) {
    return make_view_as(hwnd, w, h, WMTPixelFormatBGRA8Unorm);
}

static void release_view(obj_handle_t view) {
    struct unixcall_generic_obj_noret r = {view};
    CHECK(W(73, &r) == 0);
    sweep();
}

/* One frame (the caller's pool open); the command buffer, committed and completed unless commit is 0. */
static obj_handle_t frame(obj_handle_t queue, obj_handle_t layer, int commit) {
    struct unixcall_generic_obj_obj_ret nd = {layer, 0};
    CHECK(W(67, &nd) == 0 && nd.ret);
    if (!nd.ret) return 0;
    struct unixcall_generic_obj_obj_ret tx = {nd.ret, 0};
    CHECK(W(66, &tx) == 0 && tx.ret);
    struct unixcall_generic_obj_obj_ret cb = {queue, 0};
    CHECK(W(11, &cb) == 0 && cb.ret);
    struct WMTRenderPassInfo rp;
    memset(&rp, 0, sizeof rp);
    rp.colors[0].texture = tx.ret;
    rp.colors[0].load_action = WMTLoadActionClear;
    rp.colors[0].store_action = WMTStoreActionStore;
    rp.colors[0].clear_color = (struct WMTClearColor){0, 0.3, 0, 1};
    struct unixcall_generic_obj_uint64_obj_ret enc = {cb.ret, (uint64_t)(uintptr_t)&rp, 0};
    CHECK(W(32, &enc) == 0 && enc.ret);
    struct unixcall_generic_obj_noret end = {enc.ret};
    CHECK(W(33, &end) == 0);
    struct unixcall_generic_obj_obj_noret pd = {cb.ret, nd.ret};
    CHECK(W(47, &pd) == 0);
    if (commit) {
        struct unixcall_generic_obj_noret c = {cb.ret};
        CHECK(W(12, &c) == 0 && W(13, &c) == 0);
    }
    return cb.ret;
}

/* The window's content view, from a layer's host object. */
static NSView *content_of(obj_handle_t layer) {
    CAMetalLayer *l = host_obj(layer);
    __block NSView *v = nil;
    dispatch_sync(dispatch_get_main_queue(), ^{ v = [((NSView *)l.delegate).superview retain]; });
    return [v autorelease];
}

static unsigned metal_views(NSView *content) {
    __block unsigned n = 0;
    dispatch_sync(dispatch_get_main_queue(), ^{
        for (NSView *v in content.subviews) n += [v.layer isKindOfClass:[CAMetalLayer class]];
    });
    return n;
}

static obj_handle_t other_view;
static void *release_other(void *arg) {
    (void)arg;
    release_view(other_view);
    return NULL;
}

static void present(void) {
    uint64_t before = loop_ctx.presented;
    struct swapchain s = make_view(0x1234, 320, 180);
    if (!s.layer) return;
    NSView *content = content_of(s.layer);
    CHECK(content && metal_views(content) == 1);
    obj_handle_t q = new_queue(), pool = pool_push();
    obj_handle_t cb = frame(q, s.layer, 0);
    CHECK(loop_ctx.presented == before + 1);
    other_view = s.view; /* released on another thread (its own ring) while the present waits for its commit */
    pthread_t t;
    pthread_create(&t, NULL, release_other, NULL);
    pthread_join(t, NULL);
    CHECK(metal_views(content) == 0);
    struct unixcall_generic_obj_noret c = {cb};
    CHECK(cb && W(12, &c) == 0 && W(13, &c) == 0);
    struct unixcall_generic_obj_uint64_ret st = {cb, 0};
    CHECK(W(14, &st) == 0 && st.ret == MTLCommandBufferStatusCompleted);
    release(pool);
    release(q);
}

/* stderr, into a file, while the probe case runs. */
static int stderr_to(const char *path) {
    fflush(stderr);
    int saved = dup(2), fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(fd, 2);
    close(fd);
    return saved;
}

static void probe_as(enum WMTPixelFormat format) {
    struct swapchain s = make_view_as(0x1234, 320, 180, format);
    if (!s.layer) return;
    obj_handle_t q = new_queue();
    char path[] = "/tmp/memory-test-probe-XXXXXX";
    close(mkstemp(path));
    int saved = stderr_to(path);
    for (int i = 0; i < 60; i++) {
        obj_handle_t pool = pool_push();
        frame(q, s.layer, 1);
        release(pool);
    }
    char text[4096] = "";
    for (int i = 0; i < 200 && !strstr(text, "colors"); i++) { /* the completed handler logs it */
        usleep(10000);
        FILE *f = fopen(path, "r");
        size_t n = f ? fread(text, 1, sizeof text - 1, f) : 0;
        text[n] = 0;
        if (f) fclose(f);
    }
    fflush(stderr);
    dup2(saved, 2);
    close(saved);
    unlink(path);
    const char *line = strstr(text, "mn-present: probe ");
    CHECK(line && !strstr(line + 1, "mn-present: probe ")); /* one in 60 frames */
    CHECK(line && strstr(line, " pixels 57600 green 100 white 0 colors 1\n"));
    if (line) fprintf(stderr, "  %.*s", (int)(strchr(line, '\n') ? strchr(line, '\n') - line + 1 : 0), line);
    release_view(s.view);
    release(q);
}

static char frames_file[] = "/tmp/memory-test-frames-XXXXXX";

static void frames(void) {
    struct swapchain s = make_view_as(0x1234, 320, 180, WMTPixelFormatBGRA8Unorm);
    if (!s.layer) return;
    obj_handle_t q = new_queue();
    struct stat sb;
    long from = stat(frames_file, &sb) == 0 ? (long)sb.st_size : 0; /* earlier cases' presents wrote lines already */
    struct timespec t0, t;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    do { /* two periods of presents */
        obj_handle_t pool = pool_push();
        frame(q, s.layer, 1);
        release(pool);
        clock_gettime(CLOCK_MONOTONIC, &t);
    } while ((t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000 < 2200);
    char text[4096] = "";
    FILE *f = fopen(frames_file, "r");
    size_t n = f && fseek(f, from, SEEK_SET) == 0 ? fread(text, 1, sizeof text - 1, f) : 0;
    text[n] = 0;
    if (f) fclose(f);
    unsigned long long frames = 0;
    const char *line = strrchr(text, '#'); /* the last window line, from this case's presents */
    CHECK(line && sscanf(line, "# %*u:%*u:%*u %*f s, %llu frames (", &frames) == 1 && frames > 0);
    CHECK(line && strstr(line, " fps), every 1 s\n"));
    if (line) fprintf(stderr, "  %.*s", (int)(strchr(line, '\n') ? strchr(line, '\n') - line + 1 : 0), line);
    release_view(s.view);
    release(q);
}

static void probe(void) { probe_as(WMTPixelFormatBGRA8Unorm); }
static void probe_10bit(void) { probe_as(WMTPixelFormatRGB10A2Unorm); } /* SMITE 2's swapchain */

static void props(void) {
    struct swapchain s = make_view(0x1234, 320, 180);
    if (!s.layer) return;
    struct WMTLayerProps p;
    memset(&p, 0xab, sizeof p); /* DXMT's are uninitialised before its first getProps */
    obj_handle_t guest_value = p.device;
    struct unixcall_generic_obj_ptr_noret gp = {s.layer, {&p}};
    /* The record and the host's reply, by hand: the host's device pointer must be nowhere in it. */
    static uint8_t rec[MN_REC_MAX], reply[MN_REPLY_BYTES];
    struct mn_wm_out o = {.buf = rec, .cap = sizeof rec};
    uint32_t n = 0;
    void *host_device = host_obj(device);
    CHECK(mn_wm_pack(71, &gp, &o) == 0 && o.sync);
    CHECK(mn_wm_host_call(&loop_ctx, loop_ring(), 71, rec, o.len, reply, sizeof reply, &n) == 0 && n > 16);
    CHECK(!memmem(reply, n, &host_device, sizeof host_device));
    CHECK(mn_wm_unpack(71, &gp, &o, reply, n) == 0);
    CHECK(p.device == guest_value);
    CHECK(p.drawable_width == 320 && p.drawable_height == 180 && p.pixel_format == WMTPixelFormatBGRA8Unorm &&
          !p.framebuffer_only && p.opaque);
    p.device = guest_value;
    CHECK(W(71, &gp) == 0 && p.device == guest_value); /* and through the guest's table */
    release_view(s.view);
}

static void nil_drawable(void) {
    struct swapchain s = make_view(0x1234, 320, 180);
    if (!s.layer) return;
    struct WMTLayerProps none = {.contents_scale = 1, .drawable_width = 320, .drawable_height = 180,
                                 .pixel_format = WMTPixelFormatBGRA8Unorm}; /* device 0: nil */
    struct unixcall_generic_obj_constptr_noret sp = {s.layer, {&none}};
    CHECK(W(70, &sp) == 0);
    obj_handle_t pool = pool_push();
    struct unixcall_generic_obj_obj_ret nd = {s.layer, 0};
    CHECK(W(67, &nd) == 0 && nd.ret == 0);
    release(pool);
    release_view(s.view);
}

/* view-left: a second context of its own (the loopback's ends with the process), its calls packed and answered by
 * hand on this thread's ring. */
static size_t no_marks(void *c, void *e, struct mn_mark *m, size_t cap) { (void)c, (void)e, (void)m, (void)cap; return 0; }
static int all_passed(void *c, const struct mn_mark *m, size_t n) { (void)c, (void)m, (void)n; return 1; }

static NTSTATUS_T call_on(struct mn_wm_ctx *c, unsigned slot, void *params) {
    static uint8_t rec[MN_REC_MAX], reply[MN_REPLY_BYTES];
    struct mn_wm_out o = {.buf = rec, .cap = sizeof rec};
    NTSTATUS_T s = mn_wm_pack(slot, params, &o);
    if (s) return s;
    uint32_t n = 0;
    int32_t r = mn_wm_host_call(c, loop_ring(), slot, rec, o.len, o.sync ? reply : NULL, o.sync ? sizeof reply : 0, &n);
    if (!o.sync) return 0;
    if (r) {
        mn_wm_unwind(&o, 1);
        return (NTSTATUS_T)r;
    }
    return mn_wm_unpack(slot, params, &o, reply, n);
}

static void hud(void) {
    uint64_t before = loop_ctx.failures;
    struct unixcall_generic_obj_ret h = {0};
    CHECK(W(62, &h) == 0 && h.ret == 0); /* no HUD in a process like this one or macneutron-vm */
    CHECK(loop_ctx.failures == before);
}

static void view_left(void) {
    struct mn_ht_rings rings = {NULL, no_marks, all_passed};
    struct mn_wm_ctx c2 = {.ctx_id = 2, .ht = mn_ht_new(&rings, mn_wm_release_object)};
    struct unixcall_generic_obj_ret all = {0};
    CHECK(call_on(&c2, 4, &all) == 0 && all.ret);
    struct unixcall_generic_obj_uint64_obj_ret first = {all.ret, 0, 0};
    CHECK(call_on(&c2, 2, &first) == 0 && first.ret);
    struct unixcall_create_metal_view_from_hwnd v = {0x9abc, first.ret, 0, 0};
    CHECK(call_on(&c2, 72, &v) == 0 && v.ret_view && v.ret_layer);
    int ok;
    CAMetalLayer *l = mn_ht_get(c2.ht, v.ret_layer, MN_HK_OBJC, &ok);
    __block NSView *content = nil;
    if (l) dispatch_sync(dispatch_get_main_queue(), ^{ content = [((NSView *)l.delegate).superview retain]; });
    CHECK(content && metal_views(content) == 1);
    mn_ht_free(c2.ht); /* the context's end, as dispatch.m's */
    CHECK(content && metal_views(content) == 0);
    [content release];
}

static obj_handle_t late_cb;
static void *commit_late(void *arg) { /* another thread: its own ring */
    (void)arg;
    usleep(100000);
    struct unixcall_generic_obj_noret c = {late_cb};
    CHECK(W(12, &c) == 0);
    return NULL;
}

static void wait_uncommitted(void) {
    obj_handle_t q = new_queue(), pool = pool_push();
    struct unixcall_generic_obj_obj_ret nc = {q, 0};
    CHECK(W(11, &nc) == 0 && nc.ret);
    late_cb = nc.ret;
    pthread_t t;
    pthread_create(&t, NULL, commit_late, NULL);
    struct unixcall_generic_obj_noret w = {late_cb};
    CHECK(W(13, &w) == 0);
    struct unixcall_generic_obj_uint64_ret st = {late_cb, 0};
    CHECK(W(14, &st) == 0 && st.ret == MTLCommandBufferStatusCompleted);
    pthread_join(t, NULL);
    release(pool);
    release(q);
}

static void *run(void *arg) {
    (void)arg;
    struct { const char *name; void (*fn)(void); } tests[] = {
        {"galloc", galloc},           {"buffer-outlives-free", buffer_outlives_free},
        {"update-bounds", update_bounds}, {"region-bounds", region_bounds},
        {"heap", heap},               {"wmt-memory", wmt_memory}, {"big-blob", big_blob},
        {"sequence", sequence},
        {"present", present},         {"probe", probe},           {"probe-10bit", probe_10bit},
        {"frames", frames},
        {"props", props},
        {"nil-drawable", nil_drawable}, {"view-left", view_left}, {"hud", hud},
        {"wait-uncommitted", wait_uncommitted},
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
    unlink(frames_file);
    printf("memory-test: %s\n", failures ? "FAIL" : "all passed");
    fflush(stdout);
    exit(failures != 0);
}

int main(int argc, char **argv) {
    if (argc != 2 || mn_wm_load(argv[1])) {
        fprintf(stderr, "memory-test: FAIL loading winemetal.so\n");
        return 1;
    }
    setenv("MN_PRESENT_PROBE", "1", 1); /* before the first present: the host reads it once */
    close(mkstemp(frames_file));
    setenv("MN_PRESENT_STATS", frames_file, 1); /* the same */
    setenv("MN_PRESENT_STATS_PERIOD", "1", 1);
    alarm(600);                         /* a main-thread deadlock fails the test instead of hanging it */
    [NSApplication sharedApplication];
    dispatch_async(dispatch_get_main_queue(), ^{ /* the cases start once the main thread runs the application */
        pthread_t t;
        pthread_create(&t, NULL, run, NULL);
    });
    [NSApp run];
    return 1;
}
