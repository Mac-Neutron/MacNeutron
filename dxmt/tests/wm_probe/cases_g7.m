/* wm_probe's g7 cases (slots.txt's g7; probe.h), recorded from DXMT's C: shared events, fences, the shared-event
 * listener (108-110, 104, 146), Mach ports and the bootstrap server. A listener runs on a probe thread T (109 parks
 * it in its run loop); the stand-in's NtSetEvent hook (Wine mode) or a native relay event (native mode) reports the
 * set and its thread. Every wait has a time limit, so a stubbed slot (cover.sh bites) shows instead of hanging; what
 * a hook or a thread saw is printed by the worker on the line of the call it observes. */
#include "probe.h"
#include "../util/win32_native_event.h"
#include <servers/bootstrap.h>
#include <sys/resource.h>

#define WAIT_NS (5 * NSEC_PER_SEC)

/* ---- the hook: what NtSetEvent (or a native relay event) saw ---- */

static dispatch_semaphore_t hook_sem;
static _Atomic unsigned hook_calls;
static _Atomic uint64_t hook_handle;
static pthread_t hook_thread;

static void hook(uint64_t h) {
    hook_handle = h;
    hook_thread = pthread_self();
    hook_calls++;
    dispatch_semaphore_signal(hook_sem);
}
static void relay(void *ctx) { hook((uint64_t)(uintptr_t)ctx); }

static void hook_reset(void) {
    hook_sem = dispatch_semaphore_create(0);
    hook_calls = 0, hook_handle = 0;
    if (!wm_native()) wm_event_hook(hook);
}

static bool hook_wait(void) { return !dispatch_semaphore_wait(hook_sem, dispatch_time(DISPATCH_TIME_NOW, WAIT_NS)); }

/* ---- a listener's thread ---- */

struct starter {
    obj_handle_t listener;
    pthread_t thread;
    CFRunLoopRef loop;
    dispatch_semaphore_t done;
    _Atomic int destroyed; /* the worker is past 110 */
    bool quiet;            /* no after-destroy field (a race's answer varies) */
};

static void *start_main(void *arg) {
    struct starter *s = arg;
    s->loop = CFRunLoopGetCurrent();
    struct unixcall_generic_obj_noret p = {.handle = s->listener};
    wm_call(109, &p);
    if (!s->quiet) wm_val("after-destroy", "%d", s->destroyed);
    dispatch_semaphore_signal(s->done);
    return NULL;
}

static void start_q(struct starter *s, obj_handle_t listener, bool quiet) {
    memset(s, 0, sizeof *s);
    s->listener = listener;
    s->quiet = quiet;
    s->done = dispatch_semaphore_create(0);
    pthread_create(&s->thread, NULL, start_main, s);
}
static void start(struct starter *s, obj_handle_t listener) { start_q(s, listener, false); }

/* T's 109 returned (it's joined: its line is printed) or not within the limit. */
static bool finished(struct starter *s) {
    if (dispatch_semaphore_wait(s->done, dispatch_time(DISPATCH_TIME_NOW, WAIT_NS))) return false;
    pthread_join(s->thread, NULL);
    return true;
}

/* The listener's first word non-null (T's start published its run loop), or 0 at the limit. */
static uint64_t published(obj_handle_t listener) {
    for (int i = 0; i < 500; i++) {
        uint64_t w = wm_word(listener);
        if (w) return w;
        usleep(10000);
    }
    return 0;
}

static obj_handle_t create(void) {
    struct unixcall_generic_obj_ret p;
    wm_fill(&p, sizeof p);
    wm_call(108, &p);
    wm_h("ret", p.ret);
    return p.ret;
}

static void destroy_and_join(struct starter *s) {
    s->destroyed = 1;
    struct unixcall_generic_obj_noret p = {.handle = s->listener};
    wm_call(110, &p);
    wm_val("thread", "%s", finished(s) ? "returned" : "stuck");
}

static void set_event_at(id<MTLSharedEvent> ev, uint64_t nt, obj_handle_t listener, uint64_t value) {
    struct unixcall_mtlsharedevent_setevent p = {
        .shared_event = (obj_handle_t)ev, .event_handle = nt, .shared_event_listener = listener, .value = value};
    wm_call(104, &p);
}

static id<MTLSharedEvent> shared_event(void) { return [[wm_device newSharedEvent] autorelease]; }

