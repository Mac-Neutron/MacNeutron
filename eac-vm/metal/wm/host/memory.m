/* The host's side of memory the guest touches (milestone 4b-1, plan Task 5): buffers over blob memory, emulated
 * heaps, the residency sets they join, and the bounds winemetal doesn't check (slots 107, 45, 145). A Metal object
 * made over a blob region carries a holder (an associated object): when Metal deallocates the object, the holder
 * releases the blob and tells the guest the region is free (MN_NOTIFY_REGION_FREE). */
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../host/executor.h"
#include "../marshal.h"
#include "../notify.h"
#include "notifyq.h"
#include "winemetal_thunks.h"

#define STORAGE(o) ((uint64_t)(o) & 0xf0)
#define PAGES(n) (((uint64_t)(n) + MN_PAGE - 1) / MN_PAGE * MN_PAGE)
#define BACKLOG_MAX (1u << 20)

/* ---- notify queues ---- */

struct mn_notifyq {
    _Atomic int refs;
    uint32_t ctx_id;
    uint64_t blob_id;
    pthread_mutex_t m;
    struct mn_notify *n;
    void *hold;
    struct mn_notify_rec *backlog; /* what didn't fit while the guest hadn't read */
    size_t nback, capback;
};
static pthread_mutex_t notify_lock = PTHREAD_MUTEX_INITIALIZER;

void mn_notifyq_put(struct mn_notifyq *q) {
    if (!q || atomic_fetch_sub(&q->refs, 1) != 1) return;
    mn_executor_blob_release(q->hold);
    pthread_mutex_destroy(&q->m);
    free(q->backlog);
    free(q);
}

static struct mn_notifyq *q_get(struct mn_wm_ctx *c) {
    pthread_mutex_lock(&notify_lock);
    struct mn_notifyq *q = c->notify;
    if (q) atomic_fetch_add(&q->refs, 1);
    pthread_mutex_unlock(&notify_lock);
    return q;
}

/* Under q->m: 1 if the guest had room for it. */
static int q_write(struct mn_notifyq *q, struct mn_notify_rec r) {
    uint64_t head = atomic_load_explicit(&q->n->head, memory_order_relaxed);
    uint64_t tail = atomic_load_explicit(&q->n->tail, memory_order_acquire);
    if (head - tail >= MN_NOTIFY_SLOTS) return 0;
    q->n->recs[head % MN_NOTIFY_SLOTS] = r;
    atomic_store_explicit(&q->n->head, head + 1, memory_order_release);
    return 1;
}

/* ponytail: the backlog drains on the next post; a flush on the guest's read if a stuck tail ever shows. */
void mn_notifyq_post(struct mn_notifyq *q, uint32_t type, uint64_t value) {
    if (!q) return;
    pthread_mutex_lock(&q->m);
    size_t i = 0;
    while (i < q->nback && q_write(q, q->backlog[i])) i++;
    memmove(q->backlog, q->backlog + i, (q->nback - i) * sizeof *q->backlog);
    q->nback -= i;
    struct mn_notify_rec r = {type, 0, value};
    if (q->nback || !q_write(q, r)) {
        if (q->nback == q->capback && q->capback < BACKLOG_MAX) {
            struct mn_notify_rec *b = realloc(q->backlog, (q->capback ? q->capback * 2 : 256) * sizeof *b);
            if (b) q->backlog = b, q->capback = q->capback ? q->capback * 2 : 256;
        }
        if (q->nback < q->capback) q->backlog[q->nback++] = r;
        else fprintf(stderr, "mn-winemetal: notify queue full, a record lost\n");
    }
    pthread_mutex_unlock(&q->m);
    mn_executor_notified(q->ctx_id, q->blob_id);
}

struct mn_notifyq *mn_notifyq_new(struct mn_wm_ctx *c, uint64_t blob_id) {
    struct mn_notifyq *q = calloc(1, sizeof *q);
    if (!q) return NULL;
    if (!(q->n = mn_executor_blob_hold(c->ctx_id, blob_id, 0, MN_NOTIFY_BYTES, &q->hold))) {
        free(q);
        return NULL;
    }
    q->refs = 1, q->ctx_id = c->ctx_id, q->blob_id = blob_id;
    pthread_mutex_init(&q->m, NULL);
    return q;
}

NTSTATUS_T mn_wm_notify_attach(struct mn_wm_ctx *c, uint64_t blob_id, uint64_t listener) {
    if (listener) return MN_STATUS_INVALID_PARAMETER; /* a listener's queue comes with slot 108 */
    struct mn_notifyq *q = mn_notifyq_new(c, blob_id);
    if (!q) return MN_STATUS_INVALID_PARAMETER;
    pthread_mutex_lock(&notify_lock);
    struct mn_notifyq *old = c->notify;
    c->notify = q;
    pthread_mutex_unlock(&notify_lock);
    mn_notifyq_put(old);
    return 0;
}

