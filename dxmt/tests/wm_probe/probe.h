/* wm_probe (DXMTRS step 2): winemetal's 158 unix-call slots driven natively, without Wine or the VM. The probe loads
 * one winemetal.so beside the VM host's stand-ins (or the native winemetal.dylib), copies its table, and runs case
 * tables (cases_g0.m .. cases_g10.m, one per slot group of slots.txt) on a worker thread while main runs its run loop.
 * Each call prints one line, "<group> <slot> <case> <status-hex>|raised <name> [<key>=<value>...]", with the fields
 * the case adds after it; recorders, dumps and the runner print lines of their own, all "<group> <slot> <case> ...",
 * where <slot> is the slot of the call in progress or the last one ("-" before any). `fixed` mode prints only what
 * is machine-independent (relations where a value isn't); `full` adds raw values and runs with NSZombieEnabled=YES.
 *
 * A case is a function of no arguments. It makes its inputs from Metal directly (wm_device, wm_queue, or its own
 * objects: never another group's slot), fills its params with wm_fill (0xA5), calls wm_call, and prints what the
 * handler did with the helpers below. Every case prints at least one line: wm-probe-check.sh compares each group's
 * case set with its golden's. A case that needs a handle (wm_h) and gets 0 or the fill pattern stops with
 * "bad-handle"; anything the case itself raises outside wm_call stops it with "uncaught <name>". */
#ifndef WM_PROBE_H
#define WM_PROBE_H
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <mach/mach.h>
#include <stdbool.h>
#include <stdint.h>
#include "winemetal_thunks.h"

/* AppKit and QuartzCore are left to the cases that use them: AppKit brings Carbon's names (crash, ...). */
@class NSWindow, NSView, CAMetalLayer;

/* Case flags. */
enum {
    WM_ISOLATE = 1 << 0,     /* runs in a child (the probe re-spawned with --case), 10 s limit: "hang" */
    WM_ENV_ANISO = 1 << 1,   /* a child per DXMT_MAX_ANISOTROPY value of wm_env_aniso: wm_env() is the value */
    WM_ENV_PRESENT = 1 << 2, /* two children with MACNEUTRON_PRESENT=1: wm_env() "absent" (the image as it is) and
                                "present" (a copy of its folder with the presenter and its metallib beside it: the
                                main checkout's build/wine-arm64-src/presenter, or WM_PRESENTER) */
    WM_FULL_ONLY = 1 << 3,   /* runs in full mode only */
    WM_NATIVE = 1 << 4,      /* runs in native mode only */
    WM_WINE_ONLY = 1 << 5,   /* needs a Wine stand-in (NtSetEvent's hook, winemac): native mode skips it */
    WM_GMALLOC = 1 << 6,     /* an isolate child under DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib */
    WM_NOT_ON_C = 1 << 7,    /* kept out of the C goldens: compared once the golden holds a line of it */
};

struct wm_case {
    const char *group; /* "g0" .. "g10" */
    unsigned slot;     /* the slot the case is about (--list) */
    const char *name;
    unsigned flags;
    void (*run)(void);
};

/* Each cases_gN.m: a table ending with a zero entry. */
extern const struct wm_case wm_cases_g0[], wm_cases_g1[], wm_cases_g2[], wm_cases_g3[], wm_cases_g4[], wm_cases_g5[],
    wm_cases_g6[], wm_cases_g7[], wm_cases_g8[], wm_cases_g9[], wm_cases_g10[];

#define WM_FILL 0xA5
#define WM_FILL64 0xA5A5A5A5A5A5A5A5ull

/* The run. */
extern id<MTLDevice> wm_device;      /* MTLCreateSystemDefaultDevice(), the probe's own (+1, never released) */
extern id<MTLCommandQueue> wm_queue; /* the probe's own queue on it */
bool wm_full(void);                  /* full mode */
bool wm_native(void);                /* native mode (the dylib), else the Wine .so beside the stand-ins */
const char *wm_env(void);            /* in an env case's child: its variant (see the flags); else NULL */
const char *wm_child_stderr(void);   /* in an env case's child: its stderr so far (the constructors' lines) */
extern const char *const wm_env_aniso[]; /* WM_ENV_ANISO's values, NULL-terminated: "8", "0", "17", "8x" */
void *wm_sym(const char *name);      /* dlsym in the loaded winemetal image (native: WMTSet* hooks), or NULL */
void wm_event_hook(void (*hook)(uint64_t nt_event)); /* the ntdll stand-in's NtSetEvent calls it (Wine mode) */
const char *wm_thread(void);         /* "main", "worker" (the cases' thread) or "other" */

