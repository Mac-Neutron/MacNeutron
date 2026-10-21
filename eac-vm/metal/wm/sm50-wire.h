/* SM50's params and compilation arguments as the transport carries them (slots.c): airconv's 64-bit layouts
 * (airconv_thunks.h, airconv_public.h), each pointer a uint64_t, so that a guest of either width packs the same
 * records. An x86-64 caller's params are these; a 32-bit caller's (new WoW64's, and an i686 winemetal.so's under old
 * WoW64) are converted to them (guest/wow64.c, guest/sm50-args32.c). An i686 build needs -malign-double, as DXMT's i386
 * PE side lays out a uint64_t (eac-vm/metal/layout32-check.sh). */
#ifndef MN_WM_SM50_WIRE_H
#define MN_WM_SM50_WIRE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "airconv_thunks.h"

struct mn_sm50_initialize { uint64_t bytecode, bytecode_size, shader, reflection, error; int32_t ret; };
struct mn_sm50_compile { uint64_t shader, args, func_name, bitcode, error; int32_t ret; };
struct mn_sm50_compile2 { uint64_t shaders[2], args, func_name, bitcode, error; int32_t ret; }; /* 81-85 */
struct mn_sm50_get_compiled_bitcode { uint64_t bitcode, data_out; };
struct mn_sm50_get_error_message { uint64_t error, buffer, buffer_size, ret_size; };
struct mn_sm50_get_arguments_info { uint64_t shader, constant_buffers, arguments; };

/* The arguments: a list through next, each node's type at offset 8. */
#define MN_SM50_ARG uint64_t next; enum SM50_SHADER_COMPILATION_ARGUMENT_TYPE type
struct mn_sm50_arg { MN_SM50_ARG; };
struct mn_sm50_stream_output { MN_SM50_ARG; uint32_t num_output_slots, num_elements, strides[4]; uint64_t elements; };
struct mn_sm50_common { MN_SM50_ARG; enum SM50_SHADER_METAL_VERSION metal_version; enum SM50_SHADER_FLAG flags; };
struct mn_sm50_pixel {
    MN_SM50_ARG;
    uint32_t sample_mask;
    bool dual_source_blending, disable_depth_output;
    uint32_t unorm_output_reg_mask, pixel_formats[8];
};
struct mn_sm50_input_layout {
    MN_SM50_ARG;
    enum SM50_INDEX_BUFFER_FORMAT index_buffer_format;
    uint32_t slot_mask, num_elements;
    uint64_t elements;
};
struct mn_sm50_gs_pass_through { MN_SM50_ARG; uint32_t DataEncoded; bool RasterizationDisabled; };
struct mn_sm50_geometry { MN_SM50_ARG; bool strip_topology; };
struct mn_sm50_tessellator { MN_SM50_ARG; uint32_t max_potential_tess_factor; };
struct mn_sm50_root_signature { MN_SM50_ARG; uint64_t bytecode, bytecode_length; };

#if defined(__i386__)
_Static_assert(offsetof(struct mn_sm50_arg, type) == 8 && sizeof(struct mn_sm50_arg) == 16,
               "an i686 build needs -malign-double");
