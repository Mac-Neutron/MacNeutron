/* The command-node tables (cmdlist.h), from DXMT's own structs: the call catalog's §1. */
#include "cmdlist.h"
#include <stddef.h>
#include <string.h>
#include "marshal.h"
#include "winemetal_thunks.h"

#define PAD8(n) (((n) + 7u) & ~(uint64_t)7)

#define N(st) {.size = sizeof(struct st)}
#define N1(st, a) {.size = sizeof(struct st), .nhandles = 1, .handles = {offsetof(struct st, a)}}
#define N2(st, a, b) {.size = sizeof(struct st), .nhandles = 2, .handles = {offsetof(struct st, a), offsetof(struct st, b)}}
#define N3(st, a, b, c)                                                                                                \
    {.size = sizeof(struct st), .nhandles = 3,                                                                         \
     .handles = {offsetof(struct st, a), offsetof(struct st, b), offsetof(struct st, c)}}
#define N4(st, a, b, c, d)                                                                                             \
    {.size = sizeof(struct st), .nhandles = 4,                                                                         \
     .handles = {offsetof(struct st, a), offsetof(struct st, b), offsetof(struct st, c), offsetof(struct st, d)}}
#define NP(st, ptr, cnt, el)                                                                                           \
    {.size = sizeof(struct st), .payload = offsetof(struct st, ptr), .count = offsetof(struct st, cnt),                \
     .count_width = sizeof(((struct st *)0)->cnt), .elem = el}

static const struct mn_wm_node blit[] = {
    [WMTBlitCommandNop] = N(wmtcmd_blit_nop),
    [WMTBlitCommandCopyFromBufferToBuffer] = N2(wmtcmd_blit_copy_from_buffer_to_buffer, src, dst),
    [WMTBlitCommandCopyFromBufferToTexture] = N2(wmtcmd_blit_copy_from_buffer_to_texture, src, dst),
    [WMTBlitCommandCopyFromTextureToBuffer] = N2(wmtcmd_blit_copy_from_texture_to_buffer, src, dst),
    [WMTBlitCommandCopyFromTextureToTexture] = N2(wmtcmd_blit_copy_from_texture_to_texture, src, dst),
    [WMTBlitCommandGenerateMipmaps] = N1(wmtcmd_blit_generate_mipmaps, texture),
    [WMTBlitCommandWaitForFence] = N1(wmtcmd_blit_fence_op, fence),
    [WMTBlitCommandUpdateFence] = N1(wmtcmd_blit_fence_op, fence),
    [WMTBlitCommandFillBuffer] = N1(wmtcmd_blit_fillbuffer, buffer),
    [WMTBlitCommandResolveCounters] = N2(wmtcmd_blit_resolvecounters, sample_buffer, dst_buffer),
    [WMTBlitCommandCopyFromBufferToTextureWithBlitOption] =
        N2(wmtcmd_blit_copy_from_buffer_to_texture_withblitoption, src, dst),
    [WMTBlitCommandCopyFromTextureToBufferWithBlitOption] =
        N2(wmtcmd_blit_copy_from_texture_to_buffer_withblitoption, src, dst),
    [WMTBlitCommandResetCommandsInBuffer] = N1(wmtcmd_blit_resetcommands, indirect_command_buffer),
    [WMTBlitCommandCopyTexture] = N2(wmtcmd_blit_copy_texture, src, dst),
};

static const struct mn_wm_node compute[] = {
    [WMTComputeCommandNop] = N(wmtcmd_compute_nop),
    [WMTComputeCommandDispatch] = N(wmtcmd_compute_dispatch),
    [WMTComputeCommandDispatchIndirect] = N1(wmtcmd_compute_dispatch_indirect, indirect_args_buffer),
    [WMTComputeCommandSetPSO] = N1(wmtcmd_compute_setpso, pso),
    [WMTComputeCommandSetBuffer] = N1(wmtcmd_compute_setbuffer, buffer),
    [WMTComputeCommandSetBufferOffset] = N(wmtcmd_compute_setbufferoffset),
    [WMTComputeCommandUseResource] = N1(wmtcmd_compute_useresource, resource),
    [WMTComputeCommandSetBytes] = NP(wmtcmd_compute_setbytes, bytes, length, 1),
    [WMTComputeCommandSetTexture] = N1(wmtcmd_compute_settexture, texture),
    [WMTComputeCommandDispatchThreads] = N(wmtcmd_compute_dispatch),
    [WMTComputeCommandWaitForFence] = N1(wmtcmd_compute_fence_op, fence),
    [WMTComputeCommandUpdateFence] = N1(wmtcmd_compute_fence_op, fence),
    [WMTComputeCommandMemoryBarrier] = N(wmtcmd_compute_memory_barrier),
    [WMTComputeCommandExecuteCommandsInBuffer] = N1(wmtcmd_compute_executecommands, indirect_command_buffer),
};

