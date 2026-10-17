/* Plan 3a Task 3: every marshalling shape, both directions, in one process (guest side packs, host side unpacks and
 * "calls", the reply comes back). Build with -fsanitize=address: truncated messages must fail without reading out of
 * bounds. Prints one line per case and exits 1 on any failure. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../marshal.h"

static int failures;
#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL %s: %s\n", what, #cond); failures++; } } while (0)

#pragma pack(push, 1)
struct str_params { int32_t _ret; const char *name; };
struct out_params { uint32_t *out; int8_t _ret; };
struct ticket_params { uint64_t iface; uint32_t _ret; void *pTicket; int32_t cbMaxTicket; uint32_t *pcbTicket;
                       const uint64_t *identity; };
struct cbmsg { int32_t user, id; void *pub; int32_t cub; };
struct cb_params { uint64_t cookie; struct cbmsg *msg; };
struct bad_params { void **pp; };
struct retbuf_params { uint64_t iface; struct { uint64_t ptr, len; } _ret; };
/* ISteamInventory's GetItemDefinitionProperty: a buffer whose count is behind a pointer, in and out. */
struct prop_params { uint64_t iface; int8_t _ret; int32_t def; const char *name; char *buf; uint32_t *size; };
/* ISteamInventory's GetResultItems: an array of 16-byte items whose count is behind a pointer. */
struct item { uint64_t id; int32_t def; uint16_t quantity, flags; };
struct items_params { uint64_t iface; int8_t _ret; int32_t result; struct item *items; uint32_t *count; };
#pragma pack(pop)

static const struct mn_field str_fields[] = { { offsetof(struct str_params, name), MN_STR_IN, MN_IN_ONLY } };
static const struct mn_field out_fields[] = { { offsetof(struct out_params, out), MN_FIXED, 0, 4 } };
static const struct mn_field ticket_fields[] = {
    { offsetof(struct ticket_params, pTicket), MN_BUF, 0, 1, offsetof(struct ticket_params, cbMaxTicket), 4 },
    { offsetof(struct ticket_params, pcbTicket), MN_FIXED, 0, 4 },
    { offsetof(struct ticket_params, identity), MN_FIXED, MN_IN_ONLY, 8 },
};
static const struct mn_field cb_fields[] = { { offsetof(struct cb_params, msg), MN_FIXED, MN_INNER, sizeof(struct cbmsg),
    0, 0, 0, offsetof(struct cbmsg, pub), offsetof(struct cbmsg, cub) } };
static const struct mn_field bad_fields[] = { { offsetof(struct bad_params, pp), MN_UNSUPPORTED } };
static const struct mn_field retbuf_fields[] = { { offsetof(struct retbuf_params, _ret), MN_RET_BUF } };
static const struct mn_field prop_fields[] = {
    { offsetof(struct prop_params, name), MN_STR_IN, MN_IN_ONLY },
    { offsetof(struct prop_params, buf), MN_BUF, MN_COUNT_PTR, 1, offsetof(struct prop_params, size), 4 },
    { offsetof(struct prop_params, size), MN_FIXED, 0, 4 },
};
static const struct mn_field items_fields[] = {
    { offsetof(struct items_params, items), MN_BUF, MN_COUNT_PTR, sizeof(struct item), offsetof(struct items_params, count), 4 },
    { offsetof(struct items_params, count), MN_FIXED, 0, 4 },
};
static const struct mn_call_desc descs[] = {
    { "str", sizeof(struct str_params), 1, str_fields },
    { "out", sizeof(struct out_params), 1, out_fields },
    { "ticket", sizeof(struct ticket_params), 3, ticket_fields },
    { "cb", sizeof(struct cb_params), 1, cb_fields },
    { "bad", sizeof(struct bad_params), 1, bad_fields },
    { "retbuf", sizeof(struct retbuf_params), 1, retbuf_fields },
    { "prop", sizeof(struct prop_params), 3, prop_fields },
    { "items", sizeof(struct items_params), 2, items_fields },
};
#define NDESCS (sizeof descs / sizeof *descs)

static int reg_seen;
static void reg_apply(void *ctx, uint32_t type, const char *key, size_t key_len, const char *name, size_t name_len,
                      const void *data, size_t data_len) {
    (void)ctx;
    reg_seen = type == 1 && key_len == 20 && !memcmp(key, "Software\\Valve\\Steam", 20) && name_len == 8 &&
               !memcmp(name, "language", 8) && data_len == 8 && !memcmp(data, "english", 8);
}

