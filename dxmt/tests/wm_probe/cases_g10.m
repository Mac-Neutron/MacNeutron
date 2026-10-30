/* wm_probe's g10 cases (slots.txt's g10; probe.h), recorded from DXMT's C: the translator thunks (74-88), which
 * forward field for field to the static libairconv's SM50*. Shaders, bitcode and errors are raw pointers (wm_h names
 * them by number); the shaders are dxmt/tests/shaders' fixtures. */
#include "probe.h"
#include "airconv_thunks.h"

/* 74 on a fixture: the shader handle (the case stops when there is none), the status and the reflection. */
static sm50_shader_t init_shader(const char *fixture, bool print_refl) {
    NSData *d = wm_fixture(fixture);
    if (!d) {
        wm_line("no-fixture %s", fixture);
        return NULL;
    }
    sm50_shader_t shader;
    sm50_error_t error;
    struct MTL_SHADER_REFLECTION refl;
    struct sm50_initialize_params p;
    wm_fill(&shader, sizeof shader), wm_fill(&error, sizeof error), wm_fill(&refl, sizeof refl), wm_fill(&p, sizeof p);
    p.bytecode = d.bytes, p.bytecode_size = d.length, p.shader = &shader, p.reflection = &refl, p.error = &error;
    wm_call(74, &p);
    wm_val("ret", "%d", p.ret);
    wm_val("error", "%s", error == (sm50_error_t)WM_FILL64 ? "fill" : error ? "nonnull" : "0");
    if (print_refl) {
        wm_bytes("refl", &refl, sizeof refl);
        wm_sha("refl-sha", &refl, sizeof refl);
        wm_val("cbs", "%u", refl.NumConstantBuffers);
        wm_val("args", "%u", refl.NumArguments);
        wm_val("cb-index", "%u", refl.ConstanttBufferTableBindIndex);
        wm_val("arg-index", "%u", refl.ArgumentBufferBindIndex);
        wm_val("outputs", "%u", refl.NumOutputElement);
    }
    wm_h("shader", (uint64_t)shader);
    return shader;
}

static struct SM50_SHADER_COMMON_DATA common(void *next) {
    struct SM50_SHADER_COMMON_DATA c;
    wm_fill(&c, sizeof c);
    c.next = next, c.type = SM50_SHADER_COMMON, c.metal_version = SM50_SHADER_METAL_310, c.flags = 0;
    return c;
}

/* 77 on a bitcode: written fields and the bytes' sha256 (the case stops on no bitcode). */
static void bitcode_sha(sm50_bitcode_t bitcode) {
    struct SM50_COMPILED_BITCODE out;
    wm_fill(&out, sizeof out);
    struct sm50_get_compiled_bitcode_params p = {.bitcode = bitcode, .data_out = &out};
    wm_call(77, &p);
    if (!out.Data || out.Data == (void *)WM_FILL64 || out.Size == WM_FILL64) {
        wm_val("data", "%s", out.Data == (void *)WM_FILL64 ? "fill" : "0");
        return;
    }
    wm_sha("bitcode", out.Data, out.Size);
}

static void destroy_shader(sm50_shader_t s) {
    struct sm50_destroy_params p = {.shader = s};
    wm_call(75, &p);
}

static void destroy_bitcode(sm50_bitcode_t b) {
    struct sm50_destroy_bitcode_params p = {.bitcode = b};
    wm_call(78, &p);
}

/* The status and the bitcode/error outs of a compile-shaped call. */
static sm50_bitcode_t compiled(int ret, sm50_bitcode_t bitcode, sm50_error_t error) {
    wm_val("ret", "%d", ret);
    wm_val("error", "%s", error == (sm50_error_t)WM_FILL64 ? "fill" : error ? "nonnull" : "0");
    wm_h("bitcode", (uint64_t)bitcode);
    return bitcode;
}

static void initialize(void) {
    sm50_shader_t s = init_shader("../shaders/triangle.vs.dxil", true);
    destroy_shader(s);
}

