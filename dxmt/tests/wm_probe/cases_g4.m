/* wm_probe's g4 cases (slots.txt's g4; probe.h), recorded from DXMT's C: libraries (25, 144, 157), functions (26, 98),
 * pipelines (29 compute, 34 render, 35 mesh, 131 tile, 156 render with a vertex descriptor) and binary archives (112,
 * 113). The pipeline slots go through a recording device (the real device behind it makes the objects): the descriptor
 * the handler builds is dumped, and a spy in front of the recorder adds the immutable-buffer masks over all 31 slots
 * (the dump shows 8). Functions and libraries the cases need come from Metal directly (runtime MSL), never from
 * these slots. Every path the recorder could print is a fixed one that does not exist (wm_tmpdir() is random per run);
 * the files a case really writes are under wm_tmpdir() and only compared. */
#include "probe.h"
#include <objc/runtime.h>
#include <string.h>
#include <sys/stat.h>

static NSString *const kMSL = @"#include <metal_stdlib>\n"
                               "using namespace metal;\n"
                               "kernel void k(device uint *b [[buffer(0)]], uint i [[thread_position_in_grid]]) { b[i] = i; }\n"
                               "kernel void k2(device uint *b [[buffer(0)]], uint i [[thread_position_in_grid]]) { b[i] = i * 2; }\n"
                               "vertex float4 v(uint i [[vertex_id]]) { return float4(0, 0, 0, 1); }\n"
                               "fragment half4 f() { return half4(1); }\n"
                               "struct VIn { float4 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; };\n"
                               "vertex float4 vin(VIn in [[stage_in]]) { return in.pos + float4(in.uv, 0, 0); }\n";

static NSString *const kMSLConstants =
    @"#include <metal_stdlib>\n"
     "using namespace metal;\n"
     "constant int ci [[function_constant(0)]];\n"
     "constant float cf [[function_constant(1)]];\n"
     "constant bool cb [[function_constant(2)]];\n"
     "constant uint cu [[function_constant(3)]];\n"
     "constant float4 c4 [[function_constant(4)]];\n"
     "kernel void kc(device int *o [[buffer(0)]], uint i [[thread_position_in_grid]]) {\n"
     "  o[i] = ci + int(cf) + (cb ? 1 : 0) + int(cu) + int(c4.x);\n"
     "}\n";

static NSString *const kMSLMesh =
    @"#include <metal_stdlib>\n"
     "#include <metal_mesh>\n"
     "using namespace metal;\n"
     "struct MV { float4 position [[position]]; };\n"
     "using MM = mesh<MV, void, 3, 1, topology::triangle>;\n"
     "[[mesh]] void mshf(MM m, uint tid [[thread_index_in_threadgroup]]) {\n"
     "  if (tid < 3) { MV v; v.position = float4(float(tid), 0, 0, 1); m.set_vertex(tid, v); m.set_index(tid, tid); }\n"
     "  if (tid == 0) m.set_primitive_count(1);\n"
     "}\n"
     "fragment half4 mf() { return half4(1); }\n";

static NSString *const kMSLTile =
    @"#include <metal_stdlib>\n"
     "using namespace metal;\n"
     "struct TD { half4 c [[color(0)]]; };\n"
     "kernel void tf(imageblock<TD, imageblock_layout_implicit> ib, ushort2 tid [[thread_position_in_threadgroup]]) {\n"
     "}\n";

/* ---- the spy: a device proxy that adds the immutable-buffer masks of a pipeline descriptor, then passes on ---- */

@interface WMG4Spy : NSProxy {
  @public
    id inner;
}
@end

@implementation WMG4Spy
- (void)dealloc {
    [inner release];
    [super dealloc];
}
- (NSMethodSignature *)methodSignatureForSelector:(SEL)s {
    return [inner methodSignatureForSelector:s];
}
- (BOOL)respondsToSelector:(SEL)s {
    return [inner respondsToSelector:s];
}
- (void)forwardInvocation:(NSInvocation *)inv {
    NSString *sel = NSStringFromSelector(inv.selector);
    if ([sel hasPrefix:@"new"] && [sel containsString:@"PipelineState"] && inv.methodSignature.numberOfArguments > 2) {
        id d = nil;
        if (*[inv.methodSignature getArgumentTypeAtIndex:2] == '@') [inv getArgument:&d atIndex:2];
        NSString *cls = d ? @(object_getClassName(d)) : @"";
        NSArray *names = [cls containsString:@"Mesh"]      ? @[ @"objectBuffers", @"meshBuffers", @"fragmentBuffers" ]
                         : [cls containsString:@"Tile"]    ? @[ @"tileBuffers" ]
                         : [cls containsString:@"Compute"] ? @[ @"buffers" ]
                                                           : @[ @"vertexBuffers", @"fragmentBuffers" ];
        for (NSString *name in names) {
            id array = [d valueForKey:name];
            unsigned mask = 0;
            for (unsigned i = 0; i < 31; i++)
                if ([[[array objectAtIndexedSubscript:i] valueForKey:@"mutability"] intValue] == MTLMutabilityImmutable) mask |= 1u << i;
            wm_line("immutable %s=0x%x", name.UTF8String, mask);
        }
    }
    [inv invokeWithTarget:inner];
}
@end

/* A recording device with the spy in front. */
static id spy_device(void) {
    WMG4Spy *s = [WMG4Spy alloc];
    s->inner = [wm_rec(wm_device, @protocol(MTLDevice)) retain];
    return [s autorelease];
}

/* ---- helpers ---- */

static id<MTLLibrary> shared_lib(void) { return wm_library(kMSL); }

static id<MTLFunction> fn(id<MTLLibrary> lib, NSString *name) { return [[lib newFunctionWithName:name] autorelease]; }

static const char *nz(uint64_t h) { return !h ? "0" : h == WM_FILL64 ? "fill" : "nonnull"; }

/* The error object a handler gave back: its domain and code, and the text when the case wants it. */
static void error_of(const char *name, uint64_t h, bool text) {
    if (!h || h == WM_FILL64) {
        wm_val(name, "%s", nz(h));
        return;
    }
    NSError *e = (NSError *)h;
    wm_val(name, "%s/%ld", e.domain.UTF8String, (long)e.code);
    if (text) wm_val("error-text", "%s", e.localizedDescription.UTF8String);
}

/* A WMTColorAttachmentBlendInfo varied by index, valid for the formats used. */
static void color_info(struct WMTColorAttachmentBlendInfo *c, unsigned i) {
    static const uint32_t formats[] = {WMTPixelFormatBGRA8Unorm, WMTPixelFormatRGBA8Unorm, WMTPixelFormatRGBA16Float,
                                       WMTPixelFormatRGB10A2Unorm, WMTPixelFormatRG11B10Float, WMTPixelFormatRGBA8Unorm_sRGB,
                                       WMTPixelFormatR16Float, WMTPixelFormatRG8Unorm};
    c->pixel_format = formats[i];
    c->rgb_blend_operation = (enum WMTBlendOperation)(i % 5);
    c->alpha_blend_operation = (enum WMTBlendOperation)((i + 2) % 5);
    c->src_rgb_blend_factor = (enum WMTBlendFactor)(i + 2);
    c->dst_rgb_blend_factor = (enum WMTBlendFactor)(i + 4);
    c->src_alpha_blend_factor = (enum WMTBlendFactor)(i + 6);
    c->dst_alpha_blend_factor = (enum WMTBlendFactor)(i + 8);
    c->write_mask = (uint8_t)(15 - i);
    c->blending_enabled = i % 2 == 0;
}

/* ---- 25, 26 ---- */

/* 25 over data the probe made: a metallib (the presenter's, which is built beside the image) gives a library, junk an
 * error +1. */