/* The host's side of one call: unpack, run `call` on the host copy, pack the reply. */
static int host_roundtrip(const struct mn_buf *req, void (*call)(void *), struct mn_reg_log *log, struct mn_buf *rep) {
    uint32_t index; void *hp; struct mn_arena arena = {0};
    int r = mn_unpack_request(descs, NDESCS, req->data, req->len, &index, &hp, &arena);
    if (!r) { call(hp); r = mn_pack_reply(&descs[index], 0, hp, log, rep); }
    mn_arena_free(&arena);
    return r;
}

static void call_str(void *p) { struct str_params *s = p; s->_ret = (int32_t)strlen(s->name); }
static void call_out(void *p) { struct out_params *s = p; *s->out = 0x12345678; s->_ret = 1; }
static void call_ticket(void *p) {
    struct ticket_params *s = p;
    for (int i = 0; i < 234; i++) ((uint8_t *)s->pTicket)[i] = (uint8_t)i;
    *s->pcbTicket = 234; s->_ret = *s->identity == 77 ? 0xabc : 0; s->iface = 0xdeadbeef;
    *(uint64_t *)s->identity = 0; /* a const pointee the host scribbles on must not come back */
}
static void call_cb(void *p) {
    struct cb_params *s = p;
    for (int i = 0; i < s->msg->cub; i++) ((uint8_t *)s->msg->pub)[i] = (uint8_t)(0xc0 + i);
    s->msg->id = 703;
}
static void call_none(void *p) { (void)p; }
static const char persona[] = "Persona";
static void call_retbuf(void *p) { struct retbuf_params *s = p; s->_ret.ptr = (uintptr_t)persona; s->_ret.len = sizeof persona; }
static void call_retbuf_null(void *p) { struct retbuf_params *s = p; s->_ret.ptr = 0; s->_ret.len = 0; }
/* As Steam does: a too-small (or NULL) buffer gets the size it needs in *size, and as much as fits. */
static void call_prop(void *p) {
    struct prop_params *s = p;
    static const char value[] = "hello, inventory"; /* 17 bytes with the NUL */
    if (s->buf) memcpy(s->buf, value, *s->size < sizeof value ? *s->size : sizeof value);
    s->_ret = s->buf && *s->size >= sizeof value;
    *s->size = sizeof value;
}

/* As Steam does: three items, as many as fit, and the count it has. */
static void call_items(void *p) {
    struct items_params *s = p;
    for (uint32_t i = 0; s->items && i < 3 && i < *s->count; i++) s->items[i] = (struct item){100 + i, 7, 1, 0};
    s->_ret = 1;
    *s->count = 3;
}

static int roundtrip(uint32_t index, void *params, void (*call)(void *), struct mn_reg_log *log) {
    struct mn_buf req = {0}, rep = {0};
    uint32_t status = 99;
    int r = mn_pack_request(&descs[index], index, params, &req);
    if (!r) r = host_roundtrip(&req, call, log, &rep);
    if (!r) r = mn_unpack_reply(&descs[index], rep.data, rep.len, params, &status, reg_apply, NULL);
    if (!r && status) r = -100;
    mn_buf_free(&req); mn_buf_free(&rep);
    return r;
}

static void items_cases(void) {
    { struct item it[4] = {{0}}; uint32_t count = 4; struct items_params p = {0x1111, 0, 5, it, &count};
      CHECK(roundtrip(7, &p, call_items, NULL) == 0 && count == 3 && it[2].id == 102 && it[3].id == 0 && p.items == it,
            "typed array behind a count pointer"); }
    { struct item it[3] = {{0}}; it[2].id = 0xfeed; uint32_t count = 2; struct items_params p = {0x1111, 0, 5, it, &count};
      CHECK(roundtrip(7, &p, call_items, NULL) == 0 && count == 3 && it[1].id == 101 && it[2].id == 0xfeed,
            "typed array: no more items back than the guest's count"); }
}

