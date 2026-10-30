/* wm_probe's g1 cases (slots.txt's g1; probe.h), recorded from DXMT's C: objects (0-3), the device queries, strings,
 * pools (10), labels, dispatch data and the OS version. Values that depend on this Mac (the device, its limits, its
 * strings) are compared with what Metal and Foundation answer directly (wm_eq and wm_rel), so a line holds on any Mac.
 * Slot 10 never gets a wm_own (probe.h): its ownership is the pool-drain cases. */
#include "probe.h"
#include <objc/runtime.h>
#include <string.h>

extern id objc_storeWeak(id *location, id obj);
extern id objc_loadWeakRetained(id *location);

/* A plain Objective-C class that is not a Metal type; its description is a string long enough to be heap-allocated. */
@interface WMG1Plain : NSObject
@end
@implementation WMG1Plain
- (NSString *)description {
    return [NSString stringWithFormat:@"WMG1Plain description %d (a heap string)", 42];
}
@end

static bool weak_alive(id *weak) {
    id o = objc_loadWeakRetained(weak);
    if (!o) return false;
    [o release];
    return true;
}

/* A relation both modes print the same way (pointers and clocks have no raw value worth a golden). */
static void same(const char *name, bool holds, const char *relation, const char *otherwise) {
    wm_val(name, "%s", holds ? relation : otherwise);
}

/* An unsigned value; the 0xA5 fill (the handler wrote nothing) prints as "fill". */
static void num(const char *name, uint64_t v) {
    if (v == WM_FILL64) wm_val(name, "fill");
    else wm_val(name, "%llu", (unsigned long long)v);
}

static uint64_t call_obj_uint64_ret(unsigned slot, uint64_t handle, uint64_t arg) {
    struct unixcall_generic_obj_uint64_uint64_ret p;
    wm_fill(&p, sizeof p);
    p.handle = handle;
    p.arg = arg;
    wm_call(slot, &p);
    return p.ret;
}

static uint64_t call_obj_ret(unsigned slot, uint64_t handle) {
    struct unixcall_generic_obj_uint64_ret p;
    wm_fill(&p, sizeof p);
    p.handle = handle;
    wm_call(slot, &p);
    return p.ret;
}

/* ---- 0, 1 ---- */

/* 0 retains and 1 releases the object behind the handle, whatever its class (the VM host sends them to Foundation
 * objects too): the probe's own retain count moves by one, in and out. */
static void retain_release(void) {
    id<MTLBuffer> buffer = [wm_device newBufferWithLength:64 options:MTLResourceStorageModeShared];
    id kinds[] = {
        [[[NSObject alloc] init] autorelease],
        [[[WMG1Plain alloc] init] autorelease],
        [[[NSMutableString alloc] initWithFormat:@"a heap string %d", 1] autorelease],
        [[[NSMutableArray alloc] initWithObjects:@"x", nil] autorelease],
        [[[NSData alloc] initWithBytes:"0123456789abcdef0123" length:20] autorelease],
        [buffer autorelease],
    };
    for (unsigned i = 0; i < sizeof kinds / sizeof *kinds; i++) {
        uint64_t h = (uint64_t)kinds[i];
        wm_in(kinds[i]);
        wm_call(0, &h);
        wm_call(1, &h);
    }
    /* The last release frees: a fresh object nobody else holds is gone, so 1 sent a release (not an autorelease). */
    id weak = nil;
    id fresh = [[WMG1Plain alloc] init];
    objc_storeWeak(&weak, fresh);
    uint64_t h = (uint64_t)fresh;
    wm_call(1, &h);
    wm_val("gone", "%s", weak_alive(&weak) ? "no" : "yes");
    objc_storeWeak(&weak, nil);
    uint64_t zero = 0;
    wm_call(0, &zero);
    wm_call(1, &zero);
}

/* ---- 10, 1 ---- */

/* 10 returns an NSAutoreleasePool object (not a push token) that becomes the thread's innermost pool; 1 drains it: an
 * object autoreleased since is gone, one autoreleased before is not. (Isolated: a pool left undrained would break the
 * probe's own.) */
static void pool_drain(void) {
    id weak_outer = nil, weak_inner = nil;
    id outer = [[WMG1Plain alloc] init];
    objc_storeWeak(&weak_outer, outer);
    [outer autorelease];
    struct unixcall_generic_obj_ret p;
    wm_fill(&p, sizeof p);
    wm_call(10, &p);
    wm_h("pool", p.ret);
    wm_val("class", "%s", object_getClassName((id)p.ret));
    id inner = [[WMG1Plain alloc] init];
    objc_storeWeak(&weak_inner, inner);
    [inner autorelease];
    wm_val("inner-before", "%s", weak_alive(&weak_inner) ? "alive" : "gone");
    struct unixcall_generic_obj_noret r = {.handle = p.ret};
    wm_call(1, &r);
    wm_val("gone", "%s", weak_alive(&weak_inner) ? "no" : "yes");
    wm_val("outer-alive", "%s", weak_alive(&weak_outer) ? "yes" : "no");
    objc_storeWeak(&weak_outer, nil);
}