static void library_data(void) {
    const char *presenter = getenv("WM_PRESENTER"); /* as the probe's present_copy takes it */
    NSString *metallib = [@(presenter ? presenter : WM_PRESENTER_DEFAULT) stringByDeletingPathExtension];
    NSData *lib = [NSData dataWithContentsOfFile:[metallib stringByAppendingPathExtension:@"metallib"]];
    NSData *junk = [NSData dataWithBytes:"this is not a metallib at all, just text" length:39];
    NSData *inputs[] = {lib, junk};
    const char *names[] = {"metallib", "junk"};
    for (unsigned i = 0; i < 2; i++) {
        wm_line("data=%s", names[i]);
        if (!inputs[i]) {
            wm_val("data", "missing");
            continue;
        }
        dispatch_data_t d = dispatch_data_create(inputs[i].bytes, inputs[i].length, NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
        struct unixcall_mtldevice_newlibrary p;
        wm_fill(&p, sizeof p);
        p.device = (obj_handle_t)wm_device;
        p.data = (obj_handle_t)d;
        wm_own("ret_library", &p.ret_library);
        wm_own("ret_error", &p.ret_error);
        wm_in((id)d);
        wm_call(25, &p);
        error_of("error", p.ret_error, i == 1); /* the junk data's error with its text */
        if (p.ret_library && p.ret_library != WM_FILL64) {
            NSArray *names = [(id<MTLLibrary>)p.ret_library functionNames];
            wm_val("functions", "%s", names.count ? "some" : "none");
        }
        [(id)d release];
    }
}

/* 26 on a library: a function +1 for a name it has, 0 for one it hasn't. */
static void function(void) {
    id<MTLLibrary> lib = shared_lib();
    static const char *const names[] = {"k", "v", "f", "no_such_function", "", "K"};
    for (unsigned i = 0; i < sizeof names / sizeof *names; i++) {
        struct unixcall_generic_obj_uint64_obj_ret p;
        wm_fill(&p, sizeof p);
        p.handle = (uint64_t)lib;
        p.arg = (uint64_t)names[i];
        wm_line("name=%s", names[i]);
        wm_own("ret", &p.ret);
        wm_call(26, &p);
        if (p.ret && p.ret != WM_FILL64) wm_val("function-name", "%s", [(id<MTLFunction>)p.ret name].UTF8String);
    }
}

/* 26 with a device where the library should be: unrecognized selector. */
static void x_library_on_device(void) {
    struct unixcall_generic_obj_uint64_obj_ret p;
    wm_fill(&p, sizeof p);
    p.handle = (uint64_t)wm_device;
    p.arg = (uint64_t) "k";
    wm_call(26, &p);
    wm_bytes("ret", &p.ret, sizeof p.ret);
    struct unixcall_mtldevice_newbinaryarchive q;
    wm_fill(&q, sizeof q);
    q.device = (uint64_t)wm_queue;
    q.url.ptr = NULL;
    wm_call(112, &q);
    wm_bytes("ret_archive", &q.ret_archive, sizeof q.ret_archive);
    wm_bytes("ret_error", &q.ret_error, sizeof q.ret_error);
}

/* ---- 29 ---- */

static void compute_info(struct WMTComputePipelineInfo *info, id<MTLFunction> f) {
    memset(info, 0, sizeof *info);
    info->compute_function = (obj_handle_t)f;
}

/* 29: a plain compute pipeline, then each flag and immutable bit set, with the archives off and on. */
static void compute_pso(void) {
    id<MTLLibrary> lib = shared_lib();
    id<MTLFunction> k = fn(lib, @"k");
    id dev = spy_device();
    id<MTLBinaryArchive> archive = [[wm_device newBinaryArchiveWithDescriptor:[[[MTLBinaryArchiveDescriptor alloc] init] autorelease] error:nil] autorelease];
    obj_handle_t archives[2] = {(obj_handle_t)archive, 0};
    struct {
        const char *what;
        bool tg, icb, miss;
        uint32_t immutable;
        int lookups;
        bool serialize;
    } runs[] = {
        {"plain", false, false, false, 0, 0, false},
        {"flags", true, true, false, 0, 0, false},
        {"immutable-low", false, false, false, 0x0000000f, 0, false},
        {"immutable-high", false, false, false, 0x7ffffff0, 0, false},
        {"immutable-bit-31", false, false, false, 0xffffffff, 0, false},
        {"lookup", false, false, false, 0, 1, false},
        {"lookup-miss-fails", false, false, true, 0, 1, false},
        {"lookup-null-array", false, false, false, 0, -1, false},
        {"serialize", true, false, false, 0x00000101, 1, true},
    };
    for (unsigned i = 0; i < sizeof runs / sizeof *runs; i++) {
        struct WMTComputePipelineInfo info;
        compute_info(&info, k);
        info.tgsize_is_multiple_of_sgwidth = runs[i].tg;
        info.support_indirect_command_buffers = runs[i].icb;
        info.fail_on_binary_archive_miss = runs[i].miss;
        info.immutable_buffers = runs[i].immutable;
        if (runs[i].lookups > 0) {
            info.binary_archives_for_lookup.ptr = archives;
            info.num_binary_archives_for_lookup = (uint8_t)runs[i].lookups;
        } else if (runs[i].lookups < 0) {
            info.num_binary_archives_for_lookup = 1;
        }
        if (runs[i].serialize) info.binary_archive_for_serialization = (obj_handle_t)archive;
        struct unixcall_mtldevice_newcomputepso p;
        wm_fill(&p, sizeof p);
        p.device = (obj_handle_t)dev;
        p.info.ptr = &info;
        wm_line("run=%s", runs[i].what);
        wm_own("ret_pso", &p.ret_pso);
        wm_own("ret_error", &p.ret_error);
        wm_call(29, &p);
        error_of("error", p.ret_error, false);
    }
}

/* 29 without archives and with: the handler adds the function to the serialization archive after success, and drops that
 * call's error. A pipeline from an archive that holds it, and one asked of an archive that doesn't (FailOnMiss). */
static void archive_pipelines(void) {
    id<MTLLibrary> lib = shared_lib();
    id<MTLFunction> k = fn(lib, @"k"), k2 = fn(lib, @"k2");
    /* The archive is made and serialized through slots 112 and 113 (this case's group), the pipeline through 29. */
    NSString *file = [NSString stringWithFormat:@"%s/archive-pipelines.bin", wm_tmpdir()];
    struct unixcall_mtldevice_newbinaryarchive a;
    wm_fill(&a, sizeof a);
    a.device = (uint64_t)wm_device;
    a.url.ptr = NULL;
    wm_call(112, &a);
    if (!a.ret_archive || a.ret_archive == WM_FILL64) return;
    struct WMTComputePipelineInfo info;
    compute_info(&info, k);
    info.binary_archive_for_serialization = a.ret_archive;
    struct unixcall_mtldevice_newcomputepso p;
    wm_fill(&p, sizeof p);
    p.device = (obj_handle_t)wm_device;
    p.info.ptr = &info;
    wm_call(29, &p);
    error_of("error", p.ret_error, false);
    struct unixcall_mtlbinaryarchive_serialize s;
    wm_fill(&s, sizeof s);
    s.archive = a.ret_archive;
    s.url.ptr = file.fileSystemRepresentation;
    wm_call(113, &s);
    error_of("serialize-error", s.ret_error, false);
    struct stat st;
    wm_val("serialized", "%s", !stat(file.fileSystemRepresentation, &st) && st.st_size > 0 ? "yes" : "no");
    /* Load the archive back through 112 with its URL, and look the pipelines up in it. */
    struct unixcall_mtldevice_newbinaryarchive l;
    wm_fill(&l, sizeof l);
    l.device = (uint64_t)wm_device;
    l.url.ptr = file.fileSystemRepresentation;
    wm_own("ret_archive", &l.ret_archive);
    wm_call(112, &l);
    error_of("load-error", l.ret_error, false);
    if (!l.ret_archive || l.ret_archive == WM_FILL64) return;
    obj_handle_t archives[1] = {l.ret_archive};
    for (unsigned i = 0; i < 2; i++) {
        struct WMTComputePipelineInfo lookup;
        compute_info(&lookup, i ? k2 : k);
        lookup.binary_archives_for_lookup.ptr = archives;
        lookup.num_binary_archives_for_lookup = 1;
        lookup.fail_on_binary_archive_miss = true;
        struct unixcall_mtldevice_newcomputepso q;
        wm_fill(&q, sizeof q);
        q.device = (obj_handle_t)wm_device;
        q.info.ptr = &lookup;
        wm_line("lookup=%s", i ? "function-not-in-archive" : "function-in-archive");
        wm_call(29, &q);
        wm_val("ret_pso", "%s", nz(q.ret_pso));
        error_of("error", q.ret_error, false);
    }
}

/* ---- 34, 156 ---- */

static void render_info(struct WMTRenderPipelineInfo *info, id<MTLFunction> vf, id<MTLFunction> ff) {
    memset(info, 0, sizeof *info);
    info->colors[0].pixel_format = WMTPixelFormatBGRA8Unorm;
    info->colors[0].write_mask = 15;
    info->rasterization_enabled = true;
    info->raster_sample_count = 1;
    info->vertex_function = (obj_handle_t)vf;
    info->fragment_function = (obj_handle_t)ff;
    info->input_primitive_topology = WMTPrimitiveTopologyClassUnspecified;
    info->max_tessellation_factor = 16; /* Metal aborts the process on a factor outside 1..64 */
}

static void render_call(unsigned slot, const char *what, id dev, struct WMTRenderPipelineInfo *info,
                        const struct WMTVertexDescriptorInfo *vertex) {
    wm_line("run=%s", what);
    if (slot == 34) {
        struct unixcall_mtldevice_newrenderpso p;
        wm_fill(&p, sizeof p);
        p.device = (obj_handle_t)dev;
        p.info.ptr = info;
        wm_own("ret_pso", &p.ret_pso);
        wm_own("ret_error", &p.ret_error);
        wm_call(34, &p);
        error_of("error", p.ret_error, false);
    } else {
        struct unixcall_mtldevice_newrenderpso_vertex p;
        wm_fill(&p, sizeof p);
        p.device = (obj_handle_t)dev;
        p.info.ptr = info;
        p.vertex.ptr = vertex;
        wm_own("ret_pso", &p.ret_pso);
        wm_own("ret_error", &p.ret_error);
        wm_call(156, &p);
        error_of("error", p.ret_error, false);
    }
}

/* 34: a plain pipeline; all eight attachments varied (formats, blend ops and factors, write masks); the logic operation
 * (private selectors) on and off; rasterization off; multisampling and alpha to coverage; depth and stencil formats; the
 * tessellation fields; the immutable bits of both buffer arrays; the archives. */
static void render_pso(void) {
    id<MTLLibrary> lib = shared_lib();
    id<MTLFunction> vf = fn(lib, @"v"), ff = fn(lib, @"f");
    id dev = spy_device();
    struct WMTRenderPipelineInfo info;
    render_info(&info, vf, ff);
    render_call(34, "plain", dev, &info, NULL);
    render_info(&info, vf, ff);
    for (unsigned i = 0; i < 8; i++) color_info(&info.colors[i], i);
    render_call(34, "attachments-0-7", dev, &info, NULL);
    render_info(&info, vf, ff);
    info.logic_operation_enabled = true;
    info.logic_operation = WMTLogicOperationXor;
    render_call(34, "logic-op-xor", dev, &info, NULL);
    info.logic_operation = WMTLogicOperationOrInverted;
    render_call(34, "logic-op-or-inverted", dev, &info, NULL);
    render_info(&info, vf, ff);
    info.logic_operation_enabled = false;
    info.logic_operation = WMTLogicOperationNand;
    render_call(34, "logic-op-off", dev, &info, NULL);
    render_info(&info, vf, NULL);
    info.rasterization_enabled = false;
    render_call(34, "rasterization-off", dev, &info, NULL);
    render_info(&info, vf, ff);
    info.raster_sample_count = 4;
    info.alpha_to_coverage_enabled = true;
    info.depth_pixel_format = WMTPixelFormatDepth32Float;
    info.stencil_pixel_format = WMTPixelFormatStencil8;
    render_call(34, "msaa-depth-stencil", dev, &info, NULL);
    render_info(&info, vf, ff);
    info.input_primitive_topology = WMTPrimitiveTopologyClassTriangle;
    info.tessellation_partition_mode = WMTTessellationPartitionModeFractionalEven;
    info.tessellation_factor_step = WMTTessellationFactorStepFunctionPerPatchAndPerInstance;
    info.tessellation_output_winding_order = WMTWindingCounterClockwise;
    info.max_tessellation_factor = 48;
    render_call(34, "tessellation", dev, &info, NULL);
    render_info(&info, vf, ff);
    info.immutable_vertex_buffers = 0x40000081;
    info.immutable_fragment_buffers = 0x2000010a;
    info.support_indirect_command_buffers = true;
    render_call(34, "immutable-icb", dev, &info, NULL);
    render_info(&info, vf, ff);
    id<MTLBinaryArchive> archive = [[wm_device newBinaryArchiveWithDescriptor:[[[MTLBinaryArchiveDescriptor alloc] init] autorelease] error:nil] autorelease];
    obj_handle_t archives[1] = {(obj_handle_t)archive};
    info.binary_archives_for_lookup.ptr = archives;
    info.num_binary_archives_for_lookup = 1;
    info.binary_archive_for_serialization = (obj_handle_t)archive;
    render_call(34, "archives", dev, &info, NULL);
    info.fail_on_binary_archive_miss = true;
    info.binary_archive_for_serialization = 0;
    render_call(34, "archives-fail-on-miss", dev, &info, NULL);
}

/* 156: the same pipeline with a vertex descriptor, 31 attributes and layouts; a format of 0 and a stride of 0 are left
 * unset. A vertex function that reads [[stage_in]] needs it: without any attribute Metal refuses (the error). */
static void vertex_pso(void) {
    id<MTLLibrary> lib = shared_lib();
    id<MTLFunction> vin = fn(lib, @"vin"), ff = fn(lib, @"f");
    id dev = spy_device();
    struct WMTRenderPipelineInfo info;
    struct WMTVertexDescriptorInfo vd;
    memset(&vd, 0, sizeof vd);
    render_info(&info, vin, ff);
    render_call(156, "no-attributes", dev, &info, &vd);
    vd.attributes[0].format = MTLVertexFormatFloat4;
    vd.attributes[0].offset = 0;
    vd.attributes[0].buffer_index = 0;
    vd.attributes[1].format = MTLVertexFormatFloat2;
    vd.attributes[1].offset = 16;
    vd.attributes[1].buffer_index = 0;
    vd.layouts[0].stride = 24;
    vd.layouts[0].step_function = MTLVertexStepFunctionPerVertex;
    vd.layouts[0].step_rate = 1;
    render_call(156, "two-attributes", dev, &info, &vd);
    for (unsigned i = 2; i < 31; i++) {
        vd.attributes[i].format = MTLVertexFormatFloat + (i % 5);
        vd.attributes[i].offset = i * 4;
        vd.attributes[i].buffer_index = i;
        vd.layouts[i].stride = 16 + i;
        vd.layouts[i].step_function = i % 3 == 0 ? MTLVertexStepFunctionPerInstance : MTLVertexStepFunctionPerVertex;
        vd.layouts[i].step_rate = i % 3 == 0 ? 2 : 1;
    }
    /* Entries the C leaves unset (format 0, stride 0) keep their other fields out of the descriptor. */
    for (unsigned i = 5; i <= 7; i += 2) {
        vd.attributes[i].format = 0;
        vd.attributes[i].offset = 90 + i;
        vd.attributes[i].buffer_index = i;
        vd.layouts[i].stride = 0;
        vd.layouts[i].step_function = MTLVertexStepFunctionPerInstance;
        vd.layouts[i].step_rate = 7;
    }
    vd.layouts[30].step_rate = 3;
    render_call(156, "all-31", dev, &info, &vd);
    render_info(&info, vin, ff);
    info.logic_operation_enabled = true;
    info.logic_operation = WMTLogicOperationInvert;
    info.immutable_vertex_buffers = 0x00000003;
    render_call(156, "logic-op-immutable", dev, &info, &vd);
    render_info(&info, fn(lib, @"v"), ff);
    render_call(156, "function-without-stage-in", dev, &info, &vd);
}

/* ---- 35, 131 ---- */

/* 35: a mesh pipeline (the object function off), the descriptor dumped. */
static void mesh_pso(void) {
    id<MTLLibrary> lib = wm_library(kMSLMesh);
    if (!lib) return;
    id<MTLFunction> mf = fn(lib, @"mshf"), ff = fn(lib, @"mf");
    id dev = spy_device();
    struct WMTMeshRenderPipelineInfo info;
    memset(&info, 0, sizeof info);
    info.colors[0].pixel_format = WMTPixelFormatBGRA8Unorm;
    info.colors[0].write_mask = 15;
    info.rasterization_enabled = true;
    info.raster_sample_count = 1;
    info.mesh_function = (obj_handle_t)mf;
    info.fragment_function = (obj_handle_t)ff;
    for (unsigned run = 0; run < 3; run++) {
        if (run == 1) {
            for (unsigned i = 0; i < 8; i++) color_info(&info.colors[i], i);
            info.logic_operation_enabled = false;
            info.logic_operation = WMTLogicOperationNor;
            info.alpha_to_coverage_enabled = true;
            info.depth_pixel_format = WMTPixelFormatDepth32Float;
            info.payload_memory_length = 512;
            info.mesh_tgsize_is_multiple_of_sgwidth = true;
            info.object_tgsize_is_multiple_of_sgwidth = true;
            info.immutable_mesh_buffers = 0x40000005;
            info.immutable_object_buffers = 0x00000030;
            info.immutable_fragment_buffers = 0x20000006;
        } else if (run == 2) {
            memset(&info, 0, sizeof info);
            info.colors[0].pixel_format = WMTPixelFormatBGRA8Unorm;
            info.colors[0].write_mask = 15;
            info.rasterization_enabled = true;
            info.raster_sample_count = 1;
            info.mesh_function = (obj_handle_t)mf;
            info.fragment_function = (obj_handle_t)ff;
            info.logic_operation_enabled = true;
            info.logic_operation = WMTLogicOperationXor;
            info.support_indirect_command_buffers = true;
        }
        struct unixcall_mtldevice_newmeshrenderpso p;
        wm_fill(&p, sizeof p);
        p.device = (obj_handle_t)dev;
        p.info.ptr = &info;
        wm_line("run=%d", run);
        wm_own("ret_pso", &p.ret_pso);
        wm_own("ret_error", &p.ret_error);
        wm_call(35, &p);
        error_of("error", p.ret_error, false);
    }
}

/* 131: a tile pipeline, the descriptor dumped. */
static void tile_pso(void) {
    id<MTLLibrary> lib = wm_library(kMSLTile);
    id dev = spy_device();
    id<MTLFunction> tf = lib ? fn(lib, @"tf") : nil;
    struct WMTTileRenderPipelineInfo info;
    for (unsigned run = 0; run < 2; run++) { /* (a nil tile function aborts Metal's validation: no case) */
        memset(&info, 0, sizeof info);
        info.tile_function = (obj_handle_t)tf;
        info.color_formats[0] = WMTPixelFormatBGRA8Unorm;
        info.raster_sample_count = 1;
        if (run >= 1) {
            for (unsigned i = 1; i < 8; i++) info.color_formats[i] = i % 2 ? WMTPixelFormatRGBA8Unorm : WMTPixelFormatRGBA16Float;
            info.immutable_tile_buffers = 0x40000009;
            info.tgsize_matches_tile_size = true;
            info.raster_sample_count = 4;
        }
        struct unixcall_mtldevice_newrenderpso p;
        wm_fill(&p, sizeof p);
        p.device = (obj_handle_t)dev;
        p.info.ptr = &info;
        wm_line("run=%d", run);
        wm_own("ret_pso", &p.ret_pso);
        wm_own("ret_error", &p.ret_error);
        wm_call(131, &p);
        error_of("error", p.ret_error, false);
    }
}

/* ---- 98 ---- */

/* 98: a function made from constant values the handler builds from the table (each value's bytes by type, its type
 * and index are printed by the swizzle on setConstantValue:type:atIndex:); a missing constant or a wrong type is
 * Metal's error. */
static void constants(void) {
    id<MTLLibrary> lib = wm_library(kMSLConstants);
    if (!lib) return;
    wm_swizzles(true);
    int32_t i32 = -7;
    float f32 = 2.5f;
    uint8_t b8 = 1;
    uint32_t u32 = 0x01020304;
    float v4[4] = {3.0f, 4.0f, 5.0f, 6.0f};
    struct WMTFunctionConstant all[] = {
        {.data = {&i32}, .type = WMTDataTypeInt, .index = 0},
        {.data = {&f32}, .type = WMTDataTypeFloat, .index = 1},
        {.data = {&b8}, .type = WMTDataTypeBool, .index = 2},
        {.data = {&u32}, .type = WMTDataTypeUInt, .index = 3},
        {.data = {v4}, .type = WMTDataTypeFloat4, .index = 4},
    };
    struct {
        const char *what;
        const struct WMTFunctionConstant *c;
        uint64_t n;
        const char *name;
    } runs[] = {
        {"all-five", all, 5, "kc"},
        {"first-three", all, 3, "kc"},
        {"none", all, 0, "kc"},
        {"no-such-function", all, 5, "nope"},
        {"reordered", all + 1, 4, "kc"},
    };
    for (unsigned r = 0; r < sizeof runs / sizeof *runs; r++) {
        struct unixcall_mtllibrary_newfunction_with_constants p;
        wm_fill(&p, sizeof p);
        p.library = (uint64_t)lib;
        p.name.ptr = runs[r].name;
        p.constants.ptr = runs[r].n ? runs[r].c : NULL;
        p.num_constants = runs[r].n;
        wm_line("run=%s", runs[r].what);
        wm_own("ret", &p.ret);
        wm_own("ret_error", &p.ret_error);
        wm_call(98, &p);
        error_of("error", p.ret_error, false);
    }
    /* A wrong type for index 1 (an Int where the shader wants a Float), and an index the shader doesn't have. */
    struct WMTFunctionConstant wrong[] = {
        {.data = {&i32}, .type = WMTDataTypeInt, .index = 0},
        {.data = {&i32}, .type = WMTDataTypeInt, .index = 1},
        {.data = {&b8}, .type = WMTDataTypeBool, .index = 2},
        {.data = {&u32}, .type = WMTDataTypeUInt, .index = 3},
        {.data = {v4}, .type = WMTDataTypeFloat4, .index = 4},
        {.data = {&i32}, .type = WMTDataTypeInt, .index = 9},
    };
    struct unixcall_mtllibrary_newfunction_with_constants w;
    wm_fill(&w, sizeof w);
    w.library = (uint64_t)lib;
    w.name.ptr = "kc";
    w.constants.ptr = wrong;
    w.num_constants = 6;
    wm_line("run=wrong-type-extra-index");
    wm_own("ret", &w.ret);
    wm_own("ret_error", &w.ret_error);
    wm_call(98, &w);
    error_of("error", w.ret_error, false);
    wm_swizzles(false);
}

/* ---- 112, 113 ---- */

/* 112: an empty archive (no URL), one over a URL that doesn't exist (a recording device: the descriptor's URL, the
 * error +1), one over a file 113 wrote; 113: an empty archive can't be serialized (the error +1), one that 29 added a
 * pipeline to can, and a path that can't be written is an error. */
static void archive(void) {
    struct unixcall_mtldevice_newbinaryarchive e;
    wm_fill(&e, sizeof e);
    e.device = (uint64_t)wm_device;
    e.url.ptr = NULL;
    wm_own("ret_archive", &e.ret_archive);
    wm_own("ret_error", &e.ret_error);
    wm_call(112, &e);
    error_of("error", e.ret_error, false);
    id rec = wm_rec(wm_device, @protocol(MTLDevice));
    struct unixcall_mtldevice_newbinaryarchive r;
    wm_fill(&r, sizeof r);
    r.device = (uint64_t)rec;
    r.url.ptr = NULL;
    wm_call(112, &r);
    struct unixcall_mtldevice_newbinaryarchive b;
    wm_fill(&b, sizeof b);
    b.device = (uint64_t)rec;
    b.url.ptr = "/wm-probe/no such archive.bin";
    wm_own("ret_archive", &b.ret_archive);
    wm_own("ret_error", &b.ret_error);
    wm_call(112, &b);
    error_of("error", b.ret_error, true);
    if (!e.ret_archive || e.ret_archive == WM_FILL64) return;
    NSString *file = [NSString stringWithFormat:@"%s/archive.bin", wm_tmpdir()];
    struct unixcall_mtlbinaryarchive_serialize s;
    wm_fill(&s, sizeof s);
    s.archive = e.ret_archive;
    s.url.ptr = file.fileSystemRepresentation;
    wm_own("ret_error", &s.ret_error);
    wm_call(113, &s);
    error_of("empty-error", s.ret_error, false);
    struct stat st;
    wm_val("empty-file", "%s", !stat(file.fileSystemRepresentation, &st) ? "written" : "missing");
    id<MTLLibrary> lib = shared_lib();
    struct WMTComputePipelineInfo ci;
    compute_info(&ci, fn(lib, @"k"));
    ci.binary_archive_for_serialization = e.ret_archive;
    struct unixcall_mtldevice_newcomputepso c;
    wm_fill(&c, sizeof c);
    c.device = (uint64_t)wm_device;
    c.info.ptr = &ci;
    wm_call(29, &c);
    struct unixcall_mtlbinaryarchive_serialize s2;
    wm_fill(&s2, sizeof s2);
    s2.archive = e.ret_archive;
    s2.url.ptr = file.fileSystemRepresentation;
    wm_own("ret_error", &s2.ret_error);
    wm_call(113, &s2);
    error_of("error", s2.ret_error, false);
    wm_val("file", "%s", !stat(file.fileSystemRepresentation, &st) && st.st_size > 0 ? "written" : "missing");
    struct unixcall_mtldevice_newbinaryarchive l;
    wm_fill(&l, sizeof l);
    l.device = (uint64_t)wm_device;
    l.url.ptr = file.fileSystemRepresentation;
    wm_own("ret_archive", &l.ret_archive);
    wm_own("ret_error", &l.ret_error);
    wm_call(112, &l);
    error_of("error", l.ret_error, false);
    struct unixcall_mtlbinaryarchive_serialize bad;
    wm_fill(&bad, sizeof bad);
    bad.archive = e.ret_archive;
    bad.url.ptr = "/wm-probe-no-such-folder/x/archive.bin";
    wm_own("ret_error", &bad.ret_error);
    wm_call(113, &bad);
    error_of("error", bad.ret_error, true);
}

/* After a pipeline is made without an error the handler adds its functions to the serialization archive (and drops that
 * call's error): an archive nothing was added to can't be serialized (113's error), one that got a pipeline can. A
 * pipeline that failed adds nothing. */
static void archive_serialize(void) {
    id<MTLLibrary> lib = shared_lib(), mlib = wm_library(kMSLMesh), tlib = wm_library(kMSLTile);
    id<MTLFunction> k = fn(lib, @"k"), vf = fn(lib, @"v"), ff = fn(lib, @"f"), vin = fn(lib, @"vin");
    id<MTLFunction> mf = mlib ? fn(mlib, @"mshf") : nil, mff = mlib ? fn(mlib, @"mf") : nil, tf = tlib ? fn(tlib, @"tf") : nil;
    static const char *const kinds[] = {"compute", "render", "vertex", "mesh", "tile", "compute-failed"};
    for (unsigned kind = 0; kind < sizeof kinds / sizeof *kinds; kind++) {
        struct unixcall_mtldevice_newbinaryarchive a;
        wm_fill(&a, sizeof a);
        a.device = (uint64_t)wm_device;
        a.url.ptr = NULL;
        wm_line("kind=%s", kinds[kind]);
        wm_call(112, &a);
        if (!a.ret_archive || a.ret_archive == WM_FILL64) return;
        obj_handle_t empty_lookup[1] = {a.ret_archive};
        if (kind == 0 || kind == 5) {
            struct WMTComputePipelineInfo info;
            compute_info(&info, k);
            info.binary_archive_for_serialization = a.ret_archive;
            if (kind == 5) { /* fails: the lookup archive (this one, empty) misses and the option says fail */
                info.binary_archives_for_lookup.ptr = empty_lookup;
                info.num_binary_archives_for_lookup = 1;
                info.fail_on_binary_archive_miss = true;
            }
            struct unixcall_mtldevice_newcomputepso p;
            wm_fill(&p, sizeof p);
            p.device = (uint64_t)wm_device;
            p.info.ptr = &info;
            wm_call(29, &p);
            wm_val("pipeline", "%s", nz(p.ret_pso));
        } else if (kind == 1 || kind == 2) {
            struct WMTRenderPipelineInfo info;
            render_info(&info, kind == 1 ? vf : vin, ff);
            info.binary_archive_for_serialization = a.ret_archive;
            if (kind == 1) {
                struct unixcall_mtldevice_newrenderpso p;
                wm_fill(&p, sizeof p);
                p.device = (uint64_t)wm_device;
                p.info.ptr = &info;
                wm_call(34, &p);
                wm_val("pipeline", "%s", nz(p.ret_pso));
            } else {
                struct WMTVertexDescriptorInfo vd;
                memset(&vd, 0, sizeof vd);
                vd.attributes[0].format = MTLVertexFormatFloat4;
                vd.attributes[1].format = MTLVertexFormatFloat2;
                vd.attributes[1].offset = 16;
                vd.layouts[0].stride = 24;
                vd.layouts[0].step_function = MTLVertexStepFunctionPerVertex;
                vd.layouts[0].step_rate = 1;
                struct unixcall_mtldevice_newrenderpso_vertex p;
                wm_fill(&p, sizeof p);
                p.device = (uint64_t)wm_device;
                p.info.ptr = &info;
                p.vertex.ptr = &vd;
                wm_call(156, &p);
                wm_val("pipeline", "%s", nz(p.ret_pso));
            }
        } else if (kind == 3 && mf) {
            struct WMTMeshRenderPipelineInfo info;
            memset(&info, 0, sizeof info);
            info.colors[0].pixel_format = WMTPixelFormatBGRA8Unorm;
            info.colors[0].write_mask = 15;
            info.rasterization_enabled = true;
            info.raster_sample_count = 1;
            info.mesh_function = (obj_handle_t)mf;
            info.fragment_function = (obj_handle_t)mff;
            info.binary_archive_for_serialization = a.ret_archive;
            struct unixcall_mtldevice_newmeshrenderpso p;
            wm_fill(&p, sizeof p);
            p.device = (uint64_t)wm_device;
            p.info.ptr = &info;
            wm_call(35, &p);
            wm_val("pipeline", "%s", nz(p.ret_pso));
        } else if (kind == 4 && tf) {
            struct WMTTileRenderPipelineInfo info;
            memset(&info, 0, sizeof info);
            info.tile_function = (obj_handle_t)tf;
            info.color_formats[0] = WMTPixelFormatBGRA8Unorm;
            info.raster_sample_count = 1;
            info.binary_archive_for_serialization = a.ret_archive;
            struct unixcall_mtldevice_newrenderpso p;
            wm_fill(&p, sizeof p);
            p.device = (uint64_t)wm_device;
            p.info.ptr = &info;
            wm_call(131, &p);
            wm_val("pipeline", "%s", nz(p.ret_pso));
        }
        NSString *file = [NSString stringWithFormat:@"%s/serialize-%s.bin", wm_tmpdir(), kinds[kind]];
        struct unixcall_mtlbinaryarchive_serialize s;
        wm_fill(&s, sizeof s);
        s.archive = a.ret_archive;
        s.url.ptr = file.fileSystemRepresentation;
        wm_call(113, &s);
        error_of("serialize", s.ret_error, false);
        struct stat st;
        wm_val("file", "%s", !stat(file.fileSystemRepresentation, &st) && st.st_size > 0 ? "written" : "missing");
    }
}

/* ---- 144, 157 ---- */

static void source_call(unsigned slot, const char *what, id dev, const char *src, uint64_t len, const struct WMTCompileOptions *opt) {
    wm_line("run=%s", what);
    if (slot == 144) {
        struct unixcall_mtldevice_newlibrarywithsource p;
        wm_fill(&p, sizeof p);
        p.device = (uint64_t)dev;
        p.source.ptr = src;
        p.length = len;
        wm_own("ret_library", &p.ret_library);
        wm_own("ret_error", &p.ret_error);
        wm_call(144, &p);
        error_of("error", p.ret_error, !strncmp(what, "syntax", 6));
    } else {
        struct unixcall_mtldevice_newlibrarywithsource_options p;
        wm_fill(&p, sizeof p);
        p.device = (uint64_t)dev;
        p.source.ptr = src;
        p.length = len;
        p.options.ptr = opt;
        wm_own("ret_library", &p.ret_library);
        wm_own("ret_error", &p.ret_error);
        wm_call(157, &p);
        error_of("error", p.ret_error, !strncmp(what, "syntax", 6));
    }
}

/* 144: source of a length (ASCII, MSL 3.1 for every library it builds), a syntax error (+1 error with Metal's text), a
 * shorter length than the string, length 0. */
static void source(void) {
    id rec = wm_rec(wm_device, @protocol(MTLDevice));
    const char *good = "#include <metal_stdlib>\nusing namespace metal;\nkernel void sk(device uint *b [[buffer(0)]]) { b[0] = 1; }\n";
    source_call(144, "good", rec, good, strlen(good), NULL);
    source_call(144, "good-real-device", (id)wm_device, good, strlen(good), NULL);
    const char *bad = "kernel void broken( { }\n";
    source_call(144, "syntax-error", rec, bad, strlen(bad), NULL);
    source_call(144, "short-length", rec, good, 24, NULL);
    source_call(144, "length-0", rec, good, 0, NULL);
    const char *versioned = "#include <metal_stdlib>\nusing namespace metal;\nkernel void sk(device uint *b [[buffer(0)]]) { b[0] = __METAL_VERSION__; }\n";
    source_call(144, "version-31", rec, versioned, strlen(versioned), NULL);
    /* The string is decoded as ASCII, which takes bytes of 0x80 and up: a UTF-8 decode of 0xff gets nil, and Metal aborts
     * the process on a nil source. Last, so the rest of the case is printed before such an abort. */
    const char *high = "#include <metal_stdlib>\nusing namespace metal;\n// \xff\xfe bytes\nkernel void sk(device uint *b [[buffer(0)]]) { b[0] = 1; }\n";
    source_call(144, "high-bytes-comment", rec, high, strlen(high), NULL);
}

/* 157: the compile options dumped (language version 0 keeps Metal's default), each field, and a syntax error. */
static void source_options(void) {
    id rec = wm_rec(wm_device, @protocol(MTLDevice));
    const char *good = "#include <metal_stdlib>\nusing namespace metal;\nkernel void sk(device uint *b [[buffer(0)]]) { b[0] = 1; }\n";
    struct {
        const char *what;
        uint64_t version;
        uint8_t invariance, math;
    } runs[] = {
        {"default", 0, 0, 0},
        {"msl-3.1-invariant", 0x30001, 1, 0},
        {"msl-2.4-relaxed", 0x20004, 0, 1},
        {"msl-3.2-fast", 0x30002, 1, 2},
        {"invariance-2", 0, 2, 0},
        {"math-3", 0, 0, 3},
    };
    for (unsigned i = 0; i < sizeof runs / sizeof *runs; i++) {
        struct WMTCompileOptions o;
        memset(&o, 0, sizeof o);
        o.language_version = runs[i].version;
        o.preserve_invariance = runs[i].invariance;
        o.math_mode = runs[i].math;
        source_call(157, runs[i].what, rec, good, strlen(good), &o);
    }
    struct WMTCompileOptions o = {.language_version = 0x30001};
    const char *bad = "kernel void broken( { }\n";
    source_call(157, "syntax-error", rec, bad, strlen(bad), &o);
    source_call(157, "short-length", rec, good, 24, &o);
    /* As 144: the source is ASCII-decoded, so a byte of 0x80 and up in a comment is accepted. Last, as in 144. */
    const char *high = "#include <metal_stdlib>\nusing namespace metal;\n// \xff\xfe bytes\nkernel void sk(device uint *b [[buffer(0)]]) { b[0] = 1; }\n";
    source_call(157, "high-bytes-comment", rec, high, strlen(high), &o);
}

/* ---- nil ---- */

/* The object arguments the group's sends take, 0: nil goes on (the receivers are nil, or the nil is Metal's to accept)
 * and the C writes what the nil send answers. A null descriptor field is nil too. */
static void nil_args(void) {
    id<MTLLibrary> lib = shared_lib();
    id<MTLFunction> k = fn(lib, @"k");
    struct unixcall_mtldevice_newlibrary l;
    wm_fill(&l, sizeof l);
    l.device = 0;
    l.data = 0;
    wm_call(25, &l);
    wm_val("25.ret_library", "%s", nz(l.ret_library));
    wm_val("25.ret_error", "%s", nz(l.ret_error));
    /* A live device and data 0: the nil goes to Metal, which answers its error; nothing raises. */
    struct unixcall_mtldevice_newlibrary ld;
    wm_fill(&ld, sizeof ld);
    ld.device = (uint64_t)wm_device;
    ld.data = 0;
    wm_call(25, &ld);
    wm_val("25.live.ret_library", "%s", nz(ld.ret_library));
    error_of("25.live.ret_error", ld.ret_error, false);
    struct unixcall_generic_obj_uint64_obj_ret f;
    wm_fill(&f, sizeof f);
    f.handle = 0;
    f.arg = (uint64_t) "k";
    wm_call(26, &f);
    wm_val("26.ret", "%s", nz(f.ret));
    struct WMTComputePipelineInfo ci;
    compute_info(&ci, k);
    struct unixcall_mtldevice_newcomputepso c;
    wm_fill(&c, sizeof c);
    c.device = 0;
    c.info.ptr = &ci;
    wm_call(29, &c);
    wm_val("29.ret_pso", "%s", nz(c.ret_pso));
    wm_val("29.ret_error", "%s", nz(c.ret_error));
    struct WMTRenderPipelineInfo ri;
    render_info(&ri, 0, 0);
    struct unixcall_mtldevice_newrenderpso r;
    wm_fill(&r, sizeof r);
    r.device = 0;
    r.info.ptr = &ri;
    wm_call(34, &r);
    wm_val("34.ret_pso", "%s", nz(r.ret_pso));
    wm_val("34.ret_error", "%s", nz(r.ret_error));
    struct WMTVertexDescriptorInfo vd;
    memset(&vd, 0, sizeof vd);
    struct unixcall_mtldevice_newrenderpso_vertex v;
    wm_fill(&v, sizeof v);
    v.device = 0;
    v.info.ptr = &ri;
    v.vertex.ptr = &vd;
    wm_call(156, &v);
    wm_val("156.ret_pso", "%s", nz(v.ret_pso));
    wm_val("156.ret_error", "%s", nz(v.ret_error));
    struct WMTMeshRenderPipelineInfo mi;
    memset(&mi, 0, sizeof mi);
    struct unixcall_mtldevice_newmeshrenderpso m;
    wm_fill(&m, sizeof m);
    m.device = 0;
    m.info.ptr = &mi;
    wm_call(35, &m);
    wm_val("35.ret_pso", "%s", nz(m.ret_pso));
    wm_val("35.ret_error", "%s", nz(m.ret_error));
    struct WMTTileRenderPipelineInfo ti;
    memset(&ti, 0, sizeof ti);
    struct unixcall_mtldevice_newrenderpso t;
    wm_fill(&t, sizeof t);
    t.device = 0;
    t.info.ptr = &ti;
    wm_call(131, &t);
    wm_val("131.ret_pso", "%s", nz(t.ret_pso));
    wm_val("131.ret_error", "%s", nz(t.ret_error));
    const char *src = "kernel void sk() {}\n";
    struct unixcall_mtldevice_newlibrarywithsource s;
    wm_fill(&s, sizeof s);
    s.device = 0;
    s.source.ptr = src;
    s.length = strlen(src);
    wm_call(144, &s);
    wm_val("144.ret_library", "%s", nz(s.ret_library));
    wm_val("144.ret_error", "%s", nz(s.ret_error));
    struct WMTCompileOptions o = {0};
    struct unixcall_mtldevice_newlibrarywithsource_options so;
    wm_fill(&so, sizeof so);
    so.device = 0;
    so.source.ptr = src;
    so.length = strlen(src);
    so.options.ptr = &o;
    wm_call(157, &so);
    wm_val("157.ret_library", "%s", nz(so.ret_library));
    wm_val("157.ret_error", "%s", nz(so.ret_error));
    struct unixcall_mtldevice_newbinaryarchive ba;
    wm_fill(&ba, sizeof ba);
    ba.device = 0;
    ba.url.ptr = NULL;
    wm_call(112, &ba);
    wm_val("112.ret_archive", "%s", nz(ba.ret_archive));
    wm_val("112.ret_error", "%s", nz(ba.ret_error));
    struct unixcall_mtlbinaryarchive_serialize bs;
    wm_fill(&bs, sizeof bs);
    bs.archive = 0;
    bs.url.ptr = "/wm-probe/never.bin";
    wm_call(113, &bs);
    wm_val("113.ret_error", "%s", nz(bs.ret_error));
    struct unixcall_mtllibrary_newfunction_with_constants fc;
    wm_fill(&fc, sizeof fc);
    fc.library = 0;
    fc.name.ptr = "kc";
    fc.constants.ptr = NULL;
    fc.num_constants = 0;
    wm_call(98, &fc);
    wm_val("98.ret", "%s", nz(fc.ret));
    wm_val("98.ret_error", "%s", nz(fc.ret_error));
}

/* A 0 entry among binary_archives_for_lookup: [NSArray arrayWithObjects:count:] raises NSInvalidArgumentException before
 * the pipeline is asked for, and the handler writes nothing. A port that skips the nil entry (filter_map) answers a
 * pipeline where the C refused. 29, 34, 156 (the shared descriptor), 35 and 131 (lines of their own). */
static void lookup_nil_entry(void) {
    id<MTLLibrary> lib = shared_lib(), mlib = wm_library(kMSLMesh), tlib = wm_library(kMSLTile);
    id<MTLFunction> k = fn(lib, @"k"), vf = fn(lib, @"v"), ff = fn(lib, @"f"), vin = fn(lib, @"vin");
    id<MTLFunction> mf = mlib ? fn(mlib, @"mshf") : nil, mff = mlib ? fn(mlib, @"mf") : nil, tf = tlib ? fn(tlib, @"tf") : nil;
    id<MTLBinaryArchive> archive = [[wm_device newBinaryArchiveWithDescriptor:[[[MTLBinaryArchiveDescriptor alloc] init] autorelease] error:nil] autorelease];
    obj_handle_t archives[2] = {(obj_handle_t)archive, 0};
    {
        struct WMTComputePipelineInfo info;
        compute_info(&info, k);
        info.binary_archives_for_lookup.ptr = archives;
        info.num_binary_archives_for_lookup = 2;
        struct unixcall_mtldevice_newcomputepso p;
        wm_fill(&p, sizeof p);
        p.device = (uint64_t)wm_device;
        p.info.ptr = &info;
        wm_line("run=compute");
        wm_call(29, &p);
        wm_bytes("ret_pso", &p.ret_pso, sizeof p.ret_pso);
        wm_bytes("ret_error", &p.ret_error, sizeof p.ret_error);
    }
    static const unsigned render_slots[] = {34, 156};
    for (unsigned i = 0; i < 2; i++) {
        unsigned slot = render_slots[i];
        struct WMTRenderPipelineInfo info;
        render_info(&info, slot == 34 ? vf : vin, ff);
        info.binary_archives_for_lookup.ptr = archives;
        info.num_binary_archives_for_lookup = 2;
        struct WMTVertexDescriptorInfo vd;
        memset(&vd, 0, sizeof vd);
        vd.attributes[0].format = MTLVertexFormatFloat4;
        vd.attributes[1].format = MTLVertexFormatFloat2;
        vd.attributes[1].offset = 16;
        vd.layouts[0].stride = 24;
        vd.layouts[0].step_function = MTLVertexStepFunctionPerVertex;
        vd.layouts[0].step_rate = 1;
        wm_line("run=%s", slot == 34 ? "render" : "vertex");
        if (slot == 34) {
            struct unixcall_mtldevice_newrenderpso p;
            wm_fill(&p, sizeof p);
            p.device = (uint64_t)wm_device;
            p.info.ptr = &info;
            wm_call(34, &p);
            wm_bytes("ret_pso", &p.ret_pso, sizeof p.ret_pso);
            wm_bytes("ret_error", &p.ret_error, sizeof p.ret_error);
        } else {
            struct unixcall_mtldevice_newrenderpso_vertex p;
            wm_fill(&p, sizeof p);
            p.device = (uint64_t)wm_device;
            p.info.ptr = &info;
            p.vertex.ptr = &vd;
            wm_call(156, &p);
            wm_bytes("ret_pso", &p.ret_pso, sizeof p.ret_pso);
            wm_bytes("ret_error", &p.ret_error, sizeof p.ret_error);
        }
    }
    if (mf) {
        struct WMTMeshRenderPipelineInfo info;
        memset(&info, 0, sizeof info);
        info.colors[0].pixel_format = WMTPixelFormatBGRA8Unorm;
        info.colors[0].write_mask = 15;
        info.rasterization_enabled = true;
        info.raster_sample_count = 1;
        info.mesh_function = (obj_handle_t)mf;
        info.fragment_function = (obj_handle_t)mff;
        info.binary_archives_for_lookup.ptr = archives;
        info.num_binary_archives_for_lookup = 2;
        struct unixcall_mtldevice_newmeshrenderpso p;
        wm_fill(&p, sizeof p);
        p.device = (uint64_t)wm_device;
        p.info.ptr = &info;
        wm_line("run=mesh");
        wm_call(35, &p);
        wm_bytes("ret_pso", &p.ret_pso, sizeof p.ret_pso);
        wm_bytes("ret_error", &p.ret_error, sizeof p.ret_error);
    }
    if (tf) {
        struct WMTTileRenderPipelineInfo info;
        memset(&info, 0, sizeof info);
        info.tile_function = (obj_handle_t)tf;
        info.color_formats[0] = WMTPixelFormatBGRA8Unorm;
        info.raster_sample_count = 1;
        info.binary_archives_for_lookup.ptr = archives;
        info.num_binary_archives_for_lookup = 2;
        struct unixcall_mtldevice_newrenderpso p;
        wm_fill(&p, sizeof p);
        p.device = (uint64_t)wm_device;
        p.info.ptr = &info;
        wm_line("run=tile");
        wm_call(131, &p);
        wm_bytes("ret_pso", &p.ret_pso, sizeof p.ret_pso);
        wm_bytes("ret_error", &p.ret_error, sizeof p.ret_error);
    }
}

/* 113 on an archive into a file of the case's folder: whether it wrote one. */
static void serialize_file(uint64_t archive, const char *name) {
    NSString *file = [NSString stringWithFormat:@"%s/nil-device-%s.bin", wm_tmpdir(), name];
    struct unixcall_mtlbinaryarchive_serialize s;
    wm_fill(&s, sizeof s);
    s.archive = archive;
    s.url.ptr = file.fileSystemRepresentation;
    wm_call(113, &s);
    error_of("serialize", s.ret_error, false);
    struct stat st;
    wm_val("file", "%s", !stat(file.fileSystemRepresentation, &st) && st.st_size > 0 ? "written" : "missing");
}

/* Device 0: the pipeline send answers nil and sets no error, so the handler still adds the functions to the
 * serialization archive it was given, and 113 then writes a file. A port that returns early on a nil device writes none.
 * 29, 34, 156, 35 and 131. */
static void nil_device_serialize(void) {
    id<MTLLibrary> lib = shared_lib(), mlib = wm_library(kMSLMesh), tlib = wm_library(kMSLTile);
    id<MTLFunction> k = fn(lib, @"k"), vf = fn(lib, @"v"), ff = fn(lib, @"f"), vin = fn(lib, @"vin");
    id<MTLFunction> mf = mlib ? fn(mlib, @"mshf") : nil, mff = mlib ? fn(mlib, @"mf") : nil, tf = tlib ? fn(tlib, @"tf") : nil;
    id<MTLBinaryArchive> ac = [[wm_device newBinaryArchiveWithDescriptor:[[[MTLBinaryArchiveDescriptor alloc] init] autorelease] error:nil] autorelease];
    id<MTLBinaryArchive> ar = [[wm_device newBinaryArchiveWithDescriptor:[[[MTLBinaryArchiveDescriptor alloc] init] autorelease] error:nil] autorelease];
    id<MTLBinaryArchive> av = [[wm_device newBinaryArchiveWithDescriptor:[[[MTLBinaryArchiveDescriptor alloc] init] autorelease] error:nil] autorelease];
    id<MTLBinaryArchive> am = [[wm_device newBinaryArchiveWithDescriptor:[[[MTLBinaryArchiveDescriptor alloc] init] autorelease] error:nil] autorelease];
    id<MTLBinaryArchive> at = [[wm_device newBinaryArchiveWithDescriptor:[[[MTLBinaryArchiveDescriptor alloc] init] autorelease] error:nil] autorelease];
    {
        struct WMTComputePipelineInfo info;
        compute_info(&info, k);
        info.binary_archive_for_serialization = (obj_handle_t)ac;
        struct unixcall_mtldevice_newcomputepso p;
        wm_fill(&p, sizeof p);
        p.device = 0;
        p.info.ptr = &info;
        wm_line("run=compute");
        wm_call(29, &p);
        wm_val("ret_pso", "%s", nz(p.ret_pso));
        wm_val("ret_error", "%s", nz(p.ret_error));
        serialize_file((uint64_t)ac, "compute");
    }
    {
        struct WMTRenderPipelineInfo info;
        render_info(&info, vf, ff);
        info.binary_archive_for_serialization = (obj_handle_t)ar;
        struct unixcall_mtldevice_newrenderpso p;
        wm_fill(&p, sizeof p);
        p.device = 0;
        p.info.ptr = &info;
        wm_line("run=render");
        wm_call(34, &p);
        wm_val("ret_pso", "%s", nz(p.ret_pso));
        wm_val("ret_error", "%s", nz(p.ret_error));
        serialize_file((uint64_t)ar, "render");
    }
    {
        struct WMTRenderPipelineInfo info;
        render_info(&info, vin, ff);
        info.binary_archive_for_serialization = (obj_handle_t)av;
        struct WMTVertexDescriptorInfo vd;
        memset(&vd, 0, sizeof vd);
        vd.attributes[0].format = MTLVertexFormatFloat4;
        vd.attributes[1].format = MTLVertexFormatFloat2;
        vd.attributes[1].offset = 16;
        vd.layouts[0].stride = 24;
        vd.layouts[0].step_function = MTLVertexStepFunctionPerVertex;
        vd.layouts[0].step_rate = 1;
        struct unixcall_mtldevice_newrenderpso_vertex p;
        wm_fill(&p, sizeof p);
        p.device = 0;
        p.info.ptr = &info;
        p.vertex.ptr = &vd;
        wm_line("run=vertex");
        wm_call(156, &p);
        wm_val("ret_pso", "%s", nz(p.ret_pso));
        wm_val("ret_error", "%s", nz(p.ret_error));
        serialize_file((uint64_t)av, "vertex");
    }
    if (mf) {
        struct WMTMeshRenderPipelineInfo info;
        memset(&info, 0, sizeof info);
        info.colors[0].pixel_format = WMTPixelFormatBGRA8Unorm;
        info.colors[0].write_mask = 15;
        info.rasterization_enabled = true;
        info.raster_sample_count = 1;
        info.mesh_function = (obj_handle_t)mf;
        info.fragment_function = (obj_handle_t)mff;
        info.binary_archive_for_serialization = (obj_handle_t)am;
        struct unixcall_mtldevice_newmeshrenderpso p;
        wm_fill(&p, sizeof p);
        p.device = 0;
        p.info.ptr = &info;
        wm_line("run=mesh");
        wm_call(35, &p);
        wm_val("ret_pso", "%s", nz(p.ret_pso));
        wm_val("ret_error", "%s", nz(p.ret_error));
        serialize_file((uint64_t)am, "mesh");
    }
    if (tf) {
        struct WMTTileRenderPipelineInfo info;
        memset(&info, 0, sizeof info);
        info.tile_function = (obj_handle_t)tf;
        info.color_formats[0] = WMTPixelFormatBGRA8Unorm;
        info.raster_sample_count = 1;
        info.binary_archive_for_serialization = (obj_handle_t)at;
        struct unixcall_mtldevice_newrenderpso p;
        wm_fill(&p, sizeof p);
        p.device = 0;
        p.info.ptr = &info;
        wm_line("run=tile");
        wm_call(131, &p);
        wm_val("ret_pso", "%s", nz(p.ret_pso));
        wm_val("ret_error", "%s", nz(p.ret_error));
        serialize_file((uint64_t)at, "tile");
    }
}

/* ---- leaks ---- */

/* Pipelines, functions, libraries and errors given back: nothing grows cycle after cycle. */
static void heap_growth(void) {
    id<MTLLibrary> lib = shared_lib();
    id<MTLFunction> k = fn(lib, @"k");
    const char *bad = "kernel void broken( { }\n";
    wm_heap_growth(^{
      struct WMTComputePipelineInfo ci;
      compute_info(&ci, k);
      struct unixcall_mtldevice_newcomputepso c;
      wm_fill(&c, sizeof c);
      c.device = (uint64_t)wm_device;
      c.info.ptr = &ci;
      wm_call(29, &c);
      if (c.ret_pso && c.ret_pso != WM_FILL64) [(id)c.ret_pso release];
      if (c.ret_error && c.ret_error != WM_FILL64) [(id)c.ret_error release];
      struct unixcall_mtldevice_newlibrarywithsource s;
      wm_fill(&s, sizeof s);
      s.device = (uint64_t)wm_device;
      s.source.ptr = bad;
      s.length = strlen(bad);
      wm_call(144, &s);
      if (s.ret_library && s.ret_library != WM_FILL64) [(id)s.ret_library release];
      if (s.ret_error && s.ret_error != WM_FILL64) [(id)s.ret_error release];
      struct unixcall_generic_obj_uint64_obj_ret f;
      wm_fill(&f, sizeof f);
      f.handle = (uint64_t)lib;
      f.arg = (uint64_t) "k";
      wm_call(26, &f);
      if (f.ret && f.ret != WM_FILL64) [(id)f.ret release];
    }, 20);
}

const struct wm_case wm_cases_g4[] = {
    {"g4", 25, "library-data", 0, library_data},
    {"g4", 26, "function", 0, function},
    {"g4", 29, "compute-pso", 0, compute_pso},
    {"g4", 29, "archive-pipelines", 0, archive_pipelines},
    {"g4", 113, "archive-serialize", 0, archive_serialize},
    {"g4", 34, "render-pso", 0, render_pso},
    {"g4", 35, "mesh-pso", 0, mesh_pso},
    {"g4", 131, "tile-pso", 0, tile_pso},
    {"g4", 156, "vertex-pso", 0, vertex_pso},
    {"g4", 98, "constants", 0, constants},
    {"g4", 112, "archive", 0, archive},
    {"g4", 144, "source", 0, source},
    {"g4", 157, "source-options", 0, source_options},
    {"g4", 25, "nil-args", 0, nil_args},
    {"g4", 29, "lookup-nil-entry", 0, lookup_nil_entry},
    {"g4", 29, "nil-device-serialize", 0, nil_device_serialize},
    {"g4", 26, "x-library-on-device", 0, x_library_on_device},
    {"g4", 29, "heap-growth", 0, heap_growth},
    {0},
};