static const struct mn_wm_node render[] = {
    [WMTRenderCommandNop] = N(wmtcmd_render_nop),
    [WMTRenderCommandUseResource] = N1(wmtcmd_render_useresource, resource),
    [WMTRenderCommandSetVertexBuffer] = N1(wmtcmd_render_setbuffer, buffer),
    [WMTRenderCommandSetVertexBufferOffset] = N(wmtcmd_render_setbufferoffset),
    [WMTRenderCommandSetFragmentBuffer] = N1(wmtcmd_render_setbuffer, buffer),
    [WMTRenderCommandSetFragmentBufferOffset] = N(wmtcmd_render_setbufferoffset),
    [WMTRenderCommandSetMeshBuffer] = N1(wmtcmd_render_setbuffer, buffer),
    [WMTRenderCommandSetMeshBufferOffset] = N(wmtcmd_render_setbufferoffset),
    [WMTRenderCommandSetObjectBuffer] = N1(wmtcmd_render_setbuffer, buffer),
    [WMTRenderCommandSetObjectBufferOffset] = N(wmtcmd_render_setbufferoffset),
    [WMTRenderCommandSetFragmentTexture] = N1(wmtcmd_render_settexture, texture),
    [WMTRenderCommandSetFragmentBytes] = NP(wmtcmd_render_setbytes, bytes, length, 1),
    [WMTRenderCommandSetRasterizerState] = N(wmtcmd_render_setrasterizerstate),
    [WMTRenderCommandSetViewports] = NP(wmtcmd_render_setviewports, viewports, viewport_count, sizeof(struct WMTViewport)),
    [WMTRenderCommandSetScissorRects] =
        NP(wmtcmd_render_setscissorrects, scissor_rects, rect_count, sizeof(struct WMTScissorRect)),
    [WMTRenderCommandSetPSO] = N1(wmtcmd_render_setpso, pso),
    [WMTRenderCommandSetDSSO] = N1(wmtcmd_render_setdsso, dsso),
    [WMTRenderCommandSetBlendFactorAndStencilRef] = N(wmtcmd_render_setblendcolor),
    [WMTRenderCommandSetVisibilityMode] = N(wmtcmd_render_setvisibilitymode),
    [WMTRenderCommandDraw] = N(wmtcmd_render_draw),
    [WMTRenderCommandDrawIndexed] = N1(wmtcmd_render_draw_indexed, index_buffer),
    [WMTRenderCommandDrawIndirect] = N1(wmtcmd_render_draw_indirect, indirect_args_buffer),
    [WMTRenderCommandDrawIndexedIndirect] = N2(wmtcmd_render_draw_indexed_indirect, index_buffer, indirect_args_buffer),
    [WMTRenderCommandDrawMeshThreadgroups] = N(wmtcmd_render_draw_meshthreadgroups),
    [WMTRenderCommandDrawMeshThreadgroupsIndirect] =
        N1(wmtcmd_render_draw_meshthreadgroups_indirect, indirect_args_buffer),
    [WMTRenderCommandMemoryBarrier] = N(wmtcmd_render_memory_barrier),
    /* 26-28 (Unused0-2): no node */
    [WMTRenderCommandDXMTGeometryDraw] = N(wmtcmd_render_dxmt_geometry_draw),
    [WMTRenderCommandDXMTGeometryDrawIndexed] = N1(wmtcmd_render_dxmt_geometry_draw_indexed, index_buffer),
    [WMTRenderCommandDXMTGeometryDrawIndirect] = N3(wmtcmd_render_dxmt_geometry_draw_indirect, imm_draw_arguments,
                                                    indirect_args_buffer, dispatch_args_buffer),
    [WMTRenderCommandDXMTGeometryDrawIndexedIndirect] =
        N4(wmtcmd_render_dxmt_geometry_draw_indexed_indirect, index_buffer, imm_draw_arguments, indirect_args_buffer,
           dispatch_args_buffer),
    [WMTRenderCommandWaitForFence] = N1(wmtcmd_render_fence_op, fence),
    [WMTRenderCommandUpdateFence] = N1(wmtcmd_render_fence_op, fence),
    [WMTRenderCommandSetViewport] = N(wmtcmd_render_setviewport),
    [WMTRenderCommandSetScissorRect] = N(wmtcmd_render_setscissorrect),
    [WMTRenderCommandDXMTTessellationMeshDraw] = N(wmtcmd_render_dxmt_tessellation_mesh_draw),
    [WMTRenderCommandDXMTTessellationMeshDrawIndexed] =
        N1(wmtcmd_render_dxmt_tessellation_mesh_draw_indexed, index_buffer),
    [WMTRenderCommandDXMTTessellationMeshDrawIndirect] =
        N3(wmtcmd_render_dxmt_tessellation_mesh_draw_indirect, imm_draw_arguments, indirect_args_buffer,
           dispatch_args_buffer),
    [WMTRenderCommandDXMTTessellationMeshDrawIndexedIndirect] =
        N4(wmtcmd_render_dxmt_tessellation_mesh_draw_indexed_indirect, imm_draw_arguments, indirect_args_buffer,
           dispatch_args_buffer, index_buffer),
    [WMTRenderCommandDispatchThreadsPerTile] = N(wmtcmd_render_dispatch_threads_per_tile),
    [WMTRenderCommandExecuteCommandsInBuffer] = N1(wmtcmd_render_executecommands, indirect_command_buffer),
    [WMTRenderCommandSetBlendFactor] = N(wmtcmd_render_setblendcolor),
    [WMTRenderCommandSetStencilRef] = N(wmtcmd_render_setstencilref),
    [WMTRenderCommandSetDepthStencilState] = N1(wmtcmd_render_setdepthstencilstate, depth_stencil_state),
    /* mtld3d's direct binds (D3D9 M2b, DXMT's MacNeutron nodes) */
    [WMTRenderCommandSetVertexBytes] = NP(wmtcmd_render_setbytes, bytes, length, 1),
    [WMTRenderCommandSetVertexTexture] = N1(wmtcmd_render_settexture, texture),
    [WMTRenderCommandSetVertexSamplerState] = N1(wmtcmd_render_setsampler, sampler),
    [WMTRenderCommandSetFragmentSamplerState] = N1(wmtcmd_render_setsampler, sampler),
};