#elif defined(__LP64__)
/* The same layouts as airconv's. */
#define MN_SAME(w, a, f) _Static_assert(offsetof(struct w, f) == offsetof(struct a, f), #w "." #f);
#define MN_SAME_SIZE(w, a) _Static_assert(sizeof(struct w) == sizeof(struct a), #w);
MN_SAME_SIZE(mn_sm50_initialize, sm50_initialize_params)
MN_SAME(mn_sm50_initialize, sm50_initialize_params, bytecode_size)
MN_SAME(mn_sm50_initialize, sm50_initialize_params, shader)
MN_SAME(mn_sm50_initialize, sm50_initialize_params, reflection)
MN_SAME(mn_sm50_initialize, sm50_initialize_params, error)
MN_SAME(mn_sm50_initialize, sm50_initialize_params, ret)
MN_SAME_SIZE(mn_sm50_compile, sm50_compile_params)
MN_SAME(mn_sm50_compile, sm50_compile_params, args)
MN_SAME(mn_sm50_compile, sm50_compile_params, func_name)
MN_SAME(mn_sm50_compile, sm50_compile_params, bitcode)
MN_SAME(mn_sm50_compile, sm50_compile_params, error)
MN_SAME(mn_sm50_compile, sm50_compile_params, ret)
#define MN_SAME2(a, args_field)                                                                                        \
    MN_SAME_SIZE(mn_sm50_compile2, a)                                                                                  \
    _Static_assert(offsetof(struct mn_sm50_compile2, args) == offsetof(struct a, args_field), #a);                    \
    MN_SAME(mn_sm50_compile2, a, func_name) MN_SAME(mn_sm50_compile2, a, bitcode) MN_SAME(mn_sm50_compile2, a, error)  \
    MN_SAME(mn_sm50_compile2, a, ret)
MN_SAME2(sm50_compile_geometry_pipeline_vertex_params, vertex_args)
MN_SAME2(sm50_compile_geometry_pipeline_geometry_params, geometry_args)
MN_SAME2(sm50_compile_tessellation_pipeline_vertex_params, vertex_args)
MN_SAME2(sm50_compile_tessellation_pipeline_hull_params, hull_args)
MN_SAME2(sm50_compile_tessellation_pipeline_domain_params, domain_args)
MN_SAME_SIZE(mn_sm50_get_compiled_bitcode, sm50_get_compiled_bitcode_params)
MN_SAME(mn_sm50_get_compiled_bitcode, sm50_get_compiled_bitcode_params, data_out)
MN_SAME_SIZE(mn_sm50_get_error_message, sm50_get_error_message_params)
MN_SAME(mn_sm50_get_error_message, sm50_get_error_message_params, buffer)
MN_SAME(mn_sm50_get_error_message, sm50_get_error_message_params, buffer_size)
MN_SAME(mn_sm50_get_error_message, sm50_get_error_message_params, ret_size)
MN_SAME_SIZE(mn_sm50_get_arguments_info, sm50_get_arguments_info_params)
MN_SAME(mn_sm50_get_arguments_info, sm50_get_arguments_info_params, constant_buffers)
MN_SAME(mn_sm50_get_arguments_info, sm50_get_arguments_info_params, arguments)
MN_SAME_SIZE(mn_sm50_arg, SM50_SHADER_COMPILATION_ARGUMENT_DATA)
MN_SAME(mn_sm50_arg, SM50_SHADER_COMPILATION_ARGUMENT_DATA, type)
MN_SAME_SIZE(mn_sm50_stream_output, SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA)
MN_SAME(mn_sm50_stream_output, SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA, num_output_slots)
MN_SAME(mn_sm50_stream_output, SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA, num_elements)
MN_SAME(mn_sm50_stream_output, SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA, strides)
MN_SAME(mn_sm50_stream_output, SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA, elements)
MN_SAME_SIZE(mn_sm50_common, SM50_SHADER_COMMON_DATA)
MN_SAME(mn_sm50_common, SM50_SHADER_COMMON_DATA, metal_version)
MN_SAME(mn_sm50_common, SM50_SHADER_COMMON_DATA, flags)
MN_SAME_SIZE(mn_sm50_pixel, SM50_SHADER_PSO_PIXEL_SHADER_DATA)
MN_SAME(mn_sm50_pixel, SM50_SHADER_PSO_PIXEL_SHADER_DATA, sample_mask)
MN_SAME(mn_sm50_pixel, SM50_SHADER_PSO_PIXEL_SHADER_DATA, dual_source_blending)
MN_SAME(mn_sm50_pixel, SM50_SHADER_PSO_PIXEL_SHADER_DATA, disable_depth_output)
MN_SAME(mn_sm50_pixel, SM50_SHADER_PSO_PIXEL_SHADER_DATA, unorm_output_reg_mask)
MN_SAME(mn_sm50_pixel, SM50_SHADER_PSO_PIXEL_SHADER_DATA, pixel_formats)
MN_SAME_SIZE(mn_sm50_input_layout, SM50_SHADER_IA_INPUT_LAYOUT_DATA)
MN_SAME(mn_sm50_input_layout, SM50_SHADER_IA_INPUT_LAYOUT_DATA, index_buffer_format)
MN_SAME(mn_sm50_input_layout, SM50_SHADER_IA_INPUT_LAYOUT_DATA, slot_mask)
MN_SAME(mn_sm50_input_layout, SM50_SHADER_IA_INPUT_LAYOUT_DATA, num_elements)
MN_SAME(mn_sm50_input_layout, SM50_SHADER_IA_INPUT_LAYOUT_DATA, elements)
MN_SAME_SIZE(mn_sm50_gs_pass_through, SM50_SHADER_GS_PASS_THROUGH_DATA)
MN_SAME(mn_sm50_gs_pass_through, SM50_SHADER_GS_PASS_THROUGH_DATA, DataEncoded)
MN_SAME(mn_sm50_gs_pass_through, SM50_SHADER_GS_PASS_THROUGH_DATA, RasterizationDisabled)
MN_SAME_SIZE(mn_sm50_geometry, SM50_SHADER_PSO_GEOMETRY_SHADER_DATA)
MN_SAME(mn_sm50_geometry, SM50_SHADER_PSO_GEOMETRY_SHADER_DATA, strip_topology)
MN_SAME_SIZE(mn_sm50_tessellator, SM50_SHADER_PSO_TESSELLATOR_DATA)
MN_SAME(mn_sm50_tessellator, SM50_SHADER_PSO_TESSELLATOR_DATA, max_potential_tess_factor)
MN_SAME_SIZE(mn_sm50_root_signature, SM50_SHADER_ROOT_SIGNATURE_DATA)
MN_SAME(mn_sm50_root_signature, SM50_SHADER_ROOT_SIGNATURE_DATA, bytecode)
MN_SAME(mn_sm50_root_signature, SM50_SHADER_ROOT_SIGNATURE_DATA, bytecode_length)
#undef MN_SAME
#undef MN_SAME_SIZE
#undef MN_SAME2
#endif
#endif
