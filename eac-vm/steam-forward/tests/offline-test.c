/* The forwarder with no host (the VM started without --steam-appid): steamclient_init and steamclient_init_registry
 * succeed and CreateInterface finds no interface, as lsteamclient answers when Steam isn't running, so SteamAPI_Init
 * returns false instead of lsteamclient's PE side asserting on a failed call. Any other call still reports the
 * disconnection. */
#include <stdio.h>
#include <string.h>
#include "client.h"
#include "unixlib.h"
#include "wine/unixlib.h"

extern const unixlib_entry_t __wine_unix_call_wow64_funcs[];

static int no_host(void) { return -1; }
static unsigned call_index(const char *name) {
    for (unsigned i = 0; i < mn_ncalls; i++)
        if (!strcmp(mn_calls[i].name, name)) return i;
    return ~0u;
}

int main(void) {
    int fail = 0;
    mn_client_init(no_host, NULL, NULL);
    char tmp[1] = "";
    struct steamclient_init_params init = {.g_tmppath = tmp};
    uint32_t s = mn_client_call(call_index("steamclient_init"), &init);
    if (s) { printf("FAIL steamclient_init: 0x%x, want 0\n", s); fail = 1; }
    int code = 0;
    struct steamclient_CreateInterface_params ci = {.name = "SteamClient017", .return_code = &code};
    ci._ret.handle = 1;
    s = mn_client_call(call_index("steamclient_CreateInterface"), &ci);
    if (s || ci._ret.handle) { printf("FAIL CreateInterface: 0x%x, handle %llx, want 0 and 0\n", s, (unsigned long long)ci._ret.handle); fail = 1; }
    s = mn_client_call(call_index("steamclient_init_registry"), NULL);
    if (s) { printf("FAIL steamclient_init_registry: 0x%x, want 0\n", s); fail = 1; }
    struct steamclient_next_callback_params next = {0};
    s = mn_client_call(call_index("steamclient_next_callback"), &next);
    if (s != MN_STATUS_DISCONNECTED) { printf("FAIL next_callback: 0x%x, want 0x%x\n", s, MN_STATUS_DISCONNECTED); fail = 1; }
    /* A 32-bit (WoW64) caller's calls are forwarded too, through their own descriptors: with no host, init and
     * CreateInterface answer as the 64-bit ones do. */
    struct wow64_steamclient_init_params init32 = {0};
    s = __wine_unix_call_wow64_funcs[call_index("steamclient_init")](&init32);
    if (s) { printf("FAIL wow64 init: 0x%x, want 0\n", s); fail = 1; }
    struct wow64_steamclient_CreateInterface_params ci32 = {0};
    ci32._ret.handle = 1;
    s = __wine_unix_call_wow64_funcs[call_index("steamclient_CreateInterface")](&ci32);
    if (s || ci32._ret.handle) { printf("FAIL wow64 CreateInterface: 0x%x, handle %llx, want 0 and 0\n", s, (unsigned long long)ci32._ret.handle); fail = 1; }
    const struct mn_call_desc *d = &mn_calls_wow64[call_index("steamclient_init")];
    int ptr32 = d->params_size == sizeof(struct wow64_steamclient_init_params) && d->nfields == 2;
    for (unsigned i = 0; i < d->nfields; i++) ptr32 &= (d->fields[i].flags & MN_PTR32) != 0;
    if (!ptr32) { printf("FAIL wow64 init's descriptor: size %u, %u fields, want 4-byte pointers\n", d->params_size, d->nfields); fail = 1; }
    if (!fail) printf("offline-test: ok\n");
    return fail;
}