const struct mn_wm_node *mn_wm_nodes(unsigned slot, unsigned *ntypes) {
    switch (slot) {
    case 36: *ntypes = sizeof blit / sizeof *blit; return blit;
    case 37: *ntypes = sizeof compute / sizeof *compute; return compute;
    case 38: *ntypes = sizeof render / sizeof *render; return render;
    default: *ntypes = 0; return NULL;
    }
}

uint64_t mn_wm_node_payload(const struct mn_wm_node *t, const uint8_t *node) {
    if (!t->payload) return 0;
    uint64_t n = 0;
    memcpy(&n, node + t->count, t->count_width); /* little-endian on both sides */
    return n * t->elem;
}

#ifndef MN_WM_GUEST
/* The guest's flat list (k->data) as winemetal's linked list, in host memory: each node copied, its type the one
 * checked (whatever the guest writes now), its handles translated, its payload pointing into the region. */
NTSTATUS_T mn_wm_encode(struct mn_wm_state *k) {
    struct unixcall_generic_obj_cmd_noret *a = (void *)k->p;
    a->cmd_head.ptr = NULL;
    if (!k->data) return mn_wm_invoke(k->slot, a); /* an empty list */
    const uint8_t *r = k->data;
    uint64_t len = k->data_len, at = 8;
    uint32_t count;
    unsigned ntypes;
    const struct mn_wm_node *tab = mn_wm_nodes(k->slot, &ntypes);
    if (len < 8) return MN_STATUS_INVALID_PARAMETER;
    memcpy(&count, r, 4);
    if (!count || count > MN_WM_MAX_NODES) return MN_STATUS_INVALID_PARAMETER;
    uint8_t *head = NULL, *prev = NULL;
    for (uint32_t i = 0; i < count; i++) {
        uint16_t type;
        if (len - at < 16) return MN_STATUS_INVALID_PARAMETER;
        memcpy(&type, r + at, 2);
        if (type >= ntypes || !tab[type].size || tab[type].size > len - at) return MN_STATUS_INVALID_PARAMETER;
        const struct mn_wm_node *t = &tab[type];
        uint8_t *n = mn_wm_alloc(k, t->size);
        if (!n) return MN_STATUS_UNSUCCESSFUL;
        memcpy(n, r + at, t->size);
        memcpy(n, &type, 2);
        memset(n + 8, 0, 8);
        for (unsigned h = 0; h < t->nhandles; h++) {
            mn_handle g;
            memcpy(&g, n + t->handles[h], 8);
            if (!g) continue;
            int ok;
            void *obj = mn_ht_get(k->c->ht, g, MN_HK_OBJC, &ok);
            if (!ok) return MN_STATUS_INVALID_HANDLE;
            memcpy(n + t->handles[h], &obj, 8);
        }
        if (t->payload) {
            uint64_t plen = mn_wm_node_payload(t, n), off;
            const void *p = NULL;
            memcpy(&off, n + t->payload, 8);
            if (plen) {
                if (off < 8 || off > len || plen > len - off) return MN_STATUS_INVALID_PARAMETER;
                p = r + off;
            }
            memcpy(n + t->payload, &p, 8);
        }
        if (prev) memcpy(prev + 8, &n, 8);
        else head = n;
        prev = n;
        at += PAD8(t->size);
        if (at > len) return MN_STATUS_INVALID_PARAMETER;
    }
    a->cmd_head.ptr = head;
    return mn_wm_invoke(k->slot, a);
}
#endif
