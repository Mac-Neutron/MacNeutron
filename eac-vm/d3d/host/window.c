/* The guest's windows and displays (M3 Task 2): what each context last pushed (ops 7 and 8), and the hooks native
 * DXMT's window system answers from (fork 0082's WMTSetWsiHooks). A hook in a call answers its context's; off a call
 * (DXMT's own threads), a window's latest in any context and the last context's displays. */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "../mn_d3d.h"
#include "dispatch.h"

uint32_t mn_d3d_current_ctx(void); /* dispatch.c: the context whose call runs on this thread, or 0 */

/* As fork 0082's winemetal.h has them. */
struct WMTWsiWindow { uint64_t monitor; uint32_t width, height, flags; };
struct WMTWsiDisplay { uint64_t monitor; int32_t left, top, right, bottom; uint16_t name[32]; };
struct WMTWsiMode { uint32_t width, height, refresh_num, refresh_den; };
struct WMTWsiHooks {
    int (*window)(uint64_t hwnd, struct WMTWsiWindow *out);
    int (*display)(uint32_t index, struct WMTWsiDisplay *out);
    int (*mode)(uint64_t monitor, uint32_t index, struct WMTWsiMode *out);
    void (*action)(const struct mn_d3d_action *action); /* fork 0083's WMTWsiAction: the same layout */
};

struct win { uint32_t ctx_id; struct mn_d3d_window w; uint64_t seq; struct win *next; };
struct displays { uint32_t ctx_id, len; uint64_t seq; uint8_t *p; struct displays *next; };
struct actions { uint32_t ctx_id, n, cap; struct mn_d3d_action *v; struct actions *next; };

/* ponytail: linear lists under one lock; a game has a window or two and a display or two */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct win *wins;
static struct displays *disps;
static struct actions *acts;
static uint64_t seq;

int mn_d3d_window_set(uint32_t ctx_id, const struct mn_d3d_window *w) {
    pthread_mutex_lock(&lock);
    struct win *k = wins;
    while (k && !(k->ctx_id == ctx_id && k->w.hwnd == w->hwnd)) k = k->next;
    if (!k && (k = calloc(1, sizeof *k))) k->ctx_id = ctx_id, k->next = wins, wins = k;
    if (k) k->w = *w, k->seq = ++seq;
    pthread_mutex_unlock(&lock);
    return k ? 0 : -1;
}

/* ctx_id 0: the latest in any context; hwnd 0: the context's latest window. Under lock. */
static struct win *find(uint32_t ctx_id, uint64_t hwnd) {
    struct win *best = NULL;
    for (struct win *k = wins; k; k = k->next)
        if ((!hwnd || k->w.hwnd == hwnd) && (!ctx_id || k->ctx_id == ctx_id) && (!best || k->seq > best->seq))
            best = k;
    return best;
}

int mn_d3d_window_of(uint32_t ctx_id, uint64_t hwnd, struct mn_d3d_window *out) {
    pthread_mutex_lock(&lock);
    struct win *k = find(ctx_id, hwnd);
    if (k) *out = k->w;
    pthread_mutex_unlock(&lock);
    return k != NULL;
}

/* The displays' records, checked: each display and its modes inside len. */
static int valid(const uint8_t *p, uint32_t len) {
    uint32_t count, at = 8;
    if (len < 8) return 0;
    memcpy(&count, p, sizeof count);
    for (uint32_t i = 0; i < count; i++) {
        struct mn_d3d_display d;
        if (len - at < sizeof d) return 0;
        memcpy(&d, p + at, sizeof d);
        at += sizeof d;
        if (!d.modes || (len - at) / sizeof(struct mn_d3d_mode) < d.modes) return 0;
        at += d.modes * (uint32_t)sizeof(struct mn_d3d_mode);
    }
    return 1;
}

int mn_d3d_displays_set(uint32_t ctx_id, const void *p, uint32_t len) {
    if (!valid(p, len)) return -1;
    uint8_t *copy = malloc(len);
    if (!copy) return -1;
    memcpy(copy, p, len);
    pthread_mutex_lock(&lock);
    struct displays *k = disps;
    while (k && k->ctx_id != ctx_id) k = k->next;
    if (!k && (k = calloc(1, sizeof *k))) k->ctx_id = ctx_id, k->next = disps, disps = k;
    if (k) free(k->p), k->p = copy, k->len = len, k->seq = ++seq, copy = NULL;
    pthread_mutex_unlock(&lock);
    free(copy);
    return k ? 0 : -1;
}