/* Two pools, drained in order: the inner one's objects first. */
static void pool_nested(void) {
    id weak_a = nil, weak_b = nil;
    struct unixcall_generic_obj_ret a, b;
    wm_fill(&a, sizeof a);
    wm_fill(&b, sizeof b);
    wm_call(10, &a);
    wm_h("pool-a", a.ret);
    id oa = [[WMG1Plain alloc] init];
    objc_storeWeak(&weak_a, oa);
    [oa autorelease];
    wm_call(10, &b);
    wm_h("pool-b", b.ret);
    id ob = [[WMG1Plain alloc] init];
    objc_storeWeak(&weak_b, ob);
    [ob autorelease];
    struct unixcall_generic_obj_noret r = {.handle = b.ret};
    wm_call(1, &r);
    wm_val("b-gone", "%s", weak_alive(&weak_b) ? "no" : "yes");
    wm_val("a-alive", "%s", weak_alive(&weak_a) ? "yes" : "no");
    r.handle = a.ret;
    wm_call(1, &r);
    wm_val("a-gone", "%s", weak_alive(&weak_a) ? "no" : "yes");
}

/* ---- 4, 3, 2 ---- */

/* 4 gives a +1 array of the devices; 3 its count; 2 an element, +0 (the device's count doesn't move: in1 would show). */
static void devices(void) {
    struct unixcall_generic_obj_ret p4;
    wm_fill(&p4, sizeof p4);
    wm_own("ret", &p4.ret);
    wm_call(4, &p4);
    if (!p4.ret || p4.ret == WM_FILL64) return;
    NSArray *array = (NSArray *)p4.ret;
    uint64_t count = call_obj_ret(3, p4.ret);
    wm_eq("count", count, [array count], "array-count");
    wm_rel("count-some", count >= 1, ">=1", "%llu", (unsigned long long)count);
    struct unixcall_generic_obj_uint64_obj_ret p2;
    wm_fill(&p2, sizeof p2);
    p2.handle = p4.ret;
    p2.arg = 0;
    wm_in([array objectAtIndex:0]); /* a device has many holders: its count must not move */
    wm_call(2, &p2);
    same("element", p2.ret == (uint64_t)[array objectAtIndex:0], "objectAtIndex", "other");
    same("system-default", p2.ret == (uint64_t)MTLCreateSystemDefaultDevice(), "default-device", "other");
    [array release];
}

/* 2 on a probe-made array of fresh objects: +0 and not autoreleased, whatever the index. */
static void array_object(void) {
    NSArray *array = wm_nsarray(3);
    for (uint64_t i = 0; i < 3; i++) {
        struct unixcall_generic_obj_uint64_obj_ret p;
        wm_fill(&p, sizeof p);
        p.handle = (uint64_t)array;
        p.arg = i;
        wm_own("ret", &p.ret);
        wm_call(2, &p);
        same("element", p.ret == (uint64_t)[array objectAtIndex:i], "objectAtIndex", "other");
    }
    uint64_t c = call_obj_ret(3, (uint64_t)array);
    num("count", c);
    c = call_obj_ret(3, (uint64_t)wm_nsarray(0));
    num("count-empty", c);
}

/* 2 with an index at or past the count raises NSRangeException (a checked get() that answered 0 is wrong). */
static void x_array_past_end(void) {
    NSArray *array = wm_nsarray(3);
    static const uint64_t indexes[] = {3, 4, 5, 0x100000000ull, UINT64_MAX};
    for (unsigned i = 0; i < sizeof indexes / sizeof *indexes; i++) {
        struct unixcall_generic_obj_uint64_obj_ret p;
        wm_fill(&p, sizeof p);
        p.handle = (uint64_t)array;
        p.arg = indexes[i];
        wm_call(2, &p);
        wm_bytes("ret", &p.ret, sizeof p.ret);
    }
    struct unixcall_generic_obj_uint64_obj_ret e;
    wm_fill(&e, sizeof e);
    e.handle = (uint64_t)wm_nsarray(0);
    e.arg = 0;
    wm_call(2, &e);
    wm_bytes("ret", &e.ret, sizeof e.ret);
}

