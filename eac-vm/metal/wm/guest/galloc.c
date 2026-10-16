/* The guest's blob sub-allocator (galloc.h): first fit over each blob's 16 KiB pages, one lock.
 * ponytail: blobs are kept until the process ends; free empty ones if guest memory pressure shows. */
#include "galloc.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "../../guest/transport.h"
#include "../calls.h"
#include "../notify.h"

#define BLOB_BYTES (64ull << 20)
#define BLOB_PAGES ((uint32_t)(BLOB_BYTES / MN_PAGE))

struct rinfo { uint32_t pages, holders; uint8_t live, freed; };
enum { FREE, USED, DIRTY };
struct gblob {
    struct mn_blob b;
    uint32_t npages;
    uint8_t *used;     /* per page: FREE, USED or DIRTY (free, and written since the blob was made) */
    struct rinfo *r;   /* per page: a region's record at its first page */
};
static struct gblob **blobs;
static size_t nblobs;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct mn_notify *queue;
static int queue_tried;

static void on_free(uint64_t value) {
    if (MN_GH_TAG(value) == MN_GH_REGION) mn_gfree(MN_GH_VALUE(value));
}

/* The context's notify queue, attached on the first allocation. Without one, freed regions are never reused. */
static void attach_queue(void) {
    queue_tried = 1;
    mn_gh_on_free = on_free;
    struct mn_blob b;
    if (mn_blob_alloc(MN_NOTIFY_BYTES, &b)) return;
    memset(b.ptr, 0, MN_NOTIFY_BYTES);
    struct mn_wm_notify_attach a = {b.blob_id, 0};
    uint32_t n;
    if (mn_call(MN_OP_WM + MN_WM_NOTIFY_ATTACH, &a, sizeof a, NULL, 0, &n) == 0) queue = b.ptr;
}

static struct gblob *blob_of_id(uint64_t blob_id) {
    for (size_t i = 0; i < nblobs; i++)
        if (blobs[i]->b.blob_id == blob_id) return blobs[i];
    return NULL;
}

static struct rinfo *region(uint64_t id, struct gblob **gb) {
    struct gblob *b = blob_of_id(id >> 32);
    uint32_t page = (uint32_t)id;
    if (!b || page >= b->npages || !b->r[page].live) return NULL;
    if (gb) *gb = b;
    return &b->r[page];
}

static void release_pages(struct gblob *b, uint32_t page) {
    memset(b->used + page, DIRTY, b->r[page].pages);
    b->r[page] = (struct rinfo){0};
}

static void drop_holder(uint64_t id) {
    struct gblob *b;
    struct rinfo *r = region(id, &b);
    if (!r || !r->holders) return;
    if (!--r->holders && r->freed) release_pages(b, (uint32_t)id);
}

static void drain_locked(void) {
    struct mn_notify_rec rec;
    while (queue && mn_notify_next(queue, &rec))
        if (rec.type == MN_NOTIFY_REGION_FREE) drop_holder(rec.value);
}

static struct gblob *new_blob(uint32_t pages) {
    struct gblob *g = calloc(1, sizeof *g);
    struct gblob **grown = realloc(blobs, (nblobs + 1) * sizeof *blobs);
    if (!g || !grown) {
        free(g);
        return NULL;
    }
    blobs = grown;
    if (mn_blob_alloc((uint64_t)pages * MN_PAGE, &g->b) || !(g->used = calloc(pages, 1)) ||
        !(g->r = calloc(pages, sizeof *g->r))) {
        free(g->used);
        free(g);
        return NULL;
    }
    g->npages = pages;
    blobs[nblobs++] = g;
    return g;
}

static int fit(struct gblob *b, uint32_t pages, uint32_t *at) {
    for (uint32_t p = 0, run = 0; p < b->npages; p++) {
        run = b->used[p] == USED ? 0 : run + 1;
        if (run == pages) {
            *at = p + 1 - pages;
            return 1;
        }
    }
    return 0;
}

