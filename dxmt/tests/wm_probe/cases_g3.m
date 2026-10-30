/* wm_probe's g3 cases (slots.txt's g3; probe.h), recorded from DXMT's C: queues, command buffers, encoders, the three
 * command-list interpreters, counters (Task 9). A command case puts one command (every field the C reads set to its
 * own value, the rest left 0xA5) on a target-less recording encoder, so each message line pins the fields it reads. */
#include "probe.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* ---- helpers (g3 only) ---- */

/* A handle the case goes on with: 0 or the fill pattern stops it ("bad-handle"). */
static id need(const char *name, uint64_t h) {
    if (!h || h == WM_FILL64) wm_h(name, h);
    return (id)h;
}

/* A stand-in object for a handle inside a command (a recorder prints it as h<n>), watched for retain changes. */
static obj_handle_t so(void) {
    id o = [[[NSObject alloc] init] autorelease];
    wm_in(o);
    return (obj_handle_t)o;
}

static void hex(const char *name, const void *p, size_t n) {
    char *s = malloc(2 * n + 1);
    for (size_t i = 0; i < n; i++) snprintf(s + 2 * i, 3, "%02x", ((const uint8_t *)p)[i]);
    wm_val(name, "%s", n ? s : "-");
    free(s);
}

/* A command's header: 0xA5 everywhere (reserved and padding stay so), its type, no next. */
static void head(void *c, size_t size, unsigned type) {
    wm_fill(c, size);
    ((struct wmtcmd_base *)c)->type = (uint16_t)type;
    ((struct wmtcmd_base *)c)->next.ptr = NULL;
}

static void chain(void *a, void *b) { ((struct wmtcmd_base *)a)->next.ptr = b; }

/* The list at `cmds` through `slot` on `encoder` (0: a target-less recorder of `proto`). */
static void encode_on(unsigned slot, id encoder, const void *cmds) {
    struct unixcall_generic_obj_cmd_noret p;
    wm_fill(&p, sizeof p);
    p.encoder = (obj_handle_t)encoder;
    p.cmd_head.ptr = cmds;
    wm_call(slot, &p);
}

static void encode(unsigned slot, Protocol *proto, const void *cmds) {
    id enc = wm_rec(nil, proto);
    wm_in(enc);
    encode_on(slot, enc, cmds);
}

#define BLIT(cmd) encode(36, @protocol(MTLBlitCommandEncoder), &(cmd))
#define COMPUTE(cmd) encode(37, @protocol(MTLComputeCommandEncoder), &(cmd))
#define RENDER(cmd) encode(38, @protocol(MTLRenderCommandEncoder), &(cmd))
#define CMD(st, var, type) \
    struct st var;         \
    head(&var, sizeof var, type)

/* ---- 9, 11-14, 17 ---- */

static void queue(void) {
    id dev = wm_rec(wm_device, @protocol(MTLDevice));
    wm_in(dev);
    struct unixcall_generic_obj_uint64_obj_ret p;
    wm_fill(&p, sizeof p);
    p.handle = (obj_handle_t)dev;
    p.arg = 5;
    wm_own("ret", &p.ret);
    wm_call(9, &p);
    need("ret", p.ret);
    wm_val("arg", "%llu", (unsigned long long)p.arg);
}

static void cmdbuf_lifecycle(void) {
    id rq = wm_rec(wm_queue, @protocol(MTLCommandQueue));
    wm_in(rq);
    struct unixcall_generic_obj_obj_ret q;
    wm_fill(&q, sizeof q);
    q.handle = (obj_handle_t)rq;
    wm_own("ret", &q.ret);
    wm_call(11, &q);
    id<MTLCommandBuffer> cb = need("ret", q.ret);
    id rcb = wm_rec(cb, @protocol(MTLCommandBuffer));
    wm_in(rcb);
    struct unixcall_generic_obj_uint64_ret s;
    wm_fill(&s, sizeof s);
    s.handle = (obj_handle_t)rcb;
    wm_call(14, &s);
    wm_val("status", "%llu", (unsigned long long)s.ret);
    struct unixcall_generic_obj_noret n = {(obj_handle_t)rcb};
    wm_call(12, &n);
    if (cb.status == MTLCommandBufferStatusNotEnqueued) [cb commit]; /* a stubbed 12 must not hang 13 */
    n.handle = (obj_handle_t)rcb;
    wm_call(13, &n);
    if (cb.status < MTLCommandBufferStatusCompleted) [cb waitUntilCompleted];
    wm_fill(&s, sizeof s);
    s.handle = (obj_handle_t)rcb;
    wm_call(14, &s);
    wm_val("status", "%llu", (unsigned long long)s.ret);
    /* 11 straight on the probe's queue: the fresh command buffer has no owner but the handler's result (through the
     * recorder above, the forwarding's own pool entry keeps it alive), so a handler that drops its only reference
     * before returning hands back a freed object here (Review Focus 3). */
    wm_fill(&q, sizeof q);
    q.handle = (obj_handle_t)wm_queue;
    wm_own("ret", &q.ret);
    wm_call(11, &q);
    need("ret", q.ret);
}

static void signal_event(void) {
    id<MTLSharedEvent> ev = [[wm_device newSharedEvent] autorelease];
    id<MTLCommandBuffer> cb = [wm_queue commandBuffer];
    id rcb = wm_rec(cb, @protocol(MTLCommandBuffer));
    wm_in(rcb);
    struct unixcall_generic_obj_obj_uint64_noret p;
    wm_fill(&p, sizeof p);
    p.handle = (obj_handle_t)rcb;
    p.arg0 = (obj_handle_t)ev;
    p.arg1 = 7;
    wm_call(17, &p);
    [cb commit];
    [cb waitUntilCompleted];
    wm_val("signaledValue", "%llu", (unsigned long long)ev.signaledValue);
}

/* ---- 30-33 ---- */

static id<MTLTexture> make_rt(MTLPixelFormat format, NSUInteger w, NSUInteger h) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:w height:h mipmapped:NO];
    d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    d.storageMode = MTLStorageModePrivate;
    return [[wm_device newTextureWithDescriptor:d] autorelease];
}

/* 33 on a recording encoder around `enc`. */
static void end_encoding(id enc, Protocol *proto) {
    id r = wm_rec(enc, proto);
    wm_in(r);
    struct unixcall_generic_obj_noret n = {(obj_handle_t)r};
    wm_call(33, &n);
}

static void encoders(void) {
    id<MTLCommandBuffer> cb = [wm_queue commandBuffer];
    id rcb = wm_rec(cb, @protocol(MTLCommandBuffer));
    wm_in(rcb);
    struct unixcall_generic_obj_obj_ret b;
    wm_fill(&b, sizeof b);
    b.handle = (obj_handle_t)rcb;
    wm_own("ret", &b.ret);
    wm_call(30, &b);
    end_encoding(need("ret", b.ret), @protocol(MTLBlitCommandEncoder));
    /* 0 serial, any other value concurrent. Metal hands the ended concurrent encoder out again for 7, so its own=
     * would be Metal's count: no own= there. */
    static const uint64_t args[] = {0, 1, 7};
    for (unsigned i = 0; i < 3; i++) {
        uint64_t arg = args[i];
        struct unixcall_generic_obj_uint64_obj_ret c;
        wm_fill(&c, sizeof c);
        c.handle = (obj_handle_t)rcb;
        c.arg = arg;
        if (i < 2) wm_own("ret", &c.ret);
        wm_call(31, &c);
        id<MTLComputeCommandEncoder> e = need("ret", c.ret);
        wm_val("dispatchType", "%lu", (unsigned long)e.dispatchType);
        end_encoding(e, @protocol(MTLComputeCommandEncoder));
    }
    struct WMTRenderPassInfo info;
    memset(&info, 0, sizeof info);
    info.colors[0].texture = (obj_handle_t)make_rt(MTLPixelFormatRGBA8Unorm, 16, 16);
    info.colors[0].load_action = WMTLoadActionClear;
    info.colors[0].store_action = WMTStoreActionStore;
    info.default_raster_sample_count = 1;
    info.render_target_array_length = 0;
    info.render_target_width = 16;
    info.render_target_height = 16;
    struct unixcall_generic_obj_uint64_obj_ret r;
    wm_fill(&r, sizeof r);
    r.handle = (obj_handle_t)cb;
    r.arg = (uint64_t)&info;
    wm_own("ret", &r.ret);
    wm_call(32, &r);
    end_encoding(need("ret", r.ret), @protocol(MTLRenderCommandEncoder));
}

/* ---- 36: blit ---- */

static void blit_nop(void) {
    CMD(wmtcmd_blit_fence_op, a, WMTBlitCommandUpdateFence);
    a.fence = so();
    CMD(wmtcmd_blit_nop, nop, WMTBlitCommandNop);
    CMD(wmtcmd_blit_fence_op, b, WMTBlitCommandWaitForFence);
    b.fence = so();
    chain(&a, &nop);
    chain(&nop, &b);
    BLIT(a);
}

static void blit_copy_buffer_buffer(void) {
    CMD(wmtcmd_blit_copy_from_buffer_to_buffer, c, WMTBlitCommandCopyFromBufferToBuffer);
    c.src = so();
    c.src_offset = 0x1111;
    c.dst = so();
    c.dst_offset = 0x2222;
    c.copy_length = 0x3333;
    BLIT(c);
}

static void blit_copy_buffer_texture(void) {
    CMD(wmtcmd_blit_copy_from_buffer_to_texture, c, WMTBlitCommandCopyFromBufferToTexture);
    c.src = so();
    c.src_offset = 0x1010;
    c.bytes_per_row = 0x2020;
    c.bytes_per_image = 0x30303;
    c.size = (struct WMTSize){5, 6, 7};
    c.dst = so();
    c.slice = 8;
    c.level = 9;
    c.origin = (struct WMTOrigin){1, 2, 3};
    BLIT(c);
}