/* What the hook saw, on the observing call's line: the handle it was given, set on T's thread. */
static void hooked(struct starter *s, uint64_t given) {
    if (!hook_wait()) {
        wm_val("hook", "timeout");
        return;
    }
    wm_eq("hook", hook_handle, given, "given");
    wm_val("same-thread", "%d", pthread_equal(hook_thread, s->thread) ? 1 : 0);
}

static double cpu_now(void) {
    struct rusage r;
    getrusage(RUSAGE_SELF, &r);
    return r.ru_utime.tv_sec + r.ru_stime.tv_sec + (r.ru_utime.tv_usec + r.ru_stime.tv_usec) / 1e6;
}

/* The signal, then 300 ms: the hook's calls and the CPU time those 300 ms took (a spin takes them all). */
static void signal_after_end(id<MTLSharedEvent> ev) {
    double t0 = cpu_now();
    ev.signaledValue = 1;
    usleep(300000);
    double t = cpu_now() - t0;
    wm_val("hooks", "%u", hook_calls);
    wm_rel("cpu", t < 0.1, "no-spin", "%.3f", t);
}

/* ---- cases ---- */

static void shared_event_case(void) {
    struct unixcall_generic_obj_obj_ret p;
    wm_fill(&p, sizeof p);
    p.handle = (obj_handle_t)wm_device;
    wm_own("ret", &p.ret);
    wm_call(15, &p);
    if (!p.ret || p.ret == WM_FILL64) return;
    id<MTLSharedEvent> ev = (id)p.ret;
    wm_val("shared", "%d", [ev conformsToProtocol:@protocol(MTLSharedEvent)]);
    wm_in(ev);
    struct unixcall_generic_obj_uint64_noret s = {.handle = p.ret, .arg = 7};
    wm_call(103, &s);
    wm_val("signaled", "%llu", (unsigned long long)ev.signaledValue);
    struct unixcall_generic_obj_uint64_ret v;
    wm_fill(&v, sizeof v);
    v.handle = p.ret;
    wm_call(16, &v);
    wm_val("ret", "%llu", (unsigned long long)v.ret);
}

static void fence_event(void) {
    struct unixcall_generic_obj_obj_ret p;
    wm_fill(&p, sizeof p);
    p.handle = (obj_handle_t)wm_device;
    wm_own("ret", &p.ret);
    wm_call(105, &p);
    if (p.ret && p.ret != WM_FILL64) wm_val("fence", "%d", [(id)p.ret conformsToProtocol:@protocol(MTLFence)]);
    wm_fill(&p, sizeof p);
    p.handle = (obj_handle_t)wm_device;
    wm_own("ret", &p.ret);
    wm_call(106, &p);
    if (p.ret && p.ret != WM_FILL64)
        wm_val("event", "%d shared=%d", [(id)p.ret conformsToProtocol:@protocol(MTLEvent)],
               [(id)p.ret conformsToProtocol:@protocol(MTLSharedEvent)]);
}

/* 108, 109 on T, 104, a signal: NtSetEvent on T with the handle 104 was given; 110; T returns. */
static void win32_event(void) {
    hook_reset();
    obj_handle_t l = create();
    struct starter s;
    start(&s, l);
    wm_val("published", "%d", published(l) != 0);
    id<MTLSharedEvent> ev = shared_event();
    set_event_at(ev, 0x1234, l, 1);
    ev.signaledValue = 1;
    hooked(&s, 0x1234);
    destroy_and_join(&s);
}

/* After T's start published and before 110: the listener's first word is T's run loop. */
static void first_word(void) {
    obj_handle_t l = create();
    struct starter s;
    start(&s, l);
    uint64_t w = published(l);
    wm_rel("first-word", w && w == (uint64_t)s.loop, "runloop(T)", "0x%llx", (unsigned long long)w);
    destroy_and_join(&s);
}

