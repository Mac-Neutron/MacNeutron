/* Milestone 4b's handle decision: what translating guest-chosen handles costs the host per SMITE 2 frame, against
 * passing host pointers through.
 *   cc -O2 -o handles-bench handles-bench.c && ./handles-bench
 * Guest handles are (generation << 32 | index) into a dense host table, so translating one is a bounds check, a
 * generation check and a load; a stale or forged handle fails the check. Per frame, from results/callstats.txt:
 * ~2,770 calls (one handle each), ~5,150 command nodes (about 1.3 handles each, rewritten in place as the host
 * decodes them), ~700 releases and as many creations, ~30,000 live objects. The pass-through baseline runs the same
 * loops without the table. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CAP 65536
#define LIVE 30000
#define CALLS 2770
#define NODES 5150
#define CHURN 700
#define FRAMES 10000

struct entry { void *obj; uint32_t gen, refs; };
static struct entry table[CAP];
static uint32_t free_list[CAP], nfree;
static uint64_t live[CAP];
static uint32_t nlive;
static uint64_t rng = 88172645463325252ull;

static uint64_t next(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
static uint64_t now(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

static uint64_t insert(void *obj) {
    uint32_t i = free_list[--nfree];
    table[i].obj = obj;
    table[i].refs = 1;
    return (uint64_t)table[i].gen << 32 | i;
}

static inline void *lookup(uint64_t h) {
    uint32_t i = (uint32_t)h;
    if (i >= CAP || table[i].gen != (uint32_t)(h >> 32) || !table[i].obj) return NULL;
    return table[i].obj;
}

static void release(uint64_t h) {
    uint32_t i = (uint32_t)h;
    if (!lookup(h) || --table[i].refs) return;
    table[i].obj = NULL;
    table[i].gen++;
    free_list[nfree++] = i;
}

/* A command node as DXMT encodes them: a 16-byte header, a handle, operands. */
struct node { uint16_t type, r[3]; uint64_t next, handle, a, b; };
static struct node nodes[NODES];

static int cmp(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static void frame_nodes(int translate, uint64_t *sink) {
    for (int n = 0; n < NODES; n++) {
        nodes[n].type = (uint16_t)(next() % 40);
        nodes[n].handle = live[next() % nlive];
    }
    uint64_t s = 0;
    for (int n = 0; n < NODES; n++) {
        int handles = 1 + (n % 10 < 3); /* about 1.3 per node */
        for (int k = 0; k < handles; k++) {
            uint64_t h = k ? live[(n * 7919u) % nlive] : nodes[n].handle;
            void *obj = translate ? lookup(h) : (void *)h;
            if (translate) nodes[n].handle = (uint64_t)obj; /* rewritten in place for the host's decoder */
            s += (uint64_t)obj;
        }
        nodes[n].next = (uint64_t)&nodes[n + 1]; /* both designs rebuild the list with host pointers */
    }
    *sink += s;
}

static uint64_t run(int translate) {
    uint64_t *t = malloc(FRAMES * sizeof *t), sink = 0;
    for (int f = 0; f < FRAMES; f++) {
        uint64_t t0 = now();
        for (int c = 0; c < CALLS; c++) {
            uint64_t h = live[next() % nlive];
            sink += (uint64_t)(translate ? lookup(h) : (void *)h);
        }
        frame_nodes(translate, &sink);
        for (int c = 0; c < CHURN; c++) { /* steady state: as many created as released */
            uint32_t k = (uint32_t)(next() % nlive);
            if (translate) {
                release(live[k]);
                live[k] = insert((void *)(uintptr_t)(0x100000000ull + next()));
            } else {
                live[k] = 0x100000000ull + next();
            }
        }
        t[f] = now() - t0;
    }
    qsort(t, FRAMES, sizeof *t, cmp);
    printf("%s: median %llu ns/frame, p99 %llu ns (sink %llu)\n", translate ? "translated  " : "pass-through",
           (unsigned long long)t[FRAMES / 2], (unsigned long long)t[FRAMES * 99 / 100], (unsigned long long)(sink & 1));
    uint64_t m = t[FRAMES / 2];
    free(t);
    return m;
}

/* The lookups alone: a frame's handles drawn in advance (random over the live set, as a frame's mix of objects is),
 * then only the walk is timed. */
#define STREAM (CALLS + NODES * 13 / 10)
static uint64_t stream[STREAM];

static uint64_t walk(int translate) {
    uint64_t *t = malloc(FRAMES * sizeof *t), sink = 0;
    for (int f = 0; f < FRAMES; f++) {
        for (int i = 0; i < STREAM; i++) stream[i] = live[next() % nlive];
        uint64_t s = 0, t0 = now();
        if (translate)
            for (int i = 0; i < STREAM; i++) s += (uint64_t)lookup(stream[i]);
        else
            for (int i = 0; i < STREAM; i++) s += stream[i];
        t[f] = now() - t0;
        sink += s;
    }
    qsort(t, FRAMES, sizeof *t, cmp);
    uint64_t m = t[FRAMES / 2];
    printf("walk %s: median %llu ns per frame of %d handles (sink %llu)\n", translate ? "translated  " : "pass-through",
           (unsigned long long)m, STREAM, (unsigned long long)(sink & 1));
    free(t);
    return m;
}

int main(void) {
    for (uint32_t i = 0; i < CAP; i++) free_list[nfree++] = CAP - 1 - i;
    for (nlive = 0; nlive < LIVE; nlive++) live[nlive] = insert((void *)(uintptr_t)(0x100000000ull + next()));
    uint64_t wb = walk(0), wt = walk(1);
    printf("lookups alone: %.2f µs per frame, %.2f ns per handle\n", (double)(wt - wb) / 1000, (double)(wt - wb) / STREAM);
    uint64_t base = run(0), tr = run(1);
    double lookups = CALLS + NODES * 1.3 + CHURN;
    printf("translation adds %.1f µs per frame: %.2f ns per handle, %.0f handles per frame\n",
           (double)(tr - base) / 1000, (double)(tr - base) / lookups, lookups);
    return 0;
}
