/* A 32-bit caller's SM50 compilation arguments, converted to airconv's 64-bit chain (sm50-args32.h). */
#include "sm50-args32.h"
#include <stdlib.h>
#include <string.h>

static void *p64(uint32_t v) { return (void *)(uintptr_t)v; }

void mn_wm_sm50_args_free(struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *first) {
    while (first) {
        struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *next = first->next;
        free(first);
        first = next;
    }
}

/* One argument's 64-bit copy (its next and type set by the caller), or NULL; *known is 0 for a type left out. */
static void *convert(const struct mn_sm50_arg32 *a, int *known) {
    *known = 1;
    switch (a->type) {
    case SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT: {
        const struct mn_sm50_stream_output32 *s = (const void *)a;
        struct SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA *d = calloc(1, sizeof *d);
        if (d) {
            d->num_output_slots = s->num_output_slots, d->num_elements = s->num_elements;
            memcpy(d->strides, s->strides, sizeof d->strides);
            d->elements = p64(s->elements);
        }
        return d;
    }
    case SM50_SHADER_COMMON: {
        const struct mn_sm50_common32 *s = (const void *)a;
        struct SM50_SHADER_COMMON_DATA *d = calloc(1, sizeof *d);
        if (d) d->metal_version = s->metal_version, d->flags = s->flags;
        return d;
    }
    case SM50_SHADER_PSO_PIXEL_SHADER: {
        const struct mn_sm50_pixel32 *s = (const void *)a;
        struct SM50_SHADER_PSO_PIXEL_SHADER_DATA *d = calloc(1, sizeof *d);
        if (d) {
            d->sample_mask = s->sample_mask, d->dual_source_blending = s->dual_source_blending;
            d->disable_depth_output = s->disable_depth_output, d->unorm_output_reg_mask = s->unorm_output_reg_mask;
            memcpy(d->pixel_formats, s->pixel_formats, sizeof d->pixel_formats);
        }
        return d;
    }
    case SM50_SHADER_IA_INPUT_LAYOUT: {
        const struct mn_sm50_input_layout32 *s = (const void *)a;
        struct SM50_SHADER_IA_INPUT_LAYOUT_DATA *d = calloc(1, sizeof *d);
        if (d) {
            d->index_buffer_format = s->index_buffer_format, d->slot_mask = s->slot_mask;
            d->num_elements = s->num_elements, d->elements = p64(s->elements);
        }
        return d;
    }
    case SM50_SHADER_GS_PASS_THROUGH: {
        const struct mn_sm50_gs_pass_through32 *s = (const void *)a;
        struct SM50_SHADER_GS_PASS_THROUGH_DATA *d = calloc(1, sizeof *d);
        if (d) d->DataEncoded = s->DataEncoded, d->RasterizationDisabled = s->RasterizationDisabled;
        return d;
    }
    case SM50_SHADER_PSO_GEOMETRY_SHADER: {
        const struct mn_sm50_geometry32 *s = (const void *)a;
        struct SM50_SHADER_PSO_GEOMETRY_SHADER_DATA *d = calloc(1, sizeof *d);
        if (d) d->strip_topology = s->strip_topology;
        return d;
    }
    case SM50_SHADER_PSO_TESSELLATOR: {
        const struct mn_sm50_tessellator32 *s = (const void *)a;
        struct SM50_SHADER_PSO_TESSELLATOR_DATA *d = calloc(1, sizeof *d);
        if (d) d->max_potential_tess_factor = s->max_potential_tess_factor;
        return d;
    }
    case SM50_SHADER_ROOT_SIGNATURE:
    case SM50_SHADER_ROOT_SIGNATURE2: {
        const struct mn_sm50_root_signature32 *s = (const void *)a;
        struct SM50_SHADER_ROOT_SIGNATURE_DATA *d = calloc(1, sizeof *d);
        if (d) d->bytecode = p64(s->bytecode), d->bytecode_length = s->bytecode_length;
        return d;
    }
    default:
        *known = 0;
        return NULL;
    }
}

int mn_wm_sm50_args32(uint32_t args32, struct SM50_SHADER_COMPILATION_ARGUMENT_DATA **out) {
    struct SM50_SHADER_COMPILATION_ARGUMENT_DATA head = {0}, *last = &head;
    int n = 0;
    for (const struct mn_sm50_arg32 *a = p64(args32); a; a = p64(a->next)) {
        int known;
        if (++n > MN_WM_SM50_ARGS_MAX) {
            mn_wm_sm50_args_free(head.next);
            *out = NULL;
            return -1;
        }
        struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *d = convert(a, &known);
        if (!known) continue;
        if (!d) {
            mn_wm_sm50_args_free(head.next);
            *out = NULL;
            return -1;
        }
        d->type = a->type;
        last->next = d;
        last = d;
    }
    *out = head.next;
    return 0;
}