/* 146, count 3, needed 2: one set, on T, after the second signal. */
static void win32_events(void) {
    hook_reset();
    obj_handle_t l = create();
    struct starter s;
    start(&s, l);
    wm_val("published", "%d", published(l) != 0);
    id<MTLSharedEvent> ev[3] = {shared_event(), shared_event(), shared_event()};
    obj_handle_t evs[3] = {(obj_handle_t)ev[0], (obj_handle_t)ev[1], (obj_handle_t)ev[2]};
    uint64_t values[3] = {1, 2, 3};
    struct unixcall_mtlsharedevent_seteventatvalues p = {.shared_event_listener = l,
                                                         .shared_events = {.ptr = evs},
                                                         .values = {.ptr = values},
                                                         .count = 3,
                                                         .needed = 2,
                                                         .event_handle = 0x5678};
    wm_call(146, &p);
    ev[0].signaledValue = 1;
    usleep(50000);
    wm_val("after-one", "%u", hook_calls);
    ev[1].signaledValue = 2;
    hooked(&s, 0x5678);
    usleep(100000);
    wm_val("hooks", "%u", hook_calls);
    destroy_and_join(&s);
}

/* The notification comes before the start: NtSetEvent still runs on T once it starts (the C's success path). */
static void notify_before_start(void) {
    hook_reset();
    obj_handle_t l = create();
    id<MTLSharedEvent> ev = shared_event();
    set_event_at(ev, 0x2345, l, 1);
    ev.signaledValue = 1;
    usleep(50000);
    wm_val("before-start", "%u", hook_calls);
    struct starter s;
    start(&s, l);
    hooked(&s, 0x2345);
    destroy_and_join(&s);
}

/* Destroy, then start, on the worker: the C publishes and runs forever (hang); D5 makes start return. */
static void destroy_first(void) {
    obj_handle_t l = create();
    struct unixcall_generic_obj_noret p = {.handle = l};
    wm_call(110, &p);
    wm_call(109, &p);
    wm_line("returned");
}

/* The table's own entry, outside wm_call (no line, no count, no stub): the race's 99 unprinted rounds. */
static unsigned raw(unsigned slot, void *params) {
    unsigned (*const *t)(void *) = wm_sym("__wine_unix_call_funcs");
    return t ? t[slot](params) : 0;
}

struct raw_starter {
    obj_handle_t listener;
    dispatch_semaphore_t done;
};
static void *raw_start_main(void *arg) {
    struct raw_starter *s = arg;
    struct unixcall_generic_obj_noret p = {.handle = s->listener};
    raw(109, &p);
    dispatch_semaphore_signal(s->done);
    return NULL;
}

/* Start and destroy racing, 100 rounds (the first printed): every start returns. */
static void stop_race(void) {
    struct starter s;
    obj_handle_t l = create();
    start_q(&s, l, true);
    struct unixcall_generic_obj_noret p = {.handle = l};
    wm_call(110, &p);
    wm_val("thread", "%s", finished(&s) ? "returned" : "stuck");
    for (int i = 1; i < 100; i++) {
        struct unixcall_generic_obj_ret c;
        raw(108, &c);
        struct raw_starter r = {.listener = c.ret, .done = dispatch_semaphore_create(0)};
        pthread_t t;
        pthread_create(&t, NULL, raw_start_main, &r);
        struct unixcall_generic_obj_noret d = {.handle = c.ret};
        raw(110, &d);
        if (dispatch_semaphore_wait(r.done, dispatch_time(DISPATCH_TIME_NOW, WAIT_NS))) {
            wm_line("stuck round=%d", i);
            return;
        }
        pthread_join(t, NULL);
    }
    wm_line("rounds=100");
}

/* A notification after 110: NtSetEvent never runs, nothing spins. */
static void late_notify(void) {
    hook_reset();
    obj_handle_t l = create();
    struct starter s;
    start(&s, l);
    wm_val("published", "%d", published(l) != 0);
    id<MTLSharedEvent> ev = shared_event();
    set_event_at(ev, 0x3456, l, 1);
    destroy_and_join(&s);
    signal_after_end(ev);
    wm_line("returned");
}

/* Create, 104, destroy, then start (returns at once), then the signal: the same. */
static void destroy_first_notify(void) {
    hook_reset();
    obj_handle_t l = create();
    id<MTLSharedEvent> ev = shared_event();
    set_event_at(ev, 0x4567, l, 1);
    struct unixcall_generic_obj_noret p = {.handle = l};
    wm_call(110, &p);
    struct starter s;
    start(&s, l);
    wm_val("thread", "%s", finished(&s) ? "returned" : "stuck");
    signal_after_end(ev);
    wm_line("returned");
}

