/* The marshaller's field kinds for Direct3D forwarding (M2 Task 3), guest and host in one process with no transport:
 * a call table packed by mn_wm_pack_call and run by mn_wm_host_call_table, as the generator's tables are.
 *   WMF_WSTR_IN  the guest's UTF-16 string (a surrogate pair too) arrives as the host's native wide string
 *   WMF_UNION    an array of D3D12_RESOURCE_BARRIER-shaped elements: each one's handles translate per its type
 * Prints "kinds-test: ok" or the first failure. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../marshal.h"
#include "../../guest/transport.h"
#include "loopback.h"

extern struct mn_wm_ctx loop_ctx;
static int failed;
#define CHECK(c) do { if (!(c)) { printf("kinds-test: FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failed = 1; } } while (0)

static int ran;

/* The guest winemetal.c's listener sets Wine's events: none here. */
unsigned int NtSetEvent(void *handle, void *prev) { (void)handle, (void)prev; return 0; }

/* WMF_WSTR_IN */
struct name_params { uint64_t self; uint64_t name; };
static const struct mn_wm_field name_fields[] = {{.offset = 8, .kind = WMF_WSTR_IN}};
static const struct mn_wm_call name_call = {"SetName", sizeof(struct name_params), MN_WM_SYNC, MN_SP_NONE, 1, 0,
                                            name_fields, NULL};
static NTSTATUS_T name_invoke(void *params) {
    struct name_params *p = params;
    ran = !wcscmp((const wchar_t *)(uintptr_t)p->name, L"caché \U0001F600");
    return 0;
}

/* WMF_UNION: {type, flags, union {transition {resource, subresource, before, after}, aliasing {before, after},
 * uav {resource}}}, 32 bytes, in an array the count gives. */
struct barrier { uint32_t type, flags; uint64_t a, b, c; };
struct barrier_params { uint64_t self; uint32_t count, pad; uint64_t barriers; };
static const struct mn_wm_field transition_fields[] = {{.offset = 0, .kind = WMF_HANDLE_IN, .hkind = MN_HK_D3D}};
static const struct mn_wm_field aliasing_fields[] = {{.offset = 0, .kind = WMF_HANDLE_IN, .hkind = MN_HK_D3D},
                                                     {.offset = 8, .kind = WMF_HANDLE_IN, .hkind = MN_HK_D3D}};
static const struct mn_wm_field uav_fields[] = {{.offset = 0, .kind = WMF_HANDLE_IN, .hkind = MN_HK_D3D}};
static const struct mn_wm_field element_fields[] = {
    {.offset = 8, .kind = WMF_UNION, .count_offset = 0, .count_width = 4, .nested = 1}};
static const struct mn_wm_nested nested[] = {{1, element_fields}, {1, transition_fields}, {2, aliasing_fields},
                                             {1, uav_fields}};
static const uint32_t variant_values[] = {0, 1, 2};
static const uint8_t variant_nested[] = {2, 3, 4}; /* 1 + an index into nested */
static const struct mn_wm_union unions[] = {{3, variant_values, variant_nested}};
static const struct mn_wm_tables tables = {nested, unions};
static const struct mn_wm_field barrier_fields[] = {{.offset = 16, .kind = WMF_IN, .flags = WMF_ARRAY,
                                                     .size = sizeof(struct barrier), .count_offset = 8,
                                                     .count_width = 4, .nested = 1}};
static const struct mn_wm_call barrier_call = {"ResourceBarrier", sizeof(struct barrier_params), MN_WM_QUEUED,
                                               MN_SP_NONE, 1, 0, barrier_fields, &tables};
static int obj_a, obj_b;
static NTSTATUS_T barrier_invoke(void *params) {
    struct barrier_params *p = params;
    const struct barrier *b = (const struct barrier *)(uintptr_t)p->barriers;
    ran = p->count == 2 && b[0].a == (uintptr_t)&obj_a && b[0].b == 7 && b[1].a == (uintptr_t)&obj_b;
    return 0;
}

static void run(const struct mn_wm_call *c, void *params, NTSTATUS_T (*invoke)(void *)) {
    static uint8_t rec[MN_REC_MAX], reply[MN_REPLY_BYTES];
    struct mn_wm_out o = {.buf = rec, .cap = sizeof rec};
    ran = 0;
    CHECK(mn_wm_pack_call(c, params, &o) == 0);
    uint32_t n = 0;
    CHECK(mn_wm_host_call_table(&loop_ctx, loop_ring(), c, invoke, rec, o.len, o.sync ? reply : NULL,
                                o.sync ? sizeof reply : 0, &n) == 0);
    if (o.sync) CHECK(mn_wm_unpack_call(c, params, &o, reply, n) == 0);
    CHECK(ran);
}

int main(void) {
    mn_open();
    /* "caché 😀" in UTF-16: é one unit, the face a surrogate pair. */
    uint16_t name[] = {'c', 'a', 'c', 'h', 0xe9, ' ', 0xd83d, 0xde00, 0};
    struct name_params np = {.self = 1, .name = (uintptr_t)name};
    run(&name_call, &np, name_invoke);

    mn_handle ha = mn_gh_alloc(MN_OWN_PLUS1), hb = mn_gh_alloc(MN_OWN_PLUS1);
    CHECK(!mn_ht_bind(loop_ctx.ht, loop_ring(), ha, MN_HK_D3D, &obj_a, MN_OWN_PLUS1));
    CHECK(!mn_ht_bind(loop_ctx.ht, loop_ring(), hb, MN_HK_D3D, &obj_b, MN_OWN_PLUS1));
    struct barrier b[2] = {{.type = 0, .a = ha, .b = 7}, {.type = 2, .a = hb}};
    struct barrier_params bp = {.self = 1, .count = 2, .barriers = (uintptr_t)b};
    run(&barrier_call, &bp, barrier_invoke);
    if (!failed) puts("kinds-test: ok");
    return failed;
}
