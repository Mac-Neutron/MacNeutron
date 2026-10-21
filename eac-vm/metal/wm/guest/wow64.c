/* winemetal.so's WoW64 table (M4a of the 32-bit games' spec, 2026-10-20): DXMT built for i386 calls through it. DXMT's
 * params are the same for both widths (its WMTMemoryPointer is 8 bytes, its i386 code zeroes the upper half), so each
 * entry is the 64-bit one, after marking the process 32-bit (galloc.h: its memory then goes below 2 GiB), except:
 * SM50's calls whose i386 params hold 32-bit pointers or an argument chain (airconv_thunks.h's *_params32,
 * sm50-args32.h), converted here (slot 83's too, though both of DXMT's tables leave it NULL and the 64-bit table here
 * refuses it, so a DXMT that adds it gets converted params); and Direct3D forwarding
 * (MN_WM_D3D), whose 32-bit calls eac-vm/d3d/guest/unix.c converts (M4b). */
#include <stdint.h>
#include "airconv_thunks.h"
#include "calls.h"
#include "marshal.h"
#include "galloc.h"
#include "sm50-args32.h"

typedef NTSTATUS_T (*unixlib_entry_t)(void *params);
extern const unixlib_entry_t __wine_unix_call_funcs[];

static void *p64(uint32_t v) { return (void *)(uintptr_t)v; }
static NTSTATUS_T call64(unsigned slot, void *p) {
    if (!mn_wm_wow64) mn_wm_wow64 = 1; /* once: a store a call would share galloc's hot cache line */
    return __wine_unix_call_funcs[slot](p);
}

static NTSTATUS_T sm50_initialize(void *args) {
    struct sm50_initialize_params32 *a = args;
    struct sm50_initialize_params p = {p64(a->bytecode), a->bytecode_size, p64(a->shader), p64(a->reflection),
                                       p64(a->error), 0};
    NTSTATUS_T s = call64(unix_sm50_initialize, &p);
    a->ret = p.ret;
    return s;
}

/* The compiles: the argument chain converted for the call, then freed. */
#define COMPILE(slot, st, args_field, ...)                                                                             \
    static NTSTATUS_T st(void *args) {                                                                                 \
        struct st##_params32 *a = args;                                                                                \
        struct st##_params p = {__VA_ARGS__, .func_name = p64(a->func_name), .bitcode = p64(a->bitcode),            \
                                .error = p64(a->error)};                                                               \
        struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *chain;                                                           \
        if (mn_wm_sm50_args32(a->args_field, &chain)) return MN_STATUS_NO_MEMORY;                                      \
        p.args_field = chain;                                                                                          \
        NTSTATUS_T s = call64(slot, &p);                                                                               \
        a->ret = p.ret;                                                                                                \
        mn_wm_sm50_args_free(chain);                                                                                   \
        return s;                                                                                                      \
    }
COMPILE(unix_sm50_compile, sm50_compile, args, .shader = a->shader)
COMPILE(unix_sm50_compile_geometry_pipeline_vertex, sm50_compile_geometry_pipeline_vertex, vertex_args,
        .vertex = a->vertex, .geometry = a->geometry)
COMPILE(unix_sm50_compile_geometry_pipeline_geometry, sm50_compile_geometry_pipeline_geometry, geometry_args,
        .vertex = a->vertex, .geometry = a->geometry)
COMPILE(unix_sm50_compile_tessellation_vertex, sm50_compile_tessellation_pipeline_vertex, vertex_args,
        .vertex = a->vertex, .hull = a->hull)
COMPILE(unix_sm50_compile_tessellation_hull, sm50_compile_tessellation_pipeline_hull, hull_args,
        .vertex = a->vertex, .hull = a->hull)
COMPILE(unix_sm50_compile_tessellation_domain, sm50_compile_tessellation_pipeline_domain, domain_args,
        .hull = a->hull, .domain = a->domain)

static NTSTATUS_T sm50_get_compiled_bitcode(void *args) {
    struct sm50_get_compiled_bitcode_params32 *a = args;
    struct sm50_get_compiled_bitcode_params p = {a->bitcode, p64(a->data_out)};
    return call64(unix_sm50_get_compiled_bitcode, &p);
}

static NTSTATUS_T sm50_get_error_message(void *args) {
    struct sm50_get_error_message_params32 *a = args;
    struct sm50_get_error_message_params p = {a->error, p64(a->buffer), a->buffer_size, 0};
    NTSTATUS_T s = call64(unix_sm50_get_error_message, &p);
    a->ret_size = (uint32_t)p.ret_size;
    return s;
}

static NTSTATUS_T sm50_get_arguments_info(void *args) {
    struct sm50_get_arguments_info_params32 *a = args;
    struct sm50_get_arguments_info_params p = {a->shader, p64(a->constant_buffers), p64(a->arguments)};
    return call64(unix_sm50_get_arguments_info, &p);
}

/* Direct3D forwarding's 32-bit calls (M4b): eac-vm/d3d/guest/unix.c converts their data; a build without it (the
 * tests') refuses. */
__attribute__((weak)) int32_t mn_d3d_unix_call32(void *args) {
    (void)args;
    return (int32_t)MN_STATUS_NOT_IMPLEMENTED;
}
static NTSTATUS_T d3d32(void *args) {
    if (!mn_wm_wow64) mn_wm_wow64 = 1;
    return (NTSTATUS_T)mn_d3d_unix_call32(args);
}

#define W(n) __attribute__((unused)) static NTSTATUS_T w##n(void *p) { return call64(n, p); }
#define W10(t) W(t##0) W(t##1) W(t##2) W(t##3) W(t##4) W(t##5) W(t##6) W(t##7) W(t##8) W(t##9)
#define S10(t) w##t##0, w##t##1, w##t##2, w##t##3, w##t##4, w##t##5, w##t##6, w##t##7, w##t##8, w##t##9,
W(0) W(1) W(2) W(3) W(4) W(5) W(6) W(7) W(8) W(9)
W10(1) W10(2) W10(3) W10(4) W10(5) W10(6) W10(7) W10(8) W10(9) W10(10) W10(11) W10(12) W10(13) W10(14)
W(150) W(151) W(152) W(153) W(154) W(156) W(157)

const unixlib_entry_t __wine_unix_call_wow64_funcs[] = {
    w0, w1, w2, w3, w4, w5, w6, w7, w8, w9,
    S10(1) S10(2) S10(3) S10(4) S10(5) S10(6)
    w70, w71, w72, w73, sm50_initialize, w75, sm50_compile, sm50_get_compiled_bitcode, w78, sm50_get_error_message,
    w80, sm50_compile_geometry_pipeline_vertex, sm50_compile_geometry_pipeline_geometry,
    sm50_compile_tessellation_pipeline_vertex, sm50_compile_tessellation_pipeline_hull,
    sm50_compile_tessellation_pipeline_domain, w86, w87, sm50_get_arguments_info, w89,
    S10(9) S10(10) S10(11) S10(12) S10(13) S10(14)
    w150, w151, w152, w153, w154, d3d32, w156, w157,
};
_Static_assert(sizeof __wine_unix_call_wow64_funcs / sizeof *__wine_unix_call_wow64_funcs == MN_WM_SLOTS,
               "an entry per slot");