/* Op 9: the context's actions into out (at most cap bytes, whole ones), then forgotten: the bytes. */
uint32_t mn_d3d_actions_take(uint32_t ctx_id, void *out, uint32_t cap) {
    uint32_t n = 0;
    pthread_mutex_lock(&lock);
    for (struct actions *k = acts; k; k = k->next)
        if (k->ctx_id == ctx_id) {
            n = k->n < cap / sizeof *k->v ? k->n : cap / (uint32_t)sizeof *k->v;
            memcpy(out, k->v, n * sizeof *k->v);
            k->n = 0;
        }
    pthread_mutex_unlock(&lock);
    return n * (uint32_t)sizeof(struct mn_d3d_action);
}

/* An action native DXMT took during one of a context's calls; one off a call (a release) has nobody to make it. */
static void hook_action(const struct mn_d3d_action *a) {
    uint32_t ctx_id = mn_d3d_current_ctx();
    if (!ctx_id) return;
    pthread_mutex_lock(&lock);
    struct actions *k = acts;
    while (k && k->ctx_id != ctx_id) k = k->next;
    if (!k && (k = calloc(1, sizeof *k))) k->ctx_id = ctx_id, k->next = acts, acts = k;
    if (k && k->n == k->cap) {
        struct mn_d3d_action *v = realloc(k->v, (k->cap * 2 + 8) * sizeof *v);
        if (v) k->v = v, k->cap = k->cap * 2 + 8;
    }
    if (k && k->n < k->cap) k->v[k->n++] = *a;
    pthread_mutex_unlock(&lock);
}

void mn_d3d_windows_end(uint32_t ctx_id) {
    pthread_mutex_lock(&lock);
    for (struct actions **kp = &acts; *kp;) {
        struct actions *k = *kp;
        if (k->ctx_id == ctx_id) *kp = k->next, free(k->v), free(k);
        else kp = &k->next;
    }
    for (struct win **kp = &wins; *kp;) {
        struct win *k = *kp;
        if (k->ctx_id == ctx_id) *kp = k->next, free(k);
        else kp = &k->next;
    }
    for (struct displays **kp = &disps; *kp;) {
        struct displays *k = *kp;
        if (k->ctx_id == ctx_id) *kp = k->next, free(k->p), free(k);
        else kp = &k->next;
    }
    pthread_mutex_unlock(&lock);
}

/* The context's displays (ctx_id 0: the latest pushed), then display index's offset in them: 1, or 0 past the last.
 * Under lock. */
static int display_at(uint32_t index, const uint8_t **p, struct mn_d3d_display *d) {
    uint32_t ctx_id = mn_d3d_current_ctx();
    struct displays *k = NULL;
    for (struct displays *j = disps; j; j = j->next)
        if (ctx_id ? j->ctx_id == ctx_id : (!k || j->seq > k->seq)) k = j;
    if (!k) return 0;
    uint32_t count, at = 8;
    memcpy(&count, k->p, sizeof count);
    for (uint32_t i = 0; i < count; i++) {
        memcpy(d, k->p + at, sizeof *d);
        if (i == index) return *p = k->p + at, 1;
        at += (uint32_t)sizeof *d + d->modes * (uint32_t)sizeof(struct mn_d3d_mode);
    }
    return 0;
}

static int hook_window(uint64_t hwnd, struct WMTWsiWindow *out) {
    struct mn_d3d_window w;
    if (!mn_d3d_window_of(mn_d3d_current_ctx(), hwnd, &w)) return 0;
    *out = (struct WMTWsiWindow){w.monitor, w.width, w.height, w.flags};
    return 1;
}

static int hook_display(uint32_t index, struct WMTWsiDisplay *out) {
    const uint8_t *p;
    struct mn_d3d_display d;
    pthread_mutex_lock(&lock);
    int ok = display_at(index, &p, &d);
    pthread_mutex_unlock(&lock);
    if (ok) {
        *out = (struct WMTWsiDisplay){d.monitor, d.left, d.top, d.right, d.bottom, {0}};
        memcpy(out->name, d.name, sizeof out->name);
    }
    return ok;
}

static int hook_mode(uint64_t monitor, uint32_t index, struct WMTWsiMode *out) {
    const uint8_t *p;
    struct mn_d3d_display d;
    int ok = 0;
    pthread_mutex_lock(&lock);
    for (uint32_t i = 0; !ok && display_at(i, &p, &d); i++)
        if (d.monitor == monitor && index < d.modes) {
            struct mn_d3d_mode m;
            memcpy(&m, p + sizeof d + index * sizeof m, sizeof m);
            *out = (struct WMTWsiMode){m.width, m.height, m.refresh_num, m.refresh_den};
            ok = 1;
        } else if (d.monitor == monitor) {
            break;
        }
    pthread_mutex_unlock(&lock);
    return ok;
}

void mn_d3d_wsi_hooks(void (*set)(const void *hooks)) {
    static const struct WMTWsiHooks hooks = {hook_window, hook_display, hook_mode, hook_action};
    set(&hooks);
}