int main(void) {
    items_cases();
    { struct str_params p; p._ret = 0; p.name = "SteamClient017";
      CHECK(roundtrip(0, &p, call_str, NULL) == 0 && p._ret == 14, "string in"); }

    { uint32_t v = 0; struct out_params p; p.out = &v; p._ret = 0;
      CHECK(roundtrip(1, &p, call_out, NULL) == 0 && v == 0x12345678 && p._ret == 1 && p.out == &v, "uint32_t out"); }

    { uint8_t ticket[1024] = {0}; uint32_t cb = 0; uint64_t id = 77;
      struct ticket_params p; p.iface = 0x1111; p._ret = 0; p.pTicket = ticket; p.cbMaxTicket = sizeof ticket; p.pcbTicket = &cb; p.identity = &id;
      int r = roundtrip(2, &p, call_ticket, NULL);
      CHECK(r == 0 && cb == 234 && ticket[233] == 233 && ticket[234] == 0 && p._ret == 0xabc, "buffer and count out");
      CHECK(p.pTicket == ticket && p.pcbTicket == &cb && p.identity == &id, "caller's pointers kept");
      CHECK(id == 77, "const struct in not copied back");
      CHECK(p.iface == 0xdeadbeef, "opaque host value carried back"); }

    { uint8_t data[16]; struct cbmsg m; m.user = 1; m.id = 0; m.pub = data; m.cub = 16; struct cb_params p; p.cookie = 42; p.msg = &m;
      memset(data, 0, sizeof data);
      CHECK(roundtrip(3, &p, call_cb, NULL) == 0 && m.pub == data && data[15] == 0xcf && m.id == 703,
            "callback message with inner buffer"); }

    { struct out_params p; p.out = NULL; p._ret = 0;
      CHECK(roundtrip(1, &p, call_none, NULL) == 0 && p.out == NULL, "NULL out pointer"); }

    { void *x = NULL; struct bad_params p; p.pp = &x; struct mn_buf req = {0};
      CHECK(mn_pack_request(&descs[4], 4, &p, &req) == MN_E_UNSUPPORTED && req.len == 0, "unsupported refused");
      mn_buf_free(&req); }

    { struct mn_reg_log log = {0}; struct str_params p; p._ret = 0; p.name = "x";
      mn_reg_log_add(&log, 1, "Software\\Valve\\Steam", 20, "language", 8, "english", 8);
      reg_seen = 0;
      CHECK(roundtrip(0, &p, call_str, &log) == 0 && reg_seen, "registry write replayed");
      mn_buf_free(&log.entries); }

    { /* A returned u_buffer points into the host's memory: the guest gets its own copy, the same one for the same
       * bytes (Steam's strings live as long as the client, so copies are kept), and NULL stays NULL. */
      struct retbuf_params p = {0}, q = {0}, z = {0};
      int r = roundtrip(5, &p, call_retbuf, NULL);
      CHECK(r == 0 && p._ret.ptr && p._ret.ptr != (uintptr_t)persona && p._ret.len == sizeof persona &&
            !memcmp((const void *)(uintptr_t)p._ret.ptr, persona, sizeof persona), "returned buffer copied to the guest");
      CHECK(roundtrip(5, &q, call_retbuf, NULL) == 0 && q._ret.ptr == p._ret.ptr, "returned buffer kept once");
      z._ret.ptr = 1;
      CHECK(roundtrip(5, &z, call_retbuf_null, NULL) == 0 && !z._ret.ptr && !z._ret.len, "returned NULL buffer"); }

    { /* Every truncation of a valid request and reply fails cleanly (ASan catches any out-of-bounds read). */
      uint8_t ticket[300]; uint32_t cb = 0; uint64_t id = 77;
      struct ticket_params p; p.iface = 1; p._ret = 0; p.pTicket = ticket; p.cbMaxTicket = sizeof ticket; p.pcbTicket = &cb; p.identity = &id;
      struct mn_buf req = {0}, rep = {0}; int bad = 0;
      mn_pack_request(&descs[2], 2, &p, &req);
      for (size_t n = 0; n < req.len; n++) {
          uint8_t *copy = malloc(n ? n : 1); memcpy(copy, req.data, n);
          uint32_t index; void *hp; struct mn_arena arena = {0};
          if (mn_unpack_request(descs, NDESCS, copy, n, &index, &hp, &arena) == 0) bad++;
          mn_arena_free(&arena); free(copy);
      }
      CHECK(bad == 0, "truncated request refused");
      host_roundtrip(&req, call_ticket, NULL, &rep);
      for (size_t n = 0; n < rep.len; n++) {
          uint8_t *copy = malloc(n ? n : 1); memcpy(copy, rep.data, n); uint32_t st;
          if (mn_unpack_reply(&descs[2], copy, n, &p, &st, NULL, NULL) == 0) bad++;
          free(copy);
      }
      CHECK(bad == 0, "truncated reply refused");
      /* A reply whose buffer is longer than the caller's count is refused. */
      p.cbMaxTicket = 100;
      uint32_t st;
      CHECK(mn_unpack_reply(&descs[2], rep.data, rep.len, &p, &st, NULL, NULL) == MN_E_MISMATCH,
            "reply longer than the caller's buffer refused");
      mn_buf_free(&req); mn_buf_free(&rep); }

    { char buf[32]; uint32_t size = sizeof buf; memset(buf, 'x', sizeof buf);
      struct prop_params p = {0x1111, 0, 7, "name", buf, &size};
      CHECK(roundtrip(6, &p, call_prop, NULL) == 0 && p._ret == 1 && size == 17 && !strcmp(buf, "hello, inventory") &&
            buf[17] == 'x', "buffer counted through a pointer");
      CHECK(p.buf == buf && p.size == &size, "its pointers kept"); }

    { uint32_t size = 0; struct prop_params p = {0x1111, 0, 7, "name", NULL, &size};
      CHECK(roundtrip(6, &p, call_prop, NULL) == 0 && p._ret == 0 && size == 17, "NULL buffer: the size it needs"); }

    { /* The callee says it needs more than the buffer: only the buffer's bytes come back (ASan: no overread). */
      char buf[16]; uint32_t size = 8; memset(buf, 'x', sizeof buf);
      struct prop_params p = {0x1111, 0, 7, "name", buf, &size};
      CHECK(roundtrip(6, &p, call_prop, NULL) == 0 && p._ret == 0 && size == 17 && !memcmp(buf, "hello, i", 8) &&
            buf[8] == 'x', "a count grown past the buffer copies back the buffer alone"); }

    { /* A guest whose count says more than the bytes it sent is refused before the call. */
      char buf[8] = {0}; uint32_t size = 8; struct prop_params p = {0x1111, 0, 7, "name", buf, &size};
      struct mn_buf req = {0};
      CHECK(mn_pack_request(&descs[6], 6, &p, &req) == 0, "count-pointer request packed");
      uint32_t lie = 4096; memcpy(req.data + req.len - 4, &lie, 4); /* the count field's value, last in the request */
      uint32_t index; void *hp; struct mn_arena arena = {0};
      CHECK(mn_unpack_request(descs, NDESCS, req.data, req.len, &index, &hp, &arena) == MN_E_FORMAT,
            "count larger than the buffer refused");
      mn_arena_free(&arena); mn_buf_free(&req); }

    { /* A request naming an index past the table, or with the wrong magic, is refused. */
      struct str_params p; p._ret = 0; p.name = "x"; struct mn_buf req = {0};
      mn_pack_request(&descs[0], 0, &p, &req);
      ((struct mn_req_hdr *)req.data)->index = NDESCS;
      uint32_t index; void *hp; struct mn_arena arena = {0};
      CHECK(mn_unpack_request(descs, NDESCS, req.data, req.len, &index, &hp, &arena) == MN_E_FORMAT, "index out of range");
      ((struct mn_req_hdr *)req.data)->index = 0; ((struct mn_req_hdr *)req.data)->magic = 0;
      CHECK(mn_unpack_request(descs, NDESCS, req.data, req.len, &index, &hp, &arena) == MN_E_FORMAT, "bad magic");
      mn_arena_free(&arena); mn_buf_free(&req); }

    { /* The table checksum changes with any size or offset, so mismatched builds can refuse each other. */
      uint32_t a = mn_table_checksum(descs, NDESCS);
      struct mn_field changed[3]; memcpy(changed, ticket_fields, sizeof changed); changed[0].size_offset++;
      struct mn_call_desc d2[NDESCS]; memcpy(d2, descs, sizeof d2); d2[2].fields = changed;
      CHECK(a == mn_table_checksum(descs, NDESCS) && a != mn_table_checksum(d2, NDESCS), "table checksum"); }

    printf(failures ? "marshal-test: %d failure(s)\n" : "marshal-test: all passed\n", failures);
    return failures != 0;
}
