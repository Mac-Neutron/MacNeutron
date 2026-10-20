/* M4a: a 32-bit caller's SM50 compilation arguments (DXMT for i386) convert field for field to airconv's 64-bit chain,
 * for every argument type, in their order (wm/guest/sm50-args32.c). The chain sits below 4 GiB, as a 32-bit process's
 * memory does. Prints a line per check; exits 1 on a failure. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "guest/sm50-args32.h"

static int failures;
#define CHECK(c, what) do { printf("%s %s\n", (c) ? "ok  " : "FAIL", what); failures += !(c); } while (0)
static uint8_t *low;
static size_t at;
static void *put(const void *p, size_t n) { void *d = low + at; memcpy(d, p, n); at += (n + 15) & ~(size_t)15; return d; }
static uint32_t lo(const void *p) { return (uint32_t)(uintptr_t)p; }

int main(void) {
    low = mmap((void *)0x10000000, 1 << 16, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (low != (void *)0x10000000) { printf("FAIL no memory below 4 GiB\n"); return 1; }
    uint32_t elems = lo(put("elements", 9)), bytecode = lo(put("bytecode", 9));
    struct mn_sm50_root_signature32 rs2 = {{0, SM50_SHADER_ROOT_SIGNATURE2}, bytecode, 77};
    struct mn_sm50_root_signature32 rs = {{lo(put(&rs2, sizeof rs2)), SM50_SHADER_ROOT_SIGNATURE}, bytecode, 9};
    struct mn_sm50_tessellator32 te = {{lo(put(&rs, sizeof rs)), SM50_SHADER_PSO_TESSELLATOR}, 64};
    struct mn_sm50_geometry32 ge = {{lo(put(&te, sizeof te)), SM50_SHADER_PSO_GEOMETRY_SHADER}, true};
    struct mn_sm50_gs_pass_through32 gs = {{lo(put(&ge, sizeof ge)), SM50_SHADER_GS_PASS_THROUGH}, .DataEncoded = 0x12345678,
                                           .RasterizationDisabled = true};
    struct mn_sm50_input_layout32 ia = {{lo(put(&gs, sizeof gs)), SM50_SHADER_IA_INPUT_LAYOUT}, SM50_INDEX_BUFFER_FORMAT_UINT32,
                                        0x5, 3, elems};
    struct mn_sm50_arg32 max = {lo(put(&ia, sizeof ia)), SM50_SHADER_ARGUMENT_TYPE_MAX};
    struct mn_sm50_pixel32 ps = {{lo(put(&max, sizeof max)), SM50_SHADER_PSO_PIXEL_SHADER}, 0xff, false, true, 0x3,
                                 {1, 2, 3, 4, 5, 6, 7, 8}};
    struct mn_sm50_common32 co = {{lo(put(&ps, sizeof ps)), SM50_SHADER_COMMON}, SM50_SHADER_METAL_320, (enum SM50_SHADER_FLAG)6};
    struct mn_sm50_stream_output32 so = {{lo(put(&co, sizeof co)), SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT}, 2, 4,
                                         {16, 32, 0, 0}, elems};
    uint32_t first = lo(put(&so, sizeof so));

    struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *c = NULL, *a;
    CHECK(mn_wm_sm50_args32(first, &c) == 0 && c, "a chain converts");
    a = c;
    struct SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA *o = (void *)a;
    CHECK(a && a->type == SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT && o->num_output_slots == 2 && o->num_elements == 4 &&
          o->strides[1] == 32 && (uintptr_t)o->elements == elems, "stream output");
    a = a ? a->next : NULL;
    struct SM50_SHADER_COMMON_DATA *m = (void *)a;
    CHECK(a && a->type == SM50_SHADER_COMMON && m->metal_version == SM50_SHADER_METAL_320 && m->flags == 6, "common");
    a = a ? a->next : NULL;
    struct SM50_SHADER_PSO_PIXEL_SHADER_DATA *p = (void *)a;
    CHECK(a && a->type == SM50_SHADER_PSO_PIXEL_SHADER && p->sample_mask == 0xff && !p->dual_source_blending &&
          p->disable_depth_output && p->unorm_output_reg_mask == 3 && p->pixel_formats[7] == 8, "pixel shader");
    a = a ? a->next : NULL; /* SM50_SHADER_ARGUMENT_TYPE_MAX is left out */
    struct SM50_SHADER_IA_INPUT_LAYOUT_DATA *i = (void *)a;
    CHECK(a && a->type == SM50_SHADER_IA_INPUT_LAYOUT && i->index_buffer_format == SM50_INDEX_BUFFER_FORMAT_UINT32 &&
          i->slot_mask == 5 && i->num_elements == 3 && (uintptr_t)i->elements == elems, "input layout (type MAX left out)");
    a = a ? a->next : NULL;
    struct SM50_SHADER_GS_PASS_THROUGH_DATA *g = (void *)a;
    CHECK(a && a->type == SM50_SHADER_GS_PASS_THROUGH && g->DataEncoded == 0x12345678 && g->RasterizationDisabled,
          "geometry pass-through");
    a = a ? a->next : NULL;
    CHECK(a && a->type == SM50_SHADER_PSO_GEOMETRY_SHADER && ((struct SM50_SHADER_PSO_GEOMETRY_SHADER_DATA *)a)->strip_topology,
          "geometry shader");
    a = a ? a->next : NULL;
    CHECK(a && a->type == SM50_SHADER_PSO_TESSELLATOR &&
          ((struct SM50_SHADER_PSO_TESSELLATOR_DATA *)a)->max_potential_tess_factor == 64, "tessellator");
    a = a ? a->next : NULL;
    struct SM50_SHADER_ROOT_SIGNATURE_DATA *r = (void *)a;
    CHECK(a && a->type == SM50_SHADER_ROOT_SIGNATURE && (uintptr_t)r->bytecode == bytecode && r->bytecode_length == 9,
          "root signature");
    a = a ? a->next : NULL;
    r = (void *)a;
    CHECK(a && a->type == SM50_SHADER_ROOT_SIGNATURE2 && r->bytecode_length == 77 && !a->next, "root signature 2, last");
    mn_wm_sm50_args_free(c);
    c = (void *)1;
    CHECK(mn_wm_sm50_args32(0, &c) == 0 && !c, "no chain");
    /* A looping chain, of a type left out and of one converted: refused past the 64-bit marshaller's bound. */
    struct mn_sm50_arg32 *loop = put(&max, sizeof max);
    loop->next = lo(loop);
    c = (void *)1;
    CHECK(mn_wm_sm50_args32(lo(loop), &c) == -1 && !c, "a looping chain of unknown types is refused");
    struct mn_sm50_tessellator32 *loop2 = put(&te, sizeof te);
    loop2->h.next = lo(loop2);
    c = (void *)1;
    CHECK(mn_wm_sm50_args32(lo(loop2), &c) == -1 && !c, "a looping chain of arguments is refused");
    printf(failures ? "sm50-args32-test: %d failure(s)\n" : "sm50-args32-test: ok\n", failures);
    return failures != 0;
}