/* 76 on a pixel shader with PSO_PIXEL_SHADER -> COMMON: the bitcode's sha256 (77). */
static void compile(void) {
    sm50_shader_t s = init_shader("../shaders/triangle.ps.dxil", true);
    struct SM50_SHADER_COMMON_DATA c = common(NULL);
    struct SM50_SHADER_PSO_PIXEL_SHADER_DATA ps;
    wm_fill(&ps, sizeof ps);
    ps.next = &c, ps.type = SM50_SHADER_PSO_PIXEL_SHADER, ps.sample_mask = 0xffffffff;
    ps.dual_source_blending = false, ps.disable_depth_output = false, ps.unorm_output_reg_mask = 0;
    memset(ps.pixel_formats, 0, sizeof ps.pixel_formats);
    ps.pixel_formats[0] = 80; /* MTLPixelFormatBGRA8Unorm */
    sm50_bitcode_t bitcode;
    sm50_error_t error;
    wm_fill(&bitcode, sizeof bitcode), wm_fill(&error, sizeof error);
    struct sm50_compile_params p = {
        .shader = s, .args = &ps, .func_name = "probe_ps", .bitcode = &bitcode, .error = &error};
    wm_fill(&p.ret, sizeof p.ret);
    wm_call(76, &p);
    compiled(p.ret, bitcode, error);
    bitcode_sha(bitcode);
    destroy_bitcode(bitcode);
    destroy_shader(s);
}

/* 77 on a vertex shader's bitcode (COMMON only): size > 0, both fields written. */
static void bitcode(void) {
    sm50_shader_t s = init_shader("../shaders/triangle.vs.dxil", false);
    struct SM50_SHADER_COMMON_DATA c = common(NULL);
    sm50_bitcode_t b;
    sm50_error_t error;
    wm_fill(&b, sizeof b), wm_fill(&error, sizeof error);
    struct sm50_compile_params p = {.shader = s, .args = &c, .func_name = "probe_vs", .bitcode = &b, .error = &error};
    wm_call(76, &p);
    compiled(p.ret, b, error);
    struct SM50_COMPILED_BITCODE out;
    wm_fill(&out, sizeof out);
    struct sm50_get_compiled_bitcode_params g = {.bitcode = b, .data_out = &out};
    wm_call(77, &g);
    wm_val("data", "%s", out.Data == (void *)WM_FILL64 ? "fill" : out.Data ? "nonnull" : "0");
    wm_val("size", "%s", out.Size == WM_FILL64 ? "fill" : out.Size ? ">0" : "0");
    wm_raw("bytes", "%llu", (unsigned long long)out.Size);
    destroy_bitcode(b);
    destroy_shader(s);
}

/* 75 and 78 free what they're given: 100 make/free cycles each. */
static void destroy(void) {
    NSData *vs = wm_fixture("../shaders/triangle.vs.dxil");
    wm_heap_growth(^{
      sm50_shader_t shader = NULL;
      struct MTL_SHADER_REFLECTION refl;
      sm50_error_t error = NULL;
      struct sm50_initialize_params p = {.bytecode = vs.bytes, .bytecode_size = vs.length, .shader = &shader,
                                         .reflection = &refl, .error = &error};
      wm_call(74, &p);
      if (shader) destroy_shader(shader); /* none: 74 stubbed (cover.sh bites) */
    }, 100);
    sm50_shader_t s = init_shader("../shaders/triangle.vs.dxil", false);
    wm_heap_growth(^{
      struct SM50_SHADER_COMMON_DATA c = common(NULL);
      sm50_bitcode_t b = NULL;
      sm50_error_t error = NULL;
      struct sm50_compile_params p = {.shader = s, .args = &c, .func_name = "probe_vs", .bitcode = &b, .error = &error};
      wm_call(76, &p);
      if (b) destroy_bitcode(b);
    }, 100);
    destroy_shader(s);
}