/* A handle of the wrong class: the send raises (unrecognized selector), the status is never reached. */
static void x_wrong_class(void) {
    uint64_t plain = (uint64_t)[[[NSObject alloc] init] autorelease];
    struct unixcall_generic_obj_uint64_ret c;
    wm_fill(&c, sizeof c);
    c.handle = plain;
    wm_call(3, &c);
    wm_bytes("ret", &c.ret, sizeof c.ret);
    struct unixcall_generic_obj_uint64_ret d;
    wm_fill(&d, sizeof d);
    d.handle = plain;
    wm_call(5, &d);
    wm_bytes("ret", &d.ret, sizeof d.ret);
    struct unixcall_generic_obj_uint64_uint64_ret s;
    wm_fill(&s, sizeof s);
    s.handle = plain;
    s.arg = 4;
    wm_call(27, &s);
    wm_bytes("ret", &s.ret, sizeof s.ret);
    struct unixcall_generic_obj_uint64_uint64_ret f;
    wm_fill(&f, sizeof f);
    f.handle = plain;
    f.arg = WMTGPUFamilyApple7;
    wm_call(49, &f);
    wm_bytes("ret", &f.ret, sizeof f.ret);
}

/* ---- 5, 6 ---- */

/* 5 is recommendedMaxWorkingSetSize; 6 is currentAllocatedSize, between two direct reads of it. */
static void device_sizes(void) {
    uint64_t max = call_obj_ret(5, (uint64_t)wm_device);
    wm_eq("recommended", max, wm_device.recommendedMaxWorkingSetSize, "recommendedMaxWorkingSetSize");
    wm_rel("recommended-some", max > 0, "nonzero", "%llu", (unsigned long long)max);
    uint64_t before = wm_device.currentAllocatedSize;
    uint64_t now = call_obj_ret(6, (uint64_t)wm_device);
    uint64_t after = wm_device.currentAllocatedSize;
    uint64_t lo = before < after ? before : after, hi = before < after ? after : before;
    wm_rel("allocated", now >= lo && now <= hi, "between-reads", "%llu", (unsigned long long)now);
}

/* ---- 7, 27, 8 ---- */

/* 7 is the device's name, +0 and (as the C answers) a fresh autoreleased string; 27 and 8 read it back. */
static void name(void) {
    NSString *oracle = wm_device.name;
    struct unixcall_generic_obj_obj_ret p;
    wm_fill(&p, sizeof p);
    p.handle = (uint64_t)wm_device;
    wm_own("ret", &p.ret);
    wm_call(7, &p);
    struct unixcall_generic_obj_obj_ret q;
    wm_fill(&q, sizeof q);
    q.handle = (uint64_t)wm_device;
    wm_own("ret", &q.ret);
    wm_call(7, &q);
    if (!p.ret || p.ret == WM_FILL64) return;
    wm_rel("name", [(NSString *)p.ret isEqualToString:oracle], "device-name", "%s", [(NSString *)p.ret UTF8String]);
    uint64_t len = call_obj_uint64_ret(27, p.ret, WMTUTF8StringEncoding);
    wm_eq("length", len, [oracle lengthOfBytesUsingEncoding:NSUTF8StringEncoding], "lengthOfBytes");
    char buf[256];
    wm_fill(buf, sizeof buf);
    struct unixcall_nsstring_getcstring g = {.str = p.ret, .buffer_ptr = (uint64_t)buf, .max_length = sizeof buf,
                                             .encoding = WMTUTF8StringEncoding, .ret = 0xA5A5A5A5};
    wm_call(8, &g);
    wm_val("ret", "%u", g.ret);
    wm_rel("text", !strcmp(buf, oracle.UTF8String), "device-name", "%s", buf);
}

/* 8: an exact fit (the terminator included), a buffer one short, a larger one, none, and the encodings. The tail is
 * what the C leaves: 0xA5 beyond what getCString:maxLength:encoding: wrote. */
static void getcstring(void) {
    NSString *ascii = [NSString stringWithUTF8String:"ascii string of 25 chars!"];
    NSString *utf8 = [NSString stringWithUTF8String:"h\xc3\xa9llo w\xc3\xb6rld \xe2\x9c\x93 text"];
    struct {
        const char *what;
        NSString *s;
        enum WMTStringEncoding enc;
        int delta; /* the buffer holds the string's bytes + 1 + delta */
    } runs[] = {
        {"ascii-exact", ascii, WMTUTF8StringEncoding, 0},
        {"ascii-short-1", ascii, WMTUTF8StringEncoding, -1},
        {"ascii-short-5", ascii, WMTUTF8StringEncoding, -5},
        {"ascii-roomy", ascii, WMTUTF8StringEncoding, 9},
        {"ascii-zero", ascii, WMTUTF8StringEncoding, -100},
        {"utf8-exact", utf8, WMTUTF8StringEncoding, 0},
        {"utf8-short-1", utf8, WMTUTF8StringEncoding, -1},
        {"utf8-as-ascii", utf8, WMTASCIIStringEncoding, 9},
        {"ascii-as-ascii", ascii, WMTASCIIStringEncoding, 0},
        {"utf8-as-latin1", utf8, WMTWMTISOLatin1StringEncoding, 9},
    };
    for (unsigned i = 0; i < sizeof runs / sizeof *runs; i++) {
        NSUInteger bytes = [runs[i].s lengthOfBytesUsingEncoding:runs[i].enc == WMTASCIIStringEncoding ? NSUTF8StringEncoding : runs[i].enc];
        long max = (long)bytes + 1 + runs[i].delta;
        if (max < 0 || runs[i].delta == -100) max = 0;
        char buf[128];
        wm_fill(buf, sizeof buf);
        struct unixcall_nsstring_getcstring g = {.str = (uint64_t)runs[i].s, .buffer_ptr = (uint64_t)buf,
                                                 .max_length = (uint64_t)max, .encoding = runs[i].enc,
                                                 .ret = 0xA5A5A5A5};
        wm_line("run=%s max=%ld", runs[i].what, max);
        wm_call(8, &g);
        wm_val("ret", "%u", g.ret);
        wm_bytes("written", buf, sizeof buf);
        wm_val("text", "%.*s", (int)(sizeof buf), buf[0] == (char)WM_FILL ? "(untouched)" : buf);
    }
}