void mn_wm_ctx_end(struct mn_wm_ctx *c) {
    pthread_mutex_lock(&notify_lock);
    struct mn_notifyq *q = c->notify;
    c->notify = NULL;
    pthread_mutex_unlock(&notify_lock);
    mn_notifyq_put(q); /* holders still alive keep it until they go */
}

void mn_wm_drop_hold(struct mn_wm_ctx *c, struct mn_wm_hold *h) {
    if (h->hold) mn_executor_blob_release(h->hold);
    struct mn_notifyq *q = q_get(c);
    mn_notifyq_post(q, MN_NOTIFY_REGION_FREE, h->region);
    mn_notifyq_put(q);
    h->hold = NULL;
    h->taken = 1;
}

/* ---- holders and emulated heaps ---- */

@interface MNHeap : NSObject {
@public
    id device;
    uint8_t *base;
    uint64_t size, region;
    void *hold;
    struct mn_notifyq *q;
    NSMutableArray *sets; /* residency sets the heap was added to */
    NSHashTable *subs;    /* its buffers (weak) */
}
@end

@interface MNHolder : NSObject {
@public
    void *hold; /* the blob, or NULL when an emulated heap keeps it mapped */
    MNHeap *heap;
    uint64_t region;
    struct mn_notifyq *q;
    NSMutableArray *sets; /* the residency sets an emulated heap's buffer is in */
}
@end

static NSHashTable *heaps; /* every emulated heap (weak), for slot 135 */
static pthread_mutex_t heaps_lock = PTHREAD_MUTEX_INITIALIZER;
static char holder_key;

@implementation MNHeap
- (void)dealloc {
    pthread_mutex_lock(&heaps_lock);
    [heaps removeObject:self];
    pthread_mutex_unlock(&heaps_lock);
    mn_executor_blob_release(hold);
    mn_notifyq_post(q, MN_NOTIFY_REGION_FREE, region);
    mn_notifyq_put(q);
    [device release];
    [sets release];
    [subs release];
    [super dealloc];
}
@end

@implementation MNHolder
- (void)dealloc {
    if (hold) mn_executor_blob_release(hold);
    mn_notifyq_post(q, MN_NOTIFY_REGION_FREE, region);
    mn_notifyq_put(q);
    [heap release];
    [sets release];
    [super dealloc];
}
@end

