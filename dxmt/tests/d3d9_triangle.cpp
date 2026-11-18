// Direct3D 9 (D3D9 spec §6): a 256x256 window, a HAL device, then frames of a clear to blue and a fixed-function
// pretransformed red triangle over the centre, each read back through GetRenderTargetData and presented, until one shows
// the triangle and 10 more have been presented (for up to 10 s): mtld3d leaves a draw out of its frame while its
// pipeline builds (shader.asyncCompile, on by default), so the first frames may show the clear alone, as they do in a
// game (in the VM's busy DXMT gate the build has taken over 300 frames).
// Prints, each "ok" or "FAIL <what>": "d3d9 module" (mtld3d's mtld3d.dll is loaded once Direct3DCreate9 ran: the
// d3d9.dll is mtld3d's, not Wine's wined3d one), "d3d9 clear" (a corner of the first frame is 0x0000ff), "d3d9
// triangle" (a frame's centre is 0xff0000), "d3d9 present" (every present D3D_OK, at least 10 of them) and "d3d9
// imports" (this program imports d3d9.dll, so mtld3d redirected its EnumDisplaySettingsW import: the bounded mode
// list; d3d9_dynamic.cpp has a run-time load's); and "frames: <n>". With the argument "vsync" the device presents at
// D3DPRESENT_INTERVAL_ONE, as most games do, and "d3d9 vsync" says 60 more presents took under 5 s (a display paces
// them at its rate; a present the layer never reports presented makes each one wait out mtld3d's 1 s cap). Exits with
// the number of failures. With "--bench <draws> <frames>" (the probe of DXMTRS C4): no read-back, D3DPRESENT_INTERVAL_
// IMMEDIATE, each frame a Clear, <draws> DrawPrimitiveUP calls of the triangle (offset by a fraction of a pixel) and a
// Present, then one line "d3d9 bench: frames <F> draws <D> wall_ms <ms> cpu_ms <ms> vm_used_mb <mb>" (wall by
// QueryPerformanceCounter, CPU user+kernel by GetProcessTimes, the address space in use by GlobalMemoryStatusEx) and
// the number of failed module and imports rows as the exit status ("failed <n>": the draws that did not answer
// D3D_OK). "--bench <draws> <frames> tex" (DXMTRS C4b-3b) samples one 64x64 texture through a ps_2_0 each draw.
// "--bench <draws> <frames> prog" (DXMTRS C4b2-12)
// draws the triangle through a vs_2_0/ps_2_0 pair over an XYZ declaration instead, setting four VS float rows (the
// transform) before each draw; the line names the mode: "d3d9 bench: mode ff|prog|tex|texdepth|lit|buf|dyn frames ...".
// "--bench <draws> <frames> buf" (DXMTRS C4c-2a) draws the triangles from one static WRITEONLY vertex buffer of <draws>
// of them, draw d DrawPrimitive(TRIANGLELIST, 3 * d, 1); "dyn" from one DYNAMIC|WRITEONLY buffer of 256 and a cursor
// that runs across frames, each draw locking the next triangle NOOVERWRITE (the first DISCARD, at the wrap), writing
// it, unlocking and drawing it; a failed Lock or Unlock counts in "failed" too. Before the timed frames both read back
// frames until one's centre is the triangle's red (for up to 10 s: mtld3d leaves a draw out while its pipeline
// builds), else one more failure, since a dropped buffer draw answers D3D_OK.
// With "shaders" (DXMTRS C4b-2's gate rows, C4b2-11): 22 rows of programmable SM1-3 draws (shaders as DWORD token
// arrays, their assembly beside them), each printing one line "d3d9 <row> ok|FAIL <why>" (stdout unbuffered). Each row
// starts from reset()'s state; a failing Create, Set or Draw prints "FAIL <call> <hr>" at once; a drawing row asserts,
// in one frame (Clear, draws, GetRenderTargetData, Present), a drawn pixel in a colour other than the clear and an
// undrawn one that keeps it, retrying frames for up to 10 s (mtld3d leaves a draw out while its pipeline builds) unless
// "shaders strict" (the core compiles synchronously: the first frame must match). The mode has a 90 s budget; rows past
// it print "FAIL budget". Expected colours are D3DCOLOR_XRGB values, each channel within 1.
// With "state" ("state strict" under core; DXMTRS C4b-3a): 12 rows "d3d9 state-<row> ok|FAIL <why>" of the API state
// and inert objects (transforms, material and lights, stage, sampler and scissor states, stream frequency, swap chains,
// the back-buffer target switch, textures, a second device released before its objects, a buffer lock, state blocks,
// the UP draws' unbinds, a textured fixed-function draw and one whose stage reads no texture), with a 60 s budget.
// With "fixed" ("fixed strict" under core; DXMTRS C4b-3c): 31 rows "d3d9 ff-<row>|tex-vtx<row> ok|FAIL <why>" of the
// fixed-function vertex stage over untransformed layouts (transforms, lights, material sources, fog, texgen and texture
// transforms, vertex blending, points, a clip plane) and three vertex-sampled target textures the core refuses, with a
// 180 s budget. "--bench <draws> <frames> lit" draws the triangle untransformed and lit ("mode lit").
// With "targets" ("targets strict" under core; DXMTRS C4b-3d1): 27 rows "d3d9 rt-<row> ok|FAIL <why>" of render targets
// other than the back buffer (render-target textures, their levels and cube faces, CreateRenderTarget surfaces: drawn
// to, cleared, read back with GetRenderTargetData and sampled; formats, sRGB write, the viewport reset, automatic
// mipmaps, lifetime and order, a size past the device's limit, a target bound and unbound at index 1; since DXMTRS
// C4b-3d5 a multisampled surface resolved by StretchRect into a texture level, a surface and the back buffer, and the
// default pool's plain surface; since DXMTRS C4c-3a a lockable target's locks, the surfaces that do not lock, the back
// buffer's read-only lock and the front buffer's read), with a 180 s budget. The core refuses a StretchRect other than
// a whole-surface resolve (rt-msaa's into a 32x32 target).
// With "depth" ("depth strict" under core; DXMTRS C4b-3d2): 31 rows "d3d9 ds-<row> ok|FAIL <why>" of the depth-stencil,
// on a device made with an auto D24S8 (the other modes' devices have none): the depth and stencil tests and their
// state, Clear's Z and stencil, depth bias, the pretransformed clamp, a pixel shader's depth output, depth-stencil
// surfaces and depth textures and their levels, shadow, raw (INTZ, DF16, DF24) and fetch4 samples of a depth texture,
// a depth-stencil larger than the target, the pretransformed clip while one is bound, a multisampled surface beside a
// single-sample target (DXMTRS C4b-3d5), and what the core refuses (a depth-tested draw sampling the bound
// depth-stencil, a dynamic depth texture's lock), with a 150 s budget. "--bench <draws> <frames> tex depth" runs the tex bench on such a device, Z cleared each frame and
// tested ("mode texdepth").
// With "blend" ("blend strict" under core; DXMTRS C4b-3d3): 36 rows "d3d9 <cl|sc|sz|sr|bl>-<row> ok|FAIL <why>" on such
// a device: Clear with rects, under the scissor test and bounded by the viewport (colour, depth, stencil); the scissor
// test for draws; a depth-stencil larger or smaller than the target; sRGB write with Clear and under blending;
// blending (factors, operations, separate alpha, the blend factor colour, destination alpha by target), with a 180 s
// budget. The core refuses none of it.
// With "mrt" ("mrt strict" under core; DXMTRS C4b-3d4): 20 rows "d3d9 mrt-<row> ok|FAIL <why>" on such a device:
// render targets at index 1-3 (a pixel shader's oC1-oC3, an empty slot between bound ones, targets of other formats
// and of another size, the write masks of index 1-3, blending and sRGB write over several targets, Clear over every
// bound target, the scissor, the viewport and the depth test, the back buffer in a set, automatic mipmaps, order),
// with a 180 s budget. The core refuses none of it.
// With "msaa" ("msaa strict" under core; DXMTRS C4b-3d5): 19 rows "d3d9 ms-<row> ok|FAIL <why>" on a device whose
// back buffer and auto D24S8 have 4 samples: multisampled CreateRenderTarget surfaces (2, 4, 8 samples and
// non-maskable) read through StretchRect's resolve, the back buffer's too; edges, Clear with rects and the viewport,
// the depth test, a depth-stencil of another sample count, D3DRS_MULTISAMPLEMASK, alpha-to-coverage (ATOC and the A2M
// latch), sRGB write, blending, index 1-3 targets, the scissor test, a resolve's destination drawn again; what the
// creators and read-backs answer; since DXMTRS C4c-3a the back buffer read by a READONLY lock and by
// GetFrontBufferData. With a 180 s budget. The core refuses a StretchRect other than a whole-surface
// resolve (ms-desc's depth-stencil one).
// With "buffers" ("buffers strict" under core; DXMTRS C4c-1, C4c-2a, C4c-2b): 39 rows "d3d9 buf-<row> ok|FAIL <why>" of
// vertex and index buffers and the draws that read them, on a device with no depth-stencil: GetDirect3D; Lock and
// Unlock (pools, usages, DISCARD and NOOVERWRITE, a READONLY read-back, a lock held over draws, a partial relock);
// DrawPrimitive and DrawIndexedPrimitive (lists, strips, fans, a line list, the start vertex, a stream offset, base
// vertices of both signs, 16- and 32-bit indices); two streams, a stride of zero, a declared stream with nothing bound
// (buffer and UP draws), instancing; a vertex shader, the fixed-function stage and a texture over buffers; the answers
// without buffers and a released buffer; stream offsets off four bytes and attributes that end past their stride
// (buffer, UP, indexed and fan draws, three streams, the fixed-function stage); dynamic buffers locked again inside a
// frame (a whole lock's copy of the old bytes, 300 DISCARDs past a page's suballocations, index buffers under
// DISCARD, NOOVERWRITE and a fan, READONLY and NOOVERWRITE reads, a crossing layout's append) and a static WRITEONLY
// buffer locked again after its upload released its backing (buf-writeonly). With a 180 s budget. Under strict a
// layout no binding can fetch, an attribute wider than its stride (buf-wide), answers D3DERR_NOTAVAILABLE. "buffers
// [strict] skip=<row>[,<row>...]" prints "d3d9 <row> skip" for each listed row and does not run it.
#include <windows.h>
#include <d3d9.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

static int failures;
static void row(const char *what, bool ok, const char *why) {
    if (ok) printf("d3d9 %s ok\n", what);
    else printf("d3d9 %s FAIL %s\n", what, why), ++failures;
}

// The "shaders" mode (C4b2-11). Pixels: x right, y down, on the 256x256 back buffer; a clip-space x or y of c is pixel
// (c + 1) * 128 or (1 - c) * 128.
static IDirect3DDevice9 *dev;
static IDirect3DSurface9 *rtSurf, *sysSurf;
static bool strictMode;
static ULONGLONG budgetEnd;
static char why[384];

static bool fail(const char *call, HRESULT hr) {
    snprintf(why, sizeof why, "%s %08lx", call, (unsigned long)hr);
    return false;
}
// CK(Method, args...): dev->Method(args...), or the row's "FAIL Method <hr>".
#define CK(m, ...)                                                                                                     \
    do {                                                                                                               \
        HRESULT h_ = dev->m(__VA_ARGS__);                                                                              \
        if (FAILED(h_)) return fail(#m, h_);                                                                           \
    } while (0)

template <class T> struct Com {
    T *p = nullptr;
    ~Com() { if (p) p->Release(); }
};

// --- Shaders (D3D9 token streams: version, instructions with SM2+ length in bits 24-27, end 0x0000ffff) ---
static const DWORD VS11_C4[] = {
    0xfffe0101,                         // vs_1_1
    0x0000001f, 0x80000000, 0x900f0000, // dcl_position v0
    0x00000001, 0xc00f0000, 0x90e40000, // mov oPos, v0
    0x00000001, 0xd00f0000, 0xa0e40004, // mov oD0, c4
    0x0000ffff};
static const DWORD PS11_V0[] = {
    0xffff0101,                         // ps_1_1
    0x00000001, 0x800f0000, 0x90e40000, // mov r0, v0
    0x0000ffff};
static const DWORD VS_C0[] = {
    0xfffe0200,                         // vs_2_0
    0x0200001f, 0x80000000, 0x900f0000, // dcl_position v0
    0x02000001, 0xc00f0000, 0x90e40000, // mov oPos, v0
    0x02000001, 0xd00f0000, 0xa0e40000, // mov oD0, c0
    0x0000ffff};
static const DWORD VS_POS[] = {
    0xfffe0200,                         // vs_2_0
    0x0200001f, 0x80000000, 0x900f0000, // dcl_position v0
    0x02000001, 0xc00f0000, 0x90e40000, // mov oPos, v0
    0x0000ffff};
static const DWORD PS_V0[] = {
    0xffff0200,                         // ps_2_0
    0x0200001f, 0x80000000, 0x900f0000, // dcl v0
    0x02000001, 0x800f0800, 0x90e40000, // mov oC0, v0
    0x0000ffff};
static const DWORD PS_C0[] = {
    0xffff0200,                         // ps_2_0
    0x02000001, 0x800f0800, 0xa0e40000, // mov oC0, c0
    0x0000ffff};
static const DWORD VS_DEF[] = {
    0xfffe0200,                                                             // vs_2_0
    0x05000051, 0xa00f0000, 0x00000000, 0x3f800000, 0x00000000, 0x00000000, // def c0, 0, 1, 0, 0
    0x0200001f, 0x80000000, 0x900f0000,                                     // dcl_position v0
    0x02000001, 0xc00f0000, 0x90e40000,                                     // mov oPos, v0
    0x02000001, 0xd00f0000, 0xa0e40000,                                     // mov oD0, c0
    0x0000ffff};
static const DWORD PS_ADD_C3[] = {
    0xffff0200,                                     // ps_2_0
    0x0200001f, 0x80000000, 0x900f0000,             // dcl v0
    0x03000002, 0x800f0800, 0x90e40000, 0xa0e40003, // add oC0, v0, c3
    0x0000ffff};
static const DWORD VS30_NORMAL[] = {
    0xfffe0300,                         // vs_3_0
    0x0200001f, 0x80000000, 0x900f0000, // dcl_position v0
    0x0200001f, 0x80000000, 0xe00f0000, // dcl_position o0
    0x0200001f, 0x80000005, 0xe00f0001, // dcl_texcoord0 o1
    0x0200001f, 0x80000003, 0xe00f0002, // dcl_normal o2
    0x02000001, 0xe00f0000, 0x90e40000, // mov o0, v0
    0x02000001, 0xe00f0001, 0xa0e40001, // mov o1, c1 (the decoy)
    0x02000001, 0xe00f0002, 0xa0e40000, // mov o2, c0
    0x0000ffff};
static const DWORD PS30_NORMAL[] = {
    0xffff0300,                         // ps_3_0
    0x0200001f, 0x80000003, 0x900f0000, // dcl_normal v0
    0x02000001, 0x800f0800, 0x90e40000, // mov oC0, v0
    0x0000ffff};
static const DWORD VS_REP_I2[] = {
    0xfffe0200,                                                             // vs_2_0
    0x05000051, 0xa00f0001, 0x00000000, 0x00000000, 0x00000000, 0x3f800000, // def c1, 0, 0, 0, 1
    0x0200001f, 0x80000000, 0x900f0000,                                     // dcl_position v0
    0x02000001, 0xc00f0000, 0x90e40000,                                     // mov oPos, v0
    0x02000001, 0x800f0000, 0xa0e40001,                                     // mov r0, c1
    0x01000026, 0xf0e40002,                                                 // rep i2
    0x03000002, 0x800f0000, 0x80e40000, 0xa0e40000,                         //   add r0, r0, c0
    0x00000027,                                                             // endrep
    0x02000001, 0xd00f0000, 0x80e40000,                                     // mov oD0, r0
    0x0000ffff};
static const DWORD VS_IF_B5[] = {
    0xfffe0200,                                                             // vs_2_0
    0x05000051, 0xa00f0000, 0x3f800000, 0x00000000, 0x00000000, 0x3f800000, // def c0, 1, 0, 0, 1
    0x05000051, 0xa00f0001, 0x00000000, 0x3f800000, 0x00000000, 0x3f800000, // def c1, 0, 1, 0, 1
    0x0200001f, 0x80000000, 0x900f0000,                                     // dcl_position v0
    0x02000001, 0xc00f0000, 0x90e40000,                                     // mov oPos, v0
    0x02000001, 0x800f0000, 0xa0e40001,                                     // mov r0, c1
    0x01000028, 0xe0e40805,                                                 // if b5
    0x02000001, 0x800f0000, 0xa0e40000,                                     //   mov r0, c0
    0x0000002b,                                                             // endif
    0x02000001, 0xd00f0000, 0x80e40000,                                     // mov oD0, r0
    0x0000ffff};
static const DWORD VS30_POS[] = {
    0xfffe0300,                         // vs_3_0
    0x0200001f, 0x80000000, 0x900f0000, // dcl_position v0
    0x0200001f, 0x80000000, 0xe00f0000, // dcl_position o0
    0x02000001, 0xe00f0000, 0x90e40000, // mov o0, v0
    0x0000ffff};
static const DWORD PS30_IF_B5[] = {
    0xffff0300,                                                             // ps_3_0
    0x05000051, 0xa00f0000, 0x3f800000, 0x3f800000, 0x00000000, 0x3f800000, // def c0, 1, 1, 0, 1
    0x05000051, 0xa00f0001, 0x00000000, 0x3f800000, 0x3f800000, 0x3f800000, // def c1, 0, 1, 1, 1
    0x02000001, 0x800f0000, 0xa0e40001,                                     // mov r0, c1
    0x01000028, 0xe0e40805,                                                 // if b5
    0x02000001, 0x800f0000, 0xa0e40000,                                     //   mov r0, c0
    0x0000002b,                                                             // endif
    0x02000001, 0x800f0800, 0x80e40000,                                     // mov oC0, r0
    0x0000ffff};
static const DWORD VS_REL[] = {
    0xfffe0200,                                                             // vs_2_0
    0x05000051, 0xa00f0000, 0x43480000, 0x00000000, 0x00000000, 0x00000000, // def c0, 200, 0, 0, 0
    0x0200001f, 0x80000000, 0x900f0000,                                     // dcl_position v0
    0x02000001, 0xc00f0000, 0x90e40000,                                     // mov oPos, v0
    0x0200002e, 0xb0010000, 0xa0000000,                                     // mova a0.x, c0.x
    0x03000001, 0xd00f0000, 0xa0e42001, 0xb0000000,                         // mov oD0, c[a0.x + 1]
    0x0000ffff};
static const DWORD VS_TC5[] = {
    0xfffe0200,                         // vs_2_0
    0x0200001f, 0x80000000, 0x900f0000, // dcl_position v0
    0x0200001f, 0x80050005, 0x900f0001, // dcl_texcoord5 v1
    0x02000001, 0xc00f0000, 0x90e40000, // mov oPos, v0
    0x02000001, 0xd00f0000, 0x90e40001, // mov oD0, v1
    0x0000ffff};
static const DWORD VS30_COLOR[] = {
    0xfffe0300,                         // vs_3_0
    0x0200001f, 0x80000000, 0x900f0000, // dcl_position v0
    0x0200001f, 0x80000000, 0xe00f0000, // dcl_position o0
    0x0200001f, 0x8000000a, 0xe00f0003, // dcl_color o3
    0x02000001, 0xe00f0000, 0x90e40000, // mov o0, v0
    0x02000001, 0xe00f0003, 0xa0e40000, // mov o3, c0
    0x0000ffff};
static const DWORD VS_FOG[] = {
    0xfffe0200,                                                             // vs_2_0
    0x05000051, 0xa00f0000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, // def c0, 0, 0, 0, 0
    0x0200001f, 0x80000000, 0x900f0000,                                     // dcl_position v0
    0x02000001, 0xc00f0000, 0x90e40000,                                     // mov oPos, v0
    0x02000001, 0xc00f0001, 0xa0000000,                                     // mov oFog, c0.x
    0x0000ffff};
static const DWORD VS_CROSS[] = {
    0xfffe0200,                                                             // vs_2_0
    0x05000051, 0xa00f0000, 0x3f800000, 0x00000000, 0x00000000, 0x3f800000, // def c0, 1, 0, 0, 1
    0x0200001f, 0x80000000, 0x900f0000,                                     // dcl_position v0
    0x0200001f, 0x80000005, 0x900f0001,                                     // dcl_texcoord0 v1
    0x02000001, 0xc00f0000, 0x90e40000,                                     // mov oPos, v0
    0x02000001, 0x800f0000, 0xa0e40000,                                     // mov r0, c0
    0x02000001, 0x80020000, 0x90000001,                                     // mov r0.y, v1.x
    0x02000001, 0xd00f0000, 0x80e40000,                                     // mov oD0, r0
    0x0000ffff};
static const DWORD VS_BENCH[] = {
    0xfffe0200,                                                             // vs_2_0
    0x05000051, 0xa00f0004, 0x3f800000, 0x00000000, 0x00000000, 0x3f800000, // def c4, 1, 0, 0, 1
    0x0200001f, 0x80000000, 0x900f0000,                                     // dcl_position v0
    0x03000014, 0xc00f0000, 0x90e40000, 0xa0e40000,                         // m4x4 oPos, v0, c0
    0x02000001, 0xd00f0000, 0xa0e40004,                                     // mov oD0, c4
    0x0000ffff};

static const D3DVERTEXELEMENT9 DECL_XYZ[] = {
    {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};

struct V3 { float x, y, z; };
struct RV { float x, y, z, rhw; DWORD color; };
// A clip-space quad [x0, x1] x [y0, y1] as a strip, clockwise on screen (drawn under the default D3DCULL_CCW).
static void quad(V3 *v, float x0, float y0, float x1, float y1) {
    v[0] = {x0, y1, 0.5f}, v[1] = {x1, y1, 0.5f}, v[2] = {x0, y0, 0.5f}, v[3] = {x1, y0, 0.5f};
}
// The same quad counter-clockwise (culled by D3DCULL_CCW).
static void quadCcw(V3 *v, float x0, float y0, float x1, float y1) {
    v[0] = {x0, y1, 0.5f}, v[1] = {x0, y0, 0.5f}, v[2] = {x1, y1, 0.5f}, v[3] = {x1, y0, 0.5f};
}
// A pixel-space pretransformed quad, clockwise.
static void quadRhw(RV *v, float x0, float y0, float x1, float y1, DWORD c) {
    v[0] = {x0, y0, 0.5f, 1, c}, v[1] = {x1, y0, 0.5f, 1, c}, v[2] = {x0, y1, 0.5f, 1, c}, v[3] = {x1, y1, 0.5f, 1, c};
}
static bool strip(const V3 *v) {
    CK(DrawPrimitiveUP, D3DPT_TRIANGLESTRIP, 2, v, sizeof *v);
    return true;
}

static const float RED[4] = {1, 0, 0, 1}, GREEN[4] = {0, 1, 0, 1}, MAGENTA[4] = {1, 0, 1, 1}, CYAN[4] = {0, 1, 1, 1},
                   WHITE[4] = {1, 1, 1, 1};
static const DWORD BLUE = 0x0000ff;  // the clear colour of every row but srgb
static const D3DVIEWPORT9 FULL = {0, 0, 256, 256, 0.0f, 1.0f};

// A declaration, a VS and a PS (each optional), made from tokens and bound by use() (an absent shader unbinds).
struct Prog {
    Com<IDirect3DVertexDeclaration9> decl;
    Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DPixelShader9> ps;
};
static bool make(Prog &p, const D3DVERTEXELEMENT9 *decl, const DWORD *vs, const DWORD *ps) {
    if (decl) CK(CreateVertexDeclaration, decl, &p.decl.p);
    if (vs) CK(CreateVertexShader, vs, &p.vs.p);
    if (ps) CK(CreatePixelShader, ps, &p.ps.p);
    return true;
}
static bool use(const Prog &p) {
    if (p.decl.p) CK(SetVertexDeclaration, p.decl.p);
    CK(SetVertexShader, p.vs.p);
    CK(SetPixelShader, p.ps.p);
    return true;
}

static const D3DMATRIX IDENTITY = {{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}};
// Every state a row touches, back to D3D9's defaults (the back buffer the one render target and the viewport the whole
// of it; LIGHTING off, see below).
static bool reset() {
    CK(SetVertexShader, nullptr);
    CK(SetPixelShader, nullptr);
    // The back buffer at render target 0 and nothing at 1-3 (DXMTRS C4b-3d1's rows bind others).
    if (rtSurf) CK(SetRenderTarget, 0, rtSurf);
    for (DWORD i = 1; i < 4; i++) CK(SetRenderTarget, i, nullptr);
    CK(SetViewport, &FULL);
    static const DWORD rs[][2] = {
        {D3DRS_ALPHATESTENABLE, FALSE}, {D3DRS_ALPHAFUNC, D3DCMP_ALWAYS}, {D3DRS_ALPHAREF, 0},
        {D3DRS_FOGENABLE, FALSE}, {D3DRS_FOGVERTEXMODE, D3DFOG_NONE}, {D3DRS_FOGTABLEMODE, D3DFOG_NONE},
        {D3DRS_FOGCOLOR, 0}, {D3DRS_COLORWRITEENABLE, 0xf}, {D3DRS_FILLMODE, D3DFILL_SOLID},
        // Render targets 1-3's write masks (DXMTRS C4b-3d4's rows set them).
        {D3DRS_COLORWRITEENABLE1, 0xf}, {D3DRS_COLORWRITEENABLE2, 0xf}, {D3DRS_COLORWRITEENABLE3, 0xf},
        {D3DRS_CULLMODE, D3DCULL_CCW}, {D3DRS_CLIPPLANEENABLE, 0}, {D3DRS_SRGBWRITEENABLE, FALSE},
        // Blending and the scissor test (DXMTRS C4b-3d3's rows set them; the rect is reset by SetRenderTarget above).
        {D3DRS_ALPHABLENDENABLE, FALSE}, {D3DRS_SRCBLEND, D3DBLEND_ONE}, {D3DRS_DESTBLEND, D3DBLEND_ZERO},
        {D3DRS_BLENDOP, D3DBLENDOP_ADD}, {D3DRS_SEPARATEALPHABLENDENABLE, FALSE}, {D3DRS_SRCBLENDALPHA, D3DBLEND_ONE},
        {D3DRS_DESTBLENDALPHA, D3DBLEND_ZERO}, {D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD}, {D3DRS_BLENDFACTOR, 0xffffffff},
        {D3DRS_SCISSORTESTENABLE, FALSE}};
    for (const auto &s : rs) CK(SetRenderState, (D3DRENDERSTATETYPE)s[0], s[1]);
    // Textures, the cascade and the samplers at D3D9's defaults (DXMTRS C4b-3b's rows set them).
    CK(SetRenderState, D3DRS_TEXTUREFACTOR, 0xffffffff);
    for (DWORD stage = 0; stage < 8; stage++) {
        CK(SetTexture, stage, nullptr);
        CK(SetTextureStageState, stage, D3DTSS_COLOROP, stage ? D3DTOP_DISABLE : D3DTOP_MODULATE);
        CK(SetTextureStageState, stage, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        CK(SetTextureStageState, stage, D3DTSS_COLORARG2, D3DTA_CURRENT);
        CK(SetTextureStageState, stage, D3DTSS_ALPHAOP, stage ? D3DTOP_DISABLE : D3DTOP_SELECTARG1);
        CK(SetTextureStageState, stage, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        CK(SetTextureStageState, stage, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
        CK(SetTextureStageState, stage, D3DTSS_CONSTANT, 0);
        for (DWORD m = D3DTSS_BUMPENVMAT00; m <= D3DTSS_BUMPENVMAT11; m++) CK(SetTextureStageState, stage, (D3DTEXTURESTAGESTATETYPE)m, 0);
        static const DWORD ss[][2] = {{D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP}, {D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP},
                                      {D3DSAMP_ADDRESSW, D3DTADDRESS_WRAP}, {D3DSAMP_MAGFILTER, D3DTEXF_POINT},
                                      {D3DSAMP_MINFILTER, D3DTEXF_POINT}, {D3DSAMP_MIPFILTER, D3DTEXF_NONE},
                                      {D3DSAMP_MIPMAPLODBIAS, 0}, {D3DSAMP_MAXMIPLEVEL, 0}, {D3DSAMP_SRGBTEXTURE, 0}};
        for (const auto &s : ss) CK(SetSamplerState, stage, (D3DSAMPLERSTATETYPE)s[0], s[1]);
        // The vertex samplers (DXMTRS C4b-3c's rows bind them).
        if (stage < 4) {
            CK(SetTexture, D3DVERTEXTEXTURESAMPLER0 + stage, nullptr);
            for (const auto &s : ss) CK(SetSamplerState, D3DVERTEXTEXTURESAMPLER0 + stage, (D3DSAMPLERSTATETYPE)s[0], s[1]);
        }
        CK(SetTextureStageState, stage, D3DTSS_TEXCOORDINDEX, stage);
        CK(SetTextureStageState, stage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        CK(SetTransform, (D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + stage), &IDENTITY);
    }
    // The fixed-function vertex stage (DXMTRS C4b-3c's rows): D3D9's defaults, but for LIGHTING, which is off here
    // (D3D9's default is on; ff-lightdefault draws with it) so that a row lights only what it sets up.
    static const float one = 1.0f, zero = 0.0f;
    DWORD f1, f0;
    memcpy(&f1, &one, 4), memcpy(&f0, &zero, 4);
    const DWORD fs[][2] = {
        {D3DRS_LIGHTING, FALSE}, {D3DRS_AMBIENT, 0}, {D3DRS_COLORVERTEX, TRUE}, {D3DRS_SPECULARENABLE, FALSE},
        {D3DRS_LOCALVIEWER, TRUE}, {D3DRS_NORMALIZENORMALS, FALSE}, {D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1},
        {D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR2}, {D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL},
        {D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL}, {D3DRS_VERTEXBLEND, D3DVBF_DISABLE},
        {D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE}, {D3DRS_FOGSTART, f0}, {D3DRS_FOGEND, f1}, {D3DRS_FOGDENSITY, f1},
        {D3DRS_RANGEFOGENABLE, FALSE}, {D3DRS_POINTSIZE, f1}, {D3DRS_POINTSCALEENABLE, FALSE}, {D3DRS_POINTSCALE_A, f1},
        {D3DRS_POINTSCALE_B, f0}, {D3DRS_POINTSCALE_C, f0}};
    for (const auto &s : fs) CK(SetRenderState, (D3DRENDERSTATETYPE)s[0], s[1]);
    for (D3DTRANSFORMSTATETYPE t : {D3DTS_WORLD, D3DTS_VIEW, D3DTS_PROJECTION, D3DTS_WORLDMATRIX(1), D3DTS_WORLDMATRIX(5),
                                    D3DTS_WORLDMATRIX(9)})
        CK(SetTransform, t, &IDENTITY);
    const D3DMATERIAL9 none = {};
    CK(SetMaterial, &none);
    for (DWORD i : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 12u, 20u}) CK(LightEnable, i, FALSE);
    return true;
}

// A pixel to assert: (x, y) or, with w > 1, any of the w pixels from it rightwards; the channels in mask within 1.
struct Px { int x, y; DWORD want, mask = 0xffffff; int w = 1; };
static bool matches(DWORD got, const Px &p) {
    for (int s = 0; s < 24; s += 8)
        if ((p.mask >> s & 0xff) && abs((int)(got >> s & 0xff) - (int)(p.want >> s & 0xff)) > 1) return false;
    return true;
}
// Frames of: the full viewport, a Clear to clear, draw() in a scene, the read-back and a Present, until every pixel
// matches; the first frame only when strict, else for up to 10 s (within the budget).
template <class F> static bool frames(DWORD clear, const Px *px, int n, F draw) {
    const ULONGLONG now = GetTickCount64(), until = now + 10000 < budgetEnd ? now + 10000 : budgetEnd;
    for (int frame = 1;; frame++) {
        // A draw() that left another target bound (it failed part-way) does not have the retry clear that target.
        CK(SetRenderTarget, 0, rtSurf);
        CK(SetViewport, &FULL);
        CK(Clear, 0, nullptr, D3DCLEAR_TARGET, clear, 1.0f, 0);
        CK(BeginScene);
        if (!draw()) return dev->EndScene(), false;
        CK(EndScene);
        CK(GetRenderTargetData, rtSurf, sysSurf);
        D3DLOCKED_RECT lr = {};
        HRESULT hr = sysSurf->LockRect(&lr, nullptr, D3DLOCK_READONLY);
        if (FAILED(hr)) return fail("LockRect", hr);
        DWORD got[8] = {};
        bool all = true, okp[8] = {};
        for (int i = 0; i < n; i++) {
            const BYTE *row = (const BYTE *)lr.pBits + px[i].y * lr.Pitch;
            got[i] = *(const DWORD *)(row + px[i].x * 4) & 0xffffff;
            for (int k = 0; k < px[i].w && !okp[i]; k++)
                okp[i] = matches(*(const DWORD *)(row + (px[i].x + k) * 4), px[i]);
            all = all && okp[i];
        }
        sysSurf->UnlockRect();
        CK(Present, nullptr, nullptr, nullptr, nullptr);
        if (all) return true;
        if (strictMode || GetTickCount64() >= until) {
            int len = snprintf(why, sizeof why, "frame %d:", frame);
            for (int i = 0; i < n && len < (int)sizeof why; i++)
                if (!okp[i])
                    len += snprintf(why + len, sizeof why - len, " (%d,%d) %06lx want %06lx%s", px[i].x, px[i].y,
                                    (unsigned long)got[i], (unsigned long)px[i].want, px[i].w > 1 ? "+" : "");
            return false;
        }
    }
}

// caps: VS and PS 3.0.
static bool rowCaps() {
    D3DCAPS9 caps = {};
    CK(GetDeviceCaps, &caps);
    snprintf(why, sizeof why, "vs %08lx ps %08lx", (unsigned long)caps.VertexShaderVersion,
             (unsigned long)caps.PixelShaderVersion);
    return caps.VertexShaderVersion >= D3DVS_VERSION(3, 0) && caps.PixelShaderVersion >= D3DPS_VERSION(3, 0);
}
// vs1: vs_1_1 + ps_1_1, the colour in c4, c0-c3 and c5 decoys.
static bool rowVs1() {
    Prog p;
    if (!make(p, DECL_XYZ, VS11_C4, PS11_V0)) return false;
    const float c[6][4] = {{1, 0, 1, 1}, {1, 0, 1, 1}, {1, 0, 1, 1}, {1, 0, 1, 1}, {0, 1, 0, 1}, {1, 0, 1, 1}};
    V3 q[4];
    quad(q, -0.5f, -0.5f, 0.5f, 0.5f);
    const Px px[] = {{128, 128, 0x00ff00}, {16, 16, BLUE}};
    return frames(BLUE, px, 2, [&] {
        if (!use(p)) return false;
        CK(SetVertexShaderConstantF, 0, &c[0][0], 6);
        return strip(q);
    });
}
// vs2: vs_2_0 + ps_2_0, the VS's def'd green plus the PS's c3 red, c2 and c4 decoys.
static bool rowVs2() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_DEF, PS_ADD_C3)) return false;
    const float c[3][4] = {{0, 0, 1, 0}, {1, 0, 0, 1}, {0, 0, 1, 0}};
    V3 q[4];
    quad(q, -0.5f, -0.5f, 0.5f, 0.5f);
    const Px px[] = {{128, 128, 0xffff00}, {16, 16, BLUE}};
    return frames(BLUE, px, 2, [&] {
        if (!use(p)) return false;
        CK(SetPixelShaderConstantF, 2, &c[0][0], 3);
        return strip(q);
    });
}
// vs3: vs_3_0 + ps_3_0, the colour through NORMAL (o2 to the PS's v0), a decoy in TEXCOORD0 (o1).
static bool rowVs3() {
    Prog p;
    if (!make(p, DECL_XYZ, VS30_NORMAL, PS30_NORMAL)) return false;
    V3 q[4];
    quad(q, -0.5f, -0.5f, 0.5f, 0.5f);
    const Px px[] = {{128, 128, 0x00ffff}, {16, 16, BLUE}};
    return frames(BLUE, px, 2, [&] {
        if (!use(p)) return false;
        CK(SetVertexShaderConstantF, 0, CYAN, 1);
        CK(SetVertexShaderConstantF, 1, MAGENTA, 1);
        return strip(q);
    });
}
// consti: rep i2 (4) adds c0's 0.25 green four times; i0, i1 and i3 decoys (2).
static bool rowConsti() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_REP_I2, PS_V0)) return false;
    const int i[4][4] = {{2, 0, 0, 0}, {2, 0, 0, 0}, {4, 0, 0, 0}, {2, 0, 0, 0}};
    const float c0[4] = {0, 0.25f, 0, 0};
    V3 q[4];
    quad(q, -0.5f, -0.5f, 0.5f, 0.5f);
    const Px px[] = {{128, 128, 0x00ff00}, {16, 16, BLUE}};
    return frames(BLUE, px, 2, [&] {
        if (!use(p)) return false;
        CK(SetVertexShaderConstantF, 0, c0, 1);
        CK(SetVertexShaderConstantI, 0, &i[0][0], 4);
        return strip(q);
    });
}
// constb: if b5 in vs_2_0 (red, else green: top row) and ps_3_0 (yellow, else cyan: bottom row), b5 true on the left
// and false on the right, b0-b4 the opposite.
static bool rowConstb() {
    Prog v, f;
    if (!make(v, DECL_XYZ, VS_IF_B5, PS_V0) || !make(f, DECL_XYZ, VS30_POS, PS30_IF_B5)) return false;
    const BOOL on[6] = {FALSE, FALSE, FALSE, FALSE, FALSE, TRUE}, off[6] = {TRUE, TRUE, TRUE, TRUE, TRUE, FALSE};
    V3 tl[4], tr[4], bl[4], br[4];
    quad(tl, -0.875f, 0.125f, -0.125f, 0.875f), quad(tr, 0.125f, 0.125f, 0.875f, 0.875f);
    quad(bl, -0.875f, -0.875f, -0.125f, -0.125f), quad(br, 0.125f, -0.875f, 0.875f, -0.125f);
    const Px px[] = {{64, 64, 0xff0000}, {192, 64, 0x00ff00}, {64, 192, 0xffff00}, {192, 192, 0x00ffff},
                     {128, 128, BLUE}};
    return frames(BLUE, px, 5, [&] {
        if (!use(v)) return false;
        CK(SetVertexShaderConstantB, 0, on, 6);
        if (!strip(tl)) return false;
        CK(SetVertexShaderConstantB, 0, off, 6);
        if (!strip(tr) || !use(f)) return false;
        CK(SetPixelShaderConstantB, 0, on, 6);
        if (!strip(bl)) return false;
        CK(SetPixelShaderConstantB, 0, off, 6);
        return strip(br);
    });
}
// rel: mova a0.x = 200, then c[a0.x + 1] = c201 (white); c1, c200 and c202 decoys.
static bool rowRel() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_REL, PS_V0)) return false;
    const float c[3][4] = {{1, 0, 1, 1}, {1, 1, 1, 1}, {1, 0, 1, 1}};
    V3 q[4];
    quad(q, -0.5f, -0.5f, 0.5f, 0.5f);
    const Px px[] = {{128, 128, 0xffffff}, {16, 16, BLUE}};
    return frames(BLUE, px, 2, [&] {
        if (!use(p)) return false;
        CK(SetVertexShaderConstantF, 1, MAGENTA, 1);
        CK(SetVertexShaderConstantF, 200, &c[0][0], 3);
        return strip(q);
    });
}
// decl: the colour at TEXCOORD5 (offset 28), a decoy at TEXCOORD0 (offset 12).
static bool rowDecl() {
    static const D3DVERTEXELEMENT9 decl[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 12, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        {0, 28, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 5}, D3DDECL_END()};
    Prog p;
    if (!make(p, decl, VS_TC5, PS_V0)) return false;
    struct DV { V3 pos; float tc0[4], tc5[4]; } v[4];
    V3 q[4];
    quad(q, -0.5f, -0.5f, 0.5f, 0.5f);
    for (int i = 0; i < 4; i++) v[i] = {q[i], {1, 0, 1, 1}, {1, 1, 0, 1}};
    const Px px[] = {{128, 128, 0xffff00}, {16, 16, BLUE}};
    return frames(BLUE, px, 2, [&] {
        if (!use(p)) return false;
        CK(DrawPrimitiveUP, D3DPT_TRIANGLESTRIP, 2, v, sizeof *v);
        return true;
    });
}
// indexed: DrawIndexedPrimitiveUP with min_vertex_index 4 over vertices 4-7, 16-bit (left, red) and 32-bit (right,
// green); vertices 0-3 a decoy quad at the top.
static bool rowIndexed() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_C0, PS_V0)) return false;
    V3 l[8], r[8];
    quad(l, -0.25f, 0.75f, 0.25f, 0.95f), quad(r, -0.25f, 0.75f, 0.25f, 0.95f);
    quad(l + 4, -0.875f, -0.5f, -0.125f, 0.5f), quad(r + 4, 0.125f, -0.5f, 0.875f, 0.5f);
    const WORD i16[6] = {4, 5, 6, 6, 5, 7};
    const DWORD i32[6] = {4, 5, 6, 6, 5, 7};
    const Px px[] = {{64, 128, 0xff0000}, {192, 128, 0x00ff00}, {128, 20, BLUE}};
    return frames(BLUE, px, 3, [&] {
        if (!use(p)) return false;
        CK(SetVertexShaderConstantF, 0, RED, 1);
        CK(DrawIndexedPrimitiveUP, D3DPT_TRIANGLELIST, 4, 4, 2, i16, D3DFMT_INDEX16, l, sizeof *l);
        CK(SetVertexShaderConstantF, 0, GREEN, 1);
        CK(DrawIndexedPrimitiveUP, D3DPT_TRIANGLELIST, 4, 4, 2, i32, D3DFMT_INDEX32, r, sizeof *r);
        return true;
    });
}
// fan: a programmable fan (left, red) and a fixed-function pretransformed one (right, green); a zero-primitive
// DrawPrimitiveUP between them answers D3D_OK.
static bool rowFan() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_C0, PS_V0)) return false;
    const V3 f[4] = {{-0.875f, 0.5f, 0.5f}, {-0.125f, 0.5f, 0.5f}, {-0.125f, -0.5f, 0.5f}, {-0.875f, -0.5f, 0.5f}};
    const DWORD g = D3DCOLOR_XRGB(0, 255, 0);
    const RV r[4] = {{144, 64, 0.5f, 1, g}, {240, 64, 0.5f, 1, g}, {240, 192, 0.5f, 1, g}, {144, 192, 0.5f, 1, g}};
    const Px px[] = {{64, 128, 0xff0000}, {192, 128, 0x00ff00}, {128, 128, BLUE}};
    return frames(BLUE, px, 3, [&] {
        if (!use(p)) return false;
        CK(SetVertexShaderConstantF, 0, RED, 1);
        CK(DrawPrimitiveUP, D3DPT_TRIANGLEFAN, 2, f, sizeof *f);
        HRESULT hr = dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 0, f, sizeof *f);
        if (hr != D3D_OK) return fail("DrawPrimitiveUP(0 primitives)", hr);
        CK(SetVertexShader, nullptr);
        CK(SetPixelShader, nullptr);
        CK(SetFVF, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        CK(DrawPrimitiveUP, D3DPT_TRIANGLEFAN, 2, r, sizeof *r);
        return true;
    });
}
// bypass: an XYZRHW FVF with a VS bound (magenta, unused: pretransformed vertices bypass it) and a ps_2_0.
static bool rowBypass() {
    Prog p;
    if (!make(p, nullptr, VS_C0, PS_V0)) return false;
    RV v[4];
    quadRhw(v, 64, 64, 192, 192, D3DCOLOR_XRGB(0, 255, 255));
    const Px px[] = {{128, 128, 0x00ffff}, {16, 16, BLUE}};
    return frames(BLUE, px, 2, [&] {
        CK(SetFVF, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        if (!use(p)) return false;
        CK(SetVertexShaderConstantF, 0, MAGENTA, 1);
        CK(DrawPrimitiveUP, D3DPT_TRIANGLESTRIP, 2, v, sizeof *v);
        return true;
    });
}
// vsff: no PS; vs_2_0 writing oD0 (left, red) and vs_3_0 writing COLOR in o3 (right, green).
static bool rowVsff() {
    Prog a, b;
    if (!make(a, DECL_XYZ, VS_C0, nullptr) || !make(b, DECL_XYZ, VS30_COLOR, nullptr)) return false;
    V3 l[4], r[4];
    quad(l, -0.875f, -0.5f, -0.125f, 0.5f), quad(r, 0.125f, -0.5f, 0.875f, 0.5f);
    const Px px[] = {{64, 128, 0xff0000}, {192, 128, 0x00ff00}, {128, 128, BLUE}};
    return frames(BLUE, px, 3, [&] {
        if (!use(a)) return false;
        CK(SetVertexShaderConstantF, 0, RED, 1);
        if (!strip(l) || !use(b)) return false;
        CK(SetVertexShaderConstantF, 0, GREEN, 1);
        return strip(r);
    });
}
// alpha: a ps_2_0 green of alpha 0.25, ALPHATEST GREATER: ref 0x80 discards (left), ref 0x20 draws (right).
static bool rowAlpha() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_C0)) return false;
    const float c0[4] = {0, 1, 0, 0.25f};
    V3 l[4], r[4];
    quad(l, -0.875f, -0.5f, -0.125f, 0.5f), quad(r, 0.125f, -0.5f, 0.875f, 0.5f);
    const Px px[] = {{64, 128, BLUE}, {192, 128, 0x00ff00}};
    return frames(BLUE, px, 2, [&] {
        if (!use(p)) return false;
        CK(SetPixelShaderConstantF, 0, c0, 1);
        CK(SetRenderState, D3DRS_ALPHATESTENABLE, TRUE);
        CK(SetRenderState, D3DRS_ALPHAFUNC, D3DCMP_GREATER);
        CK(SetRenderState, D3DRS_ALPHAREF, 0x80);
        if (!strip(l)) return false;
        CK(SetRenderState, D3DRS_ALPHAREF, 0x20);
        return strip(r);
    });
}
// fog: vs_2_0 writing oFog 0 under linear vertex fog, a red ps_2_0: the fog colour (cyan).
static bool rowFog() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_FOG, PS_C0)) return false;
    V3 q[4];
    quad(q, -0.5f, -0.5f, 0.5f, 0.5f);
    const Px px[] = {{128, 128, 0x00ffff}, {16, 16, BLUE}};
    return frames(BLUE, px, 2, [&] {
        if (!use(p)) return false;
        CK(SetPixelShaderConstantF, 0, RED, 1);
        CK(SetRenderState, D3DRS_FOGENABLE, TRUE);
        CK(SetRenderState, D3DRS_FOGCOLOR, D3DCOLOR_XRGB(0, 255, 255));
        CK(SetRenderState, D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
        CK(SetRenderState, D3DRS_FOGTABLEMODE, D3DFOG_NONE);
        return strip(q);
    });
}
// mask: white through COLORWRITEENABLE red only over the blue clear (restored before the next Clear).
static bool rowMask() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_C0)) return false;
    V3 q[4];
    quad(q, -0.5f, -0.5f, 0.5f, 0.5f);
    const Px px[] = {{128, 128, 0xff00ff}, {16, 16, BLUE}};
    return frames(BLUE, px, 2, [&] {
        if (!use(p)) return false;
        CK(SetPixelShaderConstantF, 0, WHITE, 1);
        CK(SetRenderState, D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED);
        if (!strip(q)) return false;
        CK(SetRenderState, D3DRS_COLORWRITEENABLE, 0xf);
        return true;
    });
}
// wire: a WIREFRAME quad (pixels 64-192): its left edge drawn (somewhere in x 60-68 at y 128), its interior clear
// (100, 110, off the diagonal x + y = 256).
static bool rowWire() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_C0, PS_V0)) return false;
    V3 q[4];
    quad(q, -0.5f, -0.5f, 0.5f, 0.5f);
    const Px px[] = {{60, 128, 0x00ff00, 0xffffff, 9}, {100, 110, BLUE}};
    return frames(BLUE, px, 2, [&] {
        if (!use(p)) return false;
        CK(SetVertexShaderConstantF, 0, GREEN, 1);
        CK(SetRenderState, D3DRS_FILLMODE, D3DFILL_WIREFRAME);
        if (!strip(q)) return false;
        CK(SetRenderState, D3DRS_FILLMODE, D3DFILL_SOLID);
        return true;
    });
}
// cull: a counter-clockwise quad (left, red) under CULL_NONE until it shows; then under the default CCW cull it is gone
// and the clockwise quad beside it (right, green) drawn.
static bool rowCull() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_C0, PS_V0)) return false;
    V3 l[4], r[4];
    quadCcw(l, -0.875f, -0.5f, -0.125f, 0.5f), quad(r, 0.125f, -0.5f, 0.875f, 0.5f);
    const Px none[] = {{64, 128, 0xff0000}, {192, 128, BLUE}}, ccw[] = {{64, 128, BLUE}, {192, 128, 0x00ff00}};
    return frames(BLUE, none, 2, [&] {
        if (!use(p)) return false;
        CK(SetRenderState, D3DRS_CULLMODE, D3DCULL_NONE);
        CK(SetVertexShaderConstantF, 0, RED, 1);
        return strip(l);
    }) && frames(BLUE, ccw, 2, [&] {
        if (!use(p)) return false;
        CK(SetRenderState, D3DRS_CULLMODE, D3DCULL_CCW);
        CK(SetVertexShaderConstantF, 0, RED, 1);
        if (!strip(l)) return false;
        CK(SetVertexShaderConstantF, 0, GREEN, 1);
        return strip(r);
    });
}
// viewport: a full-target quad through the left-half viewport: left drawn, right clear.
static bool rowViewport() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_C0, PS_V0)) return false;
    V3 q[4];
    quad(q, -1, -1, 1, 1);
    const D3DVIEWPORT9 left = {0, 0, 128, 256, 0.0f, 1.0f};
    const Px px[] = {{64, 128, 0x00ff00}, {192, 128, BLUE}};
    return frames(BLUE, px, 2, [&] {
        if (!use(p)) return false;
        CK(SetVertexShaderConstantF, 0, GREEN, 1);
        CK(SetViewport, &left);
        if (!strip(q)) return false;
        CK(SetViewport, &FULL);
        return true;
    });
}
// clip: only clip plane 2 enabled, keeping x >= 0; planes 0 (x <= 0) and 1 (nothing) decoys: right drawn, left clear.
static bool rowClip() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_C0, PS_V0)) return false;
    V3 q[4];
    quad(q, -0.875f, -0.5f, 0.875f, 0.5f);
    const float p0[4] = {-1, 0, 0, 0}, p1[4] = {0, 0, 0, -1}, p2[4] = {1, 0, 0, 0};
    const Px px[] = {{64, 128, BLUE}, {192, 128, 0x00ff00}};
    return frames(BLUE, px, 2, [&] {
        if (!use(p)) return false;
        CK(SetVertexShaderConstantF, 0, GREEN, 1);
        CK(SetClipPlane, 0, p0);
        CK(SetClipPlane, 1, p1);
        CK(SetClipPlane, 2, p2);
        CK(SetRenderState, D3DRS_CLIPPLANEENABLE, D3DCLIPPLANE2);
        return strip(q);
    });
}
// srgb: SRGBWRITEENABLE: a Clear to linear 0.5 grey reads 188, a ps_2_0 draw of linear 0.25 137.
static bool rowSrgb() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_C0)) return false;
    CK(SetRenderState, D3DRS_SRGBWRITEENABLE, TRUE);
    const float c0[4] = {0.25f, 0.25f, 0.25f, 1};
    V3 q[4];
    quad(q, -0.5f, -0.5f, 0.5f, 0.5f);
    const Px px[] = {{128, 128, 0x898989}, {16, 16, 0xbcbcbc}};
    return frames(D3DCOLOR_XRGB(128, 128, 128), px, 2, [&] {
        if (!use(p)) return false;
        CK(SetPixelShaderConstantF, 0, c0, 1);
        return strip(q);
    });
}
// crossing: TEXCOORD0 FLOAT2 at offset 20 of a 16-byte stride (it ends past the stride) feeding the colour's green, red
// constant: drawn, red (green, the next vertex's y, not asserted).
static bool rowCrossing() {
    static const D3DVERTEXELEMENT9 decl[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 20, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0}, D3DDECL_END()};
    Prog p;
    if (!make(p, decl, VS_CROSS, PS_V0)) return false;
    float v[6][4] = {};  // 16-byte vertices, two past the four drawn: the last one's attribute reads stay in the array
    V3 q[4];
    quad(q, -0.5f, -0.5f, 0.5f, 0.5f);
    for (int i = 0; i < 4; i++) v[i][0] = q[i].x, v[i][1] = q[i].y, v[i][2] = q[i].z;
    const Px px[] = {{128, 128, 0xff0000, 0xff00ff}, {16, 16, BLUE}};
    return frames(BLUE, px, 2, [&] {
        if (!use(p)) return false;
        CK(DrawPrimitiveUP, D3DPT_TRIANGLESTRIP, 2, v, 16);
        return true;
    });
}
// getters: each un-refused setter's Get* returns what was set.
static bool rowGetters() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_C0, PS_V0)) return false;
    const D3DVIEWPORT9 vp = {8, 16, 100, 200, 0.25f, 0.75f};
    D3DVIEWPORT9 gvp = {};
    CK(SetViewport, &vp);
    CK(GetViewport, &gvp);
    if (memcmp(&vp, &gvp, sizeof vp)) return snprintf(why, sizeof why, "GetViewport mismatch"), false;
    const float plane[4] = {1, 2, 3, 4};
    float gplane[4] = {};
    CK(SetClipPlane, 3, plane);
    CK(GetClipPlane, 3, gplane);
    if (memcmp(plane, gplane, sizeof plane)) return snprintf(why, sizeof why, "GetClipPlane mismatch"), false;
    CK(SetFVF, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    DWORD fvf = 0;
    CK(GetFVF, &fvf);
    if (fvf != (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)) return snprintf(why, sizeof why, "GetFVF %08lx", fvf), false;
    if (!use(p)) return false;
    Com<IDirect3DVertexDeclaration9> gd;
    Com<IDirect3DVertexShader9> gvs;
    Com<IDirect3DPixelShader9> gps;
    CK(GetVertexDeclaration, &gd.p);
    CK(GetVertexShader, &gvs.p);
    CK(GetPixelShader, &gps.p);
    if (gd.p != p.decl.p || gvs.p != p.vs.p || gps.p != p.ps.p)
        return snprintf(why, sizeof why, "Get{VertexDeclaration,VertexShader,PixelShader} mismatch"), false;
    const float f[2][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}};
    const int n[4] = {1, -2, 3, 4};
    const BOOL b[2] = {TRUE, FALSE};
    float gf[2][4] = {};
    int gn[4] = {};
    BOOL gb[2] = {FALSE, TRUE};
    CK(SetVertexShaderConstantF, 7, &f[0][0], 2);
    CK(GetVertexShaderConstantF, 7, &gf[0][0], 2);
    CK(SetVertexShaderConstantI, 3, n, 1);
    CK(GetVertexShaderConstantI, 3, gn, 1);
    CK(SetVertexShaderConstantB, 5, b, 2);
    CK(GetVertexShaderConstantB, 5, gb, 2);
    if (memcmp(f, gf, sizeof f) || memcmp(n, gn, sizeof n) || memcmp(b, gb, sizeof b))
        return snprintf(why, sizeof why, "GetVertexShaderConstant{F,I,B} mismatch"), false;
    memset(gf, 0, sizeof gf), memset(gn, 0, sizeof gn), gb[0] = FALSE, gb[1] = TRUE;
    CK(SetPixelShaderConstantF, 7, &f[0][0], 2);
    CK(GetPixelShaderConstantF, 7, &gf[0][0], 2);
    CK(SetPixelShaderConstantI, 3, n, 1);
    CK(GetPixelShaderConstantI, 3, gn, 1);
    CK(SetPixelShaderConstantB, 5, b, 2);
    CK(GetPixelShaderConstantB, 5, gb, 2);
    if (memcmp(f, gf, sizeof f) || memcmp(n, gn, sizeof n) || memcmp(b, gb, sizeof b))
        return snprintf(why, sizeof why, "GetPixelShaderConstant{F,I,B} mismatch"), false;
    return true;
}

static void shaderRows(IDirect3DDevice9 *d, bool strict) {
    static const struct { const char *name; bool (*run)(); } rows[] = {
        {"caps", rowCaps},       {"vs1", rowVs1},       {"vs2", rowVs2},         {"vs3", rowVs3},
        {"consti", rowConsti},   {"constb", rowConstb}, {"rel", rowRel},         {"decl", rowDecl},
        {"indexed", rowIndexed}, {"fan", rowFan},       {"bypass", rowBypass},   {"vsff", rowVsff},
        {"alpha", rowAlpha},     {"fog", rowFog},       {"mask", rowMask},       {"wire", rowWire},
        {"cull", rowCull},       {"viewport", rowViewport}, {"clip", rowClip},   {"srgb", rowSrgb},
        {"crossing", rowCrossing}, {"getters", rowGetters}};
    dev = d, strictMode = strict, budgetEnd = GetTickCount64() + 90000;
    HRESULT hr = dev->GetRenderTarget(0, &rtSurf);
    if (SUCCEEDED(hr))
        hr = dev->CreateOffscreenPlainSurface(256, 256, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sysSurf, nullptr);
    for (const auto &r : rows) {
        if (!r.run) continue;  // a row of the other lanes (tex-big256: 32-bit only)
        why[0] = 0;
        bool ok;
        if (FAILED(hr)) ok = fail("GetRenderTarget/CreateOffscreenPlainSurface", hr);
        else if (GetTickCount64() >= budgetEnd) ok = false, snprintf(why, sizeof why, "budget");
        else ok = reset() && r.run();
        row(r.name, ok, why);
    }
    if (sysSurf) sysSurf->Release();
    if (rtSurf) rtSurf->Release();
}

// The "state" mode (DXMTRS C4b-3a): the API state and inert objects the core runtime takes, each row starting from
// reset() and leaving what it set back at D3D9's defaults. Rows whose answer differs by runtime (a draw the core
// refuses) take either answer; d3d9-check proves which runtime gave it from the launch's refusal lines.
static HWND stateHwnd;
static IDirect3D9 *stateD3d;

static D3DMATRIX diag(float a) {
    D3DMATRIX m = {};
    m._11 = m._22 = m._33 = a, m._44 = 1;
    return m;
}
// xform: SetTransform then MultiplyTransform (2 * 3), read back 6 on the diagonal.
static bool rowXform() {
    const D3DMATRIX two = diag(2), three = diag(3), six = diag(6), one = diag(1);
    D3DMATRIX got = {};
    CK(SetTransform, D3DTS_WORLD, &two);
    CK(MultiplyTransform, D3DTS_WORLD, &three);
    CK(GetTransform, D3DTS_WORLD, &got);
    CK(SetTransform, D3DTS_WORLD, &one);
    if (memcmp(&got, &six, sizeof got)) return snprintf(why, sizeof why, "GetTransform mismatch"), false;
    return true;
}
// material: SetMaterial, SetLight, LightEnable read back.
static bool rowMaterial() {
    D3DMATERIAL9 m = {}, gm = {};
    m.Diffuse = {0.25f, 0.5f, 0.75f, 1}, m.Power = 8;
    CK(SetMaterial, &m);
    CK(GetMaterial, &gm);
    D3DLIGHT9 l = {}, gl = {};
    l.Type = D3DLIGHT_DIRECTIONAL, l.Diffuse = {1, 1, 1, 1}, l.Direction = {0, 0, 1};
    CK(SetLight, 0, &l);
    CK(GetLight, 0, &gl);
    CK(LightEnable, 0, TRUE);
    BOOL on = FALSE;
    CK(GetLightEnable, 0, &on);
    CK(LightEnable, 0, FALSE);
    if (memcmp(&m, &gm, sizeof m) || memcmp(&l, &gl, sizeof l) || !on)
        return snprintf(why, sizeof why, "Get{Material,Light,LightEnable} mismatch"), false;
    return true;
}
// tss: a texture-stage state, a pixel and a vertex sampler state, the scissor rect and a stream frequency read back.
static bool rowTss() {
    DWORD v = 0;
    CK(SetTextureStageState, 1, D3DTSS_COLOROP, D3DTOP_MODULATE);
    CK(GetTextureStageState, 1, D3DTSS_COLOROP, &v);
    CK(SetTextureStageState, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    if (v != D3DTOP_MODULATE) return snprintf(why, sizeof why, "GetTextureStageState %lu", v), false;
    CK(SetSamplerState, 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    CK(GetSamplerState, 0, D3DSAMP_MAGFILTER, &v);
    CK(SetSamplerState, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    if (v != D3DTEXF_LINEAR) return snprintf(why, sizeof why, "GetSamplerState %lu", v), false;
    CK(SetSamplerState, D3DVERTEXTEXTURESAMPLER0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    CK(GetSamplerState, D3DVERTEXTEXTURESAMPLER0, D3DSAMP_ADDRESSU, &v);
    CK(SetSamplerState, D3DVERTEXTEXTURESAMPLER0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    if (v != D3DTADDRESS_CLAMP) return snprintf(why, sizeof why, "GetSamplerState (vertex) %lu", v), false;
    const RECT sr = {1, 2, 30, 40}, full = {0, 0, 256, 256};
    RECT gr = {};
    CK(SetScissorRect, &sr);
    CK(GetScissorRect, &gr);
    CK(SetScissorRect, &full);
    if (memcmp(&sr, &gr, sizeof sr)) return snprintf(why, sizeof why, "GetScissorRect mismatch"), false;
    UINT f = 0;
    CK(SetStreamSourceFreq, 1, 3);
    CK(GetStreamSourceFreq, 1, &f);
    CK(SetStreamSourceFreq, 1, 1);
    if (f != 3) return snprintf(why, sizeof why, "GetStreamSourceFreq %u", f), false;
    return true;
}
// swapchain: an additional chain, GetSwapChain(0), one chain counted, the chain's present parameters, device and
// back buffer.
static bool rowSwapchain() {
    D3DPRESENT_PARAMETERS pp = {}, gp = {};
    pp.Windowed = TRUE, pp.SwapEffect = D3DSWAPEFFECT_DISCARD, pp.hDeviceWindow = stateHwnd;
    Com<IDirect3DSwapChain9> sc, sc0;
    Com<IDirect3DDevice9> d;
    Com<IDirect3DSurface9> bb;
    CK(CreateAdditionalSwapChain, &pp, &sc.p);
    CK(GetSwapChain, 0, &sc0.p);
    if (dev->GetNumberOfSwapChains() != 1)
        return snprintf(why, sizeof why, "GetNumberOfSwapChains %u", dev->GetNumberOfSwapChains()), false;
    HRESULT hr = sc.p->GetPresentParameters(&gp);
    if (FAILED(hr)) return fail("GetPresentParameters", hr);
    if ((hr = sc.p->GetDevice(&d.p)) != D3D_OK || d.p != dev) return fail("SwapChain GetDevice", hr);
    if (FAILED(hr = sc.p->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb.p))) return fail("GetBackBuffer", hr);
    return true;
}
// rt: the additional chain's back buffer as render target 0, a red clear read back through render target 0's original
// surface (the chains alias one back buffer); SetRenderTarget(1, NULL); no depth surface; SetDepthStencilSurface(NULL).
static bool rowRt() {
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE, pp.SwapEffect = D3DSWAPEFFECT_DISCARD, pp.hDeviceWindow = stateHwnd;
    Com<IDirect3DSwapChain9> sc;
    Com<IDirect3DSurface9> bb, ds;
    CK(CreateAdditionalSwapChain, &pp, &sc.p);
    HRESULT hr = sc.p->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb.p);
    if (FAILED(hr)) return fail("GetBackBuffer", hr);
    CK(SetRenderTarget, 0, bb.p);
    CK(Clear, 0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(255, 0, 0), 1.0f, 0);
    CK(GetRenderTargetData, rtSurf, sysSurf);
    D3DLOCKED_RECT lr = {};
    if (FAILED(hr = sysSurf->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return fail("LockRect", hr);
    const DWORD got = *(const DWORD *)((const BYTE *)lr.pBits + 128 * lr.Pitch + 128 * 4) & 0xffffff;
    sysSurf->UnlockRect();
    CK(SetRenderTarget, 0, rtSurf);
    CK(Present, nullptr, nullptr, nullptr, nullptr);
    if (got != 0xff0000) return snprintf(why, sizeof why, "back buffer %06lx want ff0000", (unsigned long)got), false;
    CK(SetRenderTarget, 1, nullptr);
    hr = dev->GetDepthStencilSurface(&ds.p);
    if (hr != D3DERR_NOTFOUND || ds.p) return fail("GetDepthStencilSurface", hr);
    CK(SetDepthStencilSurface, nullptr);
    return true;
}
// tex: 2D, cube, volume, managed and system-memory textures; a level, its description, the level count, SetLOD; the
// 2D one bound on pixel stage 0 and vertex slot 0 and read back; the system-memory one bound on stage 1.
static bool rowTex() {
    Com<IDirect3DTexture9> t, m, sys;
    Com<IDirect3DCubeTexture9> c;
    Com<IDirect3DVolumeTexture9> v;
    Com<IDirect3DSurface9> level;
    Com<IDirect3DBaseTexture9> g;
    CK(CreateTexture, 64, 64, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &t.p, nullptr);
    CK(CreateTexture, 64, 64, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &m.p, nullptr);
    CK(CreateTexture, 16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys.p, nullptr);
    CK(CreateCubeTexture, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &c.p, nullptr);
    CK(CreateVolumeTexture, 8, 8, 8, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &v.p, nullptr);
    HRESULT hr = t.p->GetSurfaceLevel(1, &level.p);
    if (FAILED(hr)) return fail("GetSurfaceLevel", hr);
    D3DSURFACE_DESC desc = {};
    if (FAILED(hr = t.p->GetLevelDesc(1, &desc)) || desc.Width != 32) return fail("GetLevelDesc", hr);
    if (t.p->GetLevelCount() != 7) return snprintf(why, sizeof why, "GetLevelCount %lu", t.p->GetLevelCount()), false;
    m.p->SetLOD(2);
    if (m.p->GetLOD() != 2) return snprintf(why, sizeof why, "GetLOD %lu", m.p->GetLOD()), false;
    CK(SetTexture, 0, t.p);
    CK(SetTexture, D3DVERTEXTEXTURESAMPLER0, t.p);
    CK(GetTexture, 0, &g.p);
    bool same = g.p == t.p;
    g.p->Release(), g.p = nullptr;
    CK(GetTexture, D3DVERTEXTEXTURESAMPLER0, &g.p);
    same = same && g.p == t.p;
    hr = dev->SetTexture(1, sys.p);
    CK(SetTexture, 0, nullptr);
    CK(SetTexture, 1, nullptr);
    CK(SetTexture, D3DVERTEXTEXTURESAMPLER0, nullptr);
    if (FAILED(hr) && hr != D3DERR_INVALIDCALL) return fail("SetTexture (system memory)", hr);
    if (!same) return snprintf(why, sizeof why, "GetTexture mismatch"), false;
    return true;
}
// late: a second device's (on a window of its own) texture and vertex buffer outlive the application's last
// reference to that device.
static bool rowLate() {
    HWND w = CreateWindowW(L"d3d9_triangle", L"d3d9_triangle late", WS_OVERLAPPEDWINDOW, 400, 100, 64, 64, nullptr,
                           nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!w) return snprintf(why, sizeof why, "CreateWindowW"), false;
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE, pp.SwapEffect = D3DSWAPEFFECT_DISCARD, pp.hDeviceWindow = w;
    IDirect3DDevice9 *d2 = nullptr;
    HRESULT hr = stateD3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, w, D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                        &pp, &d2);
    if (FAILED(hr)) return DestroyWindow(w), fail("CreateDevice", hr);
    IDirect3DTexture9 *t = nullptr;
    IDirect3DVertexBuffer9 *vb = nullptr;
    hr = d2->CreateTexture(16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr);
    if (SUCCEEDED(hr)) hr = d2->CreateVertexBuffer(64, 0, 0, D3DPOOL_DEFAULT, &vb, nullptr);
    if (SUCCEEDED(hr)) hr = d2->SetTexture(0, t);
    if (SUCCEEDED(hr)) hr = d2->SetStreamSource(0, vb, 0, 16);
    d2->Release();
    if (t) t->Release();
    if (vb) vb->Release();
    DestroyWindow(w);
    return SUCCEEDED(hr) || fail("second device", hr);
}
// lock: a dynamic vertex buffer's Lock(DISCARD) and its Unlock answer D3D_OK in every lane (the core's since DXMTRS
// C4c-1).
static bool rowLock() {
    Com<IDirect3DVertexBuffer9> vb;
    CK(CreateVertexBuffer, 64, D3DUSAGE_DYNAMIC, 0, D3DPOOL_DEFAULT, &vb.p, nullptr);
    void *data = nullptr;
    HRESULT hr = vb.p->Lock(0, 0, &data, D3DLOCK_DISCARD);
    if (hr != D3D_OK) return fail("Lock", hr);
    hr = vb.p->Unlock();
    return hr == D3D_OK || fail("Unlock", hr);
}
// sb: a state block of everything captures a bound texture and stream; Apply after unbinding binds them again; a
// recorded block applies a transform; both answer GetDevice.
static bool rowSb() {
    Com<IDirect3DTexture9> t;
    Com<IDirect3DVertexBuffer9> vb, gvb;
    Com<IDirect3DStateBlock9> sb, rec;
    Com<IDirect3DBaseTexture9> g;
    Com<IDirect3DDevice9> d;
    CK(CreateTexture, 16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &t.p, nullptr);
    CK(CreateVertexBuffer, 64, 0, 0, D3DPOOL_DEFAULT, &vb.p, nullptr);
    CK(SetTexture, 0, t.p);
    CK(SetStreamSource, 0, vb.p, 0, 16);
    CK(CreateStateBlock, D3DSBT_ALL, &sb.p);
    CK(SetTexture, 0, nullptr);
    CK(SetStreamSource, 0, nullptr, 0, 0);
    HRESULT hr = sb.p->Apply();
    if (FAILED(hr)) return fail("Apply", hr);
    UINT off = 0, stride = 0;
    CK(GetTexture, 0, &g.p);
    CK(GetStreamSource, 0, &gvb.p, &off, &stride);
    const bool same = g.p == t.p && gvb.p == vb.p && stride == 16;
    CK(SetTexture, 0, nullptr);
    CK(SetStreamSource, 0, nullptr, 0, 0);
    if (!same) return snprintf(why, sizeof why, "Apply did not restore the texture and stream"), false;
    const D3DMATRIX two = diag(2), one = diag(1);
    D3DMATRIX got = {};
    CK(BeginStateBlock);
    CK(SetTransform, D3DTS_VIEW, &two);
    CK(EndStateBlock, &rec.p);
    CK(SetTransform, D3DTS_VIEW, &one);
    if (FAILED(hr = rec.p->Apply())) return fail("Apply (recorded)", hr);
    CK(GetTransform, D3DTS_VIEW, &got);
    CK(SetTransform, D3DTS_VIEW, &one);
    if (memcmp(&got, &two, sizeof got)) return snprintf(why, sizeof why, "recorded Apply mismatch"), false;
    if ((hr = rec.p->GetDevice(&d.p)) != D3D_OK || d.p != dev) return fail("StateBlock GetDevice", hr);
    return true;
}
// upunbind: a UP draw unbinds stream 0, an indexed UP draw the indices too.
static bool rowUpUnbind() {
    Com<IDirect3DVertexBuffer9> vb, gvb;
    Com<IDirect3DIndexBuffer9> ib, gib;
    CK(CreateVertexBuffer, 64, 0, 0, D3DPOOL_DEFAULT, &vb.p, nullptr);
    CK(CreateIndexBuffer, 64, 0, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &ib.p, nullptr);
    RV q[4];
    quadRhw(q, 96, 96, 160, 160, D3DCOLOR_XRGB(255, 255, 255));
    const WORD idx[6] = {0, 1, 2, 2, 1, 3};
    CK(SetFVF, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    CK(SetStreamSource, 0, vb.p, 0, sizeof(RV));
    CK(BeginScene);
    CK(DrawPrimitiveUP, D3DPT_TRIANGLESTRIP, 2, q, sizeof *q);
    UINT off = 0, stride = 0;
    CK(GetStreamSource, 0, &gvb.p, &off, &stride);
    CK(SetStreamSource, 0, vb.p, 0, sizeof(RV));
    CK(SetIndices, ib.p);
    CK(DrawIndexedPrimitiveUP, D3DPT_TRIANGLELIST, 0, 4, 2, idx, D3DFMT_INDEX16, q, sizeof *q);
    CK(EndScene);
    CK(GetIndices, &gib.p);
    if (gvb.p || gib.p) return snprintf(why, sizeof why, "stream 0 %p indices %p after a UP draw", gvb.p, gib.p), false;
    return true;
}
// fftex: a green texture on stage 0 (MODULATE TEXTURE DIFFUSE) under a white pretransformed quad over a blue clear:
// drawn green (D3D_OK), which strict (the core, which draws textures since DXMTRS C4b-3b) requires; the other lanes
// also take a refusal (D3DERR_NOTAVAILABLE) and blue. fftexoff: the same with stage 0 disabled, drawn white in every
// lane (the cascade reads no texture, so nothing refuses it).
struct TV { float x, y, z, rhw; DWORD color; float u, v; };
static bool texturedQuad(bool stageOn) {
    Com<IDirect3DTexture9> t;
    CK(CreateTexture, 4, 4, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &t.p, nullptr);
    D3DLOCKED_RECT lr = {};
    if (SUCCEEDED(t.p->LockRect(0, &lr, nullptr, 0))) {
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++) ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] = D3DCOLOR_XRGB(0, 255, 0);
        t.p->UnlockRect(0);
    }
    const DWORD white = D3DCOLOR_XRGB(255, 255, 255);
    const TV q[4] = {{64, 64, 0.5f, 1, white, 0, 0}, {192, 64, 0.5f, 1, white, 1, 0},
                     {64, 192, 0.5f, 1, white, 0, 1}, {192, 192, 0.5f, 1, white, 1, 1}};
    CK(SetFVF, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    CK(SetTexture, 0, t.p);
    CK(SetTextureStageState, 0, D3DTSS_COLOROP, stageOn ? D3DTOP_MODULATE : D3DTOP_DISABLE);
    Px px[] = {{128, 128, 0x00ff00}, {16, 16, BLUE}};
    const bool ok = frames(BLUE, px, 2, [&] {
        HRESULT hr = dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof *q);
        if (hr != D3D_OK && !(stageOn && !strictMode && hr == D3DERR_NOTAVAILABLE)) return fail("DrawPrimitiveUP", hr);
        px[0].want = hr != D3D_OK ? BLUE : stageOn ? 0x00ff00 : 0xffffff;
        return true;
    });
    dev->SetTexture(0, nullptr);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    return ok;
}
static bool rowFftex() { return texturedQuad(true); }
static bool rowFftexOff() { return texturedQuad(false); }

static void stateRows(IDirect3DDevice9 *d, IDirect3D9 *d3d, HWND hwnd, bool strict) {
    static const struct { const char *name; bool (*run)(); } rows[] = {
        {"state-xform", rowXform}, {"state-material", rowMaterial}, {"state-tss", rowTss},
        {"state-swapchain", rowSwapchain}, {"state-rt", rowRt}, {"state-tex", rowTex}, {"state-late", rowLate},
        {"state-lock", rowLock}, {"state-sb", rowSb}, {"state-upunbind", rowUpUnbind}, {"state-fftex", rowFftex},
        {"state-fftexoff", rowFftexOff}};
    dev = d, stateD3d = d3d, stateHwnd = hwnd, strictMode = strict, budgetEnd = GetTickCount64() + 60000;
    HRESULT hr = dev->GetRenderTarget(0, &rtSurf);
    if (SUCCEEDED(hr))
        hr = dev->CreateOffscreenPlainSurface(256, 256, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sysSurf, nullptr);
    for (const auto &r : rows) {
        why[0] = 0;
        bool ok;
        if (FAILED(hr)) ok = fail("GetRenderTarget/CreateOffscreenPlainSurface", hr);
        else if (GetTickCount64() >= budgetEnd) ok = false, snprintf(why, sizeof why, "budget");
        else ok = reset() && r.run();
        row(r.name, ok, why);
    }
    if (sysSurf) sysSurf->Release();
    if (rtSurf) rtSurf->Release();
}

// The "textures" mode (DXMTRS C4b-3b): sampled textures under the fixed-function cascade and ps_1_1-2_0, each row
// a frame of pretransformed quads whose texcoords are constant at a texel centre (unless the row is about addressing
// or filtering), asserting the sampled colour. Alpha is checked by routing it into the colour (a ps_2_0 `mov oC0,
// r0.wwww`). The core refuses none of it since DXMTRS C4c-3a, which locks a render-target texture's levels (in "fixed",
// a depth texture at a vertex sampler answers D3D_OK under mtld3d and D3DERR_NOTAVAILABLE under core, "fixed strict").
struct TV3 { float x, y, z, rhw; DWORD color; float u, v, w; };
// A pixel-space quad, clockwise, its texcoords (u, v, w) at every vertex.
static void tquad(TV3 *q, float x0, float y0, float x1, float y1, float u, float v, float w = 0) {
    const DWORD c = D3DCOLOR_XRGB(255, 255, 255);
    q[0] = {x0, y0, 0.5f, 1, c, u, v, w}, q[1] = {x1, y0, 0.5f, 1, c, u, v, w}, q[2] = {x0, y1, 0.5f, 1, c, u, v, w},
    q[3] = {x1, y1, 0.5f, 1, c, u, v, w};
}
static const DWORD FVF_T2 = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE3(0);
static bool tdraw(const TV3 *q) {
    CK(SetFVF, FVF_T2);
    CK(DrawPrimitiveUP, D3DPT_TRIANGLESTRIP, 2, q, sizeof *q);
    return true;
}
// ps_2_0 { dcl t0.xyz; dcl_<kind> s<slot>; texld r0, t0, s<slot>; mov oC0, r0 (or r0.wwww) }.
static void psTex(DWORD *t, DWORD kind, DWORD slot, bool alpha) {
    const DWORD toks[] = {0xffff0200, 0x0200001f, 0x80000000, 0xb0070000, 0x0200001f, 0x80000000 | kind << 27,
                          0xa00f0800 | slot, 0x03000042, 0x800f0000, 0xb0e40000, 0xa0e40800 | slot, 0x02000001,
                          0x800f0800, alpha ? 0x80ff0000u : 0x80e40000u, 0x0000ffff};
    memcpy(t, toks, sizeof toks);
}
enum { K2D = 2, KCUBE = 3, KVOLUME = 4 };
// Binds a ps_2_0 sampling `slot` of `kind` (alpha routed when `alpha`); the shader lives as long as `p`.
static bool usePs(Com<IDirect3DPixelShader9> &p, DWORD kind, DWORD slot, bool alpha) {
    DWORD t[15];
    psTex(t, kind, slot, alpha);
    CK(CreatePixelShader, t, &p.p);
    CK(SetPixelShader, p.p);
    return true;
}
// Fills level `level` of `t` by LockRect with texel(x, y) as `bpp`-byte values.
template <class F> static bool fill(IDirect3DTexture9 *t, UINT level, int bpp, F texel) {
    D3DSURFACE_DESC d = {};
    HRESULT hr = t->GetLevelDesc(level, &d);
    if (FAILED(hr)) return fail("GetLevelDesc", hr);
    D3DLOCKED_RECT lr = {};
    if (FAILED(hr = t->LockRect(level, &lr, nullptr, 0))) return fail("LockRect", hr);
    for (UINT y = 0; y < d.Height; y++)
        for (UINT x = 0; x < d.Width; x++) {
            const DWORD v = texel(x, y);
            memcpy((BYTE *)lr.pBits + y * lr.Pitch + x * bpp, &v, bpp);
        }
    t->UnlockRect(level);
    return true;
}
template <class F> static bool make2d(Com<IDirect3DTexture9> &t, UINT w, UINT h, UINT levels, D3DFORMAT f, int bpp,
                                      F texel, D3DPOOL pool = D3DPOOL_MANAGED, DWORD usage = 0) {
    CK(CreateTexture, w, h, levels, usage, f, pool, &t.p, nullptr);
    for (UINT l = 0; l < t.p->GetLevelCount(); l++)
        if (!fill(t.p, l, bpp, texel)) return false;
    return true;
}
static const DWORD TRED = D3DCOLOR_XRGB(255, 0, 0), TGRN = D3DCOLOR_XRGB(0, 255, 0), TBLU = D3DCOLOR_XRGB(0, 0, 255),
                   TWHT = D3DCOLOR_XRGB(255, 255, 255);
// Point sampling, clamped, on `stage`.
static bool pointClamp(DWORD stage) {
    CK(SetSamplerState, stage, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    CK(SetSamplerState, stage, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    CK(SetSamplerState, stage, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    CK(SetSamplerState, stage, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    return true;
}
// One row of one texture `t` on stage 0 under a ps_2_0 2D sample, quads at the given texel centres (u, v) of
// a w x h texture, each expected the given colour.
struct Probe { float u, v; DWORD want; };
static bool probe2d(IDirect3DBaseTexture9 *t, const Probe *pr, int n, bool alpha = false) {
    Com<IDirect3DPixelShader9> ps;
    if (!usePs(ps, K2D, 0, alpha) || !pointClamp(0)) return false;
    CK(SetTexture, 0, t);
    TV3 q[8][4];
    Px px[8];
    for (int i = 0; i < n; i++) {
        const float x = 16.0f + 56.0f * i;
        tquad(q[i], x, 96, x + 40, 136, pr[i].u, pr[i].v);
        px[i] = {(int)x + 20, 116, pr[i].want};
    }
    return frames(BLUE, px, n, [&] {
        for (int i = 0; i < n; i++)
            if (!tdraw(q[i])) return false;
        return true;
    });
}
static bool rowTexPs2d() {
    Com<IDirect3DTexture9> t;
    if (!make2d(t, 2, 2, 1, D3DFMT_X8R8G8B8, 4, [](int x, int y) { return x ^ y ? TRED : TGRN; })) return false;
    const Probe pr[] = {{0.25f, 0.25f, TGRN}, {0.75f, 0.25f, TRED}, {0.25f, 0.75f, TRED}, {0.75f, 0.75f, TGRN}};
    return probe2d(t.p, pr, 4);
}
// The cascade: stage 0 MODULATE TEXTURE DIFFUSE (white diffuse: the texel).
static bool rowTexFf() {
    Com<IDirect3DTexture9> t;
    if (!make2d(t, 2, 1, 1, D3DFMT_X8R8G8B8, 4, [](int x, int) { return x ? TRED : TGRN; }) || !pointClamp(0))
        return false;
    CK(SetTexture, 0, t.p);
    TV3 a[4], b[4];
    tquad(a, 16, 96, 56, 136, 0.25f, 0.5f), tquad(b, 72, 96, 112, 136, 0.75f, 0.5f);
    const Px px[] = {{36, 116, TGRN}, {92, 116, TRED}, {200, 200, BLUE}};
    return frames(BLUE, px, 3, [&] { return tdraw(a) && tdraw(b); });
}
// Stages 0-2: TEXTURE (0x404040), ADD TFACTOR (0x202020), MODULATE stage 2's CONSTANT (half): 0x303030.
static bool rowTexCascade() {
    Com<IDirect3DTexture9> t;
    if (!make2d(t, 1, 1, 1, D3DFMT_X8R8G8B8, 4, [](int, int) { return D3DCOLOR_XRGB(64, 64, 64); }) ||
        !pointClamp(0))
        return false;
    CK(SetTexture, 0, t.p);
    CK(SetTextureStageState, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    CK(SetTextureStageState, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    CK(SetRenderState, D3DRS_TEXTUREFACTOR, D3DCOLOR_XRGB(32, 32, 32));
    CK(SetTextureStageState, 1, D3DTSS_COLOROP, D3DTOP_ADD);
    CK(SetTextureStageState, 1, D3DTSS_COLORARG1, D3DTA_CURRENT);
    CK(SetTextureStageState, 1, D3DTSS_COLORARG2, D3DTA_TFACTOR);
    CK(SetTextureStageState, 2, D3DTSS_COLOROP, D3DTOP_MODULATE);
    CK(SetTextureStageState, 2, D3DTSS_COLORARG1, D3DTA_CURRENT);
    CK(SetTextureStageState, 2, D3DTSS_COLORARG2, D3DTA_CONSTANT);
    CK(SetTextureStageState, 2, D3DTSS_CONSTANT, D3DCOLOR_XRGB(128, 128, 128));
    TV3 a[4];
    tquad(a, 96, 96, 160, 160, 0.5f, 0.5f);
    const Px px[] = {{128, 128, D3DCOLOR_XRGB(48, 48, 48)}};
    return frames(BLUE, px, 1, [&] { return tdraw(a); });
}
// A cube of one colour per face, three faces sampled along +x, -y, +z.
static bool rowTexCube() {
    Com<IDirect3DCubeTexture9> c;
    CK(CreateCubeTexture, 2, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &c.p, nullptr);
    const DWORD faces[6] = {TRED, TGRN, TBLU, D3DCOLOR_XRGB(255, 255, 0), TWHT, D3DCOLOR_XRGB(0, 255, 255)};
    for (int f = 0; f < 6; f++) {
        D3DLOCKED_RECT lr = {};
        HRESULT hr = c.p->LockRect((D3DCUBEMAP_FACES)f, 0, &lr, nullptr, 0);
        if (FAILED(hr)) return fail("cube LockRect", hr);
        for (int y = 0; y < 2; y++)
            for (int x = 0; x < 2; x++) ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] = faces[f];
        c.p->UnlockRect((D3DCUBEMAP_FACES)f, 0);
    }
    Com<IDirect3DPixelShader9> ps;
    if (!usePs(ps, KCUBE, 0, false) || !pointClamp(0)) return false;
    CK(SetTexture, 0, c.p);
    TV3 a[4], b[4], d[4];
    tquad(a, 16, 96, 56, 136, 1, 0, 0), tquad(b, 72, 96, 112, 136, 0, -1, 0), tquad(d, 128, 96, 168, 136, 0, 0, 1);
    const Px px[] = {{36, 116, TRED}, {92, 116, D3DCOLOR_XRGB(255, 255, 0)}, {148, 116, TWHT}};
    return frames(BLUE, px, 3, [&] { return tdraw(a) && tdraw(b) && tdraw(d); });
}
// A 2x2x2 volume filled by LockBox, slice 0 red and slice 1 green, sampled at each slice's centre.
static bool rowTexVolume() {
    Com<IDirect3DVolumeTexture9> v;
    CK(CreateVolumeTexture, 2, 2, 2, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &v.p, nullptr);
    D3DLOCKED_BOX lb = {};
    HRESULT hr = v.p->LockBox(0, &lb, nullptr, 0);
    if (FAILED(hr)) return fail("LockBox", hr);
    for (int z = 0; z < 2; z++)
        for (int y = 0; y < 2; y++)
            for (int x = 0; x < 2; x++)
                ((DWORD *)((BYTE *)lb.pBits + z * lb.SlicePitch + y * lb.RowPitch))[x] = z ? TGRN : TRED;
    v.p->UnlockBox(0);
    Com<IDirect3DPixelShader9> ps;
    if (!usePs(ps, KVOLUME, 0, false) || !pointClamp(0)) return false;
    CK(SetSamplerState, 0, D3DSAMP_ADDRESSW, D3DTADDRESS_CLAMP);
    CK(SetTexture, 0, v.p);
    TV3 a[4], b[4];
    tquad(a, 16, 96, 56, 136, 0.5f, 0.5f, 0.25f), tquad(b, 72, 96, 112, 136, 0.5f, 0.5f, 0.75f);
    const Px px[] = {{36, 116, TRED}, {92, 116, TGRN}};
    return frames(BLUE, px, 2, [&] { return tdraw(a) && tdraw(b); });
}
// Three levels of red, green, blue (level 2 is 1x1, rows of 4 bytes); a magnified quad samples level 0 unless SetLOD
// 1, MAXMIPLEVEL 2 or a LOD bias of +1 (from level 0's footprint, minified: a 4x4 texture over 2x2 pixels) moves it.
static bool rowTexMips() {
    Com<IDirect3DTexture9> t;
    CK(CreateTexture, 4, 4, 3, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &t.p, nullptr);
    const DWORD levels[3] = {TRED, TGRN, TBLU};
    for (UINT l = 0; l < 3; l++)
        if (!fill(t.p, l, 4, [&](int, int) { return levels[l]; })) return false;
    if (!pointClamp(0)) return false;
    CK(SetSamplerState, 0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
    CK(SetTexture, 0, t.p);
    TV3 a[4];
    tquad(a, 96, 96, 160, 160, 0.5f, 0.5f);
    const Px r[] = {{128, 128, TRED}}, g[] = {{128, 128, TGRN}}, b[] = {{128, 128, TBLU}};
    if (!frames(BLUE, r, 1, [&] { return tdraw(a); })) return false;
    t.p->SetLOD(1);
    bool ok = frames(BLUE, g, 1, [&] { return tdraw(a); });
    t.p->SetLOD(0);
    if (!ok) return false;
    CK(SetSamplerState, 0, D3DSAMP_MAXMIPLEVEL, 2);
    ok = frames(BLUE, b, 1, [&] { return tdraw(a); });
    CK(SetSamplerState, 0, D3DSAMP_MAXMIPLEVEL, 0);
    if (!ok) return false;
    // Minified 2:1 (LOD 1, green); bias +1 gives LOD 2 (blue).
    TV3 m[4];
    m[0] = {126, 126, 0.5f, 1, TWHT, 0, 0, 0}, m[1] = {128, 126, 0.5f, 1, TWHT, 1, 0, 0},
    m[2] = {126, 128, 0.5f, 1, TWHT, 0, 1, 0}, m[3] = {128, 128, 0.5f, 1, TWHT, 1, 1, 0};
    const Px mg[] = {{126, 126, TGRN}}, mb[] = {{126, 126, TBLU}};
    if (!frames(D3DCOLOR_XRGB(9, 9, 9), mg, 1, [&] { return tdraw(m); })) return false;
    const float bias = 1.0f;
    CK(SetSamplerState, 0, D3DSAMP_MIPMAPLODBIAS, *(const DWORD *)&bias);
    ok = frames(D3DCOLOR_XRGB(9, 9, 9), mb, 1, [&] { return tdraw(m); });
    CK(SetSamplerState, 0, D3DSAMP_MIPMAPLODBIAS, 0);
    return ok;
}
// u = 1.25 on a 2x1 texture (red, green): CLAMP reads green, WRAP red (u 0.25), MIRROR green (u 0.75).
static bool rowTexAddress() {
    Com<IDirect3DTexture9> t;
    if (!make2d(t, 2, 1, 1, D3DFMT_X8R8G8B8, 4, [](int x, int) { return x ? TGRN : TRED; }) || !pointClamp(0))
        return false;
    CK(SetTexture, 0, t.p);
    const DWORD modes[3] = {D3DTADDRESS_CLAMP, D3DTADDRESS_WRAP, D3DTADDRESS_MIRROR}, want[3] = {TGRN, TRED, TGRN};
    TV3 a[4];
    tquad(a, 96, 96, 160, 160, 1.25f, 0.5f);
    for (int i = 0; i < 3; i++) {
        CK(SetSamplerState, 0, D3DSAMP_ADDRESSU, modes[i]);
        const Px px[] = {{128, 128, want[i]}};
        if (!frames(BLUE, px, 1, [&] { return tdraw(a); }))
            return snprintf(why + strlen(why), sizeof why - strlen(why), " mode %lu", modes[i]), false;
    }
    return true;
}
// LINEAR at u 0.5 of a 2x1 texture (black, white): their mean.
static bool rowTexFilter() {
    Com<IDirect3DTexture9> t;
    if (!make2d(t, 2, 1, 1, D3DFMT_X8R8G8B8, 4, [](int x, int) { return x ? TWHT : 0u; }) || !pointClamp(0))
        return false;
    CK(SetSamplerState, 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    CK(SetSamplerState, 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    CK(SetTexture, 0, t.p);
    TV3 a[4];
    tquad(a, 96, 96, 160, 160, 0.5f, 0.5f);
    const Px px[] = {{128, 128, D3DCOLOR_XRGB(128, 128, 128)}};
    return frames(BLUE, px, 1, [&] { return tdraw(a); });
}
// One sRGB-encoded texel (128) read raw (128) and with SRGBTEXTURE (its linear value, 55).
static bool rowTexSrgb() {
    Com<IDirect3DTexture9> t;
    if (!make2d(t, 1, 1, 1, D3DFMT_X8R8G8B8, 4, [](int, int) { return D3DCOLOR_XRGB(128, 128, 128); }) ||
        !pointClamp(0))
        return false;
    CK(SetTexture, 0, t.p);
    TV3 a[4];
    tquad(a, 96, 96, 160, 160, 0.5f, 0.5f);
    const Px raw[] = {{128, 128, D3DCOLOR_XRGB(128, 128, 128)}}, lin[] = {{128, 128, D3DCOLOR_XRGB(55, 55, 55)}};
    if (!frames(BLUE, raw, 1, [&] { return tdraw(a); })) return false;
    CK(SetSamplerState, 0, D3DSAMP_SRGBTEXTURE, TRUE);
    const bool ok = frames(BLUE, lin, 1, [&] { return tdraw(a); });
    CK(SetSamplerState, 0, D3DSAMP_SRGBTEXTURE, FALSE);
    return ok;
}
// DXT1: an 8x8 texture of four levels, each one block of colour 0 (indices 0); levels 2 (2x2) and 3 (1x1) are below
// the block size. MAXMIPLEVEL selects each.
static bool rowTexDxt1() {
    Com<IDirect3DTexture9> t;
    CK(CreateTexture, 8, 8, 4, 0, D3DFMT_DXT1, D3DPOOL_MANAGED, &t.p, nullptr);
    const WORD c565[4] = {0xf800, 0x07e0, 0x001f, 0xffff};  // red, green, blue, white
    for (UINT l = 0; l < 4; l++) {
        D3DLOCKED_RECT lr = {};
        HRESULT hr = t.p->LockRect(l, &lr, nullptr, 0);
        if (FAILED(hr)) return fail("DXT1 LockRect", hr);
        const UINT blocks = l == 0 ? 2 : 1;
        for (UINT by = 0; by < blocks; by++)
            for (UINT bx = 0; bx < blocks; bx++) {
                WORD *b = (WORD *)((BYTE *)lr.pBits + by * lr.Pitch + bx * 8);
                b[0] = c565[l], b[1] = 0, b[2] = 0, b[3] = 0;  // color0 > color1: four-colour block, index 0
            }
        t.p->UnlockRect(l);
    }
    if (!pointClamp(0)) return false;
    CK(SetSamplerState, 0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
    CK(SetTexture, 0, t.p);
    TV3 a[4];
    tquad(a, 96, 96, 160, 160, 0.5f, 0.5f);
    const DWORD want[4] = {TRED, TGRN, TBLU, TWHT};
    bool ok = true;
    for (DWORD l = 0; l < 4 && ok; l++) {
        CK(SetSamplerState, 0, D3DSAMP_MAXMIPLEVEL, l);
        const Px px[] = {{128, 128, want[l]}};
        ok = frames(D3DCOLOR_XRGB(9, 9, 9), px, 1, [&] { return tdraw(a); });
    }
    CK(SetSamplerState, 0, D3DSAMP_MAXMIPLEVEL, 0);
    return ok;
}
// Narrow formats: L8 (gray, alpha 1), A8 (alpha, black colour), A8L8, R5G6B5, A4R4G4B4 and the CPU-widened R8G8B8.
static bool fmtRow(D3DFORMAT f, int bpp, DWORD texel, DWORD rgb, DWORD alpha) {
    Com<IDirect3DTexture9> t;
    if (!make2d(t, 1, 1, 1, f, bpp, [&](int, int) { return texel; })) return false;
    const Probe c[] = {{0.5f, 0.5f, rgb}}, a[] = {{0.5f, 0.5f, alpha}};
    return probe2d(t.p, c, 1, false) && probe2d(t.p, a, 1, true);
}
static DWORD gray(int v) { return D3DCOLOR_XRGB(v, v, v); }
static bool rowTexL8() { return fmtRow(D3DFMT_L8, 1, 0x80, gray(128), gray(255)); }
static bool rowTexA8() { return fmtRow(D3DFMT_A8, 1, 0x80, gray(0), gray(128)); }
static bool rowTexA8l8() { return fmtRow(D3DFMT_A8L8, 2, 0x4080, gray(128), gray(64)); }
static bool rowTexR5g6b5() { return fmtRow(D3DFMT_R5G6B5, 2, 0xf800, TRED, gray(255)); }
static bool rowTexA4r4g4b4() { return fmtRow(D3DFMT_A4R4G4B4, 2, 0x8f00, TRED, gray(136)); }
static bool rowTexR8g8b8() { return fmtRow(D3DFMT_R8G8B8, 3, 0xff8000, D3DCOLOR_XRGB(255, 128, 0), gray(255)); }
// Two frames: a 2x1 texture (green, green), then a dirty-rect write of red into texel 1.
static bool rowTexLock() {
    Com<IDirect3DTexture9> t;
    if (!make2d(t, 2, 1, 1, D3DFMT_X8R8G8B8, 4, [](int, int) { return TGRN; })) return false;
    const Probe before[] = {{0.25f, 0.5f, TGRN}, {0.75f, 0.5f, TGRN}}, after[] = {{0.25f, 0.5f, TGRN}, {0.75f, 0.5f, TRED}};
    if (!probe2d(t.p, before, 2)) return false;
    D3DLOCKED_RECT lr = {};
    const RECT r = {1, 0, 2, 1};
    HRESULT hr = t.p->LockRect(0, &lr, &r, 0);
    if (FAILED(hr)) return fail("LockRect (rect)", hr);
    *(DWORD *)lr.pBits = TRED;
    t.p->UnlockRect(0);
    return probe2d(t.p, after, 2);
}
// One frame: a draw, a relock of the texture, a draw: the first draw keeps the old texel.
static bool rowTexTwoDraws() {
    Com<IDirect3DTexture9> t;
    Com<IDirect3DPixelShader9> ps;
    if (!make2d(t, 1, 1, 1, D3DFMT_X8R8G8B8, 4, [](int, int) { return TGRN; }) || !usePs(ps, K2D, 0, false) ||
        !pointClamp(0))
        return false;
    CK(SetTexture, 0, t.p);
    TV3 a[4], b[4];
    tquad(a, 16, 96, 56, 136, 0.5f, 0.5f), tquad(b, 72, 96, 112, 136, 0.5f, 0.5f);
    const Px px[] = {{36, 116, TGRN}, {92, 116, TRED}};
    return frames(BLUE, px, 2, [&] {
        if (!fill(t.p, 0, 4, [](int, int) { return TGRN; }) || !tdraw(a)) return false;
        return fill(t.p, 0, 4, [](int, int) { return TRED; }) && tdraw(b);
    });
}
// DEFAULT + DYNAMIC, refilled with DISCARD: blue, then yellow.
static bool rowTexDynamic() {
    Com<IDirect3DTexture9> t;
    CK(CreateTexture, 1, 1, 1, D3DUSAGE_DYNAMIC, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &t.p, nullptr);
    const DWORD colours[2] = {TBLU, D3DCOLOR_XRGB(255, 255, 0)};
    for (DWORD c : colours) {
        D3DLOCKED_RECT lr = {};
        HRESULT hr = t.p->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD);
        if (FAILED(hr)) return fail("LockRect (DISCARD)", hr);
        *(DWORD *)lr.pBits = c;
        t.p->UnlockRect(0);
        const Probe pr[] = {{0.5f, 0.5f, c}};
        if (!probe2d(t.p, pr, 1)) return false;
    }
    return true;
}
// A level written through its surface: GetSurfaceLevel(1)->LockRect.
static bool rowTexLevelSurf() {
    Com<IDirect3DTexture9> t;
    if (!make2d(t, 2, 2, 2, D3DFMT_X8R8G8B8, 4, [](int, int) { return TGRN; })) return false;
    Com<IDirect3DSurface9> s;
    HRESULT hr = t.p->GetSurfaceLevel(1, &s.p);
    if (FAILED(hr)) return fail("GetSurfaceLevel", hr);
    D3DLOCKED_RECT lr = {};
    if (FAILED(hr = s.p->LockRect(&lr, nullptr, 0))) return fail("surface LockRect", hr);
    *(DWORD *)lr.pBits = TRED;
    s.p->UnlockRect();
    CK(SetSamplerState, 0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
    CK(SetSamplerState, 0, D3DSAMP_MAXMIPLEVEL, 1);
    const Probe pr[] = {{0.5f, 0.5f, TRED}};
    const bool ok = probe2d(t.p, pr, 1);
    dev->SetSamplerState(0, D3DSAMP_MAXMIPLEVEL, 0);
    return ok;
}
// Level 1 of an AUTOGENMIPMAP texture (2x2 level 0 of 0, 0, 255, 255 red) after an explicit or automatic generate:
// the box mean, 128 (within 1). `explicitGen` calls GenerateMipSubLevels.
static bool autogenRow(bool explicitGen) {
    Com<IDirect3DTexture9> t;
    HRESULT hr = dev->CreateTexture(2, 2, 0, D3DUSAGE_AUTOGENMIPMAP, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &t.p, nullptr);
    if (FAILED(hr)) return fail("CreateTexture (AUTOGENMIPMAP)", hr);
    if (!fill(t.p, 0, 4, [](int x, int) { return x ? D3DCOLOR_XRGB(255, 0, 0) : 0u; })) return false;
    if (explicitGen) t.p->GenerateMipSubLevels();
    CK(SetSamplerState, 0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
    CK(SetSamplerState, 0, D3DSAMP_MAXMIPLEVEL, 1);
    const Probe pr[] = {{0.5f, 0.5f, D3DCOLOR_XRGB(128, 0, 0)}};
    const bool ok = probe2d(t.p, pr, 1);
    dev->SetSamplerState(0, D3DSAMP_MAXMIPLEVEL, 0);
    return ok;
}
static bool rowTexAutogen() { return autogenRow(false); }
static bool rowTexGenMips() { return autogenRow(true); }
// UpdateTexture from a SYSTEMMEM 4x4 of two levels (red, blue) into a DEFAULT 2x2 of one: the source's level 1.
static bool rowTexUpdate() {
    Com<IDirect3DTexture9> src, dst;
    CK(CreateTexture, 4, 4, 2, 0, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &src.p, nullptr);
    if (!fill(src.p, 0, 4, [](int, int) { return TRED; }) || !fill(src.p, 1, 4, [](int, int) { return TBLU; }))
        return false;
    CK(CreateTexture, 2, 2, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &dst.p, nullptr);
    CK(UpdateTexture, src.p, dst.p);
    const Probe pr[] = {{0.5f, 0.5f, TBLU}};
    return probe2d(dst.p, pr, 1);
}
// A ps_2_0 sampling s1: a red texture bound, then none, in one frame: red, then opaque black (alpha 1 routed: white).
static bool rowTexBlack() {
    Com<IDirect3DTexture9> t;
    Com<IDirect3DPixelShader9> rgb, alpha;
    if (!make2d(t, 1, 1, 1, D3DFMT_A8R8G8B8, 4, [](int, int) { return TRED | 0xff000000u; }) || !pointClamp(1))
        return false;
    DWORD tr[15], ta[15];
    psTex(tr, K2D, 1, false), psTex(ta, K2D, 1, true);
    CK(CreatePixelShader, tr, &rgb.p);
    CK(CreatePixelShader, ta, &alpha.p);
    TV3 a[4], b[4], c[4], d[4];
    tquad(a, 16, 96, 56, 136, 0.5f, 0.5f), tquad(b, 72, 96, 112, 136, 0.5f, 0.5f);
    tquad(c, 128, 96, 168, 136, 0.5f, 0.5f), tquad(d, 184, 96, 224, 136, 0.5f, 0.5f);
    const Px px[] = {{36, 116, TRED}, {92, 116, 0}, {148, 116, TWHT}, {204, 116, TWHT}};
    return frames(BLUE, px, 4, [&] {
        CK(SetTexture, 1, t.p);
        CK(SetPixelShader, rgb.p);
        if (!tdraw(a)) return false;
        CK(SetPixelShader, alpha.p);
        if (!tdraw(c)) return false;
        CK(SetTexture, 1, nullptr);
        CK(SetPixelShader, rgb.p);
        if (!tdraw(b)) return false;
        CK(SetPixelShader, alpha.p);
        return tdraw(d);
    });
}
// A V8U8 bump map of (du 0.5, dv 0) under BUMPENVMAP with an identity matrix moves stage 1's lookup from texel 0
// (red) to texel 1 (blue) of a 2x1 texture: the cascade (`ff`) or ps_1_1 texbem.
static bool bumpRow(bool ff) {
    Com<IDirect3DTexture9> bump, t;
    HRESULT hr = dev->CreateTexture(1, 1, 1, 0, D3DFMT_V8U8, D3DPOOL_MANAGED, &bump.p, nullptr);
    if (FAILED(hr)) return fail("CreateTexture (V8U8)", hr);
    if (!fill(bump.p, 0, 2, [](int, int) { return 0x0040u; }) ||
        !make2d(t, 2, 1, 1, D3DFMT_X8R8G8B8, 4, [](int x, int) { return x ? TBLU : TRED; }) || !pointClamp(0) ||
        !pointClamp(1))
        return false;
    CK(SetTexture, 0, bump.p);
    CK(SetTexture, 1, t.p);
    const float one = 1.0f;
    // The cascade's BUMPENVMAP reads the bump stage's matrix (0), texbem the destination stage's (1).
    for (DWORD stage = 0; stage < 2; stage++) {
        CK(SetTextureStageState, stage, D3DTSS_BUMPENVMAT00, *(const DWORD *)&one);
        CK(SetTextureStageState, stage, D3DTSS_BUMPENVMAT11, *(const DWORD *)&one);
    }
    Com<IDirect3DPixelShader9> ps;
    if (ff) {
        CK(SetTextureStageState, 0, D3DTSS_COLOROP, D3DTOP_BUMPENVMAP);
        CK(SetTextureStageState, 1, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        CK(SetTextureStageState, 1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    } else {
        static const DWORD texbem[] = {0xffff0101, 0x01000042, 0xb00f0000,  // ps_1_1; tex t0
                                       0x02000043, 0xb00f0001, 0xb0e40000,  // texbem t1, t0
                                       0x02000001, 0x800f0000, 0xb0e40001,  // mov r0, t1
                                       0x0000ffff};
        CK(CreatePixelShader, texbem, &ps.p);
        CK(SetPixelShader, ps.p);
    }
    struct TV2 { float x, y, z, rhw; DWORD c; float u0, v0, u1, v1; } q[4];
    const DWORD c = TWHT;
    q[0] = {96, 96, 0.5f, 1, c, 0.5f, 0.5f, 0.25f, 0.5f}, q[1] = {160, 96, 0.5f, 1, c, 0.5f, 0.5f, 0.25f, 0.5f};
    q[2] = {96, 160, 0.5f, 1, c, 0.5f, 0.5f, 0.25f, 0.5f}, q[3] = {160, 160, 0.5f, 1, c, 0.5f, 0.5f, 0.25f, 0.5f};
    const Px px[] = {{128, 128, TBLU}};
    return frames(D3DCOLOR_XRGB(9, 9, 9), px, 1, [&] {
        CK(SetFVF, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX2);
        CK(DrawPrimitiveUP, D3DPT_TRIANGLESTRIP, 2, q, sizeof *q);
        return true;
    });
}
static bool rowTexBumpEnv() { return bumpRow(true); }
static bool rowTexTexbem() { return bumpRow(false); }
// A 1024x1024 texture (4 MiB, past a 32-bit ring block) sampled.
static bool rowTexBig() {
    Com<IDirect3DTexture9> t;
    if (!make2d(t, 1024, 1024, 1, D3DFMT_X8R8G8B8, 4, [](int, int) { return D3DCOLOR_XRGB(255, 0, 255); }))
        return false;
    const Probe pr[] = {{0.5f, 0.5f, D3DCOLOR_XRGB(255, 0, 255)}};
    return probe2d(t.p, pr, 1);
}
// 32-bit lanes: 256 MiB of textures sampled in one frame (64 MANAGED 1024x1024 X8R8G8B8, 4 MiB each, each uploaded at
// its first bind): a 32-bit core process's staged-bytes budget commits the chunk early (C4b3b-3); every cell shows its
// own texture.
static bool rowTexBig256() {
    enum { N = 64 };
    Com<IDirect3DTexture9> t[N];
    for (int i = 0; i < N; i++) {
        const DWORD c = D3DCOLOR_XRGB(i * 4, 255 - i * 4, 128);
        if (!make2d(t[i], 1024, 1024, 1, D3DFMT_X8R8G8B8, 4, [c](int, int) { return c; })) return false;
    }
    Com<IDirect3DPixelShader9> ps;
    if (!usePs(ps, K2D, 0, false) || !pointClamp(0)) return false;
    static const int cells[8] = {0, 9, 18, 27, 36, 45, 54, 63};
    Px px[8];
    for (int k = 0; k < 8; k++)
        px[k] = {cells[k] % 8 * 32 + 16, cells[k] / 8 * 32 + 16, D3DCOLOR_XRGB(cells[k] * 4, 255 - cells[k] * 4, 128)};
    const bool ok = frames(BLUE, px, 8, [&] {
        for (int i = 0; i < N; i++) {
            TV3 a[4];
            const float x = (float)(i % 8 * 32), y = (float)(i / 8 * 32);
            tquad(a, x, y, x + 32, y + 32, 0.5f, 0.5f);
            CK(SetTexture, 0, t[i].p);
            if (!tdraw(a)) return false;
        }
        return true;
    });
    CK(SetTexture, 0, nullptr);
    return ok;
}
// Render targets (DXMTRS C4b-3d1). A size x size render-target texture in the default pool.
static bool makeRt(Com<IDirect3DTexture9> &t, UINT size = 64, UINT levels = 1, D3DFORMAT f = D3DFMT_A8R8G8B8,
                   DWORD usage = D3DUSAGE_RENDERTARGET) {
    CK(CreateTexture, size, size, levels, usage, f, D3DPOOL_DEFAULT, &t.p, nullptr);
    return true;
}
// Level `level` of `t` at render target 0 (which resets the viewport to the level).
static bool target(IDirect3DTexture9 *t, UINT level = 0) {
    Com<IDirect3DSurface9> s;
    const HRESULT hr = t->GetSurfaceLevel(level, &s.p);
    if (FAILED(hr)) return fail("GetSurfaceLevel", hr);
    CK(SetRenderTarget, 0, s.p);
    return true;
}
// The back buffer at render target 0 again (and the viewport the whole of it).
static bool backToScreen() {
    CK(SetRenderTarget, 0, rtSurf);
    return true;
}
static bool clearTo(DWORD color) {
    CK(Clear, 0, nullptr, D3DCLEAR_TARGET, color, 1.0f, 0);
    return true;
}
static bool rdraw(const RV *q) {
    CK(SetFVF, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    CK(DrawPrimitiveUP, D3DPT_TRIANGLESTRIP, 2, q, sizeof *q);
    return true;
}
// `t` drawn by the fixed-function pixel stage over the pixel-space quad (x0, y0)-(x1, y1), u and v from 0 to 1 across
// it: with the default 64..192 quad, texel (x, y) of a 64x64 texture covers pixels 64+2x and 65+2x (rows assert at
// 65+2x, the texel's centre: a pixel centre is at its integer coordinates, so 64+2x samples the texel's edge).
static bool show(IDirect3DBaseTexture9 *t, float x0 = 64, float y0 = 64, float x1 = 192, float y1 = 192) {
    const DWORD c = D3DCOLOR_XRGB(255, 255, 255);
    const TV3 q[4] = {{x0, y0, 0.5f, 1, c, 0, 0, 0}, {x1, y0, 0.5f, 1, c, 1, 0, 0}, {x0, y1, 0.5f, 1, c, 0, 1, 0},
                      {x1, y1, 0.5f, 1, c, 1, 1, 0}};
    CK(SetTexture, 0, t);
    const bool ok = tdraw(q);
    CK(SetTexture, 0, nullptr);
    return ok;
}
// LockRect of a render-target texture's levels (DXMTRS C4c-3a): with the targets mode's helpers, below.
static bool rowTexRtLock();
// Released by the application while bound: the stage keeps it, the draw samples it.
static bool rowTexRelease() {
    IDirect3DTexture9 *t = nullptr;
    CK(CreateTexture, 1, 1, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &t, nullptr);
    if (!fill(t, 0, 4, [](int, int) { return TGRN; })) return t->Release(), false;
    CK(SetTexture, 0, t);
    t->Release();
    Com<IDirect3DPixelShader9> ps;
    if (!usePs(ps, K2D, 0, false) || !pointClamp(0)) return false;
    TV3 a[4];
    tquad(a, 96, 96, 160, 160, 0.5f, 0.5f);
    const Px px[] = {{128, 128, TGRN}};
    const bool ok = frames(BLUE, px, 1, [&] { return tdraw(a); });
    CK(SetTexture, 0, nullptr);
    return ok;
}
// A draw past the core's slice: D3D_OK under mtld3d, D3DERR_NOTAVAILABLE and undrawn under core (strict).
template <class F> static bool refusedRow(F draw) {
    Px px[] = {{128, 128, BLUE}};
    return frames(BLUE, px, 1, [&] {
        HRESULT hr = draw();
        if (strictMode ? hr != D3DERR_NOTAVAILABLE : hr != D3D_OK) return fail("DrawPrimitiveUP", hr);
        px[0].mask = strictMode ? 0xffffff : 0;  // mtld3d draws undefined contents: not asserted
        return true;
    });
}
static HRESULT ffTexDraw(IDirect3DBaseTexture9 *t) {
    TV3 a[4];
    tquad(a, 96, 96, 160, 160, 0.5f, 0.5f);
    dev->SetTexture(0, t);
    dev->SetFVF(FVF_T2);
    const HRESULT hr = dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, a, sizeof(TV3));
    dev->SetTexture(0, nullptr);
    return hr;
}
// A depth texture never rendered to, sampled by the fixed-function stage: the draw answers D3D_OK; what it samples
// is undefined, so no pixel is asserted (the "depth" mode's rows sample defined depths).
static bool rowTexDepthTex() {
    Com<IDirect3DTexture9> t;
    CK(CreateTexture, 4, 4, 1, D3DUSAGE_DEPTHSTENCIL, D3DFMT_D24S8, D3DPOOL_DEFAULT, &t.p, nullptr);
    const Px px[] = {{128, 128, BLUE, 0}};
    return frames(BLUE, px, 1, [&] {
        const HRESULT hr = ffTexDraw(t.p);
        return hr == D3D_OK || fail("DrawPrimitiveUP", hr);
    });
}
// fetch4 (GET4 with a point mag filter) over a 2x2 texture of four values, sampled at texel (0,0)'s centre: the
// sample is the four texels, one a channel, red (1,0), green (0,1) and blue (1,1). An A8 texture's alphas come the
// same way.
static bool fetch4Row(D3DFORMAT f) {
    Com<IDirect3DTexture9> t;
    if (!make2d(t, 2, 2, 1, f, 1, [](int x, int y) { return (DWORD)(0x20 + 0x40 * (x + 2 * y)); })) return false;
    CK(SetSamplerState, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    CK(SetSamplerState, 0, D3DSAMP_MIPMAPLODBIAS, MAKEFOURCC('G', 'E', 'T', '4'));
    TV3 a[4];
    tquad(a, 96, 96, 160, 160, 0.25f, 0.25f);
    const Px px[] = {{128, 128, D3DCOLOR_XRGB(0x60, 0xa0, 0xe0)}};
    CK(SetTexture, 0, t.p);
    const bool ok = frames(BLUE, px, 1, [&] { return tdraw(a); });
    CK(SetTexture, 0, nullptr);
    CK(SetSamplerState, 0, D3DSAMP_MIPMAPLODBIAS, MAKEFOURCC('G', 'E', 'T', '1'));
    CK(SetSamplerState, 0, D3DSAMP_MIPMAPLODBIAS, 0);
    return ok;
}
static bool rowTexFetch4() { return fetch4Row(D3DFMT_L8); }
static bool rowTexFetch4a() { return fetch4Row(D3DFMT_A8); }
// A render-target texture cleared green through SetRenderTarget, then sampled by the fixed-function stage.
static bool rowTexRtTex() {
    const Px px[] = {{128, 128, TGRN}};
    return frames(BLUE, px, 1, [&] {
        Com<IDirect3DTexture9> t;
        if (!makeRt(t, 4, 1, D3DFMT_X8R8G8B8) || !target(t.p) || !clearTo(TGRN) || !backToScreen()) return false;
        const HRESULT hr = ffTexDraw(t.p);
        return SUCCEEDED(hr) || fail("DrawPrimitiveUP", hr);
    });
}
// Vertex textures (DXMTRS C4b-3c). vsTex writes vs_3_0 { dcl_position v0; dcl_position o0; dcl_color o1; dcl_<kind>
// s<slot>; texldl r0, c0, s<slot>; mov o0, v0; mov o1, r0 (r0.wwww with alpha) }: the texel at c0 (its w the level) as
// the vertex colour, which PS_VCOL ps_3_0 { dcl_color v0; mov oC0, v0 } writes. kind: 2 2D, 3 cube, 4 volume.
static void vsTex(DWORD *t, DWORD kind, DWORD slot, bool alpha) {
    const DWORD v[24] = {0xfffe0300, 0x0200001f, 0x80000000, 0x900f0000, 0x0200001f, 0x80000000, 0xe00f0000, 0x0200001f,
                         0x8000000a, 0xe00f0001, 0x0200001f, 0x80000000 | kind << 27, 0xa00f0800 | slot, 0x0300005f,
                         0x800f0000, 0xa0e40000, 0xa0e40800 | slot, 0x02000001, 0xe00f0000, 0x90e40000, 0x02000001,
                         0xe00f0001, alpha ? 0x80ff0000 : 0x80e40000, 0x0000ffff};
    memcpy(t, v, sizeof v);
}
static const DWORD PS_VCOL[] = {0xffff0300, 0x0200001f, 0x8000000a, 0x900f0000,
                                0x02000001, 0x800f0800, 0x90e40000, 0x0000ffff};
static bool makeVtx(Prog &p, DWORD kind, DWORD slot, bool alpha = false) {
    DWORD t[24];
    vsTex(t, kind, slot, alpha);
    return make(p, DECL_XYZ, t, PS_VCOL);
}
// The quad with the coordinate (u, v, w) and the level lod in c0.
static bool vtxDraw(const V3 *q, float u, float v, float w = 0, float lod = 0) {
    const float c[4] = {u, v, w, lod};
    CK(SetVertexShaderConstantF, 0, c, 1);
    return strip(q);
}
static const DWORD VS0 = D3DVERTEXTEXTURESAMPLER0;
// A 2x1 texture, red then green.
static bool makeRG(Com<IDirect3DTexture9> &t) {
    return make2d(t, 2, 1, 1, D3DFMT_X8R8G8B8, 4, [](int x, int) { return x ? TGRN : TRED; });
}
// The texel at u 0.75 (green); and at u 1.75 under ADDRESSU MIRROR (red: WRAP and CLAMP both read green there), which
// shows the vertex sampler's own state row is applied.
static bool rowTexVtx() {
    Com<IDirect3DTexture9> t;
    Prog p;
    if (!makeRG(t) || !makeVtx(p, 2, 0) || !use(p)) return false;
    CK(SetTexture, VS0, t.p);
    CK(SetSamplerState, VS0, D3DSAMP_ADDRESSU, D3DTADDRESS_MIRROR);
    V3 l[4], r[4];
    quad(l, -0.875f, -0.25f, -0.125f, 0.25f), quad(r, 0.125f, -0.25f, 0.875f, 0.25f);
    const Px px[] = {{64, 128, TGRN}, {192, 128, TRED}, {128, 20, BLUE}};
    return frames(BLUE, px, 3, [&] { return vtxDraw(l, 0.75f, 0.5f) && vtxDraw(r, 1.75f, 0.5f); });
}
// Three levels of red, green, yellow: texldl at level 1 reads green; MAXMIPLEVEL 2 moves it to yellow; and a
// MIPMAPLODBIAS of 1 moves texldl's level 0 to green, which only the vertex LOD table carries (a Metal sampler has no
// bias).
static bool rowTexVtxLod() {
    Com<IDirect3DTexture9> t;
    CK(CreateTexture, 4, 4, 3, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &t.p, nullptr);
    const DWORD levels[3] = {TRED, TGRN, D3DCOLOR_XRGB(255, 255, 0)};
    for (UINT l = 0; l < 3; l++)
        if (!fill(t.p, l, 4, [&](int, int) { return levels[l]; })) return false;
    Prog p;
    if (!makeVtx(p, 2, 0) || !use(p)) return false;
    CK(SetTexture, VS0, t.p);
    CK(SetSamplerState, VS0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
    V3 q[4];
    quad(q, -0.25f, -0.25f, 0.25f, 0.25f);
    const Px one[] = {{128, 128, TGRN}}, two[] = {{128, 128, D3DCOLOR_XRGB(255, 255, 0)}};
    if (!frames(BLUE, one, 1, [&] { return vtxDraw(q, 0.5f, 0.5f, 0, 1); })) return false;
    CK(SetSamplerState, VS0, D3DSAMP_MAXMIPLEVEL, 2);
    if (!frames(BLUE, two, 1, [&] { return vtxDraw(q, 0.5f, 0.5f, 0, 1); })) return false;
    const float bias = 1.0f;
    DWORD biasBits;
    memcpy(&biasBits, &bias, 4);
    CK(SetSamplerState, VS0, D3DSAMP_MAXMIPLEVEL, 0);
    CK(SetSamplerState, VS0, D3DSAMP_MIPMAPLODBIAS, biasBits);
    return frames(BLUE, one, 1, [&] { return vtxDraw(q, 0.5f, 0.5f, 0, 0); });
}
// Unbound vertex slots read opaque black: a 2D s0 (its colour, left) and a cube s1 (its alpha as the colour, right).
static bool rowTexVtxBlack() {
    Prog a, b;
    if (!makeVtx(a, 2, 0) || !makeVtx(b, 3, 1, true)) return false;
    V3 l[4], r[4];
    quad(l, -0.875f, -0.25f, -0.125f, 0.25f), quad(r, 0.125f, -0.25f, 0.875f, 0.25f);
    const Px px[] = {{64, 128, 0x000000}, {192, 128, 0xffffff}, {128, 20, BLUE}};
    return frames(BLUE, px, 3, [&] { return use(a) && vtxDraw(l, 0.5f, 0.5f) && use(b) && vtxDraw(r, 0, 0, 1); });
}
// A cube at vertex slot 1, sampled along -y (yellow) and +z (white).
static bool rowTexVtxCube() {
    Com<IDirect3DCubeTexture9> c;
    CK(CreateCubeTexture, 2, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &c.p, nullptr);
    const DWORD faces[6] = {TRED, TGRN, D3DCOLOR_XRGB(0, 255, 255), D3DCOLOR_XRGB(255, 255, 0), TWHT,
                            D3DCOLOR_XRGB(255, 0, 255)};
    for (int f = 0; f < 6; f++) {
        D3DLOCKED_RECT lr = {};
        HRESULT hr = c.p->LockRect((D3DCUBEMAP_FACES)f, 0, &lr, nullptr, 0);
        if (FAILED(hr)) return fail("cube LockRect", hr);
        for (int y = 0; y < 2; y++)
            for (int x = 0; x < 2; x++) ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] = faces[f];
        c.p->UnlockRect((D3DCUBEMAP_FACES)f, 0);
    }
    Prog p;
    if (!makeVtx(p, 3, 1) || !use(p)) return false;
    CK(SetTexture, VS0 + 1, c.p);
    V3 l[4], r[4];
    quad(l, -0.875f, -0.25f, -0.125f, 0.25f), quad(r, 0.125f, -0.25f, 0.875f, 0.25f);
    const Px px[] = {{64, 128, D3DCOLOR_XRGB(255, 255, 0)}, {192, 128, TWHT}};
    return frames(BLUE, px, 2, [&] { return vtxDraw(l, 0, -1, 0) && vtxDraw(r, 0, 0, 1); });
}
// A 2x2x2 volume at vertex slot 2, slice 0 red and slice 1 green, sampled in slice 1.
static bool rowTexVtxVolume() {
    Com<IDirect3DVolumeTexture9> v;
    CK(CreateVolumeTexture, 2, 2, 2, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &v.p, nullptr);
    D3DLOCKED_BOX lb = {};
    HRESULT hr = v.p->LockBox(0, &lb, nullptr, 0);
    if (FAILED(hr)) return fail("LockBox", hr);
    for (int z = 0; z < 2; z++)
        for (int y = 0; y < 2; y++)
            for (int x = 0; x < 2; x++)
                ((DWORD *)((BYTE *)lb.pBits + z * lb.SlicePitch + y * lb.RowPitch))[x] = z ? TGRN : TRED;
    v.p->UnlockBox(0);
    Prog p;
    if (!makeVtx(p, 4, 2) || !use(p)) return false;
    CK(SetTexture, VS0 + 2, v.p);
    CK(SetSamplerState, VS0 + 2, D3DSAMP_ADDRESSW, D3DTADDRESS_CLAMP);
    V3 q[4];
    quad(q, -0.25f, -0.25f, 0.25f, 0.25f);
    const Px px[] = {{128, 128, TGRN}};
    return frames(BLUE, px, 1, [&] { return vtxDraw(q, 0.5f, 0.5f, 0.75f); });
}
// SRGBTEXTURE on a vertex sampler: the 0x80 grey read raw (left), then linearised (right).
static bool rowTexVtxSrgb() {
    Com<IDirect3DTexture9> t;
    Prog p;
    if (!make2d(t, 1, 1, 1, D3DFMT_X8R8G8B8, 4, [](int, int) { return D3DCOLOR_XRGB(128, 128, 128); }) ||
        !makeVtx(p, 2, 0) || !use(p))
        return false;
    CK(SetTexture, VS0, t.p);
    V3 l[4], r[4];
    quad(l, -0.875f, -0.25f, -0.125f, 0.25f), quad(r, 0.125f, -0.25f, 0.875f, 0.25f);
    const Px px[] = {{64, 128, D3DCOLOR_XRGB(128, 128, 128)}, {192, 128, D3DCOLOR_XRGB(55, 55, 55)}};
    return frames(BLUE, px, 2, [&] {
        CK(SetSamplerState, VS0, D3DSAMP_SRGBTEXTURE, FALSE);
        if (!vtxDraw(l, 0.5f, 0.5f)) return false;
        CK(SetSamplerState, VS0, D3DSAMP_SRGBTEXTURE, TRUE);
        return vtxDraw(r, 0.5f, 0.5f);
    });
}

static void textureRows(IDirect3DDevice9 *d, bool strict) {
    static const struct { const char *name; bool (*run)(); } rows[] = {
        {"tex-ps2d", rowTexPs2d},       {"tex-ff", rowTexFf},           {"tex-cascade", rowTexCascade},
        {"tex-cube", rowTexCube},       {"tex-volume", rowTexVolume},   {"tex-mips", rowTexMips},
        {"tex-address", rowTexAddress}, {"tex-filter", rowTexFilter},   {"tex-srgb", rowTexSrgb},
        {"tex-dxt1", rowTexDxt1},       {"tex-l8", rowTexL8},           {"tex-a8", rowTexA8},
        {"tex-a8l8", rowTexA8l8},       {"tex-r5g6b5", rowTexR5g6b5},   {"tex-a4r4g4b4", rowTexA4r4g4b4},
        {"tex-r8g8b8", rowTexR8g8b8},   {"tex-lock", rowTexLock},       {"tex-twodraws", rowTexTwoDraws},
        {"tex-dynamic", rowTexDynamic}, {"tex-levelsurf", rowTexLevelSurf}, {"tex-genmips", rowTexGenMips},
        {"tex-update", rowTexUpdate},   {"tex-autogen", rowTexAutogen}, {"tex-black", rowTexBlack},
        {"tex-bumpenv", rowTexBumpEnv}, {"tex-texbem", rowTexTexbem},   {"tex-big", rowTexBig},
        {"tex-release", rowTexRelease}, {"tex-depthtex", rowTexDepthTex}, {"tex-fetch4", rowTexFetch4},
        {"tex-fetch4a", rowTexFetch4a}, {"tex-rttex", rowTexRtTex},     {"tex-vtx", rowTexVtx},         {"tex-rtlock", rowTexRtLock},
        {"tex-vtxlod", rowTexVtxLod},   {"tex-vtxblack", rowTexVtxBlack}, {"tex-vtxcube", rowTexVtxCube},
        {"tex-vtxvolume", rowTexVtxVolume}, {"tex-vtxsrgb", rowTexVtxSrgb},
        {"tex-big256", sizeof(void *) == 4 ? rowTexBig256 : nullptr}};
    dev = d, strictMode = strict, budgetEnd = GetTickCount64() + 180000;
    HRESULT hr = dev->GetRenderTarget(0, &rtSurf);
    if (SUCCEEDED(hr))
        hr = dev->CreateOffscreenPlainSurface(256, 256, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sysSurf, nullptr);
    for (const auto &r : rows) {
        why[0] = 0;
        bool ok;
        if (FAILED(hr)) ok = fail("GetRenderTarget/CreateOffscreenPlainSurface", hr);
        else if (GetTickCount64() >= budgetEnd) ok = false, snprintf(why, sizeof why, "budget");
        else ok = reset() && r.run();
        row(r.name, ok, why);
    }
    if (sysSurf) sysSurf->Release();
    if (rtSurf) rtSurf->Release();
}

// The "fixed" mode (DXMTRS C4b-3c): the fixed-function vertex stage over untransformed layouts. Model-space quads,
// clockwise on screen; a clip-space x or y of c is pixel (c + 1) * 128 or (1 - c) * 128. Every probe sits at least 8
// pixels inside its quad, and no expected colour is the clear's blue.
struct FP { float x, y, z; };
struct FC { float x, y, z; DWORD c; };
struct FN { float x, y, z, nx, ny, nz; };
struct FNC { float x, y, z, nx, ny, nz; DWORD c; };
struct FT { float x, y, z, u, v; };
struct FT3 { float x, y, z, u, v, w; };
struct FT2 { float x, y, z, u0, v0, u1, v1; };
struct FNT { float x, y, z, nx, ny, nz, u, v; };
struct FB1 { float x, y, z, b; DWORD c; };
struct FB2 { float x, y, z, b; DWORD idx, c; };
static const DWORD FVF_C = D3DFVF_XYZ | D3DFVF_DIFFUSE, FVF_N = D3DFVF_XYZ | D3DFVF_NORMAL,
                   FVF_NC = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE;
// [x0, x1] x [y0, y1] at z; the other fields are the caller's.
template <class V> static void mquad(V *v, float x0, float y0, float x1, float y1, float z) {
    v[0].x = x0, v[0].y = y1, v[1].x = x1, v[1].y = y1, v[2].x = x0, v[2].y = y0, v[3].x = x1, v[3].y = y0;
    for (int i = 0; i < 4; i++) v[i].z = z;
}
// The row layouts: one centred quad (pixels 96-160), a left and a right one (16-112, 144-240), or three (16-80,
// 96-160, 176-240), each 64 pixels tall about y 128.
template <class V> static void qOne(V *v, float z) { mquad(v, -0.25f, -0.25f, 0.25f, 0.25f, z); }
template <class V> static void qLeft(V *v, float z) { mquad(v, -0.875f, -0.25f, -0.125f, 0.25f, z); }
template <class V> static void qRight(V *v, float z) { mquad(v, 0.125f, -0.25f, 0.875f, 0.25f, z); }
template <class V> static void q3(V *v, int i, float z) { mquad(v, -0.875f + 0.625f * i, -0.25f, -0.375f + 0.625f * i, 0.25f, z); }
template <class V> static void normals(V *v, float x, float y, float z) {
    for (int i = 0; i < 4; i++) v[i].nx = x, v[i].ny = y, v[i].nz = z;
}
template <class V> static bool fdraw(DWORD fvf, const V *v) {
    CK(SetFVF, fvf);
    CK(DrawPrimitiveUP, D3DPT_TRIANGLESTRIP, 2, v, sizeof *v);
    return true;
}
static D3DMATRIX mTrans(float x, float y, float z) {
    D3DMATRIX m = IDENTITY;
    m._41 = x, m._42 = y, m._43 = z;
    return m;
}
// An orthographic projection: eye z 0-5 is depth 0-1, x and y scaled by s.
static D3DMATRIX mOrtho(float s = 1) {
    D3DMATRIX m = IDENTITY;
    m._11 = m._22 = s, m._33 = 0.2f;
    return m;
}
// A perspective projection, near 1 and far 5: w is eye z.
static D3DMATRIX mPersp() {
    D3DMATRIX m = {};
    m._11 = m._22 = 1, m._33 = 1.25f, m._34 = 1, m._43 = -1.25f;
    return m;
}
static bool xform(D3DTRANSFORMSTATETYPE t, const D3DMATRIX &m) {
    CK(SetTransform, t, &m);
    return true;
}
static bool rstate(D3DRENDERSTATETYPE s, DWORD v) {
    CK(SetRenderState, s, v);
    return true;
}
static DWORD fbits(float f) {
    DWORD d;
    memcpy(&d, &f, 4);
    return d;
}
// A directional light along +z (into the screen: a normal of (0, 0, -1) faces it), its diffuse and ambient colours.
static D3DLIGHT9 ldir(float r, float g, float b, float ar = 0, float ag = 0, float ab = 0) {
    D3DLIGHT9 l = {};
    l.Type = D3DLIGHT_DIRECTIONAL, l.Direction = {0, 0, 1}, l.Diffuse = {r, g, b, 1}, l.Ambient = {ar, ag, ab, 1};
    return l;
}
static bool lightOn(DWORD i, const D3DLIGHT9 &l) {
    CK(SetLight, i, &l);
    CK(LightEnable, i, TRUE);
    return true;
}
static bool material(float dr, float dg, float db, float ar = 0, float ag = 0, float ab = 0) {
    D3DMATERIAL9 m = {};
    m.Diffuse = {dr, dg, db, 1}, m.Ambient = {ar, ag, ab, 1};
    CK(SetMaterial, &m);
    return true;
}
static const DWORD CC00 = 0x00cc00;

// WORLD, VIEW and an x/y-scaling PROJECTION each move the quad: it lands on pixels 128-192 x 64-128 and nowhere else.
static bool rowFfXform() {
    FC q[4];
    mquad(q, -0.5f, -0.5f, 0.5f, 0.5f, 2.5f);
    for (FC &v : q) v.c = D3DCOLOR_XRGB(0, 204, 0);
    if (!xform(D3DTS_WORLD, mTrans(0.5f, 0, 0)) || !xform(D3DTS_VIEW, mTrans(0, 0.5f, 0)) ||
        !xform(D3DTS_PROJECTION, mOrtho(0.5f)))
        return false;
    const Px px[] = {{144, 80, CC00}, {112, 80, BLUE}, {200, 80, BLUE}, {144, 144, BLUE}};
    return frames(BLUE, px, 4, [&] { return fdraw(FVF_C, q); });
}
// A perspective projection divides by w (eye z 2): x halves to pixels 96-160, y to 70-122.
static bool rowFfPersp() {
    FC q[4];
    mquad(q, -0.5f, 0.1f, 0.5f, 0.9f, 2);
    for (FC &v : q) v.c = D3DCOLOR_XRGB(0, 204, 0);
    if (!xform(D3DTS_PROJECTION, mPersp())) return false;
    const Px px[] = {{128, 96, CC00}, {80, 96, BLUE}, {128, 40, BLUE}};
    return frames(BLUE, px, 3, [&] { return fdraw(FVF_C, q); });
}
// A pretransformed quad, an untransformed one and a pretransformed one in one frame: each binds its own constant rows.
static bool rowFfRhwMix() {
    RV a[4], c[4];
    quadRhw(a, 16, 100, 72, 156, D3DCOLOR_XRGB(255, 0, 0)), quadRhw(c, 144, 100, 200, 156, D3DCOLOR_XRGB(255, 255, 0));
    FC b[4];
    mquad(b, -0.375f, -0.25f, 0, 0.25f, 0.5f);
    for (FC &v : b) v.c = D3DCOLOR_XRGB(0, 255, 0);
    const Px px[] = {{44, 128, 0xff0000}, {104, 128, 0x00ff00}, {172, 128, 0xffff00}, {232, 128, BLUE}};
    return frames(BLUE, px, 4, [&] {
        return fdraw(D3DFVF_XYZRHW | D3DFVF_DIFFUSE, a) && fdraw(FVF_C, b) && fdraw(D3DFVF_XYZRHW | D3DFVF_DIFFUSE, c);
    });
}
// Two lit draws of one frame, VIEW turned half a turn about z between them: the quad moves to the opposite corner and
// stays lit the same (normals (1, 0, 0) against a light along -x: lights and normals turn together). Lights packed
// with the first draw's VIEW would leave the second draw's diffuse term dark.
static bool rowFfViewChange() {
    FN q[4];
    mquad(q, 0.25f, 0.25f, 0.75f, 0.75f, 2.5f);
    normals(q, 1, 0, 0);
    D3DLIGHT9 l = ldir(1, 1, 1, 1, 1, 1);
    l.Direction = {-1, 0, 0};
    if (!rstate(D3DRS_LIGHTING, TRUE) || !lightOn(0, l) || !material(0, 0.8f, 0, 0.4f, 0, 0) ||
        !xform(D3DTS_PROJECTION, mOrtho()))
        return false;
    D3DMATRIX turned = IDENTITY;
    turned._11 = turned._22 = -1;
    const Px px[] = {{192, 64, 0x66cc00}, {64, 192, 0x66cc00}, {64, 64, BLUE}, {192, 192, BLUE}};
    return frames(BLUE, px, 4, [&] {
        return xform(D3DTS_VIEW, IDENTITY) && fdraw(FVF_N, q) && xform(D3DTS_VIEW, turned) && fdraw(FVF_N, q);
    });
}
// LIGHTING with no light: the global ambient times the material's ambient, plus its emissive.
static bool rowFfAmbient() {
    FP q[4];
    qOne(q, 0.5f);
    D3DMATERIAL9 m = {};
    m.Ambient = {0.8f, 0.8f, 0.8f, 1}, m.Emissive = {0, 0, 0.4f, 1};
    CK(SetMaterial, &m);
    if (!rstate(D3DRS_LIGHTING, TRUE) || !rstate(D3DRS_AMBIENT, 0x00ff8000)) return false;
    const Px px[] = {{128, 128, 0xcc6666}, {16, 16, BLUE}};
    return frames(BLUE, px, 2, [&] { return fdraw(D3DFVF_XYZ, q); });
}
// D3D9's default, LIGHTING on: with no light and the zero material a coloured vertex draws black.
static bool rowFfLightDefault() {
    FC q[4];
    qOne(q, 0.5f);
    for (FC &v : q) v.c = D3DCOLOR_XRGB(0, 255, 0);
    if (!rstate(D3DRS_LIGHTING, TRUE)) return false;
    const Px px[] = {{128, 128, 0x000000}, {16, 16, BLUE}};
    return frames(BLUE, px, 2, [&] { return fdraw(FVF_C, q); });
}
// One directional light: N.L of 1 (left) and of 0.5 (right) on a 0.8 red material.
static bool rowFfLightDir() {
    FN l[4], r[4];
    qLeft(l, 0.5f), qRight(r, 0.5f);
    normals(l, 0, 0, -1), normals(r, 0.8660254f, 0, -0.5f);
    if (!rstate(D3DRS_LIGHTING, TRUE) || !lightOn(0, ldir(1, 1, 1)) || !material(0.8f, 0, 0)) return false;
    const Px px[] = {{64, 128, 0xcc0000}, {192, 128, 0x660000}};
    return frames(BLUE, px, 2, [&] { return fdraw(FVF_N, l) && fdraw(FVF_N, r); });
}
// Each vertex normal points at the light at (lx, ly, lz).
static void faceLight(FN *q, float lx, float ly, float lz) {
    for (int i = 0; i < 4; i++) {
        const float x = lx - q[i].x, y = ly - q[i].y, z = lz - q[i].z, n = sqrtf(x * x + y * y + z * z);
        q[i].nx = x / n, q[i].ny = y / n, q[i].nz = z / n;
    }
}
// A point light of range 0.9: the quad within it takes its ambient and diffuse, the one past it nothing.
static bool rowFfLightPoint() {
    FN a[4], b[4];
    mquad(a, -0.8f, -0.2f, -0.4f, 0.2f, 2.5f), mquad(b, 0.4f, -0.2f, 0.8f, 0.2f, 2.5f);
    faceLight(a, -0.6f, 0, 2), faceLight(b, -0.6f, 0, 2);
    D3DLIGHT9 l = {};
    l.Type = D3DLIGHT_POINT, l.Position = {-0.6f, 0, 2}, l.Range = 0.9f, l.Attenuation0 = 1;
    l.Diffuse = {0, 0.4f, 0, 1}, l.Ambient = {0.8f, 0, 0, 1};
    if (!rstate(D3DRS_LIGHTING, TRUE) || !lightOn(0, l) || !material(1, 1, 1, 1, 1, 1) ||
        !xform(D3DTS_PROJECTION, mOrtho()))
        return false;
    const Px px[] = {{51, 128, 0xcc6600}, {205, 128, 0x000000}};
    return frames(BLUE, px, 2, [&] { return fdraw(FVF_N, a) && fdraw(FVF_N, b); });
}
// A spot light: the quad inside its inner cone is lit in full, the one outside its outer cone not at all.
static bool rowFfLightSpot() {
    FN a[4], b[4];
    mquad(a, -0.1f, -0.1f, 0.1f, 0.1f, 2.5f), mquad(b, 0.6f, -0.1f, 0.9f, 0.1f, 2.5f);
    faceLight(a, 0, 0, 1.5f), faceLight(b, 0, 0, 1.5f);
    D3DLIGHT9 l = {};
    l.Type = D3DLIGHT_SPOT, l.Position = {0, 0, 1.5f}, l.Direction = {0, 0, 1}, l.Range = 10, l.Attenuation0 = 1;
    l.Theta = 0.6f, l.Phi = 0.8f, l.Falloff = 1, l.Diffuse = {0, 0.4f, 0, 1}, l.Ambient = {0.8f, 0, 0, 1};
    if (!rstate(D3DRS_LIGHTING, TRUE) || !lightOn(0, l) || !material(1, 1, 1, 1, 1, 1) ||
        !xform(D3DTS_PROJECTION, mOrtho()))
        return false;
    const Px px[] = {{128, 128, 0xcc6600}, {224, 128, 0x000000}};
    return frames(BLUE, px, 2, [&] { return fdraw(FVF_N, a) && fdraw(FVF_N, b); });
}
// Lights 3 (red) and 12 (green, past the eight built-in indices) both light the quad.
static bool rowFfLightSparse() {
    FN q[4];
    qOne(q, 0.5f);
    normals(q, 0, 0, -1);
    if (!rstate(D3DRS_LIGHTING, TRUE) || !lightOn(3, ldir(0.8f, 0, 0)) || !lightOn(12, ldir(0, 0.4f, 0)) ||
        !material(1, 1, 1))
        return false;
    const Px px[] = {{128, 128, 0xcc6600}, {16, 16, BLUE}};
    return frames(BLUE, px, 2, [&] { return fdraw(FVF_N, q); });
}
// Nine lights enabled (0-6 red, 12 green, 20 blue): the first eight by index light the quad, the ninth does not.
static bool rowFfLight8() {
    FN q[4];
    qOne(q, 0.5f);
    normals(q, 0, 0, -1);
    if (!rstate(D3DRS_LIGHTING, TRUE) || !material(1, 1, 1)) return false;
    for (DWORD i = 0; i < 7; i++)
        if (!lightOn(i, ldir(0.12f, 0, 0))) return false;
    if (!lightOn(12, ldir(0, 0.4f, 0)) || !lightOn(20, ldir(0, 0, 0.4f))) return false;
    const Px px[] = {{128, 128, 0xd66600}, {16, 16, BLUE}};
    return frames(BLUE, px, 2, [&] { return fdraw(FVF_N, q); });
}
// SPECULARENABLE: facing the light and the (non-local) viewer, the light's specular colour adds to its diffuse.
static bool rowFfSpecular() {
    FN q[4];
    qOne(q, 0.5f);
    normals(q, 0, 0, -1);
    D3DLIGHT9 l = ldir(0.8f, 0, 0);
    l.Specular = {0, 0.4f, 0, 1};
    D3DMATERIAL9 m = {};
    m.Diffuse = {1, 1, 1, 1}, m.Specular = {1, 1, 1, 1}, m.Power = 8;
    CK(SetMaterial, &m);
    if (!rstate(D3DRS_LIGHTING, TRUE) || !rstate(D3DRS_SPECULARENABLE, TRUE) || !rstate(D3DRS_LOCALVIEWER, FALSE) ||
        !lightOn(0, l))
        return false;
    const Px px[] = {{128, 128, 0xcc6600}, {16, 16, BLUE}};
    return frames(BLUE, px, 2, [&] { return fdraw(FVF_N, q); });
}
// The diffuse material source: the material (left), the vertex colour (middle), and the material again once
// COLORVERTEX is off (right).
static bool rowFfMatSrc() {
    FNC q[3][4];
    for (int i = 0; i < 3; i++) {
        q3(q[i], i, 0.5f), normals(q[i], 0, 0, -1);
        for (FNC &v : q[i]) v.c = D3DCOLOR_XRGB(0, 204, 0);
    }
    if (!rstate(D3DRS_LIGHTING, TRUE) || !lightOn(0, ldir(1, 1, 1)) || !material(0.8f, 0, 0)) return false;
    const Px px[] = {{48, 128, 0xcc0000}, {128, 128, CC00}, {208, 128, 0xcc0000}};
    return frames(BLUE, px, 3, [&] {
        return rstate(D3DRS_COLORVERTEX, TRUE) && rstate(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL) &&
               fdraw(FVF_NC, q[0]) && rstate(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1) && fdraw(FVF_NC, q[1]) &&
               rstate(D3DRS_COLORVERTEX, FALSE) && fdraw(FVF_NC, q[2]);
    });
}
// NORMALIZENORMALS: a half-length normal lights at half (left) unless normalised (middle); under a WORLD scale of
// (2, 1, 1) a normal goes through the inverse transpose before it is normalised (right: 0.894 of the light).
static bool rowFfNormalize() {
    FN l[4], m[4], r[4];
    q3(l, 0, 0.5f), q3(m, 1, 0.5f), mquad(r, 0.1875f, -0.25f, 0.4375f, 0.25f, 0.5f);
    normals(l, 0, 0, -0.5f), normals(m, 0, 0, -0.5f), normals(r, 0.7071068f, 0, -0.7071068f);
    D3DMATRIX wide = IDENTITY;
    wide._11 = 2;
    if (!rstate(D3DRS_LIGHTING, TRUE) || !lightOn(0, ldir(1, 1, 1)) || !material(0.8f, 0, 0)) return false;
    const Px px[] = {{48, 128, 0x660000}, {128, 128, 0xcc0000}, {208, 128, 0xb60000}};
    return frames(BLUE, px, 3, [&] {
        return xform(D3DTS_WORLD, IDENTITY) && rstate(D3DRS_NORMALIZENORMALS, FALSE) && fdraw(FVF_N, l) &&
               rstate(D3DRS_NORMALIZENORMALS, TRUE) && fdraw(FVF_N, m) && xform(D3DTS_WORLD, wide) && fdraw(FVF_N, r);
    });
}
// Linear vertex fog over eye z: halfway between FOGSTART 1 and FOGEND 5 the colour is half the fog's.
static bool rowFfFogVertex() {
    FC q[4];
    qOne(q, 3);
    for (FC &v : q) v.c = D3DCOLOR_XRGB(204, 0, 0);
    if (!xform(D3DTS_PROJECTION, mOrtho()) || !rstate(D3DRS_FOGENABLE, TRUE) ||
        !rstate(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR) || !rstate(D3DRS_FOGSTART, fbits(1)) ||
        !rstate(D3DRS_FOGEND, fbits(5)) || !rstate(D3DRS_FOGCOLOR, 0x006600))
        return false;
    const Px px[] = {{128, 128, 0x663300}, {16, 16, BLUE}};
    return frames(BLUE, px, 2, [&] { return fdraw(FVF_C, q); });
}
// Linear table (pixel) fog over the fixed-function vertex stage: its source is w under a perspective projection
// (left: w 3 between 1 and 5) and depth under an orthographic one (right: depth 0.5 between 0 and 1).
static bool rowFfFogTable() {
    FC l[4], r[4];
    mquad(l, -0.875f * 3, -0.25f * 3, -0.125f * 3, 0.25f * 3, 3), qRight(r, 2.5f);
    for (int i = 0; i < 4; i++) l[i].c = r[i].c = D3DCOLOR_XRGB(204, 0, 0);
    if (!rstate(D3DRS_FOGENABLE, TRUE) || !rstate(D3DRS_FOGTABLEMODE, D3DFOG_LINEAR) ||
        !rstate(D3DRS_FOGCOLOR, 0x006600))
        return false;
    const Px px[] = {{64, 128, 0x663300}, {192, 128, 0x663300}};
    return frames(BLUE, px, 2, [&] {
        return xform(D3DTS_PROJECTION, mPersp()) && rstate(D3DRS_FOGSTART, fbits(1)) && rstate(D3DRS_FOGEND, fbits(5)) &&
               fdraw(FVF_C, l) && xform(D3DTS_PROJECTION, mOrtho()) && rstate(D3DRS_FOGSTART, fbits(0)) &&
               rstate(D3DRS_FOGEND, fbits(1)) && fdraw(FVF_C, r);
    });
}
// RG on stage 0, point-sampled and clamped, drawn unlit: the texel alone.
static bool stageRG(Com<IDirect3DTexture9> &t) {
    if (!makeRG(t) || !pointClamp(0)) return false;
    CK(SetTexture, 0, t.p);
    CK(SetTextureStageState, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    return true;
}
// Texgen: the eye-space position (z 3 through VIEW) times a texture transform is the coordinate, u = 3 * 0.25: green.
// A passthrough coordinate (none in the layout) or the model-space position would read red.
static bool rowFfTexgen() {
    Com<IDirect3DTexture9> t;
    FP q[4];
    mquad(q, -0.5f, -0.25f, 0.5f, 0.25f, 1);
    D3DMATRIX m = {};
    m._31 = 0.25f, m._42 = 0.5f;
    if (!stageRG(t) || !xform(D3DTS_PROJECTION, mOrtho()) || !xform(D3DTS_VIEW, mTrans(0, 0, 2)) ||
        !xform(D3DTS_TEXTURE0, m))
        return false;
    CK(SetTextureStageState, 0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
    CK(SetTextureStageState, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
    const Px px[] = {{96, 128, 0x00ff00}, {160, 128, 0x00ff00}, {16, 16, BLUE}};
    return frames(BLUE, px, 3, [&] { return fdraw(D3DFVF_XYZ, q); });
}
// A COUNT2 texture transform: a two-component coordinate is (u, v, 1), so the translation is the matrix's third row.
static bool rowFfTt() {
    Com<IDirect3DTexture9> t;
    FT q[4];
    qOne(q, 0.5f);
    for (FT &v : q) v.u = 0.25f, v.v = 0.5f;
    D3DMATRIX m = IDENTITY;
    m._31 = 0.5f;
    if (!stageRG(t) || !xform(D3DTS_TEXTURE0, m)) return false;
    CK(SetTextureStageState, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
    const Px px[] = {{128, 128, 0x00ff00}, {16, 16, BLUE}};
    return frames(BLUE, px, 2, [&] { return fdraw(D3DFVF_XYZ | D3DFVF_TEX1, q); });
}
// COUNT3 | PROJECTED: (u, v) divide by the third component, 0.875 / 2 in a 4x1 texture of red, green, white, yellow.
static bool rowFfTtProj() {
    Com<IDirect3DTexture9> t;
    static const DWORD texels[4] = {TRED, TGRN, TWHT, D3DCOLOR_XRGB(255, 255, 0)};
    if (!make2d(t, 4, 1, 1, D3DFMT_X8R8G8B8, 4, [](int x, int) { return texels[x]; }) || !pointClamp(0)) return false;
    CK(SetTexture, 0, t.p);
    CK(SetTextureStageState, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    CK(SetTextureStageState, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3 | D3DTTFF_PROJECTED);
    FT3 q[4];
    qOne(q, 0.5f);
    for (FT3 &v : q) v.u = 0.875f, v.v = 0.5f, v.w = 2;
    const Px px[] = {{128, 128, 0x00ff00}, {16, 16, BLUE}};
    return frames(BLUE, px, 2, [&] { return fdraw(D3DFVF_XYZ | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE3(0), q); });
}
// TEXCOORDINDEX: stage 0 reads coordinate set 0 (red, left), then set 1 (green, right).
static bool rowFfTcIndex() {
    Com<IDirect3DTexture9> t;
    FT2 l[4], r[4];
    qLeft(l, 0.5f), qRight(r, 0.5f);
    for (int i = 0; i < 4; i++) l[i].u0 = r[i].u0 = 0.25f, l[i].u1 = r[i].u1 = 0.75f, l[i].v0 = l[i].v1 = r[i].v0 = r[i].v1 = 0.5f;
    if (!stageRG(t)) return false;
    const Px px[] = {{64, 128, 0xff0000}, {192, 128, 0x00ff00}};
    return frames(BLUE, px, 2, [&] {
        CK(SetTextureStageState, 0, D3DTSS_TEXCOORDINDEX, 0);
        if (!fdraw(D3DFVF_XYZ | D3DFVF_TEX2, l)) return false;
        CK(SetTextureStageState, 0, D3DTSS_TEXCOORDINDEX, 1);
        return fdraw(D3DFVF_XYZ | D3DFVF_TEX2, r);
    });
}
// Lit and textured, as most fixed-function games draw: the lit 0.8 grey modulates the green texel.
static bool rowFfTex() {
    Com<IDirect3DTexture9> t;
    FNT q[4];
    qOne(q, 0.5f);
    normals(q, 0, 0, -1);
    for (FNT &v : q) v.u = 0.75f, v.v = 0.5f;
    if (!makeRG(t) || !pointClamp(0) || !rstate(D3DRS_LIGHTING, TRUE) || !lightOn(0, ldir(1, 1, 1)) ||
        !material(0.8f, 0.8f, 0.8f))
        return false;
    CK(SetTexture, 0, t.p);
    const Px px[] = {{128, 128, CC00}, {16, 16, BLUE}};
    return frames(BLUE, px, 2, [&] { return fdraw(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1, q); });
}
// One blend weight of 0.5 between WORLD (identity) and WORLDMATRIX(1) (x + 0.5): the quad moves by 0.25.
static bool rowFfBlend() {
    FB1 q[4];
    mquad(q, -0.75f, 0.25f, -0.25f, 0.75f, 0.5f);
    for (FB1 &v : q) v.b = 0.5f, v.c = D3DCOLOR_XRGB(0, 255, 0);
    if (!rstate(D3DRS_VERTEXBLEND, D3DVBF_1WEIGHTS) || !xform(D3DTS_WORLDMATRIX(1), mTrans(0.5f, 0, 0))) return false;
    const Px px[] = {{96, 64, 0x00ff00}, {48, 64, BLUE}, {144, 64, BLUE}};
    return frames(BLUE, px, 3, [&] { return fdraw(D3DFVF_XYZB1 | D3DFVF_DIFFUSE, q); });
}
// Indexed blending: the vertex's first index (5) names the matrix its weight of 1 takes (x + 0.5).
static bool rowFfBlendIdx() {
    FB2 q[4];
    mquad(q, -0.75f, 0.25f, -0.25f, 0.75f, 0.5f);
    for (FB2 &v : q) v.b = 1, v.idx = 0x00000905, v.c = D3DCOLOR_XRGB(0, 255, 0);
    if (!rstate(D3DRS_VERTEXBLEND, D3DVBF_1WEIGHTS) || !rstate(D3DRS_INDEXEDVERTEXBLENDENABLE, TRUE) ||
        !xform(D3DTS_WORLDMATRIX(5), mTrans(0.5f, 0, 0)) || !xform(D3DTS_WORLDMATRIX(1), mTrans(0, -1, 0)) ||
        !xform(D3DTS_WORLDMATRIX(9), mTrans(0, -1, 0)))
        return false;
    const Px px[] = {{128, 64, 0x00ff00}, {48, 64, BLUE}};
    return frames(BLUE, px, 2, [&] { return fdraw(D3DFVF_XYZB2 | D3DFVF_LASTBETA_UBYTE4 | D3DFVF_DIFFUSE, q); });
}
// A point of POINTSIZE 16 about pixel (128, 64).
static bool rowFfPoint() {
    const FC p = {0, 0.5f, 0.5f, D3DCOLOR_XRGB(0, 255, 0)};
    if (!rstate(D3DRS_POINTSIZE, fbits(16))) return false;
    const Px px[] = {{124, 60, 0x00ff00}, {132, 68, 0x00ff00}, {142, 64, BLUE}};
    return frames(BLUE, px, 3, [&] {
        CK(SetFVF, FVF_C);
        CK(DrawPrimitiveUP, D3DPT_POINTLIST, 1, &p, sizeof p);
        return true;
    });
}
// POINTSCALEENABLE: 256 * 0.125 / the eye distance 2 is 16 pixels; a second point past the far plane (depth 1.2) is
// clipped, not clamped onto it.
static bool rowFfPointScale() {
    const FC p[2] = {{0, 0, 2, D3DCOLOR_XRGB(0, 255, 0)}, {0.5f, 0.5f, 6, D3DCOLOR_XRGB(0, 255, 0)}};
    if (!xform(D3DTS_PROJECTION, mOrtho()) || !rstate(D3DRS_POINTSCALEENABLE, TRUE) ||
        !rstate(D3DRS_POINTSCALE_A, fbits(0)) || !rstate(D3DRS_POINTSCALE_B, fbits(0)) ||
        !rstate(D3DRS_POINTSCALE_C, fbits(1)) || !rstate(D3DRS_POINTSIZE, fbits(0.125f)))
        return false;
    const Px px[] = {{124, 124, 0x00ff00}, {132, 132, 0x00ff00}, {142, 128, BLUE}, {192, 64, BLUE}};
    return frames(BLUE, px, 4, [&] {
        CK(SetFVF, FVF_C);
        CK(DrawPrimitiveUP, D3DPT_POINTLIST, 2, p, sizeof *p);
        return true;
    });
}
// A user clip plane is in world space: x >= 0 keeps the quad's right half, which VIEW then moves to pixels 160-224.
static bool rowFfClip() {
    FC q[4];
    mquad(q, -0.5f, 0.25f, 0.5f, 0.75f, 0.5f);
    for (FC &v : q) v.c = D3DCOLOR_XRGB(0, 255, 0);
    const float plane[4] = {1, 0, 0, 0};
    CK(SetClipPlane, 0, plane);
    if (!xform(D3DTS_VIEW, mTrans(0.25f, 0, 0)) || !rstate(D3DRS_CLIPPLANEENABLE, 1)) return false;
    const Px px[] = {{192, 64, 0x00ff00}, {144, 64, BLUE}, {112, 64, BLUE}};
    return frames(BLUE, px, 3, [&] { return fdraw(FVF_C, q); });
}
// DrawIndexedPrimitiveUP with min_vertex_index 4 through the fixed-function vertex stage (WORLD x + 0.25).
static bool rowFfIndexed() {
    FC v[8];
    mquad(v, -0.25f, 0.6f, 0.25f, 0.9f, 0.5f), qOne(v + 4, 0.5f);
    for (FC &q : v) q.c = D3DCOLOR_XRGB(0, 204, 0);
    const WORD idx[6] = {4, 5, 6, 6, 5, 7};
    if (!xform(D3DTS_WORLD, mTrans(0.25f, 0, 0))) return false;
    const Px px[] = {{160, 128, CC00}, {112, 128, BLUE}, {160, 32, BLUE}};
    return frames(BLUE, px, 3, [&] {
        CK(SetFVF, FVF_C);
        CK(DrawIndexedPrimitiveUP, D3DPT_TRIANGLELIST, 4, 4, 2, idx, D3DFMT_INDEX16, v, sizeof *v);
        return true;
    });
}
// A vertex declaration instead of an FVF, the colour first: it is the diffuse material (COLOR1), lit red over a
// material that alone would light green.
static bool rowFfDecl() {
    struct DV { DWORD c; float x, y, z, nx, ny, nz; };
    static const D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
        {0, 4, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 16, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
        D3DDECL_END()};
    Com<IDirect3DVertexDeclaration9> decl;
    CK(CreateVertexDeclaration, elements, &decl.p);
    DV q[4];
    qOne(q, 0.5f);
    normals(q, 0, 0, -1);
    for (DV &v : q) v.c = 0xffcc0000;
    if (!rstate(D3DRS_LIGHTING, TRUE) || !lightOn(0, ldir(1, 1, 1)) || !material(0, 0.8f, 0)) return false;
    const Px px[] = {{128, 128, 0xcc0000}, {16, 16, BLUE}};
    return frames(BLUE, px, 2, [&] {
        CK(SetVertexDeclaration, decl.p);
        CK(DrawPrimitiveUP, D3DPT_TRIANGLESTRIP, 2, q, sizeof *q);
        return true;
    });
}
// A depth texture at a vertex sampler the shader reads: past the core's slice (C4b-3d2).
static bool vtxTargetRow(DWORD usage, D3DFORMAT format) {
    Com<IDirect3DTexture9> t;
    CK(CreateTexture, 4, 4, 1, usage, format, D3DPOOL_DEFAULT, &t.p, nullptr);
    Prog p;
    if (!makeVtx(p, 2, 0) || !use(p)) return false;
    CK(SetTexture, VS0, t.p);
    V3 q[4];
    quad(q, -0.5f, -0.5f, 0.5f, 0.5f);
    const float c[4] = {0.5f, 0.5f, 0, 0};
    CK(SetVertexShaderConstantF, 0, c, 1);
    const bool ok = refusedRow([&] { return dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof *q); });
    CK(SetTexture, VS0, nullptr);
    return ok;
}
// A render-target texture cleared green through SetRenderTarget, at vertex sampler 0: the quad takes its colour.
static bool rowTexVtxRt() {
    Prog p;
    if (!makeVtx(p, 2, 0)) return false;
    V3 q[4];
    quad(q, -0.5f, -0.5f, 0.5f, 0.5f);
    const Px px[] = {{128, 128, TGRN}, {20, 20, BLUE}};
    return frames(BLUE, px, 2, [&] {
        Com<IDirect3DTexture9> t;
        if (!makeRt(t, 4, 1, D3DFMT_X8R8G8B8) || !target(t.p) || !clearTo(TGRN) || !backToScreen() || !use(p))
            return false;
        CK(SetTexture, VS0, t.p);
        const bool ok = vtxDraw(q, 0.5f, 0.5f);
        CK(SetTexture, VS0, nullptr);
        return ok;
    });
}
static bool rowTexVtxDepth() { return vtxTargetRow(D3DUSAGE_DEPTHSTENCIL, D3DFMT_D24S8); }
// A depth format made without the DEPTHSTENCIL usage is a depth texture too (the pixel slots refuse it by its format).
static bool rowTexVtxDepthFmt() { return vtxTargetRow(0, D3DFMT_D24S8); }

static void fixedRows(IDirect3DDevice9 *d, bool strict) {
    static const struct { const char *name; bool (*run)(); } rows[] = {
        {"ff-xform", rowFfXform},           {"ff-persp", rowFfPersp},           {"ff-rhwmix", rowFfRhwMix},
        {"ff-viewchange", rowFfViewChange}, {"ff-ambient", rowFfAmbient},       {"ff-lightdefault", rowFfLightDefault},
        {"ff-lightdir", rowFfLightDir},     {"ff-lightpoint", rowFfLightPoint}, {"ff-lightspot", rowFfLightSpot},
        {"ff-lightsparse", rowFfLightSparse}, {"ff-light8", rowFfLight8},       {"ff-specular", rowFfSpecular},
        {"ff-matsrc", rowFfMatSrc},         {"ff-normalize", rowFfNormalize},   {"ff-fogvertex", rowFfFogVertex},
        {"ff-fogtable", rowFfFogTable},     {"ff-texgen", rowFfTexgen},         {"ff-tt", rowFfTt},
        {"ff-ttproj", rowFfTtProj},         {"ff-tcindex", rowFfTcIndex},       {"ff-tex", rowFfTex},
        {"ff-blend", rowFfBlend},           {"ff-blendidx", rowFfBlendIdx},     {"ff-point", rowFfPoint},
        {"ff-pointscale", rowFfPointScale}, {"ff-clip", rowFfClip},             {"ff-indexed", rowFfIndexed},
        {"ff-decl", rowFfDecl},             {"tex-vtxrt", rowTexVtxRt},         {"tex-vtxdepth", rowTexVtxDepth},
        {"tex-vtxdepthfmt", rowTexVtxDepthFmt}};
    dev = d, strictMode = strict, budgetEnd = GetTickCount64() + 180000;
    HRESULT hr = dev->GetRenderTarget(0, &rtSurf);
    if (SUCCEEDED(hr))
        hr = dev->CreateOffscreenPlainSurface(256, 256, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sysSurf, nullptr);
    for (const auto &r : rows) {
        why[0] = 0;
        bool ok;
        if (FAILED(hr)) ok = fail("GetRenderTarget/CreateOffscreenPlainSurface", hr);
        else if (GetTickCount64() >= budgetEnd) ok = false, snprintf(why, sizeof why, "budget");
        else ok = reset() && r.run();
        row(r.name, ok, why);
    }
    if (sysSurf) sysSurf->Release();
    if (rtSurf) rtSurf->Release();
}

// The "targets" mode (DXMTRS C4b-3d1): render targets other than the back buffer. A row's objects are made inside its
// per-frame body, so a retried frame (mtld3d leaves a draw out while its pipeline builds) starts from new ones. A row
// that reads a target back runs its body through until(), which retries as frames() does. Texel and pixel
// coordinates: x right, y down.
template <class F> static bool scene(F body) {
    CK(BeginScene);
    const bool ok = body();
    dev->EndScene();
    return ok;
}
// attempt() (which sets `why`) and a Present, until it passes: for up to 10 s within the budget, once when strict.
template <class F> static bool until(F attempt) {
    const ULONGLONG now = GetTickCount64(), end = now + 10000 < budgetEnd ? now + 10000 : budgetEnd;
    for (;;) {
        const bool ok = attempt();
        dev->SetRenderTarget(0, rtSurf);
        CK(Present, nullptr, nullptr, nullptr, nullptr);
        if (ok) return true;
        if (strictMode || GetTickCount64() >= end) return false;
    }
}
// GetRenderTargetData of `src` (w x h, format f) into a new system-memory surface, which check(bits, pitch) reads.
template <class F> static bool readTarget(IDirect3DSurface9 *src, UINT w, UINT h, D3DFORMAT f, F check) {
    Com<IDirect3DSurface9> sys;
    CK(CreateOffscreenPlainSurface, w, h, f, D3DPOOL_SYSTEMMEM, &sys.p, nullptr);
    CK(GetRenderTargetData, src, sys.p);
    D3DLOCKED_RECT lr = {};
    const HRESULT hr = sys.p->LockRect(&lr, nullptr, D3DLOCK_READONLY);
    if (FAILED(hr)) return fail("LockRect", hr);
    const bool ok = check((const BYTE *)lr.pBits, (int)lr.Pitch);
    sys.p->UnlockRect();
    return ok;
}
static DWORD texel(const BYTE *bits, int pitch, int x, int y) {
    DWORD v;
    memcpy(&v, bits + y * pitch + x * 4, 4);
    return v;
}
// Whether `got` is `expect` in the channels of `mask`, each within 1; `why` names the texel otherwise.
static bool want(const char *what, DWORD got, DWORD expect, DWORD mask = 0xffffff) {
    for (int sh = 0; sh < 32; sh += 8)
        if ((mask >> sh & 0xff) && abs((int)(got >> sh & 0xff) - (int)(expect >> sh & 0xff)) > 1)
            return snprintf(why, sizeof why, "%s %08lx want %08lx", what, (unsigned long)got, (unsigned long)expect),
                   false;
    return true;
}
static bool levelOf(IDirect3DTexture9 *t, UINT level, Com<IDirect3DSurface9> &s) {
    const HRESULT hr = t->GetSurfaceLevel(level, &s.p);
    return SUCCEEDED(hr) || fail("GetSurfaceLevel", hr);
}
// A model-space quad of one colour (identity transforms: clip space), clockwise on screen.
static void cquad(FC *q, float x0, float y0, float x1, float y1, DWORD c) {
    mquad(q, x0, y0, x1, y1, 0.5f);
    for (int i = 0; i < 4; i++) q[i].c = c;
}

// A texture cleared green with a red pretransformed quad over texels 8..23, then shown on the back buffer: the quad's
// texel (16,16) at (97,97), green at the three other quadrant points (a flipped or mirrored target moves the red).
static bool rowRtTex() {
    const Px px[] = {{97, 97, TRED}, {161, 161, TGRN}, {97, 161, TGRN}, {161, 97, TGRN}, {32, 32, BLUE}};
    return frames(BLUE, px, 5, [&] {
        Com<IDirect3DTexture9> a;
        RV q[4];
        quadRhw(q, 8, 8, 24, 24, TRED);
        return makeRt(a) && target(a.p) && clearTo(TGRN) && rdraw(q) && backToScreen() && show(a.p);
    });
}
// The same contents read back, the Clear's alpha 0x80 kept; a destination of another size and no source are
// D3DERR_INVALIDCALL.
static bool rowRtRead() {
    return until([&] {
        Com<IDirect3DTexture9> a;
        Com<IDirect3DSurface9> s, small;
        RV q[4];
        quadRhw(q, 8, 8, 24, 24, TRED);
        if (!makeRt(a) || !levelOf(a.p, 0, s)) return false;
        if (!scene([&] { return target(a.p) && clearTo(0x8000ff00) && rdraw(q) && backToScreen(); })) return false;
        CK(CreateOffscreenPlainSurface, 32, 32, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &small.p, nullptr);
        HRESULT hr = dev->GetRenderTargetData(s.p, small.p);
        if (hr != D3DERR_INVALIDCALL) return fail("GetRenderTargetData (a 32x32 destination)", hr);
        hr = dev->GetRenderTargetData(nullptr, small.p);
        if (hr != D3DERR_INVALIDCALL) return fail("GetRenderTargetData (no source)", hr);
        return readTarget(s.p, 64, 64, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
            return want("(16,16)", texel(b, p, 16, 16), 0xffff0000, 0xffffffff) &&
                   want("(48,48)", texel(b, p, 48, 48), 0x8000ff00, 0xffffffff) &&
                   want("(16,48)", texel(b, p, 16, 48), 0x00ff00);
        });
    });
}
// A CreateRenderTarget surface: its description, GetRenderTarget answering it, a red Clear and a green model-space quad
// over clip x, y in [0, 1] (texels 32..63, 0..31).
static bool rowRtSurface() {
    return until([&] {
        Com<IDirect3DSurface9> s, back;
        CK(CreateRenderTarget, 64, 64, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &s.p, nullptr);
        D3DSURFACE_DESC d = {};
        const HRESULT hr = s.p->GetDesc(&d);
        if (FAILED(hr)) return fail("GetDesc", hr);
        if (d.Width != 64 || d.Height != 64 || d.Format != D3DFMT_X8R8G8B8 || d.Usage != D3DUSAGE_RENDERTARGET ||
            d.Pool != D3DPOOL_DEFAULT)
            return snprintf(why, sizeof why, "desc %ux%u format %d usage %lx pool %d", d.Width, d.Height, (int)d.Format,
                            (unsigned long)d.Usage, (int)d.Pool),
                   false;
        FC q[4];
        cquad(q, 0, 0, 1, 1, TGRN);
        if (!scene([&] {
                CK(SetRenderTarget, 0, s.p);
                CK(GetRenderTarget, 0, &back.p);
                return clearTo(TRED) && fdraw(FVF_C, q) && backToScreen();
            }))
            return false;
        if (back.p != s.p) return snprintf(why, sizeof why, "GetRenderTarget answers another surface"), false;
        return readTarget(s.p, 64, 64, D3DFMT_X8R8G8B8, [&](const BYTE *b, int p) {
            return want("(48,16)", texel(b, p, 48, 16), TGRN) && want("(16,48)", texel(b, p, 16, 48), TRED) &&
                   want("(16,16)", texel(b, p, 16, 16), TRED);
        });
    });
}
// SetRenderTarget resets the viewport and the scissor rectangle to the target; a viewport of the right half then
// confines a full-clip quad to it; back on the back buffer the viewport is the back buffer's.
static bool rowRtViewport() {
    return until([&] {
        Com<IDirect3DTexture9> a;
        Com<IDirect3DSurface9> s;
        if (!makeRt(a) || !levelOf(a.p, 0, s)) return false;
        D3DVIEWPORT9 onTarget = {}, onScreen = {};
        RECT sc = {};
        const D3DVIEWPORT9 half = {32, 0, 32, 64, 0.0f, 1.0f};
        FC q[4];
        cquad(q, -1, -1, 1, 1, TGRN);
        if (!scene([&] {
                if (!target(a.p)) return false;
                CK(GetViewport, &onTarget);
                CK(GetScissorRect, &sc);
                if (!clearTo(TRED)) return false;
                CK(SetViewport, &half);
                if (!fdraw(FVF_C, q) || !backToScreen()) return false;
                CK(GetViewport, &onScreen);
                return true;
            }))
            return false;
        const auto is = [](const D3DVIEWPORT9 &v, DWORD size) {
            return v.X == 0 && v.Y == 0 && v.Width == size && v.Height == size && v.MinZ == 0.0f && v.MaxZ == 1.0f;
        };
        if (!is(onTarget, 64) || !is(onScreen, 256) || sc.left != 0 || sc.top != 0 || sc.right != 64 || sc.bottom != 64)
            return snprintf(why, sizeof why, "viewport %lux%lu at %lu,%lu, scissor %ld,%ld-%ld,%ld, back %lux%lu",
                            (unsigned long)onTarget.Width, (unsigned long)onTarget.Height, (unsigned long)onTarget.X,
                            (unsigned long)onTarget.Y, (long)sc.left, (long)sc.top, (long)sc.right, (long)sc.bottom,
                            (unsigned long)onScreen.Width, (unsigned long)onScreen.Height),
                   false;
        return readTarget(s.p, 64, 64, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
            return want("(48,32)", texel(b, p, 48, 32), TGRN) && want("(16,32)", texel(b, p, 16, 32), TRED);
        });
    });
}
// A target larger than the back buffer: a quad in its lower right quarter, past the back buffer's 256 pixels.
static bool rowRtBig() {
    return until([&] {
        Com<IDirect3DTexture9> a;
        Com<IDirect3DSurface9> s;
        if (!makeRt(a, 512, 1, D3DFMT_X8R8G8B8) || !levelOf(a.p, 0, s)) return false;
        const D3DVIEWPORT9 quarter = {256, 256, 256, 256, 0.0f, 1.0f};
        FC q[4];
        cquad(q, -1, -1, 1, 1, TGRN);
        if (!scene([&] {
                if (!target(a.p) || !clearTo(TRED)) return false;
                CK(SetViewport, &quarter);
                return fdraw(FVF_C, q) && backToScreen();
            }))
            return false;
        return readTarget(s.p, 512, 512, D3DFMT_X8R8G8B8, [&](const BYTE *b, int p) {
            return want("(500,500)", texel(b, p, 500, 500), TGRN) && want("(100,100)", texel(b, p, 100, 100), TRED) &&
                   want("(300,100)", texel(b, p, 300, 100), TRED) && want("(100,300)", texel(b, p, 100, 300), TRED);
        });
    });
}
// Three levels, each its own target: level 0 red, level 1 green with a blue quad over its texels 0..15, level 2
// yellow; `level1` takes the viewport SetRenderTarget gave level 1.
static bool drawMips(Com<IDirect3DTexture9> &a, D3DVIEWPORT9 *level1) {
    RV q[4];
    quadRhw(q, 0, 0, 16, 16, TBLU);
    if (!makeRt(a, 64, 3) || !target(a.p, 0) || !clearTo(TRED) || !target(a.p, 1)) return false;
    CK(GetViewport, level1);
    return clearTo(TGRN) && rdraw(q) && target(a.p, 2) && clearTo(D3DCOLOR_XRGB(255, 255, 0)) && backToScreen();
}
static bool rowRtMip() {
    if (!until([&] {
            Com<IDirect3DTexture9> a;
            Com<IDirect3DSurface9> s0, s1;
            D3DVIEWPORT9 v = {};
            if (!scene([&] { return drawMips(a, &v); }) || !levelOf(a.p, 0, s0) || !levelOf(a.p, 1, s1)) return false;
            if (v.Width != 32 || v.Height != 32)
                return snprintf(why, sizeof why, "level 1's viewport %lux%lu", (unsigned long)v.Width,
                                (unsigned long)v.Height),
                       false;
            return readTarget(s1.p, 32, 32, D3DFMT_A8R8G8B8,
                              [&](const BYTE *b, int p) {
                                  return want("level 1 (8,8)", texel(b, p, 8, 8), TBLU) &&
                                         want("level 1 (24,24)", texel(b, p, 24, 24), TGRN);
                              }) &&
                   readTarget(s0.p, 64, 64, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
                       return want("level 0 (32,32)", texel(b, p, 32, 32), TRED);
                   });
        }))
        return false;
    // Sampled no finer than level 1: its green, away from its blue quad.
    const Px px[] = {{161, 161, TGRN}};
    return frames(BLUE, px, 1, [&] {
        Com<IDirect3DTexture9> a;
        D3DVIEWPORT9 v = {};
        if (!drawMips(a, &v)) return false;
        CK(SetSamplerState, 0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
        CK(SetSamplerState, 0, D3DSAMP_MAXMIPLEVEL, 1);
        const bool ok = show(a.p);
        CK(SetSamplerState, 0, D3DSAMP_MAXMIPLEVEL, 0);
        return ok;
    });
}
// A cube whose six faces are each cleared to a colour of their own through GetCubeMapSurface targets.
static const DWORD CUBE_FACES[6] = {0xff0000, 0x00ff00, 0x0000ff, 0xffff00, 0xff00ff, 0x00ffff};
static bool clearCube(Com<IDirect3DCubeTexture9> &c) {
    CK(CreateCubeTexture, 16, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &c.p, nullptr);
    for (int f = 0; f < 6; f++) {
        Com<IDirect3DSurface9> s;
        const HRESULT hr = c.p->GetCubeMapSurface((D3DCUBEMAP_FACES)f, 0, &s.p);
        if (FAILED(hr)) return fail("GetCubeMapSurface", hr);
        CK(SetRenderTarget, 0, s.p);
        if (!clearTo(0xff000000 | CUBE_FACES[f])) return false;
    }
    return backToScreen();
}
static bool rowRtCube() {
    if (!until([&] {
            Com<IDirect3DCubeTexture9> c;
            if (!scene([&] { return clearCube(c); })) return false;
            for (int f = 0; f < 6; f++) {
                Com<IDirect3DSurface9> s;
                const HRESULT hr = c.p->GetCubeMapSurface((D3DCUBEMAP_FACES)f, 0, &s.p);
                if (FAILED(hr)) return fail("GetCubeMapSurface", hr);
                char name[16];
                snprintf(name, sizeof name, "face %d", f);
                if (!readTarget(s.p, 16, 16, D3DFMT_X8R8G8B8,
                                [&](const BYTE *b, int p) { return want(name, texel(b, p, 8, 8), CUBE_FACES[f]); }))
                    return false;
            }
            return true;
        }))
        return false;
    // Sampled along +z: face 4.
    const Px px[] = {{148, 116, CUBE_FACES[4]}};
    return frames(BLUE, px, 1, [&] {
        Com<IDirect3DCubeTexture9> c;
        Com<IDirect3DPixelShader9> ps;
        if (!clearCube(c) || !usePs(ps, KCUBE, 0, false) || !pointClamp(0)) return false;
        CK(SetTexture, 0, c.p);
        TV3 d[4];
        tquad(d, 128, 96, 168, 136, 0, 0, 1);
        const bool ok = tdraw(d);
        CK(SetTexture, 0, nullptr);
        CK(SetPixelShader, nullptr);
        return ok;
    });
}
// Formats that are not BGRA8: a 16x16 target cleared to (0x40, 0x80, 0xc0, 0xff) with a red quad over x < 8 (red, so
// that a red/blue swap shows), read back in the format's own layout and sampled.
struct TargetFormat { D3DFORMAT f; int bytes; const char *name; bool redOnly; };
static float halfFloat(WORD h) {
    const int e = h >> 10 & 31, m = h & 1023;
    const float v = e == 0 ? ldexpf((float)m, -24) : ldexpf((float)(m + 1024), e - 25);
    return h & 0x8000 ? -v : v;
}
// The texel at `p` as (r, g, b, a) in [0, 1]; a red-only format leaves the rest 0.
static void decodeTexel(D3DFORMAT f, const BYTE *p, float out[4]) {
    WORD w[4];
    float fl[4] = {};
    switch (f) {
    case D3DFMT_A8B8G8R8:
        for (int i = 0; i < 4; i++) out[i] = p[i] / 255.0f;
        return;
    case D3DFMT_A16B16G16R16:
        memcpy(w, p, 8);
        for (int i = 0; i < 4; i++) out[i] = w[i] / 65535.0f;
        return;
    case D3DFMT_A16B16G16R16F:
        memcpy(w, p, 8);
        for (int i = 0; i < 4; i++) out[i] = halfFloat(w[i]);
        return;
    case D3DFMT_R32F:
        memcpy(fl, p, 4);
        break;
    default:
        memcpy(fl, p, 16);
        break;
    }
    for (int i = 0; i < 4; i++) out[i] = fl[i];
}
static bool formatTarget(Com<IDirect3DTexture9> &t, D3DFORMAT f) {
    RV q[4];
    quadRhw(q, 0, 0, 8, 16, TRED);
    return makeRt(t, 16, 1, f) && target(t.p) && clearTo(0xff4080c0) && rdraw(q) && backToScreen();
}
static bool rowRtFormats() {
    static const TargetFormat fmts[] = {{D3DFMT_A8B8G8R8, 4, "A8B8G8R8", false},
                                        {D3DFMT_A16B16G16R16, 8, "A16B16G16R16", false},
                                        {D3DFMT_A16B16G16R16F, 8, "A16B16G16R16F", false},
                                        {D3DFMT_R32F, 4, "R32F", true},
                                        {D3DFMT_A32B32G32R32F, 16, "A32B32G32R32F", false}};
    for (const TargetFormat &m : fmts) {
        const bool read = until([&] {
            Com<IDirect3DTexture9> t;
            Com<IDirect3DSurface9> s;
            if (!scene([&] { return formatTarget(t, m.f); }) || !levelOf(t.p, 0, s)) return false;
            return readTarget(s.p, 16, 16, m.f, [&](const BYTE *b, int pitch) {
                float l[4], r[4];
                decodeTexel(m.f, b + 8 * pitch + 4 * m.bytes, l);
                decodeTexel(m.f, b + 8 * pitch + 12 * m.bytes, r);
                const float wl[4] = {1, 0, 0, 1}, wr[4] = {0x40 / 255.0f, 0x80 / 255.0f, 0xc0 / 255.0f, 1};
                for (int i = 0; i < (m.redOnly ? 1 : 4); i++)
                    if (fabsf(l[i] - wl[i]) > 0.004f || fabsf(r[i] - wr[i]) > 0.004f)
                        return snprintf(why, sizeof why, "%s read: x 4 %.3f %.3f %.3f %.3f, x 12 %.3f %.3f %.3f %.3f",
                                        m.name, l[0], l[1], l[2], l[3], r[0], r[1], r[2], r[3]),
                               false;
                return true;
            });
        });
        if (!read) {
            if (!strstr(why, m.name)) {
                char first[300];
                snprintf(first, sizeof first, "%s", why);
                snprintf(why, sizeof why, "%s: %s", m.name, first);
            }
            return false;
        }
        // Shown: texel x of the 16 covers pixels 64+8x..71+8x; a red-only format samples (r, 1, 1, 1).
        const Px px[] = {{97, 129, m.redOnly ? 0xffffffu : 0xff0000u}, {161, 129, m.redOnly ? 0x40ffffu : 0x4080c0u}};
        if (!frames(BLUE, px, 2, [&] {
                Com<IDirect3DTexture9> t;
                return formatTarget(t, m.f) && show(t.p);
            })) {
            char first[300];
            snprintf(first, sizeof first, "%s", why);
            snprintf(why, sizeof why, "%s shown: %s", m.name, first);
            return false;
        }
    }
    return true;
}
// A quad drawn on the back buffer before the target changes is still there after it changes back.
static bool rowRtSwitch() {
    const Px px[] = {{48, 48, TRED}, {192, 192, TGRN}, {128, 128, BLUE}};
    return frames(BLUE, px, 3, [&] {
        Com<IDirect3DTexture9> a;
        RV q[4];
        quadRhw(q, 32, 32, 64, 64, TRED);
        return rdraw(q) && makeRt(a) && target(a.p) && clearTo(TGRN) && backToScreen() &&
               show(a.p, 160, 160, 224, 224);
    });
}
// A texture shown, rewritten and shown again in one frame shows each state: the rewrite does not overtake its reader.
static bool rowRtRewrite() {
    const Px px[] = {{64, 64, TGRN}, {192, 192, TRED}, {128, 128, BLUE}};
    return frames(BLUE, px, 3, [&] {
        Com<IDirect3DTexture9> a;
        return makeRt(a) && target(a.p) && clearTo(TGRN) && backToScreen() && show(a.p, 32, 32, 96, 96) &&
               target(a.p) && clearTo(TRED) && backToScreen() && show(a.p, 160, 160, 224, 224);
    });
}
// Two Clears and no draw: the later one's colour.
static bool rowRtClearOnly() {
    const Px px[] = {{129, 129, TGRN}, {32, 32, BLUE}};
    return frames(BLUE, px, 2, [&] {
        Com<IDirect3DTexture9> a;
        return makeRt(a) && target(a.p) && clearTo(TRED) && clearTo(TGRN) && backToScreen() && show(a.p);
    });
}
// A render-target texture nothing was drawn to reads black.
static bool rowRtFresh() {
    const Px px[] = {{129, 129, 0x000000}, {32, 32, BLUE}};
    return frames(BLUE, px, 2, [&] {
        Com<IDirect3DTexture9> a;
        return makeRt(a) && show(a.p);
    });
}
// An AUTOGENMIPMAP render target: its lower levels follow level 0 once the target changes away (an 8x8 quad of the 64
// texels samples level 3, which is red only if it was regenerated; left alone it is black).
static bool rowRtAutogen() {
    const Px px[] = {{128, 128, TRED}, {32, 32, BLUE}};
    return frames(BLUE, px, 2, [&] {
        Com<IDirect3DTexture9> a;
        if (!makeRt(a, 64, 0, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET | D3DUSAGE_AUTOGENMIPMAP)) return false;
        if (a.p->GetLevelCount() != 1)
            return snprintf(why, sizeof why, "GetLevelCount %lu", (unsigned long)a.p->GetLevelCount()), false;
        if (!target(a.p) || !clearTo(TRED) || !backToScreen()) return false;
        CK(SetSamplerState, 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        CK(SetSamplerState, 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        CK(SetSamplerState, 0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
        return show(a.p, 124, 124, 132, 132);
    });
}
// A texture released while it is the render target still takes the Clear (the back buffer does not: (32,32) stays
// blue), and one released while bound at stage 0 is still sampled.
static bool rowRtRelease() {
    const Px px[] = {{129, 129, TRED}, {32, 32, BLUE}};
    return frames(BLUE, px, 2, [&] {
        IDirect3DTexture9 *b = nullptr, *c = nullptr;
        CK(CreateTexture, 64, 64, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &b, nullptr);
        bool ok = target(b);
        b->Release();
        if (!ok || !clearTo(TGRN) || !backToScreen()) return false;
        CK(CreateTexture, 64, 64, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &c, nullptr);
        ok = target(c) && clearTo(TRED) && backToScreen();
        const HRESULT hr = dev->SetTexture(0, c);
        c->Release();
        if (!ok) return false;
        if (FAILED(hr)) return fail("SetTexture", hr);
        const DWORD w = D3DCOLOR_XRGB(255, 255, 255);
        const TV3 q[4] = {{64, 64, 0.5f, 1, w, 0, 0, 0}, {192, 64, 0.5f, 1, w, 1, 0, 0}, {64, 192, 0.5f, 1, w, 0, 1, 0},
                          {192, 192, 0.5f, 1, w, 1, 1, 0}};
        ok = tdraw(q);
        CK(SetTexture, 0, nullptr);
        return ok;
    });
}
// sRGB write on a target: a Clear of 128 and a quad of 64 are stored encoded (188, 137), and sampled back through
// D3DSAMP_SRGBTEXTURE they are 128 and 64 again.
static bool srgbTarget(Com<IDirect3DTexture9> &a) {
    RV q[4];
    quadRhw(q, 0, 0, 32, 64, D3DCOLOR_XRGB(64, 64, 64));
    if (!makeRt(a) || !target(a.p)) return false;
    CK(SetRenderState, D3DRS_SRGBWRITEENABLE, TRUE);
    const bool ok = clearTo(D3DCOLOR_XRGB(128, 128, 128)) && rdraw(q);
    CK(SetRenderState, D3DRS_SRGBWRITEENABLE, FALSE);
    return ok && backToScreen();
}
static bool rowRtSrgb() {
    if (!until([&] {
            Com<IDirect3DTexture9> a;
            Com<IDirect3DSurface9> s;
            if (!scene([&] { return srgbTarget(a); }) || !levelOf(a.p, 0, s)) return false;
            return readTarget(s.p, 64, 64, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
                return want("(48,32)", texel(b, p, 48, 32), 0xbcbcbc) && want("(16,32)", texel(b, p, 16, 32), 0x898989);
            });
        }))
        return false;
    const Px px[] = {{161, 129, 0x808080}, {97, 129, 0x404040}};
    return frames(BLUE, px, 2, [&] {
        Com<IDirect3DTexture9> a;
        if (!srgbTarget(a)) return false;
        CK(SetSamplerState, 0, D3DSAMP_SRGBTEXTURE, TRUE);
        const bool ok = show(a.p);
        CK(SetSamplerState, 0, D3DSAMP_SRGBTEXTURE, FALSE);
        return ok;
    });
}
// A programmable pair draws to a target: a ps_2_0 writing c0 (magenta) over a green Clear.
static bool rowRtPs() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_C0)) return false;
    return until([&] {
        Com<IDirect3DTexture9> a;
        Com<IDirect3DSurface9> s;
        V3 q[4];
        quad(q, -1, -1, 1, 1);
        if (!makeRt(a) || !levelOf(a.p, 0, s)) return false;
        if (!scene([&] {
                if (!target(a.p) || !clearTo(TGRN) || !use(p)) return false;
                CK(SetPixelShaderConstantF, 0, MAGENTA, 1);
                return strip(q) && backToScreen();
            }))
            return false;
        return readTarget(s.p, 64, 64, D3DFMT_A8R8G8B8,
                          [&](const BYTE *b, int pch) { return want("(32,32)", texel(b, pch, 32, 32), 0xff00ff); });
    });
}
// UpdateTexture into a render-target texture, then sampled.
static bool rowRtUpdate() {
    const Px px[] = {{129, 129, 0x00ffff}, {32, 32, BLUE}};
    return frames(BLUE, px, 2, [&] {
        Com<IDirect3DTexture9> src, dst;
        CK(CreateTexture, 4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &src.p, nullptr);
        if (!fill(src.p, 0, 4, [](int, int) { return 0xff00ffffu; }) || !makeRt(dst, 4)) return false;
        CK(UpdateTexture, src.p, dst.p);
        return show(dst.p);
    });
}
// UpdateTexture into the bound target lands where it is called: after the Clear, before the quad drawn next.
static bool rowRtUpdateBound() {
    return until([&] {
        Com<IDirect3DTexture9> src, a;
        Com<IDirect3DSurface9> s;
        CK(CreateTexture, 64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &src.p, nullptr);
        if (!fill(src.p, 0, 4, [](int, int) { return 0xff00ffffu; }) || !makeRt(a) || !levelOf(a.p, 0, s))
            return false;
        RV q[4];
        quadRhw(q, 0, 0, 32, 64, TRED);
        if (!scene([&] {
                if (!target(a.p) || !clearTo(TGRN)) return false;
                CK(UpdateTexture, src.p, a.p);
                return rdraw(q) && backToScreen();
            }))
            return false;
        return readTarget(s.p, 64, 64, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
            return want("(16,32)", texel(b, p, 16, 32), TRED) && want("(48,32)", texel(b, p, 48, 32), 0x00ffff);
        });
    });
}
// A render target at index 1 (DXMTRS C4b-3d4): bound without moving the viewport or the scissor rect, answered by
// GetRenderTarget, unbound by a null (GetRenderTarget is then D3DERR_NOTFOUND with a null surface); index 4 and a
// surface that is no render target are D3DERR_INVALIDCALL.
static bool rowRtMrt() {
    Com<IDirect3DTexture9> a;
    Com<IDirect3DSurface9> s;
    if (!makeRt(a) || !levelOf(a.p, 0, s)) return false;
    const D3DVIEWPORT9 v = {13, 17, 40, 30, 0.0f, 1.0f};
    const RECT sc = {5, 6, 70, 80};
    CK(SetViewport, &v);
    CK(SetScissorRect, &sc);
    HRESULT hr = dev->SetRenderTarget(1, s.p);
    if (hr != D3D_OK) return fail("SetRenderTarget(1)", hr);
    D3DVIEWPORT9 gv = {};
    RECT gs = {};
    IDirect3DSurface9 *got = nullptr;
    bool ok = SUCCEEDED(dev->GetViewport(&gv)) && SUCCEEDED(dev->GetScissorRect(&gs)) && gv.X == 13 && gv.Y == 17 &&
              gv.Width == 40 && gv.Height == 30 && gs.left == 5 && gs.top == 6 && gs.right == 70 && gs.bottom == 80;
    if (!ok) snprintf(why, sizeof why, "the viewport or the scissor rect moved");
    hr = dev->GetRenderTarget(1, &got);
    if (got) got->Release();
    if (ok && (hr != D3D_OK || got != s.p)) ok = fail("GetRenderTarget(1)", hr);
    hr = dev->SetRenderTarget(1, nullptr);
    if (ok && hr != D3D_OK) ok = fail("SetRenderTarget(1, null)", hr);
    got = s.p;
    hr = dev->GetRenderTarget(1, &got);
    if (SUCCEEDED(hr) && got) got->Release();
    if (ok && (hr != D3DERR_NOTFOUND || got)) ok = fail("GetRenderTarget(1) of an empty slot", hr);
    hr = dev->SetRenderTarget(4, s.p);
    if (ok && hr != D3DERR_INVALIDCALL) ok = fail("SetRenderTarget(4)", hr);
    hr = dev->SetRenderTarget(1, sysSurf);
    if (SUCCEEDED(hr)) dev->SetRenderTarget(1, nullptr);
    if (ok && hr != D3DERR_INVALIDCALL) ok = fail("SetRenderTarget(1, a system-memory surface)", hr);
    CK(SetViewport, &FULL);
    return ok;
}
// A multisampled render target resolved by StretchRect (DXMTRS C4b-3d5): with the msaa mode's helpers, below.
static bool rowRtMsaa();
// The default pool's plain surface (DXMTRS C4b-3d5): its description (default pool, usage 0, no multisampling); a
// texel written under LockRect is read back under a second lock; it is no render target: SetRenderTarget(0, it) is
// D3DERR_INVALIDCALL.
static bool rowRtPlain() {
    Com<IDirect3DSurface9> s;
    CK(CreateOffscreenPlainSurface, 64, 64, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &s.p, nullptr);
    D3DSURFACE_DESC d = {};
    HRESULT hr = s.p->GetDesc(&d);
    if (FAILED(hr)) return fail("GetDesc", hr);
    if (d.Width != 64 || d.Height != 64 || d.Format != D3DFMT_A8R8G8B8 || d.Pool != D3DPOOL_DEFAULT || d.Usage != 0 ||
        d.MultiSampleType != D3DMULTISAMPLE_NONE)
        return snprintf(why, sizeof why, "desc %ux%u format %d pool %d usage %lx samples %d", d.Width, d.Height,
                        (int)d.Format, (int)d.Pool, (unsigned long)d.Usage, (int)d.MultiSampleType),
               false;
    const DWORD v = 0x80402010;
    D3DLOCKED_RECT lr = {};
    if (FAILED(hr = s.p->LockRect(&lr, nullptr, 0))) return fail("LockRect", hr);
    memcpy((BYTE *)lr.pBits + 7 * lr.Pitch + 5 * 4, &v, 4);
    if (FAILED(hr = s.p->UnlockRect())) return fail("UnlockRect", hr);
    if (FAILED(hr = s.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return fail("LockRect (the second)", hr);
    const DWORD got = texel((const BYTE *)lr.pBits, (int)lr.Pitch, 5, 7);
    s.p->UnlockRect();
    if (!want("(5,7) locked again", got, v, 0xffffffff)) return false;
    hr = dev->SetRenderTarget(0, s.p);
    if (SUCCEEDED(hr)) dev->SetRenderTarget(0, rtSurf);
    if (hr != D3DERR_INVALIDCALL) return fail("SetRenderTarget(0, a plain surface)", hr);
    return true;
}
// A render-target cube is in the default pool and not dynamic: its LockRect is D3DERR_INVALIDCALL in every lane.
static bool rowRtCubeLock() {
    Com<IDirect3DCubeTexture9> c;
    CK(CreateCubeTexture, 16, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &c.p, nullptr);
    D3DLOCKED_RECT lr = {};
    const HRESULT hr = c.p->LockRect(D3DCUBEMAP_FACE_POSITIVE_X, 0, &lr, nullptr, D3DLOCK_READONLY);
    if (SUCCEEDED(hr)) c.p->UnlockRect(D3DCUBEMAP_FACE_POSITIVE_X, 0);
    if (hr != D3DERR_INVALIDCALL) return fail("cube LockRect", hr);
    return true;
}
// A render target past the device's 16384 limit: D3DERR_INVALIDCALL in every lane, and no surface (Metal raises for
// such a texture, which ends the process, so the create must answer first).
static bool rowRtHuge() {
    IDirect3DSurface9 *s = nullptr;
    const HRESULT hr = dev->CreateRenderTarget(16385, 16, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &s, nullptr);
    if (s) s->Release();
    if (hr != D3DERR_INVALIDCALL) return fail("CreateRenderTarget (16385 wide)", hr);
    return true;
}

// Locks of render targets, of the back buffer and of render-target textures, and the front buffer's read (DXMTRS
// C4c-3a; tex-rtlock is the textures mode's, ms-bblock and ms-front the msaa mode's). Their colours have every channel
// non-zero and no two alike, so a read of zeros (a lock that read nothing back) or of another texel, level or frame
// never passes for the one a cell wants. Every lock is unlocked before what it read is judged.
static const DWORD KSTEEL = D3DCOLOR_XRGB(0x30, 0x60, 0x90), KRUST = D3DCOLOR_XRGB(0xc0, 0x40, 0x20),
                   KAQUA = D3DCOLOR_XRGB(0x20, 0xc0, 0xe0), KMINT = D3DCOLOR_XRGB(0x10, 0xf0, 0x80),
                   KROSE = D3DCOLOR_XRGB(0xf0, 0x20, 0x60), KSAGE = D3DCOLOR_XRGB(0x50, 0xa0, 0x70),
                   KLILAC = D3DCOLOR_XRGB(0x80, 0x70, 0xf0), KGOLD = D3DCOLOR_XRGB(0xe0, 0xd0, 0x10);
// LockRect(r, flags) of `s`, f(bits, pitch) under the lock (it reads into the caller's locals, or writes), then
// UnlockRect whatever f did: the back buffer persists across until()'s attempts, so no path may leave it locked.
template <class F> static bool underLock(IDirect3DSurface9 *s, const char *what, const RECT *r, DWORD flags, F f) {
    D3DLOCKED_RECT lr = {};
    HRESULT hr = s->LockRect(&lr, r, flags);
    if (FAILED(hr)) return fail(what, hr);
    f((BYTE *)lr.pBits, (int)lr.Pitch);
    hr = s->UnlockRect();
    return SUCCEEDED(hr) || fail("UnlockRect", hr);
}
// The same for level `level` of a texture (the whole level).
template <class F> static bool underLevelLock(IDirect3DTexture9 *t, UINT level, DWORD flags, F f) {
    D3DLOCKED_RECT lr = {};
    HRESULT hr = t->LockRect(level, &lr, nullptr, flags);
    if (FAILED(hr)) return fail(level ? "IDirect3DTexture9::LockRect (level 1)" : "IDirect3DTexture9::LockRect", hr);
    f((BYTE *)lr.pBits, (int)lr.Pitch);
    hr = t->UnlockRect(level);
    return SUCCEEDED(hr) || fail("IDirect3DTexture9::UnlockRect", hr);
}
// Texels [x0, x1) x [y0, y1) of 4-byte rows `pitch` apart set to c.
static void paint(BYTE *bits, int pitch, int x0, int y0, int x1, int y1, DWORD c) {
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) memcpy(bits + y * pitch + x * 4, &c, 4);
}
// A 64x64 A8R8G8B8 CreateRenderTarget surface, lockable unless said.
static bool target64(Com<IDirect3DSurface9> &s, BOOL lockable = TRUE) {
    CK(CreateRenderTarget, 64, 64, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, lockable, &s.p, nullptr);
    return true;
}
// rt-lockable: L, a lockable target, cleared steel with a rust triangle (4,4)-(36,4)-(4,36). LockRect(NULL, READONLY)
// reports the pitch 256 and reads the triangle at (12,12), steel at (48,48) (a lock that read nothing back hands out
// its staging's zeros); LockRect(NULL, 0) shows the same, and an aqua block written over (40..55, 8..23) reaches the
// target at UnlockRect (GetRenderTargetData: aqua at (48,16), the triangle and the steel kept). Mint written at (48,48)
// under a READONLY lock does not reach it. A lock of the rect (8,8)-(16,16) hands out (8,8): rose written at pBits[0]
// lands there, over the triangle, and (0,0) stays steel. A Clear to sage, then a READONLY lock: sage. A DISCARD lock
// that writes lilac over the whole surface: lilac everywhere. A second LockRect while locked and a second UnlockRect
// are D3DERR_INVALIDCALL; so is GetRenderTargetData into L from a non-lockable target of its size and format (a
// lockable target is no system-memory destination).
static bool rowRtLockable() {
    const bool first = until([&] {
        Com<IDirect3DSurface9> l, n;
        const RV tri[3] = {{4, 4, 0.5f, 1, KRUST}, {36, 4, 0.5f, 1, KRUST}, {4, 36, 0.5f, 1, KRUST}};
        if (!target64(l) || !scene([&] {
                CK(SetRenderTarget, 0, l.p);
                if (!clearTo(KSTEEL)) return false;
                CK(SetFVF, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
                CK(DrawPrimitiveUP, D3DPT_TRIANGLELIST, 1, tri, sizeof *tri);
                return backToScreen();
            }))
            return false;
        int pitch = 0;
        DWORD in = 0, out = 0;
        if (!underLock(l.p, "LockRect (READONLY)", nullptr, D3DLOCK_READONLY, [&](BYTE *b, int p) {
                pitch = p, in = texel(b, p, 12, 12), out = texel(b, p, 48, 48);
            }))
            return false;
        if (pitch != 256) return snprintf(why, sizeof why, "READONLY lock pitch %d want 256", pitch), false;
        if (!want("READONLY lock (12,12)", in, KRUST) || !want("READONLY lock (48,48)", out, KSTEEL)) return false;
        if (!underLock(l.p, "LockRect", nullptr, 0, [&](BYTE *b, int p) {
                in = texel(b, p, 12, 12), out = texel(b, p, 48, 48);
                paint(b, p, 40, 8, 56, 24, KAQUA);
            }))
            return false;
        if (!want("lock (12,12)", in, KRUST) || !want("lock (48,48)", out, KSTEEL) ||
            !readTarget(l.p, 64, 64, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
                return want("written (48,16)", texel(b, p, 48, 16), KAQUA) &&
                       want("written (12,12)", texel(b, p, 12, 12), KRUST) &&
                       want("written (48,48)", texel(b, p, 48, 48), KSTEEL);
            }))
            return false;
        if (!underLock(l.p, "LockRect (READONLY, written)", nullptr, D3DLOCK_READONLY,
                       [&](BYTE *b, int p) { paint(b, p, 48, 48, 49, 49, KMINT); }) ||
            !readTarget(l.p, 64, 64, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
                return want("after a READONLY lock (48,48)", texel(b, p, 48, 48), KSTEEL);
            }))
            return false;
        const RECT r = {8, 8, 16, 16};
        if (!underLock(l.p, "LockRect (8,8)-(16,16)", &r, 0, [&](BYTE *b, int p) { paint(b, p, 0, 0, 1, 1, KROSE); }) ||
            !readTarget(l.p, 64, 64, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
                return want("rect lock (8,8)", texel(b, p, 8, 8), KROSE) &&
                       want("rect lock (9,8)", texel(b, p, 9, 8), KRUST) &&
                       want("rect lock (0,0)", texel(b, p, 0, 0), KSTEEL);
            }))
            return false;
        DWORD c[3] = {};
        if (!scene([&] {
                CK(SetRenderTarget, 0, l.p);
                return clearTo(KSAGE) && backToScreen();
            }) ||
            !underLock(l.p, "LockRect (READONLY, cleared)", nullptr, D3DLOCK_READONLY, [&](BYTE *b, int p) {
                c[0] = texel(b, p, 12, 12), c[1] = texel(b, p, 48, 16), c[2] = texel(b, p, 8, 8);
            }))
            return false;
        if (!want("cleared (12,12)", c[0], KSAGE) || !want("cleared (48,16)", c[1], KSAGE) ||
            !want("cleared (8,8)", c[2], KSAGE))
            return false;
        if (!underLock(l.p, "LockRect (DISCARD)", nullptr, D3DLOCK_DISCARD,
                       [&](BYTE *b, int p) { paint(b, p, 0, 0, 64, 64, KLILAC); }) ||
            !readTarget(l.p, 64, 64, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
                for (int y = 0; y < 64; y++)
                    for (int x = 0; x < 64; x++) {
                        char what[32];
                        snprintf(what, sizeof what, "DISCARD (%d,%d)", x, y);
                        if (!want(what, texel(b, p, x, y), KLILAC)) return false;
                    }
                return true;
            }))
            return false;
        D3DLOCKED_RECT lr = {}, lr2 = {};
        HRESULT hr = l.p->LockRect(&lr, nullptr, D3DLOCK_READONLY);
        if (FAILED(hr)) return fail("LockRect", hr);
        const HRESULT second = l.p->LockRect(&lr2, nullptr, D3DLOCK_READONLY);
        hr = l.p->UnlockRect();
        const HRESULT again = l.p->UnlockRect();
        if (second != D3DERR_INVALIDCALL) return fail("LockRect (locked)", second);
        if (FAILED(hr)) return fail("UnlockRect", hr);
        if (again != D3DERR_INVALIDCALL) return fail("UnlockRect (unlocked)", again);
        if (!target64(n, FALSE)) return false;
        hr = dev->GetRenderTargetData(n.p, l.p);
        if (hr != D3DERR_INVALIDCALL) return fail("GetRenderTargetData (into a lockable target)", hr);
        return true;
    });
    // On a new L, bound at index 0 throughout: a Clear to steel, an aqua block locked in over (40..55, 8..23), then a
    // gold quad over (8,40)-(24,56): the block, the Clear and the quad all hold (the pass after the upload loads the
    // target; it does not clear it again). On M, in application order: a rust quad over the left half, an aqua block
    // locked in over (16..47, 16..47), a gold quad over the top half: each texel is its last writer's.
    return first && until([&] {
        Com<IDirect3DSurface9> l, m;
        RV gold[4], rust[4], top[4];
        quadRhw(gold, 8, 40, 24, 56, KGOLD), quadRhw(rust, 0, 0, 32, 64, KRUST), quadRhw(top, 0, 0, 64, 32, KGOLD);
        const auto block = [&](IDirect3DSurface9 *s, int x0, int y0, int x1, int y1) {
            return underLock(s, "LockRect (bound)", nullptr, 0,
                             [&](BYTE *b, int p) { paint(b, p, x0, y0, x1, y1, KAQUA); });
        };
        if (!target64(l) || !target64(m) || !scene([&] {
                CK(SetRenderTarget, 0, l.p);
                return clearTo(KSTEEL);
            }) ||
            !block(l.p, 40, 8, 56, 24) || !scene([&] { return rdraw(gold) && backToScreen(); }) ||
            !readTarget(l.p, 64, 64, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
                return want("block (48,16)", texel(b, p, 48, 16), KAQUA) &&
                       want("Clear (48,48)", texel(b, p, 48, 48), KSTEEL) &&
                       want("quad after (16,48)", texel(b, p, 16, 48), KGOLD);
            }))
            return false;
        return scene([&] {
                   CK(SetRenderTarget, 0, m.p);
                   return clearTo(KSTEEL) && rdraw(rust);
               }) &&
               block(m.p, 16, 16, 48, 48) && scene([&] { return rdraw(top) && backToScreen(); }) &&
               readTarget(m.p, 64, 64, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
                   return want("the quad before (8,48)", texel(b, p, 8, 48), KRUST) &&
                          want("the block over it (24,40)", texel(b, p, 24, 40), KAQUA) &&
                          want("the quad after (24,24)", texel(b, p, 24, 24), KGOLD) &&
                          want("the Clear (56,48)", texel(b, p, 56, 48), KSTEEL);
               });
    });
}
// rt-nolock: LockRect is D3DERR_INVALIDCALL in every lane on the surfaces with no CPU copy: a Lockable = FALSE target,
// a 4-sample one, a D24S8 CreateDepthStencilSurface (this device has no auto one), a render-target texture's
// GetSurfaceLevel(0) and a render-target cube's GetCubeMapSurface face. Nothing is unlocked after a refused lock and
// its out structure is not read.
static bool rowRtNolock() {
    Com<IDirect3DSurface9> plain, ms, ds, level, face;
    Com<IDirect3DTexture9> t;
    Com<IDirect3DCubeTexture9> c;
    if (!target64(plain, FALSE)) return false;
    CK(CreateRenderTarget, 64, 64, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_4_SAMPLES, 0, FALSE, &ms.p, nullptr);
    CK(CreateDepthStencilSurface, 64, 64, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &ds.p, nullptr);
    if (!makeRt(t) || !levelOf(t.p, 0, level)) return false;
    CK(CreateCubeTexture, 16, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &c.p, nullptr);
    HRESULT hr = c.p->GetCubeMapSurface(D3DCUBEMAP_FACE_POSITIVE_X, 0, &face.p);
    if (FAILED(hr)) return fail("GetCubeMapSurface", hr);
    const struct { IDirect3DSurface9 *s; const char *what; } cells[] = {
        {plain.p, "LockRect (a non-lockable target)"}, {ms.p, "LockRect (a 4-sample target)"},
        {ds.p, "LockRect (a depth-stencil surface)"}, {level.p, "LockRect (a render-target texture's level)"},
        {face.p, "LockRect (a render-target cube's face)"}};
    for (const auto &k : cells) {
        D3DLOCKED_RECT lr = {};
        hr = k.s->LockRect(&lr, nullptr, D3DLOCK_READONLY);
        if (hr != D3DERR_INVALIDCALL) return fail(k.what, hr);
    }
    return true;
}
// rt-bblock: the back buffer cleared steel, a rust quad over (32,32)-(96,96) and an aqua one over (160,160)-(224,224).
// GetBackBuffer's surface locked READONLY by the one-texel rects at (64,64) and (192,192) reads rust and aqua at pBits
// (the rect's texel; a read from the surface's origin would give the corner's steel); locked READONLY whole, the two
// through the pitch it reports, and steel at (0,0). LockRect(NULL, 0) (the device was made without
// D3DPRESENTFLAG_LOCKABLE_BACKBUFFER), an empty rect, a second lock while locked and a second UnlockRect are
// D3DERR_INVALIDCALL, and a lock after the empty rect's succeeds. No path leaves the back buffer locked.
static bool rowRtBblock() {
    return until([&] {
        Com<IDirect3DSurface9> bb;
        RV a[4], b[4];
        quadRhw(a, 32, 32, 96, 96, KRUST), quadRhw(b, 160, 160, 224, 224, KAQUA);
        if (!scene([&] { return backToScreen() && clearTo(KSTEEL) && rdraw(a) && rdraw(b); })) return false;
        CK(GetBackBuffer, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb.p);
        const RECT ra = {64, 64, 65, 65}, rb = {192, 192, 193, 193}, empty = {10, 10, 10, 11};
        DWORD pa = 0, pb = 0, wa = 0, wb = 0, corner = 0;
        // pBits of a one-texel rect is that texel: read at (0,0) of the mapping, whatever pitch it reports.
        const auto one = [&](const RECT *r, DWORD &got) {
            return underLock(bb.p, "LockRect (a one-texel rect)", r, D3DLOCK_READONLY,
                             [&](BYTE *p, int) { got = texel(p, 0, 0, 0); });
        };
        if (!one(&ra, pa) || !one(&rb, pb) ||
            !underLock(bb.p, "LockRect (NULL, READONLY)", nullptr, D3DLOCK_READONLY, [&](BYTE *p, int pitch) {
                wa = texel(p, pitch, 64, 64), wb = texel(p, pitch, 192, 192), corner = texel(p, pitch, 0, 0);
            }))
            return false;
        if (!want("(64,64) by its rect", pa, KRUST) || !want("(192,192) by its rect", pb, KAQUA) ||
            !want("(64,64) whole", wa, KRUST) || !want("(192,192) whole", wb, KAQUA) ||
            !want("(0,0) whole", corner, KSTEEL))
            return false;
        D3DLOCKED_RECT lr = {};
        HRESULT hr = bb.p->LockRect(&lr, nullptr, 0);
        if (SUCCEEDED(hr)) bb.p->UnlockRect();
        if (hr != D3DERR_INVALIDCALL) return fail("LockRect (NULL, 0)", hr);
        hr = bb.p->LockRect(&lr, &empty, D3DLOCK_READONLY);
        if (SUCCEEDED(hr)) bb.p->UnlockRect();
        if (hr != D3DERR_INVALIDCALL) return fail("LockRect (an empty rect)", hr);
        if (FAILED(hr = bb.p->LockRect(&lr, nullptr, D3DLOCK_READONLY)))
            return fail("LockRect (after the empty rect)", hr);
        const HRESULT second = bb.p->LockRect(&lr, nullptr, D3DLOCK_READONLY);
        hr = bb.p->UnlockRect();
        const HRESULT again = bb.p->UnlockRect();
        if (second != D3DERR_INVALIDCALL) return fail("LockRect (locked)", second);
        if (FAILED(hr)) return fail("UnlockRect", hr);
        if (again != D3DERR_INVALIDCALL) return fail("UnlockRect (unlocked)", again);
        return true;
    });
}
// rt-front: the back buffer cleared steel with a gold quad over (96,96)-(160,160), and no Present: the device's
// GetFrontBufferData(0) into a 256x256 A8R8G8B8 system-memory surface reads it, gold at (128,128) and steel at (16,16)
// (the front buffer is the back buffer as it is now, mtld3d's reading), as the implicit swap chain's own
// GetFrontBufferData does at (128,128). Swap chain 1, no destination and a 512x512 one are D3DERR_INVALIDCALL.
static bool rowRtFront() {
    return until([&] {
        Com<IDirect3DSurface9> d, e, big;
        Com<IDirect3DSwapChain9> sc;
        RV q[4];
        quadRhw(q, 96, 96, 160, 160, KGOLD);
        if (!scene([&] { return backToScreen() && clearTo(KSTEEL) && rdraw(q); })) return false;
        CK(CreateOffscreenPlainSurface, 256, 256, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &d.p, nullptr);
        CK(CreateOffscreenPlainSurface, 256, 256, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &e.p, nullptr);
        CK(CreateOffscreenPlainSurface, 512, 512, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &big.p, nullptr);
        CK(GetSwapChain, 0, &sc.p);
        CK(GetFrontBufferData, 0, d.p);
        HRESULT hr = sc.p->GetFrontBufferData(e.p);
        if (FAILED(hr)) return fail("IDirect3DSwapChain9::GetFrontBufferData", hr);
        DWORD in = 0, out = 0, chain = 0;
        if (!underLock(d.p, "LockRect (the device's read)", nullptr, D3DLOCK_READONLY, [&](BYTE *b, int p) {
                in = texel(b, p, 128, 128), out = texel(b, p, 16, 16);
            }) ||
            !underLock(e.p, "LockRect (the swap chain's read)", nullptr, D3DLOCK_READONLY,
                       [&](BYTE *b, int p) { chain = texel(b, p, 128, 128); }))
            return false;
        if (!want("front (128,128)", in, KGOLD) || !want("front (16,16)", out, KSTEEL) ||
            !want("the swap chain's front (128,128)", chain, KGOLD))
            return false;
        if ((hr = dev->GetFrontBufferData(1, d.p)) != D3DERR_INVALIDCALL)
            return fail("GetFrontBufferData (swap chain 1)", hr);
        if ((hr = dev->GetFrontBufferData(0, nullptr)) != D3DERR_INVALIDCALL)
            return fail("GetFrontBufferData (no destination)", hr);
        if ((hr = dev->GetFrontBufferData(0, big.p)) != D3DERR_INVALIDCALL)
            return fail("GetFrontBufferData (512x512)", hr);
        return true;
    });
}
// tex-rtlock (the textures mode's row): IDirect3DTexture9::LockRect of a 64x64 A8R8G8B8 render-target texture of two
// levels (D3D9 refuses the lock of a non-dynamic default-pool texture; mtld3d takes it, a divergence kept). Level 0
// cleared steel with a rust quad over (8,8)-(24,24), level 1 sage with a gold quad over (0,0)-(16,16): a READONLY lock
// of level 0 reads rust at (16,16) and steel at (48,48) (a lock that read nothing back hands out its staging's zeros),
// one of level 1 gold at (8,8) and sage at (24,24) (no colour of level 0's: a read of another level shows). Level 0
// locked to write an aqua block over (32..47, 32..47), then a rose quad over (40,40)-(56,56) drawn into it:
// GetRenderTargetData of level 0 reads rust at (16,16), aqua at (36,36), rose at (44,44) (the draw after the write
// lands over it) and steel at (8,48).
static bool rowTexRtLock() {
    return until([&] {
        Com<IDirect3DTexture9> t;
        Com<IDirect3DSurface9> s0;
        RV q0[4], q1[4], over[4];
        quadRhw(q0, 8, 8, 24, 24, KRUST), quadRhw(q1, 0, 0, 16, 16, KGOLD), quadRhw(over, 40, 40, 56, 56, KROSE);
        if (!makeRt(t, 64, 2) || !levelOf(t.p, 0, s0) || !scene([&] {
                return target(t.p, 0) && clearTo(KSTEEL) && rdraw(q0) && target(t.p, 1) && clearTo(KSAGE) &&
                       rdraw(q1) && backToScreen();
            }))
            return false;
        DWORD l0[2] = {}, l1[2] = {};
        if (!underLevelLock(t.p, 0, D3DLOCK_READONLY, [&](BYTE *b, int p) {
                l0[0] = texel(b, p, 16, 16), l0[1] = texel(b, p, 48, 48);
            }) ||
            !underLevelLock(t.p, 1, D3DLOCK_READONLY, [&](BYTE *b, int p) {
                l1[0] = texel(b, p, 8, 8), l1[1] = texel(b, p, 24, 24);
            }))
            return false;
        if (!want("level 0 (16,16)", l0[0], KRUST) || !want("level 0 (48,48)", l0[1], KSTEEL) ||
            !want("level 1 (8,8)", l1[0], KGOLD) || !want("level 1 (24,24)", l1[1], KSAGE))
            return false;
        return underLevelLock(t.p, 0, 0, [&](BYTE *b, int p) { paint(b, p, 32, 32, 48, 48, KAQUA); }) &&
               scene([&] { return target(t.p, 0) && rdraw(over) && backToScreen(); }) &&
               readTarget(s0.p, 64, 64, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
                   return want("written (16,16)", texel(b, p, 16, 16), KRUST) &&
                          want("written (36,36)", texel(b, p, 36, 36), KAQUA) &&
                          want("drawn over (44,44)", texel(b, p, 44, 44), KROSE) &&
                          want("written (8,48)", texel(b, p, 8, 48), KSTEEL);
               });
    });
}

static void targetRows(IDirect3DDevice9 *d, bool strict) {
    static const struct { const char *name; bool (*run)(); } rows[] = {
        {"rt-tex", rowRtTex},           {"rt-read", rowRtRead},         {"rt-surface", rowRtSurface},
        {"rt-viewport", rowRtViewport}, {"rt-big", rowRtBig},           {"rt-mip", rowRtMip},
        {"rt-cube", rowRtCube},         {"rt-switch", rowRtSwitch},     {"rt-rewrite", rowRtRewrite},
        {"rt-clearonly", rowRtClearOnly}, {"rt-fresh", rowRtFresh},     {"rt-autogen", rowRtAutogen},
        {"rt-release", rowRtRelease},   {"rt-srgb", rowRtSrgb},         {"rt-ps", rowRtPs},
        {"rt-update", rowRtUpdate},     {"rt-updatebound", rowRtUpdateBound}, {"rt-formats", rowRtFormats},
        {"rt-mrt", rowRtMrt},           {"rt-msaa", rowRtMsaa},         {"rt-plain", rowRtPlain},
        {"rt-cubelock", rowRtCubeLock}, {"rt-huge", rowRtHuge},     {"rt-lockable", rowRtLockable},
        {"rt-nolock", rowRtNolock},     {"rt-bblock", rowRtBblock},     {"rt-front", rowRtFront}};
    dev = d, strictMode = strict, budgetEnd = GetTickCount64() + 180000;
    HRESULT hr = dev->GetRenderTarget(0, &rtSurf);
    if (SUCCEEDED(hr))
        hr = dev->CreateOffscreenPlainSurface(256, 256, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sysSurf, nullptr);
    for (const auto &r : rows) {
        why[0] = 0;
        bool ok;
        if (FAILED(hr)) ok = fail("GetRenderTarget/CreateOffscreenPlainSurface", hr);
        else if (GetTickCount64() >= budgetEnd) ok = false, snprintf(why, sizeof why, "budget");
        else ok = reset() && r.run();
        row(r.name, ok, why);
    }
    if (sysSurf) sysSurf->Release();
    if (rtSurf) rtSurf->Release();
}

// The "depth" mode (DXMTRS C4b-3d2): the depth-stencil, on a device of its own with an auto D24S8 (every other mode's
// device has none). Quads are pretransformed at the depth named. A row clears Z (and stencil where it tests it) inside
// its retried body: the device defaults ZENABLE on, and a never-cleared depth is undefined. Every "not drawn" pixel
// has a drawn control from the same pipeline in the same pass (mtld3d leaves a draw out while its pipeline builds, and
// a pass whose pipeline disagrees with its planes drops its draws without an error). Depth textures a row attaches,
// samples or feeds back are 256x256, the size of the back buffer they are drawn against.
static IDirect3DSurface9 *dsSurf;  // the implicit depth-stencil
static DWORD zenableDefault;       // D3DRS_ZENABLE as the device made it, read before the first reset
static const DWORD TYEL = D3DCOLOR_XRGB(255, 255, 0), TMAG = D3DCOLOR_XRGB(255, 0, 255);
static const D3DFORMAT FMT_INTZ = (D3DFORMAT)MAKEFOURCC('I', 'N', 'T', 'Z'),
                       FMT_DF16 = (D3DFORMAT)MAKEFOURCC('D', 'F', '1', '6'),
                       FMT_DF24 = (D3DFORMAT)MAKEFOURCC('D', 'F', '2', '4');
static const DWORD PS_ODEPTH[] = {
    0xffff0200,                         // ps_2_0
    0x02000001, 0x800f0000, 0xa0e40001, // mov r0, c1
    0x02000001, 0x800f0800, 0xa0e40000, // mov oC0, c0
    0x02000001, 0x900f0800, 0x80000000, // mov oDepth, r0.x
    0x0000ffff};

// The depth plane cleared to z (the stencil plane too, to `stencil`, with clearZS).
static bool clearZ(float z = 1.0f) {
    CK(Clear, 0, nullptr, D3DCLEAR_ZBUFFER, 0xffffffff, z, 0);
    return true;
}
static bool clearZS(float z, DWORD stencil) {
    CK(Clear, 0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0xffffffff, z, stencil);
    return true;
}
static void zq(RV *q, float x0, float y0, float x1, float y1, float z, DWORD c) {
    quadRhw(q, x0, y0, x1, y1, c);
    for (int i = 0; i < 4; i++) q[i].z = z;
}
static bool zquad(float x0, float y0, float x1, float y1, float z, DWORD c) {
    RV q[4];
    zq(q, x0, y0, x1, y1, z, c);
    return rdraw(q);
}
static HRESULT zquadHr(float x0, float y0, float x1, float y1, float z, DWORD c) {
    RV q[4];
    zq(q, x0, y0, x1, y1, z, c);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    return dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof *q);
}
static void recolor(RV *q, DWORD c) {
    for (int i = 0; i < 4; i++) q[i].color = c;
}
static bool bindDs(IDirect3DSurface9 *s) {
    CK(SetDepthStencilSurface, s);
    return true;
}
static bool makeDs(Com<IDirect3DSurface9> &s, D3DFORMAT f = D3DFMT_D24X8, UINT size = 256) {
    CK(CreateDepthStencilSurface, size, size, f, D3DMULTISAMPLE_NONE, 0, FALSE, &s.p, nullptr);
    return true;
}
static bool makeDepthTex(Com<IDirect3DTexture9> &t, D3DFORMAT f, UINT levels = 1) {
    CK(CreateTexture, 256, 256, levels, D3DUSAGE_DEPTHSTENCIL, f, D3DPOOL_DEFAULT, &t.p, nullptr);
    return true;
}
static bool bindDsLevel(IDirect3DTexture9 *t, UINT level = 0) {
    Com<IDirect3DSurface9> s;
    return levelOf(t, level, s) && bindDs(s.p);
}
// The depth and stencil state at D3D9's defaults (ZENABLE on, as this device has it), fetch4 unlatched at stage 0 and
// the implicit depth-stencil bound: what reset() leaves alone.
static bool depthReset() {
    static const DWORD rs[][2] = {
        {D3DRS_ZENABLE, D3DZB_TRUE}, {D3DRS_ZWRITEENABLE, TRUE}, {D3DRS_ZFUNC, D3DCMP_LESSEQUAL},
        {D3DRS_STENCILENABLE, FALSE}, {D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP}, {D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP},
        {D3DRS_STENCILPASS, D3DSTENCILOP_KEEP}, {D3DRS_STENCILFUNC, D3DCMP_ALWAYS}, {D3DRS_STENCILREF, 0},
        {D3DRS_STENCILMASK, 0xffffffff}, {D3DRS_STENCILWRITEMASK, 0xffffffff}, {D3DRS_TWOSIDEDSTENCILMODE, FALSE},
        {D3DRS_CCW_STENCILFAIL, D3DSTENCILOP_KEEP}, {D3DRS_CCW_STENCILZFAIL, D3DSTENCILOP_KEEP},
        {D3DRS_CCW_STENCILPASS, D3DSTENCILOP_KEEP}, {D3DRS_CCW_STENCILFUNC, D3DCMP_ALWAYS}, {D3DRS_DEPTHBIAS, 0},
        {D3DRS_SLOPESCALEDEPTHBIAS, 0}};
    for (const auto &s : rs) CK(SetRenderState, (D3DRENDERSTATETYPE)s[0], s[1]);
    // A numeric bias keeps a fetch4 latch: GET1 clears it.
    CK(SetSamplerState, 0, D3DSAMP_MIPMAPLODBIAS, MAKEFOURCC('G', 'E', 'T', '1'));
    CK(SetSamplerState, 0, D3DSAMP_MIPMAPLODBIAS, 0);
    return bindDs(dsSurf);
}

// The auto depth-stencil: its description, and ZENABLE on by default with it.
static bool rowDsDefault() {
    Com<IDirect3DSurface9> ds;
    CK(GetDepthStencilSurface, &ds.p);
    D3DSURFACE_DESC d = {};
    const HRESULT hr = ds.p->GetDesc(&d);
    if (FAILED(hr)) return fail("GetDesc", hr);
    if (d.Width != 256 || d.Height != 256 || d.Format != D3DFMT_D24S8 || d.Usage != D3DUSAGE_DEPTHSTENCIL)
        return snprintf(why, sizeof why, "desc %ux%u format %d usage %lx", d.Width, d.Height, (int)d.Format,
                        (unsigned long)d.Usage),
               false;
    if (zenableDefault != D3DZB_TRUE)
        return snprintf(why, sizeof why, "ZENABLE defaults to %lu", (unsigned long)zenableDefault), false;
    return true;
}
// The depth test: red at 0.25 over x 32..160, then green at 0.75 over x 96..224, which the red hides where they
// overlap.
static const Px DS_TEST[] = {{64, 128, TRED}, {128, 128, TRED}, {192, 128, TGRN}, {128, 32, BLUE}};
static bool dsTestBody() {
    return clearZ() && zquad(32, 96, 160, 160, 0.25f, TRED) && zquad(96, 96, 224, 160, 0.75f, TGRN);
}
static bool rowDsTest() { return frames(BLUE, DS_TEST, 4, dsTestBody); }
// ZFUNC against a depth of 0.5, the functions changing within one pass: NEVER, LESS (nearer, then farther), GREATER,
// EQUAL (against the depth the same vertices wrote), ALWAYS.
static bool rowDsFunc() {
    const Px px[] = {{32, 128, BLUE}, {72, 128, TGRN}, {112, 128, BLUE}, {152, 128, TGRN}, {192, 128, TGRN},
                     {232, 128, TMAG}};
    return frames(BLUE, px, 6, [&] {
        auto with = [&](DWORD func, float x, float z, DWORD c) {
            return rstate(D3DRS_ZFUNC, func) && zquad(x, 96, x + 32, 160, z, c);
        };
        RV e[4];
        zq(e, 176, 96, 208, 160, 0.3f, TYEL);
        if (!clearZ(0.5f) || !with(D3DCMP_NEVER, 16, 0.25f, TRED) || !with(D3DCMP_LESS, 56, 0.25f, TGRN) ||
            !with(D3DCMP_LESS, 96, 0.75f, TRED) || !with(D3DCMP_GREATER, 136, 0.75f, TGRN))
            return false;
        if (!rstate(D3DRS_ZFUNC, D3DCMP_ALWAYS) || !rdraw(e)) return false;
        recolor(e, TGRN);
        return rstate(D3DRS_ZFUNC, D3DCMP_EQUAL) && rdraw(e) && with(D3DCMP_ALWAYS, 216, 0.75f, TMAG);
    });
}
// ZWRITEENABLE off: the near quad writes no depth, so the far one drawn after it shows over it.
static bool rowDsWrite() {
    const Px px[] = {{128, 128, TGRN}, {72, 128, TRED}};
    return frames(BLUE, px, 2, [&] {
        return clearZ() && rstate(D3DRS_ZWRITEENABLE, FALSE) && zquad(64, 64, 192, 192, 0.25f, TRED) &&
               rstate(D3DRS_ZWRITEENABLE, TRUE) && zquad(96, 96, 160, 160, 0.75f, TGRN);
    });
}
// ZENABLE off within a pass: the draw behind a tested one shows and writes no depth (a tested draw at 0.5 after it is
// hidden by the first quad's 0.25 where it would pass against the untested quad's 0.75).
static bool rowDsZenable() {
    const Px px[] = {{128, 128, TGRN}, {176, 128, TRED}, {216, 128, TYEL}, {72, 128, TRED}};
    return frames(BLUE, px, 4, [&] {
        return clearZ() && zquad(64, 64, 192, 192, 0.25f, TRED) && rstate(D3DRS_ZENABLE, D3DZB_FALSE) &&
               zquad(96, 96, 160, 160, 0.75f, TGRN) && rstate(D3DRS_ZENABLE, D3DZB_TRUE) &&
               zquad(112, 120, 240, 136, 0.5f, TYEL);
    });
}
// Clear's z: against 0.5 a quad at 0.25 draws and one at 0.75 does not.
static bool rowDsClearZ() {
    const Px px[] = {{76, 128, TGRN}, {180, 128, BLUE}};
    return frames(BLUE, px, 2, [&] {
        return clearZ(0.5f) && zquad(32, 96, 120, 160, 0.25f, TGRN) && zquad(136, 96, 224, 160, 0.75f, TRED);
    });
}
// A Clear with no draw after it before the surface is unbound: A cleared to 0.25, B bound, cleared and drawn against,
// A bound again with no clear: it still holds 0.25.
static bool rowDsClearOnly() {
    const Px px[] = {{76, 128, TGRN}, {180, 128, BLUE}, {32, 32, TRED}};
    return frames(BLUE, px, 3, [&] {
        Com<IDirect3DSurface9> a, b;
        return makeDs(a) && makeDs(b) && bindDs(a.p) && clearZ(0.25f) && bindDs(b.p) && clearZ() &&
               zquad(16, 16, 48, 48, 0.5f, TRED) && bindDs(a.p) && zquad(32, 96, 120, 160, 0.2f, TGRN) &&
               zquad(136, 96, 224, 160, 0.5f, TRED) && bindDs(dsSurf);
    });
}
// Stencil 1 everywhere, then in one pass: EQUAL with reference 1, 2 (not drawn) and 0x101 (its low byte); REPLACE then
// EQUAL; INCR then EQUAL; the read mask; the write mask (bit 0 kept under a REPLACE of 0); the reference back to 0.
static bool rowDsStencil() {
    const Px px[] = {{28, 128, TGRN}, {60, 128, BLUE}, {92, 128, TGRN}, {124, 128, TGRN},
                     {156, 128, TGRN}, {188, 128, TGRN}, {220, 128, TGRN}, {244, 128, TGRN}};
    return frames(BLUE, px, 8, [&] {
        auto eq = [&](DWORD ref, float x, float z = 0.5f, DWORD c = TGRN) {
            return rstate(D3DRS_STENCILFUNC, D3DCMP_EQUAL) && rstate(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP) &&
                   rstate(D3DRS_STENCILREF, ref) && zquad(x, 96, x + 24, 160, z, c);
        };
        auto write = [&](DWORD op, DWORD ref, float x) {
            return rstate(D3DRS_STENCILFUNC, D3DCMP_ALWAYS) && rstate(D3DRS_STENCILPASS, op) &&
                   rstate(D3DRS_STENCILREF, ref) && zquad(x, 96, x + 24, 160, 0.5f, TYEL);
        };
        if (!clearZS(1.0f, 1) || !rstate(D3DRS_STENCILENABLE, TRUE)) return false;
        if (!eq(1, 16) || !eq(2, 48, 0.5f, TRED) || !eq(0x101, 80)) return false;
        if (!write(D3DSTENCILOP_REPLACE, 5, 112) || !eq(5, 112, 0.25f)) return false;
        if (!write(D3DSTENCILOP_INCR, 0, 144) || !eq(2, 144, 0.25f)) return false;
        if (!rstate(D3DRS_STENCILMASK, 0x0f) || !eq(0x11, 176) || !rstate(D3DRS_STENCILMASK, 0xffffffff))
            return false;
        if (!rstate(D3DRS_STENCILWRITEMASK, 0xfe) || !write(D3DSTENCILOP_REPLACE, 0, 208) ||
            !rstate(D3DRS_STENCILWRITEMASK, 0xffffffff) || !eq(1, 208, 0.25f))
            return false;
        return rstate(D3DRS_STENCILFUNC, D3DCMP_NOTEQUAL) && rstate(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP) &&
               rstate(D3DRS_STENCILREF, 0) && zquad(236, 96, 252, 160, 0.5f, TGRN);
    });
}
// Two-sided stencil over a stencil of 4: a clockwise quad increments (5), a counter-clockwise one decrements (3).
static bool rowDsTwoSided() {
    const Px px[] = {{76, 128, TGRN}, {180, 128, TGRN}};
    return frames(BLUE, px, 2, [&] {
        RV cw[4], ccw[4];
        zq(cw, 32, 96, 120, 160, 0.5f, TYEL);
        zq(ccw, 136, 96, 224, 160, 0.5f, TYEL);
        const RV second = ccw[1];
        ccw[1] = ccw[2], ccw[2] = second;
        if (!clearZS(1.0f, 4) || !rstate(D3DRS_CULLMODE, D3DCULL_NONE) || !rstate(D3DRS_STENCILENABLE, TRUE) ||
            !rstate(D3DRS_TWOSIDEDSTENCILMODE, TRUE) || !rstate(D3DRS_STENCILFUNC, D3DCMP_ALWAYS) ||
            !rstate(D3DRS_CCW_STENCILFUNC, D3DCMP_ALWAYS) || !rstate(D3DRS_STENCILPASS, D3DSTENCILOP_INCR) ||
            !rstate(D3DRS_CCW_STENCILPASS, D3DSTENCILOP_DECR) || !rdraw(cw) || !rdraw(ccw))
            return false;
        return rstate(D3DRS_TWOSIDEDSTENCILMODE, FALSE) && rstate(D3DRS_STENCILFUNC, D3DCMP_EQUAL) &&
               rstate(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP) && rstate(D3DRS_STENCILREF, 5) &&
               zquad(32, 96, 120, 160, 0.25f, TGRN) && rstate(D3DRS_STENCILREF, 3) &&
               zquad(136, 96, 224, 160, 0.25f, TGRN);
    });
}
// DEPTHBIAS under LESS: the same vertices drawn again fail without a bias and pass with -0.01; the same through a
// programmable vertex shader (clip-space quads over the lower half).
static bool rowDsBias() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_C0)) return false;
    const Px px[] = {{76, 96, TRED}, {180, 96, TGRN}, {64, 192, TRED}, {192, 192, TGRN}};
    return frames(BLUE, px, 4, [&] {
        const DWORD bias = fbits(-0.01f);
        RV l[4], r[4];
        zq(l, 32, 64, 120, 128, 0.5f, TRED), zq(r, 136, 64, 224, 128, 0.5f, TRED);
        if (!clearZ() || !rstate(D3DRS_ZFUNC, D3DCMP_LESS) || !rdraw(l) || !rdraw(r)) return false;
        recolor(l, TGRN), recolor(r, TGRN);
        if (!rdraw(l) || !rstate(D3DRS_DEPTHBIAS, bias) || !rdraw(r) || !rstate(D3DRS_DEPTHBIAS, 0)) return false;
        V3 pl[4], pr[4];
        quad(pl, -0.75f, -0.75f, -0.25f, -0.25f), quad(pr, 0.25f, -0.75f, 0.75f, -0.25f);
        if (!use(p)) return false;
        CK(SetPixelShaderConstantF, 0, RED, 1);
        if (!strip(pl) || !strip(pr)) return false;
        CK(SetPixelShaderConstantF, 0, GREEN, 1);
        if (!strip(pl) || !rstate(D3DRS_DEPTHBIAS, bias) || !strip(pr) || !rstate(D3DRS_DEPTHBIAS, 0)) return false;
        CK(SetVertexShader, nullptr);
        CK(SetPixelShader, nullptr);
        return true;
    });
}
// SLOPESCALEDEPTHBIAS under LESS: a quad slanted in z (0.2 to 0.8 across it) drawn again passes only with a slope
// term of -1.
static bool rowDsSlope() {
    const Px px[] = {{128, 92, TRED}, {128, 164, TGRN}};
    return frames(BLUE, px, 2, [&] {
        RV t[4], b[4];
        quadRhw(t, 64, 64, 192, 120, TRED), quadRhw(b, 64, 136, 192, 192, TRED);
        for (RV *q : {t, b}) q[0].z = q[2].z = 0.2f, q[1].z = q[3].z = 0.8f;
        if (!clearZ() || !rstate(D3DRS_ZFUNC, D3DCMP_LESS) || !rdraw(t) || !rdraw(b)) return false;
        recolor(t, TGRN), recolor(b, TGRN);
        return rdraw(t) && rstate(D3DRS_SLOPESCALEDEPTHBIAS, fbits(-1.0f)) && rdraw(b) &&
               rstate(D3DRS_SLOPESCALEDEPTHBIAS, 0);
    });
}
// A pretransformed quad at z 1.5: drawn (its depth clamped) with ZENABLE off, clipped with the depth test live.
static bool rowDsClamp() {
    const Px px[] = {{76, 128, TGRN}, {180, 128, BLUE}, {128, 32, TYEL}};
    return frames(BLUE, px, 3, [&] {
        return clearZ() && rstate(D3DRS_ZENABLE, D3DZB_FALSE) && zquad(32, 96, 120, 160, 1.5f, TGRN) &&
               rstate(D3DRS_ZENABLE, D3DZB_TRUE) && zquad(136, 96, 224, 160, 1.5f, TRED) &&
               zquad(96, 16, 160, 48, 0.5f, TYEL);
    });
}
// A pixel shader's depth output: a quad at 0.75 writing oDepth 0.25 hides a quad at 0.5 drawn half over it.
static bool rowDsODepth() {
    Com<IDirect3DPixelShader9> ps;
    CK(CreatePixelShader, PS_ODEPTH, &ps.p);
    const Px px[] = {{96, 128, TRED}, {160, 128, TRED}, {216, 128, TGRN}};
    return frames(BLUE, px, 3, [&] {
        static const float depth[4] = {0.25f, 0, 0, 0};
        if (!clearZ()) return false;
        CK(SetPixelShader, ps.p);
        CK(SetPixelShaderConstantF, 0, RED, 1);
        CK(SetPixelShaderConstantF, 1, depth, 1);
        if (!zquad(64, 96, 192, 160, 0.75f, TRED)) return false;
        CK(SetPixelShader, nullptr);
        return zquad(128, 96, 240, 160, 0.5f, TGRN);
    });
}
// CreateDepthStencilSurface in the depth-only formats: the description, GetDepthStencilSurface, the depth test.
static bool rowDsSurface() {
    for (D3DFORMAT f : {D3DFMT_D24X8, D3DFMT_D16}) {
        Com<IDirect3DSurface9> s, got;
        if (!makeDs(s, f)) return false;
        D3DSURFACE_DESC d = {};
        const HRESULT hr = s.p->GetDesc(&d);
        if (FAILED(hr)) return fail("GetDesc", hr);
        if (d.Width != 256 || d.Height != 256 || d.Format != f || d.Usage != D3DUSAGE_DEPTHSTENCIL ||
            d.Pool != D3DPOOL_DEFAULT)
            return snprintf(why, sizeof why, "desc %ux%u format %d usage %lx pool %d", d.Width, d.Height, (int)d.Format,
                            (unsigned long)d.Usage, (int)d.Pool),
                   false;
        if (!bindDs(s.p)) return false;
        CK(GetDepthStencilSurface, &got.p);
        if (got.p != s.p) return snprintf(why, sizeof why, "GetDepthStencilSurface answers another surface"), false;
        if (!frames(BLUE, DS_TEST, 4, dsTestBody)) return false;
    }
    return bindDs(dsSurf);
}
// A format with no stencil plane: STENCILENABLE with NEVER still draws; a Clear of Z and stencil clears Z; a Clear of
// stencil alone is D3D_OK and touches neither the colour nor Z.
static bool rowDsNoStencil() {
    Com<IDirect3DSurface9> s;
    if (!makeDs(s) || !bindDs(s.p)) return false;
    const Px px[] = {{128, 48, TGRN}, {76, 128, TGRN}, {180, 128, BLUE}};
    const bool ok = frames(BLUE, px, 3, [&] {
        if (!clearZ() || !rstate(D3DRS_STENCILENABLE, TRUE) || !rstate(D3DRS_STENCILFUNC, D3DCMP_NEVER) ||
            !zquad(96, 32, 160, 64, 0.5f, TGRN) || !rstate(D3DRS_STENCILENABLE, FALSE) || !clearZS(0.5f, 1))
            return false;
        CK(Clear, 0, nullptr, D3DCLEAR_STENCIL, 0xffffffff, 0.0f, 1);
        return zquad(32, 96, 120, 160, 0.4f, TGRN) && zquad(136, 96, 224, 160, 0.6f, TRED);
    });
    CK(SetDepthStencilSurface, dsSurf);
    return ok;
}
// No depth-stencil bound: a Z Clear is D3DERR_INVALIDCALL, Get answers D3DERR_NOTFOUND, draws are untested (the far
// quad shows over the near one) and a pretransformed quad at z 1.5 is clamped and drawn; the implicit one bound again
// tests again.
static bool rowDsUnbound() {
    CK(SetDepthStencilSurface, nullptr);
    HRESULT hr = dev->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    if (hr != D3DERR_INVALIDCALL) return fail("Clear (no depth-stencil)", hr);
    IDirect3DSurface9 *none = nullptr;
    hr = dev->GetDepthStencilSurface(&none);
    if (none) none->Release();
    if (hr != D3DERR_NOTFOUND) return fail("GetDepthStencilSurface (none bound)", hr);
    const Px px[] = {{128, 128, TGRN}, {64, 128, TRED}, {128, 32, TYEL}};
    if (!frames(BLUE, px, 3, [&] {
            return zquad(32, 96, 160, 160, 0.25f, TRED) && zquad(96, 96, 224, 160, 0.75f, TGRN) &&
                   zquad(96, 16, 160, 48, 1.5f, TYEL);
        }))
        return false;
    CK(SetDepthStencilSurface, dsSurf);
    return frames(BLUE, DS_TEST, 4, dsTestBody);
}
// The depth-stencil stays bound across SetRenderTarget: depth written with the back buffer bound hides a quad drawn
// behind it on a 256x256 render-target texture, which draws beside it.
static bool rowDsPersist() {
    return until([&] {
        Com<IDirect3DTexture9> t;
        Com<IDirect3DSurface9> s;
        if (!makeRt(t, 256) || !levelOf(t.p, 0, s)) return false;
        if (!scene([&] {
                CK(Clear, 0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, BLUE, 1.0f, 0);
                return zquad(64, 64, 192, 192, 0.25f, TRED) && target(t.p) && clearTo(BLUE) &&
                       zquad(128, 96, 240, 160, 0.5f, TGRN) && backToScreen();
            }))
            return false;
        return readTarget(s.p, 256, 256, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
            return want("(160,128)", texel(b, p, 160, 128), BLUE) && want("(216,128)", texel(b, p, 216, 128), TGRN);
        });
    });
}
// Level 0 of a D24S8 depth texture as the depth-stencil.
static bool rowDsTexture() {
    Com<IDirect3DTexture9> t;
    if (!makeDepthTex(t, D3DFMT_D24S8) || !bindDsLevel(t.p)) return false;
    const bool ok = frames(BLUE, DS_TEST, 4, dsTestBody);
    CK(SetDepthStencilSurface, dsSurf);
    return ok;
}
// A stencil Clear on a texture's level (D24S8): a quad writes stencil 1; Clear(STENCIL) alone, to 0; a quad under EQUAL
// 1 is then hidden and one under EQUAL 0 shows; Clear(ZBUFFER | STENCIL) to 3, and a quad under EQUAL 3 shows.
static bool rowDsTexStencil() {
    Com<IDirect3DTexture9> t;
    if (!makeDepthTex(t, D3DFMT_D24S8) || !bindDsLevel(t.p)) return false;
    const Px px[] = {{64, 128, TYEL}, {128, 128, TGRN}, {192, 128, TGRN}};
    const bool ok = frames(BLUE, px, 3, [&] {
        auto eq = [&](DWORD ref, float x, DWORD c) {
            return rstate(D3DRS_STENCILFUNC, D3DCMP_EQUAL) && rstate(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP) &&
                   rstate(D3DRS_STENCILREF, ref) && zquad(x, 96, x + 48, 160, 0.25f, c);
        };
        if (!clearZ() || !rstate(D3DRS_STENCILENABLE, TRUE) || !rstate(D3DRS_STENCILFUNC, D3DCMP_ALWAYS) ||
            !rstate(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE) || !rstate(D3DRS_STENCILREF, 1) ||
            !zquad(16, 96, 240, 160, 0.5f, TYEL))
            return false;
        CK(Clear, 0, nullptr, D3DCLEAR_STENCIL, 0, 1.0f, 0);
        if (!eq(1, 40, TRED) || !eq(0, 104, TGRN)) return false;
        return clearZS(1.0f, 3) && eq(3, 168, TGRN);
    });
    CK(SetDepthStencilSurface, dsSurf);
    return ok;
}
// A level below 0: level 0 cleared to 0.25 with the back buffer; level 1 with a 128x128 target cleared to 1 and the
// depth test drawn there, its corner (8..24) untouched; level 0 again with the back buffer and no clear: a quad at 0.4
// over that corner is hidden (level 0 still holds 0.25, not level 1's clear) and one at 0.2 beside it shows.
static bool rowDsMip() {
    return until([&] {
        Com<IDirect3DTexture9> d, c;
        Com<IDirect3DSurface9> d0, d1, cs;
        CK(CreateTexture, 256, 256, 2, D3DUSAGE_DEPTHSTENCIL, D3DFMT_D24X8, D3DPOOL_DEFAULT, &d.p, nullptr);
        if (!levelOf(d.p, 0, d0) || !levelOf(d.p, 1, d1) || !makeRt(c, 128) || !levelOf(c.p, 0, cs)) return false;
        const bool drawn = scene([&] {
            if (!bindDs(d0.p)) return false;
            CK(Clear, 0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, BLUE, 0.25f, 0);
            if (!target(c.p) || !bindDs(d1.p)) return false;
            CK(Clear, 0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, BLUE, 1.0f, 0);
            return zquad(16, 48, 80, 80, 0.25f, TRED) && zquad(48, 48, 112, 80, 0.75f, TGRN) && backToScreen() &&
                   bindDs(d0.p) && zquad(8, 8, 24, 24, 0.4f, TYEL) && zquad(40, 8, 56, 24, 0.2f, TGRN);
        });
        dev->SetRenderTarget(0, rtSurf);
        dev->SetDepthStencilSurface(dsSurf);
        if (!drawn) return false;
        return readTarget(cs.p, 128, 128, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
                   return want("level 1 (32,64)", texel(b, p, 32, 64), TRED) &&
                          want("level 1 (64,64)", texel(b, p, 64, 64), TRED) &&
                          want("level 1 (96,64)", texel(b, p, 96, 64), TGRN);
               }) &&
               readTarget(rtSurf, 256, 256, D3DFMT_X8R8G8B8, [&](const BYTE *b, int p) {
                   return want("level 0 (16,16)", texel(b, p, 16, 16), BLUE) &&
                          want("level 0 (48,16)", texel(b, p, 48, 16), TGRN);
               });
    });
}
// A depth-stencil surface the application released while it is bound still tests.
static bool rowDsRelease() {
    IDirect3DSurface9 *s = nullptr;
    CK(CreateDepthStencilSurface, 256, 256, D3DFMT_D24X8, D3DMULTISAMPLE_NONE, 0, FALSE, &s, nullptr);
    const HRESULT hr = dev->SetDepthStencilSurface(s);
    s->Release();
    if (FAILED(hr)) return fail("SetDepthStencilSurface", hr);
    const bool ok = frames(BLUE, DS_TEST, 4, dsTestBody);
    CK(SetDepthStencilSurface, dsSurf);
    return ok;
}
// Depth 0.25 over the left half and 0.75 over the right written into level 0 of `t`, then the implicit depth-stencil
// back and the frame cleared.
static bool writeHalves(IDirect3DTexture9 *t) {
    if (!bindDsLevel(t) || !clearZ() || !zquad(0, 0, 128, 256, 0.25f, TRED) ||
        !zquad(128, 0, 256, 256, 0.75f, TRED) || !bindDs(dsSurf))
        return false;
    CK(Clear, 0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, BLUE, 1.0f, 0);
    return true;
}
// `t` sampled by the fixed-function stage at (u, 0.5) with a third coordinate (a shadow sample's reference) of 0.5,
// over the 64x64 quad at (x, y).
static bool sampleAt(IDirect3DBaseTexture9 *t, float x, float y, float u) {
    TV3 q[4];
    tquad(q, x, y, x + 64, y + 64, u, 0.5f, 0.5f);
    CK(SetTexture, 0, t);
    const bool ok = tdraw(q);
    CK(SetTexture, 0, nullptr);
    return ok;
}
// A shadow sample of a D24X8 texture: the reference 0.5 is lit against the right half's 0.75 and not against the
// left half's 0.25.
static bool rowDsShadow() {
    const Px px[] = {{64, 128, 0x000000}, {192, 128, 0xffffff}};
    return frames(BLUE, px, 2, [&] {
        Com<IDirect3DTexture9> t;
        return makeDepthTex(t, D3DFMT_D24X8) && writeHalves(t.p) && sampleAt(t.p, 32, 96, 64.5f / 256) &&
               sampleAt(t.p, 160, 96, 192.5f / 256);
    });
}
// INTZ sampled raw: each half's depth as grey; with LINEAR filters a quarter texel into the right half, still that
// texel's own depth (a raw depth sample is a point sample).
static bool rowDsIntz() {
    const Px px[] = {{64, 128, 0x404040}, {192, 128, 0xbfbfbf}, {128, 48, 0xbfbfbf}};
    return frames(BLUE, px, 3, [&] {
        Com<IDirect3DTexture9> t;
        if (!makeDepthTex(t, FMT_INTZ) || !writeHalves(t.p) || !sampleAt(t.p, 32, 96, 64.5f / 256) ||
            !sampleAt(t.p, 160, 96, 192.5f / 256))
            return false;
        CK(SetSamplerState, 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        CK(SetSamplerState, 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        const bool ok = sampleAt(t.p, 96, 16, 128.25f / 256);
        CK(SetSamplerState, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        CK(SetSamplerState, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        return ok;
    });
}
// DF16 and DF24 sampled raw: the depth in red, green and blue 0.
static bool rowDsDf() {
    const Px px[] = {{64, 128, 0x400000}, {192, 128, 0xbf0000}};
    for (D3DFORMAT f : {FMT_DF16, FMT_DF24})
        if (!frames(BLUE, px, 2, [&] {
                Com<IDirect3DTexture9> t;
                return makeDepthTex(t, f) && writeHalves(t.p) && sampleAt(t.p, 32, 96, 64.5f / 256) &&
                       sampleAt(t.p, 160, 96, 192.5f / 256);
            }))
            return false;
    return true;
}
// fetch4 on a DF24 texture cleared to 0.5 and never drawn to: the four texels' depth, one a channel.
static bool rowDsFetch4() {
    const Px px[] = {{128, 128, 0x808080}};
    const bool ok = frames(BLUE, px, 1, [&] {
        Com<IDirect3DTexture9> t;
        if (!makeDepthTex(t, FMT_DF24) || !bindDsLevel(t.p) || !clearZ(0.5f) || !bindDs(dsSurf)) return false;
        CK(Clear, 0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, BLUE, 1.0f, 0);
        CK(SetSamplerState, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        CK(SetSamplerState, 0, D3DSAMP_MIPMAPLODBIAS, MAKEFOURCC('G', 'E', 'T', '4'));
        return sampleAt(t.p, 96, 96, 64.5f / 256);
    });
    CK(SetSamplerState, 0, D3DSAMP_MIPMAPLODBIAS, MAKEFOURCC('G', 'E', 'T', '1'));
    CK(SetSamplerState, 0, D3DSAMP_MIPMAPLODBIAS, 0);
    return ok;
}
// A depth-stencil larger than the target (512x512 with the 256x256 back buffer), which D3D9 allows: a Z Clear and
// depth-tested draws as with one of the target's size (a near quad hides a far one drawn after it).
static bool rowDsSize() {
    const Px px[] = {{128, 128, TGRN}, {48, 128, TRED}, {128, 80, TGRN}, {208, 128, TRED}};
    Com<IDirect3DSurface9> big;
    if (!makeDs(big, D3DFMT_D24X8, 512)) return false;
    const bool ok = frames(BLUE, px, 4, [&] {
        return bindDs(big.p) && clearZ() && zquad(96, 64, 160, 192, 0.3f, TGRN) && zquad(32, 96, 224, 160, 0.7f, TRED) &&
               bindDs(dsSurf);
    });
    CK(SetDepthStencilSurface, dsSurf);
    return ok;
}
// The bound depth-stencil (INTZ, cleared to 0.5) sampled by the draw. With Z and stencil off the draw reads it in
// every lane. With Z on it is past the core's slice (C4c's copy): D3DERR_NOTAVAILABLE under core (strict); D3D_OK
// elsewhere, the result not asserted.
static bool rowDsFeedback() {
    Px px[] = {{64, 128, 0x808080}, {192, 128, BLUE}};
    px[1].mask = strictMode ? 0xffffff : 0;
    const bool ok = frames(BLUE, px, 2, [&] {
        Com<IDirect3DTexture9> t;
        TV3 a[4], b[4];
        tquad(a, 32, 96, 96, 160, 64.5f / 256, 0.5f, 0.5f), tquad(b, 160, 96, 224, 160, 192.5f / 256, 0.5f, 0.5f);
        if (!makeDepthTex(t, FMT_INTZ) || !bindDsLevel(t.p) || !clearZ(0.5f)) return false;
        CK(SetTexture, 0, t.p);
        if (!rstate(D3DRS_ZENABLE, D3DZB_FALSE) || !tdraw(a) || !rstate(D3DRS_ZENABLE, D3DZB_TRUE)) return false;
        const HRESULT hr = dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, b, sizeof *b);
        CK(SetTexture, 0, nullptr);
        if (strictMode ? hr != D3DERR_NOTAVAILABLE : hr != D3D_OK) return fail("DrawPrimitiveUP (Z on)", hr);
        return bindDs(dsSurf);
    });
    CK(SetTexture, 0, nullptr);
    CK(SetDepthStencilSurface, dsSurf);
    return ok;
}
// A multisampled depth-stencil surface (DXMTRS C4b-3d5): a 4-sample 256x256 D24S8 surface and its description; bound
// with the single-sample back buffer, ZENABLE on and Z cleared to 1, a near red quad (x 32..160) then a far green one
// (x 96..224) over part of it: the green one shows where they overlap (a depth-stencil of another sample count than the
// target's is left out of the pass, as mtld3d leaves it out), the red beside it, every call D3D_OK.
static bool rowDsMsaa() {
    const Px px[] = {{128, 128, TGRN}, {64, 128, TRED}, {32, 32, BLUE}};
    const bool ok = frames(BLUE, px, 3, [&] {
        Com<IDirect3DSurface9> d;
        CK(CreateDepthStencilSurface, 256, 256, D3DFMT_D24S8, D3DMULTISAMPLE_4_SAMPLES, 0, FALSE, &d.p, nullptr);
        D3DSURFACE_DESC desc = {};
        const HRESULT hr = d.p->GetDesc(&desc);
        if (FAILED(hr)) return fail("GetDesc", hr);
        if (desc.MultiSampleType != D3DMULTISAMPLE_4_SAMPLES)
            return snprintf(why, sizeof why, "desc %d samples", (int)desc.MultiSampleType), false;
        return bindDs(d.p) && clearZ() && zquad(32, 64, 160, 192, 0.2f, TRED) && zquad(96, 64, 224, 192, 0.8f, TGRN) &&
               bindDs(dsSurf);
    });
    CK(SetDepthStencilSurface, dsSurf);
    return ok;
}
// CreateDepthStencilSurface past the device's 16384 limit, of no width and in a colour format: D3DERR_INVALIDCALL in
// every lane, and no surface.
static bool rowDsHuge() {
    static const struct { UINT w, h; D3DFORMAT f; const char *what; } bad[] = {
        {16385, 16, D3DFMT_D24X8, "CreateDepthStencilSurface (16385 wide)"},
        {0, 16, D3DFMT_D24X8, "CreateDepthStencilSurface (no width)"},
        {64, 64, D3DFMT_X8R8G8B8, "CreateDepthStencilSurface (a colour format)"}};
    for (const auto &b : bad) {
        IDirect3DSurface9 *s = nullptr;
        const HRESULT hr = dev->CreateDepthStencilSurface(b.w, b.h, b.f, D3DMULTISAMPLE_NONE, 0, FALSE, &s, nullptr);
        if (s) s->Release();
        if (hr != D3DERR_INVALIDCALL) return fail(b.what, hr);
    }
    return true;
}
// A dynamic depth texture's LockRect, past the core's slice (C4c's packed upload): D3DERR_NOTAVAILABLE under core
// (strict); D3D_OK elsewhere (a lock that succeeds is unlocked: an unlock would log a refusal of its own).
static bool rowDsLock() {
    Com<IDirect3DTexture9> t;
    CK(CreateTexture, 64, 64, 1, D3DUSAGE_DYNAMIC, D3DFMT_D16, D3DPOOL_DEFAULT, &t.p, nullptr);
    D3DLOCKED_RECT lr = {};
    const HRESULT hr = t.p->LockRect(0, &lr, nullptr, 0);
    if (SUCCEEDED(hr)) t.p->UnlockRect(0);
    if (strictMode ? hr != D3DERR_NOTAVAILABLE : hr != D3D_OK) return fail("LockRect", hr);
    return true;
}

// A pre-transformed draw past the far plane (z 1.5) while a depth-stencil is bound and ZENABLE is on is clipped, not
// clamped, also when the draw uses no plane of it (ZFUNC ALWAYS, no Z write) and samples it: the quad at z 0.5 draws
// the sampled depth (0.5, grey), the one at z 1.5 nothing.
static bool rowDsRhwClip() {
    const Px px[] = {{64, 128, 0x808080}, {192, 128, BLUE}};
    const bool ok = frames(BLUE, px, 2, [&] {
        Com<IDirect3DTexture9> t;
        TV3 a[4], b[4];
        tquad(a, 32, 96, 96, 160, 64.5f / 256, 0.5f, 0.5f), tquad(b, 160, 96, 224, 160, 192.5f / 256, 0.5f, 0.5f);
        for (int i = 0; i < 4; i++) b[i].z = 1.5f;
        if (!makeDepthTex(t, FMT_INTZ) || !bindDsLevel(t.p) || !clearZ(0.5f) || !rstate(D3DRS_ZFUNC, D3DCMP_ALWAYS) ||
            !rstate(D3DRS_ZWRITEENABLE, FALSE))
            return false;
        CK(SetTexture, 0, t.p);
        const bool drawn = tdraw(a) && tdraw(b);
        CK(SetTexture, 0, nullptr);
        return drawn && bindDs(dsSurf);
    });
    CK(SetTexture, 0, nullptr);
    CK(SetDepthStencilSurface, dsSurf);
    return ok;
}
static void depthRows(IDirect3DDevice9 *d, bool strict) {
    static const struct { const char *name; bool (*run)(); } rows[] = {
        {"ds-default", rowDsDefault},   {"ds-test", rowDsTest},         {"ds-func", rowDsFunc},
        {"ds-write", rowDsWrite},       {"ds-zenable", rowDsZenable},   {"ds-clearz", rowDsClearZ},
        {"ds-clearonly", rowDsClearOnly}, {"ds-stencil", rowDsStencil}, {"ds-twosided", rowDsTwoSided},
        {"ds-bias", rowDsBias},         {"ds-slope", rowDsSlope},       {"ds-clamp", rowDsClamp},
        {"ds-odepth", rowDsODepth},     {"ds-surface", rowDsSurface},   {"ds-nostencil", rowDsNoStencil},
        {"ds-unbound", rowDsUnbound},   {"ds-persist", rowDsPersist},   {"ds-texture", rowDsTexture},
        {"ds-texstencil", rowDsTexStencil}, {"ds-mip", rowDsMip},           {"ds-release", rowDsRelease},   {"ds-shadow", rowDsShadow},
        {"ds-intz", rowDsIntz},         {"ds-df", rowDsDf},             {"ds-fetch4", rowDsFetch4},
        {"ds-size", rowDsSize},         {"ds-rhwclip", rowDsRhwClip},   {"ds-feedback", rowDsFeedback},
        {"ds-msaa", rowDsMsaa},
        {"ds-huge", rowDsHuge},         {"ds-lock", rowDsLock}};
    dev = d, strictMode = strict, budgetEnd = GetTickCount64() + 150000;
    HRESULT hr = dev->GetRenderTarget(0, &rtSurf);
    if (SUCCEEDED(hr))
        hr = dev->CreateOffscreenPlainSurface(256, 256, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sysSurf, nullptr);
    if (SUCCEEDED(hr)) hr = dev->GetRenderState(D3DRS_ZENABLE, &zenableDefault);
    if (SUCCEEDED(hr)) hr = dev->GetDepthStencilSurface(&dsSurf);
    for (const auto &r : rows) {
        why[0] = 0;
        bool ok;
        if (FAILED(hr)) ok = fail("GetRenderTarget/GetDepthStencilSurface", hr);
        else if (GetTickCount64() >= budgetEnd) ok = false, snprintf(why, sizeof why, "budget");
        else ok = reset() && depthReset() && r.run();
        row(r.name, ok, why);
    }
    if (dsSurf) dsSurf->Release();
    if (sysSurf) sysSurf->Release();
    if (rtSurf) rtSurf->Release();
}

// --- The "blend" mode (DXMTRS C4b-3d3): Clear with rects, under the scissor and bounded by the viewport; the scissor
// test; a depth-stencil of another size than the target; sRGB write under blending; blending. On the depth mode's
// device (auto D24S8). Each row starts with Z cleared to 1, stencil to 0 and ZENABLE off: a row that tests depth
// turns it on. frames() resets the scissor rect each frame (its SetRenderTarget), so a row sets its rect in its draw.
static bool scissor(LONG x1, LONG y1, LONG x2, LONG y2) {
    const RECT r = {x1, y1, x2, y2};
    CK(SetScissorRect, &r);
    return true;
}
static bool clearRects(DWORD count, const D3DRECT *rects, DWORD flags, DWORD color, float z = 1.0f, DWORD stencil = 0) {
    CK(Clear, count, rects, flags, color, z, stencil);
    return true;
}
static bool viewport(DWORD x, DWORD y, DWORD w, DWORD h) {
    const D3DVIEWPORT9 v = {x, y, w, h, 0.0f, 1.0f};
    CK(SetViewport, &v);
    return true;
}
static bool blendStart() {
    CK(Clear, 0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);
    return rstate(D3DRS_ZENABLE, D3DZB_FALSE);
}
// Blending on with these factors and operation; the fixed-function alpha is the diffuse alpha.
static bool blend(DWORD src, DWORD dst, DWORD op = D3DBLENDOP_ADD) {
    CK(SetTextureStageState, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    CK(SetTextureStageState, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    return rstate(D3DRS_ALPHABLENDENABLE, TRUE) && rstate(D3DRS_SRCBLEND, src) && rstate(D3DRS_DESTBLEND, dst) &&
           rstate(D3DRS_BLENDOP, op);
}
// A cell of a row of cells: the 32-pixel-wide quad i (0-5) at y 96-160, its probe at its centre.
static bool cell(int i, DWORD c, float z = 0.5f) { return zquad(16.0f + 40 * i, 96, 48.0f + 40 * i, 160, z, c); }
static Px cellPx(int i, DWORD want) { return {32 + 40 * i, 128, want}; }
static const DWORD GREY = D3DCOLOR_XRGB(128, 128, 128);

// Clear with two rects: they alone are cleared.
static bool rowClRects() {
    const Px px[] = {{48, 48, TGRN}, {208, 208, TGRN}, {128, 128, BLUE}, {100, 48, BLUE}, {15, 48, BLUE}, {16, 48, TGRN},
                     {79, 48, TGRN}, {80, 48, BLUE}};
    return frames(BLUE, px, 8, [&] {
        const D3DRECT r[2] = {{16, 16, 80, 80}, {176, 176, 240, 240}};
        return clearRects(2, r, D3DCLEAR_TARGET, TGRN);
    });
}
// Rects with a count of 0 clear nothing; no rects with a count clear everything.
static bool rowClCount() {
    const Px none[] = {{48, 48, BLUE}, {200, 200, BLUE}}, all[] = {{48, 48, TRED}, {200, 200, TRED}};
    const D3DRECT r = {16, 16, 80, 80};
    return frames(BLUE, none, 2, [&] { return clearRects(0, &r, D3DCLEAR_TARGET, TGRN); }) &&
           frames(BLUE, all, 2, [&] { return clearRects(5, nullptr, D3DCLEAR_TARGET, TRED); });
}
// Under the scissor test a Clear with no rects clears the scissor rect, colour and depth (a quad at z 0.5 is hidden
// where Z was cleared to 0.2); a rect is cut to its overlap with the scissor rect.
static bool rowClScissor() {
    const Px a[] = {{128, 128, TGRN}, {32, 32, TRED}, {224, 128, TRED}, {63, 128, TRED}, {64, 128, TGRN}};
    if (!frames(BLUE, a, 5, [&] {
            if (!clearZ() || !scissor(64, 64, 192, 192) || !rstate(D3DRS_SCISSORTESTENABLE, TRUE) ||
                !clearRects(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, TGRN, 0.2f) ||
                !rstate(D3DRS_SCISSORTESTENABLE, FALSE))
                return false;
            return rstate(D3DRS_ZENABLE, D3DZB_TRUE) && zquad(0, 0, 256, 256, 0.5f, TRED) &&
                   rstate(D3DRS_ZENABLE, D3DZB_FALSE);
        }))
        return false;
    const Px b[] = {{96, 96, TYEL}, {48, 48, BLUE}, {160, 160, BLUE}, {127, 127, TYEL}, {128, 128, BLUE}};
    return frames(BLUE, b, 5, [&] {
        const D3DRECT r = {32, 32, 128, 128};
        return scissor(64, 64, 192, 192) && rstate(D3DRS_SCISSORTESTENABLE, TRUE) &&
               clearRects(1, &r, D3DCLEAR_TARGET, TYEL) && rstate(D3DRS_SCISSORTESTENABLE, FALSE);
    });
}
// A Clear is bounded by the viewport: colour, depth (a z 0.5 quad hidden inside) and stencil (an EQUAL 7 quad shows
// inside only); a rect is cut at the viewport's edge.
static bool rowClViewport() {
    const Px a[] = {{128, 128, TYEL}, {32, 32, TRED}, {200, 128, TRED}, {63, 128, TRED}, {64, 128, TYEL},
                    {191, 128, TYEL}, {192, 128, TRED}};
    if (!frames(BLUE, a, 7, [&] {
            if (!clearZS(1.0f, 0) || !viewport(64, 64, 128, 128) ||
                !clearRects(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, TGRN, 0.2f, 7))
                return false;
            CK(SetViewport, &FULL);
            if (!rstate(D3DRS_ZENABLE, D3DZB_TRUE) || !zquad(0, 0, 256, 256, 0.5f, TRED)) return false;
            return rstate(D3DRS_STENCILENABLE, TRUE) && rstate(D3DRS_STENCILFUNC, D3DCMP_EQUAL) &&
                   rstate(D3DRS_STENCILREF, 7) && zquad(0, 0, 256, 256, 0.1f, TYEL) &&
                   rstate(D3DRS_STENCILENABLE, FALSE) && rstate(D3DRS_ZENABLE, D3DZB_FALSE);
        }))
        return false;
    const Px b[] = {{80, 120, TMAG}, {48, 120, BLUE}, {120, 120, BLUE}, {80, 160, BLUE}};
    return frames(BLUE, b, 4, [&] {
        const D3DRECT r = {32, 100, 100, 150};
        if (!viewport(64, 64, 128, 128) || !clearRects(1, &r, D3DCLEAR_TARGET, TMAG)) return false;
        CK(SetViewport, &FULL);
        return true;
    });
}
// A Z Clear of one rect: a quad at z 0.5 is hidden inside it (Z 0.2) and shows outside (Z 1).
static bool rowClZ() {
    const Px px[] = {{128, 128, BLUE}, {32, 32, TRED}, {63, 128, TRED}, {64, 128, BLUE}, {191, 191, BLUE}, {192, 192, TRED}};
    return frames(BLUE, px, 6, [&] {
        const D3DRECT r = {64, 64, 192, 192};
        return clearZ() && clearRects(1, &r, D3DCLEAR_ZBUFFER, 0, 0.2f) && rstate(D3DRS_ZENABLE, D3DZB_TRUE) &&
               zquad(0, 0, 256, 256, 0.5f, TRED) && rstate(D3DRS_ZENABLE, D3DZB_FALSE);
    });
}
// A stencil Clear of one rect: an EQUAL 7 quad shows inside it only, and the depth is left (Z 0.3 everywhere: a quad
// at z 0.5 stays hidden). On a D24X8 surface the same Clear answers D3D_OK and changes nothing.
static bool rowClStencil() {
    const Px px[] = {{128, 128, TGRN}, {32, 32, BLUE}, {63, 128, BLUE}, {64, 128, TGRN}};
    const D3DRECT r = {64, 64, 192, 192};
    if (!frames(BLUE, px, 4, [&] {
            if (!clearZS(0.3f, 0) || !clearRects(1, &r, D3DCLEAR_STENCIL, 0, 1.0f, 7)) return false;
            if (!rstate(D3DRS_STENCILENABLE, TRUE) || !rstate(D3DRS_STENCILFUNC, D3DCMP_EQUAL) ||
                !rstate(D3DRS_STENCILREF, 7) || !zquad(0, 0, 256, 256, 0.5f, TGRN) ||
                !rstate(D3DRS_STENCILENABLE, FALSE))
                return false;
            return rstate(D3DRS_ZENABLE, D3DZB_TRUE) && zquad(0, 0, 256, 256, 0.5f, TRED) &&
                   rstate(D3DRS_ZENABLE, D3DZB_FALSE);
        }))
        return false;
    Com<IDirect3DSurface9> d;
    if (!makeDs(d)) return false;
    const Px none[] = {{128, 128, BLUE}, {32, 32, BLUE}};
    const bool ok = frames(BLUE, none, 2, [&] {
        if (!bindDs(d.p) || !clearZ(0.3f) || !clearRects(1, &r, D3DCLEAR_STENCIL, 0, 1.0f, 7)) return false;
        return rstate(D3DRS_ZENABLE, D3DZB_TRUE) && zquad(0, 0, 256, 256, 0.5f, TRED) &&
               rstate(D3DRS_ZENABLE, D3DZB_FALSE) && bindDs(dsSurf);
    });
    CK(SetDepthStencilSurface, dsSurf);
    return ok;
}
// The colour write mask does not reach a Clear, whole or of a rect.
static bool rowClMask() {
    const Px px[] = {{32, 32, TGRN}, {128, 128, TYEL}};
    return frames(BLUE, px, 2, [&] {
        const D3DRECT r = {64, 64, 192, 192};
        return rstate(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED) &&
               clearRects(0, nullptr, D3DCLEAR_TARGET, TGRN) && clearRects(1, &r, D3DCLEAR_TARGET, TYEL) &&
               rstate(D3DRS_COLORWRITEENABLE, 0xf);
    });
}
// A rect Clear between two draws keeps its place: white over green over red where they stack.
static bool rowClOrder() {
    const Px px[] = {{64, 64, TWHT}, {64, 176, TGRN}, {176, 64, TRED}, {240, 240, BLUE}};
    return frames(BLUE, px, 4, [&] {
        const D3DRECT r = {32, 32, 128, 224};
        return zquad(32, 32, 224, 224, 0.5f, TRED) && clearRects(1, &r, D3DCLEAR_TARGET, TGRN) &&
               zquad(32, 32, 128, 128, 0.5f, TWHT);
    });
}
// Rect and viewport-bounded clears of a texture's level 1 (32x32) and of a cube target's face.
static bool rowClLevel() {
    if (!until([&] {
            Com<IDirect3DTexture9> t;
            Com<IDirect3DSurface9> s;
            if (!scene([&] {
                    const D3DRECT r = {8, 8, 24, 24};
                    return makeRt(t, 64, 2) && target(t.p, 1) && clearTo(TBLU) &&
                           clearRects(1, &r, D3DCLEAR_TARGET, TGRN) && viewport(16, 0, 16, 8) &&
                           clearRects(0, nullptr, D3DCLEAR_TARGET, TRED) && backToScreen();
                }) ||
                !levelOf(t.p, 1, s))
                return false;
            return readTarget(s.p, 32, 32, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
                return want("(16,16)", texel(b, p, 16, 16), TGRN) && want("(4,4)", texel(b, p, 4, 4), TBLU) &&
                       want("(20,4)", texel(b, p, 20, 4), TRED) && want("(28,20)", texel(b, p, 28, 20), TBLU) &&
                       want("(12,4)", texel(b, p, 12, 4), TBLU);
            });
        }))
        return false;
    return until([&] {
        Com<IDirect3DCubeTexture9> c;
        Com<IDirect3DSurface9> s;
        if (!scene([&] {
                const D3DRECT r = {4, 4, 12, 12};
                if (!clearCube(c)) return false;
                const HRESULT hr = c.p->GetCubeMapSurface((D3DCUBEMAP_FACES)3, 0, &s.p);
                if (FAILED(hr)) return fail("GetCubeMapSurface", hr);
                CK(SetRenderTarget, 0, s.p);
                return clearRects(1, &r, D3DCLEAR_TARGET, TWHT) && backToScreen();
            }))
            return false;
        return readTarget(s.p, 16, 16, D3DFMT_X8R8G8B8, [&](const BYTE *b, int p) {
            return want("face (8,8)", texel(b, p, 8, 8), TWHT) && want("face (1,1)", texel(b, p, 1, 1), CUBE_FACES[3]);
        });
    });
}
// A depth-stencil larger than the target (512 with the 256 back buffer). A Z Clear is bounded by the viewport: seen
// through a 512 target, a quad at z 0.5 shows where the Clear reached (the 256 square) and is hidden past it (Z 0.1
// left by a near quad). With the viewport set to the depth-stencil's size, the same Clear reaches all of it.
static bool rowClBigZ() {
    Com<IDirect3DSurface9> big;
    Com<IDirect3DTexture9> t;
    Com<IDirect3DSurface9> s;
    if (!makeDs(big, D3DFMT_D24X8, 512) || !makeRt(t, 512) || !levelOf(t.p, 0, s)) return false;
    const bool ok = until([&] {
        if (!scene([&] {
                if (!target(t.p) || !bindDs(big.p) ||
                    !clearRects(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, TBLU, 1.0f) ||
                    !rstate(D3DRS_ZENABLE, D3DZB_TRUE) || !zquad(0, 0, 512, 512, 0.1f, TRED))
                    return false;
                if (!backToScreen() || !clearZ() || !target(t.p) || !zquad(0, 0, 512, 512, 0.5f, TGRN)) return false;
                return backToScreen();
            }))
            return false;
        if (!readTarget(s.p, 512, 512, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
                return want("(100,100)", texel(b, p, 100, 100), TGRN) && want("(400,400)", texel(b, p, 400, 400), TRED) &&
                       want("(300,100)", texel(b, p, 300, 100), TRED) && want("(255,255)", texel(b, p, 255, 255), TGRN) &&
                       want("(256,256)", texel(b, p, 256, 256), TRED);
            }))
            return false;
        if (!scene([&] {
                if (!viewport(0, 0, 512, 512) || !clearZ() || !target(t.p) || !zquad(0, 0, 512, 512, 0.5f, TYEL))
                    return false;
                return backToScreen();
            }))
            return false;
        return readTarget(s.p, 512, 512, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
            return want("whole (400,400)", texel(b, p, 400, 400), TYEL) &&
                   want("whole (100,100)", texel(b, p, 100, 100), TYEL);
        });
    });
    dev->SetRenderTarget(0, rtSurf);
    CK(SetDepthStencilSurface, dsSurf);
    return ok;
}
// A depth-stencil smaller than the target (128 with the 256 back buffer): a Z Clear of a rect that leaves it is cut
// to it; a quad at z 0.5 is hidden where the rect cleared to 0.2 and shows beside it.
static bool rowClSmallZ() {
    Com<IDirect3DSurface9> small;
    if (!makeDs(small, D3DFMT_D24X8, 128)) return false;
    const Px px[] = {{100, 100, BLUE}, {32, 32, TRED}};
    const bool ok = frames(BLUE, px, 2, [&] {
        const D3DRECT r = {64, 64, 200, 200};
        return bindDs(small.p) && clearZ() && clearRects(1, &r, D3DCLEAR_ZBUFFER, 0, 0.2f) &&
               rstate(D3DRS_ZENABLE, D3DZB_TRUE) && zquad(0, 0, 256, 256, 0.5f, TRED) &&
               rstate(D3DRS_ZENABLE, D3DZB_FALSE) && bindDs(dsSurf);
    });
    CK(SetDepthStencilSurface, dsSurf);
    return ok;
}

// The scissor test: a quad over the whole target draws inside the rect only.
static bool rowScRect() {
    const Px px[] = {{128, 128, TRED}, {32, 32, BLUE}, {63, 128, BLUE}, {64, 128, TRED}, {191, 128, TRED},
                     {192, 128, BLUE}, {128, 63, BLUE}, {128, 192, BLUE}};
    return frames(BLUE, px, 8, [&] {
        return scissor(64, 64, 192, 192) && rstate(D3DRS_SCISSORTESTENABLE, TRUE) && zquad(0, 0, 256, 256, 0.5f, TRED) &&
               rstate(D3DRS_SCISSORTESTENABLE, FALSE);
    });
}
// The rect with the test off bounds nothing.
static bool rowScOff() {
    const Px px[] = {{128, 128, TRED}, {32, 32, TRED}, {224, 224, TRED}};
    return frames(BLUE, px, 3, [&] { return scissor(64, 64, 192, 192) && zquad(0, 0, 256, 256, 0.5f, TRED); });
}
// Rects that hold no pixel of the target (inverted, empty, past it, negative) and a viewport off it: every draw
// answers D3D_OK and draws nothing.
static bool rowScEmpty() {
    const Px px[] = {{128, 128, BLUE}, {32, 32, BLUE}, {224, 224, BLUE}};
    return frames(BLUE, px, 3, [&] {
        static const LONG rects[][4] = {{192, 192, 64, 64}, {100, 100, 100, 100}, {300, 300, 400, 400}, {-50, -50, -10, -10}};
        if (!rstate(D3DRS_SCISSORTESTENABLE, TRUE)) return false;
        for (const auto &r : rects) {
            if (!scissor(r[0], r[1], r[2], r[3])) return false;
            const HRESULT hr = zquadHr(0, 0, 256, 256, 0.5f, TRED);
            if (hr != D3D_OK) return fail("DrawPrimitiveUP (an empty scissor)", hr);
        }
        if (!rstate(D3DRS_SCISSORTESTENABLE, FALSE) || !viewport(300, 300, 64, 64)) return false;
        const HRESULT hr = zquadHr(300, 300, 364, 364, 0.5f, TRED);
        CK(SetViewport, &FULL);
        return hr == D3D_OK || fail("DrawPrimitiveUP (a viewport off the target)", hr);
    });
}
// A rect larger than a 64x64 target is cut to it; one that starts before it draws its part on the target.
static bool rowScClamp() {
    return until([&] {
        Com<IDirect3DTexture9> t;
        Com<IDirect3DSurface9> s;
        if (!scene([&] {
                return makeRt(t) && target(t.p) && clearTo(TBLU) && scissor(0, 0, 256, 256) &&
                       rstate(D3DRS_SCISSORTESTENABLE, TRUE) && zquad(0, 0, 64, 64, 0.5f, TRED) &&
                       scissor(-32, -32, 32, 32) && zquad(0, 0, 64, 64, 0.5f, TGRN) &&
                       rstate(D3DRS_SCISSORTESTENABLE, FALSE) && backToScreen();
            }) ||
            !levelOf(t.p, 0, s))
            return false;
        return readTarget(s.p, 64, 64, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
            return want("(16,16)", texel(b, p, 16, 16), TGRN) && want("(48,48)", texel(b, p, 48, 48), TRED) &&
                   want("(48,16)", texel(b, p, 48, 16), TRED) && want("(62,62)", texel(b, p, 62, 62), TRED) &&
                   want("(31,31)", texel(b, p, 31, 31), TGRN) && want("(32,32)", texel(b, p, 32, 32), TRED);
        });
    });
}
// The rect follows every change inside one pass: two rects, then the test off.
static bool rowScPass() {
    const Px px[] = {{64, 64, TRED}, {192, 64, TGRN}, {128, 64, BLUE}, {128, 228, TWHT}, {64, 150, BLUE}, {8, 228, TWHT}};
    return frames(BLUE, px, 6, [&] {
        return rstate(D3DRS_SCISSORTESTENABLE, TRUE) && scissor(16, 16, 112, 112) && zquad(0, 0, 256, 256, 0.5f, TRED) &&
               scissor(144, 16, 240, 112) && zquad(0, 0, 256, 256, 0.5f, TGRN) &&
               rstate(D3DRS_SCISSORTESTENABLE, FALSE) && zquad(0, 200, 256, 256, 0.5f, TWHT);
    });
}
// The rect survives a pass break that leaves it alone (another depth-stencil bound): the next quad, with no scissor
// call since, draws inside it only.
static bool rowScBreak() {
    Com<IDirect3DSurface9> d;
    if (!makeDs(d)) return false;
    const Px px[] = {{128, 128, TGRN}, {32, 32, BLUE}, {224, 224, BLUE}, {63, 128, BLUE}, {64, 128, TGRN}};
    const bool ok = frames(BLUE, px, 5, [&] {
        return rstate(D3DRS_SCISSORTESTENABLE, TRUE) && scissor(64, 64, 192, 192) && zquad(0, 0, 256, 256, 0.5f, TRED) &&
               bindDs(d.p) && zquad(0, 0, 256, 256, 0.5f, TGRN) && bindDs(dsSurf) &&
               rstate(D3DRS_SCISSORTESTENABLE, FALSE);
    });
    CK(SetDepthStencilSurface, dsSurf);
    return ok;
}

// A depth-stencil larger than the target (512 D24S8 with a 64x64 texture): the depth and stencil tests as on a pair
// of one size. Z: a near quad on the left half hides a far one. Stencil: written on the top half, an EQUAL quad
// shows there.
static bool rowSzLarge() {
    Com<IDirect3DSurface9> big;
    if (!makeDs(big, D3DFMT_D24S8, 512)) return false;
    const bool ok = until([&] {
        Com<IDirect3DTexture9> t;
        Com<IDirect3DSurface9> s;
        if (!scene([&] {
                if (!makeRt(t) || !target(t.p) || !bindDs(big.p) ||
                    !clearRects(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, TBLU, 1.0f, 0) ||
                    !rstate(D3DRS_ZENABLE, D3DZB_TRUE) || !zquad(0, 0, 32, 64, 0.3f, TRED) ||
                    !zquad(0, 0, 64, 64, 0.6f, TGRN))
                    return false;
                if (!rstate(D3DRS_STENCILENABLE, TRUE) || !rstate(D3DRS_STENCILFUNC, D3DCMP_ALWAYS) ||
                    !rstate(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE) || !rstate(D3DRS_STENCILREF, 3) ||
                    !rstate(D3DRS_COLORWRITEENABLE, 0) || !rstate(D3DRS_ZENABLE, D3DZB_FALSE) ||
                    !zquad(0, 0, 64, 32, 0.5f, TWHT) || !rstate(D3DRS_COLORWRITEENABLE, 0xf))
                    return false;
                return rstate(D3DRS_STENCILFUNC, D3DCMP_EQUAL) && rstate(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP) &&
                       zquad(0, 0, 64, 64, 0.5f, TYEL) && rstate(D3DRS_STENCILENABLE, FALSE) && bindDs(dsSurf) &&
                       backToScreen();
            }) ||
            !levelOf(t.p, 0, s))
            return false;
        return readTarget(s.p, 64, 64, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
            return want("(16,16)", texel(b, p, 16, 16), TYEL) && want("(48,16)", texel(b, p, 48, 16), TYEL) &&
                   want("(16,48)", texel(b, p, 16, 48), TRED) && want("(48,48)", texel(b, p, 48, 48), TGRN);
        });
    });
    dev->SetRenderTarget(0, rtSurf);
    CK(SetDepthStencilSurface, dsSurf);
    return ok;
}
// A whole Z Clear of a 512 depth-stencil reaches all of it though the next pass draws to a 64x64 target: back on a
// 512 target, a quad at z 0.5 shows at (400,400), where a near quad (z 0.1) was before the Clear.
static bool rowSzClear() {
    Com<IDirect3DSurface9> big;
    Com<IDirect3DTexture9> t, small;
    Com<IDirect3DSurface9> s;
    if (!makeDs(big, D3DFMT_D24X8, 512) || !makeRt(t, 512) || !makeRt(small) || !levelOf(t.p, 0, s)) return false;
    const bool ok = until([&] {
        if (!scene([&] {
                if (!target(t.p) || !bindDs(big.p) ||
                    !clearRects(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, TBLU, 1.0f) ||
                    !rstate(D3DRS_ZENABLE, D3DZB_TRUE) || !zquad(0, 0, 512, 512, 0.1f, TRED) || !clearZ())
                    return false;
                if (!target(small.p) || !zquad(0, 0, 64, 64, 0.5f, TGRN)) return false;
                return target(t.p) && zquad(0, 0, 512, 512, 0.5f, TYEL) && bindDs(dsSurf) && backToScreen();
            }))
            return false;
        return readTarget(s.p, 512, 512, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
            return want("(400,400)", texel(b, p, 400, 400), TYEL) && want("(100,300)", texel(b, p, 100, 300), TYEL) &&
                   want("(32,32)", texel(b, p, 32, 32), TYEL);
        });
    });
    dev->SetRenderTarget(0, rtSurf);
    CK(SetDepthStencilSurface, dsSurf);
    return ok;
}
// A depth-stencil smaller than the target (128 with the 256 back buffer): a depth-tested quad draws within it. What
// lies outside the depth-stencil is not asserted.
static bool rowSzSmall() {
    Com<IDirect3DSurface9> small;
    if (!makeDs(small, D3DFMT_D24X8, 128)) return false;
    const Px px[] = {{64, 64, TRED}, {16, 100, TRED}};
    const bool ok = frames(BLUE, px, 2, [&] {
        return bindDs(small.p) && clearZ() && rstate(D3DRS_ZENABLE, D3DZB_TRUE) && zquad(0, 0, 256, 256, 0.5f, TRED) &&
               rstate(D3DRS_ZENABLE, D3DZB_FALSE) && bindDs(dsSurf);
    });
    CK(SetDepthStencilSurface, dsSurf);
    return ok;
}

// sRGB write and Clear: a Clear to 128 and a rect Clear to 64 are stored encoded (188, 137); with it off, as given;
// a Clear with it off and a quad with it on each keep their own.
static bool rowSrClear() {
    const D3DRECT r = {64, 64, 192, 192};
    const Px on[] = {{32, 32, 0xbcbcbc}, {128, 128, 0x898989}};
    if (!frames(BLUE, on, 2, [&] {
            return rstate(D3DRS_SRGBWRITEENABLE, TRUE) && clearRects(0, nullptr, D3DCLEAR_TARGET, GREY) &&
                   clearRects(1, &r, D3DCLEAR_TARGET, D3DCOLOR_XRGB(64, 64, 64)) && rstate(D3DRS_SRGBWRITEENABLE, FALSE);
        }))
        return false;
    const Px off[] = {{32, 32, 0x808080}, {128, 128, 0x898989}};
    return frames(BLUE, off, 2, [&] {
        return clearRects(0, nullptr, D3DCLEAR_TARGET, GREY) && rstate(D3DRS_SRGBWRITEENABLE, TRUE) &&
               zquad(64, 64, 192, 192, 0.5f, D3DCOLOR_XRGB(64, 64, 64)) && rstate(D3DRS_SRGBWRITEENABLE, FALSE);
    });
}
// The grey Clear and the half-transparent black quad of sr-blend on the bound target.
static bool srgbBlend(float x0, float y0, float x1, float y1) {
    return rstate(D3DRS_SRGBWRITEENABLE, TRUE) && clearRects(0, nullptr, D3DCLEAR_TARGET, GREY) &&
           blend(D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA) && zquad(x0, y0, x1, y1, 0.5f, D3DCOLOR_ARGB(128, 0, 0, 0)) &&
           rstate(D3DRS_ALPHABLENDENABLE, FALSE) && rstate(D3DRS_SRGBWRITEENABLE, FALSE);
}
// sRGB write under blending. On a target with an sRGB view (the back buffer, an A8R8G8B8 texture) the blend is in
// linear space: black at alpha 128 over the 128 grey (stored 188) reads 137. On a target without one
// (A16B16G16R16F) the pixel stage encodes before the blender: 0.735 * 0.498.
static bool rowSrBlend() {
    const Px px[] = {{128, 128, 0x898989}, {32, 32, 0xbcbcbc}};
    if (!frames(BLUE, px, 2, [&] { return srgbBlend(64, 64, 192, 192); })) return false;
    if (!until([&] {
            Com<IDirect3DTexture9> t;
            Com<IDirect3DSurface9> s;
            if (!scene([&] { return makeRt(t) && target(t.p) && srgbBlend(16, 16, 48, 48) && backToScreen(); }) ||
                !levelOf(t.p, 0, s))
                return false;
            return readTarget(s.p, 64, 64, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
                return want("(32,32)", texel(b, p, 32, 32), 0x898989) && want("(4,4)", texel(b, p, 4, 4), 0xbcbcbc);
            });
        }))
        return false;
    return until([&] {
        Com<IDirect3DTexture9> t;
        Com<IDirect3DSurface9> s;
        if (!scene([&] {
                return makeRt(t, 64, 1, D3DFMT_A16B16G16R16F) && target(t.p) && srgbBlend(16, 16, 48, 48) &&
                       backToScreen();
            }) ||
            !levelOf(t.p, 0, s))
            return false;
        return readTarget(s.p, 64, 64, D3DFMT_A16B16G16R16F, [&](const BYTE *b, int pitch) {
            float in[4], out[4];
            decodeTexel(D3DFMT_A16B16G16R16F, b + 32 * pitch + 32 * 8, in);
            decodeTexel(D3DFMT_A16B16G16R16F, b + 4 * pitch + 4 * 8, out);
            if (fabsf(in[0] - 0.366f) > 0.01f || fabsf(out[0] - 0.735f) > 0.01f)
                return snprintf(why, sizeof why, "16F: blended %.3f want 0.366, clear %.3f want 0.735", in[0], out[0]),
                       false;
            return true;
        });
    });
}
// sRGB write toggled between blended quads of one frame, over a 128 grey stored as given: with it on the blend is
// linear (92), with it off it is of the stored values (64).
static bool rowSrToggle() {
    const Px px[] = {cellPx(0, 0x5c5c5c), cellPx(2, 0x404040), cellPx(4, 0x5c5c5c), {128, 32, 0x808080}};
    return frames(BLUE, px, 4, [&] {
        const DWORD black = D3DCOLOR_ARGB(128, 0, 0, 0);
        return clearRects(0, nullptr, D3DCLEAR_TARGET, GREY) && blend(D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA) &&
               rstate(D3DRS_SRGBWRITEENABLE, TRUE) && cell(0, black) && rstate(D3DRS_SRGBWRITEENABLE, FALSE) &&
               cell(2, black) && rstate(D3DRS_SRGBWRITEENABLE, TRUE) && cell(4, black) &&
               rstate(D3DRS_SRGBWRITEENABLE, FALSE) && rstate(D3DRS_ALPHABLENDENABLE, FALSE);
    });
}

// Source alpha blending: red at alpha 128 over blue.
static bool rowBlAlpha() {
    const Px px[] = {{128, 128, 0x80007f}, {32, 32, BLUE}};
    return frames(BLUE, px, 2, [&] {
        return blend(D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA) &&
               zquad(64, 64, 192, 192, 0.5f, D3DCOLOR_ARGB(128, 255, 0, 0));
    });
}
// Additive blending: (64, 64, 64) twice over blue.
static bool rowBlAdd() {
    const Px px[] = {{128, 128, 0x8080ff}, {32, 32, BLUE}};
    return frames(BLUE, px, 2, [&] {
        const DWORD c = D3DCOLOR_XRGB(64, 64, 64);
        return blend(D3DBLEND_ONE, D3DBLEND_ONE) && zquad(64, 64, 192, 192, 0.5f, c) && zquad(64, 64, 192, 192, 0.5f, c);
    });
}
// The blend operations, source (150, 100, 50) over (100, 150, 200) with ONE/ONE; MIN and MAX read no factor.
static bool rowBlOps() {
    const Px px[] = {cellPx(0, 0x320000), cellPx(1, 0x003296), cellPx(2, 0x646432),
                     cellPx(3, 0x9696c8), cellPx(4, 0x646432), cellPx(5, 0x9696c8)};
    return frames(D3DCOLOR_XRGB(100, 150, 200), px, 6, [&] {
        const DWORD c = D3DCOLOR_XRGB(150, 100, 50);
        return blend(D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_SUBTRACT) && cell(0, c) &&
               rstate(D3DRS_BLENDOP, D3DBLENDOP_REVSUBTRACT) && cell(1, c) && rstate(D3DRS_BLENDOP, D3DBLENDOP_MIN) &&
               cell(2, c) && rstate(D3DRS_BLENDOP, D3DBLENDOP_MAX) && cell(3, c) &&
               blend(D3DBLEND_ZERO, D3DBLEND_ZERO, D3DBLENDOP_MIN) && cell(4, c) &&
               rstate(D3DRS_BLENDOP, D3DBLENDOP_MAX) && cell(5, c);
    });
}
// The colour and alpha factors on an A8R8G8B8 target cleared to (40, 80, 120) with alpha 200, source (200, 100, 50)
// with alpha 128: destination colour and destination alpha give different values.
static bool rowBlFactors() {
    return until([&] {
        Com<IDirect3DTexture9> t;
        Com<IDirect3DSurface9> s;
        if (!scene([&] {
                static const DWORD f[6][2] = {{D3DBLEND_DESTCOLOR, D3DBLEND_ZERO},  {D3DBLEND_INVDESTCOLOR, D3DBLEND_ZERO},
                                              {D3DBLEND_ZERO, D3DBLEND_SRCCOLOR},   {D3DBLEND_ZERO, D3DBLEND_INVSRCCOLOR},
                                              {D3DBLEND_DESTALPHA, D3DBLEND_ZERO},  {D3DBLEND_SRCALPHASAT, D3DBLEND_ZERO}};
                if (!makeRt(t) || !target(t.p) || !clearTo(D3DCOLOR_ARGB(200, 40, 80, 120))) return false;
                for (int i = 0; i < 6; i++)
                    if (!blend(f[i][0], f[i][1]) ||
                        !zquad(2.0f + 10 * i, 16, 10.0f + 10 * i, 48, 0.5f, D3DCOLOR_ARGB(128, 200, 100, 50)))
                        return false;
                return rstate(D3DRS_ALPHABLENDENABLE, FALSE) && backToScreen();
            }) ||
            !levelOf(t.p, 0, s))
            return false;
        return readTarget(s.p, 64, 64, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
            static const DWORD wants[6] = {0x1f1f18, 0xa9451a, 0x1f1f18, 0x093160, 0x9d4e27, 0x2b160b};
            static const char *names[6] = {"DESTCOLOR", "INVDESTCOLOR", "SRCCOLOR", "INVSRCCOLOR", "DESTALPHA", "SRCALPHASAT"};
            for (int i = 0; i < 6; i++)
                if (!want(names[i], texel(b, p, 6 + 10 * i, 32), wants[i])) return false;
            return want("clear", texel(b, p, 6, 56), D3DCOLOR_XRGB(40, 80, 120));
        });
    });
}
// BOTHSRCALPHA and BOTHINVSRCALPHA as the source factor set both sides, whatever DESTBLEND says.
static bool rowBlBoth() {
    const Px px[] = {cellPx(0, 0x80007f), cellPx(2, 0x7f0080)};
    return frames(BLUE, px, 2, [&] {
        const DWORD c = D3DCOLOR_ARGB(128, 255, 0, 0);
        return blend(D3DBLEND_BOTHSRCALPHA, D3DBLEND_ONE) && cell(0, c) && blend(D3DBLEND_BOTHINVSRCALPHA, D3DBLEND_ZERO) &&
               cell(2, c);
    });
}
// Destination alpha (red by DESTALPHA over a blue of alpha 0x40 by INVDESTALPHA). The back buffer and an A8R8G8B8
// target read the stored alpha; an X8R8G8B8 target reads 1.
static bool rowBlDestAlpha() {
    const DWORD clear = D3DCOLOR_ARGB(0x40, 0, 0, 255);
    const Px px[] = {{128, 128, 0x4000bf}, {32, 32, BLUE}};
    if (!frames(BLUE, px, 2, [&] {
            return clearTo(clear) && blend(D3DBLEND_DESTALPHA, D3DBLEND_INVDESTALPHA) &&
                   zquad(64, 64, 192, 192, 0.5f, TRED | 0xff000000);
        }))
        return false;
    static const struct { D3DFORMAT f; DWORD want; const char *name; } targets[] = {
        {D3DFMT_X8R8G8B8, 0xff0000, "X8R8G8B8"}, {D3DFMT_A8R8G8B8, 0x4000bf, "A8R8G8B8"}};
    for (const auto &m : targets)
        if (!until([&] {
                Com<IDirect3DTexture9> t;
                Com<IDirect3DSurface9> s;
                if (!scene([&] {
                        return makeRt(t, 64, 1, m.f) && target(t.p) && clearTo(clear) &&
                               blend(D3DBLEND_DESTALPHA, D3DBLEND_INVDESTALPHA) &&
                               zquad(16, 16, 48, 48, 0.5f, TRED | 0xff000000) && rstate(D3DRS_ALPHABLENDENABLE, FALSE) &&
                               backToScreen();
                    }) ||
                    !levelOf(t.p, 0, s))
                    return false;
                return readTarget(s.p, 64, 64, m.f, [&](const BYTE *b, int p) {
                    return want(m.name, texel(b, p, 32, 32), m.want) && want("clear", texel(b, p, 4, 4), 0x0000ff);
                });
            }))
            return false;
    return true;
}
// Separate alpha blending on an A8R8G8B8 target (blue, alpha 0x80): with it on the alpha is the source's (ONE/ZERO),
// with it off the alpha blends as the colour does.
static bool rowBlSeparate() {
    return until([&] {
        Com<IDirect3DTexture9> t;
        Com<IDirect3DSurface9> s;
        if (!scene([&] {
                const DWORD c = D3DCOLOR_ARGB(0x40, 255, 0, 0);
                return makeRt(t) && target(t.p) && clearTo(D3DCOLOR_ARGB(0x80, 0, 0, 255)) &&
                       blend(D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA) && rstate(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE) &&
                       rstate(D3DRS_DESTBLENDALPHA, D3DBLEND_ZERO) && rstate(D3DRS_SEPARATEALPHABLENDENABLE, TRUE) &&
                       zquad(4, 16, 28, 48, 0.5f, c) && rstate(D3DRS_SEPARATEALPHABLENDENABLE, FALSE) &&
                       zquad(36, 16, 60, 48, 0.5f, c) && rstate(D3DRS_ALPHABLENDENABLE, FALSE) && backToScreen();
            }) ||
            !levelOf(t.p, 0, s))
            return false;
        return readTarget(s.p, 64, 64, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
            return want("separate", texel(b, p, 16, 32), 0x404000bf, 0xffffffff) &&
                   want("joint", texel(b, p, 48, 32), 0x704000bf, 0xffffffff);
        });
    });
}
// The blend factor colour: white by BLENDFACTOR over (0, 0, 100) by INVBLENDFACTOR, the factor changed between two
// quads of one pass; and as the alpha factor on an A8R8G8B8 target (the factor's alpha).
static bool rowBlFactor() {
    const Px px[] = {cellPx(0, 0x40408b), cellPx(2, 0xc0c0d9)};
    if (!frames(D3DCOLOR_XRGB(0, 0, 100), px, 2, [&] {
            return blend(D3DBLEND_BLENDFACTOR, D3DBLEND_INVBLENDFACTOR) && rstate(D3DRS_BLENDFACTOR, 0x80404040) &&
                   cell(0, 0xffffffff) && rstate(D3DRS_BLENDFACTOR, 0xffc0c0c0) && cell(2, 0xffffffff);
        }))
        return false;
    return until([&] {
        Com<IDirect3DTexture9> t;
        Com<IDirect3DSurface9> s;
        if (!scene([&] {
                return makeRt(t) && target(t.p) && clearTo(D3DCOLOR_ARGB(0, 0, 0, 255)) &&
                       blend(D3DBLEND_ONE, D3DBLEND_ZERO) && rstate(D3DRS_SRCBLENDALPHA, D3DBLEND_BLENDFACTOR) &&
                       rstate(D3DRS_DESTBLENDALPHA, D3DBLEND_ZERO) && rstate(D3DRS_SEPARATEALPHABLENDENABLE, TRUE) &&
                       rstate(D3DRS_BLENDFACTOR, 0x80404040) && zquad(16, 16, 48, 48, 0.5f, 0xffff0000) &&
                       rstate(D3DRS_SEPARATEALPHABLENDENABLE, FALSE) && rstate(D3DRS_ALPHABLENDENABLE, FALSE) &&
                       backToScreen();
            }) ||
            !levelOf(t.p, 0, s))
            return false;
        return readTarget(s.p, 64, 64, D3DFMT_A8R8G8B8, [&](const BYTE *b, int p) {
            return want("alpha factor", texel(b, p, 32, 32), 0x80ff0000, 0xffffffff);
        });
    });
}
// A quad that reads the blend factor and is stencil-tested: it shows on the strip whose stencil is its reference (5)
// and nowhere with another reference (4).
static bool rowBlStencil() {
    const Px px[] = {{128, 128, TGRN}, {128, 48, BLUE}, {32, 128, BLUE}, {128, 200, BLUE}};
    return frames(BLUE, px, 4, [&] {
        if (!clearZS(1.0f, 0) || !rstate(D3DRS_STENCILENABLE, TRUE) || !rstate(D3DRS_STENCILFUNC, D3DCMP_ALWAYS) ||
            !rstate(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE) || !rstate(D3DRS_STENCILREF, 5) ||
            !rstate(D3DRS_COLORWRITEENABLE, 0) || !zquad(64, 96, 192, 160, 0.5f, TWHT) ||
            !rstate(D3DRS_COLORWRITEENABLE, 0xf))
            return false;
        if (!rstate(D3DRS_STENCILFUNC, D3DCMP_EQUAL) || !rstate(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP) ||
            !blend(D3DBLEND_BLENDFACTOR, D3DBLEND_ZERO) || !rstate(D3DRS_BLENDFACTOR, 0xffffffff) ||
            !zquad(0, 0, 256, 256, 0.5f, TGRN | 0xff000000))
            return false;
        return rstate(D3DRS_STENCILREF, 4) && zquad(0, 0, 256, 256, 0.5f, TRED | 0xff000000) &&
               rstate(D3DRS_STENCILENABLE, FALSE);
    });
}
// Blending off with factors and an operation left behind: opaque.
static bool rowBlOff() {
    const Px px[] = {{128, 128, TRED}, {32, 32, BLUE}};
    return frames(BLUE, px, 2, [&] {
        return blend(D3DBLEND_SRCALPHA, D3DBLEND_ZERO, D3DBLENDOP_MAX) && rstate(D3DRS_ALPHABLENDENABLE, FALSE) &&
               zquad(64, 64, 192, 192, 0.5f, D3DCOLOR_ARGB(64, 255, 0, 0));
    });
}
// The colour write mask under blending: red and alpha alone move.
static bool rowBlMask() {
    const Px px[] = {{128, 128, 0x4000ff}, {32, 32, BLUE}};
    return frames(BLUE, px, 2, [&] {
        return rstate(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_ALPHA) &&
               blend(D3DBLEND_ONE, D3DBLEND_ONE) && zquad(64, 64, 192, 192, 0.5f, D3DCOLOR_XRGB(64, 64, 64)) &&
               rstate(D3DRS_COLORWRITEENABLE, 0xf);
    });
}
// Blending with a pixel shader's alpha (ps_2_0 writing c0 = red at 0.25) and with the fixed-function untransformed
// path's (diffuse alpha 128).
static bool rowBlProg() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_C0)) return false;
    const Px px[] = {{64, 128, 0x4000bf}, {192, 128, 0x80007f}, {128, 128, BLUE}};
    const bool ok = frames(BLUE, px, 3, [&] {
        static const float c0[4] = {1, 0, 0, 0.25f};
        V3 q[4];
        FC f[4];
        quad(q, -0.75f, -0.25f, -0.25f, 0.25f);
        cquad(f, 0.25f, -0.25f, 0.75f, 0.25f, D3DCOLOR_ARGB(128, 255, 0, 0));
        if (!blend(D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA) || !use(p)) return false;
        CK(SetPixelShaderConstantF, 0, c0, 1);
        if (!strip(q)) return false;
        CK(SetVertexShader, nullptr);
        CK(SetPixelShader, nullptr);
        return fdraw(FVF_C, f);
    });
    dev->SetVertexShader(nullptr);
    dev->SetPixelShader(nullptr);
    return ok;
}
// The blend factor colour reaches a draw after a pass break (another depth-stencil bound) with no state call since.
static bool rowBlPass() {
    Com<IDirect3DSurface9> d;
    if (!makeDs(d)) return false;
    const Px px[] = {cellPx(0, 0x8080b2), cellPx(4, 0x8080b2)};
    const bool ok = frames(D3DCOLOR_XRGB(0, 0, 100), px, 2, [&] {
        return blend(D3DBLEND_BLENDFACTOR, D3DBLEND_INVBLENDFACTOR) && rstate(D3DRS_BLENDFACTOR, 0x80808080) &&
               cell(0, 0xffffffff) && bindDs(d.p) && cell(4, 0xffffffff) && bindDs(dsSurf);
    });
    CK(SetDepthStencilSurface, dsSurf);
    return ok;
}

static void blendRows(IDirect3DDevice9 *d, bool strict) {
    static const struct { const char *name; bool (*run)(); } rows[] = {
        {"cl-rects", rowClRects},     {"cl-count", rowClCount},       {"cl-scissor", rowClScissor},
        {"cl-viewport", rowClViewport}, {"cl-z", rowClZ},             {"cl-stencil", rowClStencil},
        {"cl-mask", rowClMask},       {"cl-order", rowClOrder},       {"cl-level", rowClLevel},
        {"cl-bigz", rowClBigZ},       {"cl-smallz", rowClSmallZ},     {"sc-rect", rowScRect},
        {"sc-off", rowScOff},         {"sc-empty", rowScEmpty},       {"sc-clamp", rowScClamp},
        {"sc-pass", rowScPass},       {"sc-break", rowScBreak},       {"sz-large", rowSzLarge},
        {"sz-clear", rowSzClear},     {"sz-small", rowSzSmall},       {"sr-clear", rowSrClear},
        {"sr-blend", rowSrBlend},     {"sr-toggle", rowSrToggle},     {"bl-alpha", rowBlAlpha},
        {"bl-add", rowBlAdd},         {"bl-ops", rowBlOps},           {"bl-factors", rowBlFactors},
        {"bl-both", rowBlBoth},       {"bl-destalpha", rowBlDestAlpha}, {"bl-separate", rowBlSeparate},
        {"bl-factor", rowBlFactor},   {"bl-stencil", rowBlStencil},   {"bl-off", rowBlOff},
        {"bl-mask", rowBlMask},       {"bl-prog", rowBlProg},         {"bl-pass", rowBlPass}};
    dev = d, strictMode = strict, budgetEnd = GetTickCount64() + 180000;
    HRESULT hr = dev->GetRenderTarget(0, &rtSurf);
    if (SUCCEEDED(hr))
        hr = dev->CreateOffscreenPlainSurface(256, 256, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sysSurf, nullptr);
    if (SUCCEEDED(hr)) hr = dev->GetDepthStencilSurface(&dsSurf);
    for (const auto &r : rows) {
        why[0] = 0;
        bool ok;
        if (FAILED(hr)) ok = fail("GetRenderTarget/GetDepthStencilSurface", hr);
        else if (GetTickCount64() >= budgetEnd) ok = false, snprintf(why, sizeof why, "budget");
        else ok = reset() && depthReset() && blendStart() && r.run();
        row(r.name, ok, why);
    }
    if (dsSurf) dsSurf->Release();
    if (sysSurf) sysSurf->Release();
    if (rtSurf) rtSurf->Release();
}

// --- The "mrt" mode (DXMTRS C4b-3d4): render targets at index 1-3. On the blend mode's device (auto D24S8, Z off
// unless a row turns it on). Targets are 64x64 A8R8G8B8 render-target textures unless said, T0..T3 by their index. A
// row draws and reads back inside one mrtUntil() attempt (retried while mtld3d's pipelines build), or probes the back
// buffer through frames(); either way an attempt starts from mrtStart()'s state and ends with index 1-3 unbound.
static const DWORD PS_MRT2[] = {
    0xffff0200,                         // ps_2_0
    0x02000001, 0x800f0800, 0xa0e40000, // mov oC0, c0
    0x02000001, 0x800f0801, 0xa0e40001, // mov oC1, c1
    0x0000ffff};
static const DWORD PS_MRT4[] = {
    0xffff0200,                         // ps_2_0
    0x02000001, 0x800f0800, 0xa0e40000, // mov oC0, c0
    0x02000001, 0x800f0801, 0xa0e40001, // mov oC1, c1
    0x02000001, 0x800f0802, 0xa0e40002, // mov oC2, c2
    0x02000001, 0x800f0803, 0xa0e40003, // mov oC3, c3
    0x0000ffff};
static const DWORD TBLK = 0xff000000, TYLW = D3DCOLOR_XRGB(255, 255, 0);
static const float F_RED[4] = {1, 0, 0, 1}, F_GRN[4] = {0, 1, 0, 1}, F_BLU[4] = {0, 0, 1, 1}, F_WHT[4] = {1, 1, 1, 1},
                   F_MAG[4] = {1, 0, 1, 1}, F_YLW[4] = {1, 1, 0, 1};

// A render-target texture and its level 0.
struct Rt {
    Com<IDirect3DTexture9> t;
    Com<IDirect3DSurface9> s;
};
static bool mkRt(Rt &r, UINT size = 64, D3DFORMAT f = D3DFMT_A8R8G8B8) {
    return makeRt(r.t, size, 1, f) && levelOf(r.t.p, 0, r.s);
}
static bool bindRt(DWORD slot, IDirect3DSurface9 *s) {
    CK(SetRenderTarget, slot, s);
    return true;
}
// `s` alone at index 0 (which resets the viewport to it), cleared to `color`: for targets that start apart.
static bool clearRt(IDirect3DSurface9 *s, DWORD color) { return bindRt(0, s) && clearTo(color); }
// What an attempt starts from: nothing at index 1-3, no shader, and the states the mrt rows move at their defaults.
static bool mrtStart() {
    for (DWORD i = 1; i < 4; i++) CK(SetRenderTarget, i, nullptr);
    CK(SetVertexShader, nullptr);
    CK(SetPixelShader, nullptr);
    static const DWORD rs[][2] = {
        {D3DRS_COLORWRITEENABLE, 0xf}, {D3DRS_COLORWRITEENABLE1, 0xf}, {D3DRS_COLORWRITEENABLE2, 0xf},
        {D3DRS_COLORWRITEENABLE3, 0xf}, {D3DRS_ALPHABLENDENABLE, FALSE}, {D3DRS_SRGBWRITEENABLE, FALSE},
        {D3DRS_SCISSORTESTENABLE, FALSE}, {D3DRS_ZENABLE, D3DZB_FALSE}, {D3DRS_BLENDFACTOR, 0xffffffff}};
    for (const auto &s : rs) CK(SetRenderState, (D3DRENDERSTATETYPE)s[0], s[1]);
    return true;
}
// attempt() from mrtStart() through until(), index 1-3 unbound after each attempt (until() rebinds index 0 alone).
template <class F> static bool mrtUntil(F attempt) {
    return until([&] {
        const bool ok = mrtStart() && attempt();
        for (DWORD i = 1; i < 4; i++) dev->SetRenderTarget(i, nullptr);
        return ok;
    });
}
// The program `p` with c0.. from the n rows of `c`, over a quad covering the viewport at depth z.
static bool mrtDraw(const Prog &p, const float (*c)[4], int n, float z = 0.5f) {
    V3 q[4];
    quad(q, -1, -1, 1, 1);
    for (V3 &v : q) v.z = z;
    if (!use(p)) return false;
    CK(SetPixelShaderConstantF, 0, c[0], n);
    return strip(q);
}
static bool mrtDraw2(const Prog &p, const float *c0, const float *c1, float z = 0.5f) {
    float c[2][4];
    memcpy(c[0], c0, sizeof c[0]), memcpy(c[1], c1, sizeof c[1]);
    return mrtDraw(p, c, 2, z);
}
// Texels of the size x size 4-byte target `s` (format f), each within 1 a channel.
struct Tx { int x, y; DWORD want; };
static bool rtHas(const char *name, IDirect3DSurface9 *s, UINT size, D3DFORMAT f, std::initializer_list<Tx> px) {
    return readTarget(s, size, size, f, [&](const BYTE *b, int pitch) {
        for (const Tx &t : px) {
            char what[64];
            snprintf(what, sizeof what, "%s (%d,%d)", name, t.x, t.y);
            if (!want(what, texel(b, pitch, t.x, t.y), t.want)) return false;
        }
        return true;
    });
}
// One texel of the half or float target `s` (format f, `bytes` a texel): its first `channels` within 0.004.
static bool rtHasF(const char *name, IDirect3DSurface9 *s, UINT size, D3DFORMAT f, int bytes, int x, int y,
                   const float *expect, int channels) {
    return readTarget(s, size, size, f, [&](const BYTE *b, int pitch) {
        float got[4];
        decodeTexel(f, b + y * pitch + x * bytes, got);
        for (int i = 0; i < channels; i++)
            if (fabsf(got[i] - expect[i]) > 0.004f)
                return snprintf(why, sizeof why, "%s (%d,%d) %.3f %.3f %.3f %.3f want channel %d %.3f", name, x, y,
                                got[0], got[1], got[2], got[3], i, expect[i]),
                       false;
        return true;
    });
}
static const D3DFORMAT A8 = D3DFMT_A8R8G8B8, X8 = D3DFMT_X8R8G8B8;

// Two targets, a ps_2_0 writing oC0 and oC1: each takes its own colour; index 1 is read back while still bound.
static bool rowMrtTwo() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_MRT2)) return false;
    return mrtUntil([&] {
        Rt a, b;
        if (!mkRt(a) || !mkRt(b) ||
            !scene([&] { return bindRt(0, a.s.p) && bindRt(1, b.s.p) && clearTo(TGRN) && mrtDraw2(p, F_MAG, F_YLW); }))
            return false;
        Com<IDirect3DSurface9> got;
        CK(GetRenderTarget, 1, &got.p);
        if (got.p != b.s.p) return snprintf(why, sizeof why, "GetRenderTarget(1) answers another surface"), false;
        return rtHas("T1 bound", b.s.p, 64, A8, {{32, 32, 0xffff00}, {2, 60, 0xffff00}}) &&
               rtHas("T0", a.s.p, 64, A8, {{32, 32, 0xff00ff}});
    });
}
// Four targets of four kinds (a texture, a CreateRenderTarget surface, a cube's face 3, level 1 of a 128 texture), a
// ps_2_0 writing oC0..oC3; COLORWRITEENABLE3 lets green and alpha through on index 3 alone.
static bool rowMrtFour() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_MRT4)) return false;
    return mrtUntil([&] {
        Rt a;
        Com<IDirect3DSurface9> s1, s2, s3;
        Com<IDirect3DCubeTexture9> cube;
        Com<IDirect3DTexture9> mips;
        if (!mkRt(a) || !makeRt(mips, 128, 2) || !levelOf(mips.p, 1, s3)) return false;
        CK(CreateRenderTarget, 64, 64, A8, D3DMULTISAMPLE_NONE, 0, FALSE, &s1.p, nullptr);
        CK(CreateCubeTexture, 64, 1, D3DUSAGE_RENDERTARGET, A8, D3DPOOL_DEFAULT, &cube.p, nullptr);
        const HRESULT hr = cube.p->GetCubeMapSurface(D3DCUBEMAP_FACE_NEGATIVE_Y, 0, &s2.p);
        if (FAILED(hr)) return fail("GetCubeMapSurface", hr);
        const float c[4][4] = {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}, {1, 1, 1, 1}};
        if (!scene([&] {
                return bindRt(0, a.s.p) && bindRt(1, s1.p) && bindRt(2, s2.p) && bindRt(3, s3.p) && clearTo(TBLK) &&
                       rstate(D3DRS_COLORWRITEENABLE3, D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_ALPHA) &&
                       mrtDraw(p, c, 4);
            }))
            return false;
        return rtHas("T0", a.s.p, 64, A8, {{32, 32, 0xff0000}}) && rtHas("T1", s1.p, 64, A8, {{32, 32, 0x00ff00}}) &&
               rtHas("T2", s2.p, 64, A8, {{32, 32, 0x0000ff}}) && rtHas("T3", s3.p, 64, A8, {{32, 32, 0x00ff00}});
    });
}
// A hole: index 0 and 2 bound, 1 empty, index 2 a half-float target; COLORWRITEENABLE1 (nothing) is the empty slot's
// and COLORWRITEENABLE2 (red and alpha) index 2's; oC1 goes nowhere, oC2 to index 2.
static bool rowMrtHole() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_MRT4)) return false;
    return mrtUntil([&] {
        Rt a, h;
        const float c[4][4] = {{1, 0, 1, 1}, {1, 1, 0, 1}, {0.5f, 0.5f, 0.5f, 1}, {1, 1, 1, 1}};
        if (!mkRt(a) || !mkRt(h, 64, D3DFMT_A16B16G16R16F) ||
            !scene([&] {
                return bindRt(0, a.s.p) && bindRt(2, h.s.p) && clearTo(0) && rstate(D3DRS_COLORWRITEENABLE1, 0) &&
                       rstate(D3DRS_COLORWRITEENABLE2, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_ALPHA) &&
                       mrtDraw(p, c, 4);
            }))
            return false;
        const float half[4] = {0.5f, 0, 0, 1};
        return rtHas("T0", a.s.p, 64, A8, {{32, 32, 0xff00ff}}) &&
               rtHasF("T2", h.s.p, 64, D3DFMT_A16B16G16R16F, 8, 32, 32, half, 4);
    });
}
// A bound target the pixel stage does not write keeps its pixels: a ps_2_0 writing oC0 alone, then no pixel shader.
static bool rowMrtUnwritten() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_C0)) return false;
    return mrtUntil([&] {
        Rt a, b;
        V3 q[4];
        quad(q, -1, -1, 1, 1);
        if (!mkRt(a) || !mkRt(b) ||
            !scene([&] {
                if (!bindRt(0, a.s.p) || !bindRt(1, b.s.p) || !clearTo(TGRN) || !use(p)) return false;
                CK(SetPixelShaderConstantF, 0, F_RED, 1);
                return strip(q);
            }) ||
            !rtHas("T0 (ps)", a.s.p, 64, A8, {{32, 32, 0xff0000}}) ||
            !rtHas("T1 (ps)", b.s.p, 64, A8, {{32, 32, 0x00ff00}}))
            return false;
        RV r[4];
        quadRhw(r, 0, 0, 64, 64, TBLU);
        if (!scene([&] {
                CK(SetVertexShader, nullptr);
                CK(SetPixelShader, nullptr);
                return clearTo(TGRN) && rdraw(r);
            }))
            return false;
        return rtHas("T0 (fixed)", a.s.p, 64, A8, {{32, 32, 0x0000ff}}) &&
               rtHas("T1 (fixed)", b.s.p, 64, A8, {{32, 32, 0x00ff00}});
    });
}
// A ps_2_0 writing oC1 with nothing at index 1: index 0 is drawn.
static bool rowMrtNoBind() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_MRT2)) return false;
    return mrtUntil([&] {
        Rt a;
        if (!mkRt(a) || !scene([&] { return bindRt(0, a.s.p) && clearTo(TGRN) && mrtDraw2(p, F_MAG, F_YLW); }))
            return false;
        return rtHas("T0", a.s.p, 64, A8, {{32, 32, 0xff00ff}});
    });
}
// Clear reaches every bound target: whole, a rect, bounded by the viewport, under the scissor test, whatever the
// write masks; and not a target that was unbound.
static bool rowMrtClear() {
    return mrtUntil([&] {
        Rt a, b;
        const D3DRECT r = {8, 8, 24, 24};
        if (!mkRt(a) || !mkRt(b) ||
            !scene([&] {
                return bindRt(0, a.s.p) && bindRt(1, b.s.p) && clearTo(TRED) &&
                       clearRects(1, &r, D3DCLEAR_TARGET, TGRN) && viewport(32, 32, 16, 16) && clearTo(TBLU) &&
                       viewport(0, 0, 64, 64) && scissor(0, 48, 64, 64) && rstate(D3DRS_SCISSORTESTENABLE, TRUE) &&
                       clearTo(TWHT) && rstate(D3DRS_SCISSORTESTENABLE, FALSE);
            }))
            return false;
        for (int i = 0; i < 2; i++)
            if (!rtHas(i ? "T1" : "T0", i ? b.s.p : a.s.p, 64, A8,
                       {{16, 16, 0x00ff00}, {40, 40, 0x0000ff}, {32, 56, 0xffffff}, {28, 28, 0xff0000},
                        {56, 40, 0xff0000}, {20, 44, 0xff0000}}))
                return false;
        if (!scene([&] {
                return rstate(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED) &&
                       rstate(D3DRS_COLORWRITEENABLE1, D3DCOLORWRITEENABLE_RED) && clearTo(TYLW) &&
                       rstate(D3DRS_COLORWRITEENABLE, 0xf) && rstate(D3DRS_COLORWRITEENABLE1, 0xf);
            }) ||
            !rtHas("T0 masked", a.s.p, 64, A8, {{32, 32, 0xffff00}}) ||
            !rtHas("T1 masked", b.s.p, 64, A8, {{32, 32, 0xffff00}}))
            return false;
        if (!scene([&] { return bindRt(1, nullptr) && clearTo(TBLK); })) return false;
        return rtHas("T0 alone", a.s.p, 64, A8, {{32, 32, 0x000000}}) &&
               rtHas("T1 unbound", b.s.p, 64, A8, {{32, 32, 0xffff00}});
    });
}
// COLORWRITEENABLE1 masks index 1 alone, COLORWRITEENABLE index 0 alone; with index 0 masked off whole, index 1 is
// still drawn.
static bool rowMrtMask() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_MRT2)) return false;
    return mrtUntil([&] {
        Rt a, b;
        if (!mkRt(a) || !mkRt(b) ||
            !scene([&] {
                return bindRt(0, a.s.p) && bindRt(1, b.s.p) && clearTo(TBLK) &&
                       rstate(D3DRS_COLORWRITEENABLE1, D3DCOLORWRITEENABLE_RED) && mrtDraw2(p, F_WHT, F_WHT);
            }) ||
            !rtHas("T0", a.s.p, 64, A8, {{32, 32, 0xffffff}}) || !rtHas("T1 red", b.s.p, 64, A8, {{32, 32, 0xff0000}}))
            return false;
        if (!scene([&] {
                return clearTo(TBLK) && rstate(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_GREEN) &&
                       rstate(D3DRS_COLORWRITEENABLE1, 0xf) && mrtDraw2(p, F_WHT, F_WHT);
            }))
            return false;
        if (!rtHas("T0 green", a.s.p, 64, A8, {{32, 32, 0x00ff00}}) ||
            !rtHas("T1", b.s.p, 64, A8, {{32, 32, 0xffffff}}) ||
            !scene([&] {
                return clearTo(TBLK) && rstate(D3DRS_COLORWRITEENABLE, 0) && mrtDraw2(p, F_WHT, F_WHT);
            }))
            return false;
        return rtHas("T0 off", a.s.p, 64, A8, {{32, 32, 0x000000}}) &&
               rtHas("T1 alone", b.s.p, 64, A8, {{32, 32, 0xffffff}});
    });
}
// One blend state, each target its own destination and its own source alpha; the blend factor colour on both.
static bool rowMrtBlend() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_MRT2)) return false;
    return mrtUntil([&] {
        Rt a, b;
        const float half[4] = {0, 1, 0, 0.5f}, quarter[4] = {1, 1, 1, 0.25f};
        if (!mkRt(a) || !mkRt(b) ||
            !scene([&] {
                return clearRt(b.s.p, TBLU) && clearRt(a.s.p, TRED) && bindRt(1, b.s.p) &&
                       rstate(D3DRS_ALPHABLENDENABLE, TRUE) && rstate(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA) &&
                       rstate(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA) && mrtDraw2(p, half, quarter);
            }) ||
            !rtHas("T0 alpha", a.s.p, 64, A8, {{32, 32, 0x808000}}) ||
            !rtHas("T1 alpha", b.s.p, 64, A8, {{32, 32, 0x4040ff}}))
            return false;
        if (!scene([&] {
                return bindRt(1, nullptr) && clearRt(b.s.p, TBLU) && clearRt(a.s.p, TRED) && bindRt(1, b.s.p) &&
                       rstate(D3DRS_SRCBLEND, D3DBLEND_BLENDFACTOR) && rstate(D3DRS_DESTBLEND, D3DBLEND_INVBLENDFACTOR) &&
                       rstate(D3DRS_BLENDFACTOR, 0x80808080) && mrtDraw2(p, F_WHT, F_WHT);
            }))
            return false;
        return rtHas("T0 factor", a.s.p, 64, A8, {{32, 32, 0xff8080}}) &&
               rtHas("T1 factor", b.s.p, 64, A8, {{32, 32, 0x8080ff}});
    });
}
// DESTALPHA/INVDESTALPHA over a clear of alpha 0x40: a target with alpha blends by it, one without takes the source
// (factors 1 and 0), whichever index each is at.
static bool rowMrtDestAlpha() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_MRT2)) return false;
    for (int swap = 0; swap < 2; swap++) {
        const D3DFORMAT fa = swap ? X8 : A8, fb = swap ? A8 : X8;
        if (!mrtUntil([&] {
                Rt a, b;
                const DWORD clear = 0x40285078;
                if (!mkRt(a, 64, fa) || !mkRt(b, 64, fb) ||
                    !scene([&] {
                        return clearRt(b.s.p, clear) && clearRt(a.s.p, clear) && bindRt(1, b.s.p) &&
                               rstate(D3DRS_ALPHABLENDENABLE, TRUE) && rstate(D3DRS_SRCBLEND, D3DBLEND_DESTALPHA) &&
                               rstate(D3DRS_DESTBLEND, D3DBLEND_INVDESTALPHA) && mrtDraw2(p, F_WHT, F_WHT);
                    }))
                    return false;
                return rtHas(swap ? "T0 X8" : "T0 A8", a.s.p, 64, fa, {{32, 32, swap ? 0xffffffu : 0x5e7c9au}}) &&
                       rtHas(swap ? "T1 A8" : "T1 X8", b.s.p, 64, fb, {{32, 32, swap ? 0x5e7c9au : 0xffffffu}});
            }))
            return false;
    }
    return true;
}
// Targets of three formats in one pass: 8-bit, half float (values above 1 kept) and one-channel float.
static bool rowMrtFormats() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_MRT4)) return false;
    return mrtUntil([&] {
        Rt a, b, r;
        const float c[4][4] = {{0.25f, 0.5f, 0.75f, 1}, {2.5f, 0.5f, 0.25f, 1}, {3.5f, 0, 0, 1}, {1, 1, 1, 1}};
        if (!mkRt(a) || !mkRt(b, 64, D3DFMT_A16B16G16R16F) || !mkRt(r, 64, D3DFMT_R32F) ||
            !scene([&] {
                return bindRt(0, a.s.p) && bindRt(1, b.s.p) && bindRt(2, r.s.p) && clearTo(TBLK) && mrtDraw(p, c, 4);
            }))
            return false;
        return rtHas("T0", a.s.p, 64, A8, {{32, 32, 0x4080bf}}) &&
               rtHasF("T1", b.s.p, 64, D3DFMT_A16B16G16R16F, 8, 32, 32, c[1], 4) &&
               rtHasF("T2", r.s.p, 64, D3DFMT_R32F, 4, 32, 32, c[2], 1);
    });
}
// sRGB write over two targets, a black quad at alpha 0.5 over a 128 grey Clear. Two A8R8G8B8 targets: the blend is
// linear and encoded (137) on both. With a half-float target at index 1 (no sRGB view) the whole set takes the pixel
// stage's encode, before the blender: 94 and 0.368 (mtld3d's rule, kept: DXMTRS C4b-3d4 ruling 10).
static bool rowMrtSrgb() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_MRT2)) return false;
    for (int mixed = 0; mixed < 2; mixed++) {
        const D3DFORMAT fb = mixed ? D3DFMT_A16B16G16R16F : A8;
        if (!mrtUntil([&] {
                Rt a, b;
                const float black[4] = {0, 0, 0, 0.5f};
                if (!mkRt(a) || !mkRt(b, 64, fb) ||
                    !scene([&] {
                        return bindRt(0, a.s.p) && bindRt(1, b.s.p) && rstate(D3DRS_SRGBWRITEENABLE, TRUE) &&
                               clearTo(D3DCOLOR_XRGB(128, 128, 128)) && rstate(D3DRS_ALPHABLENDENABLE, TRUE) &&
                               rstate(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA) &&
                               rstate(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA) && mrtDraw2(p, black, black);
                    }))
                    return false;
                if (!mixed)
                    return rtHas("T0 twins", a.s.p, 64, A8, {{32, 32, 0x898989}}) &&
                           rtHas("T1 twins", b.s.p, 64, A8, {{32, 32, 0x898989}});
                const float f[4] = {0.368f, 0.368f, 0.368f, 0};
                return rtHas("T0 mixed", a.s.p, 64, A8, {{32, 32, 0x5e5e5e}}) &&
                       rtHasF("T1 mixed", b.s.p, 64, fb, 8, 32, 32, f, 3);
            }))
            return false;
    }
    return true;
}
// Clear under sRGB write encodes each target once: an 8-bit target beside a float one (188 and 0.737, a rect to 64
// reading 137 and 0.538), and beside an 8-bit one of another size (188 on both).
static bool rowMrtSrgbClear() {
    return mrtUntil([&] {
               Rt a, r;
               const D3DRECT rect = {8, 8, 24, 24};
               if (!mkRt(a) || !mkRt(r, 64, D3DFMT_R32F) ||
                   !scene([&] {
                       return bindRt(0, a.s.p) && bindRt(1, r.s.p) && rstate(D3DRS_SRGBWRITEENABLE, TRUE) &&
                              clearTo(D3DCOLOR_XRGB(128, 128, 128)) &&
                              clearRects(1, &rect, D3DCLEAR_TARGET, D3DCOLOR_XRGB(64, 64, 64));
                   }))
                   return false;
               const float whole[4] = {0.737f, 0, 0, 0}, part[4] = {0.538f, 0, 0, 0};
               return rtHas("T0", a.s.p, 64, A8, {{40, 40, 0xbcbcbc}, {16, 16, 0x898989}}) &&
                      rtHasF("T1 whole", r.s.p, 64, D3DFMT_R32F, 4, 40, 40, whole, 1) &&
                      rtHasF("T1 rect", r.s.p, 64, D3DFMT_R32F, 4, 16, 16, part, 1);
           }) &&
           mrtUntil([&] {
               Rt a, s;
               if (!mkRt(a) || !mkRt(s, 32) ||
                   !scene([&] {
                       return bindRt(0, a.s.p) && bindRt(1, s.s.p) && rstate(D3DRS_SRGBWRITEENABLE, TRUE) &&
                              clearTo(D3DCOLOR_XRGB(128, 128, 128));
                   }))
                   return false;
               return rtHas("T0 64", a.s.p, 64, A8, {{40, 40, 0xbcbcbc}}) &&
                      rtHas("T1 32", s.s.p, 32, A8, {{16, 16, 0xbcbcbc}});
           });
}
// A target of another size than index 0's is bound, skipped by draws and reached by Clear (over its own extent); it
// takes part once index 0 has its size.
static bool rowMrtSize() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_MRT2)) return false;
    return mrtUntil([&] {
               Rt a, b, u;
               const D3DRECT r = {8, 8, 48, 48};
               if (!mkRt(a) || !mkRt(b, 32) || !mkRt(u, 32) ||
                   !scene([&] {
                       return bindRt(0, a.s.p) && bindRt(1, b.s.p) && clearTo(TGRN) && mrtDraw2(p, F_RED, F_BLU);
                   }) ||
                   !rtHas("T0 drawn", a.s.p, 64, A8, {{48, 48, 0xff0000}, {8, 8, 0xff0000}}) ||
                   !rtHas("T1 skipped", b.s.p, 32, A8, {{16, 16, 0x00ff00}}) ||
                   !scene([&] { return clearRects(1, &r, D3DCLEAR_TARGET, TWHT); }) ||
                   !rtHas("T0 rect", a.s.p, 64, A8, {{40, 40, 0xffffff}, {56, 56, 0xff0000}}) ||
                   !rtHas("T1 rect", b.s.p, 32, A8, {{24, 24, 0xffffff}, {4, 4, 0x00ff00}}) ||
                   !scene([&] { return bindRt(0, u.s.p) && mrtDraw2(p, F_RED, F_BLU); }) ||
                   !rtHas("a 32 index 0", u.s.p, 32, A8, {{16, 16, 0xff0000}}) ||
                   !rtHas("T1 joined", b.s.p, 32, A8, {{16, 16, 0x0000ff}}) ||
                   !scene([&] { return bindRt(0, a.s.p) && mrtDraw2(p, F_YLW, F_MAG); }))
                   return false;
               return rtHas("T0 again", a.s.p, 64, A8, {{32, 32, 0xffff00}}) &&
                      rtHas("T1 left", b.s.p, 32, A8, {{16, 16, 0x0000ff}});
           }) &&
           mrtUntil([&] {
               Rt a, d;
               if (!mkRt(a) || !mkRt(d, 128) ||
                   !scene([&] {
                       return clearRt(d.s.p, TGRN) && clearRt(a.s.p, TGRN) && bindRt(1, d.s.p) &&
                              mrtDraw2(p, F_RED, F_BLU);
                   }) ||
                   !rtHas("T0 drawn (128)", a.s.p, 64, A8, {{32, 32, 0xff0000}}) ||
                   !rtHas("T1 128 skipped", d.s.p, 128, A8, {{32, 32, 0x00ff00}, {100, 100, 0x00ff00}}) ||
                   !scene([&] { return clearTo(TWHT); }))
                   return false;
               return rtHas("T0 cleared", a.s.p, 64, A8, {{32, 32, 0xffffff}}) &&
                      rtHas("T1 128 cleared", d.s.p, 128, A8, {{32, 32, 0xffffff}, {100, 100, 0x00ff00}});
           });
}
// Index 1 rebound between two draws, with nothing else between them: each target has its own draw (two passes that
// differ at index 1 alone are two passes); then index 0 changed with index 1 kept.
static bool rowMrtSwitch() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_MRT2)) return false;
    return mrtUntil([&] {
        Rt t0, a, b, t1;
        if (!mkRt(t0) || !mkRt(a) || !mkRt(b) || !mkRt(t1) ||
            !scene([&] {
                return clearRt(a.s.p, TBLK) && clearRt(b.s.p, TBLK) && clearRt(t0.s.p, TBLK) && bindRt(1, a.s.p) &&
                       mrtDraw2(p, F_RED, F_GRN) && bindRt(1, b.s.p) && mrtDraw2(p, F_RED, F_BLU);
            }) ||
            !rtHas("A", a.s.p, 64, A8, {{32, 32, 0x00ff00}}) || !rtHas("B", b.s.p, 64, A8, {{32, 32, 0x0000ff}}) ||
            !scene([&] { return bindRt(0, t1.s.p) && mrtDraw2(p, F_WHT, F_YLW); }))
            return false;
        return rtHas("the new index 0", t1.s.p, 64, A8, {{32, 32, 0xffffff}}) &&
               rtHas("B kept", b.s.p, 64, A8, {{32, 32, 0xffff00}});
    });
}
// The scissor test and a partial viewport cut every target alike.
static bool rowMrtScissor() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_MRT2)) return false;
    return mrtUntil([&] {
        Rt a, b;
        if (!mkRt(a) || !mkRt(b) ||
            !scene([&] {
                return bindRt(0, a.s.p) && bindRt(1, b.s.p) && clearTo(TBLU) && scissor(16, 16, 48, 48) &&
                       rstate(D3DRS_SCISSORTESTENABLE, TRUE) && mrtDraw2(p, F_RED, F_GRN) &&
                       rstate(D3DRS_SCISSORTESTENABLE, FALSE);
            }) ||
            !rtHas("T0 scissor", a.s.p, 64, A8, {{32, 32, 0xff0000}, {8, 8, 0x0000ff}, {15, 32, 0x0000ff}}) ||
            !rtHas("T1 scissor", b.s.p, 64, A8, {{32, 32, 0x00ff00}, {8, 8, 0x0000ff}, {48, 32, 0x0000ff}}) ||
            !scene([&] { return clearTo(TBLU) && viewport(0, 0, 32, 64) && mrtDraw2(p, F_RED, F_GRN); }))
            return false;
        return rtHas("T0 viewport", a.s.p, 64, A8, {{16, 32, 0xff0000}, {48, 32, 0x0000ff}}) &&
               rtHas("T1 viewport", b.s.p, 64, A8, {{16, 32, 0x00ff00}, {48, 32, 0x0000ff}});
    });
}
// The depth test with two targets (256x256, the auto depth-stencil's size): a far quad is hidden behind a near one on
// both.
static bool rowMrtDepth() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_MRT2)) return false;
    return mrtUntil([&] {
        Rt a, b;
        if (!mkRt(a, 256) || !mkRt(b, 256) ||
            !scene([&] {
                if (!bindRt(0, a.s.p) || !bindRt(1, b.s.p)) return false;
                CK(Clear, 0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, TBLK, 1.0f, 0);
                return rstate(D3DRS_ZENABLE, D3DZB_TRUE) && viewport(0, 0, 128, 256) && mrtDraw2(p, F_RED, F_GRN, 0.2f) &&
                       viewport(0, 0, 256, 256) && mrtDraw2(p, F_WHT, F_WHT, 0.8f) && rstate(D3DRS_ZENABLE, D3DZB_FALSE);
            }))
            return false;
        return rtHas("T0", a.s.p, 256, A8, {{64, 128, 0xff0000}, {192, 128, 0xffffff}}) &&
               rtHas("T1", b.s.p, 256, A8, {{64, 128, 0x00ff00}, {192, 128, 0xffffff}});
    });
}
// The back buffer in a set: at index 0 with a texture at index 1 (drawn, presented, the texture then shown), and at
// index 1 under a texture, where it blends by its stored alpha as it does at index 0 (DXMTRS C4b-3d3 ruling 10).
static bool rowMrtBack() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_MRT2)) return false;
    const Px px[] = {{32, 32, TRED}, {129, 129, TGRN}};
    if (!frames(BLUE, px, 2, [&] {
            Rt b;
            if (!mrtStart() || !mkRt(b, 256) || !bindRt(1, b.s.p) || !mrtDraw2(p, F_RED, F_GRN) || !bindRt(1, nullptr))
                return false;
            CK(SetVertexShader, nullptr);
            CK(SetPixelShader, nullptr);
            return show(b.t.p);
        }))
        return false;
    return mrtUntil([&] {
        Rt a;
        const DWORD clear = 0x40285078;
        if (!mkRt(a, 256) ||
            !scene([&] {
                return clearRt(rtSurf, clear) && clearRt(a.s.p, clear) && bindRt(1, rtSurf) &&
                       rstate(D3DRS_ALPHABLENDENABLE, TRUE) && rstate(D3DRS_SRCBLEND, D3DBLEND_DESTALPHA) &&
                       rstate(D3DRS_DESTBLEND, D3DBLEND_INVDESTALPHA) && mrtDraw2(p, F_WHT, F_WHT);
            }))
            return false;
        return rtHas("the texture", a.s.p, 256, A8, {{128, 128, 0x5e7c9a}}) &&
               rtHas("the back buffer at index 1", rtSurf, 256, X8, {{128, 128, 0x5e7c9a}});
    });
}
// An AUTOGENMIPMAP target at index 1: its lower levels follow level 0 once it leaves the slot, by an unbind or by
// another target taking the slot (an 8x8 quad samples level 3: red only if regenerated, black left alone).
static bool rowMrtAutogen() {
    const Px px[] = {{128, 128, TRED}, {32, 32, BLUE}};
    for (int swap = 0; swap < 2; swap++)
        if (!frames(BLUE, px, 2, [&] {
                Rt a, other;
                Com<IDirect3DTexture9> g;
                Com<IDirect3DSurface9> gs;
                if (!mrtStart() || !mkRt(a) || !mkRt(other) ||
                    !makeRt(g, 64, 0, X8, D3DUSAGE_RENDERTARGET | D3DUSAGE_AUTOGENMIPMAP) || !levelOf(g.p, 0, gs) ||
                    !bindRt(0, a.s.p) || !bindRt(1, gs.p) || !clearTo(TRED) || !bindRt(1, swap ? other.s.p : nullptr) ||
                    !bindRt(1, nullptr) || !backToScreen())
                    return false;
                CK(SetSamplerState, 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
                CK(SetSamplerState, 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
                CK(SetSamplerState, 0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
                return show(g.p, 124, 124, 132, 132);
            }))
            return false;
    return true;
}
// A rect Clear between two draws keeps its place on both targets.
static bool rowMrtOrder() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_MRT2)) return false;
    return mrtUntil([&] {
        Rt a, b;
        const D3DRECT left = {0, 0, 32, 64};
        if (!mkRt(a) || !mkRt(b) ||
            !scene([&] {
                return bindRt(0, a.s.p) && bindRt(1, b.s.p) && mrtDraw2(p, F_RED, F_RED) &&
                       clearRects(1, &left, D3DCLEAR_TARGET, TGRN) && viewport(0, 0, 64, 32) &&
                       mrtDraw2(p, F_WHT, F_WHT) && viewport(0, 0, 64, 64);
            }))
            return false;
        for (int i = 0; i < 2; i++)
            if (!rtHas(i ? "T1" : "T0", i ? b.s.p : a.s.p, 64, A8,
                       {{16, 16, 0xffffff}, {16, 48, 0x00ff00}, {48, 48, 0xff0000}, {48, 16, 0xffffff}}))
                return false;
        return true;
    });
}
// A read-back of index 1 between two draws of one set: it sees the first draw, and the second still reaches both.
static bool rowMrtRead() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_MRT2)) return false;
    return mrtUntil([&] {
        Rt a, b;
        if (!mkRt(a) || !mkRt(b) ||
            !scene([&] { return bindRt(0, a.s.p) && bindRt(1, b.s.p) && mrtDraw2(p, F_RED, F_GRN); }) ||
            !rtHas("T1 first", b.s.p, 64, A8, {{32, 32, 0x00ff00}}) ||
            !scene([&] { return mrtDraw2(p, F_BLU, F_YLW); }))
            return false;
        return rtHas("T0", a.s.p, 64, A8, {{32, 32, 0x0000ff}}) && rtHas("T1", b.s.p, 64, A8, {{32, 32, 0xffff00}});
    });
}

static void mrtRows(IDirect3DDevice9 *d, bool strict) {
    static const struct { const char *name; bool (*run)(); } rows[] = {
        {"mrt-two", rowMrtTwo},       {"mrt-four", rowMrtFour},         {"mrt-hole", rowMrtHole},
        {"mrt-unwritten", rowMrtUnwritten}, {"mrt-nobind", rowMrtNoBind}, {"mrt-clear", rowMrtClear},
        {"mrt-mask", rowMrtMask},     {"mrt-blend", rowMrtBlend},       {"mrt-destalpha", rowMrtDestAlpha},
        {"mrt-formats", rowMrtFormats}, {"mrt-srgb", rowMrtSrgb},       {"mrt-srgbclear", rowMrtSrgbClear},
        {"mrt-size", rowMrtSize},     {"mrt-switch", rowMrtSwitch},     {"mrt-scissor", rowMrtScissor},
        {"mrt-depth", rowMrtDepth},   {"mrt-back", rowMrtBack},         {"mrt-autogen", rowMrtAutogen},
        {"mrt-order", rowMrtOrder},   {"mrt-read", rowMrtRead}};
    dev = d, strictMode = strict, budgetEnd = GetTickCount64() + 180000;
    HRESULT hr = dev->GetRenderTarget(0, &rtSurf);
    if (SUCCEEDED(hr))
        hr = dev->CreateOffscreenPlainSurface(256, 256, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sysSurf, nullptr);
    if (SUCCEEDED(hr)) hr = dev->GetDepthStencilSurface(&dsSurf);
    for (const auto &r : rows) {
        why[0] = 0;
        bool ok;
        if (FAILED(hr)) ok = fail("GetRenderTarget/GetDepthStencilSurface", hr);
        else if (GetTickCount64() >= budgetEnd) ok = false, snprintf(why, sizeof why, "budget");
        else ok = reset() && depthReset() && blendStart() && r.run();
        row(r.name, ok, why);
    }
    if (dsSurf) dsSurf->Release();
    if (sysSurf) sysSurf->Release();
    if (rtSurf) rtSurf->Release();
}

// --- The "msaa" mode (DXMTRS C4b-3d5): multisampling, on a device whose X8R8G8B8 back buffer and auto D24S8 have 4
// samples (Z off unless a row turns it on). "T" is a 64x64 A8R8G8B8 CreateRenderTarget surface of 4 samples unless
// said; it is read by resolveInto() a new single-sample render-target texture (StretchRect, whole surfaces), then
// GetRenderTargetData of that. No row uses frames(): it reads the back buffer with GetRenderTargetData, which a
// multisampled one refuses. A row draws and reads inside one msUntil() attempt (retried while mtld3d's pipelines
// build), which starts from msStart()'s state and ends with index 1-3 unbound, the auto depth-stencil bound and
// msStart()'s state again. Quads are pretransformed; a probe sits 8 pixels or more from a quad's edge (a D3D9 quad
// edge on a pixel boundary halves the edge pixels' samples). A value coverage decides is a band, 48..208 (DXMTRS
// C4b-3d5 ruling 7), of one texel or of the mean of a 4x4 block; a blended or coverage quad's alpha is its diffuse
// alpha. Under D3DRS_MULTISAMPLEMASK a Clear is made with the mask at 0xffffffff.
static const DWORD D3DFMT_ATOC = MAKEFOURCC('A', 'T', 'O', 'C'), FOURCC_A2M1 = MAKEFOURCC('A', '2', 'M', '1'),
                   FOURCC_A2M0 = MAKEFOURCC('A', '2', 'M', '0');
// A size x size render-target surface of `type` samples at `quality`, not lockable.
static bool msRt(Com<IDirect3DSurface9> &s, UINT size = 64, D3DFORMAT f = A8,
                 D3DMULTISAMPLE_TYPE type = D3DMULTISAMPLE_4_SAMPLES, DWORD quality = 0) {
    CK(CreateRenderTarget, size, size, f, type, quality, FALSE, &s.p, nullptr);
    return true;
}
// `src` resolved into `dst`, a new single-sample size x size render-target texture of format f, which rtHas then reads.
static bool resolveInto(IDirect3DSurface9 *src, Rt &dst, UINT size = 64, D3DFORMAT f = A8) {
    if (!mkRt(dst, size, f)) return false;
    CK(StretchRect, src, nullptr, dst.s.p, nullptr, D3DTEXF_NONE);
    return true;
}
// rtHas of the square 4-byte target `s`, through resolveInto() when it is multisampled.
static bool msHas(const char *name, IDirect3DSurface9 *s, std::initializer_list<Tx> px) {
    D3DSURFACE_DESC d = {};
    const HRESULT hr = s->GetDesc(&d);
    if (FAILED(hr)) return fail("GetDesc", hr);
    if (d.MultiSampleType == D3DMULTISAMPLE_NONE) return rtHas(name, s, d.Width, d.Format, px);
    Rt r;
    return resolveInto(s, r, d.Width, d.Format) && rtHas(name, r.s.p, d.Width, d.Format, px);
}
// Whether `got` is within lo..hi; `why` names it otherwise.
static bool inBand(const char *what, int got, int lo = 48, int hi = 208) {
    if (got >= lo && got <= hi) return true;
    snprintf(why, sizeof why, "%s %d want %d..%d", what, got, lo, hi);
    return false;
}
// The mean of the colour channels of the n x n block from texel (x, y) of the square 4-byte target `s` (format f);
// -1, with `why` set, when it cannot be read.
static int meanOf(IDirect3DSurface9 *s, D3DFORMAT f, int x, int y, int n) {
    D3DSURFACE_DESC d = {};
    const HRESULT hr = s->GetDesc(&d);
    if (FAILED(hr)) return fail("GetDesc", hr), -1;
    int sum = 0;
    if (!readTarget(s, d.Width, d.Height, f, [&](const BYTE *b, int pitch) {
            for (int j = y; j < y + n; j++)
                for (int i = x; i < x + n; i++) {
                    const DWORD v = texel(b, pitch, i, j);
                    sum += (int)(v >> 16 & 0xff) + (int)(v >> 8 & 0xff) + (int)(v & 0xff);
                }
            return true;
        }))
        return -1;
    return sum / (3 * n * n);
}
// Whether the adapter offers `type` for a windowed A8R8G8B8 target (CheckDeviceMultiSampleType), asked of main's
// IDirect3D9 (the core refuses GetDirect3D, which would hide the 2- and 8-sample cells from it).
static IDirect3D9 *direct3d;
static bool offers(D3DMULTISAMPLE_TYPE type) {
    return direct3d && SUCCEEDED(direct3d->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, A8, TRUE,
                                                                      type, nullptr));
}
// What an msaa attempt starts from: mrtStart()'s state, no sample mask, alpha-to-coverage off (ADAPTIVETESS_Y 0, the
// A2M latch cleared by A2M0, then the numeric point size 1: 1.0 alone keeps the latch), the alpha test off and stage
// 0's alpha the diffuse alpha. reset() restores none of the first three.
static bool msStart() {
    const DWORD rs[][2] = {{D3DRS_MULTISAMPLEMASK, 0xffffffff}, {D3DRS_ADAPTIVETESS_Y, 0},
                           {D3DRS_POINTSIZE, FOURCC_A2M0},      {D3DRS_POINTSIZE, fbits(1.0f)},
                           {D3DRS_ALPHATESTENABLE, FALSE},      {D3DRS_ALPHAFUNC, D3DCMP_ALWAYS},
                           {D3DRS_ALPHAREF, 0}};
    if (!mrtStart()) return false;
    for (const auto &s : rs) CK(SetRenderState, (D3DRENDERSTATETYPE)s[0], s[1]);
    CK(SetTextureStageState, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    return true;
}
// attempt() from msStart() through until(); after each attempt, passed or not, msStart()'s state again and the auto
// depth-stencil bound (until() rebinds the back buffer at index 0).
template <class F> static bool msUntil(F attempt) {
    return until([&] {
        const bool ok = msStart() && attempt();
        msStart();
        dev->SetDepthStencilSurface(dsSurf);
        return ok;
    });
}

// ms-desc: the back buffer and the auto depth-stencil say 4 samples; GetRenderTargetData of the back buffer, 16-sample
// creates (mtld3d's creators answer D3DERR_INVALIDCALL for every reject: parity) and a StretchRect from no surface are
// D3DERR_INVALIDCALL in every lane; a depth-stencil StretchRect (the auto one into a 4-sample D24S8 surface) is past
// the core's resolve shape: D3DERR_NOTAVAILABLE under core (strict), any answer elsewhere.
static bool rowMsDesc() {
    D3DSURFACE_DESC b = {}, d = {};
    HRESULT hr = rtSurf->GetDesc(&b);
    if (SUCCEEDED(hr)) hr = dsSurf->GetDesc(&d);
    if (FAILED(hr)) return fail("GetDesc", hr);
    if (b.MultiSampleType != D3DMULTISAMPLE_4_SAMPLES || d.MultiSampleType != D3DMULTISAMPLE_4_SAMPLES)
        return snprintf(why, sizeof why, "back buffer %d samples, depth-stencil %d", (int)b.MultiSampleType,
                        (int)d.MultiSampleType),
               false;
    hr = dev->GetRenderTargetData(rtSurf, sysSurf);
    if (hr != D3DERR_INVALIDCALL) return fail("GetRenderTargetData (the back buffer)", hr);
    Com<IDirect3DSurface9> rt16, ds16, ds;
    hr = dev->CreateRenderTarget(64, 64, A8, D3DMULTISAMPLE_16_SAMPLES, 0, FALSE, &rt16.p, nullptr);
    if (hr != D3DERR_INVALIDCALL) return fail("CreateRenderTarget (16 samples)", hr);
    hr = dev->CreateDepthStencilSurface(64, 64, D3DFMT_D24S8, D3DMULTISAMPLE_16_SAMPLES, 0, FALSE, &ds16.p, nullptr);
    if (hr != D3DERR_INVALIDCALL) return fail("CreateDepthStencilSurface (16 samples)", hr);
    hr = dev->StretchRect(nullptr, nullptr, rtSurf, nullptr, D3DTEXF_NONE);
    if (hr != D3DERR_INVALIDCALL) return fail("StretchRect (no source)", hr);
    CK(CreateDepthStencilSurface, 256, 256, D3DFMT_D24S8, D3DMULTISAMPLE_4_SAMPLES, 0, FALSE, &ds.p, nullptr);
    hr = dev->StretchRect(dsSurf, nullptr, ds.p, nullptr, D3DTEXF_NONE);
    if (strictMode && hr != D3DERR_NOTAVAILABLE) return fail("StretchRect (depth-stencil)", hr);
    return true;
}
// ms-resolve: T cleared green, a red quad over x < 32: read (16,32) red, (48,32) green.
static bool rowMsResolve() {
    return msUntil([&] {
        Com<IDirect3DSurface9> t;
        return msRt(t) && scene([&] { return bindRt(0, t.p) && clearTo(TGRN) && zquad(0, 0, 32, 64, 0.5f, TRED); }) &&
               msHas("T", t.p, {{16, 32, TRED}, {48, 32, TGRN}});
    });
}
// ms-edge: T cleared black, a white clip-space triangle over its lower left half, corners (-1,-1), (-1,1), (1,-1) in
// that order (clockwise on screen: the default cull keeps it): (8,56) white, (56,8) black. Its hypotenuse is the texel
// diagonal x = y, through the diagonal texels' centres, so 2 of 4 samples of each are in (128 under the standard
// pattern; 0 or 255 without multisampling): (31,31) in the band, and the 4x4 mean from (30,30) (6 texels in, 6 out,
// 4 diagonal: (6 * 255 + 4 * 128) / 16 = 128) in the band.
static bool rowMsEdge() {
    return msUntil([&] {
        Com<IDirect3DSurface9> t;
        Rt r;
        const FC tri[3] = {{-1, -1, 0.5f, TWHT}, {-1, 1, 0.5f, TWHT}, {1, -1, 0.5f, TWHT}};
        int mean = -1, diagonal = -1;
        if (!msRt(t) || !scene([&] {
                if (!bindRt(0, t.p) || !clearTo(TBLK)) return false;
                CK(SetFVF, FVF_C);
                CK(DrawPrimitiveUP, D3DPT_TRIANGLELIST, 1, tri, sizeof *tri);
                return true;
            }) ||
            !resolveInto(t.p, r) || !rtHas("T", r.s.p, 64, A8, {{8, 56, 0xffffff}, {56, 8, 0x000000}}) ||
            (mean = meanOf(r.s.p, A8, 30, 30, 4)) < 0 || (diagonal = meanOf(r.s.p, A8, 31, 31, 1)) < 0)
            return false;
        return inBand("the edge's 4x4 mean from (30,30)", mean) && inBand("the edge's texel (31,31)", diagonal);
    });
}
// ms-back: the multisampled back buffer cleared blue, a red quad over (64,64)-(192,192), Present D3D_OK; then cleared
// blue and drawn again (a presented DISCARD back buffer is undefined) and read through a 256 X8R8G8B8 target.
static bool rowMsBack() {
    return msUntil([&] {
        const auto frame = [&] {
            return scene([&] { return backToScreen() && clearTo(TBLU) && zquad(64, 64, 192, 192, 0.5f, TRED); });
        };
        if (!frame()) return false;
        CK(Present, nullptr, nullptr, nullptr, nullptr);
        return frame() && msHas("the back buffer", rtSurf, {{128, 128, TRED}, {32, 32, TBLU}});
    });
}
// ms-clear: T cleared red whole, green in the rect (8,8)-(24,24), blue through the viewport (32,32,16,16) with no rect:
// each texel on either side of the rect's edges exact.
static bool rowMsClear() {
    return msUntil([&] {
        Com<IDirect3DSurface9> t;
        const D3DRECT rect = {8, 8, 24, 24};
        return msRt(t) && scene([&] {
                   return bindRt(0, t.p) && clearTo(TRED) && clearRects(1, &rect, D3DCLEAR_TARGET, TGRN) &&
                          viewport(32, 32, 16, 16) && clearTo(TBLU);
               }) &&
               msHas("T", t.p, {{16, 16, TGRN}, {40, 40, TBLU}, {28, 28, TRED}, {56, 40, TRED}, {7, 16, TRED},
                                {8, 16, TGRN}, {23, 16, TGRN}, {24, 16, TRED}});
    });
}
// ms-depth: T 256x256 with the 4-sample auto depth-stencil, Z cleared to 1, LESSEQUAL: a near red quad (z 0.6) over
// the left half, then a far white one (0.8) over all: (64,128) red, (192,128) white. Then Z of the right half cleared to
// 0.1 by a rect and a green quad at 0.5 over all: drawn on the left (0.5 passes the red's 0.6: the drawn control),
// hidden on the right (0.5 fails 0.1).
static bool rowMsDepth() {
    return msUntil([&] {
        Com<IDirect3DSurface9> t;
        const D3DRECT right = {128, 0, 256, 256};
        return msRt(t, 256) && scene([&] {
                   return bindRt(0, t.p) && clearRects(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, TBLK) &&
                          rstate(D3DRS_ZENABLE, D3DZB_TRUE) && zquad(0, 0, 128, 256, 0.6f, TRED) &&
                          zquad(0, 0, 256, 256, 0.8f, TWHT);
               }) &&
               msHas("T", t.p, {{64, 128, TRED}, {192, 128, 0xffffff}}) && scene([&] {
                   return clearRects(1, &right, D3DCLEAR_ZBUFFER, 0, 0.1f) && zquad(0, 0, 256, 256, 0.5f, TGRN);
               }) &&
               msHas("T after the rect's Z", t.p, {{64, 128, TGRN}, {192, 128, 0xffffff}});
    });
}
// ms-mismatch: T (4 samples) with a single-sample 64x64 D24S8 surface bound and ZENABLE on: a Z Clear answers D3D_OK;
// a near red quad over all, then a far white one over y < 48: white at (48,32) (the depth-stencil of another count is
// left out of the pass: no depth test), red at (48,56); a pretransformed green quad at z 1.5 over the left half: drawn
// (no depth plane: the clamp is on; parity).
static bool rowMsMismatch() {
    return msUntil([&] {
        Com<IDirect3DSurface9> t, d;
        if (!msRt(t) || !makeDs(d, D3DFMT_D24S8, 64) || !scene([&] {
                if (!bindRt(0, t.p) || !bindDs(d.p) || !clearTo(TBLK)) return false;
                const HRESULT hr = dev->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
                if (hr != D3D_OK) return fail("Clear (Z)", hr);
                return rstate(D3DRS_ZENABLE, D3DZB_TRUE) && zquad(0, 0, 64, 64, 0.2f, TRED) &&
                       zquad(0, 0, 64, 48, 0.8f, TWHT) && zquad(0, 0, 32, 64, 1.5f, TGRN) && bindDs(dsSurf);
            }))
            return false;
        if (!msHas("T", t.p, {{48, 32, 0xffffff}, {48, 56, TRED}, {16, 32, TGRN}})) return false;
        // A Z Clear does nothing to a depth-stencil of another count than index 0's (parity: mtld3d's is a no-op):
        // Z cleared to 1 beside a single-sample target, "cleared" to 0.1 beside T, then a quad at z 0.5 beside the
        // single-sample target again still passes LESSEQUAL.
        Rt s;
        if (!mkRt(s) || !scene([&] {
                if (!bindRt(0, s.s.p) || !bindDs(d.p)) return false;
                CK(Clear, 0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, TBLK, 1.0f, 0);
                if (!bindRt(0, t.p)) return false;
                CK(Clear, 0, nullptr, D3DCLEAR_ZBUFFER, 0, 0.1f, 0);
                return bindRt(0, s.s.p) && rstate(D3DRS_ZENABLE, D3DZB_TRUE) && zquad(0, 0, 64, 64, 0.5f, TGRN) &&
                       rstate(D3DRS_ZENABLE, D3DZB_FALSE) && bindDs(dsSurf);
            }))
            return false;
        return rtHas("the depth-stencil of another count, not cleared", s.s.p, 64, A8, {{32, 32, TGRN}});
    });
}
// ms-mask: T cleared black, white quads in four columns 16 wide, each under its own D3DRS_MULTISAMPLEMASK: 0x3 (2 of 4
// samples: 255 * 2 / 4 = 127.5, 128), 0x1 (63.75, 64), 0xf (255) and 0x3 through a ps_2_0 writing white (128). On a
// single-sample target the mask 0x2 leaves a white quad white (the mask does not apply at one sample; were it applied,
// sample 0 would be out).
static bool rowMsMask() {
    Prog p;
    if (!make(p, nullptr, nullptr, PS_C0)) return false;
    return msUntil([&] {
        Com<IDirect3DSurface9> t;
        Rt s;
        if (!msRt(t) || !mkRt(s) || !scene([&] {
                if (!bindRt(0, t.p) || !clearTo(TBLK) || !rstate(D3DRS_MULTISAMPLEMASK, 0x3) ||
                    !zquad(0, 0, 16, 64, 0.5f, TWHT) || !rstate(D3DRS_MULTISAMPLEMASK, 0x1) ||
                    !zquad(16, 0, 32, 64, 0.5f, TWHT) || !rstate(D3DRS_MULTISAMPLEMASK, 0xf) ||
                    !zquad(32, 0, 48, 64, 0.5f, TWHT) || !rstate(D3DRS_MULTISAMPLEMASK, 0x3) || !use(p))
                    return false;
                CK(SetPixelShaderConstantF, 0, F_WHT, 1);
                if (!zquad(48, 0, 64, 64, 0.5f, TWHT)) return false;
                CK(SetPixelShader, nullptr);
                return rstate(D3DRS_MULTISAMPLEMASK, 0xffffffff) && bindRt(0, s.s.p) && clearTo(TBLK) &&
                       rstate(D3DRS_MULTISAMPLEMASK, 0x2) && zquad(0, 0, 64, 64, 0.5f, TWHT);
            }))
            return false;
        return msHas("T", t.p, {{8, 32, 0x808080}, {24, 32, 0x404040}, {40, 32, 0xffffff}, {56, 32, 0x808080}}) &&
               msHas("a single-sample target", s.s.p, {{32, 32, 0xffffff}});
    });
}
// ms-nonmask: a D3DMULTISAMPLE_NONMASKABLE target of quality 2 (4 samples), its description; cleared black, the mask
// 0x3 and a white quad: 255 (the mask does not apply to it).
static bool rowMsNonMask() {
    return msUntil([&] {
        Com<IDirect3DSurface9> t;
        if (!msRt(t, 64, A8, D3DMULTISAMPLE_NONMASKABLE, 2)) return false;
        D3DSURFACE_DESC d = {};
        const HRESULT hr = t.p->GetDesc(&d);
        if (FAILED(hr)) return fail("GetDesc", hr);
        if (d.MultiSampleType != D3DMULTISAMPLE_NONMASKABLE || d.MultiSampleQuality != 2)
            return snprintf(why, sizeof why, "desc type %d quality %lu", (int)d.MultiSampleType,
                            (unsigned long)d.MultiSampleQuality),
                   false;
        return scene([&] {
                   return bindRt(0, t.p) && clearTo(TBLK) && rstate(D3DRS_MULTISAMPLEMASK, 0x3) &&
                          zquad(0, 0, 64, 64, 0.5f, TWHT);
               }) &&
               msHas("T", t.p, {{32, 32, 0xffffff}});
    });
}
// ms-a2c: alpha-to-coverage. On T cleared black, white quads in its quadrants: top left, D3DRS_ADAPTIVETESS_Y ATOC
// with the alpha test on (GREATEREQUAL 0xc0) and alpha 0x80: the test does not kill it, coverage thins it (the 4x4
// mean from (14,14) in the band); top right, ATOC with the alpha test LESS 0x80 and alpha 0xff: white (full coverage, no
// alpha test); bottom left, ATOC off, the alpha test off, D3DRS_POINTSIZE A2M1 (the latch) and alpha 0x80: the band
// (the mean from (14,46)); bottom right, after A2M0, alpha 0x80: white. On a single-sample target, ATOC with the alpha
// test LESS 0x80: alpha 0 white (the test passes: the pipeline's drawn control, drawn first) and alpha 0xff black (the
// test applies there and kills it; were coverage taken instead, the two would swap).
static bool rowMsA2c() {
    return msUntil([&] {
        Com<IDirect3DSurface9> t;
        Rt r, s;
        const DWORD half = D3DCOLOR_ARGB(0x80, 255, 255, 255), none = D3DCOLOR_ARGB(0, 255, 255, 255);
        int q1 = -1, q3 = -1;
        if (!msRt(t) || !mkRt(s) || !scene([&] {
                return bindRt(0, t.p) && clearTo(TBLK) && rstate(D3DRS_ADAPTIVETESS_Y, D3DFMT_ATOC) &&
                       rstate(D3DRS_ALPHATESTENABLE, TRUE) && rstate(D3DRS_ALPHAREF, 0xc0) &&
                       rstate(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL) && zquad(0, 0, 32, 32, 0.5f, half) &&
                       rstate(D3DRS_ALPHAREF, 0x80) && rstate(D3DRS_ALPHAFUNC, D3DCMP_LESS) &&
                       zquad(32, 0, 64, 32, 0.5f, TWHT) && rstate(D3DRS_ADAPTIVETESS_Y, 0) &&
                       rstate(D3DRS_ALPHATESTENABLE, FALSE) && rstate(D3DRS_POINTSIZE, FOURCC_A2M1) &&
                       zquad(0, 32, 32, 64, 0.5f, half) && rstate(D3DRS_POINTSIZE, FOURCC_A2M0) &&
                       rstate(D3DRS_POINTSIZE, fbits(1.0f)) && zquad(32, 32, 64, 64, 0.5f, half) &&
                       bindRt(0, s.s.p) && clearTo(TBLK) && rstate(D3DRS_ADAPTIVETESS_Y, D3DFMT_ATOC) &&
                       rstate(D3DRS_ALPHATESTENABLE, TRUE) && zquad(32, 0, 64, 64, 0.5f, none) &&
                       zquad(0, 0, 32, 64, 0.5f, TWHT);
            }) ||
            !resolveInto(t.p, r) || !rtHas("T", r.s.p, 64, A8, {{48, 16, 0xffffff}, {48, 48, 0xffffff}}) ||
            !rtHas("a single-sample target", s.s.p, 64, A8, {{48, 32, 0xffffff}, {16, 32, 0x000000}}) ||
            (q1 = meanOf(r.s.p, A8, 14, 14, 4)) < 0 || (q3 = meanOf(r.s.p, A8, 14, 46, 4)) < 0)
            return false;
        return inBand("ATOC at alpha 0x80 (the 4x4 mean from (14,14))", q1) &&
               inBand("A2M1 at alpha 0x80 (the 4x4 mean from (14,46))", q3);
    });
}
// ms-srgbmask: T under SRGBWRITEENABLE, cleared black, the mask 0x3 and a white quad; a second T the same with sRGB
// write off; then a Clear of another target, so neither resolve folds into its source's pass. The first reads 188 (2
// of 4 samples white: the linear average 0.5, encoded 1.055 * 0.5^(1/2.4) - 0.055 = 0.735, 187.6: resolved through the
// view T was last written in, parity, DXMTRS C4b-3d5 ruling 4), the second 128 (127.5).
static bool rowMsSrgbMask() {
    return msUntil([&] {
        Com<IDirect3DSurface9> on, off;
        Rt other;
        const auto masked = [&](IDirect3DSurface9 *t, BOOL srgb) {
            return bindRt(0, t) && rstate(D3DRS_SRGBWRITEENABLE, srgb) && clearTo(TBLK) &&
                   rstate(D3DRS_MULTISAMPLEMASK, 0x3) && zquad(0, 0, 64, 64, 0.5f, TWHT) &&
                   rstate(D3DRS_MULTISAMPLEMASK, 0xffffffff) && rstate(D3DRS_SRGBWRITEENABLE, FALSE);
        };
        return msRt(on) && msRt(off) && mkRt(other) && scene([&] {
                   return masked(on.p, TRUE) && masked(off.p, FALSE) && bindRt(0, other.s.p) && clearTo(TBLU);
               }) &&
               msHas("sRGB write", on.p, {{32, 32, 0xbcbcbc}}) && msHas("no sRGB write", off.p, {{32, 32, 0x808080}});
    });
}
// ms-blend: T cleared red, SRCALPHA/INVSRCALPHA, a green quad of alpha 0x80: red 255 * 127 / 255 = 127, green 128.
static bool rowMsBlend() {
    return msUntil([&] {
        Com<IDirect3DSurface9> t;
        return msRt(t) && scene([&] {
                   return bindRt(0, t.p) && clearTo(TRED) && blend(D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA) &&
                          zquad(0, 0, 64, 64, 0.5f, D3DCOLOR_ARGB(0x80, 0, 255, 0));
               }) &&
               msHas("T", t.p, {{32, 32, 0x7f8000}});
    });
}
// ms-mrt: T at index 0 and a second 4-sample target at index 1, PS_MRT2 red, green: each reads its own; the same pair
// under SRGBWRITEENABLE, cleared black, with the mask 0x3 and white on both: 188 on both. Then, at index 1, a
// single-sample 64x64 texture, and a 2-sample target when the device offers 2: it and T cleared blue apart, PS_MRT2
// red, green: T red, it blue (of another sample count than index 0: out of the draw's set; T's red is the drawn
// control); a whole Clear to white reaches T alone (mtld3d clears apart only a target of another size).
static bool rowMsMrt() {
    Prog p;
    if (!make(p, DECL_XYZ, VS_POS, PS_MRT2)) return false;
    const bool two = offers(D3DMULTISAMPLE_2_SAMPLES);
    return msUntil([&] {
        Com<IDirect3DSurface9> t, t1, t2;
        Rt u;
        if (!msRt(t) || !msRt(t1) || !mkRt(u) || (two && !msRt(t2, 64, A8, D3DMULTISAMPLE_2_SAMPLES)) ||
            !scene([&] { return bindRt(0, t.p) && bindRt(1, t1.p) && clearTo(TBLK) && mrtDraw2(p, F_RED, F_GRN); }) ||
            !msHas("T0", t.p, {{32, 32, TRED}}) || !msHas("T1", t1.p, {{32, 32, TGRN}}) || !scene([&] {
                return rstate(D3DRS_SRGBWRITEENABLE, TRUE) && clearTo(TBLK) && rstate(D3DRS_MULTISAMPLEMASK, 0x3) &&
                       mrtDraw2(p, F_WHT, F_WHT) && rstate(D3DRS_MULTISAMPLEMASK, 0xffffffff) &&
                       rstate(D3DRS_SRGBWRITEENABLE, FALSE);
            }) ||
            !msHas("T0 sRGB", t.p, {{32, 32, 0xbcbcbc}}) || !msHas("T1 sRGB", t1.p, {{32, 32, 0xbcbcbc}}))
            return false;
        for (IDirect3DSurface9 *x : {u.s.p, t2.p}) {
            if (!x) continue;
            const char *name = x == u.s.p ? "a single-sample T1" : "a 2-sample T1";
            if (!scene([&] {
                    return bindRt(1, nullptr) && clearRt(x, TBLU) && clearRt(t.p, TBLU) && bindRt(1, x) &&
                           mrtDraw2(p, F_RED, F_GRN);
                }) ||
                !msHas("T0 beside it", t.p, {{32, 32, TRED}}) || !msHas(name, x, {{32, 32, TBLU}}) ||
                !scene([&] { return clearTo(TWHT); }) || !msHas("T0 cleared", t.p, {{32, 32, 0xffffff}}) ||
                // Not the Clear either: mtld3d clears apart only a target of another size (parity).
                !msHas(name, x, {{32, 32, TBLU}}))
                return false;
        }
        return true;
    });
}
// ms-scissor: T cleared blue, the scissor rect (16,16)-(48,48) and a red quad over all: (32,32) red, (15,32) and
// (48,32) blue; cleared blue again, the viewport (0,0,32,64) and a full clip-space quad: (16,32) red, (48,32) blue.
static bool rowMsScissor() {
    return msUntil([&] {
        Com<IDirect3DSurface9> t;
        FC f[4];
        cquad(f, -1, -1, 1, 1, TRED);
        return msRt(t) && scene([&] {
                   return bindRt(0, t.p) && clearTo(TBLU) && scissor(16, 16, 48, 48) &&
                          rstate(D3DRS_SCISSORTESTENABLE, TRUE) && zquad(0, 0, 64, 64, 0.5f, TRED) &&
                          rstate(D3DRS_SCISSORTESTENABLE, FALSE);
               }) &&
               msHas("the scissor", t.p, {{32, 32, TRED}, {15, 32, TBLU}, {48, 32, TBLU}}) &&
               scene([&] { return clearTo(TBLU) && viewport(0, 0, 32, 64) && fdraw(FVF_C, f); }) &&
               msHas("the viewport", t.p, {{16, 32, TRED}, {48, 32, TBLU}});
    });
}
// ms-redraw: T cleared black, a red quad over its left half, resolved into R1; a green quad over the right half,
// resolved into R2: R1 red and black, R2 red and green. R1 then at index 0, a white quad over its top half: white
// there, red and black below.
static bool rowMsRedraw() {
    return msUntil([&] {
        Com<IDirect3DSurface9> t;
        Rt r1, r2;
        return msRt(t) && scene([&] { return bindRt(0, t.p) && clearTo(TBLK) && zquad(0, 0, 32, 64, 0.5f, TRED); }) &&
               resolveInto(t.p, r1) && scene([&] { return zquad(32, 0, 64, 64, 0.5f, TGRN); }) &&
               resolveInto(t.p, r2) && rtHas("R1", r1.s.p, 64, A8, {{16, 32, TRED}, {48, 32, 0x000000}}) &&
               rtHas("R2", r2.s.p, 64, A8, {{16, 32, TRED}, {48, 32, TGRN}}) &&
               scene([&] { return bindRt(0, r1.s.p) && zquad(0, 0, 64, 32, 0.5f, TWHT); }) &&
               rtHas("R1 drawn on", r1.s.p, 64, A8,
                     {{16, 16, 0xffffff}, {48, 16, 0xffffff}, {16, 48, TRED}, {48, 48, 0x000000}});
    });
}
// ms-samples: for 2 and 8 samples, each where the device offers it: a target of that count cleared black, the mask of
// its lower half (0x1 of 2, 0x0f of 8) and a white quad: 128 (127.5: half the samples white).
static bool rowMsSamples() {
    static const struct { D3DMULTISAMPLE_TYPE type; DWORD mask; const char *name; } counts[] = {
        {D3DMULTISAMPLE_2_SAMPLES, 0x1, "2 samples"}, {D3DMULTISAMPLE_8_SAMPLES, 0x0f, "8 samples"}};
    for (const auto &c : counts)
        if (offers(c.type) && !msUntil([&] {
                Com<IDirect3DSurface9> t;
                return msRt(t, 64, A8, c.type) && scene([&] {
                           return bindRt(0, t.p) && clearTo(TBLK) && rstate(D3DRS_MULTISAMPLEMASK, c.mask) &&
                                  zquad(0, 0, 64, 64, 0.5f, TWHT);
                       }) &&
                       msHas(c.name, t.p, {{32, 32, 0x808080}});
            }))
            return false;
    return true;
}
// rt-msaa (the targets kind's device: a single-sample X8R8G8B8 back buffer): a 4-sample 64x64 A8R8G8B8 target and its
// description; cleared green with a red quad over its left half, resolved into a texture level and into a
// single-sample CreateRenderTarget surface: (16,32) red, (48,32) green in both; a 4-sample 256x256 X8R8G8B8 target
// cleared green, resolved into the back buffer cleared blue: green there. GetRenderTargetData of the multisampled
// target (into a system-memory surface of its size and format) and a lockable multisampled target are
// D3DERR_INVALIDCALL in every lane; its StretchRect into a 32x32 target is past the core's resolve shape:
// D3DERR_NOTAVAILABLE under core (strict), any answer elsewhere.
static bool rowRtMsaa() {
    return until([&] {
        Com<IDirect3DSurface9> m, big, s, sys, lock, small;
        Rt r;
        D3DSURFACE_DESC d = {};
        if (!msRt(m)) return false;
        HRESULT hr = m.p->GetDesc(&d);
        if (FAILED(hr)) return fail("GetDesc", hr);
        if (d.MultiSampleType != D3DMULTISAMPLE_4_SAMPLES)
            return snprintf(why, sizeof why, "desc %d samples", (int)d.MultiSampleType), false;
        CK(CreateRenderTarget, 64, 64, A8, D3DMULTISAMPLE_NONE, 0, FALSE, &s.p, nullptr);
        if (!msRt(big, 256, X8) || !scene([&] {
                return bindRt(0, m.p) && clearTo(TGRN) && zquad(0, 0, 32, 64, 0.5f, TRED) && bindRt(0, big.p) &&
                       clearTo(TGRN) && backToScreen() && clearTo(TBLU);
            }) ||
            !resolveInto(m.p, r) || !rtHas("a texture level", r.s.p, 64, A8, {{16, 32, TRED}, {48, 32, TGRN}}))
            return false;
        CK(StretchRect, m.p, nullptr, s.p, nullptr, D3DTEXF_NONE);
        if (!rtHas("a CreateRenderTarget surface", s.p, 64, A8, {{16, 32, TRED}, {48, 32, TGRN}})) return false;
        CK(StretchRect, big.p, nullptr, rtSurf, nullptr, D3DTEXF_NONE);
        if (!rtHas("the back buffer", rtSurf, 256, X8, {{128, 128, TGRN}})) return false;
        CK(CreateOffscreenPlainSurface, 64, 64, A8, D3DPOOL_SYSTEMMEM, &sys.p, nullptr);
        hr = dev->GetRenderTargetData(m.p, sys.p);
        if (hr != D3DERR_INVALIDCALL) return fail("GetRenderTargetData (4 samples)", hr);
        hr = dev->CreateRenderTarget(64, 64, A8, D3DMULTISAMPLE_4_SAMPLES, 0, TRUE, &lock.p, nullptr);
        if (hr != D3DERR_INVALIDCALL) return fail("CreateRenderTarget (4 samples, lockable)", hr);
        CK(CreateRenderTarget, 32, 32, A8, D3DMULTISAMPLE_NONE, 0, FALSE, &small.p, nullptr);
        hr = dev->StretchRect(m.p, nullptr, small.p, nullptr, D3DTEXF_NONE);
        if (strictMode && hr != D3DERR_NOTAVAILABLE) return fail("StretchRect (into 32x32)", hr);
        return true;
    });
}

// ms-initial: run before anything binds a target, with index 0 still the device's own back buffer (never set: the
// state a title that never calls SetRenderTarget draws in): the sample mask applies there, 0x3 of 4 samples and a
// white quad over black reading 128. A retried attempt (the default runtime's pipelines compiling) runs with the
// back buffer bound by until(), which the default runtime treats alike.
static bool rowMsInitial() {
    return until([&] {
        Rt r;
        CK(Clear, 0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, TBLK, 1.0f, 0);
        return rstate(D3DRS_MULTISAMPLEMASK, 0x3) && scene([&] { return zquad(0, 0, 256, 256, 0.5f, TWHT); }) &&
               rstate(D3DRS_MULTISAMPLEMASK, 0xffffffff) && resolveInto(rtSurf, r, 256, X8) &&
               rtHas("the back buffer nothing bound", r.s.p, 256, X8, {{128, 128, 0x808080}});
    });
}

// The back buffer read after two frames (DXMTRS C4c-3a): A, cleared sage with a lilac quad over (64,64)-(192,192), and
// presented; B, cleared steel with a gold quad there, not presented. B is read: the frame drawn since the Present,
// which the read resolves first (not A, which the Present resolved). Each attempt presents its own A.
static bool msTwoFrames() {
    const auto frame = [](DWORD clear, DWORD quad) {
        return scene([&] { return backToScreen() && clearTo(clear) && zquad(64, 64, 192, 192, 0.5f, quad); });
    };
    if (!frame(KSAGE, KLILAC)) return false;
    CK(Present, nullptr, nullptr, nullptr, nullptr);
    return frame(KSTEEL, KGOLD);
}
// ms-bblock: the multisampled back buffer locked READONLY reads B: gold at (128,128), steel at (32,32). The lock is
// whole (from the origin), so the row sees the resolve and not the rect's origin, which rt-bblock's rects see.
static bool rowMsBblock() {
    return msUntil([&] {
        Com<IDirect3DSurface9> bb;
        DWORD in = 0, out = 0;
        if (!msTwoFrames()) return false;
        CK(GetBackBuffer, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb.p);
        if (!underLock(bb.p, "LockRect (NULL, READONLY)", nullptr, D3DLOCK_READONLY, [&](BYTE *b, int p) {
                in = texel(b, p, 128, 128), out = texel(b, p, 32, 32);
            }))
            return false;
        return want("the back buffer (128,128)", in, KGOLD) && want("the back buffer (32,32)", out, KSTEEL);
    });
}
// ms-front: GetFrontBufferData into a 256x256 A8R8G8B8 system-memory surface reads B: gold at (128,128), steel at
// (32,32).
static bool rowMsFront() {
    return msUntil([&] {
        Com<IDirect3DSurface9> s;
        DWORD in = 0, out = 0;
        if (!msTwoFrames()) return false;
        CK(CreateOffscreenPlainSurface, 256, 256, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &s.p, nullptr);
        CK(GetFrontBufferData, 0, s.p);
        if (!underLock(s.p, "LockRect (the front buffer's read)", nullptr, D3DLOCK_READONLY, [&](BYTE *b, int p) {
                in = texel(b, p, 128, 128), out = texel(b, p, 32, 32);
            }))
            return false;
        return want("front (128,128)", in, KGOLD) && want("front (32,32)", out, KSTEEL);
    });
}

static void msaaRows(IDirect3DDevice9 *d, bool strict) {
    static const struct { const char *name; bool (*run)(); } rows[] = {
        {"ms-desc", rowMsDesc},       {"ms-resolve", rowMsResolve},   {"ms-edge", rowMsEdge},
        {"ms-back", rowMsBack},       {"ms-clear", rowMsClear},       {"ms-depth", rowMsDepth},
        {"ms-mismatch", rowMsMismatch}, {"ms-mask", rowMsMask},       {"ms-nonmask", rowMsNonMask},
        {"ms-a2c", rowMsA2c},         {"ms-srgbmask", rowMsSrgbMask}, {"ms-blend", rowMsBlend},
        {"ms-mrt", rowMsMrt},         {"ms-scissor", rowMsScissor},   {"ms-redraw", rowMsRedraw},
        {"ms-samples", rowMsSamples}, {"ms-bblock", rowMsBblock},     {"ms-front", rowMsFront}};
    dev = d, strictMode = strict, budgetEnd = GetTickCount64() + 180000;
    HRESULT hr = dev->GetRenderTarget(0, &rtSurf);
    if (SUCCEEDED(hr))
        hr = dev->CreateOffscreenPlainSurface(256, 256, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sysSurf, nullptr);
    if (SUCCEEDED(hr)) hr = dev->GetDepthStencilSurface(&dsSurf);
    // First, before any reset() binds index 0.
    why[0] = 0;
    row("ms-initial", SUCCEEDED(hr) ? rowMsInitial() : fail("GetRenderTarget/GetDepthStencilSurface", hr), why);
    for (const auto &r : rows) {
        why[0] = 0;
        bool ok;
        if (FAILED(hr)) ok = fail("GetRenderTarget/GetDepthStencilSurface", hr);
        else if (GetTickCount64() >= budgetEnd) ok = false, snprintf(why, sizeof why, "budget");
        else ok = reset() && depthReset() && blendStart() && r.run();
        row(r.name, ok, why);
    }
    if (dsSurf) dsSurf->Release();
    if (sysSurf) sysSurf->Release();
    if (rtSurf) rtSurf->Release();
}

// --- The "buffers" mode (DXMTRS C4c-1): vertex and index buffers and the draws that read them, on the state mode's
// plain device (no depth-stencil). Vertices are pretransformed XYZRHW|DIFFUSE (RV) and pixel-aligned unless said: "left"
// is the quad (16,64)-(112,192), probed at (64,128), "right" (144,64)-(240,192), probed at (192,128), and every probe
// sits 8 pixels or more inside its shape. Buffers are default-pool, usage 0, unless said. Every frame clears to blue,
// so black is a drawn colour. A row that writes a buffer between draws makes and fills it inside its attempt, so a
// retried frame (mtld3d leaves a draw out while its pipeline builds) starts again from the same bytes.
static const DWORD FVF_RV = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;
// The quad (x0,y0)-(x1,y1) as a six-vertex triangle list, both triangles clockwise: (x1,y0) (x0,y1) (x0,y0), then
// (x1,y0) (x1,y1) (x0,y1). The first alone holds the corner (x0,y0), its vertex 2; the second alone (x1,y1), vertex 4.
static void listRhw(RV *v, float x0, float y0, float x1, float y1, DWORD c) {
    v[0] = {x1, y0, 0.5f, 1, c}, v[1] = {x0, y1, 0.5f, 1, c}, v[2] = {x0, y0, 0.5f, 1, c};
    v[3] = {x1, y0, 0.5f, 1, c}, v[4] = {x1, y1, 0.5f, 1, c}, v[5] = {x0, y1, 0.5f, 1, c};
}
static void listLeft(RV *v, DWORD c) { listRhw(v, 16, 64, 112, 192, c); }
static void listRight(RV *v, DWORD c) { listRhw(v, 144, 64, 240, 192, c); }
// A triangle with its top edge (x0,y0)-(x1,y0) and its apex at ((x0 + x1) / 2, y1), clockwise: halfway down it spans
// the middle half of its width (the left quad's (16,64,112,192) spans 40-88 at y 128).
static void triRhw(RV *v, float x0, float y0, float x1, float y1, DWORD c) {
    v[0] = {x0, y0, 0.5f, 1, c}, v[1] = {x1, y0, 0.5f, 1, c}, v[2] = {(x0 + x1) / 2, y1, 0.5f, 1, c};
}
// n bytes of `data` into the buffer `b` from byte `off`, through Lock(flags) and Unlock.
template <class B> static bool put(B *b, UINT off, const void *data, UINT n, DWORD flags = 0) {
    void *p = nullptr;
    HRESULT hr = b->Lock(off, n, &p, flags);
    if (FAILED(hr)) return fail("Lock", hr);
    memcpy(p, data, n);
    hr = b->Unlock();
    return SUCCEEDED(hr) || fail("Unlock", hr);
}
// A vertex buffer (usage 0) of n bytes in `pool` holding `data`.
static bool mkVb(Com<IDirect3DVertexBuffer9> &b, const void *data, UINT n, D3DPOOL pool = D3DPOOL_DEFAULT) {
    CK(CreateVertexBuffer, n, 0, 0, pool, &b.p, nullptr);
    return put(b.p, 0, data, n);
}
static bool mkIb(Com<IDirect3DIndexBuffer9> &b, const void *data, UINT n, D3DFORMAT f, D3DPOOL pool = D3DPOOL_DEFAULT) {
    CK(CreateIndexBuffer, n, 0, f, pool, &b.p, nullptr);
    return put(b.p, 0, data, n);
}
// `b` at stream 0 as RV vertices from byte `off`.
static bool rvAt(IDirect3DVertexBuffer9 *b, UINT off = 0) {
    CK(SetFVF, FVF_RV);
    CK(SetStreamSource, 0, b, off, sizeof(RV));
    return true;
}
static bool drawList(UINT start = 0, UINT count = 2) {
    CK(DrawPrimitive, D3DPT_TRIANGLELIST, start, count);
    return true;
}
// Whether a call answered `expect`; `why` names it otherwise.
static bool answer(const char *call, HRESULT hr, HRESULT expect) { return hr == expect || fail(call, hr); }
// What a buffers row starts from, beside reset(), which restores no stream, frequency or index state: streams 0 and 1
// unbound at frequency 1 and no index buffer. Each row sets its own declaration or FVF.
static bool bufStart() {
    for (UINT s = 0; s < 2; s++) {
        CK(SetStreamSource, s, nullptr, 0, 0);
        CK(SetStreamSourceFreq, s, 1);
    }
    CK(SetIndices, nullptr);
    return true;
}

// buf-d3d: GetDirect3D answers main's IDirect3D9 with one reference added (read through AddRef and Release); a null
// out pointer is D3DERR_INVALIDCALL.
static ULONG refs(IUnknown *u) { return u->AddRef(), u->Release(); }
static bool rowBufD3d() {
    Com<IDirect3D9> d3d;
    const ULONG before = refs(direct3d);
    CK(GetDirect3D, &d3d.p);
    const ULONG after = refs(direct3d);
    if (d3d.p != direct3d || after != before + 1)
        return snprintf(why, sizeof why, "GetDirect3D %p (main's %p), references %lu then %lu", (void *)d3d.p,
                        (void *)direct3d, (unsigned long)before, (unsigned long)after),
               false;
    return answer("GetDirect3D (null)", dev->GetDirect3D(nullptr), D3DERR_INVALIDCALL);
}
// The lock cells of buf-lock on the 64-byte buffer `b`: Lock(0, 0) D3D_OK with a pointer; the bytes written read back
// under a READONLY lock after Unlock; a Lock at an offset past the length D3DERR_INVALIDCALL; two locks then two
// unlocks each D3D_OK.
template <class B> static bool lockCells(B *b) {
    BYTE *p = nullptr, *q = nullptr;
    HRESULT hr = b->Lock(0, 0, (void **)&p, 0);
    if (hr != D3D_OK || !p) return fail("Lock", hr);
    for (int i = 0; i < 64; i++) p[i] = (BYTE)(i * 7 + 3);
    if ((hr = b->Unlock()) != D3D_OK) return fail("Unlock", hr);
    if ((hr = b->Lock(0, 0, (void **)&q, D3DLOCK_READONLY)) != D3D_OK || !q) return fail("Lock (READONLY)", hr);
    int bad = -1;
    for (int i = 0; i < 64 && bad < 0; i++)
        if (q[i] != (BYTE)(i * 7 + 3)) bad = i;
    if ((hr = b->Unlock()) != D3D_OK) return fail("Unlock (READONLY)", hr);
    if (bad >= 0) return snprintf(why, sizeof why, "the READONLY lock reads byte %d otherwise", bad), false;
    hr = b->Lock(128, 4, (void **)&p, 0);
    if (SUCCEEDED(hr)) b->Unlock();
    if (hr != D3DERR_INVALIDCALL) return fail("Lock (offset 128 of 64 bytes)", hr);
    if ((hr = b->Lock(0, 0, (void **)&p, 0)) != D3D_OK) return fail("Lock (the first of two)", hr);
    if ((hr = b->Lock(0, 0, (void **)&q, 0)) != D3D_OK) return b->Unlock(), fail("Lock (the second of two)", hr);
    if ((hr = b->Unlock()) != D3D_OK) return b->Unlock(), fail("Unlock (the first of two)", hr);
    return answer("Unlock (the second of two)", b->Unlock(), D3D_OK);
}
// buf-lock: lockCells over a 64-byte vertex buffer (FVF XYZRHW|DIFFUSE) and a 64-byte 16-bit index buffer; GetDesc
// says the size, pool, usage and FVF or format given.
static bool rowBufLock() {
    Com<IDirect3DVertexBuffer9> vb;
    Com<IDirect3DIndexBuffer9> ib;
    CK(CreateVertexBuffer, 64, 0, FVF_RV, D3DPOOL_DEFAULT, &vb.p, nullptr);
    CK(CreateIndexBuffer, 64, 0, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &ib.p, nullptr);
    if (!lockCells(vb.p) || !lockCells(ib.p)) return false;
    D3DVERTEXBUFFER_DESC v = {};
    D3DINDEXBUFFER_DESC i = {};
    HRESULT hr = vb.p->GetDesc(&v);
    if (FAILED(hr)) return fail("GetDesc (vertex buffer)", hr);
    if (FAILED(hr = ib.p->GetDesc(&i))) return fail("GetDesc (index buffer)", hr);
    if (v.Size != 64 || v.Pool != D3DPOOL_DEFAULT || v.Usage || v.FVF != FVF_RV)
        return snprintf(why, sizeof why, "vertex buffer size %u pool %d usage %lx fvf %lx", v.Size, (int)v.Pool,
                        (unsigned long)v.Usage, (unsigned long)v.FVF),
               false;
    if (i.Size != 64 || i.Pool != D3DPOOL_DEFAULT || i.Usage || i.Format != D3DFMT_INDEX16)
        return snprintf(why, sizeof why, "index buffer size %u pool %d usage %lx format %d", i.Size, (int)i.Pool,
                        (unsigned long)i.Usage, (int)i.Format),
               false;
    return true;
}
// buf-draw: a red quad at the left, DrawPrimitive(TRIANGLELIST, 0, 2): left red, right blue.
static bool rowBufDraw() {
    RV q[6];
    listLeft(q, TRED);
    Com<IDirect3DVertexBuffer9> vb;
    if (!mkVb(vb, q, sizeof q)) return false;
    const Px px[] = {{64, 128, TRED}, {192, 128, BLUE}};
    return frames(BLUE, px, 2, [&] { return rvAt(vb.p) && drawList(); });
}
// One buffer of a red triangle inside the left quad over (64,128) (vertices 0-2), then a green quad at the right
// (3-8), at stream offset `off`, DrawPrimitive(TRIANGLELIST, start, 2): right green, left blue.
static bool startRow(UINT off, UINT start) {
    RV v[9];
    triRhw(v, 16, 64, 112, 192, TRED), listRight(v + 3, TGRN);
    Com<IDirect3DVertexBuffer9> vb;
    if (!mkVb(vb, v, sizeof v)) return false;
    const Px px[] = {{192, 128, TGRN}, {64, 128, BLUE}};
    return frames(BLUE, px, 2, [&] { return rvAt(vb.p, off) && drawList(start); });
}
// buf-start: start vertex 3 (from vertex 0 the triangle would cover (64,128)).
static bool rowBufStart() { return startRow(0, 3); }
// buf-offset: OffsetInBytes 60 (three vertices) and start 0: the same pixels.
static bool rowBufOffset() { return startRow(60, 0); }
// buf-strip: a four-vertex strip at the left, red; then vertices 4-5 of the same buffer as a line list, one green
// horizontal line at y = 200 from x = 16 to 240: (128,200), the pixel centre row the line runs through, green, (128,202)
// blue.
static bool rowBufStrip() {
    RV v[6];
    quadRhw(v, 16, 64, 112, 192, TRED);
    v[4] = {16, 200, 0.5f, 1, TGRN}, v[5] = {240, 200, 0.5f, 1, TGRN};
    Com<IDirect3DVertexBuffer9> vb;
    if (!mkVb(vb, v, sizeof v)) return false;
    const Px px[] = {{64, 128, TRED}, {128, 200, TGRN}, {128, 202, BLUE}};
    return frames(BLUE, px, 3, [&] {
        if (!rvAt(vb.p)) return false;
        CK(DrawPrimitive, D3DPT_TRIANGLESTRIP, 0, 2);
        CK(DrawPrimitive, D3DPT_LINELIST, 4, 1);
        return true;
    });
}
// buf-fan: five vertices, 1-4 the right quad as a red fan, 0 at the left (16,128); DrawPrimitive(TRIANGLEFAN, 1, 2):
// right red, left blue (a fan from vertex 0 would cover (64,128)).
static bool rowBufFan() {
    const DWORD r = TRED;
    const RV v[5] = {{16, 128, 0.5f, 1, r}, {144, 64, 0.5f, 1, r}, {240, 64, 0.5f, 1, r}, {240, 192, 0.5f, 1, r},
                     {144, 192, 0.5f, 1, r}};
    Com<IDirect3DVertexBuffer9> vb;
    if (!mkVb(vb, v, sizeof v)) return false;
    const Px px[] = {{192, 128, TRED}, {64, 128, BLUE}};
    return frames(BLUE, px, 2, [&] {
        if (!rvAt(vb.p)) return false;
        CK(DrawPrimitive, D3DPT_TRIANGLEFAN, 1, 2);
        return true;
    });
}
// buf-indexed's buffers in `pool`: four vertices (the left quad, red, as a strip's) and twelve indices of format f, six
// zeros (two degenerate triangles) then the quad's 0 1 2 2 1 3.
static bool indexedPair(Com<IDirect3DVertexBuffer9> &vb, Com<IDirect3DIndexBuffer9> &ib, D3DFORMAT f,
                        D3DPOOL pool = D3DPOOL_DEFAULT) {
    static const WORD i16[12] = {0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 1, 3};
    DWORD i32[12];
    for (int i = 0; i < 12; i++) i32[i] = i16[i];
    RV q[4];
    quadRhw(q, 16, 64, 112, 192, TRED);
    return mkVb(vb, q, sizeof q, pool) &&
           (f == D3DFMT_INDEX16 ? mkIb(ib, i16, sizeof i16, f, pool) : mkIb(ib, i32, sizeof i32, f, pool));
}
// DrawIndexedPrimitive(TRIANGLELIST, 0, 0, 4, 6, 2) of them: left red (from index 0 nothing is drawn).
static bool indexedDraw(IDirect3DVertexBuffer9 *vb, IDirect3DIndexBuffer9 *ib) {
    if (!rvAt(vb)) return false;
    CK(SetIndices, ib);
    CK(DrawIndexedPrimitive, D3DPT_TRIANGLELIST, 0, 0, 4, 6, 2);
    return true;
}
static bool indexedRow(D3DFORMAT f) {
    Com<IDirect3DVertexBuffer9> vb;
    Com<IDirect3DIndexBuffer9> ib;
    if (!indexedPair(vb, ib, f)) return false;
    const Px px[] = {{64, 128, TRED}};
    return frames(BLUE, px, 1, [&] { return indexedDraw(vb.p, ib.p); });
}
// buf-indexed: 16-bit indices.
static bool rowBufIndexed() { return indexedRow(D3DFMT_INDEX16); }
// buf-index32: 32-bit indices.
static bool rowBufIndex32() { return indexedRow(D3DFMT_INDEX32); }
// buf-base: eight vertices, a red quad at the left (0-3) then a green one at the right (4-7), as strips'; one index
// buffer holding 0 1 2 2 1 3, drawn at base vertex 4: right green, left blue. In a second frame the same buffer
// rewritten 4 5 6 6 5 7 and drawn at base vertex -4 (MinVertexIndex 4): left red, right blue.
static bool rowBufBase() {
    RV v[8];
    quadRhw(v, 16, 64, 112, 192, TRED), quadRhw(v + 4, 144, 64, 240, 192, TGRN);
    const WORD lo[6] = {0, 1, 2, 2, 1, 3}, hi[6] = {4, 5, 6, 6, 5, 7};
    Com<IDirect3DVertexBuffer9> vb;
    Com<IDirect3DIndexBuffer9> ib;
    if (!mkVb(vb, v, sizeof v) || !mkIb(ib, lo, sizeof lo, D3DFMT_INDEX16)) return false;
    const Px right[] = {{192, 128, TGRN}, {64, 128, BLUE}}, left[] = {{64, 128, TRED}, {192, 128, BLUE}};
    const auto draw = [&](const WORD *idx, INT base, UINT min) {
        if (!put(ib.p, 0, idx, sizeof lo) || !rvAt(vb.p)) return false;
        CK(SetIndices, ib.p);
        CK(DrawIndexedPrimitive, D3DPT_TRIANGLELIST, base, min, 4, 0, 2);
        return true;
    };
    return frames(BLUE, right, 2, [&] { return draw(lo, 4, 0); }) &&
           frames(BLUE, left, 2, [&] { return draw(hi, -4, 4); });
}
// buf-ifan: nine vertices: 0-3 a green quad at the bottom, (64,252) (80,204) (176,204) (128,252), whose vertices 1-3
// are a triangle over (128,224); 4 at the left, (16,128); 5-8 the right quad as a red fan. Indices 0 0 1 2 3 4 and
// DrawIndexedPrimitive(TRIANGLEFAN, 4, 0, 9, 2, 2): right red, left and bottom blue. Then indices 0 0 9 10 11 12 at base
// vertex -4 (MinVertexIndex 9, 4 vertices): the same pixels (a base applied twice fetches vertices 1-4: the bottom
// drawn); and those indices again from a 32-bit index buffer: the same.
static bool rowBufIfan() {
    const DWORD g = TGRN, r = TRED;
    const RV v[9] = {{64, 252, 0.5f, 1, g},  {80, 204, 0.5f, 1, g},  {176, 204, 0.5f, 1, g},
                     {128, 252, 0.5f, 1, g}, {16, 128, 0.5f, 1, g},  {144, 64, 0.5f, 1, r},
                     {240, 64, 0.5f, 1, r},  {240, 192, 0.5f, 1, r}, {144, 192, 0.5f, 1, r}};
    const WORD i1[6] = {0, 0, 1, 2, 3, 4}, i2[6] = {0, 0, 9, 10, 11, 12};
    const DWORD i3[6] = {0, 0, 9, 10, 11, 12};
    Com<IDirect3DVertexBuffer9> vb;
    Com<IDirect3DIndexBuffer9> a, b, c;
    if (!mkVb(vb, v, sizeof v) || !mkIb(a, i1, sizeof i1, D3DFMT_INDEX16) || !mkIb(b, i2, sizeof i2, D3DFMT_INDEX16) ||
        !mkIb(c, i3, sizeof i3, D3DFMT_INDEX32))
        return false;
    const Px px[] = {{192, 128, TRED}, {64, 128, BLUE}, {128, 224, BLUE}};
    const auto fan = [&](IDirect3DIndexBuffer9 *ib, INT base, UINT min, UINT n) {
        return frames(BLUE, px, 3, [&] {
            if (!rvAt(vb.p)) return false;
            CK(SetIndices, ib);
            CK(DrawIndexedPrimitive, D3DPT_TRIANGLEFAN, base, min, n, 2, 2);
            return true;
        });
    };
    return fan(a.p, 4, 0, 9) && fan(b.p, -4, 9, 4) && fan(c.p, -4, 9, 4);
}
// buf-managed: buf-indexed's buffers in the managed pool: left red; again after a READONLY Lock and Unlock of the
// vertex buffer: left red. A SYSTEMMEM-pool buffer of the left quad, DrawPrimitive: D3D_OK and left red (what a
// correct D3D9 draws; the control decides).
static bool rowBufManaged() {
    Com<IDirect3DVertexBuffer9> vb, sys;
    Com<IDirect3DIndexBuffer9> ib;
    RV q[6];
    listLeft(q, TRED);
    if (!indexedPair(vb, ib, D3DFMT_INDEX16, D3DPOOL_MANAGED) || !mkVb(sys, q, sizeof q, D3DPOOL_SYSTEMMEM))
        return false;
    const Px px[] = {{64, 128, TRED}};
    return frames(BLUE, px, 1, [&] { return indexedDraw(vb.p, ib.p); }) && frames(BLUE, px, 1, [&] {
               void *p = nullptr;
               HRESULT hr = vb.p->Lock(0, 0, &p, D3DLOCK_READONLY);
               if (FAILED(hr)) return fail("Lock (READONLY)", hr);
               if (FAILED(hr = vb.p->Unlock())) return fail("Unlock (READONLY)", hr);
               return indexedDraw(vb.p, ib.p);
           }) && frames(BLUE, px, 1, [&] {
               return rvAt(sys.p) && answer("DrawPrimitive (SYSTEMMEM)", dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2),
                                            D3D_OK);
           });
}
// buf-dynamic: a DYNAMIC|WRITEONLY buffer of nine vertices: Lock(DISCARD) of vertices 0-2, a red triangle inside the
// left quad, Unlock, draw; Lock of vertices 3-5 (NOOVERWRITE), a green triangle inside the right quad, Unlock,
// DrawPrimitive from 3; Lock of vertices 3-5 (NOOVERWRITE) again, a white triangle over (128,224) written into vertices
// 6-8, past the announced window, Unlock, DrawPrimitive from 6: left red, right green, (128,224) white (mtld3d maps
// such a buffer directly, so every byte written through the pointer is drawn: DXMTRS C4c-1's ruling 1).
static bool rowBufDynamic() {
    RV t[9];
    triRhw(t, 16, 64, 112, 192, TRED), triRhw(t + 3, 144, 64, 240, 192, TGRN), triRhw(t + 6, 80, 204, 176, 252, TWHT);
    const Px px[] = {{64, 128, TRED}, {192, 128, TGRN}, {128, 224, TWHT}};
    return frames(BLUE, px, 3, [&] {
        Com<IDirect3DVertexBuffer9> vb;
        CK(CreateVertexBuffer, sizeof t, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &vb.p, nullptr);
        if (!put(vb.p, 0, t, 3 * sizeof *t, D3DLOCK_DISCARD) || !rvAt(vb.p) || !drawList(0, 1) ||
            !put(vb.p, 3 * sizeof *t, t + 3, 3 * sizeof *t, D3DLOCK_NOOVERWRITE) || !drawList(3, 1))
            return false;
        void *p = nullptr;
        HRESULT hr = vb.p->Lock(3 * sizeof *t, 3 * sizeof *t, &p, D3DLOCK_NOOVERWRITE);
        if (FAILED(hr)) return fail("Lock (NOOVERWRITE, vertices 3-5)", hr);
        memcpy((RV *)p + 3, t + 6, 3 * sizeof *t);
        if (FAILED(hr = vb.p->Unlock())) return fail("Unlock", hr);
        return drawList(6, 1);
    });
}
// buf-order: a red quad at the left drawn; the same six vertices locked (flags 0) and overwritten with a green quad at
// the right, drawn, no Clear between: left red (the first draw kept its bytes), right green.
static bool rowBufOrder() {
    RV l[6], r[6];
    listLeft(l, TRED), listRight(r, TGRN);
    const Px px[] = {{64, 128, TRED}, {192, 128, TGRN}};
    return frames(BLUE, px, 2, [&] {
        Com<IDirect3DVertexBuffer9> vb;
        return mkVb(vb, l, sizeof l) && rvAt(vb.p) && drawList() && put(vb.p, 0, r, sizeof r) && drawList();
    });
}
// buf-discard: a DYNAMIC|WRITEONLY buffer: Lock(DISCARD), a red quad at the left, draw; Lock(DISCARD), a green quad at
// the right, draw: left red, right green.
static bool rowBufDiscard() {
    RV l[6], r[6];
    listLeft(l, TRED), listRight(r, TGRN);
    const Px px[] = {{64, 128, TRED}, {192, 128, TGRN}};
    return frames(BLUE, px, 2, [&] {
        Com<IDirect3DVertexBuffer9> vb;
        CK(CreateVertexBuffer, sizeof l, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &vb.p, nullptr);
        return put(vb.p, 0, l, sizeof l, D3DLOCK_DISCARD) && rvAt(vb.p) && drawList() &&
               put(vb.p, 0, r, sizeof r, D3DLOCK_DISCARD) && drawList();
    });
}
// buf-held: Lock, a red quad at the left written, drawn while locked; a green quad at the right written through the
// same pointer over the same vertices, drawn again, no Clear between; Unlock: left red (the first draw's bytes), right
// green (the bytes written after it).
static bool rowBufHeld() {
    RV l[6], r[6];
    listLeft(l, TRED), listRight(r, TGRN);
    const Px px[] = {{64, 128, TRED}, {192, 128, TGRN}};
    return frames(BLUE, px, 2, [&] {
        Com<IDirect3DVertexBuffer9> vb;
        CK(CreateVertexBuffer, sizeof l, 0, 0, D3DPOOL_DEFAULT, &vb.p, nullptr);
        void *p = nullptr;
        HRESULT hr = vb.p->Lock(0, 0, &p, 0);
        if (FAILED(hr)) return fail("Lock", hr);
        memcpy(p, l, sizeof l);
        bool ok = rvAt(vb.p) && drawList();
        if (ok) memcpy(p, r, sizeof r), ok = drawList();
        if (FAILED(hr = vb.p->Unlock())) return fail("Unlock", hr);
        return ok;
    });
}
// buf-relock: a white left quad (listRhw's) drawn once and read back white; then Lock(40, 20), vertex 2 alone, rewrites
// its colour red; cleared and drawn again: 8 inside vertex 2's corner (16,64), at (24,72), the first triangle is red
// 255 with green below 128 (37 interpolated), and 8 inside the second triangle's corner (112,192), at (104,184), which
// no red vertex touches, white.
static bool rowBufRelock() {
    return until([&] {
        RV q[6];
        listLeft(q, TWHT);
        RV v2 = q[2];
        v2.color = TRED;
        Com<IDirect3DVertexBuffer9> vb;
        const auto drawn = [&] { return clearTo(BLUE) && scene([&] { return rvAt(vb.p) && drawList(); }); };
        if (!mkVb(vb, q, sizeof q) || !drawn() || !rtHas("the first draw", rtSurf, 256, X8, {{64, 128, TWHT}}) ||
            !put(vb.p, 2 * sizeof(RV), &v2, sizeof v2) || !drawn())
            return false;
        return readTarget(rtSurf, 256, 256, X8, [&](const BYTE *b, int pitch) {
            const DWORD c = texel(b, pitch, 24, 72);
            return want("(24,72)", c, TRED, 0xff0000) && inBand("(24,72)'s green", (int)(c >> 8 & 0xff), 0, 127) &&
                   want("(104,184)", texel(b, pitch, 104, 184), TWHT);
        });
    });
}
// POSITIONT float4 at stream 0 and COLOR0 D3DCOLOR at stream 1, under the fixed-function stage: the left quad's six
// positions (listRhw's) at stream 0, `n` colours at stream 1 from byte `off` with `stride`, drawn as a list.
static bool twoStreams(const DWORD *c, UINT n, UINT off, UINT stride, const Px *px, int np) {
    static const D3DVERTEXELEMENT9 decl[] = {
        {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITIONT, 0},
        {1, 0, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0}, D3DDECL_END()};
    struct P4 { float x, y, z, rhw; } p4[6];
    RV q[6];
    listLeft(q, 0);
    for (int i = 0; i < 6; i++) p4[i] = {q[i].x, q[i].y, q[i].z, q[i].rhw};
    Prog p;
    Com<IDirect3DVertexBuffer9> pos, col;
    if (!make(p, decl, nullptr, nullptr) || !mkVb(pos, p4, sizeof p4) || !mkVb(col, c, n * 4)) return false;
    return frames(BLUE, px, np, [&] {
        if (!use(p)) return false;
        CK(SetStreamSource, 0, pos.p, 0, sizeof *p4);
        CK(SetStreamSource, 1, col.p, off, stride);
        return drawList();
    });
}
// buf-streams: stream 1 four red entries then six green, bound at an offset of 16 bytes: the left quad green.
static bool rowBufStreams() {
    const DWORD r = TRED, g = TGRN, c[10] = {r, r, r, r, g, g, g, g, g, g};
    const Px px[] = {{64, 128, TGRN}};
    return twoStreams(c, 10, 16, 4, px, 1);
}
// buf-zero-stride: stream 1 green, red, white, black, red, white at offset 0 and stride 0, every vertex reading the
// first: 8 inside the two corners vertex 0 does not hold, (24,72) (vertex 2's) and (104,184) (vertex 4's), both green.
static bool rowBufZeroStride() {
    const DWORD c[6] = {TGRN, TRED, TWHT, 0xff000000, TRED, TWHT};
    const Px px[] = {{24, 72, TGRN}, {104, 184, TGRN}};
    return twoStreams(c, 6, 0, 0, px, 2);
}
// vs_1_1 { dcl_position v0; dcl_texcoord0 v1; mov oPos, v0; mov oD0, v1.wwww }.
static const DWORD VS11_V1W[] = {
    0xfffe0101,                         // vs_1_1
    0x0000001f, 0x80000000, 0x900f0000, // dcl_position v0
    0x0000001f, 0x80000005, 0x900f0001, // dcl_texcoord0 v1
    0x00000001, 0xc00f0000, 0x90e40000, // mov oPos, v0
    0x00000001, 0xd00f0000, 0x90ff0001, // mov oD0, v1.wwww
    0x0000ffff};
// POSITION float3 at stream 0 and TEXCOORD0 at stream 1 as float3 (it reads w = 1 with nothing bound) or as D3DCOLOR (w
// 0).
static const D3DVERTEXELEMENT9 DECL_NULL_F3[] = {
    {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {1, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0}, D3DDECL_END()};
static const D3DVERTEXELEMENT9 DECL_NULL_C[] = {
    {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {1, 0, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0}, D3DDECL_END()};
// The left quad in clip space under `decl`, VS11_V1W and ps_1_1's colour, nothing bound at stream 1, from a buffer at
// stream 0 or by DrawPrimitiveUP: (64,128) `want`.
static bool nullCell(const D3DVERTEXELEMENT9 *decl, DWORD want, bool up) {
    Prog p;
    V3 q[4];
    quad(q, -0.875f, -0.5f, -0.125f, 0.5f);
    Com<IDirect3DVertexBuffer9> vb;
    if (!make(p, decl, VS11_V1W, PS11_V0) || (!up && !mkVb(vb, q, sizeof q))) return false;
    const Px px[] = {{64, 128, want}};
    return frames(BLUE, px, 1, [&] {
        if (!use(p)) return false;
        if (up) return strip(q);
        CK(SetStreamSource, 0, vb.p, 0, sizeof *q);
        CK(DrawPrimitive, D3DPT_TRIANGLESTRIP, 0, 2);
        return true;
    });
}
// buf-null: nullCell's float3 element white (what mtld3d gives: the control decides) and its D3DCOLOR one black, from a
// buffer. Then Wine's test_null_stream shape: the fixed-function stage over POSITIONT and COLOR0 at stream 0 and a
// NORMAL at stream 1, which a pretransformed draw does not consume, nothing bound there: D3D_OK and the left quad red.
static bool rowBufNull() {
    static const D3DVERTEXELEMENT9 decl[] = {
        {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITIONT, 0},
        {0, 16, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
        {1, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0}, D3DDECL_END()};
    if (!nullCell(DECL_NULL_F3, TWHT, false) || !nullCell(DECL_NULL_C, 0x000000, false)) return false;
    RV q[6];
    listLeft(q, TRED);
    Prog p;
    Com<IDirect3DVertexBuffer9> vb;
    if (!make(p, decl, nullptr, nullptr) || !mkVb(vb, q, sizeof q)) return false;
    const Px px[] = {{64, 128, TRED}};
    return frames(BLUE, px, 1, [&] {
        if (!use(p)) return false;
        CK(SetStreamSource, 0, vb.p, 0, sizeof(RV));
        return answer("DrawPrimitive (a stream the stage does not consume)", dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2),
                      D3D_OK);
    });
}
// buf-null-up: buf-null's first two cells by DrawPrimitiveUP: white (the control decides), black.
static bool rowBufNullUp() { return nullCell(DECL_NULL_F3, TWHT, true) && nullCell(DECL_NULL_C, 0x000000, true); }
// vs_3_0 { dcl_position v0; dcl_texcoord0 v1; dcl_position o0; mov r0, v0; add r0.x, r0.x, v1.x; mov o0, r0 }: the
// position moved right by v1.x (a float1 element reads w = 1, so x alone is added).
static const DWORD VS30_SHIFT[] = {
    0xfffe0300,                                     // vs_3_0
    0x0200001f, 0x80000000, 0x900f0000,             // dcl_position v0
    0x0200001f, 0x80000005, 0x900f0001,             // dcl_texcoord0 v1
    0x0200001f, 0x80000000, 0xe00f0000,             // dcl_position o0
    0x02000001, 0x800f0000, 0x90e40000,             // mov r0, v0
    0x03000002, 0x80010000, 0x80000000, 0x90000001, // add r0.x, r0.x, v1.x
    0x02000001, 0xe00f0000, 0x80e40000,             // mov o0, r0
    0x0000ffff};
static const DWORD PS30_GREEN[] = {
    0xffff0300,                                                             // ps_3_0
    0x05000051, 0xa00f0000, 0x00000000, 0x3f800000, 0x00000000, 0x3f800000, // def c0, 0, 1, 0, 1
    0x02000001, 0x800f0800, 0xa0e40000,                                     // mov oC0, c0
    0x0000ffff};
// buf-instanced: VS30_SHIFT and PS30_GREEN; stream 0 a quad over pixels (16,104)-(64,152) (four vertices, six indices)
// at INDEXEDDATA | 3, stream 1 three x offsets of 0, 80 and 160 pixels (FLOAT1 TEXCOORD0 in clip units: 0, 0.625,
// 1.25) at INSTANCEDATA | 1: DrawIndexedPrimitive draws three quads, (40,128) (120,128) (200,128) green; DrawPrimitive
// of the same quad as a six-vertex list draws one, at the first offset: (40,128) green, (120,128) and (200,128) blue
// (non-indexed draws never instance). With stream 1's word INSTANCEDATA | 0 (a constant stream) DrawIndexedPrimitive
// draws every instance at the first offset: the same pixels.
static bool rowBufInstanced() {
    static const D3DVERTEXELEMENT9 decl[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {1, 0, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0}, D3DDECL_END()};
    V3 q[4];
    quad(q, -0.875f, -0.1875f, -0.5f, 0.1875f);
    const V3 l[6] = {q[0], q[1], q[2], q[2], q[1], q[3]};
    const WORD idx[6] = {0, 1, 2, 2, 1, 3};
    const float dx[3] = {0, 0.625f, 1.25f};
    Prog p;
    Com<IDirect3DVertexBuffer9> quadVb, listVb, offsets;
    Com<IDirect3DIndexBuffer9> ib;
    if (!make(p, decl, VS30_SHIFT, PS30_GREEN) || !mkVb(quadVb, q, sizeof q) || !mkVb(listVb, l, sizeof l) ||
        !mkVb(offsets, dx, sizeof dx) || !mkIb(ib, idx, sizeof idx, D3DFMT_INDEX16))
        return false;
    const Px three[] = {{40, 128, TGRN}, {120, 128, TGRN}, {200, 128, TGRN}},
             one[] = {{40, 128, TGRN}, {120, 128, BLUE}, {200, 128, BLUE}};
    const auto draw = [&](IDirect3DVertexBuffer9 *v, UINT word, bool indexed) {
        if (!use(p)) return false;
        CK(SetStreamSource, 0, v, 0, sizeof(V3));
        CK(SetStreamSource, 1, offsets.p, 0, sizeof(float));
        CK(SetStreamSourceFreq, 0, D3DSTREAMSOURCE_INDEXEDDATA | 3);
        CK(SetStreamSourceFreq, 1, word);
        CK(SetIndices, ib.p);
        if (!indexed) return drawList();
        CK(DrawIndexedPrimitive, D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2);
        return true;
    };
    return frames(BLUE, three, 3, [&] { return draw(quadVb.p, D3DSTREAMSOURCE_INSTANCEDATA | 1, true); }) &&
           frames(BLUE, one, 3, [&] { return draw(listVb.p, D3DSTREAMSOURCE_INSTANCEDATA | 1, false); }) &&
           frames(BLUE, one, 3, [&] { return draw(quadVb.p, D3DSTREAMSOURCE_INSTANCEDATA, true); });
}
// vs_1_1 { dcl_position v0; m4x4 oPos, v0, c0 }.
static const DWORD VS11_M4X4[] = {
    0xfffe0101,                                     // vs_1_1
    0x0000001f, 0x80000000, 0x900f0000,             // dcl_position v0
    0x00000014, 0xc00f0000, 0x90e40000, 0xa0e40000, // m4x4 oPos, v0, c0
    0x0000ffff};
// buf-shader: a float3 POSITION buffer of a quad over clip x -0.375..0.375 (pixels 80-176) and y -0.5..0.5, under
// VS11_M4X4 with c0-c3 a translation by -0.5 in x and PS_C0 red: it lands on the left quad, (64,128) red, (160,128)
// blue. Then the fixed-function untransformed path: an XYZ|DIFFUSE green quad over pixels 32-224, WORLD halving x
// (pixels 80-176): (128,128) green, (48,128), inside the unscaled quad's edge, blue.
static bool rowBufShader() {
    V3 q[4];
    quad(q, -0.375f, -0.5f, 0.375f, 0.5f);
    FC f[4];
    cquad(f, -0.75f, -0.5f, 0.75f, 0.5f, TGRN);
    Prog p;
    Com<IDirect3DVertexBuffer9> a, b;
    if (!make(p, DECL_XYZ, VS11_M4X4, PS_C0) || !mkVb(a, q, sizeof q) || !mkVb(b, f, sizeof f)) return false;
    const float m[4][4] = {{1, 0, 0, -0.5f}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    D3DMATRIX half = IDENTITY;
    half._11 = 0.5f;
    const Px vs[] = {{64, 128, TRED}, {160, 128, BLUE}}, ff[] = {{128, 128, TGRN}, {48, 128, BLUE}};
    return frames(BLUE, vs, 2, [&] {
        if (!use(p)) return false;
        CK(SetVertexShaderConstantF, 0, &m[0][0], 4);
        CK(SetPixelShaderConstantF, 0, RED, 1);
        CK(SetStreamSource, 0, a.p, 0, sizeof *q);
        CK(DrawPrimitive, D3DPT_TRIANGLESTRIP, 0, 2);
        return true;
    }) && frames(BLUE, ff, 2, [&] {
        CK(SetVertexShader, nullptr);
        CK(SetPixelShader, nullptr);
        CK(SetFVF, FVF_C);
        CK(SetTransform, D3DTS_WORLD, &half);
        CK(SetStreamSource, 0, b.p, 0, sizeof *f);
        CK(DrawPrimitive, D3DPT_TRIANGLESTRIP, 0, 2);
        return true;
    });
}
// buf-tex: XYZRHW|TEX1 vertices from a buffer, a quad over (64,64)-(192,192) with u and v from 0 to 1, sampling a 2x2
// texture (red, green over white, yellow) point-sampled through stage 0's SELECTARG1 TEXTURE: (96,96) red, (160,96)
// green, (96,160) white, (160,160) yellow.
static bool rowBufTex() {
    static const DWORD texels[4] = {TRED, TGRN, TWHT, TYEL};
    const struct { float x, y, z, rhw, u, v; } q[4] = {
        {64, 64, 0.5f, 1, 0, 0}, {192, 64, 0.5f, 1, 1, 0}, {64, 192, 0.5f, 1, 0, 1}, {192, 192, 0.5f, 1, 1, 1}};
    Com<IDirect3DTexture9> t;
    Com<IDirect3DVertexBuffer9> vb;
    if (!make2d(t, 2, 2, 1, D3DFMT_X8R8G8B8, 4, [](int x, int y) { return texels[x + 2 * y]; }) || !pointClamp(0) ||
        !mkVb(vb, q, sizeof q))
        return false;
    CK(SetTexture, 0, t.p);
    CK(SetTextureStageState, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    const Px px[] = {{96, 96, TRED}, {160, 96, TGRN}, {96, 160, TWHT}, {160, 160, TYEL}};
    return frames(BLUE, px, 4, [&] {
        CK(SetFVF, D3DFVF_XYZRHW | D3DFVF_TEX1);
        CK(SetStreamSource, 0, vb.p, 0, sizeof *q);
        CK(DrawPrimitive, D3DPT_TRIANGLESTRIP, 0, 2);
        return true;
    });
}
// buf-nobuffer: what the draws answer without their buffers, the FVF set, the frame left blue at (64,128), where the
// bound vertices' red quad would be (each answer as mtld3d gives it: the control decides). DrawPrimitive with nothing at
// stream 0: D3DERR_INVALIDCALL. DrawIndexedPrimitive with the vertices and no index buffer: D3DERR_INVALIDCALL; with
// neither buffer: D3D_OK, nothing drawn; as a fan with neither: D3DERR_INVALIDCALL (a fan's index checks come before
// the streams'). An indexed fan from index 2^20 (2 MiB in, past any page of a 12-byte index buffer):
// D3DERR_INVALIDCALL. DrawPrimitive of 0 primitives: D3D_OK, nothing drawn; of primitive type 7: D3DERR_INVALIDCALL.
static bool rowBufNoBuffer() {
    RV q[6];
    listLeft(q, TRED);
    const WORD idx[6] = {0, 1, 2, 3, 4, 5};
    Com<IDirect3DVertexBuffer9> vb;
    Com<IDirect3DIndexBuffer9> ib;
    if (!mkVb(vb, q, sizeof q) || !mkIb(ib, idx, sizeof idx, D3DFMT_INDEX16)) return false;
    const Px px[] = {{64, 128, BLUE}};
    return frames(BLUE, px, 1, [&] {
        CK(SetFVF, FVF_RV);
        CK(SetStreamSource, 0, nullptr, 0, 0);
        CK(SetIndices, nullptr);
        if (!answer("DrawPrimitive (nothing at stream 0)", dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2),
                    D3DERR_INVALIDCALL))
            return false;
        CK(SetStreamSource, 0, vb.p, 0, sizeof(RV));
        if (!answer("DrawIndexedPrimitive (no index buffer)",
                    dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 6, 0, 2), D3DERR_INVALIDCALL))
            return false;
        CK(SetStreamSource, 0, nullptr, 0, 0);
        if (!answer("DrawIndexedPrimitive (neither buffer)", dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 6, 0, 2),
                    D3D_OK) ||
            !answer("DrawIndexedPrimitive (a fan, neither buffer)",
                    dev->DrawIndexedPrimitive(D3DPT_TRIANGLEFAN, 0, 0, 6, 0, 2), D3DERR_INVALIDCALL))
            return false;
        CK(SetStreamSource, 0, vb.p, 0, sizeof(RV));
        CK(SetIndices, ib.p);
        return answer("DrawIndexedPrimitive (a fan past the index buffer)",
                      dev->DrawIndexedPrimitive(D3DPT_TRIANGLEFAN, 0, 0, 6, 1u << 20, 2), D3DERR_INVALIDCALL) &&
               answer("DrawPrimitive (0 primitives)", dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 0), D3D_OK) &&
               answer("DrawPrimitive (type 7)", dev->DrawPrimitive((D3DPRIMITIVETYPE)7, 0, 2), D3DERR_INVALIDCALL);
    });
}
// buf-release: a buffer drawn (left red), unbound and released inside its frame, which is presented; a new buffer
// drawn, unbound and released the same way: its pixels (right green, left blue).
static bool rowBufRelease() {
    RV a[6], b[6];
    listLeft(a, TRED), listRight(b, TGRN);
    const auto drawn = [&](const RV *v) {
        Com<IDirect3DVertexBuffer9> vb;
        if (!mkVb(vb, v, sizeof a) || !rvAt(vb.p) || !drawList()) return false;
        CK(SetStreamSource, 0, nullptr, 0, 0);
        return true;
    };
    const Px first[] = {{64, 128, TRED}}, second[] = {{192, 128, TGRN}, {64, 128, BLUE}};
    return frames(BLUE, first, 1, [&] { return drawn(a); }) && frames(BLUE, second, 2, [&] { return drawn(b); });
}
// DXMTRS C4c-2a's rows: stream offsets off four bytes and attributes that end past their stream's stride, which
// mtld3d draws: a stream at an offset off four bytes binds at the multiple of 4 below and its attributes read the
// remainder further on; an attribute that ends past the stride is fetched through a slot of its own that binds the
// same stream advanced by the attribute's offset, so vertex i reads stream offset + i * stride + element offset as
// D3D9 addresses it. Every buffer holds marked bytes (0x5a, or a marked float) before and after what the draws read,
// never zeros, so a read at a wrong offset, slot or advance gives another position or colour.

// `n` RV vertices (at most 9) into a new buffer from byte `off` (at most 6), the `off` bytes before them and 4 after
// them 0x5a.
static bool shiftedVb(Com<IDirect3DVertexBuffer9> &b, UINT off, const RV *v, UINT n) {
    BYTE bytes[6 + 9 * sizeof(RV) + 4];
    memset(bytes, 0x5a, sizeof bytes);
    memcpy(bytes + off, v, n * sizeof *v);
    return mkVb(b, bytes, off + n * sizeof *v + 4);
}
// buf-odd: stream offsets off four bytes, the vertices read exactly those written (shiftedVb); a read 1-3 bytes off
// takes parts of two fields: other positions (2 bytes off, near 0: these whole-pixel floats' low halves are zero) and
// colours of another hue (green read 2 bytes early is 0x003f80). Frame 1: the left quad (listRhw's six vertices) red
// at offset 0, then under the same FVF the right quad green at offset 2: left red, right green. Frame 2: the left quad
// red at offset 1, the right yellow at offset 3. Frame 3: offset 6 and start vertex 1 over seven vertices, 1-6 the
// right quad red and 0 a copy of 1: (160,152) red (from vertex 0 the first triangle is degenerate and the second the
// quad's upper-right half, which leaves (160,152) blue), (64,128) blue. Frame 4: at offset 2 eight vertices, a white
// left quad then a green one (strip order), indices 0 1 2 2 1 3, DrawIndexedPrimitive(TRIANGLELIST, 4, 0, 4, 0, 2):
// left green (the base ignored draws white), right blue. Frame 5: twoStreams' positions at offset 0 of stream 0 and
// six orange D3DCOLORs 0xffff8000 (bytes 00 80 ff ff) at offset 2 of stream 1, so only stream 1 shifts: left 0xff8000
// (read 2 bytes early, as stream 0's shift of 0 reads them, vertex 0's is 0x80005a5a and the others' 0x8000ffff,
// cyan), right blue.
static bool rowBufOdd() {
    RV red[6], grn[6], yel[6], s[7], q[8];
    listLeft(red, TRED), listRight(grn, TGRN), listRight(yel, TYEL), listRight(s + 1, TRED), s[0] = s[1];
    quadRhw(q, 16, 64, 112, 192, TWHT), quadRhw(q + 4, 16, 64, 112, 192, TGRN);
    static const WORD idx[6] = {0, 1, 2, 2, 1, 3};
    const DWORD orange = 0xffff8000;
    DWORD c[7];  // bytes 2-25 the six colours, 0-1 and 26-27 0x5a
    memset(c, 0x5a, sizeof c);
    for (int i = 0; i < 6; i++) memcpy((BYTE *)c + 2 + 4 * i, &orange, 4);
    Com<IDirect3DVertexBuffer9> a, b, d, e, f, g;
    Com<IDirect3DIndexBuffer9> ib;
    if (!shiftedVb(a, 0, red, 6) || !shiftedVb(b, 2, grn, 6) || !shiftedVb(d, 1, red, 6) || !shiftedVb(e, 3, yel, 6) ||
        !shiftedVb(f, 6, s, 7) || !shiftedVb(g, 2, q, 8) || !mkIb(ib, idx, sizeof idx, D3DFMT_INDEX16))
        return false;
    const Px one[] = {{64, 128, TRED}, {192, 128, TGRN}}, two[] = {{64, 128, TRED}, {192, 128, TYEL}},
             three[] = {{160, 152, TRED}, {64, 128, BLUE}}, four[] = {{64, 128, TGRN}, {192, 128, BLUE}},
             five[] = {{64, 128, 0xff8000}, {192, 128, BLUE}};
    const auto pair = [&](IDirect3DVertexBuffer9 *l, UINT lo, IDirect3DVertexBuffer9 *r, UINT ro) {
        return rvAt(l, lo) && drawList() && rvAt(r, ro) && drawList();
    };
    return frames(BLUE, one, 2, [&] { return pair(a.p, 0, b.p, 2); }) &&
           frames(BLUE, two, 2, [&] { return pair(d.p, 1, e.p, 3); }) &&
           frames(BLUE, three, 2, [&] { return rvAt(f.p, 6) && drawList(1); }) && frames(BLUE, four, 2, [&] {
               if (!rvAt(g.p, 2)) return false;
               CK(SetIndices, ib.p);
               CK(DrawIndexedPrimitive, D3DPT_TRIANGLELIST, 4, 0, 4, 0, 2);
               return true;
           }) && twoStreams(c, 7, 2, 4, five, 2);
}
// VS_CROSS with blue from v1.y: vs_2_0 { def c0, 1, 0, 0, 1; dcl_position v0; dcl_texcoord0 v1; mov oPos, v0; mov r0,
// c0; mov r0.y, v1.x; mov r0.z, v1.y; mov oD0, r0 }: the colour (1, v1.x, v1.y).
static const DWORD VS_CROSS_Y[] = {
    0xfffe0200,                                                             // vs_2_0
    0x05000051, 0xa00f0000, 0x3f800000, 0x00000000, 0x00000000, 0x3f800000, // def c0, 1, 0, 0, 1
    0x0200001f, 0x80000000, 0x900f0000,                                     // dcl_position v0
    0x0200001f, 0x80000005, 0x900f0001,                                     // dcl_texcoord0 v1
    0x02000001, 0xc00f0000, 0x90e40000,                                     // mov oPos, v0
    0x02000001, 0x800f0000, 0xa0e40000,                                     // mov r0, c0
    0x02000001, 0x80020000, 0x90000001,                                     // mov r0.y, v1.x
    0x02000001, 0x80040000, 0x90550001,                                     // mov r0.z, v1.y
    0x02000001, 0xd00f0000, 0x80e40000,                                     // mov oD0, r0
    0x0000ffff};
// DXMTRS C4c-1's stride-12 layout: POSITION FLOAT3 at 0 and TEXCOORD0 FLOAT2 at 8, which ends at 16, past a stride
// of 12: vertex i reads floats 3i to 3i + 2 of its stream as its position and 3i + 2 and 3i + 3 as its texcoord, so
// v1.x is its own z and v1.y the next vertex's x, or for the last vertex the float after the vertices.
static const D3DVERTEXELEMENT9 DECL_CROSS[] = {
    {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 8, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0}, D3DDECL_END()};
// Two triangles as six stride-12 vertices (x, y, z) at floats 0-17 of `f`, all at depth z, clockwise: A, its apex at
// pixel (32,64) and its right edge on x = 192 from y 16 to 112; B, its apex at (192,184) and its other two vertices on
// the pixel column of clip x `ex`, at y 136 and 232. Under DECL_CROSS and VS_CROSS_Y a vertex is (1, z, the next
// vertex's x): A's three take x 0.5 (A's last two and B's apex sit on x = 192), B's first two `ex` and B's last f[18],
// so each triangle is one colour when f[18] is `ex`. z is none of the cell's x and y, so a texcoord read from the
// vertex's own start (no advance) gives another green.
static void crossTris(float *f, float z, float ex) {
    // B's vertex 4 above its 5 when its edge is right of its apex, below it otherwise: clockwise either way.
    const float y4 = ex > 0.5f ? -0.0625f : -0.8125f, y5 = ex > 0.5f ? -0.8125f : -0.0625f;
    const float v[6][2] = {{-0.75f, 0.5f}, {0.5f, 0.875f}, {0.5f, 0.125f}, {0.5f, -0.4375f}, {ex, y4}, {ex, y5}};
    for (int i = 0; i < 6; i++) f[3 * i] = v[i][0], f[3 * i + 1] = v[i][1], f[3 * i + 2] = z;
}
// crossTris(f, 0.25, 0.875) with f[18] 0.875 under VS_CROSS_Y: A 0xff4080 at (120,64); B 0xff40df at (228,204), where
// B's last vertex, coloured by f[18], weighs 7/12; (64,192) the clear.
static const Px CROSS_PX[] = {{120, 64, 0xff4080}, {228, 204, 0xff40df}, {64, 192, BLUE}};
// A frame of CROSS_PX: `p` (DECL_CROSS, VS_CROSS_Y), `vb` at stream 0 from byte `off` at a stride of 12, then draw().
template <class F> static bool crossFrame(const Prog &p, IDirect3DVertexBuffer9 *vb, UINT off, F draw) {
    return frames(BLUE, CROSS_PX, 3, [&] {
        if (!use(p)) return false;
        CK(SetStreamSource, 0, vb, off, 12);
        return draw();
    });
}
// buf-cross: DECL_CROSS from buffers through DrawPrimitive, three cells. DXMTRS C4c-1's: the left quad in clip space
// (z 0.5) as a strip under VS_CROSS (1, v1.x, 0) and a marked float after it, which VS_CROSS does not read: v1.x is
// each vertex's own z, left 0xff8000 (with no advance v1.x is its x, negative: red). crossTris(z 0.25, B's edge at
// 0.875, pixel 240) with 0.875 after the six as a list under VS_CROSS_Y: CROSS_PX, B's last vertex coloured by the
// float after the six. SetFVF XYZ|NORMAL|DIFFUSE (the normal at 12, the colour at 24: 28 bytes) at a stride of 16
// under the fixed-function stage, LIGHTING off, the left quad as a strip and a fifth vertex: vertex i's colour is the
// bytes at 16i + 24, vertex i + 1's z, and every z is the float 0x3e808000 (0.2509765625), which as a D3DCOLOR is
// 0x808000: left 0x808000 (with no advance the colour is the vertex's own x, -0.875 or -0.125: 0x600000 or black).
// The normal, past the stride too (its first float the vertex's marked fourth), is unused with LIGHTING off.
static bool rowBufCross() {
    V3 c[4];
    quad(c, -0.875f, -0.5f, -0.125f, 0.5f);
    const DWORD zc = 0x3e808000, mark = 0x3ec00000;  // mark: 0.375
    float q[13], t[20];
    DWORD n[20];
    for (int i = 0; i < 4; i++) {
        q[3 * i] = c[i].x, q[3 * i + 1] = c[i].y, q[3 * i + 2] = 0.5f;
        memcpy(n + 4 * i, &c[i], 8), n[4 * i + 2] = zc, n[4 * i + 3] = mark;
    }
    q[12] = 0.375f, n[16] = n[17] = n[19] = mark, n[18] = zc;
    crossTris(t, 0.25f, 0.875f), t[18] = 0.875f, t[19] = 0.375f;
    Prog x, y;
    Com<IDirect3DVertexBuffer9> a, b, d;
    if (!make(x, DECL_CROSS, VS_CROSS, PS_V0) || !make(y, DECL_CROSS, VS_CROSS_Y, PS_V0) || !mkVb(a, q, sizeof q) ||
        !mkVb(b, t, sizeof t) || !mkVb(d, n, sizeof n))
        return false;
    const Px orange[] = {{64, 128, 0xff8000}, {192, 128, BLUE}}, olive[] = {{64, 128, 0x808000}, {192, 128, BLUE}};
    return frames(BLUE, orange, 2, [&] {
               if (!use(x)) return false;
               CK(SetStreamSource, 0, a.p, 0, 12);
               CK(DrawPrimitive, D3DPT_TRIANGLESTRIP, 0, 2);
               return true;
           }) && crossFrame(y, b.p, 0, [] { return drawList(); }) && frames(BLUE, olive, 2, [&] {
               CK(SetVertexShader, nullptr);
               CK(SetPixelShader, nullptr);
               CK(SetFVF, FVF_NC);
               CK(SetStreamSource, 0, d.p, 0, 16);
               CK(DrawPrimitive, D3DPT_TRIANGLESTRIP, 0, 2);
               return true;
           });
}
// buf-cross-up: crossTris(z 0.25, B's edge at 0, pixel 128) under VS_CROSS_Y from the application's memory at a stride
// of 12, the six floats after the six vertices 1.0: DrawPrimitiveUP(TRIANGLELIST, 2), then DrawIndexedPrimitiveUP of
// indices 3 4 5 0 1 2 (B first; MinVertexIndex 0 and 6 vertices, so vertex 5 is the last one copied). mtld3d copies
// the 6 x 12 bytes D3D9 promises, zero-filled up to the last vertex's texcoord end, and reads nothing past them, so B's
// last vertex reads 0 as its first two read its x: A (120,64) 0xff4080; B (140,160) 0xff4000, where B's last vertex
// weighs 21/32 (blue 167 had it read the 1.0 after the six); (64,192) the clear.
static bool rowBufCrossUp() {
    float f[24];
    crossTris(f, 0.25f, 0.0f);
    for (int i = 18; i < 24; i++) f[i] = 1.0f;
    static const WORD idx[6] = {3, 4, 5, 0, 1, 2};
    static const Px px[] = {{120, 64, 0xff4080}, {140, 160, 0xff4000}, {64, 192, BLUE}};
    Prog p;
    if (!make(p, DECL_CROSS, VS_CROSS_Y, PS_V0)) return false;
    return frames(BLUE, px, 3, [&] {
               if (!use(p)) return false;
               CK(DrawPrimitiveUP, D3DPT_TRIANGLELIST, 2, f, 12);
               return true;
           }) && frames(BLUE, px, 3, [&] {
               if (!use(p)) return false;
               CK(DrawIndexedPrimitiveUP, D3DPT_TRIANGLELIST, 0, 6, 2, idx, D3DFMT_INDEX16, f, 12);
               return true;
           });
}
// vs_2_0 { dcl_position v0; dcl_texcoord0 v1; dcl_texcoord1 v2; dcl_color v3; dcl_texcoord2 v4; mov oPos, v0; mov r0,
// v3; add r0.x, r0.x, v4.x; mov r0.y, v1.x; mov r0.z, v2.z; mov oD0, r0 }: red the colour's plus v4.x, green v1.x,
// blue v2.z.
static const DWORD VS_MULTI[] = {
    0xfffe0200,                                     // vs_2_0
    0x0200001f, 0x80000000, 0x900f0000,             // dcl_position v0
    0x0200001f, 0x80000005, 0x900f0001,             // dcl_texcoord0 v1
    0x0200001f, 0x80010005, 0x900f0002,             // dcl_texcoord1 v2
    0x0200001f, 0x8000000a, 0x900f0003,             // dcl_color v3
    0x0200001f, 0x80020005, 0x900f0004,             // dcl_texcoord2 v4
    0x02000001, 0xc00f0000, 0x90e40000,             // mov oPos, v0
    0x02000001, 0x800f0000, 0x90e40003,             // mov r0, v3
    0x03000002, 0x80010000, 0x80000000, 0x90000004, // add r0.x, r0.x, v4.x
    0x02000001, 0x80020000, 0x90000001,             // mov r0.y, v1.x
    0x02000001, 0x80040000, 0x90aa0002,             // mov r0.z, v2.z
    0x02000001, 0xd00f0000, 0x80e40000,             // mov oD0, r0
    0x0000ffff};
// buf-cross-multi: three streams under VS_MULTI. Stream 0 at a stride of 12: POSITION FLOAT3 at 0, which fits, and
// TEXCOORD1 FLOAT3 at 4 and TEXCOORD0 FLOAT2 at 8, which both end at 16; stream 1 six D3DCOLORs 0x00c00000 and a marked
// one at a stride of 4; stream 2 a TEXCOORD2 FLOAT1 with nothing bound, read as 0. Streams 0-2 keep their slots, so
// mtld3d fetches the two crossing attributes at slots 3 and 4, each binding stream 0 advanced by its offset: a slot's
// number is not its stream's. Stream 0 holds crossTris(z 0.25, B's edge at 0.875) and 0.875 after the six: v1.x is z
// (green 0x40), v2 is (y, z, the next vertex's x) and gives blue, stream 1 red 0xc0: A (120,64) 0xc04080, B (228,204)
// 0xc040df, (64,192) the clear. Then stream 1 alone crosses: POSITION FLOAT3 at stream 0 (the left quad as a strip,
// z 0.5) and TEXCOORD0 FLOAT2 at offset 4 of stream 1 at a stride of 8, so no attribute of stream 1 fits and the
// texcoord takes stream 1's own slot advanced by 4. Stream 1 holds five (p, q) pairs, p 0.75 but the first's 0.125, q
// 0.25 but the last's 0.375, and vertex i reads (q_i, p_i+1) under VS_CROSS_Y: left 0xff40bf (with no advance it reads
// (p_i, q_i): green 0xbf, blue 0x40), right blue.
static bool rowBufCrossMulti() {
    static const D3DVERTEXELEMENT9 three[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 4, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
        {0, 8, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        {1, 0, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
        {2, 0, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2}, D3DDECL_END()};
    static const D3DVERTEXELEMENT9 one[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {1, 4, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0}, D3DDECL_END()};
    static const DWORD col[7] = {0x00c00000, 0x00c00000, 0x00c00000, 0x00c00000, 0x00c00000, 0x00c00000, 0x5a5a5a5a};
    float t[20], s[10];
    crossTris(t, 0.25f, 0.875f), t[18] = 0.875f, t[19] = 0.375f;
    for (int i = 0; i < 5; i++) s[2 * i] = i ? 0.75f : 0.125f, s[2 * i + 1] = i < 4 ? 0.25f : 0.375f;
    V3 c[4];
    quad(c, -0.875f, -0.5f, -0.125f, 0.5f);
    Prog p, q;
    Com<IDirect3DVertexBuffer9> a, b, pos, pairs;
    if (!make(p, three, VS_MULTI, PS_V0) || !make(q, one, VS_CROSS_Y, PS_V0) || !mkVb(a, t, sizeof t) ||
        !mkVb(b, col, sizeof col) || !mkVb(pos, c, sizeof c) || !mkVb(pairs, s, sizeof s))
        return false;
    static const Px multi[] = {{120, 64, 0xc04080}, {228, 204, 0xc040df}, {64, 192, BLUE}},
                    own[] = {{64, 128, 0xff40bf}, {192, 128, BLUE}};
    return frames(BLUE, multi, 3, [&] {
               if (!use(p)) return false;
               CK(SetStreamSource, 0, a.p, 0, 12);
               CK(SetStreamSource, 1, b.p, 0, 4);
               CK(SetStreamSource, 2, nullptr, 0, 0);
               return drawList();
           }) && frames(BLUE, own, 2, [&] {
               if (!use(q)) return false;
               CK(SetStreamSource, 0, pos.p, 0, sizeof(V3));
               CK(SetStreamSource, 1, pairs.p, 0, 8);
               CK(DrawPrimitive, D3DPT_TRIANGLESTRIP, 0, 2);
               return true;
           });
}
// buf-cross-odd: crossTris(z 0.25, B's edge at 0.875) and 0.875 after the six from byte 2 of a buffer whose first two
// bytes and last two are 0x5a, bound at offset 2 under VS_CROSS_Y: mtld3d binds stream 0 at 0 and the crossing
// texcoord's slot at 8, each attribute 2 bytes further on: CROSS_PX (read 2 bytes early, each float takes the zero low
// half of the next: no triangle where it should be).
static bool rowBufCrossOdd() {
    float t[20];
    crossTris(t, 0.25f, 0.875f), t[18] = 0.875f, t[19] = 0.375f;
    BYTE b[2 + sizeof t + 2];
    memset(b, 0x5a, sizeof b), memcpy(b + 2, t, sizeof t);
    Prog p;
    Com<IDirect3DVertexBuffer9> vb;
    if (!make(p, DECL_CROSS, VS_CROSS_Y, PS_V0) || !mkVb(vb, b, sizeof b)) return false;
    return crossFrame(p, vb.p, 2, [] { return drawList(); });
}
// buf-cross-indexed's fan as four stride-12 vertices at depth z, (32,128) then (192,32), (192,128), (192,224) on the
// column x = 192, clockwise as a fan from the first: under VS_CROSS_Y each vertex's blue is the next vertex's x, 0.5,
// when the float after the four is 0.5.
static void crossFan(float *f, float z) {
    const float v[4][2] = {{-0.75f, 0.0f}, {0.5f, 0.75f}, {0.5f, 0.0f}, {0.5f, -0.75f}};
    for (int i = 0; i < 4; i++) f[3 * i] = v[i][0], f[3 * i + 1] = v[i][1], f[3 * i + 2] = z;
}
// buf-cross-indexed: DECL_CROSS under VS_CROSS_Y from buffers, indexed at a base vertex. Twelve vertices,
// crossTris(z 0.75) then crossTris(z 0.25), B's edge at 0.875 in both, then 0.875; 16-bit indices 0-5,
// DrawIndexedPrimitive(TRIANGLELIST, 6, 0, 6, 0, 2): CROSS_PX (the base ignored draws the first six, green 0xbf). Then
// a fan: eight vertices, crossFan(z 0.625) then crossFan(z 0.375), then 0.5; indices 0-3,
// DrawIndexedPrimitive(TRIANGLEFAN, 4, 0, 4, 0, 2): (150,100) in its first triangle and (150,156) in its second
// 0xff6080 (the base ignored, green 0x9f), (224,128) the clear.
static bool rowBufCrossIndexed() {
    float t[38], g[26];
    crossTris(t, 0.75f, 0.875f), crossTris(t + 18, 0.25f, 0.875f), t[36] = 0.875f, t[37] = 0.375f;
    crossFan(g, 0.625f), crossFan(g + 12, 0.375f), g[24] = 0.5f, g[25] = 0.375f;
    static const WORD list[6] = {0, 1, 2, 3, 4, 5}, fan[4] = {0, 1, 2, 3};
    Prog p;
    Com<IDirect3DVertexBuffer9> a, b;
    Com<IDirect3DIndexBuffer9> ia, ib;
    if (!make(p, DECL_CROSS, VS_CROSS_Y, PS_V0) || !mkVb(a, t, sizeof t) || !mkVb(b, g, sizeof g) ||
        !mkIb(ia, list, sizeof list, D3DFMT_INDEX16) || !mkIb(ib, fan, sizeof fan, D3DFMT_INDEX16))
        return false;
    static const Px fanPx[] = {{150, 100, 0xff6080}, {150, 156, 0xff6080}, {224, 128, BLUE}};
    return crossFrame(p, a.p, 0, [&] {
               CK(SetIndices, ia.p);
               CK(DrawIndexedPrimitive, D3DPT_TRIANGLELIST, 6, 0, 6, 0, 2);
               return true;
           }) && frames(BLUE, fanPx, 3, [&] {
               if (!use(p)) return false;
               CK(SetStreamSource, 0, b.p, 0, 12);
               CK(SetIndices, ib.p);
               CK(DrawIndexedPrimitive, D3DPT_TRIANGLEFAN, 4, 0, 4, 0, 2);
               return true;
           });
}
// buf-wide: DECL_XYZ's FLOAT3 position at a stride of 8, narrower than it, over a buffer of four 12-byte vertices (the
// left quad, z 0.5) under VS_C0 and PS_V0 green, DrawPrimitive(TRIANGLESTRIP, 0, 2): no binding can fetch it. Under
// strict D3DERR_NOTAVAILABLE and (64,128) the clear; elsewhere D3D_OK and no pixel asserted (mtld3d widens the stride
// to the attribute's 12 and draws what that reads).
static bool rowBufWide() {
    V3 q[4];
    quad(q, -0.875f, -0.5f, -0.125f, 0.5f);
    Prog p;
    Com<IDirect3DVertexBuffer9> vb;
    if (!make(p, DECL_XYZ, VS_C0, PS_V0) || !mkVb(vb, q, sizeof q)) return false;
    const Px px[] = {{64, 128, BLUE, strictMode ? 0xffffffu : 0u}};
    return frames(BLUE, px, 1, [&] {
        if (!use(p)) return false;
        CK(SetVertexShaderConstantF, 0, GREEN, 1);
        CK(SetStreamSource, 0, vb.p, 0, 8);
        return answer("DrawPrimitive (stride 8)", dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2),
                      strictMode ? D3DERR_NOTAVAILABLE : D3D_OK);
    });
}
// DXMTRS C4c-2b's rows: dynamic buffers locked again inside one frame, and a static WRITEONLY buffer locked again after
// its upload released its backing. mtld3d maps a DYNAMIC default-pool buffer directly: a queued draw reads the pages
// the Lock pointer wrote, so a lock without DISCARD or NOOVERWRITE over bytes an earlier draw of the frame reads would
// change that draw. No row makes one: each writes through DISCARD, a whole lock after a draw (a new name, the old bytes
// copied) or NOOVERWRITE past every byte an earlier draw reads. Where two draws of a frame would paint the same
// pixels, each draws in a scissor band of its own, so every asserted pixel is one draw's.

// A DYNAMIC default-pool vertex buffer of n bytes, `usage` added (WRITEONLY or 0).
static bool mkDyn(Com<IDirect3DVertexBuffer9> &b, UINT n, DWORD usage) {
    CK(CreateVertexBuffer, n, D3DUSAGE_DYNAMIC | usage, 0, D3DPOOL_DEFAULT, &b.p, nullptr);
    return true;
}
// The draws that follow inside the scissor band x0 <= x < x1 (the test on; reset() turns it off).
static bool band(LONG x0, LONG x1) { return scissor(x0, 0, x1, 256) && rstate(D3DRS_SCISSORTESTENABLE, TRUE); }
// A wide triangle, its top edge across the target at y0 and its apex (128, y0 + 64): 16 pixels down it spans x 32-224.
static void bandTri(RV *v, float y0, DWORD c) { triRhw(v, 0, y0, 256, y0 + 64, c); }
// Lock(0, 0, 0) of `b` (whole, no flags), `n` vertices of `v` written from vertex `at`, Unlock.
static bool rewrite(IDirect3DVertexBuffer9 *b, UINT at, const RV *v, UINT n) {
    void *p = nullptr;
    HRESULT hr = b->Lock(0, 0, &p, 0);
    if (FAILED(hr)) return fail("Lock (whole)", hr);
    memcpy((RV *)p + at, v, n * sizeof *v);
    hr = b->Unlock();
    return SUCCEEDED(hr) || fail("Unlock", hr);
}
// buf-dyn-preserve: a DYNAMIC buffer (not WRITEONLY) locked whole without flags after a draw, which mtld3d renames and
// whose old bytes it copies into the new pages (PreserveKind::Cpu): a rewrite of part keeps the rest. Small cell, 180
// bytes (one page): three bandTri triangles, T0 at y 16, T1 at 96, T2 at 176, red, drawn in the band x 0-80;
// rewrite(), T0 green, drawn in x 88-168; rewrite() again, T1 yellow, drawn in x 176-256. Left T0 and T1 red (the first
// draw read the fill: the lock after it took a new name); middle T0 green, T1 and T2 red (the copy carried the fill);
// right T0 green (the second copy read the pages the first rewrite wrote, not the name the first draw reads), T1
// yellow, T2 red. Large cell, 4320 bytes (216 vertices): T0 at vertices 0-2 and T1 at 213-215, past byte 4096, each
// drawn alone in x 0-128; rewrite(), T0 green, both drawn in x 128-256: left T0 and T1 red, right T0 green and T1 red
// (the copy carried the bytes past the first page).
static bool rowBufDynPreserve() {
    RV fill[9], g0[3], y1[3], big[216] = {};
    bandTri(fill, 16, TRED), bandTri(fill + 3, 96, TRED), bandTri(fill + 6, 176, TRED);
    bandTri(g0, 16, TGRN), bandTri(y1, 96, TYEL), bandTri(big, 16, TRED), bandTri(big + 213, 96, TRED);
    const Px small[] = {{40, 32, TRED},   {40, 112, TRED},  {128, 32, TGRN},  {128, 112, TRED},
                        {128, 192, TRED}, {216, 32, TGRN},  {216, 112, TYEL}, {216, 192, TRED}},
             large[] = {{64, 32, TRED}, {64, 112, TRED}, {192, 32, TGRN}, {192, 112, TRED}};
    const auto both = [] { return drawList(0, 1) && drawList(213, 1); };
    return frames(BLUE, small, 8, [&] {
               Com<IDirect3DVertexBuffer9> vb;
               return mkDyn(vb, sizeof fill, 0) && put(vb.p, 0, fill, sizeof fill) && rvAt(vb.p) && band(0, 80) &&
                      drawList(0, 3) && rewrite(vb.p, 0, g0, 3) && band(88, 168) && drawList(0, 3) &&
                      rewrite(vb.p, 3, y1, 3) && band(176, 256) && drawList(0, 3);
           }) && frames(BLUE, large, 4, [&] {
               Com<IDirect3DVertexBuffer9> vb;
               return mkDyn(vb, sizeof big, 0) && put(vb.p, 0, big, sizeof big) && rvAt(vb.p) && band(0, 128) &&
                      both() && rewrite(vb.p, 0, g0, 3) && band(128, 256) && both();
           });
}
// buf-dyn-many's colour of draw i: red 255 (never the clear), green and blue stepping with i.
static DWORD manyColour(int i) { return D3DCOLOR_XRGB(255, (i * 7) & 0xff, (i * 13) & 0x7f); }
// One buf-dyn-many cell: a DYNAMIC|WRITEONLY buffer of `length` bytes, n times Lock(DISCARD) of vertices at to at + 2,
// a triangle in cell i of a 20 x 15 grid of 12 x 16 cells from (8,8) written there in manyColour(i), Unlock and
// DrawPrimitive(TRIANGLELIST, at, 1), in one frame. Asserts the eight `probe` cells, each 4 pixels inside its triangle.
// A DISCARD that wrote in place would move the draw before it to the next cell, leaving its own cell the clear.
static bool manyCell(UINT length, UINT at, int n, const int (&probe)[8]) {
    const auto cx = [](int i) { return 8 + 12 * (i % 20); };
    const auto cy = [](int i) { return 8 + 16 * (i / 20); };
    Px px[8];
    for (int k = 0; k < 8; k++) px[k] = {cx(probe[k]) + 6, cy(probe[k]) + 5, manyColour(probe[k])};
    return frames(BLUE, px, 8, [&] {
        Com<IDirect3DVertexBuffer9> vb;
        if (!mkDyn(vb, length, D3DUSAGE_WRITEONLY) || !rvAt(vb.p)) return false;
        for (int i = 0; i < n; i++) {
            RV t[3];
            const float x = (float)cx(i), y = (float)cy(i);
            triRhw(t, x + 1, y + 1, x + 11, y + 15, manyColour(i));
            if (!put(vb.p, at * sizeof(RV), t, sizeof t, D3DLOCK_DISCARD) || !drawList(at, 1)) return false;
        }
        return true;
    });
}
// buf-dyn-many: manyCell over a one-triangle buffer (60 bytes) 300 times, past one page's suballocations into new
// names (on the core a 60-byte name is a page of sixteen 256-byte suballocations: cells 15 and 16 straddle the first
// new name), cells 0, 1 (the first DISCARD after a draw), 2, 15, 16, 17, 150, 299; and over a 4800-byte buffer, its
// triangle at vertices 237-239 (past byte 4096), 40 times, cells 0, 1 (the first new name the draws must bind: with
// the rename left out of the frame the second draw binds the first name, whose triangle sits in cell 0), 2, 15, 16,
// 20, 38, 39.
static bool rowBufDynMany() {
    static const int small[8] = {0, 1, 2, 15, 16, 17, 150, 299}, large[8] = {0, 1, 2, 15, 16, 20, 38, 39};
    return manyCell(3 * sizeof(RV), 0, 300, small) && manyCell(240 * sizeof(RV), 237, 40, large);
}
// buf-dyn-index: a DYNAMIC|WRITEONLY 16-bit index buffer over a static vertex buffer of four quads (quadRhw's order):
// Q0 (16,16)-(112,112) red, Q1 (144,16)-(240,112) green, Q2 (16,144)-(112,240) white, Q3 (144,144)-(240,240) yellow.
// Filled whole through DISCARD (a first lock: in place) with Q0's list 0 1 2 2 1 3 and zeros after it,
// DrawIndexedPrimitive(TRIANGLELIST) from index 0. Lock(DISCARD) of indices 0-5, contended after that draw: Q1's 4 5 6
// 6 5 7, drawn from index 0. A NOOVERWRITE append of indices 6-15, past every index the draws before read: Q2's 8 9 10
// 10 9 11, drawn from index 6, and Q3's fan 12 13 15 14, an indexed TRIANGLEFAN from index 12 (mtld3d rewrites a fan
// on the CPU at its draw, from the pages the Lock pointer wrote). Q0 red: the first draw read the first indices (the
// DISCARD took new pages; one that wrote in place, or a draw that left the buffer unmarked as read, moves that draw
// to Q1). Q1 green, Q2 white; Q3 yellow: the fan read the appended indices (the first fill's pages hold zeros there,
// which draw nothing). Two cells: 16 indices, one page (on the core the DISCARD steps to the next suballocation of the
// same name), and 2052 (4104 bytes, more than a page: the DISCARD takes a new name, which the fan must read, not the
// name the first draw reads).
static bool rowBufDynIndex() {
    RV v[16];
    quadRhw(v, 16, 16, 112, 112, TRED), quadRhw(v + 4, 144, 16, 240, 112, TGRN);
    quadRhw(v + 8, 16, 144, 112, 240, TWHT), quadRhw(v + 12, 144, 144, 240, 240, TYEL);
    static const WORD first[2052] = {0, 1, 2, 2, 1, 3}, second[6] = {4, 5, 6, 6, 5, 7},
                      append[10] = {8, 9, 10, 10, 9, 11, 12, 13, 15, 14};
    Com<IDirect3DVertexBuffer9> vb;
    if (!mkVb(vb, v, sizeof v)) return false;
    const Px px[] = {{64, 64, TRED}, {192, 64, TGRN}, {64, 192, TWHT}, {192, 192, TYEL}};
    const auto cell = [&](UINT bytes) {
        return frames(BLUE, px, 4, [&] {
            Com<IDirect3DIndexBuffer9> ib;
            CK(CreateIndexBuffer, bytes, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &ib.p,
               nullptr);
            if (!put(ib.p, 0, first, bytes, D3DLOCK_DISCARD) || !rvAt(vb.p)) return false;
            CK(SetIndices, ib.p);
            CK(DrawIndexedPrimitive, D3DPT_TRIANGLELIST, 0, 0, 16, 0, 2);
            if (!put(ib.p, 0, second, sizeof second, D3DLOCK_DISCARD)) return false;
            CK(DrawIndexedPrimitive, D3DPT_TRIANGLELIST, 0, 0, 16, 0, 2);
            if (!put(ib.p, 12, append, sizeof append, D3DLOCK_NOOVERWRITE)) return false;
            CK(DrawIndexedPrimitive, D3DPT_TRIANGLELIST, 0, 0, 16, 6, 2);
            CK(DrawIndexedPrimitive, D3DPT_TRIANGLEFAN, 0, 0, 16, 12, 2);
            return true;
        });
    };
    return cell(16 * sizeof *first) && cell(sizeof first);
}
// buf-dyn-read: a DYNAMIC buffer (not WRITEONLY) of the left quad red then the right quad green (listRhw's, twelve
// vertices) written through Lock and unlocked; Lock(0, 0, READONLY) reads those 240 bytes; the left quad drawn;
// Lock(0, 0, NOOVERWRITE) after that draw reads them too (both write in place: new pages would hold other bytes); the
// right quad drawn. Left red, right green: each draw read the bytes written.
static bool rowBufDynRead() {
    RV q[12];
    listLeft(q, TRED), listRight(q + 6, TGRN);
    const auto reads = [&](IDirect3DVertexBuffer9 *b, DWORD flags, const char *what) {
        void *p = nullptr;
        HRESULT hr = b->Lock(0, 0, &p, flags);
        if (FAILED(hr)) return fail(what, hr);
        const bool same = !memcmp(p, q, sizeof q);
        if (FAILED(hr = b->Unlock())) return fail("Unlock", hr);
        return same || (snprintf(why, sizeof why, "%s reads other bytes than were written", what), false);
    };
    const Px px[] = {{64, 128, TRED}, {192, 128, TGRN}};
    return frames(BLUE, px, 2, [&] {
        Com<IDirect3DVertexBuffer9> vb;
        return mkDyn(vb, sizeof q, 0) && put(vb.p, 0, q, sizeof q) && reads(vb.p, D3DLOCK_READONLY, "Lock (READONLY)") &&
               rvAt(vb.p) && drawList() && reads(vb.p, D3DLOCK_NOOVERWRITE, "Lock (NOOVERWRITE)") && drawList(6);
    });
}
// buf-dyn-cross: DECL_CROSS under VS_CROSS_Y (DXMTRS C4c-2a's crossing layout, stride 12) from a DYNAMIC|WRITEONLY
// buffer of 164 bytes, filled through DISCARD: 0x5a, then from byte 2 crossTris(z 0.25, B's edge at 0.875), 0.875 and
// a marked float (to byte 82), then 0x5a. Drawn at stream offset 2, which fetches bytes 2-77 (vertex 5's texcoord, the
// 0.875, ends at 78). A NOOVERWRITE append from byte 82, past every byte that draw fetches: crossTris(z 0.75, B's edge
// at 0, pixel 128), 0 and a marked float; drawn at stream offset 82 (its vertex 5's texcoord ends at 158). (228,204)
// 0xff40df: the first draw's B, coloured by the float after its six, which the append, written in place, leaves alone.
// (120,64) 0xffbf80: the second draw's A, over the first's. (140,160) 0xffbf00: the second draw's B, coloured by the 0
// after its six (the marks read there give another blue). (64,192) the clear.
static bool rowBufDynCross() {
    float a[20], b[20];
    crossTris(a, 0.25f, 0.875f), a[18] = 0.875f, a[19] = 0.375f;
    crossTris(b, 0.75f, 0.0f), b[18] = 0.0f, b[19] = 0.375f;
    BYTE fill[2 + sizeof a + sizeof b + 2];
    memset(fill, 0x5a, sizeof fill), memcpy(fill + 2, a, sizeof a);
    Prog p;
    if (!make(p, DECL_CROSS, VS_CROSS_Y, PS_V0)) return false;
    static const Px px[] = {{228, 204, 0xff40df}, {120, 64, 0xffbf80}, {140, 160, 0xffbf00}, {64, 192, BLUE}};
    return frames(BLUE, px, 4, [&] {
        Com<IDirect3DVertexBuffer9> vb;
        if (!mkDyn(vb, sizeof fill, D3DUSAGE_WRITEONLY) || !put(vb.p, 0, fill, sizeof fill, D3DLOCK_DISCARD) || !use(p))
            return false;
        CK(SetStreamSource, 0, vb.p, 2, 12);
        if (!drawList() || !put(vb.p, 2 + sizeof a, b, sizeof b, D3DLOCK_NOOVERWRITE)) return false;
        CK(SetStreamSource, 0, vb.p, 2 + sizeof a, 12);
        return drawList();
    });
}
// A static WRITEONLY default-pool vertex buffer of n bytes holding `data`, filled by one Lock of its whole length (an
// explicit size, not 0): mtld3d uploads it whole at the Unlock and then releases the backing
// (release_backing_after_upload).
static bool mkWriteOnly(Com<IDirect3DVertexBuffer9> &b, const void *data, UINT n) {
    CK(CreateVertexBuffer, n, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &b.p, nullptr);
    return put(b.p, 0, data, n);
}
// Vertex k of `b` rewritten as `v` through Lock(20k, 20, 0): flags 0 and a size, a window mtld3d takes at its word
// (may_trust_lock_bounds) and uploads alone. With `zeros`, the 20 bytes read through the pointer first must be 0.
static bool window(IDirect3DVertexBuffer9 *b, UINT k, const RV &v, bool zeros) {
    BYTE *p = nullptr;
    HRESULT hr = b->Lock(k * sizeof v, sizeof v, (void **)&p, 0);
    if (FAILED(hr)) return fail("Lock (one vertex)", hr);
    int bad = -1, got = 0;
    for (int i = 0; zeros && i < (int)sizeof v && bad < 0; i++)
        if (p[i]) bad = i, got = p[i];
    memcpy(p, &v, sizeof v);
    if (FAILED(hr = b->Unlock())) return fail("Unlock (one vertex)", hr);
    if (bad >= 0)
        return snprintf(why, sizeof why, "vertex %u's window reads byte %d as %02x, not 0: the backing was not released",
                        k, bad, got),
               false;
    return true;
}
// buf-writeonly: a static WRITEONLY default-pool vertex buffer, whose backing mtld3d releases once its first fill is
// uploaded whole, locked again: the lock re-creates a zeroed backing and uploads only the window it names. No READONLY
// or size-0 lock (on a re-created backing either one dirties the whole buffer and uploads its zeros). The left quad
// (listRhw's, red) filled by mkWriteOnly and drawn; vertex 4 moved from (112,192) to (240,192) stretches the second
// triangle over (160,176), which only it covers and which needs vertices 3 and 5 as the device holds them (zeros, as a
// whole upload of the re-created backing gives them, draw nothing there). Cell 1: vertex 4's window reads zeros (red
// under the core until it releases the backing: DXMTRS C4c-2b), then is moved; drawn: (64,128) and (160,176) red,
// (200,96) the clear. Cell 2: vertex 2 moved from (16,64) to (16,8), which stretches the first triangle over (32,48),
// and vertex 4 moved, two windows each unlocked before the draw: (32,48) red, (160,176) red, (114,80) red (inside the
// second triangle only with vertex 3, between the windows, as the device holds it, at (112,64): were it the backing's
// zeros, the triangle would end at x 100 on that line; (160,176) cannot tell, a vertex of rhw 0 weighs nothing in the
// colour and the stretched triangle still covers it), (64,128) red, (200,96) the clear. Cell 3: a second such buffer,
// its first lock of its whole length, the left quad written and drawn while locked, the right quad green written
// through the same pointer over it, Unlock, drawn: left red (the first draw's bytes), right green.
static bool rowBufWriteOnly() {
    RV q[6], r[6];
    listLeft(q, TRED), listRight(r, TGRN);
    RV v2 = q[2], v4 = q[4];
    v2.y = 8, v4.x = 240;
    const Px one[] = {{64, 128, TRED}, {160, 176, TRED}, {200, 96, BLUE}},
             two[] = {{32, 48, TRED}, {160, 176, TRED}, {114, 80, TRED}, {64, 128, TRED}, {200, 96, BLUE}},
             three[] = {{64, 128, TRED}, {192, 128, TGRN}};
    return frames(BLUE, one, 3, [&] {
               Com<IDirect3DVertexBuffer9> vb;
               return mkWriteOnly(vb, q, sizeof q) && rvAt(vb.p) && drawList() && window(vb.p, 4, v4, true) &&
                      drawList();
           }) && frames(BLUE, two, 5, [&] {
               Com<IDirect3DVertexBuffer9> vb;
               return mkWriteOnly(vb, q, sizeof q) && rvAt(vb.p) && drawList() && window(vb.p, 2, v2, false) &&
                      window(vb.p, 4, v4, false) && drawList();
           }) && frames(BLUE, three, 2, [&] {
               Com<IDirect3DVertexBuffer9> vb;
               CK(CreateVertexBuffer, sizeof q, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &vb.p, nullptr);
               void *p = nullptr;
               HRESULT hr = vb.p->Lock(0, sizeof q, &p, 0);
               if (FAILED(hr)) return fail("Lock", hr);
               memcpy(p, q, sizeof q);
               bool ok = rvAt(vb.p) && drawList();
               if (ok) memcpy(p, r, sizeof r);
               if (FAILED(hr = vb.p->Unlock())) return fail("Unlock", hr);
               return ok && drawList();
           });
}

// Whether `name` is one of the comma-separated names in `list`.
static bool listed(const char *list, const char *name) {
    const size_t n = strlen(name);
    for (const char *p = list; p; p = strchr(p, ',')) {
        if (*p == ',') p++;
        if (!strncmp(p, name, n) && (p[n] == ',' || !p[n])) return true;
    }
    return false;
}
// The rows named in `skip` (comma-separated) print "skip" and do not run.
static void buffersRows(IDirect3DDevice9 *d, bool strict, const char *skip) {
    static const struct { const char *name; bool (*run)(); } rows[] = {
        {"buf-d3d", rowBufD3d},           {"buf-lock", rowBufLock},         {"buf-draw", rowBufDraw},
        {"buf-start", rowBufStart},       {"buf-offset", rowBufOffset},     {"buf-strip", rowBufStrip},
        {"buf-fan", rowBufFan},           {"buf-indexed", rowBufIndexed},   {"buf-index32", rowBufIndex32},
        {"buf-base", rowBufBase},         {"buf-ifan", rowBufIfan},         {"buf-managed", rowBufManaged},
        {"buf-dynamic", rowBufDynamic},   {"buf-order", rowBufOrder},       {"buf-discard", rowBufDiscard},
        {"buf-held", rowBufHeld},         {"buf-relock", rowBufRelock},     {"buf-streams", rowBufStreams},
        {"buf-null", rowBufNull},         {"buf-null-up", rowBufNullUp},    {"buf-zero-stride", rowBufZeroStride},
        {"buf-instanced", rowBufInstanced}, {"buf-shader", rowBufShader},   {"buf-tex", rowBufTex},
        {"buf-nobuffer", rowBufNoBuffer}, {"buf-release", rowBufRelease},   {"buf-odd", rowBufOdd},
        {"buf-cross", rowBufCross},       {"buf-cross-up", rowBufCrossUp},  {"buf-cross-multi", rowBufCrossMulti},
        {"buf-cross-odd", rowBufCrossOdd}, {"buf-cross-indexed", rowBufCrossIndexed}, {"buf-wide", rowBufWide},
        {"buf-dyn-preserve", rowBufDynPreserve}, {"buf-dyn-many", rowBufDynMany}, {"buf-dyn-index", rowBufDynIndex},
        {"buf-dyn-read", rowBufDynRead},  {"buf-dyn-cross", rowBufDynCross}, {"buf-writeonly", rowBufWriteOnly}};
    dev = d, strictMode = strict, budgetEnd = GetTickCount64() + 180000;
    HRESULT hr = dev->GetRenderTarget(0, &rtSurf);
    if (SUCCEEDED(hr))
        hr = dev->CreateOffscreenPlainSurface(256, 256, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sysSurf, nullptr);
    for (const auto &r : rows) {
        if (listed(skip, r.name)) {
            printf("d3d9 %s skip\n", r.name);
            continue;
        }
        why[0] = 0;
        bool ok;
        if (FAILED(hr)) ok = fail("GetRenderTarget/CreateOffscreenPlainSurface", hr);
        else if (GetTickCount64() >= budgetEnd) ok = false, snprintf(why, sizeof why, "budget");
        else ok = reset() && bufStart() && r.run();
        row(r.name, ok, why);
    }
    if (sysSurf) sysSurf->Release();
    if (rtSurf) rtSurf->Release();
}

int main(int argc, char **argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const bool bench = argc > 3 && !strcmp(argv[1], "--bench");
    const int benchDraws = bench ? atoi(argv[2]) : 0, benchFrames = bench ? atoi(argv[3]) : 0;
    const bool prog = bench && argc > 4 && !strcmp(argv[4], "prog");
    const bool texBench = bench && argc > 4 && !strcmp(argv[4], "tex");
    const bool vsync = !bench && argc > 1 && !strcmp(argv[1], "vsync");
    const bool shaders = !bench && argc > 1 && !strcmp(argv[1], "shaders");
    const bool state = !bench && argc > 1 && !strcmp(argv[1], "state");
    const bool textures = !bench && argc > 1 && !strcmp(argv[1], "textures");
    const bool fixed = !bench && argc > 1 && !strcmp(argv[1], "fixed");
    const bool targets = !bench && argc > 1 && !strcmp(argv[1], "targets");
    const bool depth = !bench && argc > 1 && !strcmp(argv[1], "depth");
    const bool blendMode = !bench && argc > 1 && !strcmp(argv[1], "blend");
    const bool mrtMode = !bench && argc > 1 && !strcmp(argv[1], "mrt");
    const bool msaaMode = !bench && argc > 1 && !strcmp(argv[1], "msaa");
    const bool buffersMode = !bench && argc > 1 && !strcmp(argv[1], "buffers");
    const bool depthBench = bench && argc > 5 && !strcmp(argv[5], "depth");
    const bool litBench = bench && argc > 4 && !strcmp(argv[4], "lit");
    const bool bufBench = bench && argc > 4 && !strcmp(argv[4], "buf");
    const bool dynBench = bench && argc > 4 && !strcmp(argv[4], "dyn");
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"d3d9_triangle";
    RegisterClassW(&wc);
    RECT r = {0, 0, 256, 256};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowW(L"d3d9_triangle", L"d3d9_triangle", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100,
                              r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    direct3d = d3d;
    row("module", d3d && GetModuleHandleW(L"mtld3d.dll"), d3d ? "no mtld3d.dll in the process" : "Direct3DCreate9");
    // A dllimport function's address is its import slot's content: d3d9.dll is imported here, so mtld3d redirected it.
    row("imports", (void *)&EnumDisplaySettingsW !=
                       (void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "EnumDisplaySettingsW"),
        "EnumDisplaySettingsW still user32's");
    if (!d3d || !hwnd) return printf("d3d9 clear FAIL no device\n"), failures + 3;

    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = 256, pp.BackBufferHeight = 256, pp.BackBufferFormat = D3DFMT_X8R8G8B8, pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD, pp.hDeviceWindow = hwnd, pp.Windowed = TRUE;
    pp.PresentationInterval = vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
    // The "depth", "blend", "mrt" and "msaa" modes' devices alone have a depth-stencil: the other modes' rows assume none.
    if (depth || blendMode || mrtMode || msaaMode || depthBench)
        pp.EnableAutoDepthStencil = TRUE, pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    // The "msaa" mode's back buffer and auto depth-stencil have 4 samples (DXMTRS C4b-3d5).
    if (msaaMode) pp.MultiSampleType = D3DMULTISAMPLE_4_SAMPLES;
    IDirect3DDevice9 *dev = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp,
                                   &dev);
    if (FAILED(hr)) return printf("d3d9 clear FAIL CreateDevice %08lx\n", (unsigned long)hr), failures + 3;

    struct Vertex { float x, y, z, rhw; DWORD color; };
    const Vertex tri[3] = {{128, 16, 0.5f, 1, D3DCOLOR_XRGB(255, 0, 0)},
                           {240, 240, 0.5f, 1, D3DCOLOR_XRGB(255, 0, 0)},
                           {16, 240, 0.5f, 1, D3DCOLOR_XRGB(255, 0, 0)}};
    if (buffersMode) {
        bool strict = false;
        const char *skip = "";
        for (int i = 2; i < argc; i++)
            if (!strcmp(argv[i], "strict")) strict = true;
            else if (!strncmp(argv[i], "skip=", 5)) skip = argv[i] + 5;
        buffersRows(dev, strict, skip);
        dev->Release();
        d3d->Release();
        DestroyWindow(hwnd);
        return failures;
    }
    if (msaaMode) {
        msaaRows(dev, argc > 2 && !strcmp(argv[2], "strict"));
        dev->Release();
        d3d->Release();
        DestroyWindow(hwnd);
        return failures;
    }
    if (mrtMode) {
        mrtRows(dev, argc > 2 && !strcmp(argv[2], "strict"));
        dev->Release();
        d3d->Release();
        DestroyWindow(hwnd);
        return failures;
    }
    if (blendMode) {
        blendRows(dev, argc > 2 && !strcmp(argv[2], "strict"));
        dev->Release();
        d3d->Release();
        DestroyWindow(hwnd);
        return failures;
    }
    if (depth) {
        depthRows(dev, argc > 2 && !strcmp(argv[2], "strict"));
        dev->Release();
        d3d->Release();
        DestroyWindow(hwnd);
        return failures;
    }
    if (targets) {
        targetRows(dev, argc > 2 && !strcmp(argv[2], "strict"));
        dev->Release();
        d3d->Release();
        DestroyWindow(hwnd);
        return failures;
    }
    if (fixed) {
        fixedRows(dev, argc > 2 && !strcmp(argv[2], "strict"));
        dev->Release();
        d3d->Release();
        DestroyWindow(hwnd);
        return failures;
    }
    if (textures) {
        textureRows(dev, argc > 2 && !strcmp(argv[2], "strict"));
        dev->Release();
        d3d->Release();
        DestroyWindow(hwnd);
        return failures;
    }
    if (state) {
        stateRows(dev, d3d, hwnd, argc > 2 && !strcmp(argv[2], "strict"));
        dev->Release();
        d3d->Release();
        DestroyWindow(hwnd);
        return failures;
    }
    if (shaders) {
        shaderRows(dev, argc > 2 && !strcmp(argv[2], "strict"));
        dev->Release();
        d3d->Release();
        DestroyWindow(hwnd);
        return failures;
    }
    if (bench) {
        LARGE_INTEGER f, t0, t1;
        QueryPerformanceFrequency(&f);
        // prog: the triangle in clip space through VS_BENCH (m4x4 oPos, v0, c0) and PS_V0, c0-c3 set per draw.
        Prog p;
        const V3 ctri[3] = {{0, 0.875f, 0.5f}, {0.875f, -0.875f, 0.5f}, {-0.875f, -0.875f, 0.5f}};
        ::dev = dev;
        if (prog && !(make(p, DECL_XYZ, VS_BENCH, PS_V0) && use(p)))
            return printf("d3d9 bench FAIL %s\n", why), failures + 1;
        // tex (DXMTRS C4b-3b): the triangle through a ps_2_0 sampling one 64x64 texture.
        Com<IDirect3DTexture9> benchTex;
        Com<IDirect3DPixelShader9> benchPs;
        TV3 ttri[3];
        if (texBench) {
            if (!make2d(benchTex, 64, 64, 1, D3DFMT_X8R8G8B8, 4, [](int x, int y) { return (DWORD)(x * 4 << 8 | y * 4); }) ||
                !usePs(benchPs, K2D, 0, false))
                return printf("d3d9 bench FAIL %s\n", why), failures + 1;
            dev->SetTexture(0, benchTex.p);
            for (int k = 0; k < 3; k++)
                ttri[k] = {tri[k].x, tri[k].y, tri[k].z, tri[k].rhw, tri[k].color, tri[k].x / 256, tri[k].y / 256, 0};
        }
        dev->SetFVF(texBench ? FVF_T2 : D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        int failedDraws = 0;
        // One frame of draw() over the blue clear, read back: its centre, or 0 when it cannot be read.
        const auto centreOf = [&](auto draw) {
            IDirect3DSurface9 *brt = nullptr, *bsys = nullptr;
            D3DLOCKED_RECT lr = {};
            DWORD centre = 0;
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 255), 1.0f, 0);
            dev->BeginScene();
            draw();
            dev->EndScene();
            if (SUCCEEDED(dev->GetRenderTarget(0, &brt)) &&
                SUCCEEDED(dev->CreateOffscreenPlainSurface(256, 256, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &bsys, nullptr)) &&
                SUCCEEDED(dev->GetRenderTargetData(brt, bsys)) && SUCCEEDED(bsys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
                centre = ((const DWORD *)((const BYTE *)lr.pBits + 128 * lr.Pitch))[128] & 0xffffff;
                bsys->UnlockRect();
            }
            dev->Present(nullptr, nullptr, nullptr, nullptr);
            if (bsys) bsys->Release();
            if (brt) brt->Release();
            return centre;
        };
        // lit (DXMTRS C4b-3c): the triangle untransformed through the fixed-function vertex stage, one directional
        // light on a 0.8 red material; a frame read back before the timed loop must show the lit colour at the
        // centre (counted with the failed draws), so the line's numbers are a lit draw's.
        FN ltri[3] = {{0, 0.875f, 0.5f, 0, 0, -1}, {0.875f, -0.875f, 0.5f, 0, 0, -1}, {-0.875f, -0.875f, 0.5f, 0, 0, -1}};
        if (litBench) {
            if (!(rstate(D3DRS_LIGHTING, TRUE) && lightOn(0, ldir(1, 1, 1)) && material(0.8f, 0, 0)))
                return printf("d3d9 bench FAIL %s\n", why), failures + 1;
            dev->SetFVF(FVF_N);
            const DWORD centre = centreOf([&] {
                failedDraws += FAILED(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, ltri, sizeof *ltri));
            });
            // mtld3d may leave the first draw out while its pipeline builds: only a wrong colour counts, not blue.
            if (centre != 0xcc0000 && centre != 0x0000ff) failedDraws++, printf("d3d9 bench lit centre %06lx\n", (unsigned long)centre);
        }
        // buf and dyn (DXMTRS C4c-2a): the triangle (offset as ff's) from a vertex buffer, `cursor` dyn's next
        // triangle. bufDraw(d) is draw d's failures: buf's DrawPrimitive; dyn's Lock (NOOVERWRITE, DISCARD at
        // triangle 0), Unlock and DrawPrimitive.
        Com<IDirect3DVertexBuffer9> benchVb;
        UINT cursor = 0;
        const auto triAt = [&](int d, Vertex *v) {
            for (int k = 0; k < 3; k++) v[k] = tri[k], v[k].x += (float)(d & 7) * 0.125f;
        };
        const auto bufDraw = [&](int d) {
            if (bufBench) return (int)FAILED(dev->DrawPrimitive(D3DPT_TRIANGLELIST, 3 * d, 1));
            const UINT at = cursor;
            cursor = (cursor + 1) % 256;
            void *p = nullptr;
            if (FAILED(benchVb.p->Lock(at * sizeof tri, sizeof tri, &p, at ? D3DLOCK_NOOVERWRITE : D3DLOCK_DISCARD)))
                return 1;
            triAt(d, (Vertex *)p);
            return (int)FAILED(benchVb.p->Unlock()) + (int)FAILED(dev->DrawPrimitive(D3DPT_TRIANGLELIST, 3 * at, 1));
        };
        if (bufBench || dynBench) {
            const UINT n = dynBench ? 256 : benchDraws > 0 ? benchDraws : 1;
            Vertex *v = nullptr;
            hr = dev->CreateVertexBuffer(n * sizeof tri, D3DUSAGE_WRITEONLY | (dynBench ? D3DUSAGE_DYNAMIC : 0), FVF_RV,
                                         D3DPOOL_DEFAULT, &benchVb.p, nullptr);
            if (SUCCEEDED(hr) && bufBench && SUCCEEDED(hr = benchVb.p->Lock(0, 0, (void **)&v, 0))) {
                for (UINT d = 0; d < n; d++) triAt(d, v + 3 * d);
                hr = benchVb.p->Unlock();
            }
            if (SUCCEEDED(hr)) hr = dev->SetStreamSource(0, benchVb.p, 0, sizeof(Vertex));
            if (FAILED(hr)) return printf("d3d9 bench FAIL vertex buffer %08lx\n", (unsigned long)hr), failures + 1;
            // A dropped buffer draw answers D3D_OK: frames are read back until one's centre is red, for up to 10 s
            // (mtld3d leaves a draw out while its pipeline builds), else one more failure.
            DWORD centre = 0;
            for (const ULONGLONG end = GetTickCount64() + 10000; centre != 0xff0000 && GetTickCount64() < end;)
                centre = centreOf([&] { failedDraws += bufDraw(0); });
            if (centre != 0xff0000)
                failedDraws++, printf("d3d9 bench %s centre %06lx\n", bufBench ? "buf" : "dyn", (unsigned long)centre);
        }
        QueryPerformanceCounter(&t0);
        for (int i = 0; i < benchFrames; i++) {
            dev->Clear(0, nullptr, depthBench ? D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER : D3DCLEAR_TARGET,
                       D3DCOLOR_XRGB(0, 0, 255), 1.0f, 0);
            dev->BeginScene();
            for (int d = 0; d < benchDraws; d++) {
                if (bufBench || dynBench) {
                    failedDraws += bufDraw(d);
                    continue;
                }
                if (prog) {
                    const float m[4][4] = {{1, 0, 0, (float)(d & 7) * 0.125f / 128}, {0, 1, 0, 0}, {0, 0, 1, 0},
                                           {0, 0, 0, 1}};
                    dev->SetVertexShaderConstantF(0, &m[0][0], 4);
                    failedDraws += FAILED(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, ctri, sizeof *ctri));
                    continue;
                }
                if (litBench) {
                    FN v[3] = {ltri[0], ltri[1], ltri[2]};
                    for (FN &q : v) q.x += (float)(d & 7) * 0.125f / 128;
                    failedDraws += FAILED(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, v, sizeof *v));
                    continue;
                }
                if (texBench) {
                    TV3 v[3] = {ttri[0], ttri[1], ttri[2]};
                    for (TV3 &q : v) q.x += (float)(d & 7) * 0.125f;
                    failedDraws += FAILED(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, v, sizeof *v));
                    continue;
                }
                Vertex v[3] = {tri[0], tri[1], tri[2]};
                for (Vertex &p : v) p.x += (float)(d & 7) * 0.125f;
                failedDraws += FAILED(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, v, sizeof *v));
            }
            dev->EndScene();
            dev->Present(nullptr, nullptr, nullptr, nullptr);
        }
        QueryPerformanceCounter(&t1);
        FILETIME creation, exited, kernel, user;
        GetProcessTimes(GetCurrentProcess(), &creation, &exited, &kernel, &user);
        auto ms = [](FILETIME t) { return (double)(((ULONGLONG)t.dwHighDateTime << 32) | t.dwLowDateTime) / 1e4; };
        MEMORYSTATUSEX mem = {};
        mem.dwLength = sizeof mem;
        GlobalMemoryStatusEx(&mem);
        printf("d3d9 bench: mode %s frames %d draws %d wall_ms %.1f cpu_ms %.1f vm_used_mb %llu failed %d\n",
               bufBench ? "buf" : dynBench ? "dyn" : prog ? "prog" : texBench ? (depthBench ? "texdepth" : "tex")
               : litBench ? "lit" : "ff", benchFrames, benchDraws,
               (double)(t1.QuadPart - t0.QuadPart) * 1e3 / (double)f.QuadPart, ms(user) + ms(kernel),
               (unsigned long long)((mem.ullTotalVirtual - mem.ullAvailVirtual) >> 20), failedDraws);
        dev->Release();
        d3d->Release();
        DestroyWindow(hwnd);
        return failures;
    }
    IDirect3DSurface9 *rt = nullptr, *sys = nullptr;
    bool read = SUCCEEDED(dev->GetRenderTarget(0, &rt)) &&
                SUCCEEDED(dev->CreateOffscreenPlainSurface(256, 256, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr));
    DWORD corner = 0, centre = 0;
    int frames = 0, presented = 0, after = 0;
    const ULONGLONG until = GetTickCount64() + 10000;
    while (after < 10 && GetTickCount64() < until) {
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 255), 1.0f, 0);
        dev->BeginScene();
        dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, tri, sizeof *tri);
        dev->EndScene();
        D3DLOCKED_RECT lr = {};
        read = read && SUCCEEDED(dev->GetRenderTargetData(rt, sys)) &&
               SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY));
        if (!read) break;
        const BYTE *p = (const BYTE *)lr.pBits;
        if (!frames++) corner = *(const DWORD *)(p + 2 * lr.Pitch + 2 * 4) & 0xffffff;
        if (centre != 0xff0000) centre = *(const DWORD *)(p + 128 * lr.Pitch + 128 * 4) & 0xffffff;
        sys->UnlockRect();
        presented += dev->Present(nullptr, nullptr, nullptr, nullptr) == D3D_OK;
        after += centre == 0xff0000;
    }
    char why[64];
    snprintf(why, sizeof why, read ? "corner %06lx" : "no readback", (unsigned long)corner);
    row("clear", read && corner == 0x0000ff, why);
    snprintf(why, sizeof why, read ? "centre %06lx after %d frames" : "no readback", (unsigned long)centre, frames);
    row("triangle", read && centre == 0xff0000, why);
    snprintf(why, sizeof why, "%d of %d", presented, frames);
    row("present", frames >= 10 && presented == frames, why);
    printf("frames: %d\n", frames);
    if (vsync) {
        LARGE_INTEGER f, t0, t1;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&t0);
        int ok = 0;
        for (int i = 0; i < 60; i++) {
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 255), 1.0f, 0);
            ok += dev->Present(nullptr, nullptr, nullptr, nullptr) == D3D_OK;
        }
        QueryPerformanceCounter(&t1);
        double s = (double)(t1.QuadPart - t0.QuadPart) / (double)f.QuadPart;
        printf("vsync: 60 presents in %.2f s\n", s);
        snprintf(why, sizeof why, "%d of 60 presents in %.1f s", ok, s);
        row("vsync", ok == 60 && s < 5.0, why);
    }

    if (sys) sys->Release();
    if (rt) rt->Release();
    dev->Release();
    d3d->Release();
    DestroyWindow(hwnd);
    return failures;
}