/* ---- 28 ---- */

/* 28 is the object's description, +0 (a string the object made); read back through 8 against description itself. */
static void description(void) {
    NSError *error = [NSError errorWithDomain:@"WMProbe" code:7 userInfo:nil];
    id objects[] = {[[[WMG1Plain alloc] init] autorelease], error, [[[NSObject alloc] init] autorelease],
                    [NSNumber numberWithInt:42]};
    for (unsigned i = 0; i < sizeof objects / sizeof *objects; i++) {
        struct unixcall_generic_obj_obj_ret p;
        wm_fill(&p, sizeof p);
        p.handle = (uint64_t)objects[i];
        wm_own("ret", &p.ret);
        wm_call(28, &p);
        if (!p.ret || p.ret == WM_FILL64) continue;
        char buf[256];
        wm_fill(buf, sizeof buf);
        struct unixcall_nsstring_getcstring g = {.str = p.ret, .buffer_ptr = (uint64_t)buf, .max_length = sizeof buf,
                                                 .encoding = WMTUTF8StringEncoding, .ret = 0xA5A5A5A5};
        wm_call(8, &g);
        same("text", !strcmp(buf, [[objects[i] description] UTF8String]), "description", buf);
    }
}

/* ---- 60, 61 ---- */

/* 60 is stringWithCString:encoding: (+0, autoreleased), 61 initWithCString:encoding: (+1). Strings of 16 bytes or more
 * (a shorter one may be a tagged pointer), ASCII and UTF-8 with non-ASCII characters; read back through 27 and 8. */
static void strings(void) {
    static const struct {
        const char *what;
        const char *bytes;
        enum WMTStringEncoding enc;
    } inputs[] = {
        {"ascii", "an ascii string of some length", WMTUTF8StringEncoding},
        {"ascii-enc", "an ascii string of some length", WMTASCIIStringEncoding},
        {"utf8", "h\xc3\xa9llo w\xc3\xb6rld \xe2\x9c\x93 na\xc3\xafve text", WMTUTF8StringEncoding},
        {"bad-utf8", "bytes \xff\xfe that are not utf-8 at all", WMTUTF8StringEncoding},
        {"non-ascii-as-ascii", "h\xc3\xa9llo w\xc3\xb6rld in the ascii encoding", WMTASCIIStringEncoding},
        {"empty", "", WMTUTF8StringEncoding},
    };
    for (unsigned slot = 60; slot <= 61; slot++)
        for (unsigned i = 0; i < sizeof inputs / sizeof *inputs; i++) {
            struct unixcall_nsstring_string p;
            wm_fill(&p, sizeof p);
            p.buffer_ptr.ptr = inputs[i].bytes;
            p.encoding = inputs[i].enc;
            wm_line("input=%s", inputs[i].what);
            wm_own("ret", &p.ret);
            wm_call(slot, &p);
            if (!p.ret || p.ret == WM_FILL64) continue;
            NSString *oracle = [[[NSString alloc] initWithCString:inputs[i].bytes encoding:(NSStringEncoding)inputs[i].enc] autorelease];
            uint64_t len = call_obj_uint64_ret(27, p.ret, WMTUTF8StringEncoding);
            wm_eq("length", len, [oracle lengthOfBytesUsingEncoding:NSUTF8StringEncoding], "lengthOfBytes");
            char buf[256];
            wm_fill(buf, sizeof buf);
            struct unixcall_nsstring_getcstring g = {.str = p.ret, .buffer_ptr = (uint64_t)buf, .max_length = sizeof buf,
                                                     .encoding = WMTUTF8StringEncoding, .ret = 0xA5A5A5A5};
            wm_call(8, &g);
            wm_rel("text", !strcmp(buf, oracle.UTF8String), "input", "%s", buf);
            if (slot == 61) [(NSString *)p.ret release];
        }
}