int mn_galloc(uint64_t len, struct mn_region *out) {
    uint64_t pages64 = len ? (len + MN_PAGE - 1) / MN_PAGE : 1;
    if (pages64 > UINT32_MAX) return -ENOMEM;
    uint32_t pages = (uint32_t)pages64, at = 0;
    pthread_mutex_lock(&lock);
    if (!queue_tried) attach_queue();
    drain_locked();
    struct gblob *b = NULL;
    if (pages <= BLOB_PAGES)
        for (size_t i = 0; i < nblobs && !b; i++)
            if (blobs[i]->npages == BLOB_PAGES && fit(blobs[i], pages, &at)) b = blobs[i];
    if (!b) {
        b = new_blob(pages > BLOB_PAGES ? pages : BLOB_PAGES);
        at = 0;
    }
    if (!b) {
        pthread_mutex_unlock(&lock);
        return -ENOMEM;
    }
    /* As a new Metal buffer's: zeroed (D3D12's committed resources start so, and DXMT relies on fresh memory). */
    for (uint32_t p = at; p < at + pages; p++)
        if (b->used[p] == DIRTY) memset((uint8_t *)b->b.ptr + (uint64_t)p * MN_PAGE, 0, MN_PAGE);
    memset(b->used + at, USED, pages);
    b->r[at] = (struct rinfo){.pages = pages, .live = 1};
    *out = (struct mn_region){MN_REGION(b->b.blob_id, at), (uint8_t *)b->b.ptr + (uint64_t)at * MN_PAGE,
                              (uint64_t)pages * MN_PAGE};
    pthread_mutex_unlock(&lock);
    return 0;
}

void mn_gfree(uint64_t id) {
    pthread_mutex_lock(&lock);
    struct gblob *b;
    struct rinfo *r = region(id, &b);
    if (r && !r->freed) {
        r->freed = 1;
        if (!r->holders) release_pages(b, (uint32_t)id);
    }
    pthread_mutex_unlock(&lock);
}

void mn_ghold(uint64_t id) {
    pthread_mutex_lock(&lock);
    struct rinfo *r = region(id, NULL);
    if (r) r->holders++;
    pthread_mutex_unlock(&lock);
}

void mn_gunhold(uint64_t id) {
    pthread_mutex_lock(&lock);
    drop_holder(id);
    pthread_mutex_unlock(&lock);
}

int mn_glookup(const void *p, uint64_t len, uint64_t *id, uint64_t *blob_id, uint64_t *offset) {
    int found = -1;
    pthread_mutex_lock(&lock);
    for (size_t i = 0; i < nblobs && found; i++) {
        struct gblob *b = blobs[i];
        const uint8_t *base = b->b.ptr;
        if ((const uint8_t *)p < base || (const uint8_t *)p >= base + (uint64_t)b->npages * MN_PAGE) continue;
        uint64_t off = (uint64_t)((const uint8_t *)p - base);
        uint32_t page = (uint32_t)(off / MN_PAGE), head = page;
        while (head > 0 && !b->r[head].live) head--; /* the region's first page, at or before page */
        struct rinfo *r = &b->r[head];
        if (!r->live || page >= head + r->pages || len > (uint64_t)(head + r->pages) * MN_PAGE - off) break;
        *id = MN_REGION(b->b.blob_id, head), *blob_id = b->b.blob_id, *offset = off;
        found = 0;
    }
    pthread_mutex_unlock(&lock);
    return found;
}

void *mn_gptr(uint64_t id) {
    pthread_mutex_lock(&lock);
    struct gblob *b;
    struct rinfo *r = region(id, &b);
    void *p = r ? (uint8_t *)b->b.ptr + (uint64_t)(uint32_t)id * MN_PAGE : NULL;
    pthread_mutex_unlock(&lock);
    return p;
}

unsigned mn_gholders(uint64_t id) {
    pthread_mutex_lock(&lock);
    struct rinfo *r = region(id, NULL);
    unsigned n = r ? r->holders : 0;
    pthread_mutex_unlock(&lock);
    return n;
}

void mn_gdrain(void) {
    pthread_mutex_lock(&lock);
    drain_locked();
    pthread_mutex_unlock(&lock);
}

void mn_galloc_forget(void) {
    blobs = NULL, nblobs = 0; /* ponytail: the parent's mappings stay in the child, unused */
    queue = NULL, queue_tried = 0;
    pthread_mutex_init(&lock, NULL);
}