static void blit_copy_buffer_texture_option(void) {
    CMD(wmtcmd_blit_copy_from_buffer_to_texture_withblitoption, c, WMTBlitCommandCopyFromBufferToTextureWithBlitOption);
    c.src = so();
    c.src_offset = 0x1414;
    c.bytes_per_row = 0x2424;
    c.bytes_per_image = 0x34343;
    c.size = (struct WMTSize){15, 16, 17};
    c.dst = so();
    c.slice = 11;
    c.level = 10;
    c.options = WMTBlitOptionStencilFromDepthStencil;
    c.origin = (struct WMTOrigin){12, 13, 14};
    BLIT(c);
}

static void blit_copy_texture_buffer(void) {
    CMD(wmtcmd_blit_copy_from_texture_to_buffer, c, WMTBlitCommandCopyFromTextureToBuffer);
    c.src = so();
    c.slice = 4;
    c.level = 5;
    c.origin = (struct WMTOrigin){6, 7, 8};
    c.size = (struct WMTSize){9, 10, 11};
    c.dst = so();
    c.offset = 0x4040;
    c.bytes_per_row = 0x5050;
    c.bytes_per_image = 0x60606;
    BLIT(c);
}

static void blit_copy_texture_buffer_option(void) {
    CMD(wmtcmd_blit_copy_from_texture_to_buffer_withblitoption, c, WMTBlitCommandCopyFromTextureToBufferWithBlitOption);
    c.src = so();
    c.slice = 24;
    c.level = 25;
    c.options = WMTBlitOptionDepthFromDepthStencil;
    c.origin = (struct WMTOrigin){26, 27, 28};
    c.size = (struct WMTSize){29, 30, 31};
    c.dst = so();
    c.offset = 0x4848;
    c.bytes_per_row = 0x5858;
    c.bytes_per_image = 0x68686;
    BLIT(c);
}

static void blit_copy_texture_texture(void) {
    CMD(wmtcmd_blit_copy_from_texture_to_texture, c, WMTBlitCommandCopyFromTextureToTexture);
    c.src = so();
    c.src_slice = 1;
    c.src_level = 2;
    c.src_origin = (struct WMTOrigin){3, 4, 5};
    c.src_size = (struct WMTSize){6, 7, 8};
    c.dst = so();
    c.dst_slice = 9;
    c.dst_level = 10;
    c.dst_origin = (struct WMTOrigin){11, 12, 13};
    BLIT(c);
}

static void blit_generate_mipmaps(void) {
    CMD(wmtcmd_blit_generate_mipmaps, c, WMTBlitCommandGenerateMipmaps);
    c.texture = so();
    BLIT(c);
}

static void blit_wait_for_fence(void) {
    CMD(wmtcmd_blit_fence_op, c, WMTBlitCommandWaitForFence);
    c.fence = so();
    BLIT(c);
}

static void blit_update_fence(void) {
    CMD(wmtcmd_blit_fence_op, c, WMTBlitCommandUpdateFence);
    c.fence = so();
    BLIT(c);
}

static void blit_fill_buffer(void) {
    CMD(wmtcmd_blit_fillbuffer, c, WMTBlitCommandFillBuffer);
    c.buffer = so();
    c.offset = 0x700;
    c.length = 0x800;
    c.value = 0x5c;
    BLIT(c);
}

static void blit_resolve_counters(void) {
    CMD(wmtcmd_blit_resolvecounters, c, WMTBlitCommandResolveCounters);
    c.sample_buffer = so();
    c.start = 3;
    c.len = 5;
    c.dst_buffer = so();
    c.dst_offset = 0x900;
    BLIT(c);
}

static void blit_reset_commands(void) {
    CMD(wmtcmd_blit_resetcommands, c, WMTBlitCommandResetCommandsInBuffer);
    c.indirect_command_buffer = so();
    c.location = 7;
    c.length = 9;
    BLIT(c);
}

static void blit_copy_texture(void) {
    CMD(wmtcmd_blit_copy_texture, c, WMTBlitCommandCopyTexture);
    c.src = so();
    c.dst = so();
    BLIT(c);
}

/* ---- 37: compute ---- */

static void compute_nop(void) {
    CMD(wmtcmd_compute_setbufferoffset, a, WMTComputeCommandSetBufferOffset);
    a.offset = 0x60;
    a.index = 6;
    CMD(wmtcmd_compute_nop, nop, WMTComputeCommandNop);
    CMD(wmtcmd_compute_memory_barrier, b, WMTComputeCommandMemoryBarrier);
    b.scope = WMTBarrierScopeTextures;
    chain(&a, &nop);
    chain(&nop, &b);
    COMPUTE(a);
}

static void compute_dispatch(void) {
    CMD(wmtcmd_compute_dispatch, c, WMTComputeCommandDispatch);
    c.size = (struct WMTSize){2, 3, 4};
    COMPUTE(c);
}

static void compute_dispatch_indirect(void) {
    CMD(wmtcmd_compute_dispatch_indirect, c, WMTComputeCommandDispatchIndirect);
    c.indirect_args_buffer = so();
    c.indirect_args_offset = 0x123;
    COMPUTE(c);
}

static void compute_set_pso(void) {
    CMD(wmtcmd_compute_setpso, c, WMTComputeCommandSetPSO);
    c.pso = so();
    c.threadgroup_size = (struct WMTSize){8, 4, 2};
    COMPUTE(c);
}

static void compute_set_buffer(void) {
    CMD(wmtcmd_compute_setbuffer, c, WMTComputeCommandSetBuffer);
    c.buffer = so();
    c.offset = 0x40;
    c.index = 29;
    COMPUTE(c);
}

static void compute_set_buffer_offset(void) {
    CMD(wmtcmd_compute_setbufferoffset, c, WMTComputeCommandSetBufferOffset);
    c.offset = 0x80;
    c.index = 30;
    COMPUTE(c);
}

static void compute_use_resource(void) {
    CMD(wmtcmd_compute_useresource, c, WMTComputeCommandUseResource);
    c.resource = so();
    c.usage = WMTResourceUsageRead | WMTResourceUsageWrite;
    COMPUTE(c);
}