/* 60 with no C string raises (stringWithCString:NULL). (61 with NULL crashes the C: not a case.) */
static void strings_null(void) {
    for (unsigned slot = 60; slot <= 60; slot++) {
        struct unixcall_nsstring_string p;
        wm_fill(&p, sizeof p);
        p.buffer_ptr.ptr = NULL;
        p.encoding = WMTUTF8StringEncoding;
        wm_call(slot, &p);
        wm_bytes("ret", &p.ret, sizeof p.ret);
    }
}

/* ---- 24 ---- */

/* 24 for three formats, with the custom swizzle bits (stripped) and with bits above 32 (the C passes the argument on as
 * a 32-bit format, so they are dropped): the alignment of the plain format. */
static void min_align(void) {
    static const uint32_t formats[] = {WMTPixelFormatR8Unorm, WMTPixelFormatBGRA8Unorm, WMTPixelFormatRGBA16Float,
                                       WMTPixelFormatRGBA32Float};
    static const struct {
        const char *what;
        uint64_t extra;
    } mods[] = {
        {"plain", 0},
        {"alpha-is-one", WMTPixelFormatAlphaIsOne},
        {"r001", WMTPixelFormatR001Swizzle},
        {"gbar", WMTPixelFormatGBARSwizzle},
        {"all-bits", WMTPixelFormatCustomSwizzle},
        {"high-bits", 1ull << 32},
        {"high-and-swizzle", (1ull << 40) | WMTPixelFormatAlphaIsOne},
    };
    for (unsigned f = 0; f < sizeof formats / sizeof *formats; f++)
        for (unsigned m = 0; m < sizeof mods / sizeof *mods; m++) {
            wm_line("format=%u mod=%s", formats[f], mods[m].what);
            uint64_t arg = formats[f] | mods[m].extra;
            uint64_t ret = call_obj_uint64_ret(24, (uint64_t)wm_device, arg);
            wm_eq("ret", ret, [wm_device minimumLinearTextureAlignmentForPixelFormat:(MTLPixelFormat)formats[f]],
                  "minimumLinearTextureAlignment");
        }
}

/* ---- 49-52 ---- */

static void supports(void) {
    static const uint64_t families[] = {WMTGPUFamilyApple1, WMTGPUFamilyApple2, WMTGPUFamilyApple3, WMTGPUFamilyApple4,
                                        WMTGPUFamilyApple5, WMTGPUFamilyApple6, WMTGPUFamilyApple7, WMTGPUFamilyApple8,
                                        WMTGPUFamilyApple9, WMTGPUFamilyMac1, WMTGPUFamilyMac2, WMTGPUFamilyCommon1,
                                        WMTGPUFamilyCommon2, WMTGPUFamilyCommon3, WMTGPUFamilyMacCatalyst1,
                                        WMTGPUFamilyMacCatalyst2, WMTGPUFamilyMetal3, 1010, 5002, 9999, 0};
    for (unsigned i = 0; i < sizeof families / sizeof *families; i++) {
        wm_line("family=%llu", (unsigned long long)families[i]);
        bool oracle = [wm_device supportsFamily:(MTLGPUFamily)families[i]];
        uint64_t ret = call_obj_uint64_ret(49, (uint64_t)wm_device, families[i]);
        wm_eq("ret", ret, oracle, oracle ? "supported" : "unsupported");
    }
    uint64_t ret = call_obj_ret(50, (uint64_t)wm_device);
    wm_eq("bc", ret, wm_device.supportsBCTextureCompression, "supportsBCTextureCompression");
    static const uint64_t counts[] = {1, 2, 3, 4, 8, 0, 16, 0x100000004ull};
    for (unsigned i = 0; i < sizeof counts / sizeof *counts; i++) {
        wm_line("sample-count=%llu", (unsigned long long)counts[i]);
        ret = call_obj_uint64_ret(51, (uint64_t)wm_device, counts[i]);
        wm_eq("ret", ret, [wm_device supportsTextureSampleCount:counts[i]], "supportsTextureSampleCount");
    }
    ret = call_obj_ret(52, (uint64_t)wm_device);
    wm_eq("unified", ret, wm_device.hasUnifiedMemory, "hasUnifiedMemory");
}

/* ---- 86 ---- */

/* 86 sets the label of any encoder-or-resource handle (the handler sends setLabel: to it): it reads back. */
static void label(void) {
    id<MTLBuffer> buffer = [[wm_device newBufferWithLength:64 options:MTLResourceStorageModeShared] autorelease];
    id<MTLCommandQueue> queue = [[wm_device newCommandQueue] autorelease];
    id objects[] = {buffer, queue};
    NSString *labels[] = {@"a buffer label with spaces", @"héllo ✓", @""};
    for (unsigned o = 0; o < 2; o++)
        for (unsigned i = 0; i < 3; i++) {
            struct unixcall_generic_obj_obj_noret p = {.handle = (uint64_t)objects[o],
                                                       .arg = (uint64_t)[NSString stringWithString:labels[i]]};
            wm_line("object=%s", o ? "queue" : "buffer");
            wm_call(86, &p);
            NSString *got = [objects[o] label];
            wm_val("label", "%s", got ? got.UTF8String : "nil");
        }
}

