/* SM50's compilation arguments as DXMT built for i386 lays them out (its PE side builds the chain; winemetal.so's WoW64
 * table converts it to airconv's 64-bit chain before forwarding, wm/guest/wow64.c). Every argument starts with its
 * 32-bit next pointer and its type; its pointers are 32-bit, the rest as in airconv_public.h. */
#ifndef MN_WM_SM50_ARGS32_H
#define MN_WM_SM50_ARGS32_H
#include <stdbool.h>
#include <stdint.h>
#include "airconv_public.h"

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

/* The 64-bit chain for the 32-bit one at args32 (0: none, *out NULL), malloc'd; 0, or -1 when out of memory. Types it
 * doesn't know, and SM50_SHADER_ARGUMENT_TYPE_MAX, are left out, as airconv skips them. */
int mn_wm_sm50_args32(uint32_t args32, struct SM50_SHADER_COMPILATION_ARGUMENT_DATA **out);
void mn_wm_sm50_args_free(struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *first);
#endif