/* Calls. wm_call runs one slot through the probe-held table (WM_STUB=<slot>[,<slot>...] puts a stub that writes
 * nothing and returns 0 there) inside a probe autorelease pool (not for 0, 1 and 10: slot 10's pool stays the
 * thread's innermost until slot 1 releases it), counts it (--counts), prints "cross <slot>" for a slot outside the
 * case's group, and starts the call's line: the status, or "raised <name>" when the handler raised, or "null" for an
 * empty table entry (not called). Fields then go on that line until the next line.
 * Threads: each thread has its own line, slot and wm_own registrations, so a call on a thread the case starts (109)
 * prints its own whole line while the worker goes on; the thread's last line prints when it exits, or at the case's
 * end (a dispatch thread). A case joins such a thread before its next call (after 110), and waits for a hook's line
 * before its own next line, so the lines' order is fixed. A thread with no call of its own in the case (main in
 * wm_main_blocked, a completion handler) names the last slot any thread called, and each of its fields prints as a
 * line at once. wm_on_main lends the caller's line to main for the block. wm_in, and wm_h's stop, are the worker's
 * (and main's under wm_on_main): a bad handle elsewhere aborts. own= retains under the probe's lock: never on slot
 * 10's pool (NSAutoreleasePool raises on retain, and every other thread would then wait on the lock). */
unsigned wm_call(unsigned slot, void *params);
/* Before wm_call: the handle-out field at `field` gets "<name>=h<n> own=<delta> pool=+1|+0" after the call. own: for
 * an object first seen in the case, its count once the pool popped minus the probe's own retain (+1: the caller owns
 * it); for one seen before (a getter's), the growth since its last call. pool: its count before the pop minus after
 * (+1: autoreleased). "n/a" for a tagged pointer or a count >= 2^31 (a singleton); "<name>=0" for nil. The probe
 * keeps one retain on each object until the case ends, so +0 results stay usable. Up to 8 per call. */
void wm_own(const char *name, const obj_handle_t *field);
/* A probe-made input: every later call of the case that changes its retain count (after the call's pool popped)
 * prints "in<k>=<delta>". Returns k, from 1 in each case. */
int wm_in(id obj);