static void wait_case(void) {
    id<MTLSharedEvent> ev = shared_event();
    ev.signaledValue = 5;
    wm_in(ev);
    struct unixcall_mtlsharedevent_waituntilsignaledvalue p;
    for (unsigned i = 0; i < 2; i++) {
        wm_fill(&p, sizeof p);
        p.event = (obj_handle_t)ev, p.value = 5 + i, p.timeout_ms = i ? 10 : 1000;
        wm_call(126, &p);
        wm_val("ret_timeout", "%u", (unsigned)*(uint8_t *)&p.ret_timeout);
    }
}

/* 121 then 122 for a unique name; 122 for an unknown one (c0000001, mach_port written); 121 twice. */
static void bootstrap(void) {
    mach_port_t port = MACH_PORT_NULL;
    mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port);
    mach_port_insert_right(mach_task_self(), port, port, MACH_MSG_TYPE_MAKE_SEND);
    static unsigned k;
    struct unixcall_bootstrap p;
    wm_fill(&p, sizeof p);
    snprintf(p.name, sizeof p.name, "com.macneutron.wm-probe.%d.%u", getpid(), k++);
    p.mach_port = port;
    wm_call(121, &p);
    mach_port_t found = MACH_PORT_NULL, bp = MACH_PORT_NULL;
    task_get_bootstrap_port(mach_task_self(), &bp);
    kern_return_t kr = bootstrap_look_up(bp, p.name, &found);
    wm_val("registered", "%s", kr ? "no" : found == port ? "yes" : "other");
    struct unixcall_bootstrap q;
    wm_fill(&q, sizeof q);
    memcpy(q.name, p.name, sizeof q.name);
    wm_call(122, &q);
    wm_eq("mach_port", q.mach_port, port, "registered");
    wm_send_refs(port);
    wm_val("reserved", "%s", q.reserved == 0xA5A5A5A5u ? "fill" : "written");
    wm_fill(&q, sizeof q);
    snprintf(q.name, sizeof q.name, "com.macneutron.wm-probe.%d.none", getpid());
    wm_call(122, &q);
    wm_val("mach_port", "%#x", q.mach_port);
    p.mach_port = port;
    wm_call(121, &p);
}

/* 123's port into 124: one event seen through both; the send right's references after each. */
static void mach_event(void) {
    id<MTLSharedEvent> ev = shared_event();
    wm_in(ev);
    struct unixcall_mtlsharedevent_createmachport c;
    wm_fill(&c, sizeof c);
    c.event = (obj_handle_t)ev;
    wm_call(123, &c);
    if (c.ret_mach_port == 0xA5A5A5A5u || !c.ret_mach_port) {
        wm_val("port", "%s", c.ret_mach_port ? "fill" : "0");
        return;
    }
    wm_val("port", "nonzero");
    wm_send_refs(c.ret_mach_port);
    struct unixcall_mtldevice_newsharedeventwithmachport n;
    wm_fill(&n, sizeof n);
    n.device = (obj_handle_t)wm_device, n.mach_port = c.ret_mach_port;
    wm_own("ret_event", &n.ret_event);
    wm_call(124, &n);
    wm_send_refs(c.ret_mach_port);
    if (!n.ret_event || n.ret_event == WM_FILL64) return;
    id<MTLSharedEvent> other = (id)n.ret_event;
    ev.signaledValue = 3;
    wm_val("seen-there", "%llu", (unsigned long long)other.signaledValue);
    other.signaledValue = 4;
    wm_val("seen-here", "%llu", (unsigned long long)ev.signaledValue);
}

/* The info's fields a call wrote (changed from before): their names, or none. */
static void written(const char *name, const struct WMTTextureInfo *a, const struct WMTTextureInfo *b) {
    char s[160] = "";
#define F(cond, n) if (cond) snprintf(s + strlen(s), sizeof s - strlen(s), "%s%s", *s ? "," : "", n)
    F(a->pixel_format != b->pixel_format, "pixel_format");
    F(a->width != b->width, "width");
    F(a->height != b->height, "height");
    F(a->depth != b->depth, "depth");
    F(a->array_length != b->array_length, "array_length");
    F(a->type != b->type, "type");
    F(a->mipmap_level_count != b->mipmap_level_count, "mipmap_level_count");
    F(a->sample_count != b->sample_count, "sample_count");
    F(a->usage != b->usage, "usage");
    F(a->options != b->options, "options");
    F(a->flags != b->flags, "flags");
    F(a->mach_port != b->mach_port, "mach_port");
    F(a->gpu_resource_id != b->gpu_resource_id, "gpu_resource_id");
#undef F
    wm_val(name, "%s", *s ? s : "none");
}