/* 86 with a 0 label sends nil on (the label goes away), and with a 0 handle sends nothing. */
static void nil_args(void) {
    id<MTLBuffer> buffer = [[wm_device newBufferWithLength:64 options:MTLResourceStorageModeShared] autorelease];
    buffer.label = @"set before";
    wm_val("before", "%s", buffer.label.UTF8String);
    struct unixcall_generic_obj_obj_noret p = {.handle = (uint64_t)buffer, .arg = 0};
    wm_call(86, &p);
    wm_val("label", "%s", buffer.label ? buffer.label.UTF8String : "nil");
    struct unixcall_generic_obj_obj_noret n = {.handle = 0, .arg = (uint64_t)@"a label for nothing"};
    wm_call(86, &n);
    struct unixcall_generic_obj_obj_noret nn = {.handle = 0, .arg = 0};
    wm_call(86, &nn);
}

/* ---- 87 ---- */

/* 87 stores (BOOL)arg on the device: any nonzero 64-bit value is YES. */
static void max_concurrent(void) {
    BOOL was = wm_device.shouldMaximizeConcurrentCompilation;
    static const uint64_t values[] = {1, 0, 2, 256, 0x100000000ull, 1, 0};
    for (unsigned i = 0; i < sizeof values / sizeof *values; i++) {
        struct unixcall_generic_obj_uint64_noret p = {.handle = (uint64_t)wm_device, .arg = values[i]};
        wm_line("arg=%llu", (unsigned long long)values[i]);
        wm_call(87, &p);
        wm_val("maximize", "%d", wm_device.shouldMaximizeConcurrentCompilation);
    }
    wm_device.shouldMaximizeConcurrentCompilation = was;
}

/* ---- 111 ---- */

static void os_version(void) {
    NSOperatingSystemVersion v = [NSProcessInfo processInfo].operatingSystemVersion;
    struct unixcall_get_os_version p;
    wm_fill(&p, sizeof p);
    wm_call(111, &p);
    wm_rel("major", p.ret_major >= 27, ">=27", "%llu", (unsigned long long)p.ret_major);
    wm_eq("major-is", p.ret_major, (uint64_t)v.majorVersion, "processinfo");
    wm_eq("minor", p.ret_minor, (uint64_t)v.minorVersion, "processinfo");
    wm_eq("patch", p.ret_patch, (uint64_t)v.patchVersion, "processinfo");
}

/* ---- 114, 149 ---- */

static const uint8_t *pattern(size_t n, uint8_t *buf) {
    for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)(i * 7 + 1);
    return buf;
}

/* 114 copies the bytes (the source can change after) into a +1 dispatch data; 149 copies out. */
static void dispatch_data(void) {
    uint8_t src[40], want[40];
    pattern(40, src);
    memcpy(want, src, 40);
    struct unixcall_generic_obj_uint64_obj_ret p;
    wm_fill(&p, sizeof p);
    p.handle = (uint64_t)src;
    p.arg = 40;
    wm_own("ret", &p.ret);
    wm_call(114, &p);
    if (!p.ret || p.ret == WM_FILL64) return;
    memset(src, 0xEE, sizeof src);
    static const uint64_t caps[] = {40, 64, 16, 1, 0};
    for (unsigned i = 0; i < sizeof caps / sizeof *caps; i++) {
        uint8_t out[64];
        wm_fill(out, sizeof out);
        struct unixcall_dispatchdata_copybytes c;
        wm_fill(&c, sizeof c);
        c.data = p.ret;
        c.buffer.ptr = out;
        c.capacity = caps[i];
        wm_line("capacity=%llu", (unsigned long long)caps[i]);
        wm_call(149, &c);
        num("ret_size", c.ret_size);
        wm_bytes("written", out, sizeof out);
        size_t n = caps[i] < 40 ? caps[i] : 40;
        wm_val("copied", "%s", !memcmp(out, want, n) ? "original" : "other");
    }
    [(id)p.ret release];
}

/* 114 with no bytes: an empty data (a singleton), a null pointer included. */
static void dd_empty(void) {
    for (unsigned i = 0; i < 2; i++) {
        uint8_t src[4] = {1, 2, 3, 4};
        struct unixcall_generic_obj_uint64_obj_ret p;
        wm_fill(&p, sizeof p);
        p.handle = i ? 0 : (uint64_t)src;
        p.arg = 0;
        wm_own("ret", &p.ret);
        wm_call(114, &p);
        if (!p.ret || p.ret == WM_FILL64) continue;
        uint8_t out[8];
        wm_fill(out, sizeof out);
        struct unixcall_dispatchdata_copybytes c;
        wm_fill(&c, sizeof c);
        c.data = p.ret;
        c.buffer.ptr = out;
        c.capacity = sizeof out;
        wm_call(149, &c);
        num("ret_size", c.ret_size);
        wm_bytes("written", out, sizeof out);
    }
}