/* Fields of the current line. A value holding a space or a quote prints quoted ("...", \" and \\ escaped). */
void wm_h(const char *name, uint64_t handle);  /* name=h<n>; 0 or the fill pattern: "bad-handle", the case stops */
void wm_h0(const char *name, uint64_t handle); /* the same with 0 allowed: name=0 */
const char *wm_hs(uint64_t handle);            /* "h<n>" (numbered by first appearance in the case), "0" */
void wm_val(const char *name, const char *fmt, ...) __attribute__((format(printf, 2, 3))); /* both modes */
void wm_raw(const char *name, const char *fmt, ...) __attribute__((format(printf, 2, 3))); /* full mode only */
/* fixed: name=<relation> when it holds, else name=<value>; full: name=<value>. */
void wm_rel(const char *name, bool holds, const char *relation, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
void wm_eq(const char *name, uint64_t value, uint64_t oracle, const char *oracle_name); /* wm_rel on value == oracle */
void wm_bytes(const char *name, const void *p, size_t size); /* name=<ranges of bytes no longer 0xA5>|none */
void wm_sha(const char *name, const void *p, size_t size);   /* name=sha256:<hex>/<size>, as the recorder's NSData */
void wm_send_refs(mach_port_t port);                         /* send-refs=<n>: the send right's user references */
void wm_fill(void *p, size_t size);                          /* 0xA5 */
uint64_t wm_word(uint64_t handle);                           /* the first 8 bytes behind a handle */

/* Lines. */
void wm_line(const char *fmt, ...) __attribute__((format(printf, 1, 2))); /* "<group> <slot> <case> <text>" */
/* "dump <key>=<value>" lines: every readable property along the class chain (class_copyPropertyList, valueForKey:
 * in @try, "<key>=!" on a raise), sorted; MTL*-class values recursed (depth 3, cycle guard), indexed arrays by
 * objectAtIndexedSubscript: 0-7 (0-30 in a vertex descriptor); the private keys logicOperationEnabled,
 * logicOperation, compressionMode and wm_dump_key's, where they answer. Values print as the recorder's do. */
void wm_dump(id descriptor);
void wm_dump_as(id descriptor, const char *prefix); /* the same, each key prefixed: "dump <prefix><key>=..." */
void wm_dump_key(const char *key); /* one more private key, for the rest of the case */
/* A recorder (NSProxy): "msg@main|worker|other <selector> [<piece>=<argument>...]" for every message, forwarded to
 * target when it has one; without one, wm_rec_ret's canned value or zero. Signatures: the protocol's, else the
 * target's class's (class_getInstanceMethod); a Class as target gives a target-less recorder with that class's
 * signatures (CAMetalLayer, MTLCaptureManager, the HUD's class). Arguments print as integers, %a doubles, struct bytes
 * in hex, pointers dereferenced by the selector's count:/length:/withRange: argument, objects by kind (NSString
 * quoted, NSURL by absoluteString, NSNumber by value, NSData by sha256, arrays and dictionaries recursively, MTL*
 * descriptors and options (a class name holding "Descriptor" or "Options") as their class with dump lines
 * "<piece>.<key>=..." after, anything else by wm_hs). Autoreleased (the
 * case's pool). */
id wm_rec(id target, Protocol *protocol);
void wm_rec_ret(id recorder, SEL selector, id value); /* the canned return of one selector (an object) */
/* The MetalFX descriptors' new*WithDevice: dump their descriptor ("swz@<thread> <selector>", "dump desc.<key>=...")
 * and -[MTLFunctionConstantValues setConstantValue:type:atIndex:] prints its value's bytes by type, the type and the
 * index, then each calls through. Off again when the case ends. */
void wm_swizzles(bool on);

/* Fixtures and Metal. */
const char *wm_fixture_path(const char *name); /* WM_PROBE_DIR (default this folder)/<name>; "../shaders/..." too */
NSData *wm_fixture(const char *name);          /* its bytes; nil when missing */
const char *wm_tmpdir(void);                   /* a mkdtemp folder under $TMPDIR, the process's (children share it),
                                                  removed at exit */
id<MTLLibrary> wm_library(NSString *msl);      /* MSL compiled at run time (newLibraryWithSource:) */
NSData *wm_readback(id resource);              /* a buffer's bytes, or a texture's level 0 slice 0, through a blit
                                                  into a Shared buffer; nil for a pixel format of unknown size */
NSArray *wm_nsarray(unsigned n);               /* n fresh NSObjects (autoreleased, the case's pool) */
struct wm_window {
    NSWindow *window;
    NSView *view;        /* layer-backed: the layer's delegate is the view */
    CAMetalLayer *layer;
};
/* Made on main once, on the main screen, 320x180 points. Slots 72/73 go through the winemac stand-in, which makes
 * its own window ("MacNeutron VM") and takes none. */
struct wm_window wm_window(void);
void wm_on_main(void (^block)(void));           /* dispatch_sync to main (the probe's main runs its run loop) */
/* block runs on the worker while main waits on a semaphore: true and "hang" when the block doesn't finish within
 * 5 s (main then goes on, so a block waiting on main finishes after). */
bool wm_main_blocked(void (^block)(void));
/* Runs block once (printed), then `cycles` times with output off, between malloc_zone_statistics(NULL)'s
 * size_in_use: "heap-growth=<bytes per cycle>" in full, "freed" or "leaked" (more than 64 bytes a cycle) in fixed,
 * on a line naming the block's last slot. */
void wm_heap_growth(void (^block)(void), unsigned cycles);

#endif