static void texture_info(const char *prefix, const struct WMTTextureInfo *i) {
    wm_val(prefix, "type=%u,format=%u,%ux%ux%u,array=%u,mips=%u,samples=%u,usage=%u,options=%llu,flags=%u",
           (unsigned)i->type, (unsigned)i->pixel_format, i->width, i->height, i->depth, i->array_length,
           (unsigned)i->mipmap_level_count, (unsigned)i->sample_count, (unsigned)i->usage,
           (unsigned long long)i->options, i->flags);
}

/* 120 exports (mach_port 0: port written, info's gpu_resource_id) and imports (the port: the whole info written). */
static void shared_texture(void) {
    struct WMTTextureInfo info, before;
    wm_fill(&info, sizeof info);
    info.pixel_format = WMTPixelFormatBGRA8Unorm;
    info.width = 256, info.height = 128, info.depth = 1, info.array_length = 1;
    info.type = WMTTextureType2D, info.mipmap_level_count = 1, info.sample_count = 1;
    info.usage = WMTTextureUsageShaderRead | WMTTextureUsageRenderTarget;
    info.options = WMTResourceStorageModePrivate, info.flags = 0, info.mach_port = 0;
    before = info;
    struct unixcall_mtldevice_newtexture p;
    wm_fill(&p, sizeof p);
    p.device = (obj_handle_t)wm_device, p.info.ptr = &info;
    wm_own("ret", &p.ret);
    wm_call(120, &p);
    written("written", &before, &info);
    if (!p.ret || p.ret == WM_FILL64) return;
    id<MTLTexture> t = (id)p.ret;
    wm_eq("gpu_resource_id", info.gpu_resource_id, t.gpuResourceID._impl, "gpuResourceID");
    wm_val("mach_port", "%s", info.mach_port ? "nonzero" : "0");
    if (!info.mach_port) return;
    wm_send_refs(info.mach_port);
    struct WMTTextureInfo in;
    wm_fill(&in, sizeof in);
    in.mach_port = info.mach_port;
    before = in;
    wm_fill(&p, sizeof p);
    p.device = (obj_handle_t)wm_device, p.info.ptr = &in;
    wm_own("ret", &p.ret);
    wm_call(120, &p);
    written("written", &before, &in);
    texture_info("info", &in);
    if (!p.ret || p.ret == WM_FILL64) return;
    id<MTLTexture> u = (id)p.ret;
    wm_eq("gpu_resource_id", in.gpu_resource_id, u.gpuResourceID._impl, "gpuResourceID");
    wm_val("same-size-format", "%d", u.width == t.width && u.height == t.height && u.pixelFormat == t.pixelFormat);
    wm_send_refs(info.mach_port);
}

/* Natively: 104 holds the mn_win32 event (one more reference) until its set on T drops it. */
static void win32_event_native(void) {
    hook_reset();
    void *e = mn_win32_relay_event(relay, NULL, (void *)0x6789);
    obj_handle_t l = create();
    struct starter s;
    start(&s, l);
    wm_val("published", "%d", published(l) != 0);
    id<MTLSharedEvent> ev = shared_event();
    int refs = ((struct mn_win32_handle *)e)->refs;
    set_event_at(ev, (obj_handle_t)e, l, 1);
    wm_val("held", "%+d", ((struct mn_win32_handle *)e)->refs - refs);
    ev.signaledValue = 1;
    hooked(&s, 0x6789);
    destroy_and_join(&s);
    wm_val("refs", "%+d", ((struct mn_win32_handle *)e)->refs - refs);
    mn_win32_unref(e);
}

