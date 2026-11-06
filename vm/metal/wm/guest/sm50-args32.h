/* SM50's compilation arguments as DXMT built for i386 lays them out (its PE side builds the chain; winemetal.so's WoW64
 * table converts it to the wire's chain before forwarding, wm/guest/wow64.c). Every argument starts with its 32-bit next
 * pointer and its type; its pointers are 32-bit, the rest as in airconv_public.h. */
#ifndef MN_WM_SM50_ARGS32_H
#define MN_WM_SM50_ARGS32_H
#include <stdbool.h>
#include <stdint.h>
#include "airconv_public.h"
#include "../sm50-wire.h"

struct mn_sm50_arg32 { uint32_t next; enum SM50_SHADER_COMPILATION_ARGUMENT_TYPE type; };
struct mn_sm50_stream_output32 {
    struct mn_sm50_arg32 h;
    uint32_t num_output_slots, num_elements, strides[4], elements;
};
struct mn_sm50_common32 { struct mn_sm50_arg32 h; enum SM50_SHADER_METAL_VERSION metal_version; enum SM50_SHADER_FLAG flags; };
struct mn_sm50_pixel32 {
    struct mn_sm50_arg32 h;
    uint32_t sample_mask;
    bool dual_source_blending, disable_depth_output;
    uint32_t unorm_output_reg_mask, pixel_formats[8];
};
struct mn_sm50_input_layout32 {
    struct mn_sm50_arg32 h;
    enum SM50_INDEX_BUFFER_FORMAT index_buffer_format;
    uint32_t slot_mask, num_elements, elements;
};
struct mn_sm50_gs_pass_through32 {
    struct mn_sm50_arg32 h;
    union { struct MTL_GEOMETRY_SHADER_PASS_THROUGH Data; uint32_t DataEncoded; };
    bool RasterizationDisabled;
};
struct mn_sm50_geometry32 { struct mn_sm50_arg32 h; bool strip_topology; };
struct mn_sm50_tessellator32 { struct mn_sm50_arg32 h; uint32_t max_potential_tess_factor; };
struct mn_sm50_root_signature32 { struct mn_sm50_arg32 h; uint32_t bytecode, bytecode_length; };
/* DXMT's i386 layouts (its airconv_public.h's *_DATA built for i686). */
#include <stddef.h>
_Static_assert(sizeof(struct mn_sm50_stream_output32) == 36 && offsetof(struct mn_sm50_stream_output32, elements) == 32,
               "stream output");
_Static_assert(sizeof(struct mn_sm50_common32) == 16, "common");
_Static_assert(sizeof(struct mn_sm50_pixel32) == 52 && offsetof(struct mn_sm50_pixel32, disable_depth_output) == 13 &&
               offsetof(struct mn_sm50_pixel32, unorm_output_reg_mask) == 16, "pixel shader");
_Static_assert(sizeof(struct mn_sm50_input_layout32) == 24 && offsetof(struct mn_sm50_input_layout32, elements) == 20,
               "input layout");
_Static_assert(sizeof(struct mn_sm50_gs_pass_through32) == 16 &&
               offsetof(struct mn_sm50_gs_pass_through32, RasterizationDisabled) == 12, "geometry pass-through");
_Static_assert(sizeof(struct mn_sm50_geometry32) == 12 && sizeof(struct mn_sm50_tessellator32) == 12 &&
               sizeof(struct mn_sm50_root_signature32) == 16, "geometry, tessellator, root signature");

/* The wire's chain (sm50-wire.h) for the 32-bit one at args32 (0: none, *out NULL), malloc'd; 0, or -1 when out of
 * memory or longer than MN_WM_SM50_ARGS_MAX arguments (a looping chain). Types it doesn't know, and
 * SM50_SHADER_ARGUMENT_TYPE_MAX, are left out, as airconv skips them; they count towards the bound. */
#define MN_WM_SM50_ARGS_MAX 64 /* the marshaller's own bound (wm/slots.c's MAX_ARGS) */
int mn_wm_sm50_args32(uint32_t args32, struct mn_sm50_arg **out);
void mn_wm_sm50_args_free(struct mn_sm50_arg *first);
#endif