/* 74 on junk: the error's text (79, whole and cut short), then 80 frees it (100 cycles). */
static void error(void) {
    static const char junk[64] = "not a shader, not a container: junk for SM50Initialize";
    sm50_shader_t shader;
    sm50_error_t err;
    wm_fill(&shader, sizeof shader), wm_fill(&err, sizeof err);
    struct MTL_SHADER_REFLECTION refl;
    struct sm50_initialize_params p = {
        .bytecode = junk, .bytecode_size = sizeof junk, .shader = &shader, .reflection = &refl, .error = &err};
    wm_call(74, &p);
    wm_val("ret", "%s", p.ret ? "nonzero" : "0");
    wm_raw("ret-value", "%d", p.ret);
    wm_val("shader", "%s", shader == (sm50_shader_t)WM_FILL64 ? "fill" : shader ? "nonnull" : "0");
    wm_h("error", (uint64_t)err);
    for (unsigned size = 256; size; size = size == 256 ? 8 : 0) {
        char buf[256];
        wm_fill(buf, sizeof buf);
        struct sm50_get_error_message_params m = {.error = err, .buffer = buf, .buffer_size = size};
        wm_fill(&m.ret_size, sizeof m.ret_size);
        wm_call(79, &m);
        wm_val("ret_size", "%llu", (unsigned long long)m.ret_size);
        wm_bytes("written", buf, sizeof buf);
        size_t n = m.ret_size < size ? m.ret_size : size;
        wm_val("text", "%.*s", (int)n, buf);
    }
    struct sm50_free_error_params f = {.error = err};
    wm_call(80, &f);
    /* Two errors a cycle: one is 64 bytes, heap-growth's own threshold. */
    wm_heap_growth(^{
      for (int i = 0; i < 2; i++) {
          sm50_shader_t s2 = NULL;
          sm50_error_t e2 = NULL;
          struct MTL_SHADER_REFLECTION r2;
          struct sm50_initialize_params q = {
              .bytecode = junk, .bytecode_size = sizeof junk, .shader = &s2, .reflection = &r2, .error = &e2};
          wm_call(74, &q);
          struct sm50_free_error_params g = {.error = e2};
          if (e2) wm_call(80, &g);
      }
    }, 100);
}

/* 81/82 on triangle2's vertex and geometry shaders: each pipeline stage's bitcode. */
static void geometry(void) {
    sm50_shader_t vs = init_shader("../shaders/triangle2.vs.dxil", false);
    sm50_shader_t gs = init_shader("../shaders/triangle2.gs.dxil", true);
    struct SM50_SHADER_COMMON_DATA c = common(NULL);
    struct SM50_SHADER_IA_INPUT_LAYOUT_DATA ia;
    wm_fill(&ia, sizeof ia);
    ia.next = &c, ia.type = SM50_SHADER_IA_INPUT_LAYOUT, ia.index_buffer_format = SM50_INDEX_BUFFER_FORMAT_NONE;
    ia.slot_mask = 0, ia.num_elements = 0, ia.elements = NULL;
    struct SM50_SHADER_PSO_GEOMETRY_SHADER_DATA geo;
    wm_fill(&geo, sizeof geo);
    geo.next = &ia, geo.type = SM50_SHADER_PSO_GEOMETRY_SHADER, geo.strip_topology = false;
    sm50_bitcode_t b;
    sm50_error_t error;
    wm_fill(&b, sizeof b), wm_fill(&error, sizeof error);
    struct sm50_compile_geometry_pipeline_vertex_params v = {
        .vertex = vs, .geometry = gs, .vertex_args = &geo, .func_name = "probe_vsgs", .bitcode = &b, .error = &error};
    wm_call(81, &v);
    bitcode_sha(compiled(v.ret, b, error));
    destroy_bitcode(b);
    geo.next = &c;
    wm_fill(&b, sizeof b), wm_fill(&error, sizeof error);
    struct sm50_compile_geometry_pipeline_geometry_params g = {
        .vertex = vs, .geometry = gs, .geometry_args = &geo, .func_name = "probe_gs", .bitcode = &b, .error = &error};
    wm_call(82, &g);
    bitcode_sha(compiled(g.ret, b, error));
    destroy_bitcode(b);
    destroy_shader(gs);
    destroy_shader(vs);
}