/* Each object argument as 0, sent on as nil (a listener is not an object: 109/110 with 0 return at once). */
static void nil_args(void) {
    struct unixcall_generic_obj_obj_ret o;
    for (unsigned slot = 15; slot; slot = slot == 15 ? 105 : slot == 105 ? 106 : 0) {
        wm_fill(&o, sizeof o);
        o.handle = 0;
        wm_own("ret", &o.ret);
        wm_call(slot, &o);
    }
    struct unixcall_generic_obj_uint64_ret v;
    wm_fill(&v, sizeof v);
    v.handle = 0;
    wm_call(16, &v);
    wm_val("ret", "%llu", (unsigned long long)v.ret);
    struct unixcall_generic_obj_uint64_noret sv = {.handle = 0, .arg = 5};
    wm_call(103, &sv);
    obj_handle_t l = create();
    set_event_at(nil, 0, l, 1);
    obj_handle_t evs[1] = {0};
    uint64_t values[1] = {1};
    struct unixcall_mtlsharedevent_seteventatvalues m = {.shared_event_listener = l,
                                                         .shared_events = {.ptr = evs},
                                                         .values = {.ptr = values},
                                                         .count = 1,
                                                         .needed = 1,
                                                         .event_handle = 0};
    wm_call(146, &m);
    struct unixcall_generic_obj_noret z = {.handle = 0};
    wm_call(109, &z);
    wm_call(110, &z);
    struct WMTTextureInfo info;
    wm_fill(&info, sizeof info);
    info.pixel_format = WMTPixelFormatBGRA8Unorm, info.width = 16, info.height = 16, info.depth = 1;
    info.array_length = 1, info.type = WMTTextureType2D, info.mipmap_level_count = 1, info.sample_count = 1;
    info.usage = WMTTextureUsageShaderRead, info.options = WMTResourceStorageModePrivate, info.flags = 0;
    info.mach_port = 0;
    struct unixcall_mtldevice_newtexture t = {.device = 0, .info = {.ptr = &info}};
    wm_fill(&t.ret, sizeof t.ret);
    wm_own("ret", &t.ret);
    wm_call(120, &t);
    wm_val("gpu_resource_id", "%llu", (unsigned long long)info.gpu_resource_id);
    wm_val("mach_port", "%u", info.mach_port);
    struct unixcall_mtlsharedevent_createmachport c;
    wm_fill(&c, sizeof c);
    c.event = 0;
    wm_call(123, &c);
    wm_val("ret_mach_port", "%u", c.ret_mach_port);
    struct unixcall_mtldevice_newsharedeventwithmachport n;
    wm_fill(&n, sizeof n);
    n.device = 0, n.mach_port = 0;
    wm_own("ret_event", &n.ret_event);
    wm_call(124, &n);
    struct unixcall_mtlsharedevent_waituntilsignaledvalue w;
    wm_fill(&w, sizeof w);
    w.event = 0, w.value = 1, w.timeout_ms = 1;
    wm_call(126, &w);
    wm_val("ret_timeout", "%u", (unsigned)*(uint8_t *)&w.ret_timeout);
}

static void x_signal_on_fence(void) {
    id<MTLFence> f = [[wm_device newFence] autorelease];
    struct unixcall_generic_obj_uint64_noret p = {.handle = (obj_handle_t)f, .arg = 1};
    wm_call(103, &p);
}

#define LISTENER (WM_ISOLATE | WM_GMALLOC)
const struct wm_case wm_cases_g7[] = {
    {"g7", 15, "shared-event", 0, shared_event_case},
    {"g7", 105, "fence-event", 0, fence_event},
    {"g7", 104, "win32-event", WM_WINE_ONLY, win32_event},
    {"g7", 108, "first-word", 0, first_word},
    {"g7", 146, "win32-events", WM_WINE_ONLY, win32_events},
    {"g7", 104, "listener-notify-before-start", LISTENER | WM_WINE_ONLY, notify_before_start},
    {"g7", 109, "listener-destroy-first", LISTENER | WM_FULL_ONLY, destroy_first},
    {"g7", 110, "listener-stop-race", LISTENER | WM_NOT_ON_C, stop_race},
    {"g7", 110, "listener-late-notify", LISTENER | WM_NOT_ON_C | WM_WINE_ONLY, late_notify},
    {"g7", 110, "listener-destroy-first-notify", LISTENER | WM_NOT_ON_C | WM_WINE_ONLY, destroy_first_notify},
    {"g7", 126, "wait", 0, wait_case},
    {"g7", 121, "bootstrap", 0, bootstrap},
    {"g7", 123, "mach-event", 0, mach_event},
    {"g7", 120, "shared-texture", 0, shared_texture},
    {"g7", 104, "win32-event-native", WM_NATIVE, win32_event_native},
    {"g7", 15, "nil-args", 0, nil_args},
    {"g7", 103, "x-signal-on-fence", 0, x_signal_on_fence},
    {0},
};