static void compute_set_bytes(void) {
    static const uint8_t bytes[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    CMD(wmtcmd_compute_setbytes, c, WMTComputeCommandSetBytes);
    c.bytes.ptr = (void *)bytes;
    c.length = sizeof bytes;
    c.index = 7;
    COMPUTE(c);
}

static void compute_set_texture(void) {
    CMD(wmtcmd_compute_settexture, c, WMTComputeCommandSetTexture);
    c.texture = so();
    c.index = 17;
    COMPUTE(c);
}

static void compute_dispatch_threads(void) {
    CMD(wmtcmd_compute_dispatch, c, WMTComputeCommandDispatchThreads);
    c.size = (struct WMTSize){64, 2, 1};
    COMPUTE(c);
}

static void compute_wait_for_fence(void) {
    CMD(wmtcmd_compute_fence_op, c, WMTComputeCommandWaitForFence);
    c.fence = so();
    COMPUTE(c);
}

static void compute_update_fence(void) {
    CMD(wmtcmd_compute_fence_op, c, WMTComputeCommandUpdateFence);
    c.fence = so();
    COMPUTE(c);
}

static void compute_memory_barrier(void) {
    CMD(wmtcmd_compute_memory_barrier, c, WMTComputeCommandMemoryBarrier);
    c.scope = WMTBarrierScopeBuffers | WMTBarrierScopeTextures;
    COMPUTE(c);
}

static void compute_execute_commands(void) {
    CMD(wmtcmd_compute_executecommands, c, WMTComputeCommandExecuteCommandsInBuffer);
    c.indirect_command_buffer = so();
    c.location = 2;
    c.length = 6;
    COMPUTE(c);
}

/* The threadgroup size is the last SetPSO's of the same call, and zero again on the next call. */
static void compute_threadgroup(void) {
    CMD(wmtcmd_compute_setpso, a, WMTComputeCommandSetPSO);
    a.pso = so();
    a.threadgroup_size = (struct WMTSize){8, 4, 2};
    CMD(wmtcmd_compute_dispatch, d1, WMTComputeCommandDispatch);
    d1.size = (struct WMTSize){2, 2, 2};
    CMD(wmtcmd_compute_setpso, b, WMTComputeCommandSetPSO);
    b.pso = so();
    b.threadgroup_size = (struct WMTSize){16, 1, 1};
    CMD(wmtcmd_compute_dispatch, d2, WMTComputeCommandDispatchThreads);
    d2.size = (struct WMTSize){64, 1, 1};
    chain(&a, &d1);
    chain(&d1, &b);
    chain(&b, &d2);
    id enc = wm_rec(nil, @protocol(MTLComputeCommandEncoder));
    wm_in(enc);
    encode_on(37, enc, &a);
    CMD(wmtcmd_compute_dispatch, d3, WMTComputeCommandDispatch);
    d3.size = (struct WMTSize){3, 3, 3};
    encode_on(37, enc, &d3);
    /* DispatchIndirect takes it too. */
    CMD(wmtcmd_compute_setpso, c, WMTComputeCommandSetPSO);
    c.pso = so();
    c.threadgroup_size = (struct WMTSize){4, 2, 1};
    CMD(wmtcmd_compute_dispatch_indirect, d4, WMTComputeCommandDispatchIndirect);
    d4.indirect_args_buffer = so();
    d4.indirect_args_offset = 0x40;
    chain(&c, &d4);
    encode_on(37, enc, &c);
}

/* ---- 38: render ---- */

static void render_nop(void) {
    CMD(wmtcmd_render_setstencilref, a, WMTRenderCommandSetStencilRef);
    a.stencil_ref = 0x21;
    CMD(wmtcmd_render_nop, nop, WMTRenderCommandNop);
    CMD(wmtcmd_render_setvisibilitymode, b, WMTRenderCommandSetVisibilityMode);
    b.offset = 0x88;
    b.mode = WMTVisibilityResultModeBoolean;
    chain(&a, &nop);
    chain(&nop, &b);
    RENDER(a);
}

static void render_use_resource(void) {
    CMD(wmtcmd_render_useresource, c, WMTRenderCommandUseResource);
    c.resource = so();
    c.usage = WMTResourceUsageSample;
    c.stages = WMTRenderStageVertex | WMTRenderStageFragment;
    RENDER(c);
}

static void set_buffer(unsigned type, uint64_t offset, uint8_t index) {
    CMD(wmtcmd_render_setbuffer, c, type);
    c.buffer = so();
    c.offset = offset;
    c.index = index;
    RENDER(c);
}

static void set_buffer_offset(unsigned type, uint64_t offset, uint8_t index) {
    CMD(wmtcmd_render_setbufferoffset, c, type);
    c.offset = offset;
    c.index = index;
    RENDER(c);
}

static void render_set_vertex_buffer(void) { set_buffer(WMTRenderCommandSetVertexBuffer, 0x100, 1); }
static void render_set_vertex_buffer_offset(void) { set_buffer_offset(WMTRenderCommandSetVertexBufferOffset, 0x200, 2); }
static void render_set_fragment_buffer(void) { set_buffer(WMTRenderCommandSetFragmentBuffer, 0x300, 3); }
static void render_set_fragment_buffer_offset(void) {
    set_buffer_offset(WMTRenderCommandSetFragmentBufferOffset, 0x400, 4);
}
static void render_set_mesh_buffer(void) { set_buffer(WMTRenderCommandSetMeshBuffer, 0x500, 5); }
static void render_set_mesh_buffer_offset(void) { set_buffer_offset(WMTRenderCommandSetMeshBufferOffset, 0x600, 6); }
static void render_set_object_buffer(void) { set_buffer(WMTRenderCommandSetObjectBuffer, 0x700, 7); }
static void render_set_object_buffer_offset(void) { set_buffer_offset(WMTRenderCommandSetObjectBufferOffset, 0x800, 8); }

static void set_texture(unsigned type, uint8_t index) {
    CMD(wmtcmd_render_settexture, c, type);
    c.texture = so();
    c.index = index;
    RENDER(c);
}

static void set_bytes(unsigned type, uint8_t index) {
    static const uint8_t bytes[8] = {0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8};
    CMD(wmtcmd_render_setbytes, c, type);
    c.bytes.ptr = (void *)bytes;
    c.length = sizeof bytes;
    c.index = index;
    RENDER(c);
}

static void set_sampler(unsigned type, uint8_t index) {
    CMD(wmtcmd_render_setsampler, c, type);
    c.sampler = so();
    c.index = index;
    RENDER(c);
}

static void render_set_fragment_texture(void) { set_texture(WMTRenderCommandSetFragmentTexture, 9); }
static void render_set_fragment_bytes(void) { set_bytes(WMTRenderCommandSetFragmentBytes, 10); }
static void render_set_vertex_bytes(void) { set_bytes(WMTRenderCommandSetVertexBytes, 11); }
static void render_set_vertex_texture(void) { set_texture(WMTRenderCommandSetVertexTexture, 12); }
static void render_set_vertex_sampler(void) { set_sampler(WMTRenderCommandSetVertexSamplerState, 13); }
static void render_set_fragment_sampler(void) { set_sampler(WMTRenderCommandSetFragmentSamplerState, 14); }

static void render_set_rasterizer_state(void) {
    CMD(wmtcmd_render_setrasterizerstate, c, WMTRenderCommandSetRasterizerState);
    c.fill_mode = WMTTriangleFillModeLines;
    c.cull_mode = WMTCullModeBack;
    c.depth_clip_mode = WMTDepthClipModeClamp;
    c.winding = WMTWindingCounterClockwise;
    c.depth_bias = 0.5f;
    c.scole_scale = 1.5f;
    c.depth_bias_clamp = 2.5f;
    RENDER(c);
}

static void render_set_viewports(void) {
    static const struct WMTViewport v[2] = {{1, 2, 3, 4, 0.125, 0.875}, {5, 6, 7, 8, 0.25, 0.75}};
    CMD(wmtcmd_render_setviewports, c, WMTRenderCommandSetViewports);
    c.viewports.ptr = (void *)v;
    c.viewport_count = 2;
    RENDER(c);
}

static void render_set_scissor_rects(void) {
    static const struct WMTScissorRect r[2] = {{1, 2, 3, 4}, {5, 6, 7, 8}};
    CMD(wmtcmd_render_setscissorrects, c, WMTRenderCommandSetScissorRects);
    c.scissor_rects.ptr = (void *)r;
    c.rect_count = 2;
    RENDER(c);
}

static void render_set_pso(void) {
    CMD(wmtcmd_render_setpso, c, WMTRenderCommandSetPSO);
    c.pso = so();
    RENDER(c);
}

static void render_set_dsso(void) {
    CMD(wmtcmd_render_setdsso, c, WMTRenderCommandSetDSSO);
    c.dsso = so();
    c.stencil_ref = 0x7e;
    RENDER(c);
}

static void render_set_blend_factor_and_stencil_ref(void) {
    CMD(wmtcmd_render_setblendcolor, c, WMTRenderCommandSetBlendFactorAndStencilRef);
    c.red = 0.125f, c.green = 0.25f, c.blue = 0.5f, c.alpha = 1.0f;
    c.stencil_ref = 0x33;
    RENDER(c);
}

static void render_set_visibility_mode(void) {
    CMD(wmtcmd_render_setvisibilitymode, c, WMTRenderCommandSetVisibilityMode);
    c.offset = 0x1000;
    c.mode = WMTVisibilityResultModeCounting;
    RENDER(c);
}

static void render_draw(void) {
    CMD(wmtcmd_render_draw, c, WMTRenderCommandDraw);
    c.primitive_type = WMTPrimitiveTypeTriangleStrip;
    c.vertex_start = 11;
    c.vertex_count = 12;
    c.instance_count = 13;
    c.base_instance = 14;
    RENDER(c);
}

static void render_draw_indexed(void) {
    CMD(wmtcmd_render_draw_indexed, c, WMTRenderCommandDrawIndexed);
    c.primitive_type = WMTPrimitiveTypeLine;
    c.index_type = WMTIndexTypeUInt32;
    c.index_count = 15;
    c.index_buffer = so();
    c.index_buffer_offset = 0x40;
    c.instance_count = 16;
    c.base_vertex = -3;
    c.base_instance = 17;
    RENDER(c);
}

static void render_draw_indirect(void) {
    CMD(wmtcmd_render_draw_indirect, c, WMTRenderCommandDrawIndirect);
    c.primitive_type = WMTPrimitiveTypeLineStrip;
    c.indirect_args_buffer = so();
    c.indirect_args_offset = 0x50;
    RENDER(c);
}

static void render_draw_indexed_indirect(void) {
    CMD(wmtcmd_render_draw_indexed_indirect, c, WMTRenderCommandDrawIndexedIndirect);
    c.primitive_type = WMTPrimitiveTypeTriangle;
    c.index_type = WMTIndexTypeUInt16;
    c.index_buffer = so();
    c.index_buffer_offset = 0x60;
    c.indirect_args_buffer = so();
    c.indirect_args_offset = 0x70;
    RENDER(c);
}

static void render_draw_mesh_threadgroups(void) {
    CMD(wmtcmd_render_draw_meshthreadgroups, c, WMTRenderCommandDrawMeshThreadgroups);
    c.threadgroup_per_grid = (struct WMTSize){1, 2, 3};
    c.object_threadgroup_size = (struct WMTSize){4, 5, 6};
    c.mesh_threadgroup_size = (struct WMTSize){7, 8, 9};
    RENDER(c);
}

static void render_draw_mesh_threadgroups_indirect(void) {
    CMD(wmtcmd_render_draw_meshthreadgroups_indirect, c, WMTRenderCommandDrawMeshThreadgroupsIndirect);
    c.indirect_args_buffer = so();
    c.indirect_args_offset = 0x80;
    c.object_threadgroup_size = (struct WMTSize){10, 11, 12};
    c.mesh_threadgroup_size = (struct WMTSize){13, 14, 15};
    RENDER(c);
}

/* The C passes stages_after then stages_before (the struct holds them the other way round). */
static void render_memory_barrier(void) {
    CMD(wmtcmd_render_memory_barrier, c, WMTRenderCommandMemoryBarrier);
    c.scope = WMTBarrierScopeRenderTargets;
    c.stages_before = WMTRenderStageFragment;
    c.stages_after = WMTRenderStageVertex;
    RENDER(c);
}

static void render_geometry_draw(void) {
    CMD(wmtcmd_render_dxmt_geometry_draw, c, WMTRenderCommandDXMTGeometryDraw);
    c.draw_arguments_offset = 0x90;
    c.warp_count = 3;
    c.instance_count = 4;
    c.vertex_per_warp = 5;
    RENDER(c);
}

static void render_geometry_draw_indexed(void) {
    CMD(wmtcmd_render_dxmt_geometry_draw_indexed, c, WMTRenderCommandDXMTGeometryDrawIndexed);
    c.draw_arguments_offset = 0x98;
    c.index_buffer = so();
    c.index_buffer_offset = 0xa0;
    c.warp_count = 6;
    c.instance_count = 7;
    c.vertex_per_warp = 8;
    RENDER(c);
}

static void render_geometry_draw_indirect(void) {
    CMD(wmtcmd_render_dxmt_geometry_draw_indirect, c, WMTRenderCommandDXMTGeometryDrawIndirect);
    c.imm_draw_arguments = so();
    c.indirect_args_buffer = so();
    c.indirect_args_offset = 0xb0;
    c.dispatch_args_buffer = so();
    c.dispatch_args_offset = 0xc0;
    c.vertex_per_warp = 9;
    RENDER(c);
}

static void render_geometry_draw_indexed_indirect(void) {
    CMD(wmtcmd_render_dxmt_geometry_draw_indexed_indirect, c, WMTRenderCommandDXMTGeometryDrawIndexedIndirect);
    c.index_buffer = so();
    c.index_buffer_offset = 0xd0;
    c.imm_draw_arguments = so();
    c.indirect_args_buffer = so();
    c.indirect_args_offset = 0xe0;
    c.dispatch_args_buffer = so();
    c.dispatch_args_offset = 0xf0;
    c.vertex_per_warp = 10;
    RENDER(c);
}

static void render_wait_for_fence(void) {
    CMD(wmtcmd_render_fence_op, c, WMTRenderCommandWaitForFence);
    c.fence = so();
    c.stages = WMTRenderStageFragment;
    RENDER(c);
}

static void render_update_fence(void) {
    CMD(wmtcmd_render_fence_op, c, WMTRenderCommandUpdateFence);
    c.fence = so();
    c.stages = WMTRenderStageVertex | WMTRenderStageObject;
    RENDER(c);
}

static void render_set_viewport(void) {
    CMD(wmtcmd_render_setviewport, c, WMTRenderCommandSetViewport);
    c.viewport = (struct WMTViewport){1.5, 2.5, 640, 480, 0.25, 0.75};
    RENDER(c);
}

static void render_set_scissor_rect(void) {
    CMD(wmtcmd_render_setscissorrect, c, WMTRenderCommandSetScissorRect);
    c.scissor_rect = (struct WMTScissorRect){9, 10, 11, 12};
    RENDER(c);
}

static void render_tess_draw(void) {
    CMD(wmtcmd_render_dxmt_tessellation_mesh_draw, c, WMTRenderCommandDXMTTessellationMeshDraw);
    c.draw_arguments_offset = 0x110;
    c.instance_count = 2;
    c.threads_per_patch = 3;
    c.patch_per_group = 4;
    c.patch_per_mesh_instance = 5;
    RENDER(c);
}

static void render_tess_draw_indexed(void) {
    CMD(wmtcmd_render_dxmt_tessellation_mesh_draw_indexed, c, WMTRenderCommandDXMTTessellationMeshDrawIndexed);
    c.draw_arguments_offset = 0x118;
    c.index_buffer = so();
    c.index_buffer_offset = 0x120;
    c.instance_count = 6;
    c.threads_per_patch = 7;
    c.patch_per_group = 8;
    c.patch_per_mesh_instance = 9;
    RENDER(c);
}

static void render_tess_draw_indirect(void) {
    CMD(wmtcmd_render_dxmt_tessellation_mesh_draw_indirect, c, WMTRenderCommandDXMTTessellationMeshDrawIndirect);
    c.imm_draw_arguments = so();
    c.indirect_args_buffer = so();
    c.indirect_args_offset = 0x130;
    c.dispatch_args_buffer = so();
    c.dispatch_args_offset = 0x140;
    c.threads_per_patch = 10;
    c.patch_per_group = 11;
    RENDER(c);
}

static void render_tess_draw_indexed_indirect(void) {
    CMD(wmtcmd_render_dxmt_tessellation_mesh_draw_indexed_indirect, c,
        WMTRenderCommandDXMTTessellationMeshDrawIndexedIndirect);
    c.imm_draw_arguments = so();
    c.indirect_args_buffer = so();
    c.indirect_args_offset = 0x150;
    c.dispatch_args_buffer = so();
    c.dispatch_args_offset = 0x160;
    c.index_buffer = so();
    c.index_buffer_offset = 0x170;
    c.threads_per_patch = 12;
    c.patch_per_group = 13;
    RENDER(c);
}

static void render_dispatch_threads_per_tile(void) {
    CMD(wmtcmd_render_dispatch_threads_per_tile, c, WMTRenderCommandDispatchThreadsPerTile);
    c.width = 16;
    c.height = 8;
    RENDER(c);
}

static void render_execute_commands(void) {
    CMD(wmtcmd_render_executecommands, c, WMTRenderCommandExecuteCommandsInBuffer);
    c.indirect_command_buffer = so();
    c.location = 3;
    c.length = 4;
    RENDER(c);
}

static void render_set_blend_factor(void) {
    CMD(wmtcmd_render_setblendcolor, c, WMTRenderCommandSetBlendFactor);
    c.red = 0.75f, c.green = 0.625f, c.blue = 0.375f, c.alpha = 0.0625f;
    c.stencil_ref = 0x99;
    RENDER(c);
}

static void render_set_stencil_ref(void) {
    CMD(wmtcmd_render_setstencilref, c, WMTRenderCommandSetStencilRef);
    c.stencil_ref = 0x44;
    RENDER(c);
}

static void render_set_depth_stencil_state(void) {
    CMD(wmtcmd_render_setdepthstencilstate, c, WMTRenderCommandSetDepthStencilState);
    c.depth_stencil_state = so();
    RENDER(c);
}

/* ---- unknown types: 999 between two commands, both run ---- */

static void blit_unknown_type(void) {
    CMD(wmtcmd_blit_generate_mipmaps, a, WMTBlitCommandGenerateMipmaps);
    a.texture = so();
    CMD(wmtcmd_base, u, 999);
    CMD(wmtcmd_blit_copy_texture, b, WMTBlitCommandCopyTexture);
    b.src = so();
    b.dst = so();
    chain(&a, &u);
    chain(&u, &b);
    BLIT(a);
}

static void compute_unknown_type(void) {
    CMD(wmtcmd_compute_setbufferoffset, a, WMTComputeCommandSetBufferOffset);
    a.offset = 0x10;
    a.index = 1;
    CMD(wmtcmd_base, u, 999);
    CMD(wmtcmd_compute_memory_barrier, b, WMTComputeCommandMemoryBarrier);
    b.scope = WMTBarrierScopeBuffers;
    chain(&a, &u);
    chain(&u, &b);
    COMPUTE(a);
}

static void render_unknown_type(void) {
    CMD(wmtcmd_render_setstencilref, a, WMTRenderCommandSetStencilRef);
    a.stencil_ref = 0x12;
    CMD(wmtcmd_base, u, 999);
    CMD(wmtcmd_render_dispatch_threads_per_tile, b, WMTRenderCommandDispatchThreadsPerTile);
    b.width = 4;
    b.height = 2;
    chain(&a, &u);
    chain(&u, &b);
    RENDER(a);
}

/* ---- GPU: the lists on real encoders ---- */

static void blit_gpu(void) {
    id<MTLBuffer> src = [[wm_device newBufferWithLength:64 options:MTLResourceStorageModeShared] autorelease];
    id<MTLBuffer> dst = [[wm_device newBufferWithLength:64 options:MTLResourceStorageModeShared] autorelease];
    for (unsigned i = 0; i < 64; i++) ((uint8_t *)src.contents)[i] = (uint8_t)i;
    memset(dst.contents, WM_FILL, 64);
    wm_in(src);
    wm_in(dst);
    CMD(wmtcmd_blit_copy_from_buffer_to_buffer, c, WMTBlitCommandCopyFromBufferToBuffer);
    c.src = (obj_handle_t)src;
    c.src_offset = 4;
    c.dst = (obj_handle_t)dst;
    c.dst_offset = 8;
    c.copy_length = 16;
    CMD(wmtcmd_blit_fillbuffer, f, WMTBlitCommandFillBuffer);
    f.buffer = (obj_handle_t)dst;
    f.offset = 32;
    f.length = 8;
    f.value = 0x5c;
    chain(&c, &f);
    id<MTLCommandBuffer> cb = [wm_queue commandBuffer];
    id<MTLBlitCommandEncoder> enc = [cb blitCommandEncoder];
    encode_on(36, enc, &c);
    [enc endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    wm_bytes("dst", dst.contents, 64);
    hex("bytes", dst.contents, 64);
}

static void compute_gpu(void) {
    id<MTLLibrary> lib = wm_library(@"#include <metal_stdlib>\nusing namespace metal;\n"
                                    @"kernel void twice(device uint *b [[buffer(0)]], uint i [[thread_position_in_grid]])"
                                    @" { b[i] *= 2; }\n");
    if (!lib) return;
    id<MTLFunction> fn = [[lib newFunctionWithName:@"twice"] autorelease];
    id<MTLComputePipelineState> pso = [[wm_device newComputePipelineStateWithFunction:fn error:NULL] autorelease];
    id<MTLBuffer> buf = [[wm_device newBufferWithLength:64 options:MTLResourceStorageModeShared] autorelease];
    for (unsigned i = 0; i < 16; i++) ((uint32_t *)buf.contents)[i] = i + 1;
    wm_in(pso);
    wm_in(buf);
    CMD(wmtcmd_compute_setpso, s, WMTComputeCommandSetPSO);
    s.pso = (obj_handle_t)pso;
    s.threadgroup_size = (struct WMTSize){4, 1, 1};
    CMD(wmtcmd_compute_setbuffer, b, WMTComputeCommandSetBuffer);
    b.buffer = (obj_handle_t)buf;
    b.offset = 0;
    b.index = 0;
    CMD(wmtcmd_compute_dispatch, d, WMTComputeCommandDispatch);
    d.size = (struct WMTSize){4, 1, 1};
    chain(&s, &b);
    chain(&b, &d);
    id<MTLCommandBuffer> cb = [wm_queue commandBuffer];
    id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
    encode_on(37, enc, &s);
    [enc endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    char words[16 * 4];
    words[0] = 0;
    for (unsigned i = 0; i < 16; i++)
        snprintf(words + strlen(words), sizeof words - strlen(words), "%s%u", i ? "," : "",
                 ((uint32_t *)buf.contents)[i]);
    wm_val("words", "%s", words);
}

static void render_gpu(void) {
    id<MTLLibrary> lib = wm_library(@"#include <metal_stdlib>\nusing namespace metal;\n"
                                    @"vertex float4 v(uint i [[vertex_id]]) {\n"
                                    @"  return float4(i == 1 ? 3.0 : -1.0, i == 2 ? 3.0 : -1.0, 0, 1); }\n"
                                    @"fragment float4 f(constant float4 &c [[buffer(0)]]) { return c; }\n");
    if (!lib) return;
    MTLRenderPipelineDescriptor *rd = [[[MTLRenderPipelineDescriptor alloc] init] autorelease];
    rd.vertexFunction = [[lib newFunctionWithName:@"v"] autorelease];
    rd.fragmentFunction = [[lib newFunctionWithName:@"f"] autorelease];
    rd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
    id<MTLRenderPipelineState> pso = [[wm_device newRenderPipelineStateWithDescriptor:rd error:NULL] autorelease];
    id<MTLTexture> rt = make_rt(MTLPixelFormatRGBA8Unorm, 8, 8);
    wm_in(pso);
    static const float color[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    CMD(wmtcmd_render_setpso, s, WMTRenderCommandSetPSO);
    s.pso = (obj_handle_t)pso;
    CMD(wmtcmd_render_setviewport, v, WMTRenderCommandSetViewport);
    v.viewport = (struct WMTViewport){0, 0, 8, 8, 0, 1};
    CMD(wmtcmd_render_setbytes, b, WMTRenderCommandSetFragmentBytes);
    b.bytes.ptr = (void *)color;
    b.length = sizeof color;
    b.index = 0;
    CMD(wmtcmd_render_draw, d, WMTRenderCommandDraw);
    d.primitive_type = WMTPrimitiveTypeTriangle;
    d.vertex_start = 0;
    d.vertex_count = 3;
    d.instance_count = 1;
    d.base_instance = 0;
    chain(&s, &v);
    chain(&v, &b);
    chain(&b, &d);
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = rt;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLCommandBuffer> cb = [wm_queue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
    encode_on(38, enc, &s);
    [enc endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    NSData *px = wm_readback(rt);
    hex("centre", (const uint8_t *)px.bytes + (4 * 8 + 4) * 4, 4);
    hex("corner", px.bytes, 4);
}

/* ---- 32: the render pass ---- */

/* 32 through a target-less recording command buffer: the descriptor dump. Full: every field set (each attachment its
 * own values); sparse: no depth or stencil texture, a tile width alone, no accumulate, no sample buffer. */
static void renderpass(void) {
    id rcb = wm_rec(nil, @protocol(MTLCommandBuffer));
    wm_in(rcb);
    for (int full = 1; full >= 0; full--) {
        struct WMTRenderPassInfo info;
        wm_fill(&info, sizeof info);
        for (unsigned i = 0; i < 8; i++) {
            struct WMTColorAttachmentInfo *c = &info.colors[i];
            c->texture = full && i % 3 != 2 ? so() : 0;
            c->load_action = (enum WMTLoadAction)(i % 3);
            c->store_action = (enum WMTStoreAction)(i % 4);
            c->level = (uint16_t)(i + 1);
            c->slice = (uint16_t)(i + 2);
            c->depth_plane = i + 3;
            c->clear_color = (struct WMTClearColor){i + 0.125, i + 0.25, i + 0.5, i + 0.75};
            c->resolve_texture = full && i % 2 ? so() : 0;
            c->resolve_level = (uint16_t)(i + 4);
            c->resolve_slice = (uint16_t)(i + 5);
            c->resolve_depth_plane = i + 6;
        }
        info.depth.texture = full ? so() : 0;
        info.depth.load_action = WMTLoadActionLoad;
        info.depth.store_action = WMTStoreActionDontCare;
        info.depth.level = 2;
        info.depth.slice = 3;
        info.depth.depth_plane = 4;
        info.depth.clear_depth = 0.75f;
        info.stencil.texture = full ? so() : 0;
        info.stencil.load_action = WMTLoadActionClear;
        info.stencil.store_action = WMTStoreActionStore;
        info.stencil.level = 5;
        info.stencil.slice = 6;
        info.stencil.depth_plane = 7;
        info.stencil.clear_stencil = 0x5a;
        info.default_raster_sample_count = 4;
        info.render_target_array_length = 3;
        info.tile_width = 16;
        info.tile_height = full ? 32 : 0;
        info.render_target_height = 600;
        info.render_target_width = 800;
        info.visibility_accumulate = (uint8_t)full;
        info.visibility_buffer = full ? so() : 0;
        for (unsigned i = 0; i < 4; i++) {
            info.sample_buffers[i].sample_buffer = full && i % 2 == 0 ? so() : 0;
            info.sample_buffers[i].end_of_fragment_sample_index = 2 * i + 1;
            info.sample_buffers[i].start_of_vertex_sample_index = i == 2 ? ~0ull : 2 * i;
        }
        struct unixcall_generic_obj_uint64_obj_ret p;
        wm_fill(&p, sizeof p);
        p.handle = (obj_handle_t)rcb;
        p.arg = (uint64_t)&info;
        wm_own("ret", &p.ret);
        wm_call(32, &p);
    }
}

/* ---- 47, 48 ---- */

static void present_selectors(void) {
    id rcb = wm_rec(nil, @protocol(MTLCommandBuffer));
    wm_in(rcb);
    obj_handle_t drawable = so();
    struct unixcall_generic_obj_obj_noret p = {(obj_handle_t)rcb, drawable};
    wm_call(47, &p);
    struct unixcall_generic_obj_obj_double_noret q;
    wm_fill(&q, sizeof q);
    q.handle = (obj_handle_t)rcb;
    q.arg0 = drawable;
    q.arg1 = 0.25;
    wm_call(48, &q);
}

/* ---- 89-91 ---- */

static void error_logs(void) {
    id<MTLCommandBuffer> cb = [wm_queue commandBuffer];
    [cb commit];
    [cb waitUntilCompleted];
    for (unsigned slot = 89; slot <= 90; slot++) {
        struct unixcall_generic_obj_obj_ret p;
        wm_fill(&p, sizeof p);
        p.handle = (obj_handle_t)cb;
        wm_own("ret", &p.ret);
        wm_call(slot, &p);
    }
    id rcb = wm_rec(nil, @protocol(MTLCommandBuffer));
    wm_in(rcb);
    NSError *e = [NSError errorWithDomain:@"wm_probe" code:89 userInfo:nil];
    NSArray *logs = wm_nsarray(2);
    wm_rec_ret(rcb, @selector(error), e);
    wm_rec_ret(rcb, @selector(logs), logs);
    for (unsigned slot = 89; slot <= 90; slot++) {
        struct unixcall_generic_obj_obj_ret p;
        wm_fill(&p, sizeof p);
        p.handle = (obj_handle_t)rcb;
        wm_own("ret", &p.ret);
        wm_call(slot, &p);
        wm_eq("canned", p.ret, slot == 89 ? (uint64_t)e : (uint64_t)logs, slot == 89 ? "error" : "logs");
    }
}

static void enumerate_one(NSArray *a, uint64_t start, uint64_t size) {
    obj_handle_t buf[4];
    wm_fill(buf, sizeof buf);
    struct unixcall_enumerate p;
    wm_fill(&p, sizeof p);
    p.enumerable = (obj_handle_t)a;
    p.start = start;
    p.buffer_size = size;
    p.buffer.ptr = buf;
    if (start < a.count) wm_own("buffer[0]", &buf[0]);
    if (start + 1 < a.count && size > 1) wm_own("buffer[1]", &buf[1]);
    wm_call(91, &p);
    wm_val("read", "%llu", (unsigned long long)p.ret_read);
    for (uint64_t i = 0; i < p.ret_read && i < 4; i++) {
        char k[16];
        snprintf(k, sizeof k, "item%llu", (unsigned long long)i);
        char o[16];
        snprintf(o, sizeof o, "a[%llu]", (unsigned long long)(start + i));
        wm_eq(k, buf[i], start + i < a.count ? (uint64_t)a[start + i] : 0, o);
    }
    wm_bytes("buffer", buf, sizeof buf);
}

static void enumerate(void) {
    NSArray *a = wm_nsarray(5);
    wm_in(a);
    enumerate_one(a, 1, 2);
    enumerate_one(a, 4, 2);
}

static void enumerate_past_end(void) {
    NSArray *a = wm_nsarray(5);
    wm_in(a);
    enumerate_one(a, 5, 2);
    enumerate_one(a, 7, 2);
}

/* ---- 102 ---- */

static void wait_event(void) {
    id rcb = wm_rec(nil, @protocol(MTLCommandBuffer));
    wm_in(rcb);
    struct unixcall_generic_obj_obj_uint64_noret p;
    wm_fill(&p, sizeof p);
    p.handle = (obj_handle_t)rcb;
    p.arg0 = so();
    p.arg1 = 0x1234;
    wm_call(102, &p);
}

/* ---- 127-130, 147 ---- */

static id<MTLCounterSampleBuffer> timestamp_buffer(id dev, uint32_t count, uint32_t shared) {
    struct unixcall_mtlcountersamplebuffer_newtimestampbuffer p;
    wm_fill(&p, sizeof p);
    p.device = (obj_handle_t)dev;
    p.sample_count = count;
    p.shared = shared;
    wm_own("ret", &p.ret);
    wm_call(127, &p);
    id<MTLCounterSampleBuffer> sb = need("ret", p.ret);
    wm_val("sampleCount", "%lu", (unsigned long)sb.sampleCount);
    return sb;
}

/* One 127 through a recording device (Private: the dump), one straight to the device (Shared: 128 resolves it). Metal
 * makes the counter set's objects afresh on each counterSets, so a second dump would number them by address reuse. */
static void timestamps(void) {
    id dev = wm_rec(wm_device, @protocol(MTLDevice));
    wm_in(dev);
    timestamp_buffer(dev, 2, 0);
    id<MTLCounterSampleBuffer> sb = timestamp_buffer(wm_device, 4, 1);
    wm_in(sb);
    id<MTLBuffer> buf = [[wm_device newBufferWithLength:64 options:MTLResourceStorageModeShared] autorelease];
    id<MTLLibrary> lib = wm_library(@"kernel void k(device uint *b [[buffer(0)]], uint i [[thread_position_in_grid]])"
                                    @" { b[i] = i; }\n");
    if (!lib) return;
    id<MTLComputePipelineState> pso =
        [[wm_device newComputePipelineStateWithFunction:[[lib newFunctionWithName:@"k"] autorelease] error:NULL] autorelease];
    id<MTLCommandBuffer> cb = [wm_queue commandBuffer];
    id rcb = wm_rec(cb, @protocol(MTLCommandBuffer));
    wm_in(rcb);
    struct WMTSampleBufferAttachmentInfo att = {(obj_handle_t)sb, 0, 1};
    struct unixcall_mtlcommandbuffer_blitcommandencoderwithsamplebuffers b;
    wm_fill(&b, sizeof b);
    b.cmdbuf = (obj_handle_t)rcb;
    b.attachments.ptr = &att;
    b.num_attachments = 1;
    wm_own("ret", &b.ret);
    wm_call(129, &b);
    id<MTLBlitCommandEncoder> be = need("ret", b.ret);
    [be fillBuffer:buf range:NSMakeRange(0, 64) value:1];
    [be endEncoding];
    struct WMTSampleBufferAttachmentInfo att2 = {(obj_handle_t)sb, 2, 3};
    struct unixcall_mtlcommandbuffer_computecommandencoderwithsamplebuffers c;
    wm_fill(&c, sizeof c);
    c.cmdbuf = (obj_handle_t)rcb;
    c.attachments.ptr = &att2;
    c.num_attachments = 1;
    c.concurrent = 1;
    wm_own("ret", &c.ret);
    wm_call(147, &c);
    id<MTLComputeCommandEncoder> ce = need("ret", c.ret);
    wm_val("dispatchType", "%lu", (unsigned long)ce.dispatchType);
    [ce setComputePipelineState:pso];
    [ce setBuffer:buf offset:0 atIndex:0];
    [ce dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(16, 1, 1)];
    [ce endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    /* 128: the four samples; part of them (data_length 12); no data_out. */
    uint64_t ts[4];
    wm_fill(ts, sizeof ts);
    struct unixcall_mtlcountersamplebuffer_resolvecounterrange r;
    wm_fill(&r, sizeof r);
    r.sample_buffer = (obj_handle_t)sb;
    r.start = 0;
    r.len = 4;
    r.data_out.ptr = ts;
    r.data_length = sizeof ts;
    wm_call(128, &r);
    /* Timestamps are any bytes (0xA5 among them): written words, and the bytes past data_length. */
    unsigned words = 0;
    for (unsigned i = 0; i < 4; i++) words += ts[i] != WM_FILL64;
    wm_val("words", "%u", words);
    /* Both encoders write buf (tracked), so the blit's samples come before the compute's. */
    bool mono = ts[0] && ts[0] <= ts[1] && ts[1] <= ts[2] && ts[2] <= ts[3];
    wm_rel("samples", mono, "monotonic", "%llu,%llu,%llu,%llu", (unsigned long long)ts[0], (unsigned long long)ts[1],
           (unsigned long long)ts[2], (unsigned long long)ts[3]);
    uint64_t part[4];
    wm_fill(part, sizeof part);
    wm_fill(&r, sizeof r);
    r.sample_buffer = (obj_handle_t)sb;
    r.start = 1;
    r.len = 3;
    r.data_out.ptr = part;
    r.data_length = 12;
    wm_call(128, &r);
    bool tail = true;
    for (unsigned i = 12; i < sizeof part; i++) tail &= ((const uint8_t *)part)[i] == WM_FILL;
    wm_val("tail", "%s", tail ? "untouched" : "written");
    wm_rel("first", !memcmp(part, &ts[1], 12), "samples[1..]", "%llx", (unsigned long long)part[0]);
    wm_fill(&r, sizeof r);
    r.sample_buffer = (obj_handle_t)sb;
    r.start = 0;
    r.len = 4;
    r.data_out.ptr = NULL;
    r.data_length = 32;
    wm_call(128, &r);
    /* 130: the four times in ns as relations, then an unknown property (0). */
    static const char *const names[] = {"kernelStartTime", "kernelEndTime", "GPUStartTime", "GPUEndTime"};
    double t[4] = {cb.kernelStartTime, cb.kernelEndTime, cb.GPUStartTime, cb.GPUEndTime};
    for (unsigned k = 0; k <= 4; k++) {
        struct unixcall_generic_obj_uint64_uint64_ret q;
        wm_fill(&q, sizeof q);
        q.handle = (obj_handle_t)cb;
        q.arg = k < 4 ? k : 99;
        wm_call(130, &q);
        if (k < 4) {
            char o[32];
            snprintf(o, sizeof o, "%s*1e9", names[k]);
            wm_eq(names[k], q.ret, (uint64_t)(t[k] * 1000000000.0), o);
        } else {
            wm_val("unknown", "%llu", (unsigned long long)q.ret);
        }
    }
}

/* 128 on a thread with no autorelease pool, as a queue's completion thread calls it (the C's comment): the image's own
 * table entry (wm_call wraps every call in a probe pool), 64 calls a cycle on one long-lived thread, whose implicit
 * pool drains only when it exits. The handler's own pool frees each NSData ("freed"); without it they pile up. */
typedef unsigned (*raw_handler)(void *);
static struct {
    raw_handler fn;
    id<MTLCounterSampleBuffer> sb;
    dispatch_semaphore_t go, done;
    bool quit;
    unsigned words;
} np;
static uint64_t np_out[512];

static void *no_pool_thread(void *arg) {
    for (;;) {
        dispatch_semaphore_wait(np.go, DISPATCH_TIME_FOREVER);
        if (np.quit) return NULL;
        for (unsigned i = 0; i < 64; i++) {
            struct unixcall_mtlcountersamplebuffer_resolvecounterrange r;
            memset(&r, WM_FILL, sizeof r);
            memset(np_out, WM_FILL, sizeof np_out);
            r.sample_buffer = (obj_handle_t)np.sb;
            r.start = 0;
            r.len = 512;
            r.data_out.ptr = np_out;
            r.data_length = sizeof np_out;
            np.fn(&r);
        }
        np.words = 0;
        for (unsigned k = 0; k < 512; k++) np.words += np_out[k] != WM_FILL64;
        dispatch_semaphore_signal(np.done);
    }
}

static void resolve_no_pool(void) {
    id<MTLCounterSampleBuffer> sb = timestamp_buffer(wm_device, 512, 1);
    /* Once through wm_call, so the lines name 128. */
    struct unixcall_mtlcountersamplebuffer_resolvecounterrange r;
    wm_fill(&r, sizeof r);
    r.sample_buffer = (obj_handle_t)sb;
    r.start = 0;
    r.len = 512;
    r.data_out.ptr = NULL;
    r.data_length = sizeof np_out;
    wm_call(128, &r);
    raw_handler const *funcs = wm_sym("__wine_unix_call_funcs");
    if (!funcs) {
        wm_line("no-table");
        return;
    }
    np.fn = funcs[128];
    np.sb = sb;
    np.go = dispatch_semaphore_create(0);
    np.done = dispatch_semaphore_create(0);
    np.quit = false;
    pthread_t t;
    if (pthread_create(&t, NULL, no_pool_thread, NULL)) {
        wm_line("no-thread");
        return;
    }
    wm_heap_growth(^{
        dispatch_semaphore_signal(np.go);
        dispatch_semaphore_wait(np.done, DISPATCH_TIME_FOREVER);
    }, 8);
    wm_val("words", "%u", np.words);
    np.quit = true;
    dispatch_semaphore_signal(np.go);
    pthread_join(t, NULL);
    dispatch_release(np.go);
    dispatch_release(np.done);
}

/* ---- nil ---- */

static void nil_args(void) {
    /* Receivers 0. */
    {
        struct unixcall_generic_obj_uint64_obj_ret p;
        wm_fill(&p, sizeof p);
        p.handle = 0;
        p.arg = 1;
        wm_call(9, &p);
        wm_val("ret", "0x%llx", (unsigned long long)p.ret);
    }
    for (unsigned slot = 11; slot <= 30; slot += 19) {
        struct unixcall_generic_obj_obj_ret p;
        wm_fill(&p, sizeof p);
        p.handle = 0;
        wm_call(slot, &p);
        wm_val("ret", "0x%llx", (unsigned long long)p.ret);
    }
    for (unsigned slot = 12; slot <= 13; slot++) {
        struct unixcall_generic_obj_noret n = {0};
        wm_call(slot, &n);
    }
    {
        struct unixcall_generic_obj_uint64_ret s;
        wm_fill(&s, sizeof s);
        s.handle = 0;
        wm_call(14, &s);
        wm_val("ret", "0x%llx", (unsigned long long)s.ret);
    }
    for (unsigned slot = 17; slot <= 102; slot += 85) {
        struct unixcall_generic_obj_obj_uint64_noret p = {0, 0, 1};
        wm_call(slot, &p);
    }
    {
        struct unixcall_generic_obj_uint64_obj_ret p;
        wm_fill(&p, sizeof p);
        p.handle = 0;
        p.arg = 1;
        wm_call(31, &p);
        wm_val("ret", "0x%llx", (unsigned long long)p.ret);
        struct WMTRenderPassInfo info;
        memset(&info, 0, sizeof info);
        wm_fill(&p, sizeof p);
        p.handle = 0;
        p.arg = (uint64_t)&info;
        wm_call(32, &p);
        wm_val("ret", "0x%llx", (unsigned long long)p.ret);
        struct unixcall_generic_obj_noret n = {0};
        wm_call(33, &n);
    }
    {
        CMD(wmtcmd_blit_fillbuffer, b, WMTBlitCommandFillBuffer);
        b.buffer = so();
        b.offset = 0, b.length = 4, b.value = 1;
        encode_on(36, nil, &b);
        CMD(wmtcmd_compute_dispatch, c, WMTComputeCommandDispatch);
        c.size = (struct WMTSize){1, 1, 1};
        encode_on(37, nil, &c);
        CMD(wmtcmd_render_draw, r, WMTRenderCommandDraw);
        r.primitive_type = 3, r.vertex_start = 0, r.vertex_count = 3, r.instance_count = 1, r.base_instance = 0;
        encode_on(38, nil, &r);
    }
    {
        struct unixcall_generic_obj_obj_noret p = {0, 0};
        wm_call(47, &p);
        struct unixcall_generic_obj_obj_double_noret q = {0, 0, 0.5};
        wm_call(48, &q);
    }
    for (unsigned slot = 89; slot <= 90; slot++) {
        struct unixcall_generic_obj_obj_ret p;
        wm_fill(&p, sizeof p);
        p.handle = 0;
        wm_call(slot, &p);
        wm_val("ret", "0x%llx", (unsigned long long)p.ret);
    }
    {
        obj_handle_t buf[2];
        wm_fill(buf, sizeof buf);
        struct unixcall_enumerate p;
        wm_fill(&p, sizeof p);
        p.enumerable = 0;
        p.start = 0;
        p.buffer_size = 2;
        p.buffer.ptr = buf;
        wm_call(91, &p);
        wm_val("read", "%llu", (unsigned long long)p.ret_read);
        wm_bytes("buffer", buf, sizeof buf);
    }
    {
        struct unixcall_mtlcountersamplebuffer_newtimestampbuffer p;
        wm_fill(&p, sizeof p);
        p.device = 0;
        p.sample_count = 2;
        p.shared = 1;
        wm_call(127, &p);
        wm_val("ret", "0x%llx", (unsigned long long)p.ret);
        uint8_t data[16];
        wm_fill(data, sizeof data);
        struct unixcall_mtlcountersamplebuffer_resolvecounterrange r;
        wm_fill(&r, sizeof r);
        r.sample_buffer = 0;
        r.start = 0, r.len = 2;
        r.data_out.ptr = data;
        r.data_length = sizeof data;
        wm_call(128, &r);
        wm_bytes("data", data, sizeof data);
    }
    {
        struct WMTSampleBufferAttachmentInfo att = {0, 0, 1};
        struct unixcall_mtlcommandbuffer_blitcommandencoderwithsamplebuffers b;
        wm_fill(&b, sizeof b);
        b.cmdbuf = 0;
        b.attachments.ptr = &att;
        b.num_attachments = 1;
        wm_call(129, &b);
        wm_val("ret", "0x%llx", (unsigned long long)b.ret);
        struct unixcall_generic_obj_uint64_uint64_ret q;
        wm_fill(&q, sizeof q);
        q.handle = 0;
        q.arg = 2;
        wm_call(130, &q);
        wm_val("ret", "%llu", (unsigned long long)q.ret);
        struct unixcall_mtlcommandbuffer_computecommandencoderwithsamplebuffers c;
        wm_fill(&c, sizeof c);
        c.cmdbuf = 0;
        c.attachments.ptr = &att;
        c.num_attachments = 1;
        c.concurrent = 0;
        wm_call(147, &c);
        wm_val("ret", "0x%llx", (unsigned long long)c.ret);
    }
    /* Arguments 0, sent on as nil: events, drawables, sample buffers, and each command's objects. */
    {
        id rcb = wm_rec(nil, @protocol(MTLCommandBuffer));
        wm_in(rcb);
        struct unixcall_generic_obj_obj_uint64_noret e = {(obj_handle_t)rcb, 0, 3};
        wm_call(17, &e);
        e = (struct unixcall_generic_obj_obj_uint64_noret){(obj_handle_t)rcb, 0, 4};
        wm_call(102, &e);
        struct unixcall_generic_obj_obj_noret p = {(obj_handle_t)rcb, 0};
        wm_call(47, &p);
        struct unixcall_generic_obj_obj_double_noret q = {(obj_handle_t)rcb, 0, 0.5};
        wm_call(48, &q);
        struct WMTSampleBufferAttachmentInfo att = {0, 5, 6};
        struct unixcall_mtlcommandbuffer_blitcommandencoderwithsamplebuffers b;
        wm_fill(&b, sizeof b);
        b.cmdbuf = (obj_handle_t)rcb;
        b.attachments.ptr = &att;
        b.num_attachments = 1;
        wm_call(129, &b);
        struct unixcall_mtlcommandbuffer_computecommandencoderwithsamplebuffers c;
        wm_fill(&c, sizeof c);
        c.cmdbuf = (obj_handle_t)rcb;
        c.attachments.ptr = &att;
        c.num_attachments = 1;
        c.concurrent = 1;
        wm_call(147, &c);
    }
    {
        CMD(wmtcmd_blit_fence_op, a, WMTBlitCommandUpdateFence);
        a.fence = 0;
        CMD(wmtcmd_blit_fence_op, b, WMTBlitCommandWaitForFence);
        b.fence = 0;
        CMD(wmtcmd_blit_resolvecounters, r, WMTBlitCommandResolveCounters);
        r.sample_buffer = 0, r.start = 0, r.len = 1, r.dst_buffer = 0, r.dst_offset = 0;
        CMD(wmtcmd_blit_copy_texture, t, WMTBlitCommandCopyTexture);
        t.src = 0, t.dst = 0;
        chain(&a, &b);
        chain(&b, &r);
        chain(&r, &t);
        BLIT(a);
    }
    {
        CMD(wmtcmd_compute_setpso, s, WMTComputeCommandSetPSO);
        s.pso = 0;
        s.threadgroup_size = (struct WMTSize){1, 1, 1};
        CMD(wmtcmd_compute_setbuffer, b, WMTComputeCommandSetBuffer);
        b.buffer = 0, b.offset = 0, b.index = 0;
        CMD(wmtcmd_compute_settexture, t, WMTComputeCommandSetTexture);
        t.texture = 0, t.index = 1;
        CMD(wmtcmd_compute_fence_op, f, WMTComputeCommandUpdateFence);
        f.fence = 0;
        chain(&s, &b);
        chain(&b, &t);
        chain(&t, &f);
        COMPUTE(s);
    }
    {
        CMD(wmtcmd_render_setpso, s, WMTRenderCommandSetPSO);
        s.pso = 0;
        CMD(wmtcmd_render_setbuffer, b, WMTRenderCommandSetVertexBuffer);
        b.buffer = 0, b.offset = 0, b.index = 0;
        CMD(wmtcmd_render_settexture, t, WMTRenderCommandSetFragmentTexture);
        t.texture = 0, t.index = 0;
        CMD(wmtcmd_render_setsampler, m, WMTRenderCommandSetFragmentSamplerState);
        m.sampler = 0, m.index = 0;
        CMD(wmtcmd_render_setdsso, d, WMTRenderCommandSetDSSO);
        d.dsso = 0, d.stencil_ref = 0;
        CMD(wmtcmd_render_fence_op, f, WMTRenderCommandWaitForFence);
        f.fence = 0, f.stages = WMTRenderStageFragment;
        chain(&s, &b);
        chain(&b, &t);
        chain(&t, &m);
        chain(&m, &d);
        chain(&d, &f);
        RENDER(s);
    }
}

/* ---- raises ---- */

static void x_blit_on_compute_encoder(void) {
    id<MTLBuffer> buf = [[wm_device newBufferWithLength:64 options:MTLResourceStorageModeShared] autorelease];
    id<MTLCommandBuffer> cb = [wm_queue commandBuffer];
    id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
    CMD(wmtcmd_blit_fillbuffer, f, WMTBlitCommandFillBuffer);
    f.buffer = (obj_handle_t)buf;
    f.offset = 0, f.length = 16, f.value = 7;
    encode_on(36, enc, &f);
    [enc endEncoding];
}

/* ---- the present hook (WM_NATIVE; last: the hook is the process's) ---- */

static void hook(void *cmdbuf, void *drawable) {
    wm_line("hook cmdbuf=%s drawable=%s", wm_hs((uint64_t)cmdbuf), wm_hs((uint64_t)drawable));
}

static void present_hook(void) {
    void (*set)(void (*)(void *, void *)) = wm_sym("WMTSetPresentHook");
    if (!set) {
        wm_line("no-hook");
        return;
    }
    set(hook);
    id rcb = wm_rec(nil, @protocol(MTLCommandBuffer));
    wm_in(rcb);
    obj_handle_t drawable = so();
    wm_line("cmdbuf=%s drawable=%s", wm_hs((uint64_t)rcb), wm_hs(drawable));
    struct unixcall_generic_obj_obj_noret p = {(obj_handle_t)rcb, drawable};
    wm_call(47, &p);
    struct unixcall_generic_obj_obj_double_noret q = {(obj_handle_t)rcb, drawable, 0.5};
    wm_call(48, &q);
    set(NULL);
}

const struct wm_case wm_cases_g3[] = {
    {"g3", 9, "queue", 0, queue},
    {"g3", 11, "cmdbuf-lifecycle", 0, cmdbuf_lifecycle},
    {"g3", 17, "signal-event", 0, signal_event},
    {"g3", 30, "encoders", 0, encoders},
    {"g3", 36, "blit-Nop", 0, blit_nop},
    {"g3", 36, "blit-CopyFromBufferToBuffer", 0, blit_copy_buffer_buffer},
    {"g3", 36, "blit-CopyFromBufferToTexture", 0, blit_copy_buffer_texture},
    {"g3", 36, "blit-CopyFromTextureToBuffer", 0, blit_copy_texture_buffer},
    {"g3", 36, "blit-CopyFromTextureToTexture", 0, blit_copy_texture_texture},
    {"g3", 36, "blit-GenerateMipmaps", 0, blit_generate_mipmaps},
    {"g3", 36, "blit-WaitForFence", 0, blit_wait_for_fence},
    {"g3", 36, "blit-UpdateFence", 0, blit_update_fence},
    {"g3", 36, "blit-FillBuffer", 0, blit_fill_buffer},
    {"g3", 36, "blit-ResolveCounters", 0, blit_resolve_counters},
    {"g3", 36, "blit-CopyFromBufferToTextureWithBlitOption", 0, blit_copy_buffer_texture_option},
    {"g3", 36, "blit-CopyFromTextureToBufferWithBlitOption", 0, blit_copy_texture_buffer_option},
    {"g3", 36, "blit-ResetCommandsInBuffer", 0, blit_reset_commands},
    {"g3", 36, "blit-CopyTexture", 0, blit_copy_texture},
    {"g3", 36, "blit-unknown-type", 0, blit_unknown_type},
    {"g3", 36, "blit-gpu", 0, blit_gpu},
    {"g3", 37, "compute-Nop", 0, compute_nop},
    {"g3", 37, "compute-Dispatch", 0, compute_dispatch},
    {"g3", 37, "compute-DispatchIndirect", 0, compute_dispatch_indirect},
    {"g3", 37, "compute-SetPSO", 0, compute_set_pso},
    {"g3", 37, "compute-SetBuffer", 0, compute_set_buffer},
    {"g3", 37, "compute-SetBufferOffset", 0, compute_set_buffer_offset},
    {"g3", 37, "compute-UseResource", 0, compute_use_resource},
    {"g3", 37, "compute-SetBytes", 0, compute_set_bytes},
    {"g3", 37, "compute-SetTexture", 0, compute_set_texture},
    {"g3", 37, "compute-DispatchThreads", 0, compute_dispatch_threads},
    {"g3", 37, "compute-WaitForFence", 0, compute_wait_for_fence},
    {"g3", 37, "compute-UpdateFence", 0, compute_update_fence},
    {"g3", 37, "compute-MemoryBarrier", 0, compute_memory_barrier},
    {"g3", 37, "compute-ExecuteCommandsInBuffer", 0, compute_execute_commands},
    {"g3", 37, "compute-threadgroup", 0, compute_threadgroup},
    {"g3", 37, "compute-unknown-type", 0, compute_unknown_type},
    {"g3", 37, "compute-gpu", 0, compute_gpu},
    {"g3", 38, "render-Nop", 0, render_nop},
    {"g3", 38, "render-UseResource", 0, render_use_resource},
    {"g3", 38, "render-SetVertexBuffer", 0, render_set_vertex_buffer},
    {"g3", 38, "render-SetVertexBufferOffset", 0, render_set_vertex_buffer_offset},
    {"g3", 38, "render-SetFragmentBuffer", 0, render_set_fragment_buffer},
    {"g3", 38, "render-SetFragmentBufferOffset", 0, render_set_fragment_buffer_offset},
    {"g3", 38, "render-SetMeshBuffer", 0, render_set_mesh_buffer},
    {"g3", 38, "render-SetMeshBufferOffset", 0, render_set_mesh_buffer_offset},
    {"g3", 38, "render-SetObjectBuffer", 0, render_set_object_buffer},
    {"g3", 38, "render-SetObjectBufferOffset", 0, render_set_object_buffer_offset},
    {"g3", 38, "render-SetFragmentTexture", 0, render_set_fragment_texture},
    {"g3", 38, "render-SetFragmentBytes", 0, render_set_fragment_bytes},
    {"g3", 38, "render-SetRasterizerState", 0, render_set_rasterizer_state},
    {"g3", 38, "render-SetViewports", 0, render_set_viewports},
    {"g3", 38, "render-SetScissorRects", 0, render_set_scissor_rects},
    {"g3", 38, "render-SetPSO", 0, render_set_pso},
    {"g3", 38, "render-SetDSSO", 0, render_set_dsso},
    {"g3", 38, "render-SetBlendFactorAndStencilRef", 0, render_set_blend_factor_and_stencil_ref},
    {"g3", 38, "render-SetVisibilityMode", 0, render_set_visibility_mode},
    {"g3", 38, "render-Draw", 0, render_draw},
    {"g3", 38, "render-DrawIndexed", 0, render_draw_indexed},
    {"g3", 38, "render-DrawIndirect", 0, render_draw_indirect},
    {"g3", 38, "render-DrawIndexedIndirect", 0, render_draw_indexed_indirect},
    {"g3", 38, "render-DrawMeshThreadgroups", 0, render_draw_mesh_threadgroups},
    {"g3", 38, "render-DrawMeshThreadgroupsIndirect", 0, render_draw_mesh_threadgroups_indirect},
    {"g3", 38, "render-MemoryBarrier", 0, render_memory_barrier},
    {"g3", 38, "render-DXMTGeometryDraw", 0, render_geometry_draw},
    {"g3", 38, "render-DXMTGeometryDrawIndexed", 0, render_geometry_draw_indexed},
    {"g3", 38, "render-DXMTGeometryDrawIndirect", 0, render_geometry_draw_indirect},
    {"g3", 38, "render-DXMTGeometryDrawIndexedIndirect", 0, render_geometry_draw_indexed_indirect},
    {"g3", 38, "render-WaitForFence", 0, render_wait_for_fence},
    {"g3", 38, "render-UpdateFence", 0, render_update_fence},
    {"g3", 38, "render-SetViewport", 0, render_set_viewport},
    {"g3", 38, "render-SetScissorRect", 0, render_set_scissor_rect},
    {"g3", 38, "render-DXMTTessellationMeshDraw", 0, render_tess_draw},
    {"g3", 38, "render-DXMTTessellationMeshDrawIndexed", 0, render_tess_draw_indexed},
    {"g3", 38, "render-DXMTTessellationMeshDrawIndirect", 0, render_tess_draw_indirect},
    {"g3", 38, "render-DXMTTessellationMeshDrawIndexedIndirect", 0, render_tess_draw_indexed_indirect},
    {"g3", 38, "render-DispatchThreadsPerTile", 0, render_dispatch_threads_per_tile},
    {"g3", 38, "render-ExecuteCommandsInBuffer", 0, render_execute_commands},
    {"g3", 38, "render-SetBlendFactor", 0, render_set_blend_factor},
    {"g3", 38, "render-SetStencilRef", 0, render_set_stencil_ref},
    {"g3", 38, "render-SetDepthStencilState", 0, render_set_depth_stencil_state},
    {"g3", 38, "render-SetVertexBytes", 0, render_set_vertex_bytes},
    {"g3", 38, "render-SetVertexTexture", 0, render_set_vertex_texture},
    {"g3", 38, "render-SetVertexSamplerState", 0, render_set_vertex_sampler},
    {"g3", 38, "render-SetFragmentSamplerState", 0, render_set_fragment_sampler},
    {"g3", 38, "render-unknown-type", 0, render_unknown_type},
    {"g3", 38, "render-gpu", 0, render_gpu},
    {"g3", 32, "renderpass", 0, renderpass},
    {"g3", 47, "present-selectors", 0, present_selectors},
    {"g3", 89, "error-logs", 0, error_logs},
    {"g3", 91, "enumerate", 0, enumerate},
    {"g3", 91, "enumerate-past-end", 0, enumerate_past_end},
    {"g3", 102, "wait-event", 0, wait_event},
    {"g3", 127, "timestamps", 0, timestamps},
    {"g3", 128, "resolve-no-pool", 0, resolve_no_pool},
    {"g3", 9, "nil-args", 0, nil_args},
    {"g3", 36, "x-blit-on-compute-encoder", 0, x_blit_on_compute_encoder},
    {"g3", 47, "present-hook", WM_NATIVE, present_hook},
    {0},
};