/* 149 on data of two regions: it copies region by region, stops at the first one that starts at or past the capacity
 * (offset >= capacity) and cuts the one that crosses it (MIN). */
static void dispatch_data_regions(void) {
    uint8_t a[16], b[24], want[40];
    pattern(16, a);
    for (unsigned i = 0; i < 24; i++) b[i] = (uint8_t)(0x80 + i);
    memcpy(want, a, 16);
    memcpy(want + 16, b, 24);
    dispatch_data_t da = dispatch_data_create(a, 16, NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    dispatch_data_t db = dispatch_data_create(b, 24, NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    dispatch_data_t both = dispatch_data_create_concat(da, db);
    __block unsigned regions = 0;
    dispatch_data_apply(both, ^bool(dispatch_data_t r, size_t o, const void *bytes, size_t l) {
      regions++;
      return true;
    });
    wm_val("regions", "%u", regions);
    static const uint64_t caps[] = {40, 41, 39, 16, 17, 15, 20, 8, 1, 0};
    for (unsigned i = 0; i < sizeof caps / sizeof *caps; i++) {
        uint8_t out[64];
        wm_fill(out, sizeof out);
        struct unixcall_dispatchdata_copybytes c;
        wm_fill(&c, sizeof c);
        c.data = (uint64_t)both;
        c.buffer.ptr = out;
        c.capacity = caps[i];
        wm_line("capacity=%llu", (unsigned long long)caps[i]);
        wm_call(149, &c);
        num("ret_size", c.ret_size);
        wm_bytes("written", out, sizeof out);
        size_t n = caps[i] < 40 ? caps[i] : 40;
        wm_val("copied", "%s", !memcmp(out, want, n) ? "original" : "other");
    }
    [(id)both release];
    [(id)da release];
    [(id)db release];
}

/* ---- 125, 148 ---- */

static void registry(void) {
    uint64_t ret = call_obj_ret(125, (uint64_t)wm_device);
    wm_eq("ret", ret, wm_device.registryID, "registryID");
    wm_rel("nonzero", ret != 0, "yes", "%llu", (unsigned long long)ret);
}

static void timestamps(void) {
    struct unixcall_mtldevice_sampletimestamps a, b;
    wm_fill(&a, sizeof a);
    wm_fill(&b, sizeof b);
    a.device = b.device = (uint64_t)wm_device;
    wm_call(148, &a);
    same("cpu", a.cpu != 0 && a.cpu != WM_FILL64, "nonzero", "bad");
    same("gpu", a.gpu != 0 && a.gpu != WM_FILL64, "nonzero", "bad");
    wm_call(148, &b);
    same("cpu2", b.cpu >= a.cpu && b.cpu != WM_FILL64, ">=first", "bad");
    same("gpu2", b.gpu >= a.gpu && b.gpu != WM_FILL64, ">=first", "bad");
    wm_val("device", "%s", a.device == (uint64_t)wm_device ? "kept" : "changed");
}

/* ---- handle 0 ---- */

/* Every getter of the group on handle 0: a nil send, so the C writes the zero it answers (and whatever else it writes),
 * and the setters do nothing. (149 and 114 call C functions, not sends: they have no nil case.) */
static void nil_handle(void) {
    uint64_t zero = 0;
    wm_call(0, &zero);
    wm_call(1, &zero);
    struct unixcall_generic_obj_uint64_obj_ret p2;
    wm_fill(&p2, sizeof p2);
    p2.handle = 0;
    p2.arg = 0;
    wm_call(2, &p2);
    num("2.ret", p2.ret);
    static const unsigned plain[] = {3, 5, 6, 50, 52, 125};
    for (unsigned i = 0; i < sizeof plain / sizeof *plain; i++) {
        uint64_t ret = call_obj_ret(plain[i], 0);
        char key[16];
        snprintf(key, sizeof key, "%u.ret", plain[i]);
        num(key, ret);
    }
    static const unsigned with_arg[] = {24, 27, 49, 51};
    for (unsigned i = 0; i < sizeof with_arg / sizeof *with_arg; i++) {
        uint64_t ret = call_obj_uint64_ret(with_arg[i], 0, 4);
        char key[16];
        snprintf(key, sizeof key, "%u.ret", with_arg[i]);
        num(key, ret);
    }
    static const unsigned objs[] = {7, 28};
    for (unsigned i = 0; i < sizeof objs / sizeof *objs; i++) {
        struct unixcall_generic_obj_obj_ret p;
        wm_fill(&p, sizeof p);
        p.handle = 0;
        wm_call(objs[i], &p);
        char key[16];
        snprintf(key, sizeof key, "%u.ret", objs[i]);
        num(key, p.ret);
    }
    char buf[16];
    wm_fill(buf, sizeof buf);
    struct unixcall_nsstring_getcstring g = {.str = 0, .buffer_ptr = (uint64_t)buf, .max_length = sizeof buf,
                                             .encoding = WMTUTF8StringEncoding, .ret = 0xA5A5A5A5};
    wm_call(8, &g);
    wm_val("8.ret", "%u", g.ret);
    wm_bytes("8.buffer", buf, sizeof buf);
    struct unixcall_mtldevice_sampletimestamps t;
    wm_fill(&t, sizeof t);
    t.device = 0;
    wm_call(148, &t);
    num("148.cpu", t.cpu);
    num("148.gpu", t.gpu);
    struct unixcall_generic_obj_uint64_noret s = {.handle = 0, .arg = 1};
    wm_call(87, &s);
}

/* ---- leaks ---- */

/* Everything a handler in this group allocates and the caller gives back: no growth cycle after cycle. */
static void heap_growth(void) {
    wm_heap_growth(^{
      struct unixcall_generic_obj_ret p4;
      wm_fill(&p4, sizeof p4);
      wm_call(4, &p4);
      if (!p4.ret || p4.ret == WM_FILL64) return;
      uint64_t c = call_obj_ret(3, p4.ret);
      struct unixcall_generic_obj_uint64_obj_ret p2 = {.handle = p4.ret, .arg = c ? c - 1 : 0, .ret = WM_FILL64};
      wm_call(2, &p2);
      [(id)p4.ret release];
      struct unixcall_generic_obj_ret pool;
      wm_fill(&pool, sizeof pool);
      wm_call(10, &pool);
      if (!pool.ret || pool.ret == WM_FILL64) return;
      struct unixcall_nsstring_string s;
      wm_fill(&s, sizeof s);
      s.buffer_ptr.ptr = "a string of more than sixteen bytes";
      s.encoding = WMTUTF8StringEncoding;
      wm_call(60, &s);
      wm_call(61, &s);
      if (s.ret && s.ret != WM_FILL64) [(id)s.ret release];
      struct unixcall_generic_obj_obj_ret n;
      wm_fill(&n, sizeof n);
      n.handle = (uint64_t)wm_device;
      wm_call(7, &n);
      wm_call(28, &n);
      char buf[64];
      struct unixcall_nsstring_getcstring g = {.str = (uint64_t)[NSString stringWithFormat:@"a string of %d", 17], .buffer_ptr = (uint64_t)buf, .max_length = sizeof buf,
                                               .encoding = WMTUTF8StringEncoding};
      wm_call(8, &g);
      uint8_t src[32] = {0};
      struct unixcall_generic_obj_uint64_obj_ret d = {.handle = (uint64_t)src, .arg = 32, .ret = WM_FILL64};
      wm_call(114, &d);
      if (!d.ret || d.ret == WM_FILL64) return;
      uint8_t out[32];
      struct unixcall_dispatchdata_copybytes cb = {.data = d.ret, .buffer.ptr = out, .capacity = sizeof out};
      wm_call(149, &cb);
      [(id)d.ret release];
      struct unixcall_generic_obj_noret r = {.handle = pool.ret};
      wm_call(1, &r);
    }, 400);
}

const struct wm_case wm_cases_g1[] = {
    {"g1", 0, "retain-release", 0, retain_release},
    {"g1", 10, "pool-drain", WM_ISOLATE, pool_drain},
    {"g1", 10, "pool-nested", WM_ISOLATE, pool_nested},
    {"g1", 4, "devices", 0, devices},
    {"g1", 2, "array-object", 0, array_object},
    {"g1", 2, "x-array-past-end", 0, x_array_past_end},
    {"g1", 3, "x-wrong-class", 0, x_wrong_class},
    {"g1", 5, "device-sizes", 0, device_sizes},
    {"g1", 7, "name", 0, name},
    {"g1", 8, "getcstring", 0, getcstring},
    {"g1", 28, "description", 0, description},
    {"g1", 60, "strings", 0, strings},
    {"g1", 60, "strings-null", 0, strings_null},
    {"g1", 24, "min-align", 0, min_align},
    {"g1", 49, "supports", 0, supports},
    {"g1", 86, "label", 0, label},
    {"g1", 87, "max-concurrent", 0, max_concurrent},
    {"g1", 111, "os-version", 0, os_version},
    {"g1", 114, "dispatch-data", 0, dispatch_data},
    {"g1", 114, "dispatch-data-empty", 0, dd_empty},
    {"g1", 149, "dispatch-data-regions", 0, dispatch_data_regions},
    {"g1", 125, "registry", 0, registry},
    {"g1", 148, "timestamps", 0, timestamps},
    {"g1", 2, "nil", 0, nil_handle},
    {"g1", 86, "nil-args", 0, nil_args},
    {"g1", 4, "heap-growth", 0, heap_growth},
    {0},
};