static MNHolder *hold_on(id obj, void *hold, MNHeap *heap, uint64_t region, struct mn_wm_ctx *c) {
    MNHolder *h = [[MNHolder alloc] init];
    h->hold = hold;
    h->heap = [heap retain];
    h->region = region;
    h->q = q_get(c);
    /* Non-atomic: its getter hands back the holder without an autorelease, which would keep it (and the region) until
     * some pool drains. */
    objc_setAssociatedObject(obj, &holder_key, h, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [h release];
    return h;
}

void mn_wm_will_release(void *obj) {
    MNHolder *h = objc_getAssociatedObject((id)obj, &holder_key);
    if (!h || !h->heap) return;
    NSArray *sets;
    @synchronized(h->heap) {
        sets = [h->sets copy];
        [h->sets removeAllObjects];
    }
    for (id<MTLResidencySet> set in sets) { /* native placed buffers aren't in sets: it must go when released */
        [set removeAllocation:(id<MTLAllocation>)obj];
        [set commit];
    }
    [sets release];
}

/* 18: a buffer over blob memory (the blob field put the host's mapping in memory.ptr), whole pages of it. */
static NTSTATUS_T new_buffer(struct mn_wm_state *k) {
    struct unixcall_mtldevice_newbuffer *a = (void *)k->p;
    struct WMTBufferInfo *info = a->info.ptr;
    struct mn_wm_hold *x = k->nholds ? &k->holds[0] : NULL;
    if (!x || !info) return mn_wm_invoke(18, a); /* Metal's own memory */
    uint64_t st = STORAGE(info->options);
    if (st == 32 || st == 48 || !x->ptr) return MN_STATUS_INVALID_PARAMETER;
    uint64_t length = info->length;
    info->length = PAGES(length);
    NTSTATUS_T s = mn_wm_invoke(18, a);
    info->length = length;
    if (!s && a->ret) {
        hold_on((id)a->ret, x->hold, nil, x->region, k->c);
        x->taken = 1;
    }
    return s;
}

/* 138: a CPU-visible heap is our blob region, with no MTLHeap. */
static NTSTATUS_T new_heap(struct mn_wm_state *k) {
    struct unixcall_mtldevice_newheap *a = (void *)k->p;
    struct mn_wm_hold *x = k->prefix_hold;
    if (!x) return mn_wm_invoke(138, a); /* Private: a real heap */
    const struct WMTHeapInfo *info = a->info.ptr;
    if (!info || !x->ptr || info->size > x->len || !a->device) return MN_STATUS_INVALID_PARAMETER;
    MNHeap *h = [[MNHeap alloc] init];
    h->device = [(id)a->device retain];
    h->base = x->ptr, h->size = x->len, h->hold = x->hold, h->region = x->region;
    h->q = q_get(k->c);
    h->sets = [[NSMutableArray alloc] init];
    h->subs = [[NSHashTable weakObjectsHashTable] retain];
    x->taken = 1;
    pthread_mutex_lock(&heaps_lock);
    if (!heaps) heaps = [[NSHashTable weakObjectsHashTable] retain];
    [heaps addObject:h];
    pthread_mutex_unlock(&heaps_lock);
    a->ret = (obj_handle_t)h;
    k->out_kind[0] = MN_HK_HEAP_EMU;
    return 0;
}

/* 141 on an emulated heap: a buffer over its memory at offset, in the residency sets the heap is in. */
static NTSTATUS_T heap_buffer(struct mn_wm_state *k) {
    struct unixcall_mtlheap_newbuffer *a = (void *)k->p;
    struct mn_wm_hold *x = k->prefix_hold;
    if (![(id)a->heap isKindOfClass:[MNHeap class]]) return x ? MN_STATUS_INVALID_PARAMETER : mn_wm_invoke(141, a);
    MNHeap *h = (id)a->heap;
    struct WMTBufferInfo *info = a->info.ptr;
    uint64_t off = a->offset, span = PAGES(info ? info->length : 0);
    if (!x || !info || !span || off % MN_PAGE || off > h->size || span > h->size - off)
        return MN_STATUS_INVALID_PARAMETER;
    struct WMTBufferInfo bi = {.length = span, .options = info->options};
    bi.memory.ptr = h->base + off;
    struct unixcall_mtldevice_newbuffer nb = {(obj_handle_t)h->device, {&bi}, 0};
    NTSTATUS_T s = mn_wm_invoke(18, &nb);
    id buf = (id)nb.ret;
    if (s || !buf) return s;
    MNHolder *holder = hold_on(buf, NULL, h, x->region, k->c);
    x->taken = 1;
    @synchronized(h) {
        [h->subs addObject:buf];
        holder->sets = [h->sets mutableCopy];
        for (id<MTLResidencySet> set in h->sets) {
            [set addAllocation:buf];
            [set commit];
        }
    }
    a->ret = (obj_handle_t)buf;
    info->gpu_address = bi.gpu_address;
    info->memory.ptr = bi.memory.ptr;
    return 0;
}

static void track_set(id buf, id set, int add) {
    MNHolder *holder = objc_getAssociatedObject(buf, &holder_key);
    if (!holder) return;
    if (add && ![holder->sets containsObject:set]) [holder->sets addObject:set];
    if (!add) [holder->sets removeObject:set];
}

/* 133, 134: an emulated heap in the allocations stands for its buffers, now and later. */
static NTSTATUS_T residency(struct mn_wm_state *k, int add) {
    struct unixcall_mtlresidencyset_addallocations *a = (void *)k->p;
    id<MTLResidencySet> set = (id)a->set;
    obj_handle_t *v = (obj_handle_t *)a->allocations.ptr;
    uint64_t kept = 0;
    for (uint64_t i = 0; v && i < a->count; i++) {
        id o = (id)v[i];
        if (!o) continue; /* a nil (failed) object is nothing to add */
        if (![o isKindOfClass:[MNHeap class]]) {
            v[kept++] = v[i];
            continue;
        }
        MNHeap *h = o;
        @synchronized(h) {
            if (add && ![h->sets containsObject:set]) [h->sets addObject:set];
            if (!add) [h->sets removeObject:set];
            for (id buf in h->subs) {
                if (add) [set addAllocation:buf];
                else [set removeAllocation:buf];
                track_set(buf, set, add);
            }
        }
    }
    a->count = kept;
    return kept && set ? mn_wm_invoke(k->slot, a) : 0;
}

/* 135: the set forgets every emulated heap too. */
static NTSTATUS_T residency_clear(struct mn_wm_state *k) {
    struct unixcall_generic_obj_noret *a = (void *)k->p;
    id set = (id)a->handle;
    pthread_mutex_lock(&heaps_lock);
    NSArray *all = [[heaps allObjects] retain];
    pthread_mutex_unlock(&heaps_lock);
    for (MNHeap *h in all) @synchronized(h) {
            [h->sets removeObject:set];
            for (id buf in h->subs) track_set(buf, set, 0);
        }
    [all release];
    return mn_wm_invoke(135, a);
}

/* 107: offset and length must fit in the buffer, which winemetal copies into unchecked. */
static NTSTATUS_T update_contents(struct mn_wm_state *k) {
    struct unixcall_mtlbuffer_updatecontents *a = (void *)k->p;
    id<MTLBuffer> b = (id)a->buffer;
    if (!b || [b storageMode] == MTLStorageModePrivate || a->offset > [b length] || a->length > [b length] - a->offset ||
        (a->length && !a->data.ptr))
        return MN_STATUS_INVALID_PARAMETER;
    return mn_wm_invoke(107, a);
}

/* 45, 145: the region must lie in the texture's level and slice, and the data be exactly what it covers. */
static NTSTATUS_T texture_data(struct mn_wm_state *k) {
    struct unixcall_mtltexture_replaceregion *a = (void *)k->p;
    id<MTLTexture> t = (id)a->texture;
    if (!t || [t storageMode] == MTLStorageModePrivate) return MN_STATUS_INVALID_PARAMETER;
    uint32_t fmt = (uint32_t)[t pixelFormat], bytes, bw, bh;
    uint64_t ext = mn_wm_region_extent(fmt, a->size.width, a->size.height, a->size.depth, a->bytes_per_row,
                                       a->bytes_per_image);
    if (ext == UINT64_MAX || ext != k->data_len || (ext && !k->data) || mn_wm_format_block(fmt, &bytes, &bw, &bh))
        return MN_STATUS_INVALID_PARAMETER;
    if (!ext) return 0;
    MTLTextureType type = [t textureType];
    if (type == MTLTextureType2DMultisample || type == MTLTextureType2DMultisampleArray ||
        a->level >= [t mipmapLevelCount])
        return MN_STATUS_INVALID_PARAMETER;
    uint64_t lw = [t width] >> a->level, lh = [t height] >> a->level, ld = [t depth] >> a->level;
    lw = lw ? lw : 1, lh = lh ? lh : 1, ld = type == MTLTextureType3D && ld ? ld : 1;
    uint64_t slices = type == MTLTextureType3D ? 1
                      : [t arrayLength] * (type == MTLTextureTypeCube || type == MTLTextureTypeCubeArray ? 6 : 1);
    if (a->slice >= slices || a->origin.x > lw || a->size.width > lw - a->origin.x || a->origin.y > lh ||
        a->size.height > lh - a->origin.y || a->origin.z > ld || a->size.depth > ld - a->origin.z)
        return MN_STATUS_INVALID_PARAMETER;
    if (a->origin.x % bw || a->origin.y % bh || (a->size.width % bw && a->origin.x + a->size.width != lw) ||
        (a->size.height % bh && a->origin.y + a->size.height != lh))
        return MN_STATUS_INVALID_PARAMETER;
    a->data.ptr = k->data;
    return mn_wm_invoke(k->slot, a);
}

NTSTATUS_T mn_wm_special(struct mn_wm_state *k) {
    @try { /* a guest's object of the wrong class raises here as in winemetal */
        switch (k->slot) {
        case 18: return new_buffer(k);
        case 138: return new_heap(k);
        case 141: return heap_buffer(k);
        case 142:
            return [(id)((struct unixcall_mtlheap_newtexture *)k->p)->heap isKindOfClass:[MNHeap class]]
                       ? MN_STATUS_NOT_SUPPORTED
                       : mn_wm_invoke(142, k->p);
        case 133:
        case 134: return residency(k, k->slot == 133);
        case 135: return residency_clear(k);
        case 107: return update_contents(k);
        case 45:
        case 145: return texture_data(k);
        case 36:
        case 37:
        case 38: return mn_wm_encode(k);
        case 104:
        case 108:
        case 109:
        case 110:
        case 146: return mn_wm_listener_special(k);
        case 91: return mn_wm_enumerate(k);
        case 98: return mn_wm_constants(k);
        case 54:
        case 112:
        case 113:
        case 115:
        case 117:
        case 119: return mn_wm_paths(k);
        default: return k->slot >= 74 && k->slot <= 88 ? mn_wm_sm50(k) : MN_STATUS_NOT_IMPLEMENTED;
        }
    } @catch (id e) {
        fprintf(stderr, "mn-winemetal: slot %u raised %s\n", k->slot, [[e description] UTF8String]);
        return MN_STATUS_INVALID_PARAMETER;
    }
}