/* 84/85 on d3d11's patch shaders: the hull and domain stages' bitcode. */
static void tessellation(void) {
    sm50_shader_t vs = init_shader("../shaders/d3d11/patch.vs.dxbc", false);
    sm50_shader_t hs = init_shader("../shaders/d3d11/patch.hs.dxbc", true);
    sm50_shader_t ds = init_shader("../shaders/d3d11/patch.ds.dxbc", true);
    struct SM50_SHADER_COMMON_DATA c = common(NULL);
    struct SM50_SHADER_PSO_TESSELLATOR_DATA tess;
    wm_fill(&tess, sizeof tess);
    tess.next = &c, tess.type = SM50_SHADER_PSO_TESSELLATOR, tess.max_potential_tess_factor = 16;
    struct SM50_SHADER_IA_INPUT_LAYOUT_DATA ia;
    wm_fill(&ia, sizeof ia);
    ia.next = &tess, ia.type = SM50_SHADER_IA_INPUT_LAYOUT, ia.index_buffer_format = SM50_INDEX_BUFFER_FORMAT_NONE;
    ia.slot_mask = 0, ia.num_elements = 0, ia.elements = NULL;
    sm50_bitcode_t b;
    sm50_error_t error;
    wm_fill(&b, sizeof b), wm_fill(&error, sizeof error);
    struct sm50_compile_tessellation_pipeline_hull_params h = {
        .vertex = vs, .hull = hs, .hull_args = &ia, .func_name = "probe_vshs", .bitcode = &b, .error = &error};
    wm_call(84, &h);
    bitcode_sha(compiled(h.ret, b, error));
    destroy_bitcode(b);
    struct SM50_SHADER_GS_PASS_THROUGH_DATA pass;
    wm_fill(&pass, sizeof pass);
    pass.next = &tess, pass.type = SM50_SHADER_GS_PASS_THROUGH, pass.DataEncoded = 0, pass.RasterizationDisabled = false;
    wm_fill(&b, sizeof b), wm_fill(&error, sizeof error);
    struct sm50_compile_tessellation_pipeline_domain_params d = {
        .hull = hs, .domain = ds, .domain_args = &pass, .func_name = "probe_ds", .bitcode = &b, .error = &error};
    wm_call(85, &d);
    bitcode_sha(compiled(d.ret, b, error));
    destroy_bitcode(b);
    destroy_shader(ds);
    destroy_shader(hs);
    destroy_shader(vs);
}

/* 88: the constant buffers' and the arguments' arrays, each one entry longer than the reflection's count (untouched). */
static void arguments(void) {
    NSData *d = wm_fixture("../shaders/d3d11/sample.ps.dxbc");
    sm50_shader_t s;
    sm50_error_t error = NULL;
    struct MTL_SHADER_REFLECTION refl;
    struct sm50_initialize_params p = {
        .bytecode = d.bytes, .bytecode_size = d.length, .shader = &s, .reflection = &refl, .error = &error};
    wm_fill(&s, sizeof s);
    wm_call(74, &p);
    wm_h("shader", (uint64_t)s);
    wm_val("cbs", "%u", refl.NumConstantBuffers);
    wm_val("args", "%u", refl.NumArguments);
    struct MTL_SM50_SHADER_ARGUMENT cbs[9], args[33];
    unsigned ncb = refl.NumConstantBuffers < 8 ? refl.NumConstantBuffers : 8;
    unsigned narg = refl.NumArguments < 32 ? refl.NumArguments : 32;
    wm_fill(cbs, sizeof cbs), wm_fill(args, sizeof args);
    struct sm50_get_arguments_info_params a = {.shader = s, .constant_buffers = cbs, .arguments = args};
    wm_call(88, &a);
    wm_bytes("cb-written", cbs, (ncb + 1) * sizeof *cbs);
    wm_bytes("arg-written", args, (narg + 1) * sizeof *args);
    for (unsigned i = 0; i < ncb; i++)
        wm_val("cb", "%u:%u:%x:%u", cbs[i].Type, cbs[i].SM50BindingSlot, cbs[i].Flags, cbs[i].StructurePtrOffset);
    for (unsigned i = 0; i < narg; i++)
        wm_val("arg", "%u:%u:%x:%u", args[i].Type, args[i].SM50BindingSlot, args[i].Flags, args[i].StructurePtrOffset);
    destroy_shader(s);
}

const struct wm_case wm_cases_g10[] = {
    {"g10", 74, "initialize", 0, initialize},
    {"g10", 76, "compile", 0, compile},
    {"g10", 77, "bitcode", 0, bitcode},
    {"g10", 75, "destroy", 0, destroy},
    {"g10", 79, "error", 0, error},
    {"g10", 81, "geometry", 0, geometry},
    {"g10", 84, "tessellation", 0, tessellation},
    {"g10", 88, "arguments", 0, arguments},
    {0},
};
