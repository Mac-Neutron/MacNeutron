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
// With "state" ("state strict" under core; DXMTRS C4b-3a): 16 rows "d3d9 state-<row> ok|FAIL <why>" of the API state
// and inert objects (transforms, material and lights, stage, sampler and scissor states, stream frequency, swap chains,
// the back-buffer target switch, textures, a second device released before its objects, a buffer lock, state blocks,
// the UP draws' unbinds, a textured fixed-function draw and one whose stage reads no texture; since DXMTRS C4c-6a the
// getters (creation parameters, display mode, raster status, texture memory), the stubs and obsolete calls with
// EvictManagedResources' re-publish, Present's other forms and an additional swap chain's Present, each judged by an
// occlusion query a present completes, and an extended device's calls and PresentEx), with a 60 s budget.
// With "fixed" ("fixed strict" under core; DXMTRS C4b-3c): 31 rows "d3d9 ff-<row>|tex-vtx<row> ok|FAIL <why>" of the
// fixed-function vertex stage over untransformed layouts (transforms, lights, material sources, fog, texgen and texture
// transforms, vertex blending, points, a clip plane) and three vertex-sampled target textures the core refuses, with a
// 180 s budget. "--bench <draws> <frames> lit" draws the triangle untransformed and lit ("mode lit").
// With "targets" ("targets strict" under core; DXMTRS C4b-3d1): 35 rows "d3d9 <rt|dc|lb>-<row> ok|FAIL <why>" of render
// targets other than the back buffer (render-target textures, their levels and cube faces, CreateRenderTarget surfaces:
// drawn to, cleared, read back with GetRenderTargetData and sampled; formats, sRGB write, the viewport reset, automatic
// mipmaps, lifetime and order, a size past the device's limit, a target bound and unbound at index 1; since DXMTRS
// C4b-3d5 a multisampled surface resolved by StretchRect into a texture level, a surface and the back buffer, and the
// default pool's plain surface; since DXMTRS C4c-3a a lockable target's locks, the surfaces that do not lock, the back
// buffer's read-only lock and the front buffer's read; since DXMTRS C4c-3b device contexts (GetDC, GDI, ReleaseDC) of
// CPU stores, a texture level, a render-target texture's level, a lockable target and the back buffer, an additional
// swap chain's back buffer, and, on a second device made with D3DPRESENTFLAG_LOCKABLE_BACKBUFFER, its back buffer's
// writable lock and device context, its own and an additional swap chain's), with a 180 s budget. The core refuses
// none of it (rt-msaa's StretchRect of a multisampled surface into a 32x32 target is its since DXMTRS C4c-4c). The
// program links gdi32.
// With "depth" ("depth strict" under core; DXMTRS C4b-3d2): 31 rows "d3d9 ds-<row> ok|FAIL <why>" of the depth-stencil,
// on a device made with an auto D24S8 (the other modes' devices have none): the depth and stencil tests and their
// state, Clear's Z and stencil, depth bias, the pretransformed clamp, a pixel shader's depth output, depth-stencil
// surfaces and depth textures and their levels, shadow, raw (INTZ, DF16, DF24) and fetch4 samples of a depth texture,
// a depth-stencil larger than the target, the pretransformed clip while one is bound, a multisampled surface beside a
// single-sample target (DXMTRS C4b-3d5), dynamic depth textures written through their locks and sampled (DXMTRS
// C4c-3b), and what the core refuses (a depth-tested draw sampling the bound depth-stencil), with a 150 s budget.
// "--bench <draws> <frames> tex depth" runs the tex bench on such a device, Z cleared each frame and tested ("mode
// texdepth").
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
// With "msaa" ("msaa strict" under core; DXMTRS C4b-3d5): 21 rows "d3d9 ms-<row> ok|FAIL <why>" on a device whose
// back buffer and auto D24S8 have 4 samples: multisampled CreateRenderTarget surfaces (2, 4, 8 samples and
// non-maskable) read through StretchRect's resolve, the back buffer's too; edges, Clear with rects and the viewport,
// the depth test, a depth-stencil of another sample count, D3DRS_MULTISAMPLEMASK, alpha-to-coverage (ATOC and the A2M
// latch), sRGB write, blending, index 1-3 targets, the scissor test, a resolve's destination drawn again; what the
// creators and read-backs answer; since DXMTRS C4c-3a the back buffer read by a READONLY lock and by
// GetFrontBufferData; since DXMTRS C4c-4b StretchRect of the auto depth-stencil into a depth-stencil surface of 4
// samples, its depth and stencil judged by draws (ms-dcopy); since DXMTRS C4c-4d its resolve into a single-sample one,
// judged the same way (ms-dres). With a 180 s budget. The core refuses none of it (ms-desc's and ms-dcopy's
// depth-stencil copies are the core's since DXMTRS C4c-4b, ms-dres's resolve since DXMTRS C4c-4d).
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
// With "blit" ("blit strict" under core; DXMTRS C4c-4a to C4c-4d): 41 rows "d3d9 <cf|us|sr|sc|sq|sd>-<row> ok|FAIL
// <why>", on a device with no depth-stencil: ColorFill of render targets lockable or not, the back buffer and an
// additional swap chain's, a target that is not bound, a render-target texture's levels and cube face, an AUTOGENMIPMAP
// one, a multisampled one and default-pool plain surfaces, and the surfaces it refuses; UpdateSurface into a texture's
// level, levels and cube face, a render-target texture's level, render targets and the back buffer, from R5G6B5, and
// the calls it refuses; StretchRect's D3DERR_INVALIDCALL in every lane; since DXMTRS C4c-4b StretchRect's unscaled
// copies (sc-*), their texels judged: between targets, the back buffer and an additional swap chain's, from and into a
// lockable target, render-target textures' levels and cube faces, from a default-pool texture's levels and plain
// surfaces, between plain surfaces (raw, and converted into R5G6B5 and from YUY2), within one surface and one texture,
// into AUTOGENMIPMAP textures, among draws in one scene, and of depth-stencil surfaces; since DXMTRS C4c-4c its sampled
// copies (sq-*), their texels judged: scaled (NONE, POINT, LINEAR, from and into sub-rects, levels and cube faces),
// converting (from the back buffer and X8R8G8B8, into and out of R5G6B5 and A16B16G16R16F targets, alpha judged),
// through a format's swizzle (A4R4G4B4, L8), from YUY2, UYVY, YV12 and NV12 plain surfaces, into and out of
// multisampled targets, between overlapping rects of one surface, of an additional swap chain's back buffer and the
// device's, and two levels of one texture, a plain surface onto itself, among draws in one scene (a wireframe one
// included) and into an AUTOGENMIPMAP texture; since DXMTRS C4c-4d its depth-stencil resolve (sd-resolve): a
// multisampled depth-stencil surface into a single-sample one, D24S8 and D16, its depth and stencil judged by draws.
// With a 180 s budget. The core refuses none of it (sd-resolve's resolve is its since DXMTRS C4c-4d). A format
// CheckDeviceFormat (or, for a YUV source, CheckDeviceFormatConversion) does not offer prints "d3d9 <row>-<format>
// skip" and its cells do not run.
// With "queries" ("queries strict" under core; DXMTRS C4c-5): 12 rows "d3d9 qy-<row> ok|FAIL <why>" of
// IDirect3DQuery9, on a device with no depth-stencil: CreateQuery's support probe and the types it makes (EVENT and
// OCCLUSION) or refuses, the query's type, size, device and reference; EVENT's answer before and after its END, a
// poll without the flag and without a Present that must finish; OCCLUSION's exact counts of pretransformed quads
// (GetData's sizes 2, 4, 8 and 0, a quad behind an occluder, the proxy draw with colour and Z writes off), spans with
// no draw (never issued: 0xdd bytes; END first, BEGIN and END, BEGIN|END, 400 pairs: 0), an open span's S_FALSE, BEGIN
// twice and END twice, a span across Present (its END alone after it), two overlapping queries, draws among a Clear,
// a StretchRect and a ColorFill of their target (which count nothing) and on a second target, a draw after END between
// passes of one target, a query released while open, one query used again, and a poll without the flag across
// Presents. With a 120 s budget. A runtime that refuses CreateQuery (D3DERR_NOTAVAILABLE: the core before DXMTRS
// C4c-5's front end) makes each row print "FAIL CreateQuery 8876086a" (qy-support's "FAIL CreateQuery (EVENT, NULL)
// 8876086a").
// With "reset" ("reset strict" under core; DXMTRS C4c-6c): 14 rows "d3d9 rs-<row> ok|FAIL <why>" of Reset and ResetEx
// of a windowed device, TestCooperativeLevel and CheckDeviceState, each row on a device and a visible window of its own
// (never maximized): Resets to another size (past the creation size, and a zero size taking the window's client area),
// with the auto depth-stencil kept, dropped and brought back in another format, from one sample to four and back;
// what blocks a plain device's Reset (default-pool textures, render targets and vertex buffers, a held back buffer;
// not a managed texture, a query or the device's own bindings) and the reset it then owes (TestCooperativeLevel and
// Present D3DERR_DEVICENOTRESET); the state a Reset returns to its defaults and the implicit targets it binds again
// (an AUTOGENMIPMAP target and a target at index 1 among them); a Reset inside a scene, refused or not; invalid present
// parameters (refused with nothing changed, or refused owing a reset); another device window; an extended device's
// ResetEx, which keeps its state, and its back buffer and auto depth-stencil held across one; queries issued before a
// Reset and read after it; Resets in a row. A draw after a Reset is judged by reading the back buffer back, a present
// by an occlusion query a present completes (presentCounts). Under "strict" alone, rs-refuse asks for a Reset to an
// R5G6B5 back buffer, which the core refuses (D3DERR_NOTAVAILABLE); elsewhere it does nothing and passes: mtld3d makes
// that Reset with a format it draws. With a 200 s budget. The core before DXMTRS C4c-6c's front end refuses Reset,
// ResetEx, TestCooperativeLevel and CheckDeviceState: each row prints "FAIL ... 8876086a" at its first such call.
// With "extras" ("extras strict" under core; DXMTRS C4c-7a): 15 rows "d3d9 <gm|fl|ex|rz|cu>-<row> ok|FAIL <why>", each
// on a device and a visible window of its own (reset's): the gamma ramp's Set and Get (stored and reported, a ramp that
// does not rise, null ramps and swap chain 1 ignored); an extended device's frame latency (Get, Set 0, 30 and 31, Get
// NULL; frames drawn, read back and presented at a latency of 1); CreateRenderTargetEx, CreateDepthStencilSurfaceEx and
// CreateOffscreenPlainSurfaceEx (drawn to, bound, locked in three pools; the usages they refuse, with the out slot as
// passed, D3DFMT_UNKNOWN's NULL, a restricted-content usage in GetDesc); RESZ (SetRenderState(D3DRS_POINTSIZE,
// 0x7fa05000)) of four flat depth bands into an INTZ texture, from a 4-sample surface, a 2-sample auto depth-stencil
// and a 1-sample surface, into a texture made without D3DUSAGE_DEPTHSTENCIL, and the cases it skips (no texture at
// stage 0, no depth-stencil bound, another size, a colour texture), read by the fixed-function raw sample; the cursor
// calls (SetCursorProperties' answers, ShowCursor's, the cursor GetCursor reports across a WM_SETCURSOR). A sample
// count the adapter does not offer is a FAIL. Under "strict" alone, rz-self makes the bound depth-stencil's own texture
// RESZ's destination and rz-refuse asks for a RESZ into a dynamic depth texture, which the core refuses
// (D3DERR_NOTAVAILABLE); elsewhere both do nothing and pass. No row moves the pointer. With a 200 s budget. The core
// before DXMTRS C4c-7a's front end refuses each call: every row but rz-refuse prints its "FAIL ..." at its first such
// call; and before DXMTRS C4c-6d's subclass cu-setcursor's last cell gets the class cursor back.
// With "fullscreen" ("fullscreen strict" under core; DXMTRS C4c-6d): 9 rows "d3d9 <fs|ws>-<row> ok|FAIL <why>", each
// on a plain device and a window of its own (reset's): fullscreen devices (created, refused, Reset into and out of
// fullscreen, onto another window and to another mode), the window that covers the monitor, the window subclass (its
// window procedure, its answer to WM_ACTIVATEAPP and to an external resize of a fullscreen window), and a windowed
// device's window resized (ws-size). The program runs on a person's working Mac: every fs-* row first asks whether Wine
// emulates mode sets for it (EmulateModeset) and FAILs before any device when it does not; fs-cover alone makes a
// fullscreen device without D3DCREATE_NOWINDOWCHANGES (its window covers the primary monitor for well under a second in
// a strict lane); a row that sets a display mode puts the registry's back itself; no row changes the foreground window,
// minimises a window, moves the pointer or sleeps a fixed time. Each cell wants what mtld3d answers, in every lane, but
// one: under "strict" ws-size wants the back buffer and the viewport kept where mtld3d's follow the window (the core
// does not follow a windowed device's window: DXMTRS C4c-6d's R6d-3). With a 200 s budget. The core before DXMTRS
// C4c-6d's front end refuses a fullscreen device: every fs-* row prints its "FAIL ..." at its first fullscreen create
// or Reset (fs-wndproc at its windowed half: no subclass).
// With "parity" ("parity strict" under core; DXMTRS C4c-7d-1): 9 rows "d3d9 <sr|vo|dr|pv|rz|gm>-<row> ok|FAIL <why>"
// of the last differences Wine's tests see between D3D9's core runtime and mtld3d: D3DSAMP_SRGBTEXTURE's low bit
// (sr-lsb), a volume texture of depth 1 sampled raw and decoded (vo-one), a managed texture's uploaded levels under
// READONLY and NO_DIRTY_UPDATE locks, AddDirtyRect and EvictManagedResources (dr-keep), ProcessVertices from and into
// system-memory, default-pool WRITEONLY and dynamic buffers, read back and drawn (pv-sysmem, pv-draw, pv-dynsrc,
// pv-dyndst), a one-sample RESZ from D24X8 into DF24 and into INTZ (rz-cross) and the gamma ramp on a fullscreen
// device, across a Reset to windowed and back (gm-full). sr-, vo-, dr- and pv-* run on the launch's device; rz-cross
// and gm-full on a device and a window of their own (reset's). Each cell wants what mtld3d answers, in every lane.
// gm-full keeps the fullscreen mode's rules (modesEmulated() first and a FAIL before any device without it,
// D3DCREATE_NOWINDOWCHANGES, the desktop's mode; no foreground change, minimised window, pointer move or fixed sleep);
// no cell sees the ramp on screen. With a 200 s budget. The core before DXMTRS C4c-7d-1's front end refuses
// ProcessVertices (D3DERR_NOTAVAILABLE): each pv-* row prints its "FAIL ProcessVertices ... 8876086a".
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
static IDirect3D9 *direct3d;  // main's, for the msaa mode's offers() and the targets mode's lockable device
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
// refuses) take either answer; d3d9-check proves which runtime gave it from the launch's refusal lines. DXMTRS
// C4c-6a's four (state-getters, state-stubs, state-present, state-ex; defined after the query helpers their present
// cells use) want mtld3d's answers in every lane.
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

static bool rowStateGetters();
static bool rowStateStubs();
static bool rowStatePresent();
static bool rowStateEx();

static void stateRows(IDirect3DDevice9 *d, IDirect3D9 *d3d, HWND hwnd, bool strict) {
    static const struct { const char *name; bool (*run)(); } rows[] = {
        {"state-xform", rowXform}, {"state-material", rowMaterial}, {"state-tss", rowTss},
        {"state-swapchain", rowSwapchain}, {"state-rt", rowRt}, {"state-tex", rowTex}, {"state-late", rowLate},
        {"state-lock", rowLock}, {"state-sb", rowSb}, {"state-upunbind", rowUpUnbind}, {"state-fftex", rowFftex},
        {"state-fftexoff", rowFftexOff}, {"state-getters", rowStateGetters}, {"state-stubs", rowStateStubs},
        {"state-present", rowStatePresent}, {"state-ex", rowStateEx}};
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
// Texels [x0, x1) x [y0, y1) of `bpp`-byte texels in rows `pitch` apart set to c (its low `bpp` bytes).
static void paint(BYTE *bits, int pitch, int x0, int y0, int x1, int y1, DWORD c, int bpp = 4) {
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) memcpy(bits + y * pitch + x * bpp, &c, bpp);
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

// Texels of 4-byte texels `b` (rows `pitch` apart), each within 1 a channel of `mask` (the colour channels unless
// said; WITH_ALPHA judges alpha too, DXMTRS C4c-4c); `why` names the first that is not.
struct Tx { int x, y; DWORD want, mask = 0xffffff; };
static const DWORD WITH_ALPHA = 0xffffffff;
static bool txHas(const char *name, const BYTE *b, int pitch, std::initializer_list<Tx> px) {
    for (const Tx &t : px) {
        char what[64];
        snprintf(what, sizeof what, "%s (%d,%d)", name, t.x, t.y);
        if (!want(what, texel(b, pitch, t.x, t.y), t.want, t.mask)) return false;
    }
    return true;
}
// Texels of the size x size 4-byte target `s` (format f), each within 1 a channel.
static bool rtHas(const char *name, IDirect3DSurface9 *s, UINT size, D3DFORMAT f, std::initializer_list<Tx> px) {
    return readTarget(s, size, size, f, [&](const BYTE *b, int pitch) { return txHas(name, b, pitch, px); });
}
// `why` prefixed with the failing cell's name `what` and `more`; false.
static bool failIn(const char *what, const char *more = "") {
    char w[sizeof why];
    snprintf(w, sizeof w, "%s%s: %s", what, more, why);
    memcpy(why, w, sizeof why);
    return false;
}
static bool rtri(const RV *v) {
    CK(SetFVF, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    CK(DrawPrimitiveUP, D3DPT_TRIANGLELIST, 1, v, sizeof *v);
    return true;
}

// Device contexts, the writable lock of a lockable back buffer and an additional swap chain's back buffer (DXMTRS
// C4c-3b), in the colours above. GDI's COLORREF is 0x00BBGGRR where the rows' colours are 0x00RRGGBB, and GDI writes
// alpha 0, so a cell after GDI judges the colour channels alone (want's default mask). A DC is released, as a lock is
// unlocked, before what it read is judged.
static COLORREF gdi(DWORD c) { return RGB(c >> 16 & 255, c >> 8 & 255, c & 255); }
// GetDC of `s`, f(dc) under it (it reads into the caller's locals and draws: false, with `why` set, when GDI fails),
// then ReleaseDC whatever f did: the back buffer persists across until()'s attempts, so no path may leave a DC held.
template <class F> static bool underDc(IDirect3DSurface9 *s, const char *what, F f) {
    HDC dc = nullptr;
    HRESULT hr = s->GetDC(&dc);
    if (FAILED(hr)) return fail(what, hr);
    const bool ok = f(dc);
    hr = s->ReleaseDC(dc);
    if (FAILED(hr)) return fail("ReleaseDC", hr);
    return ok;
}
// [x0, x1) x [y0, y1) of `dc` filled with c by FillRect.
static bool gdiFill(HDC dc, int x0, int y0, int x1, int y1, DWORD c) {
    const HBRUSH b = CreateSolidBrush(gdi(c));
    const RECT r = {x0, y0, x1, y1};
    const bool ok = b && FillRect(dc, &r, b);
    if (b) DeleteObject(b);
    return ok || (snprintf(why, sizeof why, "FillRect"), false);
}
// GetDC of `s` is D3DERR_INVALIDCALL and leaves the caller's handle as it was.
static bool noDc(IDirect3DSurface9 *s, const char *what) {
    const HDC mark = reinterpret_cast<HDC>(static_cast<INT_PTR>(0x5a5a));
    HDC dc = mark;
    const HRESULT hr = s->GetDC(&dc);
    if (SUCCEEDED(hr)) s->ReleaseDC(dc);
    if (hr != D3DERR_INVALIDCALL) return fail(what, hr);
    return dc == mark || (snprintf(why, sizeof why, "%s wrote the handle (%p)", what, (void *)dc), false);
}
// R5G6B5 colours exact in 5-6-5: each channel its field with its high bits repeated below it (GDI's expansion), so
// what GDI reads and writes and a lock's WORD (packed565) are one colour.
static DWORD k565(DWORD r, DWORD g, DWORD b) {
    return (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
}
static WORD packed565(DWORD c) { return (WORD)((c >> 19 & 31) << 11 | (c >> 10 & 63) << 5 | (c >> 3 & 31)); }
static const DWORD K565A = k565(0x18, 0x10, 0x04), K565B = k565(0x06, 0x30, 0x1c);

// One dc-mem surface, `bpp`-byte texels (4: X8R8G8B8, 2: R5G6B5): a written over (8..39, 8..39) through LockRect, then
// PreLoad; under GetDC, GetPixel reads a at (16,16), b fills (24..55, 24..55), and a second GetDC and a LockRect are
// D3DERR_INVALIDCALL; after ReleaseDC a READONLY lock reads b at (32,32), where GDI drew, and a at (16,16) (an R5G6B5
// texel as its WORD, exactly).
static bool dcMem(IDirect3DSurface9 *s, int bpp, DWORD a, DWORD b) {
    const auto packed = [&](DWORD c) { return bpp == 2 ? (DWORD)packed565(c) : c; };
    if (!underLock(s, "LockRect", nullptr, 0,
                   [&](BYTE *p, int pitch) { paint(p, pitch, 8, 8, 40, 40, packed(a), bpp); }))
        return false;
    s->PreLoad();
    COLORREF in = CLR_INVALID;
    HRESULT second = D3D_OK, locked = D3D_OK;
    if (!underDc(s, "GetDC", [&](HDC dc) {
            in = GetPixel(dc, 16, 16);
            HDC dc2 = nullptr;
            if (SUCCEEDED(second = s->GetDC(&dc2))) s->ReleaseDC(dc2);
            D3DLOCKED_RECT lr = {};
            if (SUCCEEDED(locked = s->LockRect(&lr, nullptr, 0))) s->UnlockRect();
            return gdiFill(dc, 24, 24, 56, 56, b);
        }))
        return false;
    if (second != D3DERR_INVALIDCALL) return fail("GetDC (a DC held)", second);
    if (locked != D3DERR_INVALIDCALL) return fail("LockRect (a DC held)", locked);
    if (!want("GetPixel (16,16)", in, gdi(a))) return false;
    DWORD in2 = 0, gdi2 = 0;
    if (!underLock(s, "LockRect (READONLY)", nullptr, D3DLOCK_READONLY, [&](BYTE *p, int pitch) {
            memcpy(&in2, p + 16 * pitch + 16 * bpp, bpp), memcpy(&gdi2, p + 32 * pitch + 32 * bpp, bpp);
        }))
        return false;
    if (bpp == 4) return want("READONLY lock (16,16)", in2, a) && want("READONLY lock (32,32)", gdi2, b);
    if (in2 != packed(a) || gdi2 != packed(b))
        return snprintf(why, sizeof why, "READONLY lock (16,16) %04lx want %04lx, (32,32) %04lx want %04lx",
                        (unsigned long)in2, (unsigned long)packed(a), (unsigned long)gdi2, (unsigned long)packed(b)),
               false;
    return true;
}
// dc-mem: GetDC and ReleaseDC of the CPU stores (dcMem): a 64x64 X8R8G8B8 and an R5G6B5 system-memory plain surface,
// a default-pool X8R8G8B8 plain surface and a face of a 64-texel MANAGED X8R8G8B8 cube. An A2R10G10B10 system-memory
// surface has no GDI format: GetDC is D3DERR_INVALIDCALL and leaves the handle.
static bool rowDcMem() {
    Com<IDirect3DSurface9> sys, r565, plain, face, a2;
    Com<IDirect3DCubeTexture9> cube;
    CK(CreateOffscreenPlainSurface, 64, 64, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sys.p, nullptr);
    CK(CreateOffscreenPlainSurface, 64, 64, D3DFMT_R5G6B5, D3DPOOL_SYSTEMMEM, &r565.p, nullptr);
    CK(CreateOffscreenPlainSurface, 64, 64, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &plain.p, nullptr);
    CK(CreateCubeTexture, 64, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &cube.p, nullptr);
    CK(CreateOffscreenPlainSurface, 64, 64, D3DFMT_A2R10G10B10, D3DPOOL_SYSTEMMEM, &a2.p, nullptr);
    const HRESULT hr = cube.p->GetCubeMapSurface(D3DCUBEMAP_FACE_NEGATIVE_Y, 0, &face.p);
    if (FAILED(hr)) return fail("GetCubeMapSurface", hr);
    if (!dcMem(sys.p, 4, KRUST, KAQUA)) return failIn("system memory");
    if (!dcMem(r565.p, 2, K565A, K565B)) return failIn("R5G6B5");
    if (!dcMem(plain.p, 4, KMINT, KROSE)) return failIn("default pool");
    if (!dcMem(face.p, 4, KSAGE, KLILAC)) return failIn("cube face");
    return noDc(a2.p, "A2R10G10B10: GetDC");
}
// dc-tex: a 64x64 MANAGED X8R8G8B8 texture written steel through a lock and drawn once by show() (steel: it is
// uploaded); under GetDC of its level 0 GetPixel reads steel at (8,8) and rust fills texels (16..47, 16..47); after
// ReleaseDC the quad drawn again shows rust at texel (32,32) and steel at (8,8) (the level is dirty again), read back.
static bool rowDcTex() {
    return until([&] {
        Com<IDirect3DTexture9> t;
        Com<IDirect3DSurface9> s;
        CK(CreateTexture, 64, 64, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &t.p, nullptr);
        const auto shown = [&](const char *when, DWORD at32) {
            return scene([&] { return backToScreen() && clearTo(KLILAC) && show(t.p); }) &&
                   rtHas(when, rtSurf, 256, D3DFMT_X8R8G8B8, {{129, 129, at32}, {81, 81, KSTEEL}});
        };
        if (!fill(t.p, 0, 4, [](int, int) { return KSTEEL; }) || !levelOf(t.p, 0, s) || !shown("drawn", KSTEEL))
            return false;
        COLORREF in = CLR_INVALID;
        if (!underDc(s.p, "GetDC", [&](HDC dc) {
                in = GetPixel(dc, 8, 8);
                return gdiFill(dc, 16, 16, 48, 48, KRUST);
            }))
            return false;
        return want("GetPixel (8,8)", in, gdi(KSTEEL)) && shown("drawn after ReleaseDC", KRUST);
    });
}
// dc-rtex: a 64x64 render-target texture's level 0 cleared steel with a rust triangle (4,4)-(36,4)-(4,36); under GetDC
// GetPixel reads rust at (12,12) and steel at (56,56), and aqua fills (40..55, 8..23); after ReleaseDC
// GetRenderTargetData reads aqua at (48,16), rust at (12,12) and steel at (56,56).
static bool rowDcRtex() {
    return until([&] {
        Com<IDirect3DTexture9> t;
        Com<IDirect3DSurface9> s;
        const RV tri[3] = {{4, 4, 0.5f, 1, KRUST}, {36, 4, 0.5f, 1, KRUST}, {4, 36, 0.5f, 1, KRUST}};
        COLORREF in = CLR_INVALID, out = CLR_INVALID;
        if (!makeRt(t) || !levelOf(t.p, 0, s) ||
            !scene([&] { return target(t.p) && clearTo(KSTEEL) && rtri(tri) && backToScreen(); }) ||
            !underDc(s.p, "GetDC", [&](HDC dc) {
                in = GetPixel(dc, 12, 12), out = GetPixel(dc, 56, 56);
                return gdiFill(dc, 40, 8, 56, 24, KAQUA);
            }))
            return false;
        return want("GetPixel (12,12)", in, gdi(KRUST)) && want("GetPixel (56,56)", out, gdi(KSTEEL)) &&
               rtHas("after ReleaseDC", s.p, 64, D3DFMT_A8R8G8B8, {{48, 16, KAQUA}, {12, 12, KRUST}, {56, 56, KSTEEL}});
    });
}
// dc-rt: L, a lockable 64x64 target, written lilac whole through a lock (its staging holds lilac), then cleared steel
// with a rust triangle (4,4)-(36,4)-(4,36): under GetDC GetPixel reads rust at (12,12) and steel at (56,56), not lilac
// (the staging is read back first), and aqua fills (40..55, 8..23); after ReleaseDC a gold triangle (60,28)-(60,60)-
// (28,60) is drawn: GetRenderTargetData reads aqua at (48,16), rust at (12,12), gold at (52,52) and steel at (8,56).
static bool rowDcRt() {
    return until([&] {
        Com<IDirect3DSurface9> l;
        const RV rust[3] = {{4, 4, 0.5f, 1, KRUST}, {36, 4, 0.5f, 1, KRUST}, {4, 36, 0.5f, 1, KRUST}};
        const RV gold[3] = {{60, 60, 0.5f, 1, KGOLD}, {28, 60, 0.5f, 1, KGOLD}, {60, 28, 0.5f, 1, KGOLD}};
        COLORREF in = CLR_INVALID, out = CLR_INVALID;
        if (!target64(l) ||
            !underLock(l.p, "LockRect", nullptr, 0, [&](BYTE *b, int p) { paint(b, p, 0, 0, 64, 64, KLILAC); }) ||
            !scene([&] {
                CK(SetRenderTarget, 0, l.p);
                return clearTo(KSTEEL) && rtri(rust) && backToScreen();
            }) ||
            !underDc(l.p, "GetDC", [&](HDC dc) {
                in = GetPixel(dc, 12, 12), out = GetPixel(dc, 56, 56);
                return gdiFill(dc, 40, 8, 56, 24, KAQUA);
            }))
            return false;
        return want("GetPixel (12,12)", in, gdi(KRUST)) && want("GetPixel (56,56)", out, gdi(KSTEEL)) && scene([&] {
                   CK(SetRenderTarget, 0, l.p);
                   return rtri(gold) && backToScreen();
               }) &&
               rtHas("after ReleaseDC", l.p, 64, D3DFMT_A8R8G8B8,
                     {{48, 16, KAQUA}, {12, 12, KRUST}, {52, 52, KGOLD}, {8, 56, KSTEEL}});
    });
}
// dc-bb: the device's back buffer (no D3DPRESENTFLAG_LOCKABLE_BACKBUFFER): GetDC is D3DERR_INVALIDCALL and leaves the
// handle; ReleaseDC of a null handle is D3DERR_INVALIDCALL.
static bool rowDcBb() {
    Com<IDirect3DSurface9> bb;
    CK(GetBackBuffer, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb.p);
    if (!noDc(bb.p, "GetDC")) return false;
    const HRESULT hr = bb.p->ReleaseDC(nullptr);
    return hr == D3DERR_INVALIDCALL || fail("ReleaseDC (a null handle)", hr);
}

// The back-buffer rows' rust triangle over a steel clear: (40,40) inside it, (248,8) the clear's.
static const RV BB_RUST[3] = {{16, 16, 0.5f, 1, KRUST}, {112, 16, 0.5f, 1, KRUST}, {16, 112, 0.5f, 1, KRUST}};
// 256x256 X8R8G8B8 windowed parameters on `w`, D3DSWAPEFFECT_COPY: an additional swap chain's or the lockable
// device's (`flags` D3DPRESENTFLAG_LOCKABLE_BACKBUFFER).
static D3DPRESENT_PARAMETERS params256(HWND w, DWORD flags) {
    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = 256, pp.BackBufferHeight = 256, pp.BackBufferFormat = D3DFMT_X8R8G8B8, pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_COPY, pp.hDeviceWindow = w, pp.Windowed = TRUE, pp.Flags = flags;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    return pp;
}
// An additional swap chain of `dev` on `w` (null: the device's window) and its back buffer.
static bool chainBackBuffer(HWND w, DWORD flags, Com<IDirect3DSwapChain9> &sc, Com<IDirect3DSurface9> &bb) {
    D3DPRESENT_PARAMETERS pp = params256(w, flags);
    CK(CreateAdditionalSwapChain, &pp, &sc.p);
    const HRESULT hr = sc.p->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb.p);
    return SUCCEEDED(hr) || fail("IDirect3DSwapChain9::GetBackBuffer", hr);
}
// rt-chain: an additional swap chain of the device's size on the device's window (a null hDeviceWindow, which names
// the device's window: this mode is given no window, and the core refused GetCreationParameters before DXMTRS
// C4c-6a); the device's back buffer cleared steel with the rust triangle. The chain's back
// buffer (an alias of the device's) locked READONLY reads rust at (40,40) and steel at (248,8); LockRect(NULL, 0) of
// it is D3DERR_INVALIDCALL (the device has no lockable back buffer); GetRenderTargetData of it into a system-memory
// surface reads the same two texels.
static bool rowRtChain() {
    return until([&] {
        Com<IDirect3DSwapChain9> sc;
        Com<IDirect3DSurface9> bb;
        DWORD in = 0, out = 0;
        if (!chainBackBuffer(nullptr, 0, sc, bb) ||
            !scene([&] { return backToScreen() && clearTo(KSTEEL) && rtri(BB_RUST); }) ||
            !underLock(bb.p, "LockRect (READONLY)", nullptr, D3DLOCK_READONLY, [&](BYTE *b, int p) {
                in = texel(b, p, 40, 40), out = texel(b, p, 248, 8);
            }))
            return false;
        if (!want("READONLY lock (40,40)", in, KRUST) || !want("READONLY lock (248,8)", out, KSTEEL)) return false;
        D3DLOCKED_RECT lr = {};
        const HRESULT hr = bb.p->LockRect(&lr, nullptr, 0);
        if (SUCCEEDED(hr)) bb.p->UnlockRect();
        if (hr != D3DERR_INVALIDCALL) return fail("LockRect (NULL, 0)", hr);
        return rtHas("GetRenderTargetData", bb.p, 256, D3DFMT_X8R8G8B8, {{40, 40, KRUST}, {248, 8, KSTEEL}});
    });
}

// The lockable device of lb-lock and lb-dc: a second device from main's IDirect3D9 on a visible window of its own,
// params256 with D3DPRESENTFLAG_LOCKABLE_BACKBUFFER, never presented. A row makes it once (one made again for each
// attempt would build its pipelines again); its members release the back buffer, then the device, then the window.
struct Window {
    HWND h = nullptr;
    ~Window() { if (h) DestroyWindow(h); }
};
struct Lockable {
    Window w;
    Com<IDirect3DDevice9> d;
    Com<IDirect3DSurface9> bb;
};
static bool makeLockable(Lockable &l) {
    RECT r = {0, 0, 256, 256};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    l.w.h = CreateWindowW(L"d3d9_triangle", L"d3d9_triangle lockable", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 400, 100,
                          r.right - r.left, r.bottom - r.top, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!l.w.h) return snprintf(why, sizeof why, "CreateWindowW"), false;
    D3DPRESENT_PARAMETERS pp = params256(l.w.h, D3DPRESENTFLAG_LOCKABLE_BACKBUFFER);
    HRESULT hr = direct3d ? direct3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, l.w.h,
                                                   D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &l.d.p)
                          : E_POINTER;
    if (FAILED(hr)) return fail("CreateDevice (a lockable back buffer)", hr);
    hr = l.d.p->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &l.bb.p);
    return SUCCEEDED(hr) || fail("GetBackBuffer (the lockable device)", hr);
}
// While it lives, the globals `dev` and `rtSurf` that the helpers read name another device and its back buffer; then
// main's again. Declared inside an attempt, it ends before until()'s Present, which presents main's device.
struct OnDevice {
    IDirect3DDevice9 *const dev0 = dev;
    IDirect3DSurface9 *const rt0 = rtSurf;
    OnDevice(IDirect3DDevice9 *d, IDirect3DSurface9 *rt) { dev = d, rtSurf = rt; }
    ~OnDevice() { dev = dev0, rtSurf = rt0; }
};
// lb-lock (on the lockable device): its back buffer cleared steel with the rust triangle. LockRect(NULL, 0) reads rust
// at (40,40) and steel at (248,8), and aqua is written over (160..223, 16..79); after UnlockRect a gold triangle
// (240,144)-(240,240)-(144,240) is drawn: GetRenderTargetData reads aqua at (192,48), rust, gold at (216,216) and steel
// (the pass after the write loads). LockRect(NULL, DISCARD) with mint written over (100..139, 100..139): mint there and
// the four texels above as they were (the lock still reads the frame, mtld3d's rule). LockRect of (8,8)-(16,16) with
// rose written at pBits: rose at (8,8), steel at (0,0). A whole READONLY lock with lilac written at (40,40): rust there
// after the unlock. A second LockRect while locked and one of an empty rect are D3DERR_INVALIDCALL. An additional
// swap chain's back buffer locked LockRect(NULL, 0) with sage written over (40..71, 160..223): the device's back
// buffer reads sage at (56,192).
static bool rowLbLock() {
    Lockable l;
    if (!makeLockable(l)) return false;
    return until([&] {
        const OnDevice on(l.d.p, l.bb.p);
        IDirect3DSurface9 *const bb = l.bb.p;
        const RV gold[3] = {{240, 240, 0.5f, 1, KGOLD}, {144, 240, 0.5f, 1, KGOLD}, {240, 144, 0.5f, 1, KGOLD}};
        const D3DFORMAT x8 = D3DFMT_X8R8G8B8;
        DWORD in = 0, out = 0;
        if (!scene([&] { return backToScreen() && clearTo(KSTEEL) && rtri(BB_RUST); }) ||
            !underLock(bb, "LockRect (NULL, 0)", nullptr, 0, [&](BYTE *b, int p) {
                in = texel(b, p, 40, 40), out = texel(b, p, 248, 8);
                paint(b, p, 160, 16, 224, 80, KAQUA);
            }))
            return false;
        if (!want("lock (40,40)", in, KRUST) || !want("lock (248,8)", out, KSTEEL) ||
            !scene([&] { return rtri(gold); }) ||
            !rtHas("written", bb, 256, x8, {{192, 48, KAQUA}, {40, 40, KRUST}, {216, 216, KGOLD}, {248, 8, KSTEEL}}) ||
            !underLock(bb, "LockRect (NULL, DISCARD)", nullptr, D3DLOCK_DISCARD,
                       [&](BYTE *b, int p) { paint(b, p, 100, 100, 140, 140, KMINT); }) ||
            !rtHas("DISCARD", bb, 256, x8,
                   {{120, 120, KMINT}, {192, 48, KAQUA}, {40, 40, KRUST}, {216, 216, KGOLD}, {248, 8, KSTEEL}}))
            return false;
        const RECT r = {8, 8, 16, 16}, empty = {10, 10, 10, 11};
        if (!underLock(bb, "LockRect (8,8)-(16,16)", &r, 0, [&](BYTE *b, int) { paint(b, 0, 0, 0, 1, 1, KROSE); }) ||
            !rtHas("rect lock", bb, 256, x8, {{8, 8, KROSE}, {0, 0, KSTEEL}}) ||
            !underLock(bb, "LockRect (NULL, READONLY)", nullptr, D3DLOCK_READONLY,
                       [&](BYTE *b, int p) { paint(b, p, 40, 40, 41, 41, KLILAC); }) ||
            !rtHas("after a READONLY lock", bb, 256, x8, {{40, 40, KRUST}}))
            return false;
        D3DLOCKED_RECT lr = {};
        HRESULT hr = bb->LockRect(&lr, nullptr, D3DLOCK_READONLY);
        if (FAILED(hr)) return fail("LockRect", hr);
        const HRESULT second = bb->LockRect(&lr, nullptr, D3DLOCK_READONLY);
        hr = bb->UnlockRect();
        if (second != D3DERR_INVALIDCALL) return fail("LockRect (locked)", second);
        if (FAILED(hr)) return fail("UnlockRect", hr);
        hr = bb->LockRect(&lr, &empty, 0);
        if (SUCCEEDED(hr)) bb->UnlockRect();
        if (hr != D3DERR_INVALIDCALL) return fail("LockRect (an empty rect)", hr);
        Com<IDirect3DSwapChain9> sc;
        Com<IDirect3DSurface9> chain;
        return chainBackBuffer(l.w.h, D3DPRESENTFLAG_LOCKABLE_BACKBUFFER, sc, chain) &&
               underLock(chain.p, "the chain's LockRect (NULL, 0)", nullptr, 0,
                         [&](BYTE *b, int p) { paint(b, p, 40, 160, 72, 224, KSAGE); }) &&
               rtHas("the chain's write", bb, 256, x8, {{56, 192, KSAGE}, {40, 40, KRUST}});
    });
}
// lb-dc (on the lockable device): its back buffer cleared steel with the rust triangle; under GetDC GetPixel reads rust
// at (40,40) and steel at (248,8), and aqua fills (160..223, 16..79); after ReleaseDC GetRenderTargetData reads aqua at
// (192,48), rust and steel. Under a second GetDC mint fills (100..139, 100..139) and UnlockRect, no lock held, is
// D3D_OK; after ReleaseDC mint is at (120,120) and aqua kept. Under GetDC of an additional swap chain's back buffer
// sage fills (40..71, 160..223); after ReleaseDC the device's back buffer reads sage at (56,192).
static bool rowLbDc() {
    Lockable l;
    if (!makeLockable(l)) return false;
    return until([&] {
        const OnDevice on(l.d.p, l.bb.p);
        IDirect3DSurface9 *const bb = l.bb.p;
        const D3DFORMAT x8 = D3DFMT_X8R8G8B8;
        COLORREF in = CLR_INVALID, out = CLR_INVALID;
        if (!scene([&] { return backToScreen() && clearTo(KSTEEL) && rtri(BB_RUST); }) ||
            !underDc(bb, "GetDC", [&](HDC dc) {
                in = GetPixel(dc, 40, 40), out = GetPixel(dc, 248, 8);
                return gdiFill(dc, 160, 16, 224, 80, KAQUA);
            }))
            return false;
        HRESULT unlocked = E_FAIL;
        if (!want("GetPixel (40,40)", in, gdi(KRUST)) || !want("GetPixel (248,8)", out, gdi(KSTEEL)) ||
            !rtHas("after ReleaseDC", bb, 256, x8, {{192, 48, KAQUA}, {40, 40, KRUST}, {248, 8, KSTEEL}}) ||
            !underDc(bb, "GetDC (the second)", [&](HDC dc) {
                if (!gdiFill(dc, 100, 100, 140, 140, KMINT)) return false;
                unlocked = bb->UnlockRect();
                return true;
            }))
            return false;
        if (unlocked != D3D_OK) return fail("UnlockRect (a DC held, no lock)", unlocked);
        Com<IDirect3DSwapChain9> sc;
        Com<IDirect3DSurface9> chain;
        return rtHas("UnlockRect under the DC", bb, 256, x8, {{120, 120, KMINT}, {192, 48, KAQUA}}) &&
               chainBackBuffer(l.w.h, D3DPRESENTFLAG_LOCKABLE_BACKBUFFER, sc, chain) &&
               underDc(chain.p, "the chain's GetDC", [&](HDC dc) { return gdiFill(dc, 40, 160, 72, 224, KSAGE); }) &&
               rtHas("the chain's DC", bb, 256, x8, {{56, 192, KSAGE}, {40, 40, KRUST}});
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
        {"rt-nolock", rowRtNolock},     {"rt-bblock", rowRtBblock},     {"rt-front", rowRtFront},
        {"dc-mem", rowDcMem},           {"dc-tex", rowDcTex},           {"dc-rtex", rowDcRtex},
        {"dc-rt", rowDcRt},             {"dc-bb", rowDcBb},             {"rt-chain", rowRtChain},
        // The lockable device's rows last (a second device beside the first; DXMTRS C4c-3b).
        {"lb-lock", rowLbLock},         {"lb-dc", rowLbDc}};
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
// A size x size depth-stencil surface of format f and `type` samples, not discarded.
static bool makeDs(Com<IDirect3DSurface9> &s, D3DFORMAT f = D3DFMT_D24X8, UINT size = 256,
                   D3DMULTISAMPLE_TYPE type = D3DMULTISAMPLE_NONE) {
    CK(CreateDepthStencilSurface, size, size, f, type, 0, FALSE, &s.p, nullptr);
    return true;
}
static bool makeDepthTex(Com<IDirect3DTexture9> &t, D3DFORMAT f, UINT levels = 1, DWORD usage = D3DUSAGE_DEPTHSTENCIL,
                         UINT size = 256) {
    CK(CreateTexture, size, size, levels, usage, f, D3DPOOL_DEFAULT, &t.p, nullptr);
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
// What a depth-copy or depth-resolve row of the blit and msaa modes (DXMTRS C4c-4b's sc-depth, ms-dcopy, C4c-4d's
// sd-resolve, ms-dres) leaves on every path, passed or not, and their reset() does not restore: `ds` bound (null:
// none), ZENABLE `zenable` (as the mode's device has it), LESSEQUAL with Z writes, the stencil test off at ALWAYS and
// reference 0. Its own answers are not judged.
static void depthBack(IDirect3DSurface9 *ds, DWORD zenable) {
    const DWORD rs[][2] = {{D3DRS_ZENABLE, zenable},       {D3DRS_ZFUNC, D3DCMP_LESSEQUAL},
                           {D3DRS_ZWRITEENABLE, TRUE},     {D3DRS_STENCILENABLE, FALSE},
                           {D3DRS_STENCILFUNC, D3DCMP_ALWAYS}, {D3DRS_STENCILREF, 0}};
    dev->SetDepthStencilSurface(ds);
    for (const auto &s : rs) dev->SetRenderState((D3DRENDERSTATETYPE)s[0], s[1]);
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
// `t` sampled by the fixed-function stage at (u, v) with a third coordinate (a shadow sample's reference) of 0.5,
// over the 64x64 quad at (x, y).
static bool sampleAt(IDirect3DBaseTexture9 *t, float x, float y, float u, float v = 0.5f) {
    TV3 q[4];
    tquad(q, x, y, x + 64, y + 64, u, v, 0.5f);
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
// A dynamic depth texture's lock (DXMTRS C4c-3b): 64x64 D3DUSAGE_DYNAMIC textures written through LockRect in mtld3d's
// packings and sampled as shadow maps (reference 0.5: lit, white, over 0.75; not, black, over 0.25), each sample at a
// texel's centre on an odd row. D16 is a little-endian WORD a texel, 0x4000 for 0.25 and 0xbfff for 0.75; D24X8 has
// its depth in bytes 0-2 under a byte 3 that is not zero (0xee400000, 0xeebfffff); D24S8 stencil 0xa5 in byte 0 under
// its depth in bytes 1-3 (0x400000a5, 0xbfffffa5).
struct DynDepth { D3DFORMAT f; const char *name; int bpp; DWORD lo, hi; };
static const DynDepth DYN_DEPTH[] = {{D3DFMT_D16, "D16", 2, 0x4000, 0xbfff},
                                     {D3DFMT_D24X8, "D24X8", 4, 0xee400000, 0xeebfffff},
                                     {D3DFMT_D24S8, "D24S8", 4, 0x400000a5, 0xbfffffa5}};
// Level `level` of the 64-texel `t` written: its texels left of the middle column `left`, the others `right`.
static bool halves(IDirect3DTexture9 *t, UINT level, const DynDepth &k, DWORD left, DWORD right) {
    const int mid = 32 >> level;
    return fill(t, level, k.bpp, [&](int x, int) { return x < mid ? left : right; });
}
// Texel (tx, ty) of a level `w` texels wide sampled, held to that level (MIPFILTER POINT, MAXMIPLEVEL `level`) when
// `level` is not 0; lit or not.
struct DepthProbe { int tx, ty, w; bool lit; UINT level = 0; };
// One frame: Z cleared, write() (the locks: the same bytes on a retry), then probe i over the quad at (64 i, 96).
template <class W> static bool dynFrame(IDirect3DTexture9 *t, W write, std::initializer_list<DepthProbe> probes) {
    Px px[4];
    int n = 0;
    for (const DepthProbe &p : probes) px[n] = {64 * n + 32, 128, p.lit ? 0xffffffu : 0u}, n++;
    return frames(BLUE, px, n, [&] {
        if (!clearZ() || !write()) return false;
        int i = 0;
        for (const DepthProbe &p : probes) {
            CK(SetSamplerState, 0, D3DSAMP_MIPFILTER, p.level ? D3DTEXF_POINT : D3DTEXF_NONE);
            CK(SetSamplerState, 0, D3DSAMP_MAXMIPLEVEL, p.level);
            if (!sampleAt(t, 64.0f * i++, 96, (p.tx + 0.5f) / p.w, (p.ty + 0.5f) / p.w)) return false;
        }
        CK(SetSamplerState, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        CK(SetSamplerState, 0, D3DSAMP_MAXMIPLEVEL, 0);
        return true;
    });
}
// ds-dynlock: per format, one texture: the left half 0.25 and the right 0.75 (texel (48,47) lit, (16,47) not), then
// locked again with the halves swapped (swapped). On D24X8 the halves again and the top-left quadrant 0.75 through
// GetSurfaceLevel(0)'s LockRect of (0,0)-(32,32): (16,15) lit, (16,47) still not. On a D24S8 texture with its full
// chain: level 0 the halves, level 1 (32x32) swapped and level 5 (2x2) 0.25 | 0.75, each sampled held to its level:
// level 1's (8,15) lit and (24,15) not, level 5's (1,1) lit and (0,1) not. One frames() a state.
static bool rowDsDynLock() {
    for (const DynDepth &k : DYN_DEPTH) {
        Com<IDirect3DTexture9> t;
        CK(CreateTexture, 64, 64, 1, D3DUSAGE_DYNAMIC, k.f, D3DPOOL_DEFAULT, &t.p, nullptr);
        if (!dynFrame(t.p, [&] { return halves(t.p, 0, k, k.lo, k.hi); }, {{16, 47, 64, false}, {48, 47, 64, true}}))
            return failIn(k.name);
        if (!dynFrame(t.p, [&] { return halves(t.p, 0, k, k.hi, k.lo); }, {{16, 47, 64, true}, {48, 47, 64, false}}))
            return failIn(k.name, " swapped");
        if (k.f != D3DFMT_D24X8) continue;
        const auto quadrant = [&] {
            Com<IDirect3DSurface9> s;
            const RECT r = {0, 0, 32, 32};
            return halves(t.p, 0, k, k.lo, k.hi) && levelOf(t.p, 0, s) &&
                   underLock(s.p, "the level's LockRect (0,0)-(32,32)", &r, 0,
                             [&](BYTE *b, int p) { paint(b, p, 0, 0, 32, 32, k.hi); });
        };
        if (!dynFrame(t.p, quadrant, {{16, 15, 64, true}, {16, 47, 64, false}})) return failIn(k.name, " quadrant");
    }
    const DynDepth &k = DYN_DEPTH[2];
    Com<IDirect3DTexture9> t;
    CK(CreateTexture, 64, 64, 0, D3DUSAGE_DYNAMIC, k.f, D3DPOOL_DEFAULT, &t.p, nullptr);
    const auto levels = [&] {
        return halves(t.p, 0, k, k.lo, k.hi) && halves(t.p, 1, k, k.hi, k.lo) && halves(t.p, 5, k, k.lo, k.hi);
    };
    if (!dynFrame(t.p, levels, {{8, 15, 32, true, 1}, {24, 15, 32, false, 1}, {1, 1, 2, true, 5}, {0, 1, 2, false, 5}}))
        return failIn(k.name, " levels");
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
        {"ds-huge", rowDsHuge},         {"ds-dynlock", rowDsDynLock}};
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
// Whether the adapter offers `type` for a windowed A8R8G8B8 target (or format f: the extras mode asks D24S8 too)
// (CheckDeviceMultiSampleType), asked of main's IDirect3D9 (the core refuses GetDirect3D, which would hide the 2- and
// 8-sample cells from it).
static bool offers(D3DMULTISAMPLE_TYPE type, D3DFORMAT f = A8) {
    return direct3d && SUCCEEDED(direct3d->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, f, TRUE,
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
// D3DERR_INVALIDCALL in every lane; a depth-stencil StretchRect (the auto one into a 4-sample D24S8 surface, a copy of
// equal sample counts since DXMTRS C4c-4b; ms-dcopy judges its planes) is D3D_OK under core (strict), any answer
// elsewhere.
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
    if (strictMode && hr != D3D_OK) return fail("StretchRect (depth-stencil)", hr);
    return true;
}
// ms-dcopy (DXMTRS C4c-4b): the auto depth-stencil (4 samples) cleared to Z 0.25 and stencil 3; D, a 256x256 D24S8
// surface of 4 samples, bound and cleared to Z 1 and stencil 0; StretchRect of the auto one into D, whole, with no draw
// between: D3D_OK in every lane. D bound with the back buffer, cleared blue, Z on: a green quad at z 0.1 under stencil
// EQUAL 3 over (32,32)-(112,224) draws (the stencil was copied), then a red one at z 0.5 under LESSEQUAL with the
// stencil test off over (144,32)-(224,224) does not (the depth was copied; the quads are apart, so the green one's Z
// write does not decide it): green at (72,128), blue at (184,128) and (128,128). Its depth-stencil state back on every
// path (msUntil restores ZENABLE and the auto depth-stencil alone).
static bool rowMsDcopy() {
    return msUntil([&] {
        Com<IDirect3DSurface9> d;
        const bool ok = [&] {
            CK(CreateDepthStencilSurface, 256, 256, D3DFMT_D24S8, D3DMULTISAMPLE_4_SAMPLES, 0, FALSE, &d.p, nullptr);
            if (!scene([&] {
                    return backToScreen() && clearTo(TBLU) && bindDs(dsSurf) && clearZS(0.25f, 3) && bindDs(d.p) &&
                           clearZS(1.0f, 0);
                }))
                return false;
            CK(StretchRect, dsSurf, nullptr, d.p, nullptr, D3DTEXF_NONE);
            return scene([&] {
                       return rstate(D3DRS_ZENABLE, D3DZB_TRUE) && rstate(D3DRS_STENCILENABLE, TRUE) &&
                              rstate(D3DRS_STENCILFUNC, D3DCMP_EQUAL) && rstate(D3DRS_STENCILREF, 3) &&
                              zquad(32, 32, 112, 224, 0.1f, TGRN) && rstate(D3DRS_STENCILENABLE, FALSE) &&
                              zquad(144, 32, 224, 224, 0.5f, TRED);
                   }) &&
                   msHas("the back buffer", rtSurf, {{72, 128, TGRN}, {184, 128, TBLU}, {128, 128, TBLU}});
        }();
        depthBack(dsSurf, D3DZB_FALSE);
        return ok;
    });
}
// ms-dres (DXMTRS C4c-4d): the auto depth-stencil (4 samples) cleared to Z 0.25 and stencil 3 beside the back buffer;
// D, a 256x256 D24S8 surface of one sample, bound, then S, a single-sample 256x256 target, beside it (a Clear or a draw
// reaches a depth-stencil only beside a target of its sample count: ms-mismatch), S cleared blue and D to Z 1 and
// stencil 0; StretchRect of the auto one into D, whole, between scenes, with no draw between: D3D_OK in every lane (the
// depth-stencil resolve). S and D bound, Z on: a green quad at z 0.1 under stencil EQUAL 3 over (16,32)-(80,224) draws
// (the stencil was resolved), then a red one at z 0.5 under LESSEQUAL with the stencil test off over (96,32)-(160,224)
// (the depth was) and a yellow one at z 0.1 under EQUAL 0 over (176,32)-(240,224) do not: green at (48,128), blue at
// (128,128) and (208,128). Its depth-stencil state back on every path (msUntil restores ZENABLE and the auto
// depth-stencil alone).
static bool rowMsDres() {
    return msUntil([&] {
        Rt s;
        Com<IDirect3DSurface9> d;
        const bool ok = [&] {
            if (!mkRt(s, 256) || !makeDs(d, D3DFMT_D24S8, 256) || !scene([&] {
                    return backToScreen() && bindDs(dsSurf) && clearZS(0.25f, 3) && bindDs(d.p) &&
                           clearRt(s.s.p, TBLU) && clearZS(1.0f, 0);
                }))
                return false;
            CK(StretchRect, dsSurf, nullptr, d.p, nullptr, D3DTEXF_NONE);
            return scene([&] {
                       return rstate(D3DRS_ZENABLE, D3DZB_TRUE) && rstate(D3DRS_STENCILENABLE, TRUE) &&
                              rstate(D3DRS_STENCILFUNC, D3DCMP_EQUAL) && rstate(D3DRS_STENCILREF, 3) &&
                              zquad(16, 32, 80, 224, 0.1f, TGRN) && rstate(D3DRS_STENCILENABLE, FALSE) &&
                              zquad(96, 32, 160, 224, 0.5f, TRED) && rstate(D3DRS_STENCILENABLE, TRUE) &&
                              rstate(D3DRS_STENCILREF, 0) && zquad(176, 32, 240, 224, 0.1f, TYEL) &&
                              rstate(D3DRS_STENCILENABLE, FALSE) && backToScreen();
                   }) &&
                   rtHas("S", s.s.p, 256, A8, {{48, 128, TGRN}, {128, 128, TBLU}, {208, 128, TBLU}});
        }();
        depthBack(dsSurf, D3DZB_FALSE);
        return ok;
    });
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
// D3DERR_INVALIDCALL in every lane; its StretchRect into a 32x32 target (a scaled copy, past the whole-surface resolve;
// the core's since DXMTRS C4c-4c) is D3D_OK in every lane: red at (8,16) and green at (24,16) (texel i samples the
// multisampled one's 2i + 1.0, between two texels of one colour: its edge is at 32).
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
        if (hr != D3D_OK) return fail("StretchRect (into 32x32)", hr);
        return rtHas("a 32x32 target", small.p, 32, A8, {{8, 16, TRED}, {24, 16, TGRN}});
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
        {"ms-desc", rowMsDesc},         {"ms-dcopy", rowMsDcopy},       {"ms-dres", rowMsDres},
        {"ms-resolve", rowMsResolve},   {"ms-edge", rowMsEdge},         {"ms-back", rowMsBack},
        {"ms-clear", rowMsClear},       {"ms-depth", rowMsDepth},       {"ms-mismatch", rowMsMismatch},
        {"ms-mask", rowMsMask},         {"ms-nonmask", rowMsNonMask},   {"ms-a2c", rowMsA2c},
        {"ms-srgbmask", rowMsSrgbMask}, {"ms-blend", rowMsBlend},       {"ms-mrt", rowMsMrt},
        {"ms-scissor", rowMsScissor},   {"ms-redraw", rowMsRedraw},     {"ms-samples", rowMsSamples},
        {"ms-bblock", rowMsBblock},     {"ms-front", rowMsFront}};
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

// --- The "blit" mode (DXMTRS C4c-4a): ColorFill, UpdateSurface and StretchRect's answers, and (DXMTRS C4c-4b, the
// sc-* rows) StretchRect's unscaled copies and (DXMTRS C4c-4c, the sq-* rows) its sampled ones (scaled, converting,
// from YUV, multisampled, overlapping) and (DXMTRS C4c-4d, sd-resolve) its depth-stencil resolve, on the buffers mode's
// plain device (256x256 X8R8G8B8, no depth-stencil: a row that needs a depth-stencil surface makes one). "A target" is
// a 64x64 A8R8G8B8 CreateRenderTarget surface, not lockable unless said; quads are pretransformed, in the pixels of the
// target bound at index 0. A row that reads texels
// back makes its objects inside one until() attempt, reads with GetRenderTargetData or a lock (unlocked before its
// texels are judged), and judges only texels the attempt wrote. Its colours have every channel non-zero and no two
// alike, and each one the GPU fills with has a channel in 0x10..0xf0 (an sRGB encode leaves 0 and 255 as they are). A
// region write (a rect ColorFill of a target, UpdateSurface) starts off the origin, lands at a point other than its
// source's origin and is no multiple of 4 wide (DXT1's block rects and the plain surface's (4,4)-(8,8) excepted); a
// cf-* or us-* GPU write of a target has a draw on it before and one after whose texels pin the order (cf-rtex's first
// use, a fill over a lock-write, cf-autogen's fill, which no SetRenderTarget may name, and the plain surface's CPU
// fills excepted; sc-order orders the copies against draws). An sc-* copy that judges texels moves a rect off its
// source's origin to another point, no multiple of 4 wide, and judges a texel inside it, one just outside it and the
// destination's (0,0) (whole copies and test_pointsize's at the origin excepted; a destination nothing wrote before is
// judged inside alone); a judged inside texel would read another colour were the rect taken from its source's origin
// (but in a copy from a cube face, cleared one colour, and in sc-order's first, from a target only cleared), and the
// levels and faces a copy must tell apart hold colours of their own; a depth cell draws a quad that must draw before
// each that must not. An sq-* copy keeps those rules (its whole copies, YUV's included, and the flat sources of
// sq-swizzle and sq-level, a cleared level and face, excepted) and: a scaled one judges texels whose source texel is
// plain under nearest sampling (a downscale by an even factor samples between two source texels, so its colour edges
// sit on multiples of the factor and each such pair is one colour), and LINEAR texels within 1 of the blend; a
// converting one judges alpha (WITH_ALPHA), its destination cleared with alpha 0x40 around the copy; an R5G6B5 one uses
// k565 colours (the GPU rounds, the CPU truncates), read 2 bytes a texel, and its way back within 1; a multisampled
// source is drawn, and probed, 2 texels or more from its quads' edges (an edge through a pixel's centre covers half its
// samples); sq-order's and sq-level's copies take their source from another texture than their destination. The sr-*
// and sd-* rows call neither ColorFill nor UpdateSurface and the cf-* rows no UpdateSurface; of the sc-* rows, sc-rtex,
// sc-tex and sc-autogen call UpdateSurface and sc-plain ColorFill; of the sq-* rows, sq-conv calls UpdateSurface (its
// usage-0 X8R8G8B8 texture) and none ColorFill. So a runtime that refuses one of the three reddens that call's rows and
// those sc-* and sq-* rows alone. A format CheckDeviceFormat does not offer prints "d3d9 <row>-<format> skip" and its
// cells do not run (the row still answers).
static const DWORD KPLUM = D3DCOLOR_XRGB(0x90, 0x30, 0xa0), KSKY = D3DCOLOR_XRGB(0x60, 0xb0, 0xf0);
// R5G6B5 colours of the update codec's widening (K565C's red field 0x1f widens to 0xff).
static const DWORD K565C = k565(0x1f, 0x2a, 0x0b), K565D = k565(0x0c, 0x22, 0x14);
static bool colorFill(IDirect3DSurface9 *s, const RECT *r, DWORD c) {
    CK(ColorFill, s, r, c);
    return true;
}
static bool updateSurface(IDirect3DSurface9 *src, const RECT *r, IDirect3DSurface9 *dst, const POINT *p) {
    CK(UpdateSurface, src, r, dst, p);
    return true;
}
// Whether main's IDirect3D9 offers f as a `type` of `usage` (0 unless said: D3DUSAGE_RENDERTARGET asks for a target);
// if not, the line "d3d9 <what> skip".
static bool offered(D3DRESOURCETYPE type, D3DFORMAT f, const char *what, DWORD usage = 0) {
    if (direct3d && SUCCEEDED(direct3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, X8, usage, type, f)))
        return true;
    printf("d3d9 %s skip\n", what);
    return false;
}
// Whether main's IDirect3D9 says StretchRect converts src into dst (CheckDeviceFormatConversion, as Wine's YUV tests
// ask); if not, the line "d3d9 <what> skip".
static bool converts(D3DFORMAT src, D3DFORMAT dst, const char *what) {
    if (direct3d && SUCCEEDED(direct3d->CheckDeviceFormatConversion(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, src, dst)))
        return true;
    printf("d3d9 %s skip\n", what);
    return false;
}
// A 64x64 4-byte surface copied out under lock(copy) (a READONLY lock that runs copy(bits, pitch)), then, unlocked, its
// texels px judged.
template <class L> static bool lockedHas(const char *name, L lock, std::initializer_list<Tx> px) {
    DWORD copy[64 * 64];
    if (!lock([&](BYTE *b, int p) {
            for (int y = 0; y < 64; y++) memcpy(copy + 64 * y, b + y * p, 256);
        }))
        return false;
    return txHas(name, (const BYTE *)copy, 256, px);
}
struct Stretch {
    const char *what;
    IDirect3DSurface9 *src;
    const RECT *srcRect;
    IDirect3DSurface9 *dst;
    const RECT *dstRect;
    D3DTEXTUREFILTERTYPE filter;
};

// cf-rt: R, a target, bound: cleared steel, filled rust whole (read: rust at (2,2) and (40,40)); a mint quad over
// (0,0)-(12,12), aqua filled over the rect (5,7)-(18,16), a gold quad over (14,12)-(24,22): mint at (2,2); rust at
// (40,40), (4,13), (18,8) and (6,16), around the rect; aqua at (5,7) (the rect's corner, where the mint quad was),
// (17,8) and (6,15); gold at (16,14) and (20,18). Cell "srgb": with D3DRS_SRGBWRITEENABLE on, lilac filled over the
// same rect, the state off, a rose quad over (0,14)-(8,24): lilac's own bytes at (8,9), (13,10) and (16,14) (where the
// gold quad was; a fill is not encoded), rose at (6,15), mint at (2,2), rust at (40,40).
static bool rowCfRt() {
    return until([&] {
        Com<IDirect3DSurface9> r;
        const RECT rect = {5, 7, 18, 16};
        CK(CreateRenderTarget, 64, 64, A8, D3DMULTISAMPLE_NONE, 0, FALSE, &r.p, nullptr);
        if (!scene([&] { return bindRt(0, r.p) && clearTo(KSTEEL); }) || !colorFill(r.p, nullptr, KRUST) ||
            !rtHas("whole", r.p, 64, A8, {{2, 2, KRUST}, {40, 40, KRUST}}) ||
            !scene([&] { return zquad(0, 0, 12, 12, 0.5f, KMINT); }) || !colorFill(r.p, &rect, KAQUA) ||
            !scene([&] { return zquad(14, 12, 24, 22, 0.5f, KGOLD); }) ||
            !rtHas("rect", r.p, 64, A8,
                   {{2, 2, KMINT}, {40, 40, KRUST}, {4, 13, KRUST}, {18, 8, KRUST}, {6, 16, KRUST}, {5, 7, KAQUA},
                    {17, 8, KAQUA}, {6, 15, KAQUA}, {16, 14, KGOLD}, {20, 18, KGOLD}}))
            return false;
        CK(SetRenderState, D3DRS_SRGBWRITEENABLE, TRUE);
        const HRESULT hr = dev->ColorFill(r.p, &rect, KLILAC);
        CK(SetRenderState, D3DRS_SRGBWRITEENABLE, FALSE);
        if (FAILED(hr)) return fail("ColorFill (srgb)", hr);
        return scene([&] { return zquad(0, 14, 8, 24, 0.5f, KROSE) && backToScreen(); }) &&
               rtHas("srgb", r.p, 64, A8,
                     {{8, 9, KLILAC}, {13, 10, KLILAC}, {16, 14, KLILAC}, {6, 15, KROSE}, {2, 2, KMINT},
                      {40, 40, KRUST}});
    });
}
// cf-lockable: L, a lockable target, bound: cleared steel, a mint quad over (0,0)-(8,8); rust filled over
// (3,5)-(16,12): a READONLY lock reads mint at (1,1), rust at (4,6) (where the quad was), (3,5) and (15,11), steel at
// (16,11), (15,12) and (20,20). Aqua written through a lock over (30..44, 30..44), gold filled over (33,37)-(50,46), a
// rose quad over (44,42)-(56,54): a READONLY lock reads aqua at (31,31), gold at (33,37), (40,44), (49,38) and (34,45),
// steel at (34,46) and (50,38), rose at (46,43) and (52,50), rust at (15,11), mint at (1,1).
static bool rowCfLockable() {
    return until([&] {
        Com<IDirect3DSurface9> l;
        const RECT first = {3, 5, 16, 12}, second = {33, 37, 50, 46};
        const auto read = [&](auto copy) {
            return underLock(l.p, "LockRect (READONLY)", nullptr, D3DLOCK_READONLY, copy);
        };
        return target64(l) && scene([&] {
                   return bindRt(0, l.p) && clearTo(KSTEEL) && zquad(0, 0, 8, 8, 0.5f, KMINT) && backToScreen();
               }) &&
               colorFill(l.p, &first, KRUST) &&
               lockedHas("the first fill", read,
                         {{1, 1, KMINT}, {4, 6, KRUST}, {3, 5, KRUST}, {15, 11, KRUST}, {16, 11, KSTEEL},
                          {15, 12, KSTEEL}, {20, 20, KSTEEL}}) &&
               underLock(l.p, "LockRect", nullptr, 0, [&](BYTE *b, int p) { paint(b, p, 30, 30, 45, 45, KAQUA); }) &&
               colorFill(l.p, &second, KGOLD) &&
               scene([&] { return bindRt(0, l.p) && zquad(44, 42, 56, 54, 0.5f, KROSE) && backToScreen(); }) &&
               lockedHas("the second fill", read,
                         {{31, 31, KAQUA}, {33, 37, KGOLD}, {40, 44, KGOLD}, {49, 38, KGOLD}, {34, 45, KGOLD},
                          {34, 46, KSTEEL}, {50, 38, KSTEEL}, {46, 43, KROSE}, {52, 50, KROSE}, {15, 11, KRUST},
                          {1, 1, KMINT}});
    });
}
// cf-bb: the back buffer: cleared steel, a mint quad over (20,30)-(60,60); rust filled over (37,45)-(90,71); a gold
// quad over (80,64)-(110,90): mint at (25,35); rust at (37,45) (the rect's corner, where the quad was), (89,50) and
// (40,70); gold at (85,68) and (100,85); steel at (36,62), (90,50), (40,71) and (200,200) (colour channels:
// X8R8G8B8). An additional swap chain's back buffer (the device's, aliased): aqua filled over (100,80)-(153,111) of
// it, a rose quad over (140,100)-(170,130) of the device's: aqua at (100,80) (where the gold quad was), (152,90) and
// (120,110), rose at (145,105) and (165,125), steel at (99,95), (120,111) and (153,90).
static bool rowCfBb() {
    return until([&] {
        Com<IDirect3DSwapChain9> sc;
        Com<IDirect3DSurface9> chain;
        const RECT rect = {37, 45, 90, 71}, onChain = {100, 80, 153, 111};
        if (!scene([&] { return backToScreen() && clearTo(KSTEEL) && zquad(20, 30, 60, 60, 0.5f, KMINT); }) ||
            !colorFill(rtSurf, &rect, KRUST) || !scene([&] { return zquad(80, 64, 110, 90, 0.5f, KGOLD); }) ||
            !rtHas("the back buffer", rtSurf, 256, X8,
                   {{25, 35, KMINT}, {37, 45, KRUST}, {89, 50, KRUST}, {40, 70, KRUST}, {85, 68, KGOLD},
                    {100, 85, KGOLD}, {36, 62, KSTEEL}, {90, 50, KSTEEL}, {40, 71, KSTEEL}, {200, 200, KSTEEL}}))
            return false;
        return chainBackBuffer(nullptr, 0, sc, chain) && colorFill(chain.p, &onChain, KAQUA) &&
               scene([&] { return zquad(140, 100, 170, 130, 0.5f, KROSE); }) &&
               rtHas("the chain's fill", rtSurf, 256, X8,
                     {{100, 80, KAQUA}, {152, 90, KAQUA}, {120, 110, KAQUA}, {145, 105, KROSE}, {165, 125, KROSE},
                      {99, 95, KSTEEL}, {120, 111, KSTEEL}, {153, 90, KSTEEL}});
    });
}
// cf-unbound: Y, a target, bound: cleared steel, a mint quad over (4,4)-(20,20). X, a render-target texture, bound:
// cleared rust, a gold quad over (0,0)-(28,64); Y filled aqua whole while X's pass is open; a rose quad over
// (36,0)-(64,64) of X; Y bound, a lilac quad over (40,40)-(60,60). X reads gold at (16,32), rust at (32,32), rose at
// (48,32): nothing of the fill; Y reads aqua at (10,10) (where its quad was), (30,30) and (62,2), lilac at (50,50).
static bool rowCfUnbound() {
    return until([&] {
        Rt x;
        Com<IDirect3DSurface9> y;
        CK(CreateRenderTarget, 64, 64, A8, D3DMULTISAMPLE_NONE, 0, FALSE, &y.p, nullptr);
        return mkRt(x) && scene([&] {
                   return bindRt(0, y.p) && clearTo(KSTEEL) && zquad(4, 4, 20, 20, 0.5f, KMINT) && bindRt(0, x.s.p) &&
                          clearTo(KRUST) && zquad(0, 0, 28, 64, 0.5f, KGOLD) && colorFill(y.p, nullptr, KAQUA) &&
                          zquad(36, 0, 64, 64, 0.5f, KROSE) && bindRt(0, y.p) &&
                          zquad(40, 40, 60, 60, 0.5f, KLILAC) && backToScreen();
               }) &&
               rtHas("X", x.s.p, 64, A8, {{16, 32, KGOLD}, {32, 32, KRUST}, {48, 32, KROSE}}) &&
               rtHas("Y", y.p, 64, A8, {{10, 10, KAQUA}, {30, 30, KAQUA}, {62, 2, KAQUA}, {50, 50, KLILAC}});
    });
}
// cf-rtex: T, a 64x64 render-target texture of two levels. Its first use, aqua filled over (9,6)-(22,19) of level 0,
// read inside the rect only (what is around it is undefined): aqua at (9,6), (21,18) and (15,12). Level 0 written steel
// whole through a lock, rust filled over (27,30)-(44,41): a READONLY lock of level 0 reads rust at (27,30), (43,40) and
// (35,35), steel at (26,30), (27,29), (44,40), (27,41) and (8,8). Level 1 bound: a mint quad over (2,2)-(10,10), gold
// filled whole, bound again, a rose quad over (20,20)-(30,30): GetRenderTargetData of level 1 reads gold at (5,5)
// (where the quad was) and (14,14), rose at (25,25). C, a render-target cube of 16 texels: LockRect of its face 3 (-y)
// is D3DERR_INVALIDCALL in every lane (a default-pool cube that is not DYNAMIC does not lock, and a render-target cube
// is never DYNAMIC); face 3 bound, a lilac quad over (2,2)-(8,8), sky filled whole (a READONLY LockRect of the face is
// again D3DERR_INVALIDCALL), bound again, a plum quad over (10,10)-(14,14): GetRenderTargetData of the face reads sky
// at (4,4) and (12,3), plum at (12,12).
static bool rowCfRtex() {
    return until([&] {
        Com<IDirect3DTexture9> t;
        Com<IDirect3DCubeTexture9> c;
        Com<IDirect3DSurface9> l0, l1, face;
        const RECT first = {9, 6, 22, 19}, rect = {27, 30, 44, 41};
        if (!makeRt(t, 64, 2) || !levelOf(t.p, 0, l0) || !levelOf(t.p, 1, l1) || !colorFill(l0.p, &first, KAQUA) ||
            !rtHas("the first use", l0.p, 64, A8, {{9, 6, KAQUA}, {21, 18, KAQUA}, {15, 12, KAQUA}}) ||
            !underLevelLock(t.p, 0, 0, [&](BYTE *b, int p) { paint(b, p, 0, 0, 64, 64, KSTEEL); }) ||
            !colorFill(l0.p, &rect, KRUST) ||
            !lockedHas("level 0's READONLY lock",
                       [&](auto copy) { return underLevelLock(t.p, 0, D3DLOCK_READONLY, copy); },
                       {{27, 30, KRUST}, {43, 40, KRUST}, {35, 35, KRUST}, {26, 30, KSTEEL}, {27, 29, KSTEEL},
                        {44, 40, KSTEEL}, {27, 41, KSTEEL}, {8, 8, KSTEEL}}) ||
            !scene([&] { return target(t.p, 1) && zquad(2, 2, 10, 10, 0.5f, KMINT) && backToScreen(); }) ||
            !colorFill(l1.p, nullptr, KGOLD) ||
            !scene([&] { return target(t.p, 1) && zquad(20, 20, 30, 30, 0.5f, KROSE) && backToScreen(); }) ||
            !rtHas("level 1", l1.p, 32, A8, {{5, 5, KGOLD}, {14, 14, KGOLD}, {25, 25, KROSE}}))
            return false;
        CK(CreateCubeTexture, 16, 1, D3DUSAGE_RENDERTARGET, A8, D3DPOOL_DEFAULT, &c.p, nullptr);
        HRESULT hr = c.p->GetCubeMapSurface(D3DCUBEMAP_FACE_NEGATIVE_Y, 0, &face.p);
        if (FAILED(hr)) return fail("GetCubeMapSurface", hr);
        const auto noLock = [&](DWORD flags, const char *what) {
            D3DLOCKED_RECT lr = {};
            const HRESULT h = c.p->LockRect(D3DCUBEMAP_FACE_NEGATIVE_Y, 0, &lr, nullptr, flags);
            if (SUCCEEDED(h)) c.p->UnlockRect(D3DCUBEMAP_FACE_NEGATIVE_Y, 0);
            return answer(what, h, D3DERR_INVALIDCALL);
        };
        return noLock(0, "cube LockRect (face 3)") &&
               scene([&] { return bindRt(0, face.p) && zquad(2, 2, 8, 8, 0.5f, KLILAC) && backToScreen(); }) &&
               colorFill(face.p, nullptr, KSKY) && noLock(D3DLOCK_READONLY, "cube LockRect (face 3, READONLY)") &&
               scene([&] { return bindRt(0, face.p) && zquad(10, 10, 14, 14, 0.5f, KPLUM) && backToScreen(); }) &&
               rtHas("face 3", face.p, 16, A8, {{4, 4, KSKY}, {12, 3, KSKY}, {12, 12, KPLUM}});
    });
}
// cf-autogen: A, a 64x64 AUTOGENMIPMAP render-target texture (X8R8G8B8): bound, cleared rust; the back buffer bound
// (the chain regenerates), cleared steel, a 4x4 quad (100,100)-(104,104) samples A at its level 4 (POINT mip filter):
// rust at (102,102); aqua filled into A's level 0 whole; the same quad at (150,100)-(154,104): aqua at (152,102) (the
// fill regenerated the chain), rust still at (102,102), steel at (20,20). No SetRenderTarget names A after the fill.
static bool rowCfAutogen() {
    return until([&] {
        Com<IDirect3DTexture9> a;
        Com<IDirect3DSurface9> l0;
        if (!makeRt(a, 64, 0, X8, D3DUSAGE_RENDERTARGET | D3DUSAGE_AUTOGENMIPMAP) || !levelOf(a.p, 0, l0)) return false;
        CK(SetSamplerState, 0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
        return scene([&] {
                   return target(a.p) && clearTo(KRUST) && backToScreen() && clearTo(KSTEEL) &&
                          show(a.p, 100, 100, 104, 104);
               }) &&
               colorFill(l0.p, nullptr, KAQUA) && scene([&] { return show(a.p, 150, 100, 154, 104); }) &&
               rtHas("the back buffer", rtSurf, 256, X8, {{102, 102, KRUST}, {152, 102, KAQUA}, {20, 20, KSTEEL}});
    });
}
// cf-msaa: M, a 4-sample target, bound: cleared steel, a mint quad over (0,0)-(24,24); rust filled whole; a gold quad
// over (30,24)-(60,44); aqua filled over (9,33)-(46,54); a rose quad over (40,48)-(62,62); M resolved by StretchRect
// into a single-sample target (the core's resolve shape): rust at (12,12) (where the mint quad was) and (4,60), gold at
// (50,30) and (47,40), aqua at (35,38) (where the gold quad was), (9,33), (45,40) and (20,53), rust at (8,40) and
// (20,54), rose at (43,51) and (55,58). A probe sits 2 pixels or more inside a quad (an edge through a pixel's centre
// covers half its samples).
static bool rowCfMsaa() {
    return until([&] {
        Com<IDirect3DSurface9> m, s;
        const RECT rect = {9, 33, 46, 54};
        CK(CreateRenderTarget, 64, 64, A8, D3DMULTISAMPLE_NONE, 0, FALSE, &s.p, nullptr);
        if (!msRt(m) || !scene([&] { return bindRt(0, m.p) && clearTo(KSTEEL) && zquad(0, 0, 24, 24, 0.5f, KMINT); }) ||
            !colorFill(m.p, nullptr, KRUST) || !scene([&] { return zquad(30, 24, 60, 44, 0.5f, KGOLD); }) ||
            !colorFill(m.p, &rect, KAQUA) ||
            !scene([&] { return zquad(40, 48, 62, 62, 0.5f, KROSE) && backToScreen(); }))
            return false;
        CK(StretchRect, m.p, nullptr, s.p, nullptr, D3DTEXF_NONE);
        return rtHas("resolved", s.p, 64, A8,
                     {{12, 12, KRUST}, {4, 60, KRUST}, {50, 30, KGOLD}, {47, 40, KGOLD}, {35, 38, KAQUA},
                      {9, 33, KAQUA}, {45, 40, KAQUA}, {20, 53, KAQUA}, {8, 40, KRUST}, {20, 54, KRUST},
                      {43, 51, KROSE}, {55, 58, KROSE}});
    });
}
// One cf-plain surface: P, a 32x32 default-pool plain surface of format f (bpp bytes a texel), filled steel whole,
// rust over (4,4)-(8,8), aqua over (28,10)-(40,14) (past the right edge: clamped to (28,10)-(32,14)) and mint over the
// inverted (20,20)-(10,10) (D3D_OK, nothing filled): a READONLY lock reads the texels' bytes exactly, rust at (4,4) and
// (7,7), aqua at (28,10) and (31,13), steel at (3,4), (8,8), (27,10), (31,14) and (15,15); an R5G6B5 texel is its
// colour's top bits (packed565).
static bool plainFills(D3DFORMAT f, int bpp) {
    Com<IDirect3DSurface9> p;
    CK(CreateOffscreenPlainSurface, 32, 32, f, D3DPOOL_DEFAULT, &p.p, nullptr);
    const RECT block = {4, 4, 8, 8}, over = {28, 10, 40, 14}, inverted = {20, 20, 10, 10};
    if (!colorFill(p.p, nullptr, KSTEEL) || !colorFill(p.p, &block, KRUST) || !colorFill(p.p, &over, KAQUA) ||
        !answer("ColorFill (an inverted rect)", dev->ColorFill(p.p, &inverted, KMINT), D3D_OK))
        return false;
    const Tx tx[] = {{4, 4, KRUST},   {7, 7, KRUST},   {28, 10, KAQUA},  {31, 13, KAQUA},  {3, 4, KSTEEL},
                     {8, 8, KSTEEL},  {27, 10, KSTEEL}, {31, 14, KSTEEL}, {15, 15, KSTEEL}};
    DWORD got[9] = {};
    if (!underLock(p.p, "LockRect (READONLY)", nullptr, D3DLOCK_READONLY, [&](BYTE *b, int pitch) {
            for (int i = 0; i < 9; i++) memcpy(&got[i], b + tx[i].y * pitch + tx[i].x * bpp, bpp);
        }))
        return false;
    for (int i = 0; i < 9; i++) {
        const DWORD w = bpp == 2 ? (DWORD)packed565(tx[i].want) : tx[i].want;
        if (got[i] != w)
            return snprintf(why, sizeof why, "(%d,%d) %08lx want %08lx", tx[i].x, tx[i].y, (unsigned long)got[i],
                            (unsigned long)w),
                   false;
    }
    return true;
}
static bool rowCfPlain() {
    if (!plainFills(A8, 4)) return failIn("A8R8G8B8");
    return plainFills(D3DFMT_R5G6B5, 2) || failIn("R5G6B5");
}
// cf-reject: ColorFill is D3DERR_INVALIDCALL of no surface, a system-memory plain surface, a D24S8 depth-stencil
// surface and level 0 of a default-pool texture of usage 0, of a DYNAMIC one and of a MANAGED one, and of a 32x32
// default-pool DXT1 plain surface with the rect (5,5)-(7,7) (off its block grid); D3D_OK on it with (4,4)-(8,8), and
// whole on a 32x32 default-pool YUY2 and UYVY plain surface (no fill encoding: the surface is left as it is). A format
// CheckDeviceFormat does not offer as a plain surface is skipped ("d3d9 cf-reject-<dxt1|yuy2|uyvy> skip").
static bool rowCfReject() {
    Com<IDirect3DSurface9> sys, ds, l0, ld, lm;
    Com<IDirect3DTexture9> t0, td, tm;
    CK(CreateOffscreenPlainSurface, 32, 32, A8, D3DPOOL_SYSTEMMEM, &sys.p, nullptr);
    CK(CreateDepthStencilSurface, 32, 32, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &ds.p, nullptr);
    CK(CreateTexture, 32, 32, 1, 0, A8, D3DPOOL_DEFAULT, &t0.p, nullptr);
    CK(CreateTexture, 32, 32, 1, D3DUSAGE_DYNAMIC, A8, D3DPOOL_DEFAULT, &td.p, nullptr);
    CK(CreateTexture, 32, 32, 1, 0, A8, D3DPOOL_MANAGED, &tm.p, nullptr);
    if (!levelOf(t0.p, 0, l0) || !levelOf(td.p, 0, ld) || !levelOf(tm.p, 0, lm)) return false;
    const struct { IDirect3DSurface9 *s; const char *what; } refused[] = {
        {nullptr, "ColorFill (no surface)"},
        {sys.p, "ColorFill (a system-memory surface)"},
        {ds.p, "ColorFill (a depth-stencil surface)"},
        {l0.p, "ColorFill (a default-pool texture's level)"},
        {ld.p, "ColorFill (a DYNAMIC texture's level)"},
        {lm.p, "ColorFill (a MANAGED texture's level)"}};
    for (const auto &k : refused)
        if (!answer(k.what, dev->ColorFill(k.s, nullptr, KSTEEL), D3DERR_INVALIDCALL)) return false;
    if (offered(D3DRTYPE_SURFACE, D3DFMT_DXT1, "cf-reject-dxt1")) {
        Com<IDirect3DSurface9> dxt;
        CK(CreateOffscreenPlainSurface, 32, 32, D3DFMT_DXT1, D3DPOOL_DEFAULT, &dxt.p, nullptr);
        const RECT off = {5, 5, 7, 7}, grid = {4, 4, 8, 8};
        if (!answer("ColorFill (DXT1, (5,5)-(7,7))", dev->ColorFill(dxt.p, &off, KSTEEL), D3DERR_INVALIDCALL) ||
            !answer("ColorFill (DXT1, (4,4)-(8,8))", dev->ColorFill(dxt.p, &grid, KSTEEL), D3D_OK))
            return false;
    }
    const struct { D3DFORMAT f; const char *skip, *what; } yuv[] = {
        {D3DFMT_YUY2, "cf-reject-yuy2", "ColorFill (YUY2)"}, {D3DFMT_UYVY, "cf-reject-uyvy", "ColorFill (UYVY)"}};
    for (const auto &k : yuv) {
        if (!offered(D3DRTYPE_SURFACE, k.f, k.skip)) continue;
        Com<IDirect3DSurface9> s;
        CK(CreateOffscreenPlainSurface, 32, 32, k.f, D3DPOOL_DEFAULT, &s.p, nullptr);
        if (!answer(k.what, dev->ColorFill(s.p, nullptr, KSTEEL), D3D_OK)) return false;
    }
    return true;
}

// The region source: `s`, 16x16 bpp-byte texels (4, or 2: R5G6B5, each colour its packed565), written `out` but for
// REGION_RECT (1,2)-(14,13): `in`, its last row (y 12) `last`, so a copy from the source's origin or of another row
// pitch moves a colour.
static const RECT REGION_RECT = {1, 2, 14, 13};
static bool paintSource(IDirect3DSurface9 *s, int bpp, DWORD out, DWORD in, DWORD last) {
    const auto v = [&](DWORD c) { return bpp == 2 ? (DWORD)packed565(c) : c; };
    return underLock(s, "LockRect (the source)", nullptr, 0, [&](BYTE *b, int p) {
        paint(b, p, 0, 0, 16, 16, v(out), bpp), paint(b, p, 1, 2, 14, 12, v(in), bpp);
        paint(b, p, 1, 12, 14, 13, v(last), bpp);
    });
}
static bool sysSurface16(Com<IDirect3DSurface9> &s, D3DFORMAT f) {
    CK(CreateOffscreenPlainSurface, 16, 16, f, D3DPOOL_SYSTEMMEM, &s.p, nullptr);
    return true;
}
// A region cell's target (us-rtex, us-rt, us-bb, us-conv), dst, bound at index 0: cleared `clear`, a `before` quad over
// (0,0)-(10,8); the source's REGION_RECT (13x11) updated to the point (5,3) (x 5..17, y 3..13); bound again, an `after`
// quad over (12,9)-(24,20). dst then holds before at (2,2); the region at (6,4) (over the before quad) and (5,3), its
// right edge at (17,8), its last row (the source's `last`) at (8,13); clear at (18,8), (8,14), (4,10) and (40,40);
// after at (16,12) and (20,17). A region written at (0,0) instead (x 0..12, y 0..10) shows at (2,2), (17,8) and (8,13).
struct Region { DWORD clear, before, in, last, after; };
static bool regionWrite(IDirect3DSurface9 *src, IDirect3DSurface9 *dst, const Region &k) {
    const POINT at = {5, 3};
    return scene([&] {
               return bindRt(0, dst) && clearTo(k.clear) && zquad(0, 0, 10, 8, 0.5f, k.before) && backToScreen();
           }) &&
           updateSurface(src, &REGION_RECT, dst, &at) &&
           scene([&] { return bindRt(0, dst) && zquad(12, 9, 24, 20, 0.5f, k.after) && backToScreen(); });
}
// read(px) of a region cell's texels.
template <class R> static bool regionHas(R read, const Region &k) {
    return read({{2, 2, k.before}, {6, 4, k.in}, {5, 3, k.in}, {17, 8, k.in}, {8, 13, k.last}, {18, 8, k.clear},
                 {8, 14, k.clear}, {4, 10, k.clear}, {16, 12, k.after}, {20, 17, k.after}, {40, 40, k.clear}});
}
// D, a 16x16 default-pool texture of usage 0 (format df), from S, a 16x16 system-memory plain surface (format sf, bpp
// bytes a texel), branch A: S written `first` and updated whole into D's level 0; D drawn by show() over
// (64,64)-(192,192) on a `bg` Clear (texel (x,y) at pixel (68 + 8x, 68 + 8y)): `first` at texels (3,1) and (15,11). S
// written the region source (`out`, `in`, `last`), its REGION_RECT updated to the point (3,1) (x 3..15, y 1..11): D
// drawn again (a second upload, of the region) reads `in` at texels (3,1), (15,10) and (9,6), `last` at (3,11) and
// (15,11), `first` at (2,1), (3,0), (15,12) and (0,15).
static bool texRegion(D3DFORMAT sf, int bpp, D3DFORMAT df, DWORD bg, DWORD first, DWORD out, DWORD in, DWORD last) {
    return until([&] {
        Com<IDirect3DSurface9> s, l;
        Com<IDirect3DTexture9> d;
        const POINT at = {3, 1};
        const auto shown = [&](const char *when, std::initializer_list<Tx> px) {
            return scene([&] { return backToScreen() && clearTo(bg) && show(d.p); }) &&
                   rtHas(when, rtSurf, 256, X8, px);
        };
        CK(CreateTexture, 16, 16, 1, 0, df, D3DPOOL_DEFAULT, &d.p, nullptr);
        return sysSurface16(s, sf) && levelOf(d.p, 0, l) && paintSource(s.p, bpp, first, first, first) &&
               updateSurface(s.p, nullptr, l.p, nullptr) && shown("whole", {{92, 76, first}, {188, 156, first}}) &&
               paintSource(s.p, bpp, out, in, last) && updateSurface(s.p, &REGION_RECT, l.p, &at) &&
               shown("the region", {{92, 76, in}, {188, 148, in}, {140, 116, in}, {92, 156, last}, {188, 156, last},
                                    {84, 76, first}, {92, 68, first}, {188, 164, first}, {68, 188, first}});
    });
}
// us-tex: texRegion of X8R8G8B8 into X8R8G8B8: first steel, the region rust, its last row gold, mint around it, on
// lilac.
static bool rowUsTex() { return texRegion(X8, 4, X8, KLILAC, KSTEEL, KMINT, KRUST, KGOLD); }
// us-level: from a 16x16 system-memory texture of two levels and an 8-texel system-memory cube, branch C: level 0
// (steel) and level 1 (rust) updated whole into a default-pool texture's, face 2 (+y, aqua) into a default-pool
// cube's; each drawn on lilac (level 0 over (0,0)-(64,64), texel (x,y) at pixel (4x + 2, 4y + 2); level 1 under
// MAXMIPLEVEL 1 over (64,0)-(128,64), at (68 + 8x, 4 + 8y); face 2 sampled through ps_2_0 by four quads at y 80..96
// along (x, 1, z), texel (3,3) at (8,88), (4,4) at (28,88), (0,0) at (48,88), (6,6) at (68,88)): its colour. The
// sources written mint, gold and rose, then updated by a rect to a point: level 0's (2,1)-(9,6) to (6,8) (x 6..12, y
// 8..12), level 1's (1,2)-(6,5) to (2,4) (x 2..6, y 4..6), face 2's (1,2)-(4,5) to (2,3) (x 2..4, y 3..5); drawn again
// (each a second, partial upload): level 0 mint at texels (6,8) and (12,12), steel at (5,8), (13,12), (6,13) and
// (0,0); level 1 gold at (2,4) and (6,6), rust at (1,4), (7,6), (2,7) and (2,3); face 2 rose at (3,3) and (4,4), aqua
// at (0,0) and (6,6) (each face texel's mirror images in x and y are on the same side of the region, whichever way the
// face is oriented).
static bool rowUsLevel() {
    return until([&] {
        Com<IDirect3DTexture9> st, dt;
        Com<IDirect3DCubeTexture9> sc, dc;
        Com<IDirect3DSurface9> s0, s1, d0, d1, sf, df;
        CK(CreateTexture, 16, 16, 2, 0, X8, D3DPOOL_SYSTEMMEM, &st.p, nullptr);
        CK(CreateTexture, 16, 16, 2, 0, X8, D3DPOOL_DEFAULT, &dt.p, nullptr);
        CK(CreateCubeTexture, 8, 1, 0, X8, D3DPOOL_SYSTEMMEM, &sc.p, nullptr);
        CK(CreateCubeTexture, 8, 1, 0, X8, D3DPOOL_DEFAULT, &dc.p, nullptr);
        if (!levelOf(st.p, 0, s0) || !levelOf(st.p, 1, s1) || !levelOf(dt.p, 0, d0) || !levelOf(dt.p, 1, d1))
            return false;
        HRESULT hr = sc.p->GetCubeMapSurface(D3DCUBEMAP_FACE_POSITIVE_Y, 0, &sf.p);
        if (SUCCEEDED(hr)) hr = dc.p->GetCubeMapSurface(D3DCUBEMAP_FACE_POSITIVE_Y, 0, &df.p);
        if (FAILED(hr)) return fail("GetCubeMapSurface", hr);
        const auto write = [&](DWORD c0, DWORD c1, DWORD cf) {
            return underLock(s0.p, "LockRect (level 0)", nullptr, 0,
                             [&](BYTE *b, int p) { paint(b, p, 0, 0, 16, 16, c0); }) &&
                   underLock(s1.p, "LockRect (level 1)", nullptr, 0,
                             [&](BYTE *b, int p) { paint(b, p, 0, 0, 8, 8, c1); }) &&
                   underLock(sf.p, "LockRect (face 2)", nullptr, 0,
                             [&](BYTE *b, int p) { paint(b, p, 0, 0, 8, 8, cf); });
        };
        const float dirs[4][2] = {{-0.125f, -0.125f}, {0.125f, 0.125f}, {-0.875f, -0.875f}, {0.625f, 0.625f}};
        const auto shown = [&](const char *when, std::initializer_list<Tx> px) {
            Com<IDirect3DPixelShader9> ps;
            return scene([&] {
                       if (!backToScreen() || !clearTo(KLILAC)) return false;
                       CK(SetSamplerState, 0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
                       if (!show(dt.p, 0, 0, 64, 64)) return false;
                       CK(SetSamplerState, 0, D3DSAMP_MAXMIPLEVEL, 1);
                       const bool one = show(dt.p, 64, 0, 128, 64);
                       CK(SetSamplerState, 0, D3DSAMP_MAXMIPLEVEL, 0);
                       if (!one || !usePs(ps, KCUBE, 0, false) || !pointClamp(0)) return false;
                       CK(SetTexture, 0, dc.p);
                       bool ok = true;
                       for (int k = 0; k < 4 && ok; k++) {
                           TV3 q[4];
                           tquad(q, 20.0f * k, 80, 20.0f * k + 16, 96, dirs[k][0], 1, dirs[k][1]);
                           ok = tdraw(q);
                       }
                       CK(SetTexture, 0, nullptr);
                       CK(SetPixelShader, nullptr);
                       return ok;
                   }) &&
                   rtHas(when, rtSurf, 256, X8, px);
        };
        const RECT r0 = {2, 1, 9, 6}, r1 = {1, 2, 6, 5}, rf = {1, 2, 4, 5};
        const POINT p0 = {6, 8}, p1 = {2, 4}, pf = {2, 3};
        return write(KSTEEL, KRUST, KAQUA) && updateSurface(s0.p, nullptr, d0.p, nullptr) &&
               updateSurface(s1.p, nullptr, d1.p, nullptr) && updateSurface(sf.p, nullptr, df.p, nullptr) &&
               shown("whole", {{26, 34, KSTEEL}, {2, 2, KSTEEL}, {84, 36, KRUST}, {124, 52, KRUST}, {8, 88, KAQUA},
                               {68, 88, KAQUA}}) &&
               write(KMINT, KGOLD, KROSE) && updateSurface(s0.p, &r0, d0.p, &p0) &&
               updateSurface(s1.p, &r1, d1.p, &p1) && updateSurface(sf.p, &rf, df.p, &pf) &&
               shown("the regions", {{26, 34, KMINT}, {50, 50, KMINT}, {22, 34, KSTEEL}, {54, 50, KSTEEL},
                                     {26, 54, KSTEEL}, {2, 2, KSTEEL}, {84, 36, KGOLD}, {116, 52, KGOLD},
                                     {76, 36, KRUST}, {124, 52, KRUST}, {84, 60, KRUST}, {84, 28, KRUST},
                                     {8, 88, KROSE}, {28, 88, KROSE}, {48, 88, KAQUA}, {68, 88, KAQUA}});
    });
}
// us-rtex: a region cell (branch A) on a 64x64 render-target texture's level 0, read by GetRenderTargetData: steel,
// rose before, the region aqua (its last row gold), lilac after, from an A8R8G8B8 source mint around the region.
static bool rowUsRtex() {
    const Region k = {KSTEEL, KROSE, KAQUA, KGOLD, KLILAC};
    return until([&] {
        Rt t;
        Com<IDirect3DSurface9> s;
        return mkRt(t) && sysSurface16(s, A8) && paintSource(s.p, 4, KMINT, KAQUA, KGOLD) &&
               regionWrite(s.p, t.s.p, k) &&
               regionHas([&](std::initializer_list<Tx> px) { return rtHas("the level", t.s.p, 64, A8, px); }, k);
    });
}
// us-rt: the same region cell (branch B) on a target, read by GetRenderTargetData, and on a lockable target, read by a
// READONLY lock.
static bool rowUsRt() {
    const Region k = {KSTEEL, KROSE, KAQUA, KGOLD, KLILAC};
    return until([&] {
        Com<IDirect3DSurface9> s, n, l;
        CK(CreateRenderTarget, 64, 64, A8, D3DMULTISAMPLE_NONE, 0, FALSE, &n.p, nullptr);
        const auto locked = [&](auto copy) {
            return underLock(l.p, "LockRect (READONLY)", nullptr, D3DLOCK_READONLY, copy);
        };
        return target64(l) && sysSurface16(s, A8) && paintSource(s.p, 4, KMINT, KAQUA, KGOLD) &&
               regionWrite(s.p, n.p, k) &&
               regionHas([&](std::initializer_list<Tx> px) { return rtHas("a target", n.p, 64, A8, px); }, k) &&
               regionWrite(s.p, l.p, k) &&
               regionHas([&](std::initializer_list<Tx> px) { return lockedHas("a lockable target", locked, px); }, k);
    });
}
// us-bb: the same region cell (branch B) on the back buffer, from an X8R8G8B8 source, read by GetRenderTargetData
// (colour channels).
static bool rowUsBb() {
    const Region k = {KSTEEL, KROSE, KAQUA, KGOLD, KLILAC};
    return until([&] {
        Com<IDirect3DSurface9> s;
        return sysSurface16(s, X8) && paintSource(s.p, 4, KMINT, KAQUA, KGOLD) && regionWrite(s.p, rtSurf, k) &&
               regionHas([&](std::initializer_list<Tx> px) {
                   return rtHas("the back buffer", rtSurf, 256, X8, px);
               }, k);
    });
}
// us-conv: from R5G6B5 system-memory sources, converted by the update codec (5- and 6-bit fields widened by bit
// replication, k565): the region cell (branch B) on an X8R8G8B8 target (steel, rose before, the region K565A, its last
// row K565C, lilac after, K565B around the region in the source), and texRegion (branch A) into an A8R8G8B8 texture
// (first K565D, the region K565A, its last row K565C, K565B around it, on mint).
static bool rowUsConv() {
    const Region k = {KSTEEL, KROSE, K565A, K565C, KLILAC};
    if (!until([&] {
            Com<IDirect3DSurface9> s, r;
            CK(CreateRenderTarget, 64, 64, X8, D3DMULTISAMPLE_NONE, 0, FALSE, &r.p, nullptr);
            return sysSurface16(s, D3DFMT_R5G6B5) && paintSource(s.p, 2, K565B, K565A, K565C) &&
                   regionWrite(s.p, r.p, k) &&
                   regionHas([&](std::initializer_list<Tx> px) { return rtHas("the target", r.p, 64, X8, px); }, k);
        }))
        return failIn("into an X8R8G8B8 target");
    return texRegion(D3DFMT_R5G6B5, 2, A8, KMINT, K565D, K565B, K565A, K565C) || failIn("into an A8R8G8B8 texture");
}
// us-reject: UpdateSurface is D3DERR_INVALIDCALL from no source and into no destination; from a locked source; into a
// 4-sample target; from a default-pool texture's level; into a MANAGED texture's level; of a 40x40 rect into a 32x32
// level; between two 4x4 DXT1 textures (system memory into default pool) of the rect (0,0)-(2,2), off the block grid
// and short of the level's edge (skipped as "d3d9 us-reject-dxt1 skip" where CheckDeviceFormat offers no DXT1 texture);
// from a texture's level 1 into its level 0. The source is a 64x64 X8R8G8B8 system-memory plain surface, a destination
// a 64x64 default-pool X8R8G8B8 texture's level, unless said.
static bool rowUsReject() {
    Com<IDirect3DSurface9> sys, ms, d0, e0, m0, small0, two0, two1;
    Com<IDirect3DTexture9> d, e, m, small, two;
    CK(CreateOffscreenPlainSurface, 64, 64, X8, D3DPOOL_SYSTEMMEM, &sys.p, nullptr);
    CK(CreateTexture, 64, 64, 1, 0, X8, D3DPOOL_DEFAULT, &d.p, nullptr);
    CK(CreateTexture, 64, 64, 1, 0, X8, D3DPOOL_DEFAULT, &e.p, nullptr);
    CK(CreateTexture, 64, 64, 1, 0, X8, D3DPOOL_MANAGED, &m.p, nullptr);
    CK(CreateTexture, 32, 32, 1, 0, X8, D3DPOOL_DEFAULT, &small.p, nullptr);
    CK(CreateTexture, 64, 64, 2, 0, X8, D3DPOOL_DEFAULT, &two.p, nullptr);
    if (!msRt(ms, 64, X8) || !levelOf(d.p, 0, d0) || !levelOf(e.p, 0, e0) || !levelOf(m.p, 0, m0) ||
        !levelOf(small.p, 0, small0) || !levelOf(two.p, 0, two0) || !levelOf(two.p, 1, two1))
        return false;
    const RECT big = {0, 0, 40, 40};
    HRESULT locked = E_FAIL;
    if (!underLock(sys.p, "LockRect (READONLY)", nullptr, D3DLOCK_READONLY,
                   [&](BYTE *, int) { locked = dev->UpdateSurface(sys.p, nullptr, d0.p, nullptr); }) ||
        !answer("UpdateSurface (a locked source)", locked, D3DERR_INVALIDCALL))
        return false;
    const struct { const char *what; IDirect3DSurface9 *src; const RECT *r; IDirect3DSurface9 *dst; } refused[] = {
        {"UpdateSurface (no source)", nullptr, nullptr, d0.p},
        {"UpdateSurface (no destination)", sys.p, nullptr, nullptr},
        {"UpdateSurface (a 4-sample target)", sys.p, nullptr, ms.p},
        {"UpdateSurface (a default-pool texture's level as source)", d0.p, nullptr, e0.p},
        {"UpdateSurface (a MANAGED texture's level)", sys.p, nullptr, m0.p},
        {"UpdateSurface (a 40x40 rect into a 32x32 level)", sys.p, &big, small0.p},
        {"UpdateSurface (two levels of one texture)", two1.p, nullptr, two0.p}};
    for (const auto &k : refused)
        if (!answer(k.what, dev->UpdateSurface(k.src, k.r, k.dst, nullptr), D3DERR_INVALIDCALL)) return false;
    if (!offered(D3DRTYPE_TEXTURE, D3DFMT_DXT1, "us-reject-dxt1")) return true;
    Com<IDirect3DTexture9> ds, dd;
    Com<IDirect3DSurface9> ds0, dd0;
    CK(CreateTexture, 4, 4, 1, 0, D3DFMT_DXT1, D3DPOOL_SYSTEMMEM, &ds.p, nullptr);
    CK(CreateTexture, 4, 4, 1, 0, D3DFMT_DXT1, D3DPOOL_DEFAULT, &dd.p, nullptr);
    const RECT two2 = {0, 0, 2, 2};
    return levelOf(ds.p, 0, ds0) && levelOf(dd.p, 0, dd0) &&
           answer("UpdateSurface (DXT1, a 2x2 rect of a 4x4 level)", dev->UpdateSurface(ds0.p, &two2, dd0.p, nullptr),
                  D3DERR_INVALIDCALL);
}
// sr-reject: StretchRect is D3DERR_INVALIDCALL in every lane, of: a flipped source rect; a destination rect past the
// edge; a target into a plain surface; a render-target texture's level into a level of a default-pool texture of
// usage 0; that level into a plain surface; a system-memory plain surface as the source and as the destination; a
// MANAGED texture's level as the source; a D24S8 depth-stencil surface into a target and a target into one; a D24S8
// depth texture's level into a target (the format gate; DXMTRS C4c-4b); a depth-stencil pair of two sizes (64 into
// 32); a depth-stencil pair of one size with the sub-rect (0,0)-(32,32); a single-sample depth-stencil into a 4-sample
// one; a plain surface's (0,0)-(32,32) into another's (0,0)-(16,16); a target's (0,0)-(32,32) onto its own
// (32,32)-(48,48). Surfaces are 64x64 A8R8G8B8 unless said.
static bool rowSrReject() {
    Com<IDirect3DSurface9> a, b, p1, p2, sys, d1, d2, d32, d4, rl, ul, ml, dl;
    Com<IDirect3DTexture9> rt, ut, mt, dt;
    CK(CreateRenderTarget, 64, 64, A8, D3DMULTISAMPLE_NONE, 0, FALSE, &a.p, nullptr);
    CK(CreateRenderTarget, 64, 64, A8, D3DMULTISAMPLE_NONE, 0, FALSE, &b.p, nullptr);
    CK(CreateOffscreenPlainSurface, 64, 64, A8, D3DPOOL_DEFAULT, &p1.p, nullptr);
    CK(CreateOffscreenPlainSurface, 64, 64, A8, D3DPOOL_DEFAULT, &p2.p, nullptr);
    CK(CreateOffscreenPlainSurface, 64, 64, A8, D3DPOOL_SYSTEMMEM, &sys.p, nullptr);
    CK(CreateDepthStencilSurface, 64, 64, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &d1.p, nullptr);
    CK(CreateDepthStencilSurface, 64, 64, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &d2.p, nullptr);
    CK(CreateDepthStencilSurface, 32, 32, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &d32.p, nullptr);
    CK(CreateDepthStencilSurface, 64, 64, D3DFMT_D24S8, D3DMULTISAMPLE_4_SAMPLES, 0, FALSE, &d4.p, nullptr);
    CK(CreateTexture, 64, 64, 1, 0, A8, D3DPOOL_DEFAULT, &ut.p, nullptr);
    CK(CreateTexture, 64, 64, 1, 0, A8, D3DPOOL_MANAGED, &mt.p, nullptr);
    CK(CreateTexture, 64, 64, 1, D3DUSAGE_DEPTHSTENCIL, D3DFMT_D24S8, D3DPOOL_DEFAULT, &dt.p, nullptr);
    if (!makeRt(rt) || !levelOf(rt.p, 0, rl) || !levelOf(ut.p, 0, ul) || !levelOf(mt.p, 0, ml) ||
        !levelOf(dt.p, 0, dl))
        return false;
    const RECT flipped = {20, 20, 10, 10}, ten = {10, 10, 20, 20}, quarter = {0, 0, 16, 16}, past = {56, 56, 72, 72},
               half = {0, 0, 32, 32}, onto = {32, 32, 48, 48};
    const D3DTEXTUREFILTERTYPE none = D3DTEXF_NONE;
    const Stretch cells[] = {
        {"StretchRect (a flipped source rect)", a.p, &flipped, b.p, &ten, none},
        {"StretchRect (a destination rect past the edge)", a.p, &quarter, b.p, &past, none},
        {"StretchRect (a target into a plain surface)", a.p, nullptr, p1.p, nullptr, none},
        {"StretchRect (a render-target texture's level into a usage-0 level)", rl.p, nullptr, ul.p, nullptr, none},
        {"StretchRect (a usage-0 level into a plain surface)", ul.p, nullptr, p1.p, nullptr, none},
        {"StretchRect (a system-memory source)", sys.p, nullptr, a.p, nullptr, none},
        {"StretchRect (a system-memory destination)", a.p, nullptr, sys.p, nullptr, none},
        {"StretchRect (a MANAGED level as the source)", ml.p, nullptr, a.p, nullptr, none},
        {"StretchRect (a depth-stencil surface into a target)", d1.p, nullptr, a.p, nullptr, none},
        {"StretchRect (a target into a depth-stencil surface)", a.p, nullptr, d1.p, nullptr, none},
        {"StretchRect (a depth texture's level into a target)", dl.p, nullptr, a.p, nullptr, none},
        {"StretchRect (a depth-stencil pair of two sizes)", d1.p, nullptr, d32.p, nullptr, none},
        {"StretchRect (a depth-stencil pair with a sub-rect)", d1.p, &half, d2.p, &half, none},
        {"StretchRect (a single-sample depth-stencil into a 4-sample one)", d1.p, nullptr, d4.p, nullptr, none},
        {"StretchRect (a plain surface into another, scaled)", p1.p, &half, p2.p, &quarter, none},
        {"StretchRect (a target onto itself, scaled)", a.p, &half, a.p, &onto, none}};
    for (const Stretch &k : cells)
        if (!answer(k.what, dev->StretchRect(k.src, k.srcRect, k.dst, k.dstRect, k.filter), D3DERR_INVALIDCALL))
            return false;
    return true;
}
// The sd-resolve row (DXMTRS C4c-4d): StretchRect of a 64x64 depth-stencil surface of 4 samples into a single-sample
// one of its format, whole, between scenes (D3D9 refuses a depth StretchRect inside one), with no draw between: D3D_OK
// in every lane, the destination's depth and stencil judged by draws (before DXMTRS C4c-4d the core refused it by name;
// the shapes that refusal held before DXMTRS C4c-4b and C4c-4c are the sc-* and sq-* rows'). A Clear or a draw reaches
// a depth-stencil only beside a target of its sample count (ms-mismatch): "M" is a 64x64 A8R8G8B8 target of 4 samples,
// bound with each 4-sample surface, "T" a single-sample one, bound with each single-sample surface: a pair is bound
// depth-stencil first, then target, and cleared or drawn to only once both are.
// One sd-resolve pair: M and T cleared steel; S, of format f and 4 samples, bound with M and cleared to Z 0.25 (and
// stencil 3); D, single-sample, bound with T and cleared to Z 1 (and stencil 0); S into D. Z on, T and D bound: a mint
// quad at z 0.1 over (4,4)-(19,28) (under stencil EQUAL 3) draws first, then a rust one at 0.5 over (24,4)-(39,28) (no
// stencil test) and, with a stencil, a gold one at 0.1 under EQUAL 0 over (44,4)-(59,28) do not (as depthPair's); then
// in that scene, before any read-back, M and S bound: an aqua quad at 0.1 over (0,32)-(28,64) draws and, with a
// stencil, a rose one at 0.5 over (36,32)-(64,64) does not (the resolve left S untouched). T: mint at (11,16), steel
// at (31,16) and (51,16); with a stencil, M through its resolve: aqua at (12,48), steel at (50,48), 8 texels or more
// from the quads' edges. Without one (the D16 pair) the aqua quad is not judged: it is there so that a Clear of S that
// is not ordered before the resolve's read folds into its pass, and D then holds what S held before that Clear (the
// mint quad does not draw).
static bool resolvePair(D3DFORMAT f, bool stencil) {
    Com<IDirect3DSurface9> t, m, s, d;
    const auto clear = [&](float z, DWORD v) { return stencil ? clearZS(z, v) : clearZ(z); };
    const auto test = [&](DWORD func, DWORD ref) {
        return !stencil || (rstate(D3DRS_STENCILENABLE, TRUE) && rstate(D3DRS_STENCILFUNC, func) &&
                            rstate(D3DRS_STENCILREF, ref));
    };
    if (!target64(t, FALSE) || !msRt(m) || !makeDs(s, f, 64, D3DMULTISAMPLE_4_SAMPLES) || !makeDs(d, f, 64) ||
        !scene([&] {
            return bindDs(s.p) && clearRt(m.p, KSTEEL) && clear(0.25f, 3) && bindDs(d.p) && clearRt(t.p, KSTEEL) &&
                   clear(1.0f, 0);
        }))
        return false;
    CK(StretchRect, s.p, nullptr, d.p, nullptr, D3DTEXF_NONE);
    return scene([&] {
               return rstate(D3DRS_ZENABLE, D3DZB_TRUE) && test(D3DCMP_EQUAL, 3) && zquad(4, 4, 19, 28, 0.1f, KMINT) &&
                      rstate(D3DRS_STENCILENABLE, FALSE) && zquad(24, 4, 39, 28, 0.5f, KRUST) &&
                      (!stencil || (test(D3DCMP_EQUAL, 0) && zquad(44, 4, 59, 28, 0.1f, KGOLD) &&
                                    rstate(D3DRS_STENCILENABLE, FALSE))) &&
                      bindDs(s.p) && bindRt(0, m.p) && zquad(0, 32, 28, 64, 0.1f, KAQUA) &&
                      (!stencil || zquad(36, 32, 64, 64, 0.5f, KROSE)) && backToScreen();
           }) &&
           rtHas("T", t.p, 64, A8, {{11, 16, KMINT}, {31, 16, KSTEEL}, {51, 16, KSTEEL}}) &&
           (!stencil || msHas("M", m.p, {{12, 48, KAQUA}, {50, 48, KSTEEL}}));
}
// The sd-resolve of a drawn source: M cleared steel with S, a D24S8 surface of 4 samples, cleared to Z 1; Z on and the
// stencil test off, a sky quad at z 0.3 over (0,0)-(48,16) (asymmetric: a flipped or transposed read shows); T cleared
// steel with D, single-sample, cleared to Z 1; S into D. T and D: a lilac quad at 0.5 over all draws outside that rect
// and not inside it: steel at (32,8), lilac at (56,8) and (32,40), each 8 texels or more from every quad's edge (an
// edge texel's samples are split between the quad and what lies beside it, and which of them is sample 0 depends on
// the sample pattern). The sky quad has no drawn control: one left out leaves (32,8) lilac, and until() retries.
static bool resolveDrawn() {
    Com<IDirect3DSurface9> t, m, s, d;
    if (!target64(t, FALSE) || !msRt(m) || !makeDs(s, D3DFMT_D24S8, 64, D3DMULTISAMPLE_4_SAMPLES) ||
        !makeDs(d, D3DFMT_D24S8, 64) || !scene([&] {
            return bindDs(s.p) && clearRt(m.p, KSTEEL) && clearZ(1.0f) && rstate(D3DRS_ZENABLE, D3DZB_TRUE) &&
                   rstate(D3DRS_STENCILENABLE, FALSE) && zquad(0, 0, 48, 16, 0.3f, KSKY) && bindDs(d.p) &&
                   clearRt(t.p, KSTEEL) && clearZ(1.0f);
        }))
        return false;
    CK(StretchRect, s.p, nullptr, d.p, nullptr, D3DTEXF_NONE);
    return scene([&] { return zquad(0, 0, 64, 64, 0.5f, KLILAC) && backToScreen(); }) &&
           rtHas("T", t.p, 64, A8, {{32, 8, KSTEEL}, {56, 8, KLILAC}, {32, 40, KLILAC}});
}
// sd-resolve: resolvePair of D24S8 with its stencil plane, resolveDrawn, then resolvePair of D16, depth alone. Each
// texel a cell judges has one value in all its samples, so no cell tells sample 0 (what mtld3d's resolve keeps) from
// another reduction. On every path, passed or not, no depth-stencil bound and the depth state as the device has it (it
// has none: ZENABLE off).
static bool rowSdResolve() {
    return until([&] {
        const bool ok = (resolvePair(D3DFMT_D24S8, true) || failIn("D24S8")) &&
                        (resolveDrawn() || failIn("a drawn source")) &&
                        (resolvePair(D3DFMT_D16, false) || failIn("D16"));
        depthBack(nullptr, D3DZB_FALSE);
        return ok;
    });
}

// The sc-* rows (DXMTRS C4c-4b): StretchRect's unscaled copies, D3D_OK in every lane, their texels judged. RECTs are
// right- and bottom-exclusive: (9,7)-(30,29) is 21x22 texels, and at (33,18) it covers x 33..53, y 18..39.
// StretchRect of src's rect r to dst at (x, y), unscaled, D3DTEXF_NONE (r null: the whole of src into the whole of
// dst); false, with `why` naming the rect, unless it succeeds.
static bool copyTo(IDirect3DSurface9 *src, const RECT *r, IDirect3DSurface9 *dst, int x = 0, int y = 0) {
    const RECT to = r ? RECT{x, y, x + r->right - r->left, y + r->bottom - r->top} : RECT{};
    const HRESULT hr = dev->StretchRect(src, r, dst, r ? &to : nullptr, D3DTEXF_NONE);
    if (SUCCEEDED(hr)) return true;
    if (!r) return fail("StretchRect (whole)", hr);
    snprintf(why, sizeof why, "StretchRect (%ld,%ld)-(%ld,%ld) to (%d,%d) %08lx", (long)r->left, (long)r->top,
             (long)r->right, (long)r->bottom, x, y, (unsigned long)hr);
    return false;
}
// The size x size surface `s` of bpp-byte texels written under a lock: `out` whole, then `in` over r (none if empty).
static bool twoTone(IDirect3DSurface9 *s, int size, DWORD out, const RECT &r, DWORD in, int bpp = 4) {
    return underLock(s, "LockRect (write)", nullptr, 0, [&](BYTE *b, int p) {
        paint(b, p, 0, 0, size, size, out, bpp), paint(b, p, r.left, r.top, r.right, r.bottom, in, bpp);
    });
}
// `t` bound, cleared `out`, an `in` quad over r, and the back buffer bound again: one scene.
static bool drawnTwo(IDirect3DSurface9 *t, DWORD out, const RECT &r, DWORD in) {
    return scene([&] {
        return clearRt(t, out) && zquad((float)r.left, (float)r.top, (float)r.right, (float)r.bottom, 0.5f, in) &&
               backToScreen();
    });
}
// `t` bound and cleared to c, and the back buffer bound again: one scene.
static bool cleared(IDirect3DSurface9 *t, DWORD c) { return scene([&] { return clearRt(t, c) && backToScreen(); }); }
// A 64x64 default-pool plain surface of format f.
static bool plain64(Com<IDirect3DSurface9> &s, D3DFORMAT f = A8) {
    CK(CreateOffscreenPlainSurface, 64, 64, f, D3DPOOL_DEFAULT, &s.p, nullptr);
    return true;
}
// Face f of the cube `c`.
static bool faceOf(IDirect3DCubeTexture9 *c, D3DCUBEMAP_FACES f, Com<IDirect3DSurface9> &s) {
    const HRESULT hr = c->GetCubeMapSurface(f, 0, &s.p);
    return SUCCEEDED(hr) || fail("GetCubeMapSurface", hr);
}
// lockedHas of the 64x64 4-byte surface `s` through a READONLY LockRect.
static bool lockHas(const char *name, IDirect3DSurface9 *s, std::initializer_list<Tx> px) {
    return lockedHas(
        name, [&](auto copy) { return underLock(s, "LockRect (READONLY)", nullptr, D3DLOCK_READONLY, copy); }, px);
}
// The R5G6B5 texels px (8 at most) of `s`, read 2 bytes each under a READONLY lock (as plainFills reads them) and
// judged after the unlock: each exactly its colour's packed565 (the codec truncates; a k565 colour survives rounding
// too).
static bool has565(const char *name, IDirect3DSurface9 *s, std::initializer_list<Tx> px) {
    WORD got[8] = {};
    if (px.size() > 8) return snprintf(why, sizeof why, "%s: more than 8 texels", name), false;
    if (!underLock(s, "LockRect (READONLY)", nullptr, D3DLOCK_READONLY, [&](BYTE *b, int pitch) {
            int i = 0;
            for (const Tx &t : px) memcpy(&got[i++], b + t.y * pitch + t.x * 2, 2);
        }))
        return false;
    int i = 0;
    for (const Tx &t : px) {
        const WORD w = packed565(t.want);
        if (got[i++] != w)
            return snprintf(why, sizeof why, "%s (%d,%d) %04x want %04x", name, t.x, t.y, got[i - 1], w), false;
    }
    return true;
}

// sc-rt: A, a target, cleared steel, a rust quad over (7,5)-(32,31); B, a target, cleared aqua. A into B whole: steel
// at (2,2) and (40,40), rust at (20,18). B cleared aqua again, A's (9,7)-(30,29) (rust) to (33,18): rust at (33,18)
// (A's (9,7); A's origin is steel), (53,39) and (43,28); aqua at (54,18) (a copy a column wider writes rust there),
// (33,17), (32,18), (33,40) and (0,0).
static bool rowScRt() {
    return until([&] {
        Com<IDirect3DSurface9> a, b;
        const RECT rect = {9, 7, 30, 29};
        return target64(a, FALSE) && target64(b, FALSE) && drawnTwo(a.p, KSTEEL, {7, 5, 32, 31}, KRUST) &&
               cleared(b.p, KAQUA) && copyTo(a.p, nullptr, b.p) &&
               rtHas("whole", b.p, 64, A8, {{2, 2, KSTEEL}, {40, 40, KSTEEL}, {20, 18, KRUST}}) &&
               cleared(b.p, KAQUA) && copyTo(a.p, &rect, b.p, 33, 18) &&
               rtHas("the rect", b.p, 64, A8,
                     {{33, 18, KRUST}, {53, 39, KRUST}, {43, 28, KRUST}, {54, 18, KAQUA}, {33, 17, KAQUA},
                      {32, 18, KAQUA}, {33, 40, KAQUA}, {0, 0, KAQUA}});
    });
}
// sc-bb: A, a target, cleared sage, a gold quad over (1,2)-(63,63); X, an X8R8G8B8 target, cleared plum; the back
// buffer cleared lilac. A's (5,9)-(38,40) to the back buffer's (101,61); the back buffer's (101,61)-(134,92) to X's
// (7,11); A's (3,6)-(36,40) to an additional swap chain's back buffer's (150,90) (read from the device's, which it
// aliases): the back buffer gold at (101,61), (133,91), (150,90) and (182,123), lilac at (134,61), (101,60), (100,61),
// (101,92), (183,90), (150,89), (149,90), (150,124) and (0,0); X gold at (7,11) and (39,41), plum at (40,11), (7,10),
// (6,11), (7,42) and (0,0). Then A's (0,0)-(64,64) to the back buffer's (0,0)-(64,64) (test_pointsize's copy, at the
// origin): sage at (0,0), gold at (32,32), lilac at (64,32) and (32,64). Colour channels (X8R8G8B8).
static bool rowScBb() {
    return until([&] {
        Com<IDirect3DSwapChain9> sc;
        Com<IDirect3DSurface9> a, x, chain;
        const RECT toBb = {5, 9, 38, 40}, fromBb = {101, 61, 134, 92}, toChain = {3, 6, 36, 40}, r64 = {0, 0, 64, 64};
        CK(CreateRenderTarget, 64, 64, X8, D3DMULTISAMPLE_NONE, 0, FALSE, &x.p, nullptr);
        if (!target64(a, FALSE) || !chainBackBuffer(nullptr, 0, sc, chain) ||
            !drawnTwo(a.p, KSAGE, {1, 2, 63, 63}, KGOLD) || !cleared(x.p, KPLUM) ||
            !scene([&] { return backToScreen() && clearTo(KLILAC); }) || !copyTo(a.p, &toBb, rtSurf, 101, 61) ||
            !copyTo(rtSurf, &fromBb, x.p, 7, 11) || !copyTo(a.p, &toChain, chain.p, 150, 90) ||
            !rtHas("the back buffer", rtSurf, 256, X8,
                   {{101, 61, KGOLD}, {133, 91, KGOLD}, {150, 90, KGOLD}, {182, 123, KGOLD}, {134, 61, KLILAC},
                    {101, 60, KLILAC}, {100, 61, KLILAC}, {101, 92, KLILAC}, {183, 90, KLILAC}, {150, 89, KLILAC},
                    {149, 90, KLILAC}, {150, 124, KLILAC}, {0, 0, KLILAC}}) ||
            !rtHas("X", x.p, 64, X8,
                   {{7, 11, KGOLD}, {39, 41, KGOLD}, {40, 11, KPLUM}, {7, 10, KPLUM}, {6, 11, KPLUM}, {7, 42, KPLUM},
                    {0, 0, KPLUM}}))
            return false;
        return copyTo(a.p, &r64, rtSurf, 0, 0) &&
               rtHas("test_pointsize's copy", rtSurf, 256, X8,
                     {{0, 0, KSAGE}, {32, 32, KGOLD}, {64, 32, KLILAC}, {32, 64, KLILAC}});
    });
}
// sc-lockable: L, a lockable target, written under a lock steel with rust over (2,3)-(34,36); A, a target, cleared
// aqua, a gold quad over (4,0)-(64,8). L's (3,5)-(28,30) (rust) to A's (20,10): rust at (20,10) and (44,34), aqua at
// (45,10), (20,9), (19,10), (20,35) and (0,0). A's (6,2)-(31,27) to L's (22,30), read under a READONLY lock: gold at
// (22,30) (A's (6,2); A's origin is aqua) and (46,30), rust at (46,54) and (36,38) (what the first copy wrote), aqua at
// (22,54); steel at (47,30), (22,55) and (0,0), rust at (21,30) and (22,29) (L's own).
static bool rowScLockable() {
    return until([&] {
        Com<IDirect3DSurface9> l, a;
        const RECT fromL = {3, 5, 28, 30}, fromA = {6, 2, 31, 27};
        return target64(l) && target64(a, FALSE) && twoTone(l.p, 64, KSTEEL, {2, 3, 34, 36}, KRUST) &&
               drawnTwo(a.p, KAQUA, {4, 0, 64, 8}, KGOLD) && copyTo(l.p, &fromL, a.p, 20, 10) &&
               rtHas("A", a.p, 64, A8,
                     {{20, 10, KRUST}, {44, 34, KRUST}, {45, 10, KAQUA}, {20, 9, KAQUA}, {19, 10, KAQUA},
                      {20, 35, KAQUA}, {0, 0, KAQUA}}) &&
               copyTo(a.p, &fromA, l.p, 22, 30) &&
               lockHas("L", l.p,
                       {{22, 30, KGOLD}, {46, 30, KGOLD}, {46, 54, KRUST}, {36, 38, KRUST}, {22, 54, KAQUA},
                        {47, 30, KSTEEL}, {22, 55, KSTEEL}, {0, 0, KSTEEL}, {21, 30, KRUST}, {22, 29, KRUST}});
    });
}
// sc-rtex: X and Y, 64x64 render-target textures of two levels. X's level 0 written by UpdateSurface from a
// system-memory surface (steel, rust over (2,1)-(40,40)) and never drawn; its (5,3)-(26,24) (rust) to Y's level 0 at
// (30,34), Y's first use (what is around the copy is undefined); Y drawn by show() on the back buffer (cleared K565B):
// rust at Y's texels (30,34), (50,54) and (40,44). X's level 1 bound, cleared aqua, a mint quad over (1,0)-(24,20); Y's
// level 1 cleared gold; X's level 1's (3,2)-(18,15) (mint) to Y's level 1 at (9,12): mint at (9,12) (X's level 0 there
// is rust, X's level 1's origin aqua) and (23,24), gold at (24,12), (9,11), (8,12), (9,25) and (0,0).
// C, a 64-texel render-target cube, face 0 (+x) cleared rose and face 3 (-y) lilac; A, a target, cleared sage, a plum
// quad over (2,4)-(30,32); A's (4,6)-(27,29) (plum) to face 3 at (31,22), read by GetRenderTargetData: plum at (31,22)
// and (53,44), lilac at (54,22), (31,21), (30,22), (31,45) and (0,0); face 0 rose at (31,22) and (53,44). D, a
// 32-texel render-target cube, face 0 cleared sky and face 2 (+y) K565A; face 2's (2,3)-(25,20) to A at (33,40): K565A
// at (33,40) and (55,56) (face 0's would be sky), sage at (56,40), (33,39), (32,40), (33,57) and (0,0).
static bool rowScRtex() {
    if (!until([&] {
            Com<IDirect3DTexture9> x, y;
            Com<IDirect3DSurface9> s, x0, x1, y0, y1;
            const RECT first = {5, 3, 26, 24}, second = {3, 2, 18, 15};
            CK(CreateOffscreenPlainSurface, 64, 64, A8, D3DPOOL_SYSTEMMEM, &s.p, nullptr);
            if (!makeRt(x, 64, 2) || !makeRt(y, 64, 2) || !levelOf(x.p, 0, x0) || !levelOf(x.p, 1, x1) ||
                !levelOf(y.p, 0, y0) || !levelOf(y.p, 1, y1) || !twoTone(s.p, 64, KSTEEL, {2, 1, 40, 40}, KRUST) ||
                !updateSurface(s.p, nullptr, x0.p, nullptr) || !copyTo(x0.p, &first, y0.p, 30, 34) ||
                !scene([&] { return backToScreen() && clearTo(K565B) && show(y.p); }) ||
                !rtHas("Y's level 0, drawn", rtSurf, 256, X8,
                       {{125, 133, KRUST}, {165, 173, KRUST}, {145, 153, KRUST}}))
                return false;
            return drawnTwo(x1.p, KAQUA, {1, 0, 24, 20}, KMINT) && cleared(y1.p, KGOLD) &&
                   copyTo(x1.p, &second, y1.p, 9, 12) &&
                   rtHas("Y's level 1", y1.p, 32, A8,
                         {{9, 12, KMINT}, {23, 24, KMINT}, {24, 12, KGOLD}, {9, 11, KGOLD}, {8, 12, KGOLD},
                          {9, 25, KGOLD}, {0, 0, KGOLD}});
        }))
        return false;
    return until([&] {
        Com<IDirect3DCubeTexture9> c, d;
        Com<IDirect3DSurface9> a, c0, c3, d0, d2;
        const RECT toFace = {4, 6, 27, 29}, fromFace = {2, 3, 25, 20};
        CK(CreateCubeTexture, 64, 1, D3DUSAGE_RENDERTARGET, A8, D3DPOOL_DEFAULT, &c.p, nullptr);
        CK(CreateCubeTexture, 32, 1, D3DUSAGE_RENDERTARGET, A8, D3DPOOL_DEFAULT, &d.p, nullptr);
        if (!target64(a, FALSE) || !faceOf(c.p, D3DCUBEMAP_FACE_POSITIVE_X, c0) ||
            !faceOf(c.p, D3DCUBEMAP_FACE_NEGATIVE_Y, c3) || !faceOf(d.p, D3DCUBEMAP_FACE_POSITIVE_X, d0) ||
            !faceOf(d.p, D3DCUBEMAP_FACE_POSITIVE_Y, d2) || !cleared(c0.p, KROSE) || !cleared(c3.p, KLILAC) ||
            !cleared(d0.p, KSKY) || !cleared(d2.p, K565A) || !drawnTwo(a.p, KSAGE, {2, 4, 30, 32}, KPLUM) ||
            !copyTo(a.p, &toFace, c3.p, 31, 22) ||
            !rtHas("face 3", c3.p, 64, A8,
                   {{31, 22, KPLUM}, {53, 44, KPLUM}, {54, 22, KLILAC}, {31, 21, KLILAC}, {30, 22, KLILAC},
                    {31, 45, KLILAC}, {0, 0, KLILAC}}) ||
            !rtHas("face 0", c0.p, 64, A8, {{31, 22, KROSE}, {53, 44, KROSE}}))
            return false;
        return copyTo(d2.p, &fromFace, a.p, 33, 40) &&
               rtHas("A", a.p, 64, A8,
                     {{33, 40, K565A}, {55, 56, K565A}, {56, 40, KSAGE}, {33, 39, KSAGE}, {32, 40, KSAGE},
                      {33, 57, KSAGE}, {0, 0, KSAGE}});
    });
}
// sc-tex: T, a 64x64 default-pool texture of usage 0 and two levels, never bound: level 0 updated whole from a
// system-memory surface (steel, rust over (5,3)-(34,32)), level 1 from a 32x32 one (aqua, mint over (1,1)-(20,20)). A,
// a target, cleared gold. Level 0's (7,5)-(30,28) (rust) to A's (26,31), level 1's (2,3)-(17,16) (mint) to A's (5,6):
// rust at (26,31) and (48,53), mint at (5,6) and (19,18) (level 0 there is steel and rust; level 1's origin is aqua),
// gold at (49,31), (26,30), (25,31), (26,54), (20,6), (5,5), (4,6), (5,19) and (0,0). The source written rose over
// (1,2)-(12,14) and that rect updated to level 0's (8,8) (x 8..18, y 8..19, owed to the core texture the first copy
// made), level 0's (7,5)-(30,28) to A's (26,31) again: rose at (27,34) and (37,45), rust at (26,31), (38,34) and
// (27,46), mint at (5,6), gold at (0,0).
static bool rowScTex() {
    return until([&] {
        Com<IDirect3DTexture9> t;
        Com<IDirect3DSurface9> s0, s1, t0, t1, a;
        const RECT r0 = {7, 5, 30, 28}, r1 = {2, 3, 17, 16}, again = {1, 2, 12, 14};
        const POINT at = {8, 8};
        CK(CreateTexture, 64, 64, 2, 0, A8, D3DPOOL_DEFAULT, &t.p, nullptr);
        CK(CreateOffscreenPlainSurface, 64, 64, A8, D3DPOOL_SYSTEMMEM, &s0.p, nullptr);
        CK(CreateOffscreenPlainSurface, 32, 32, A8, D3DPOOL_SYSTEMMEM, &s1.p, nullptr);
        if (!levelOf(t.p, 0, t0) || !levelOf(t.p, 1, t1) || !target64(a, FALSE) ||
            !twoTone(s0.p, 64, KSTEEL, {5, 3, 34, 32}, KRUST) || !twoTone(s1.p, 32, KAQUA, {1, 1, 20, 20}, KMINT) ||
            !updateSurface(s0.p, nullptr, t0.p, nullptr) || !updateSurface(s1.p, nullptr, t1.p, nullptr) ||
            !cleared(a.p, KGOLD) || !copyTo(t0.p, &r0, a.p, 26, 31) || !copyTo(t1.p, &r1, a.p, 5, 6) ||
            !rtHas("levels 0 and 1", a.p, 64, A8,
                   {{26, 31, KRUST}, {48, 53, KRUST}, {5, 6, KMINT}, {19, 18, KMINT}, {49, 31, KGOLD}, {26, 30, KGOLD},
                    {25, 31, KGOLD}, {26, 54, KGOLD}, {20, 6, KGOLD}, {5, 5, KGOLD}, {4, 6, KGOLD}, {5, 19, KGOLD},
                    {0, 0, KGOLD}}))
            return false;
        return twoTone(s0.p, 64, KSTEEL, again, KROSE) && updateSurface(s0.p, &again, t0.p, &at) &&
               copyTo(t0.p, &r0, a.p, 26, 31) &&
               rtHas("level 0 updated again", a.p, 64, A8,
                     {{27, 34, KROSE}, {37, 45, KROSE}, {26, 31, KRUST}, {38, 34, KRUST}, {27, 46, KRUST},
                      {5, 6, KMINT}, {0, 0, KGOLD}});
    });
}
// sc-plain: P, a plain surface, written under a lock steel with rust over (4,2)-(33,35); A, a target, cleared aqua.
// P's (6,4)-(29,31) (rust) to A's (27,25): rust at (27,25) and (49,51), aqua at (50,25), (27,24), (26,25), (27,52) and
// (0,0). Gold filled over P's (8,8)-(20,20), the same copy again: gold at (29,29) and (40,40) (the fill, owed to the
// core texture the first copy made), rust at (27,25), (41,29) and (29,41), aqua at (50,25).
static bool rowScPlain() {
    return until([&] {
        Com<IDirect3DSurface9> p, a;
        const RECT rect = {6, 4, 29, 31}, fill = {8, 8, 20, 20};
        return plain64(p) && target64(a, FALSE) && twoTone(p.p, 64, KSTEEL, {4, 2, 33, 35}, KRUST) &&
               cleared(a.p, KAQUA) && copyTo(p.p, &rect, a.p, 27, 25) &&
               rtHas("the copy", a.p, 64, A8,
                     {{27, 25, KRUST}, {49, 51, KRUST}, {50, 25, KAQUA}, {27, 24, KAQUA}, {26, 25, KAQUA},
                      {27, 52, KAQUA}, {0, 0, KAQUA}}) &&
               colorFill(p.p, &fill, KGOLD) && copyTo(p.p, &rect, a.p, 27, 25) &&
               rtHas("after the fill", a.p, 64, A8,
                     {{29, 29, KGOLD}, {40, 40, KGOLD}, {27, 25, KRUST}, {41, 29, KRUST}, {29, 41, KRUST},
                      {50, 25, KAQUA}});
    });
}
// sc-plain2: P, Q and N, plain surfaces, and X, an X8R8G8B8 one: P written under a lock steel with rust over
// (4,1)-(33,34), Q aqua, X mint, N nothing; A, a target, cleared gold. Q's (30,19)-(55,45) to A's (4,6) first (DXMTRS
// C4c-4c: Q copied by the GPU before the CPU copy into it): aqua at (5,7) and (27,31), gold at (29,6), (4,5) and (0,0).
// P's (6,3)-(29,30) (rust) to Q's (31,20), read under a READONLY lock: rust at (31,20) and (53,46), aqua at (54,20),
// (31,19), (30,20), (31,47) and (0,0). Q's (30,19)-(55,45) to A's (4,6) again: rust at (5,7) (Q's (31,20), the CPU
// copy's texels, which the GPU copy must see; Q's (1,1) is aqua) and (27,31), aqua at (4,6) and (28,31) (Q's own
// around the first copy), gold at (29,6), (4,5), (3,6), (4,32) and (0,0). P's (7,4)-(28,23) to X's (33,37)
// (formats unequal, bytes the same): rust at (33,37) and (53,55), mint at (54,37), (33,36), (32,37), (33,56) and (0,0)
// (colour channels). P's (6,3)-(29,30) onto itself: D3D_OK, P unchanged (rust at (6,3) and (28,29), steel at (0,0) and
// (40,40)). N, which nothing wrote, into Q, whole: D3D_OK (its texels are not judged).
static bool rowScPlain2() {
    return until([&] {
        Com<IDirect3DSurface9> p, q, x, n, a;
        const RECT toQ = {6, 3, 29, 30}, fromQ = {30, 19, 55, 45}, toX = {7, 4, 28, 23};
        if (!plain64(p) || !plain64(q) || !plain64(x, X8) || !plain64(n) || !target64(a, FALSE) ||
            !twoTone(p.p, 64, KSTEEL, {4, 1, 33, 34}, KRUST) || !twoTone(q.p, 64, KAQUA, {}, 0) ||
            !twoTone(x.p, 64, KMINT, {}, 0) || !cleared(a.p, KGOLD) || !copyTo(q.p, &fromQ, a.p, 4, 6) ||
            !rtHas("A before", a.p, 64, A8,
                   {{5, 7, KAQUA}, {27, 31, KAQUA}, {29, 6, KGOLD}, {4, 5, KGOLD}, {0, 0, KGOLD}}) ||
            !copyTo(p.p, &toQ, q.p, 31, 20) ||
            !lockHas("Q", q.p,
                     {{31, 20, KRUST}, {53, 46, KRUST}, {54, 20, KAQUA}, {31, 19, KAQUA}, {30, 20, KAQUA},
                      {31, 47, KAQUA}, {0, 0, KAQUA}}) ||
            !copyTo(q.p, &fromQ, a.p, 4, 6) ||
            !rtHas("A", a.p, 64, A8,
                   {{5, 7, KRUST}, {27, 31, KRUST}, {4, 6, KAQUA}, {28, 31, KAQUA}, {29, 6, KGOLD}, {4, 5, KGOLD},
                    {3, 6, KGOLD}, {4, 32, KGOLD}, {0, 0, KGOLD}}) ||
            !copyTo(p.p, &toX, x.p, 33, 37) ||
            !lockHas("X", x.p,
                     {{33, 37, KRUST}, {53, 55, KRUST}, {54, 37, KMINT}, {33, 36, KMINT}, {32, 37, KMINT},
                      {33, 56, KMINT}, {0, 0, KMINT}}) ||
            !answer("StretchRect (a plain surface onto itself, identical rects)",
                    dev->StretchRect(p.p, &toQ, p.p, &toQ, D3DTEXF_NONE), D3D_OK) ||
            !lockHas("P", p.p, {{6, 3, KRUST}, {28, 29, KRUST}, {0, 0, KSTEEL}, {40, 40, KSTEEL}}))
            return false;
        return answer("StretchRect (a plain surface nothing wrote)",
                      dev->StretchRect(n.p, nullptr, q.p, nullptr, D3DTEXF_NONE), D3D_OK);
    });
}
// sc-conv: P, a plain surface, written under a lock K565A with K565B over (3,2)-(28,29); R and S, R5G6B5 plain
// surfaces, R written K565C. P's (5,4)-(26,27) (K565B) to R's (30,31) and P whole into S (CPU conversions), read 2
// bytes a texel under a lock, each exactly its packed565: R K565B at (30,31) and (50,53), K565C at (51,31), (30,30),
// (29,31), (30,54) and (0,0); S K565A at (0,0) and (40,40), K565B at (10,10). Y, a YUY2 plain surface where
// CheckDeviceFormat offers one ("d3d9 sc-conv-yuy2 skip" where not), written under a lock Y 0xb4, U and V 0x80 in every
// macropixel, into an X8R8G8B8 plain surface whole: (0xc0, 0xbe, 0xc0) at (9,7), the codec's BT.601 decode worked by
// hand (the bytes copied raw would read (0xb4, 0x80, 0xb4)).
static bool rowScConv() {
    Com<IDirect3DSurface9> p, r, s;
    const RECT rect = {5, 4, 26, 27};
    if (!plain64(p) || !plain64(r, D3DFMT_R5G6B5) || !plain64(s, D3DFMT_R5G6B5) ||
        !twoTone(p.p, 64, K565A, {3, 2, 28, 29}, K565B) || !twoTone(r.p, 64, packed565(K565C), {}, 0, 2) ||
        !copyTo(p.p, &rect, r.p, 30, 31) || !copyTo(p.p, nullptr, s.p) ||
        !has565("R", r.p,
                {{30, 31, K565B}, {50, 53, K565B}, {51, 31, K565C}, {30, 30, K565C}, {29, 31, K565C}, {30, 54, K565C},
                 {0, 0, K565C}}) ||
        !has565("S", s.p, {{0, 0, K565A}, {40, 40, K565A}, {10, 10, K565B}}))
        return false;
    if (!offered(D3DRTYPE_SURFACE, D3DFMT_YUY2, "sc-conv-yuy2")) return true;
    Com<IDirect3DSurface9> y, x;
    CK(CreateOffscreenPlainSurface, 64, 64, D3DFMT_YUY2, D3DPOOL_DEFAULT, &y.p, nullptr);
    return plain64(x, X8) &&
           underLock(y.p, "LockRect (YUY2)", nullptr, 0,
                     [&](BYTE *b, int pitch) { paint(b, pitch, 0, 0, 32, 64, 0x80b480b4); }) &&
           copyTo(y.p, nullptr, x.p) && lockHas("YUY2 into X8R8G8B8", x.p, {{9, 7, D3DCOLOR_XRGB(0xc0, 0xbe, 0xc0)}});
}
// sc-same (one subresource at both ends): A, a target, cleared steel, a rust quad over (1,0)-(18,20); A's (3,2)-(16,18)
// (rust) onto A's (40,32): rust at (40,32) and (52,47), steel at (53,32), (40,31), (39,32), (40,48) and (0,0), the
// source kept (rust at (3,2) and (15,17)). The back buffer cleared aqua, a gold quad over (2,4)-(39,40); an additional
// swap chain's back buffer's (4,6)-(37,38) (the device's, aliased) onto the device's at (150,120): gold at (150,120),
// (182,151) and (4,6), aqua at (183,120), (150,119), (149,120), (150,152) and (0,0).
static bool rowScSame() {
    return until([&] {
        Com<IDirect3DSwapChain9> sc;
        Com<IDirect3DSurface9> a, chain;
        const RECT rect = {3, 2, 16, 18}, onBb = {4, 6, 37, 38};
        if (!target64(a, FALSE) || !chainBackBuffer(nullptr, 0, sc, chain) ||
            !drawnTwo(a.p, KSTEEL, {1, 0, 18, 20}, KRUST) || !copyTo(a.p, &rect, a.p, 40, 32) ||
            !rtHas("A", a.p, 64, A8,
                   {{40, 32, KRUST}, {52, 47, KRUST}, {53, 32, KSTEEL}, {40, 31, KSTEEL}, {39, 32, KSTEEL},
                    {40, 48, KSTEEL}, {0, 0, KSTEEL}, {3, 2, KRUST}, {15, 17, KRUST}}))
            return false;
        return scene([&] { return backToScreen() && clearTo(KAQUA) && zquad(2, 4, 39, 40, 0.5f, KGOLD); }) &&
               copyTo(chain.p, &onBb, rtSurf, 150, 120) &&
               rtHas("the back buffer", rtSurf, 256, X8,
                     {{150, 120, KGOLD}, {182, 151, KGOLD}, {4, 6, KGOLD}, {183, 120, KAQUA}, {150, 119, KAQUA},
                      {149, 120, KAQUA}, {150, 152, KAQUA}, {0, 0, KAQUA}});
    });
}
// sc-self (one texture, two subresources): T, a 64x64 render-target texture of two levels, level 0 cleared steel with
// a rust quad over (6,6)-(42,42), level 1 cleared aqua; level 0's (8,8)-(40,40) onto level 1 at (0,0), the whole level
// (as Wine copies): rust at (0,0) (level 0's origin is steel), (31,31) and (16,16). C, a 64-texel render-target cube,
// faces 0, 1 (-x) and 4 (+z) cleared gold, mint and rose; face 1's (3,4)-(26,25) onto face 4 at (30,33): mint at
// (30,33) and (52,53), rose at (53,33), (30,32), (29,33), (30,54) and (0,0); face 0 gold at (30,33) and (52,53), face 1
// mint at (3,4). A, a target, cleared lilac, a sage quad over (10,10)-(30,30): its (12,12)-(28,28) onto itself,
// D3D_OK, A unchanged (sage at (12,12) and (27,27), lilac at (0,0) and (40,40)). Overlapping rects of one surface are
// sq-overlap's (DXMTRS C4c-4c).
static bool rowScSelf() {
    return until([&] {
        Com<IDirect3DTexture9> t;
        Com<IDirect3DCubeTexture9> c;
        Com<IDirect3DSurface9> l0, l1, f0, f1, f4, a;
        const RECT level = {8, 8, 40, 40}, face = {3, 4, 26, 25}, same = {12, 12, 28, 28};
        CK(CreateCubeTexture, 64, 1, D3DUSAGE_RENDERTARGET, A8, D3DPOOL_DEFAULT, &c.p, nullptr);
        if (!makeRt(t, 64, 2) || !levelOf(t.p, 0, l0) || !levelOf(t.p, 1, l1) ||
            !faceOf(c.p, D3DCUBEMAP_FACE_POSITIVE_X, f0) || !faceOf(c.p, D3DCUBEMAP_FACE_NEGATIVE_X, f1) ||
            !faceOf(c.p, D3DCUBEMAP_FACE_POSITIVE_Z, f4) || !target64(a, FALSE) ||
            !drawnTwo(l0.p, KSTEEL, {6, 6, 42, 42}, KRUST) || !cleared(l1.p, KAQUA) ||
            !copyTo(l0.p, &level, l1.p, 0, 0) ||
            !rtHas("level 1", l1.p, 32, A8, {{0, 0, KRUST}, {31, 31, KRUST}, {16, 16, KRUST}}) ||
            !cleared(f0.p, KGOLD) || !cleared(f1.p, KMINT) || !cleared(f4.p, KROSE) ||
            !copyTo(f1.p, &face, f4.p, 30, 33) ||
            !rtHas("face 4", f4.p, 64, A8,
                   {{30, 33, KMINT}, {52, 53, KMINT}, {53, 33, KROSE}, {30, 32, KROSE}, {29, 33, KROSE},
                    {30, 54, KROSE}, {0, 0, KROSE}}) ||
            !rtHas("face 0", f0.p, 64, A8, {{30, 33, KGOLD}, {52, 53, KGOLD}}) ||
            !rtHas("face 1", f1.p, 64, A8, {{3, 4, KMINT}}) || !drawnTwo(a.p, KLILAC, {10, 10, 30, 30}, KSAGE) ||
            !answer("StretchRect (identical rects of one surface)",
                    dev->StretchRect(a.p, &same, a.p, &same, D3DTEXF_NONE), D3D_OK) ||
            !rtHas("A", a.p, 64, A8, {{12, 12, KSAGE}, {27, 27, KSAGE}, {0, 0, KLILAC}, {40, 40, KLILAC}}))
            return false;
        return true;
    });
}
// sc-autogen: G and H, 64x64 AUTOGENMIPMAP render-target textures (X8R8G8B8), G cleared rust and H mint; S, a 64x64
// AUTOGENMIPMAP texture of usage 0 (X8R8G8B8); A, a target, cleared aqua; the back buffer cleared steel; 4x4 quads
// sample G at (100,100) and H at (100,150) at their level 4 (POINT mip filter): rust at (102,102), mint at (102,152).
// A into G's level 0, whole (as cf-autogen's fill); S's level 0 updated whole from a system-memory surface written gold
// (test_mipmap_autogen's source); G at (150,100) and S drawn 64x64 over (180,180)-(244,244) (its level 0); S's level 0
// into H's level 0, whole; H at (150,150): aqua at (152,102) and gold at (152,152) (each chain regenerated from its
// copy), gold at (212,212) (S drawn), rust still at (102,102), mint at (102,152), steel at (20,20).
static bool rowScAutogen() {
    return until([&] {
        Com<IDirect3DTexture9> g, h, s;
        Com<IDirect3DSurface9> a, sys, g0, h0, s0;
        const DWORD autogen = D3DUSAGE_RENDERTARGET | D3DUSAGE_AUTOGENMIPMAP;
        CK(CreateTexture, 64, 64, 0, D3DUSAGE_AUTOGENMIPMAP, X8, D3DPOOL_DEFAULT, &s.p, nullptr);
        CK(CreateOffscreenPlainSurface, 64, 64, X8, D3DPOOL_SYSTEMMEM, &sys.p, nullptr);
        if (!makeRt(g, 64, 0, X8, autogen) || !makeRt(h, 64, 0, X8, autogen) || !levelOf(g.p, 0, g0) ||
            !levelOf(h.p, 0, h0) || !levelOf(s.p, 0, s0) || !target64(a, FALSE))
            return false;
        CK(SetSamplerState, 0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
        return scene([&] {
                   return clearRt(a.p, KAQUA) && target(g.p) && clearTo(KRUST) && target(h.p) && clearTo(KMINT) &&
                          backToScreen() && clearTo(KSTEEL) && show(g.p, 100, 100, 104, 104) &&
                          show(h.p, 100, 150, 104, 154);
               }) &&
               copyTo(a.p, nullptr, g0.p) && twoTone(sys.p, 64, KGOLD, {}, 0) &&
               updateSurface(sys.p, nullptr, s0.p, nullptr) &&
               scene([&] { return show(g.p, 150, 100, 154, 104) && show(s.p, 180, 180, 244, 244); }) &&
               copyTo(s0.p, nullptr, h0.p) && scene([&] { return show(h.p, 150, 150, 154, 154); }) &&
               rtHas("the back buffer", rtSurf, 256, X8,
                     {{152, 102, KAQUA}, {212, 212, KGOLD}, {152, 152, KGOLD}, {102, 102, KRUST}, {102, 152, KMINT},
                      {20, 20, KSTEEL}});
    });
}
// sc-order, in one scene: A and B, targets, B cleared aqua, A steel; A's (5,7)-(26,30) to B's (30,20), then a rust quad
// over A's (0,0)-(40,40). Y's level 1 (a 64x64 render-target texture's, 32x32) cleared lilac, a gold quad over
// (2,2)-(20,20), A's (44,40)-(57,52) (steel; A's origin is rust by now) to (10,9) over part of it, a rose quad over
// (25,2)-(31,30) beside it; the same on face 3 (-y) of a 32-texel render-target cube: sky, plum, the copy, sage. B
// bound, A's (45,44)-(60,57) to B's (5,47), a mint quad over (14,48)-(28,64) over part of it. B: steel at (30,20) and
// (50,42) (A as the Clear left it: the quad after the copy is not in it), aqua at (51,20), (30,19), (29,20), (30,43)
// and (0,0); steel at (5,47) and (10,58), mint at (16,52) (over the copy) and (24,60), aqua at (4,47) and (5,46). A:
// rust at (10,10), steel at (50,50). Y's level 1 and the face: the first quad at (4,4); steel at (10,9), (12,11) (over
// the first quad) and (22,20); the second quad at (28,15); the clear at (23,12), (10,21) and (0,0).
static bool rowScOrder() {
    return until([&] {
        Com<IDirect3DTexture9> y;
        Com<IDirect3DCubeTexture9> c;
        Com<IDirect3DSurface9> a, b, y1, f3;
        const RECT first = {5, 7, 26, 30}, mid = {44, 40, 57, 52}, last = {45, 44, 60, 57};
        CK(CreateCubeTexture, 32, 1, D3DUSAGE_RENDERTARGET, A8, D3DPOOL_DEFAULT, &c.p, nullptr);
        if (!target64(a, FALSE) || !target64(b, FALSE) || !makeRt(y, 64, 2) || !levelOf(y.p, 1, y1) ||
            !faceOf(c.p, D3DCUBEMAP_FACE_NEGATIVE_Y, f3))
            return false;
        // t bound, cleared, a first quad, the copy over part of it, a second quad beside it.
        const auto around = [&](IDirect3DSurface9 *t, DWORD clear, DWORD one, DWORD two) {
            return clearRt(t, clear) && zquad(2, 2, 20, 20, 0.5f, one) && copyTo(a.p, &mid, t, 10, 9) &&
                   zquad(25, 2, 31, 30, 0.5f, two);
        };
        const auto has = [&](const char *name, IDirect3DSurface9 *t, DWORD clear, DWORD one, DWORD two) {
            return rtHas(name, t, 32, A8,
                         {{4, 4, one}, {10, 9, KSTEEL}, {12, 11, KSTEEL}, {22, 20, KSTEEL}, {28, 15, two},
                          {23, 12, clear}, {10, 21, clear}, {0, 0, clear}});
        };
        return scene([&] {
                   return clearRt(b.p, KAQUA) && clearRt(a.p, KSTEEL) && copyTo(a.p, &first, b.p, 30, 20) &&
                          zquad(0, 0, 40, 40, 0.5f, KRUST) && around(y1.p, KLILAC, KGOLD, KROSE) &&
                          around(f3.p, KSKY, KPLUM, KSAGE) && bindRt(0, b.p) && copyTo(a.p, &last, b.p, 5, 47) &&
                          zquad(14, 48, 28, 64, 0.5f, KMINT) && backToScreen();
               }) &&
               rtHas("B", b.p, 64, A8,
                     {{30, 20, KSTEEL}, {50, 42, KSTEEL}, {51, 20, KAQUA}, {30, 19, KAQUA}, {29, 20, KAQUA},
                      {30, 43, KAQUA}, {0, 0, KAQUA}, {5, 47, KSTEEL}, {10, 58, KSTEEL}, {16, 52, KMINT},
                      {24, 60, KMINT}, {4, 47, KAQUA}, {5, 46, KAQUA}}) &&
               rtHas("A", a.p, 64, A8, {{10, 10, KRUST}, {50, 50, KSTEEL}}) &&
               has("Y's level 1", y1.p, KLILAC, KGOLD, KROSE) && has("face 3", f3.p, KSKY, KPLUM, KSAGE);
    });
}

// The sq-* rows (DXMTRS C4c-4c): StretchRect's sampled copies (scaled, converting, from YUV, multisampled,
// overlapping), D3D_OK in every lane, their texels judged. A scaled copy's destination texel x (counted from its rect's
// left) shows the source texel under x0 + (x + 0.5) * sw / dw (mtld3d's quad samples at texel centres): a 2x copy's
// texel x samples x / 2 + 0.25 past the source rect's left, so texels 2k and 2k + 1 show the source's k.
// StretchRect of src's rect sr to dst's rect dr (null: the whole surface) with `filter`; false, with `why` naming both
// rects and the filter, unless it succeeds.
static bool stretchTo(IDirect3DSurface9 *src, const RECT *sr, IDirect3DSurface9 *dst, const RECT *dr,
                      D3DTEXTUREFILTERTYPE filter) {
    const HRESULT hr = dev->StretchRect(src, sr, dst, dr, filter);
    if (SUCCEEDED(hr)) return true;
    const auto text = [](char *t, size_t n, const RECT *r) {
        if (r) snprintf(t, n, "(%ld,%ld)-(%ld,%ld)", (long)r->left, (long)r->top, (long)r->right, (long)r->bottom);
        else snprintf(t, n, "whole");
    };
    char s[96], d[96];
    text(s, sizeof s, sr), text(d, sizeof d, dr);
    snprintf(why, sizeof why, "StretchRect %s to %s filter %d %08lx", s, d, (int)filter, (unsigned long)hr);
    return false;
}
// The R5G6B5 texels px of the size x size target `s`, read 2 bytes each through GetRenderTargetData into an R5G6B5
// system-memory surface: each exactly its colour's packed565 (a k565 colour, which the GPU's rounding and the CPU's
// truncation both keep).
static bool rtHas565(const char *name, IDirect3DSurface9 *s, UINT size, std::initializer_list<Tx> px) {
    return readTarget(s, size, size, D3DFMT_R5G6B5, [&](const BYTE *b, int pitch) {
        for (const Tx &t : px) {
            WORD got;
            memcpy(&got, b + t.y * pitch + t.x * 2, 2);
            if (got != packed565(t.want))
                return snprintf(why, sizeof why, "%s (%d,%d) %04x want %04x", name, t.x, t.y, got, packed565(t.want)),
                       false;
        }
        return true;
    });
}
// c with alpha 0x40: what a converting cell's destination holds around its copy, which reads alpha 0xff.
static DWORD alpha40(DWORD c) { return (c & 0xffffff) | 0x40000000; }
// `t` bound, cleared `out` and, by a Clear of the rect (3,2)-(28,29), `in`; the back buffer bound again: one scene.
static bool clearedTwo(IDirect3DSurface9 *t, DWORD out, DWORD in) {
    const D3DRECT r = {3, 2, 28, 29};
    return scene([&] {
        if (!clearRt(t, out)) return false;
        CK(Clear, 1, &r, D3DCLEAR_TARGET, in, 1.0f, 0);
        return backToScreen();
    });
}
// The 64x64 A8R8G8B8 target `a` after a 1:1 copy of a w x h rect to (x, y), read with alpha: `in` at the copy's first,
// last and middle texels, `around` just right of it, above it, left of it and below it, and at (0,0).
static bool copiedHas(const char *name, IDirect3DSurface9 *a, int x, int y, int w, int h, DWORD in, DWORD around) {
    const DWORD m = WITH_ALPHA;
    return rtHas(name, a, 64, A8,
                 {{x, y, in, m}, {x + w - 1, y + h - 1, in, m}, {x + w / 2, y + h / 2, in, m}, {x + w, y, around, m},
                  {x, y - 1, around, m}, {x - 1, y, around, m}, {x, y + h, around, m}, {0, 0, around, m}});
}
// The bound size x size target cleared steel, its quadrants drawn: rust top right, aqua bottom left, gold bottom right.
static bool quadrants(float size) {
    const float h = size / 2;
    return clearTo(KSTEEL) && zquad(h, 0, size, h, 0.5f, KRUST) && zquad(0, h, h, size, 0.5f, KAQUA) &&
           zquad(h, h, size, size, 0.5f, KGOLD);
}
// sq-scale: A, a 32x32 target, and C, a 64x64 one, each in quadrants() (edges at 16 and at 32); B, a target, cleared
// lilac before each cell; D, a 16x16 target. A into B whole, POINT, then again NONE: steel at (8,8), rust at (56,8),
// aqua at (8,56), gold at (56,56), and texels 31 and 32 across each edge (A's 15.75 and 16.25): steel at (31,8), rust
// at (32,8), steel at (8,31), aqua at (8,32), aqua at (31,56), gold at (32,56), rust at (56,31), gold at (56,32). A's
// (8,8)-(24,24) into B's (5,7)-(37,39), POINT (2x into a sub-rect): steel at (5,7) and (20,22), rust at (21,22), aqua
// at (20,23), gold at (21,23) and (36,38) (A's 15.75 and 16.25 either side of its edges), lilac at (37,38), (36,39),
// (4,7), (5,6) and (0,0). C into D, POINT (a 4x downscale: D's texel i samples C's 4i + 2.0, between two texels of one
// colour): steel at (2,2) and (7,7), rust at (13,2) and (8,7), aqua at (2,13) and (7,8), gold at (13,13) and (8,8). A
// into B whole, LINEAR, along row 8 (A's row 4.25: its rows 3 and 4, steel and rust alike): steel at (30,8) (A's 15.25,
// between texels 14 and 15), 3/4 steel and 1/4 rust at (31,8) (15.75: (0x54, 0x58, 0x74)), 1/4 steel and 3/4 rust at
// (32,8) (16.25: (0x9c, 0x48, 0x3c)), rust at (33,8) (16.75), each within 1.
static bool rowSqScale() {
    return until([&] {
        Com<IDirect3DSurface9> a, b, c, d;
        const RECT from = {8, 8, 24, 24}, to = {5, 7, 37, 39};
        CK(CreateRenderTarget, 32, 32, A8, D3DMULTISAMPLE_NONE, 0, FALSE, &a.p, nullptr);
        CK(CreateRenderTarget, 16, 16, A8, D3DMULTISAMPLE_NONE, 0, FALSE, &d.p, nullptr);
        if (!target64(b, FALSE) || !target64(c, FALSE) || !scene([&] {
                return bindRt(0, a.p) && quadrants(32) && bindRt(0, c.p) && quadrants(64) && backToScreen();
            }))
            return false;
        const auto whole = [&](const char *name, D3DTEXTUREFILTERTYPE f) {
            return cleared(b.p, KLILAC) && stretchTo(a.p, nullptr, b.p, nullptr, f) &&
                   rtHas(name, b.p, 64, A8,
                         {{8, 8, KSTEEL}, {56, 8, KRUST}, {8, 56, KAQUA}, {56, 56, KGOLD}, {31, 8, KSTEEL},
                          {32, 8, KRUST}, {8, 31, KSTEEL}, {8, 32, KAQUA}, {31, 56, KAQUA}, {32, 56, KGOLD},
                          {56, 31, KRUST}, {56, 32, KGOLD}});
        };
        return whole("POINT", D3DTEXF_POINT) && whole("NONE", D3DTEXF_NONE) && cleared(b.p, KLILAC) &&
               stretchTo(a.p, &from, b.p, &to, D3DTEXF_POINT) &&
               rtHas("a sub-rect", b.p, 64, A8,
                     {{5, 7, KSTEEL}, {20, 22, KSTEEL}, {21, 22, KRUST}, {20, 23, KAQUA}, {21, 23, KGOLD},
                      {36, 38, KGOLD}, {37, 38, KLILAC}, {36, 39, KLILAC}, {4, 7, KLILAC}, {5, 6, KLILAC},
                      {0, 0, KLILAC}}) &&
               stretchTo(c.p, nullptr, d.p, nullptr, D3DTEXF_POINT) &&
               rtHas("64 into 16", d.p, 16, A8,
                     {{2, 2, KSTEEL}, {7, 7, KSTEEL}, {13, 2, KRUST}, {8, 7, KRUST}, {2, 13, KAQUA}, {7, 8, KAQUA},
                      {13, 13, KGOLD}, {8, 8, KGOLD}}) &&
               cleared(b.p, KLILAC) && stretchTo(a.p, nullptr, b.p, nullptr, D3DTEXF_LINEAR) &&
               rtHas("LINEAR", b.p, 64, A8,
                     {{30, 8, KSTEEL}, {31, 8, D3DCOLOR_XRGB(0x54, 0x58, 0x74)},
                      {32, 8, D3DCOLOR_XRGB(0x9c, 0x48, 0x3c)}, {33, 8, KRUST}});
    });
}
// sq-level: T, a 64x64 render-target texture of two levels, level 0 cleared steel and level 1 rust; A, a target,
// cleared aqua. T's level 1's (3,2)-(18,17) into A's (9,11)-(39,41) (2x): rust at (9,11), (38,40) and (24,26) (level 0
// would give steel), aqua at (39,40), (38,41), (8,11), (9,10) and (0,0). B, a target, cleared mint, a gold quad over
// (4,5)-(40,40); U, a second such texture, level 0 cleared lilac and level 1 sage; C, a 32-texel render-target cube,
// face 0 (+x) cleared rose and face 3 (-y) plum. B's (5,6)-(15,16) (gold; B's origin is mint) into U's level 1's and
// into C's face 3's (7,5)-(27,25) (2x): gold at (7,5), (26,24) and (16,14), the clear at (27,24), (26,25), (6,5),
// (7,4) and (0,0); U's level 0 lilac and C's face 0 rose at (7,5) and (26,24). D, a 32-texel render-target cube, face
// 0 cleared sky and face 2 (+y) mint; face 2's (2,3)-(13,14) into A's (40,30)-(62,52) (2x): mint at (40,30), (61,51)
// and (50,40) (face 0 would give sky), aqua at (62,51), (61,52), (39,30) and (40,29), the first copy's rust at (24,26).
static bool rowSqLevel() {
    return until([&] {
        Com<IDirect3DTexture9> t, u;
        Com<IDirect3DCubeTexture9> c, d;
        Com<IDirect3DSurface9> t0, t1, u0, u1, c0, c3, d0, d2, a, b;
        const RECT fromT = {3, 2, 18, 17}, toA = {9, 11, 39, 41}, fromB = {5, 6, 15, 16}, toU = {7, 5, 27, 25},
                   fromD = {2, 3, 13, 14}, toA2 = {40, 30, 62, 52};
        CK(CreateCubeTexture, 32, 1, D3DUSAGE_RENDERTARGET, A8, D3DPOOL_DEFAULT, &c.p, nullptr);
        CK(CreateCubeTexture, 32, 1, D3DUSAGE_RENDERTARGET, A8, D3DPOOL_DEFAULT, &d.p, nullptr);
        // B's rect into s (level 1 of U or face 3 of C, cleared `clear`), read.
        const auto into = [&](const char *name, IDirect3DSurface9 *s, DWORD clear) {
            return stretchTo(b.p, &fromB, s, &toU, D3DTEXF_POINT) &&
                   rtHas(name, s, 32, A8,
                         {{7, 5, KGOLD}, {26, 24, KGOLD}, {16, 14, KGOLD}, {27, 24, clear}, {26, 25, clear},
                          {6, 5, clear}, {7, 4, clear}, {0, 0, clear}});
        };
        if (!makeRt(t, 64, 2) || !makeRt(u, 64, 2) || !levelOf(t.p, 0, t0) || !levelOf(t.p, 1, t1) ||
            !levelOf(u.p, 0, u0) || !levelOf(u.p, 1, u1) || !faceOf(c.p, D3DCUBEMAP_FACE_POSITIVE_X, c0) ||
            !faceOf(c.p, D3DCUBEMAP_FACE_NEGATIVE_Y, c3) || !faceOf(d.p, D3DCUBEMAP_FACE_POSITIVE_X, d0) ||
            !faceOf(d.p, D3DCUBEMAP_FACE_POSITIVE_Y, d2) || !target64(a, FALSE) || !target64(b, FALSE) ||
            !cleared(t0.p, KSTEEL) || !cleared(t1.p, KRUST) || !cleared(a.p, KAQUA) ||
            !stretchTo(t1.p, &fromT, a.p, &toA, D3DTEXF_POINT) ||
            !rtHas("A", a.p, 64, A8,
                   {{9, 11, KRUST}, {38, 40, KRUST}, {24, 26, KRUST}, {39, 40, KAQUA}, {38, 41, KAQUA},
                    {8, 11, KAQUA}, {9, 10, KAQUA}, {0, 0, KAQUA}}))
            return false;
        if (!drawnTwo(b.p, KMINT, {4, 5, 40, 40}, KGOLD) || !cleared(u0.p, KLILAC) || !cleared(u1.p, KSAGE) ||
            !cleared(c0.p, KROSE) || !cleared(c3.p, KPLUM) || !into("U's level 1", u1.p, KSAGE) ||
            !into("C's face 3", c3.p, KPLUM) ||
            !rtHas("U's level 0", u0.p, 64, A8, {{7, 5, KLILAC}, {26, 24, KLILAC}}) ||
            !rtHas("C's face 0", c0.p, 32, A8, {{7, 5, KROSE}, {26, 24, KROSE}}))
            return false;
        return cleared(d0.p, KSKY) && cleared(d2.p, KMINT) && stretchTo(d2.p, &fromD, a.p, &toA2, D3DTEXF_POINT) &&
               rtHas("A again", a.p, 64, A8,
                     {{40, 30, KMINT}, {61, 51, KMINT}, {50, 40, KMINT}, {62, 51, KAQUA}, {61, 52, KAQUA},
                      {39, 30, KAQUA}, {40, 29, KAQUA}, {24, 26, KRUST}});
    });
}
// sq-conv: converting copies (one D3D format into another), each into a new target A cleared aqua with alpha 0x40, read
// by copiedHas: the copied texels alpha 0xff, A's own around them. The back buffer cleared lilac, a rust quad over
// (100,60)-(140,100); its (101,61)-(134,92) (X8R8G8B8) to A's (7,11), 1:1: rust. X, an X8R8G8B8 target, cleared mint
// and gold over (3,2)-(28,29), both with alpha 0x40: its padding at (10,10) read first through GetRenderTargetData,
// and where it is not 0x40 the line "d3d9 sq-conv-padding skip" (the copy's alpha 0xff then no longer tells a forced
// alpha from the padding); X's (5,4)-(26,27) (gold) to A's (30,31): gold. Where an R5G6B5 target is offered ("d3d9
// sq-conv-r5g6b5 skip" where not): S, a target, cleared K565A and K565B over (3,2)-(28,29); R, an R5G6B5 target,
// cleared K565C; S's (5,4)-(26,27) to R's (30,31), read 2 bytes a texel, each exactly: K565B at (30,31), (50,53) and
// (40,42), K565C at (51,31), (30,30), (29,31), (30,54) and (0,0); R's (31,32)-(48,50) (K565B; R's origin is K565C) to
// A's (6,5): K565B (each channel within 1 of the widening). Where an A16B16G16R16F target is offered ("d3d9
// sq-conv-a16b16g16r16f skip" where not): S cleared steel and rust over (3,2)-(28,29); F, such a target, cleared lilac
// with alpha 0x40; S's (5,4)-(26,27) to F's (30,31): rust at (30,31) and (50,53), lilac at (51,31), (30,54) and (0,0),
// as floats, each channel within 0.004; F's (31,32)-(48,50) to A's (6,5): rust. U, a 64x64 usage-0 X8R8G8B8 texture,
// updated from a system-memory surface written steel with rust over (5,3)-(34,32), both with padding 0x40; U's
// (7,5)-(30,28) to A's (26,31): rust (alpha 0xff: the texture is sampled through its X8R8G8B8 swizzle).
static bool rowSqConv() {
    const DWORD aqua = alpha40(KAQUA);
    const bool r565 = offered(D3DRTYPE_SURFACE, D3DFMT_R5G6B5, "sq-conv-r5g6b5", D3DUSAGE_RENDERTARGET);
    const bool f16 = offered(D3DRTYPE_SURFACE, D3DFMT_A16B16G16R16F, "sq-conv-a16b16g16r16f", D3DUSAGE_RENDERTARGET);
    const RECT rect = {5, 4, 26, 27}, back = {31, 32, 48, 50};
    bool padSaid = false;
    const auto fresh = [&](Com<IDirect3DSurface9> &a) { return target64(a, FALSE) && cleared(a.p, aqua); };
    if (!until([&] {
            Com<IDirect3DSurface9> a;
            const RECT bb = {101, 61, 134, 92};
            return fresh(a) &&
                   scene([&] {
                       return backToScreen() && clearTo(KLILAC) && zquad(100, 60, 140, 100, 0.5f, KRUST);
                   }) &&
                   copyTo(rtSurf, &bb, a.p, 7, 11) && copiedHas("A", a.p, 7, 11, 33, 31, KRUST, aqua);
        }))
        return failIn("the back buffer");
    if (!until([&] {
            Com<IDirect3DSurface9> a, x;
            DWORD pad = 0;
            CK(CreateRenderTarget, 64, 64, X8, D3DMULTISAMPLE_NONE, 0, FALSE, &x.p, nullptr);
            if (!fresh(a) || !clearedTwo(x.p, alpha40(KMINT), alpha40(KGOLD)) ||
                !readTarget(x.p, 64, 64, X8, [&](const BYTE *b, int p) {
                    pad = texel(b, p, 10, 10) >> 24;
                    return true;
                }))
                return false;
            if (pad != 0x40 && !padSaid) padSaid = true, printf("d3d9 sq-conv-padding skip\n");
            return copyTo(x.p, &rect, a.p, 30, 31) && copiedHas("A", a.p, 30, 31, 21, 23, KGOLD, aqua);
        }))
        return failIn("X8R8G8B8");
    if (r565 && !until([&] {
            Com<IDirect3DSurface9> a, s, r;
            CK(CreateRenderTarget, 64, 64, D3DFMT_R5G6B5, D3DMULTISAMPLE_NONE, 0, FALSE, &r.p, nullptr);
            return fresh(a) && target64(s, FALSE) && clearedTwo(s.p, K565A, K565B) && cleared(r.p, K565C) &&
                   copyTo(s.p, &rect, r.p, 30, 31) &&
                   rtHas565("R", r.p, 64,
                            {{30, 31, K565B}, {50, 53, K565B}, {40, 42, K565B}, {51, 31, K565C}, {30, 30, K565C},
                             {29, 31, K565C}, {30, 54, K565C}, {0, 0, K565C}}) &&
                   copyTo(r.p, &back, a.p, 6, 5) && copiedHas("A", a.p, 6, 5, 17, 18, K565B | 0xff000000, aqua);
        }))
        return failIn("R5G6B5");
    if (f16 && !until([&] {
            Com<IDirect3DSurface9> a, s, f;
            const DWORD lilac = alpha40(KLILAC);
            const struct { int x, y; DWORD c; } px[] = {
                {30, 31, KRUST}, {50, 53, KRUST}, {51, 31, lilac}, {30, 54, lilac}, {0, 0, lilac}};
            CK(CreateRenderTarget, 64, 64, D3DFMT_A16B16G16R16F, D3DMULTISAMPLE_NONE, 0, FALSE, &f.p, nullptr);
            if (!fresh(a) || !target64(s, FALSE) || !clearedTwo(s.p, KSTEEL, KRUST) || !cleared(f.p, lilac) ||
                !copyTo(s.p, &rect, f.p, 30, 31))
                return false;
            for (const auto &k : px) {
                const float expect[4] = {(k.c >> 16 & 255) / 255.0f, (k.c >> 8 & 255) / 255.0f, (k.c & 255) / 255.0f,
                                         (k.c >> 24) / 255.0f};
                if (!rtHasF("F", f.p, 64, D3DFMT_A16B16G16R16F, 8, k.x, k.y, expect, 4)) return false;
            }
            return copyTo(f.p, &back, a.p, 6, 5) && copiedHas("A", a.p, 6, 5, 17, 18, KRUST, aqua);
        }))
        return failIn("A16B16G16R16F");
    return until([&] {
        Com<IDirect3DTexture9> u;
        Com<IDirect3DSurface9> a, sys, u0;
        const RECT fromU = {7, 5, 30, 28};
        CK(CreateTexture, 64, 64, 1, 0, X8, D3DPOOL_DEFAULT, &u.p, nullptr);
        CK(CreateOffscreenPlainSurface, 64, 64, X8, D3DPOOL_SYSTEMMEM, &sys.p, nullptr);
        return fresh(a) && levelOf(u.p, 0, u0) &&
               twoTone(sys.p, 64, alpha40(KSTEEL), {5, 3, 34, 32}, alpha40(KRUST)) &&
               updateSurface(sys.p, nullptr, u0.p, nullptr) && copyTo(u0.p, &fromU, a.p, 26, 31) &&
               copiedHas("A", a.p, 26, 31, 23, 23, KRUST, aqua);
    }) || failIn("a usage-0 X8R8G8B8 texture");
}
// sq-swizzle: P, a default-pool plain surface written under a lock (raw texels): A4R4G4B4 0x9c63 and L8 0x5a, each
// where CheckDeviceFormat offers it ("d3d9 sq-swizzle-<a4r4g4b4|l8> skip" where not); its (5,4)-(26,27) to A's (30,31)
// (A, a target, cleared steel with alpha 0x40), 1:1, read by copiedHas: the D3D colours, (0xcc, 0x66, 0x33) alpha 0x99
// and (0x5a, 0x5a, 0x5a) alpha 0xff. The texels read raw would give (0x99, 0xcc, 0x66) alpha 0x33 and (0x5a, 0, 0)
// alpha 0xff: each differs in a channel the format's swizzle moves.
static bool rowSqSwizzle() {
    const struct { D3DFORMAT f; int bpp; DWORD fill, want; const char *skip, *name; } fmts[] = {
        {D3DFMT_A4R4G4B4, 2, 0x9c63, D3DCOLOR_ARGB(0x99, 0xcc, 0x66, 0x33), "sq-swizzle-a4r4g4b4", "A4R4G4B4"},
        {D3DFMT_L8, 1, 0x5a, D3DCOLOR_ARGB(0xff, 0x5a, 0x5a, 0x5a), "sq-swizzle-l8", "L8"}};
    const RECT rect = {5, 4, 26, 27};
    for (const auto &k : fmts) {
        if (!offered(D3DRTYPE_SURFACE, k.f, k.skip)) continue;
        if (!until([&] {
                Com<IDirect3DSurface9> p, a;
                CK(CreateOffscreenPlainSurface, 64, 64, k.f, D3DPOOL_DEFAULT, &p.p, nullptr);
                return target64(a, FALSE) && cleared(a.p, alpha40(KSTEEL)) && twoTone(p.p, 64, k.fill, {}, 0, k.bpp) &&
                       copyTo(p.p, &rect, a.p, 30, 31) &&
                       copiedHas("A", a.p, 30, 31, 21, 23, k.want, alpha40(KSTEEL));
            }))
            return failIn(k.name);
    }
    return true;
}
// YV12 and NV12, the planar YUV formats (no D3DFORMAT names them).
static const D3DFORMAT FMT_YV12 = (D3DFORMAT)MAKEFOURCC('Y', 'V', '1', '2'),
                       FMT_NV12 = (D3DFORMAT)MAKEFOURCC('N', 'V', '1', '2');
struct Yuv { BYTE y, u, v; };
// The w x h YUV plain surface `s` (format f: YUY2, UYVY, YV12 or NV12) written under a lock as Wine's yuv_layout_test
// writes it, `in` over r (its edges even) and `out` elsewhere: YUY2's macropixel is the bytes Y U Y V and UYVY's
// U Y V Y, two texels at 4 bytes; YV12 and NV12 have a byte of luma a texel, then, from pitch x h on, YV12's V plane
// and its U plane (h / 2 rows each, pitch / 2 apart) and NV12's U and V pairs (rows pitch apart), one a 2x2 block.
static bool yuvPaint(IDirect3DSurface9 *s, D3DFORMAT f, int w, int h, const RECT &r, Yuv out, Yuv in) {
    return underLock(s, "LockRect (YUV)", nullptr, 0, [&](BYTE *b, int pitch) {
        BYTE *const chroma = b + pitch * h, *const u12 = chroma + h / 2 * pitch / 2;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x += 2) {
                const Yuv k = x >= r.left && x < r.right && y >= r.top && y < r.bottom ? in : out;
                BYTE *const m = b + y * pitch;
                if (f == D3DFMT_YUY2) m[2 * x] = k.y, m[2 * x + 1] = k.u, m[2 * x + 2] = k.y, m[2 * x + 3] = k.v;
                else if (f == D3DFMT_UYVY) m[2 * x] = k.u, m[2 * x + 1] = k.y, m[2 * x + 2] = k.v, m[2 * x + 3] = k.y;
                else {
                    const int half = y / 2 * (pitch / 2) + x / 2, pair = y / 2 * pitch + x / 2 * 2;
                    m[x] = m[x + 1] = k.y;
                    if (f == FMT_YV12) u12[half] = k.u, chroma[half] = k.v;
                    else chroma[pair] = k.u, chroma[pair + 1] = k.v;
                }
            }
    });
}
// sq-yuv: S, a 32x32 default-pool plain surface of YUY2, UYVY, YV12 and NV12 in turn, each where CheckDeviceFormat
// offers it and CheckDeviceFormatConversion says it converts into A8R8G8B8 ("d3d9 sq-yuv-<format> skip" where not),
// written by yuvPaint: Y 0xb4, U 0x60, V 0xa0 around (10,6)-(24,20) and Y 0x50, U 0xa8, V 0x70 over it (Y, U and V
// pairwise distinct, so a swapped pair or byte order decodes to other colours). Both decodes (mtld3d's shader, BT.601's
// reduced range: (Y - 0.063) * 1.164 with the chroma about 0.5; the CPU codec's 16.16 form) give (0xf3, 0xb1, 0x7f)
// around (242.69, 176.75, 127.28 before rounding) and (0x32, 0x47, 0x9c) over it (49.68, 71.15, 156.11), alpha 0xff,
// each within 1. S whole into a 32x32 target, NONE: around at (0,0), (9,6), (24,19), (10,5), (10,20) and (31,31), over
// at (10,6), (23,19) and (16,12). S whole into a target, POINT (2x): around at (0,0), (19,12), (48,39), (20,11),
// (20,40) and (63,63), over at (20,12), (47,39) and (33,25).
static bool rowSqYuv() {
    const struct { D3DFORMAT f; const char *skip, *name; } fmts[] = {{D3DFMT_YUY2, "sq-yuv-yuy2", "YUY2"},
                                                                      {D3DFMT_UYVY, "sq-yuv-uyvy", "UYVY"},
                                                                      {FMT_YV12, "sq-yuv-yv12", "YV12"},
                                                                      {FMT_NV12, "sq-yuv-nv12", "NV12"}};
    const Yuv out = {0xb4, 0x60, 0xa0}, in = {0x50, 0xa8, 0x70};
    const DWORD a = D3DCOLOR_XRGB(0xf3, 0xb1, 0x7f), o = D3DCOLOR_XRGB(0x32, 0x47, 0x9c), m = WITH_ALPHA;
    const RECT area = {10, 6, 24, 20};
    for (const auto &k : fmts) {
        if (!offered(D3DRTYPE_SURFACE, k.f, k.skip) || !converts(k.f, A8, k.skip)) continue;
        if (!until([&] {
                Com<IDirect3DSurface9> s, one, two;
                CK(CreateOffscreenPlainSurface, 32, 32, k.f, D3DPOOL_DEFAULT, &s.p, nullptr);
                CK(CreateRenderTarget, 32, 32, A8, D3DMULTISAMPLE_NONE, 0, FALSE, &one.p, nullptr);
                return target64(two, FALSE) && yuvPaint(s.p, k.f, 32, 32, area, out, in) &&
                       stretchTo(s.p, nullptr, one.p, nullptr, D3DTEXF_NONE) &&
                       rtHas("1:1", one.p, 32, A8,
                             {{0, 0, a, m}, {9, 6, a, m}, {24, 19, a, m}, {10, 5, a, m}, {10, 20, a, m},
                              {31, 31, a, m}, {10, 6, o, m}, {23, 19, o, m}, {16, 12, o, m}}) &&
                       stretchTo(s.p, nullptr, two.p, nullptr, D3DTEXF_POINT) &&
                       rtHas("2x", two.p, 64, A8,
                             {{0, 0, a, m}, {19, 12, a, m}, {48, 39, a, m}, {20, 11, a, m}, {20, 40, a, m},
                              {63, 63, a, m}, {20, 12, o, m}, {47, 39, o, m}, {33, 25, o, m}});
            }))
            return failIn(k.name);
    }
    return true;
}
// sq-ms: A, a target, cleared steel, a rust quad over (7,5)-(32,31), whole into M, a 4-sample target, M then read
// through the whole resolve (msHas): steel at (2,2) and (40,40), rust at (20,18). N, a 4-sample target, cleared aqua;
// A's (9,7)-(30,29) (rust) to N's (33,18), 1:1, N resolved: rust at (33,18), (53,39) and (43,28), aqua at (54,18),
// (33,17), (32,18), (33,40) and (0,0). P, a 4-sample target, cleared sage, a plum quad over (2,4)-(40,44); B, a target,
// cleared gold: P's (6,8)-(27,30) (plum, 2 texels or more inside the quad; P's origin is sage) to B's (33,18), 1:1:
// plum at (33,18), (53,39) and (43,28), gold at (54,18), (33,17), (32,18), (33,40) and (0,0); B cleared gold again,
// P's (6,8)-(21,23) into B's (20,22)-(50,52), POINT (2x): plum at (20,22), (49,51) and (35,37), gold at (50,22),
// (20,21), (19,22), (20,52) and (0,0). P whole into Q, a 4-sample target, Q resolved: plum at (20,20), sage at (50,50)
// and (60,2).
static bool rowSqMs() {
    return until([&] {
        Com<IDirect3DSurface9> a, b, m, n, p, q;
        const RECT toN = {9, 7, 30, 29}, fromP = {6, 8, 27, 30}, half = {6, 8, 21, 23}, twice = {20, 22, 50, 52};
        if (!target64(a, FALSE) || !target64(b, FALSE) || !msRt(m) || !msRt(n) || !msRt(p) || !msRt(q) ||
            !drawnTwo(a.p, KSTEEL, {7, 5, 32, 31}, KRUST) || !stretchTo(a.p, nullptr, m.p, nullptr, D3DTEXF_NONE) ||
            !msHas("M", m.p, {{2, 2, KSTEEL}, {40, 40, KSTEEL}, {20, 18, KRUST}}) || !cleared(n.p, KAQUA) ||
            !copyTo(a.p, &toN, n.p, 33, 18) ||
            !msHas("N", n.p,
                   {{33, 18, KRUST}, {53, 39, KRUST}, {43, 28, KRUST}, {54, 18, KAQUA}, {33, 17, KAQUA},
                    {32, 18, KAQUA}, {33, 40, KAQUA}, {0, 0, KAQUA}}) ||
            !drawnTwo(p.p, KSAGE, {2, 4, 40, 44}, KPLUM) || !cleared(b.p, KGOLD) || !copyTo(p.p, &fromP, b.p, 33, 18) ||
            !rtHas("B", b.p, 64, A8,
                   {{33, 18, KPLUM}, {53, 39, KPLUM}, {43, 28, KPLUM}, {54, 18, KGOLD}, {33, 17, KGOLD},
                    {32, 18, KGOLD}, {33, 40, KGOLD}, {0, 0, KGOLD}}))
            return false;
        return cleared(b.p, KGOLD) && stretchTo(p.p, &half, b.p, &twice, D3DTEXF_POINT) &&
               rtHas("B, 2x", b.p, 64, A8,
                     {{20, 22, KPLUM}, {49, 51, KPLUM}, {35, 37, KPLUM}, {50, 22, KGOLD}, {20, 21, KGOLD},
                      {19, 22, KGOLD}, {20, 52, KGOLD}, {0, 0, KGOLD}}) &&
               stretchTo(p.p, nullptr, q.p, nullptr, D3DTEXF_NONE) &&
               msHas("Q", q.p, {{20, 20, KPLUM}, {50, 50, KSAGE}, {60, 2, KSAGE}});
    });
}
// sq-overlap's and sq-plain's pattern, drawn on the bound target (fourDrawn) or written under a lock (fourPainted):
// lilac, and over (4,6)-(37,38) four rects, rust (4,6)-(20,18), mint (20,6)-(37,18), gold (4,18)-(20,38) and aqua
// (20,18)-(37,38).
static bool fourDrawn() {
    return clearTo(KLILAC) && zquad(4, 6, 20, 18, 0.5f, KRUST) && zquad(20, 6, 37, 18, 0.5f, KMINT) &&
           zquad(4, 18, 20, 38, 0.5f, KGOLD) && zquad(20, 18, 37, 38, 0.5f, KAQUA);
}
static void fourPainted(BYTE *b, int p) {
    paint(b, p, 0, 0, 64, 64, KLILAC), paint(b, p, 4, 6, 20, 18, KRUST), paint(b, p, 20, 6, 37, 18, KMINT);
    paint(b, p, 4, 18, 20, 38, KGOLD), paint(b, p, 20, 18, 37, 38, KAQUA);
}
// read(px) of the pattern after its (4,6)-(37,38) was copied onto (20,18) (x 20..52, y 18..49) as through a buffer:
// each rect moved by (16,12), rust at (20,18) and (35,29), mint at (36,18) and (52,29), gold at (20,30) and (35,49),
// aqua at (36,30), (44,40) and (52,49) (the source's (20,18), (28,28) and (36,37), inside the destination: a copy in
// place, row by row, would read rust at (36,30) and (44,40)); outside the destination the pattern as it was: rust at
// (4,6), mint at (36,6) and (20,17), gold at (19,18) and (19,37), lilac at (53,18), (20,50), (53,30) and (0,0).
template <class R> static bool movedHas(R read) {
    return read({{20, 18, KRUST}, {35, 29, KRUST}, {36, 18, KMINT}, {52, 29, KMINT}, {20, 30, KGOLD}, {35, 49, KGOLD},
                 {36, 30, KAQUA}, {44, 40, KAQUA}, {52, 49, KAQUA}, {4, 6, KRUST}, {36, 6, KMINT}, {20, 17, KMINT},
                 {19, 18, KGOLD}, {19, 37, KGOLD}, {53, 18, KLILAC}, {20, 50, KLILAC}, {53, 30, KLILAC},
                 {0, 0, KLILAC}});
}
// sq-overlap (one subresource, or one texture, at both ends): A, a target, in the pattern; its (4,6)-(37,38) onto its
// own (20,18), overlapping: movedHas. The back buffer in the pattern; an additional swap chain's back buffer's
// (4,6)-(37,38) (the device's, aliased) onto the device's at (20,18): movedHas of the device's (colour channels). T, a
// 64x64 render-target texture of two levels: level 0 cleared steel, a rust quad over (8,6)-(24,38) and a gold one over
// (24,6)-(40,38), level 1 cleared aqua; level 0's (8,6)-(38,36) onto level 1's (3,4)-(18,19), POINT (a 2x downscale:
// level 1's texel 3 + i samples level 0's 9 + 2i, between two texels of one colour): rust at (3,4) and (10,18), gold at
// (11,4) and (17,18), aqua at (18,4), (3,3), (2,4), (3,19) and (0,0).
static bool rowSqOverlap() {
    return until([&] {
        Com<IDirect3DSwapChain9> sc;
        Com<IDirect3DTexture9> t;
        Com<IDirect3DSurface9> a, chain, t0, t1;
        const RECT rect = {4, 6, 37, 38}, from = {8, 6, 38, 36}, to = {3, 4, 18, 19};
        if (!target64(a, FALSE) || !chainBackBuffer(nullptr, 0, sc, chain) || !makeRt(t, 64, 2) ||
            !levelOf(t.p, 0, t0) || !levelOf(t.p, 1, t1) ||
            !scene([&] { return bindRt(0, a.p) && fourDrawn() && backToScreen(); }) ||
            !copyTo(a.p, &rect, a.p, 20, 18) ||
            !movedHas([&](std::initializer_list<Tx> px) { return rtHas("A", a.p, 64, A8, px); }) ||
            !scene([&] { return backToScreen() && fourDrawn(); }) || !copyTo(chain.p, &rect, rtSurf, 20, 18) ||
            !movedHas([&](std::initializer_list<Tx> px) { return rtHas("the back buffer", rtSurf, 256, X8, px); }))
            return false;
        return scene([&] {
                   return clearRt(t0.p, KSTEEL) && zquad(8, 6, 24, 38, 0.5f, KRUST) &&
                          zquad(24, 6, 40, 38, 0.5f, KGOLD) && clearRt(t1.p, KAQUA) && backToScreen();
               }) &&
               stretchTo(t0.p, &from, t1.p, &to, D3DTEXF_POINT) &&
               rtHas("level 1", t1.p, 32, A8,
                     {{3, 4, KRUST}, {10, 18, KRUST}, {11, 4, KGOLD}, {17, 18, KGOLD}, {18, 4, KAQUA}, {3, 3, KAQUA},
                      {2, 4, KAQUA}, {3, 19, KAQUA}, {0, 0, KAQUA}});
    });
}
// sq-plain (a plain surface onto itself): P, a plain surface, written in the pattern under a lock. Its (5,7)-(18,16)
// (rust) onto its own (40,52), disjoint, read under a READONLY lock: rust at (40,52), (52,60) and (46,56), lilac at
// (53,52), (40,51), (39,52) and (40,61), the source kept (rust at (5,7)). Its (4,6)-(37,38) onto its own (20,18),
// overlapping: movedHas under a READONLY lock. P whole into A, a target: movedHas, and rust at (40,52) and (52,60)
// (both copies reach the GPU).
static bool rowSqPlain() {
    return until([&] {
        Com<IDirect3DSurface9> p, a;
        const RECT disjoint = {5, 7, 18, 16}, rect = {4, 6, 37, 38};
        return plain64(p) && target64(a, FALSE) && underLock(p.p, "LockRect (write)", nullptr, 0, fourPainted) &&
               copyTo(p.p, &disjoint, p.p, 40, 52) &&
               lockHas("P, disjoint", p.p,
                       {{40, 52, KRUST}, {52, 60, KRUST}, {46, 56, KRUST}, {53, 52, KLILAC}, {40, 51, KLILAC},
                        {39, 52, KLILAC}, {40, 61, KLILAC}, {5, 7, KRUST}}) &&
               copyTo(p.p, &rect, p.p, 20, 18) &&
               movedHas([&](std::initializer_list<Tx> px) { return lockHas("P, overlapping", p.p, px); }) &&
               copyTo(p.p, nullptr, a.p) &&
               movedHas([&](std::initializer_list<Tx> px) { return rtHas("A", a.p, 64, A8, px); }) &&
               rtHas("A, disjoint", a.p, 64, A8, {{40, 52, KRUST}, {52, 60, KRUST}});
    });
}
// sq-order, in one scene (sc-order's, its copies scaled 2x, POINT): A and B, targets, B cleared aqua, A steel; A's
// (5,7)-(15,17) into B's (30,20)-(50,40), then a rust quad over A's (0,0)-(40,40). Y's level 1 (a 64x64 render-target
// texture's, 32x32) cleared lilac, a gold quad over (2,2)-(20,20), A's (44,42)-(51,50) (steel; A's origin is rust by
// now) into (10,9)-(24,25) over part of it, a rose quad over (26,2)-(31,30) beside it; the same on face 3 (-y) of a
// 32-texel render-target cube: sky, plum, the copy, sage. B bound, a mint quad over (36,46)-(60,62) drawn wireframe
// (D3DRS_FILLMODE solid again at once, whatever the draw answered), then A's (46,44)-(57,52) into B's (6,46)-(28,62):
// the copy fills its rect whatever fill mode the draw before it on B had. B: steel at (30,20), (49,39) and (40,30) (A
// as the Clear left it: the quad after the copy is not in it), aqua at (50,20), (30,19), (29,20), (30,40) and (0,0);
// steel at (6,46), (27,61), (10,50), (16,54) and (24,58) (but (6,46), on its left and top edges, inside the copy's one
// triangle, which a wireframe pass leaves aqua), aqua at (28,50), (6,45), (5,46) and (6,62). A: rust at (10,10), steel
// at (50,50). Y's level 1 and the face: the first quad at (4,4) and (9,9) (left of the copy); steel at (10,9), (12,11)
// (over the first quad) and (23,24); the second quad at (28,15); the clear at (24,12), (10,25) and (0,0).
static bool rowSqOrder() {
    return until([&] {
        Com<IDirect3DTexture9> y;
        Com<IDirect3DCubeTexture9> c;
        Com<IDirect3DSurface9> a, b, y1, f3;
        const RECT first = {5, 7, 15, 17}, firstTo = {30, 20, 50, 40}, mid = {44, 42, 51, 50},
                   midTo = {10, 9, 24, 25}, wire = {46, 44, 57, 52}, wireTo = {6, 46, 28, 62};
        CK(CreateCubeTexture, 32, 1, D3DUSAGE_RENDERTARGET, A8, D3DPOOL_DEFAULT, &c.p, nullptr);
        if (!target64(a, FALSE) || !target64(b, FALSE) || !makeRt(y, 64, 2) || !levelOf(y.p, 1, y1) ||
            !faceOf(c.p, D3DCUBEMAP_FACE_NEGATIVE_Y, f3))
            return false;
        // t bound, cleared, a first quad, the scaled copy over part of it, a second quad beside it.
        const auto around = [&](IDirect3DSurface9 *t, DWORD clear, DWORD one, DWORD two) {
            return clearRt(t, clear) && zquad(2, 2, 20, 20, 0.5f, one) &&
                   stretchTo(a.p, &mid, t, &midTo, D3DTEXF_POINT) && zquad(26, 2, 31, 30, 0.5f, two);
        };
        const auto has = [&](const char *name, IDirect3DSurface9 *t, DWORD clear, DWORD one, DWORD two) {
            return rtHas(name, t, 32, A8,
                         {{4, 4, one}, {9, 9, one}, {10, 9, KSTEEL}, {12, 11, KSTEEL}, {23, 24, KSTEEL},
                          {28, 15, two}, {24, 12, clear}, {10, 25, clear}, {0, 0, clear}});
        };
        // B bound: the wireframe quad, FILLMODE solid again whatever it answered, then the scaled copy into B.
        const auto wired = [&] {
            CK(SetRenderState, D3DRS_FILLMODE, D3DFILL_WIREFRAME);
            const bool drawn = zquad(36, 46, 60, 62, 0.5f, KMINT);
            CK(SetRenderState, D3DRS_FILLMODE, D3DFILL_SOLID);
            return drawn && stretchTo(a.p, &wire, b.p, &wireTo, D3DTEXF_POINT);
        };
        return scene([&] {
                   return clearRt(b.p, KAQUA) && clearRt(a.p, KSTEEL) &&
                          stretchTo(a.p, &first, b.p, &firstTo, D3DTEXF_POINT) &&
                          zquad(0, 0, 40, 40, 0.5f, KRUST) && around(y1.p, KLILAC, KGOLD, KROSE) &&
                          around(f3.p, KSKY, KPLUM, KSAGE) && bindRt(0, b.p) && wired() && backToScreen();
               }) &&
               rtHas("B", b.p, 64, A8,
                     {{30, 20, KSTEEL}, {49, 39, KSTEEL}, {40, 30, KSTEEL}, {50, 20, KAQUA}, {30, 19, KAQUA},
                      {29, 20, KAQUA}, {30, 40, KAQUA}, {0, 0, KAQUA}, {6, 46, KSTEEL}, {27, 61, KSTEEL},
                      {10, 50, KSTEEL}, {16, 54, KSTEEL}, {24, 58, KSTEEL}, {28, 50, KAQUA}, {6, 45, KAQUA},
                      {5, 46, KAQUA}, {6, 62, KAQUA}}) &&
               rtHas("A", a.p, 64, A8, {{10, 10, KRUST}, {50, 50, KSTEEL}}) &&
               has("Y's level 1", y1.p, KLILAC, KGOLD, KROSE) && has("face 3", f3.p, KSKY, KPLUM, KSAGE);
    });
}
// sq-autogen: G, a 64x64 AUTOGENMIPMAP render-target texture (X8R8G8B8), cleared rust; S, a 32x32 target, cleared aqua;
// the back buffer cleared steel; a 4x4 quad samples G at (100,100) at its level 4 (POINT mip filter): rust at
// (102,102). S whole into G's level 0 (2x), POINT; G at (150,100): aqua at (152,102) (the chain regenerated from the
// copy), rust still at (102,102), steel at (20,20).
static bool rowSqAutogen() {
    return until([&] {
        Com<IDirect3DTexture9> g;
        Com<IDirect3DSurface9> s, g0;
        CK(CreateRenderTarget, 32, 32, A8, D3DMULTISAMPLE_NONE, 0, FALSE, &s.p, nullptr);
        if (!makeRt(g, 64, 0, X8, D3DUSAGE_RENDERTARGET | D3DUSAGE_AUTOGENMIPMAP) || !levelOf(g.p, 0, g0)) return false;
        CK(SetSamplerState, 0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
        return scene([&] {
                   return clearRt(s.p, KAQUA) && target(g.p) && clearTo(KRUST) && backToScreen() && clearTo(KSTEEL) &&
                          show(g.p, 100, 100, 104, 104);
               }) &&
               stretchTo(s.p, nullptr, g0.p, nullptr, D3DTEXF_POINT) &&
               scene([&] { return show(g.p, 150, 100, 154, 104); }) &&
               rtHas("the back buffer", rtSurf, 256, X8, {{102, 102, KRUST}, {152, 102, KAQUA}, {20, 20, KSTEEL}});
    });
}
// One sc-depth pair (DXMTRS C4c-4b): T, a target, bound, cleared steel; D1 and D2, 64x64 depth-stencil surfaces of
// format f: D1 bound, cleared to Z 0.25 (and stencil 3), D2 bound, cleared to Z 1 (and stencil 0), then D1 into the
// bound D2, whole, with no draw between (as depth_blit_test). Z on, D2 bound: a mint quad at z 0.1 over (4,4)-(19,28)
// (under stencil EQUAL 3) draws first, then a rust one at 0.5 over (24,4)-(39,28) (no stencil test) and, with a
// stencil, a gold one at 0.1 under EQUAL 0 over (44,4)-(59,28) do not; D1 bound: an aqua quad at 0.1 over
// (4,36)-(19,60) draws, a rose one at 0.5 over (24,36)-(39,60) does not (the copy read D1 before these draws). Each
// quad on texels of its own: mint at (11,16), aqua at (11,48), steel at (31,16), (51,16) and (31,48). Then D1 onto
// itself, whole: D3D_OK.
static bool depthPair(D3DFORMAT f, bool stencil) {
    Com<IDirect3DSurface9> t, d1, d2;
    const auto clear = [&](float z, DWORD s) { return stencil ? clearZS(z, s) : clearZ(z); };
    const auto test = [&](DWORD func, DWORD ref) {
        return !stencil || (rstate(D3DRS_STENCILENABLE, TRUE) && rstate(D3DRS_STENCILFUNC, func) &&
                            rstate(D3DRS_STENCILREF, ref));
    };
    if (!target64(t, FALSE) || !makeDs(d1, f, 64) || !makeDs(d2, f, 64) || !scene([&] {
            return clearRt(t.p, KSTEEL) && bindDs(d1.p) && clear(0.25f, 3) && bindDs(d2.p) && clear(1.0f, 0);
        }))
        return false;
    CK(StretchRect, d1.p, nullptr, d2.p, nullptr, D3DTEXF_NONE);
    if (!scene([&] {
            return rstate(D3DRS_ZENABLE, D3DZB_TRUE) && test(D3DCMP_EQUAL, 3) && zquad(4, 4, 19, 28, 0.1f, KMINT) &&
                   rstate(D3DRS_STENCILENABLE, FALSE) && zquad(24, 4, 39, 28, 0.5f, KRUST) &&
                   (!stencil || (test(D3DCMP_EQUAL, 0) && zquad(44, 4, 59, 28, 0.1f, KGOLD) &&
                                 rstate(D3DRS_STENCILENABLE, FALSE))) &&
                   bindDs(d1.p) && zquad(4, 36, 19, 60, 0.1f, KAQUA) && zquad(24, 36, 39, 60, 0.5f, KROSE) &&
                   backToScreen();
        }) ||
        !rtHas("T", t.p, 64, A8,
               {{11, 16, KMINT}, {11, 48, KAQUA}, {31, 16, KSTEEL}, {51, 16, KSTEEL}, {31, 48, KSTEEL}}))
        return false;
    return answer("StretchRect (a depth-stencil surface onto itself)",
                  dev->StretchRect(d1.p, nullptr, d1.p, nullptr, D3DTEXF_NONE), D3D_OK);
}
// sc-depth (last): depthPair of D24S8 with its stencil plane, then of D16, depth alone; on every path, passed or not,
// no depth-stencil bound and the depth state as the device has it (it has none: ZENABLE off).
static bool rowScDepth() {
    return until([&] {
        const bool ok = (depthPair(D3DFMT_D24S8, true) || failIn("D24S8")) &&
                        (depthPair(D3DFMT_D16, false) || failIn("D16"));
        depthBack(nullptr, D3DZB_FALSE);
        return ok;
    });
}

static void blitRows(IDirect3DDevice9 *d, bool strict) {
    static const struct { const char *name; bool (*run)(); } rows[] = {
        {"cf-rt", rowCfRt},         {"cf-lockable", rowCfLockable}, {"cf-bb", rowCfBb},
        {"cf-unbound", rowCfUnbound}, {"cf-rtex", rowCfRtex},       {"cf-autogen", rowCfAutogen},
        {"cf-msaa", rowCfMsaa},     {"cf-plain", rowCfPlain},       {"cf-reject", rowCfReject},
        {"us-tex", rowUsTex},       {"us-level", rowUsLevel},       {"us-rtex", rowUsRtex},
        {"us-rt", rowUsRt},         {"us-bb", rowUsBb},             {"us-conv", rowUsConv},
        {"us-reject", rowUsReject}, {"sr-reject", rowSrReject},     {"sd-resolve", rowSdResolve},
        {"sc-rt", rowScRt},         {"sc-bb", rowScBb},             {"sc-lockable", rowScLockable},
        {"sc-rtex", rowScRtex},     {"sc-tex", rowScTex},           {"sc-plain", rowScPlain},
        {"sc-plain2", rowScPlain2}, {"sc-conv", rowScConv},         {"sc-same", rowScSame},
        {"sc-self", rowScSelf},     {"sc-autogen", rowScAutogen},   {"sc-order", rowScOrder},
        {"sq-scale", rowSqScale},   {"sq-level", rowSqLevel},       {"sq-conv", rowSqConv},
        {"sq-swizzle", rowSqSwizzle}, {"sq-yuv", rowSqYuv},         {"sq-ms", rowSqMs},
        {"sq-overlap", rowSqOverlap}, {"sq-plain", rowSqPlain},     {"sq-order", rowSqOrder},
        {"sq-autogen", rowSqAutogen}, {"sc-depth", rowScDepth}};
    dev = d, strictMode = strict, budgetEnd = GetTickCount64() + 180000;
    const HRESULT hr = dev->GetRenderTarget(0, &rtSurf);
    for (const auto &r : rows) {
        why[0] = 0;
        bool ok;
        if (FAILED(hr)) ok = fail("GetRenderTarget", hr);
        else if (GetTickCount64() >= budgetEnd) ok = false, snprintf(why, sizeof why, "budget");
        else ok = reset() && r.run();
        row(r.name, ok, why);
    }
    if (rtSurf) rtSurf->Release();
}

// --- The "queries" mode (DXMTRS C4c-5): IDirect3DQuery9's EVENT and OCCLUSION, on the blit mode's plain device
// (256x256 X8R8G8B8, single-sample, no depth-stencil: qy-count makes and binds one and unbinds it on every path,
// depthBack). Every count is of zquad's pretransformed quads at z 0.5 (on the back buffer unless said), exact: zquad
// puts the strip's vertices at the rect's integer corners, and D3D9 puts a pixel's centre at its integer coordinates,
// so each edge runs through a line of centres, which the top-left rule gives to the quad on its left and top edges and
// to neither side on its right and bottom ones: (x0,y0)-(x1,y1) covers exactly (x1-x0)*(y1-y0) pixels, one sample each,
// and the strip's diagonal gives each centre on it to one of its two triangles (a shift of the whole quad by under half
// a pixel moves opposite edges together and keeps the count, so no half-pixel convention changes it). A row's quads do
// not overlap unless said, and the counts a cell could add up by mistake differ from the one it wants. Counts are
// compared exactly (exact(), not want(), which allows one a byte), read with GetData(&d, 4, 0) after waitFor() (Wine's
// wait_query: GetData(NULL, 0, D3DGETDATA_FLUSH) until S_OK, 5 s at most). Every counting cell is one until() attempt,
// from its BEGIN (and what it draws before it) through the read and the compare: mtld3d leaves a draw out of its frame
// while its pipeline builds unless a query counts, so a quad drawn before a BEGIN (qy-count's occluder) can be missing
// at the first attempt; under strict a cell has one attempt. A cell's query is made in its attempt (qy-reuse's re-BEGUN
// by each) and released by Com on every path. A row stops at its first failing call and says why: a CreateQuery that
// does not answer D3D_OK (the core runtime refuses it, D3DERR_NOTAVAILABLE, until DXMTRS C4c-5's front end) prints
// "FAIL CreateQuery 8876086a" (qy-support's "FAIL CreateQuery (EVENT, NULL) 8876086a") and no query is touched. On the
// core only an application's draws count (D3D9's rule); mtld3d also counts a scaled StretchRect into the bound target
// right after a counted draw (a stated divergence), so no cell puts one there. Waits: a cell returns at its first wait
// that runs out (5 s), so under strict a failing row costs one 5 s wait at most (twelve rows: 60 s); in mtld3d lanes
// until() retries a cell for up to 10 s; the mode's budget is 120 s, the launches' watchdog 240 s.

// A query of type t (OCCLUSION unless said) in q, or false with `why` "CreateQuery <hr>" and q null.
static bool newQuery(Com<IDirect3DQuery9> &q, D3DQUERYTYPE t = D3DQUERYTYPE_OCCLUSION) {
    CK(CreateQuery, t, &q.p);
    return q.p || (snprintf(why, sizeof why, "CreateQuery D3D_OK and no query"), false);
}
// Issue(flags): D3D_OK, else false with `why`.
static bool issue(IDirect3DQuery9 *q, DWORD flags) {
    const HRESULT hr = q->Issue(flags);
    const char *call = flags == D3DISSUE_BEGIN ? "Issue(BEGIN)"
                       : flags == D3DISSUE_END ? "Issue(END)"
                                               : "Issue(BEGIN|END)";
    return hr == D3D_OK || fail(call, hr);
}
// Wine's wait_query: GetData(NULL, 0, D3DGETDATA_FLUSH) until S_OK, Sleep(10) between polls, for 5 s at most (by the
// clock: a FLUSH poll may block in the call); false with `why` at another answer or when the 5 s run out.
static bool waitFor(IDirect3DQuery9 *q) {
    const ULONGLONG end = GetTickCount64() + 5000;
    for (;;) {
        const HRESULT hr = q->GetData(nullptr, 0, D3DGETDATA_FLUSH);
        if (hr == S_OK) return true;
        if (hr != S_FALSE) return fail("GetData (NULL, 0, FLUSH)", hr);
        if (GetTickCount64() >= end)
            return snprintf(why, sizeof why, "GetData (NULL, 0, FLUSH) S_FALSE for 5 s"), false;
        Sleep(10);
    }
}
// Whether `got` is exactly `expect` (a count or a sentinel); `why` names it otherwise.
static bool exact(const char *what, unsigned long long got, unsigned long long expect) {
    if (got == expect) return true;
    snprintf(why, sizeof why, "%s %llu (%#llx) want %llu (%#llx)", what, got, got, expect, expect);
    return false;
}
// GetData(&d, 4, 0): S_OK and d exactly `expect`.
static bool countIs(IDirect3DQuery9 *q, DWORD expect) {
    DWORD d = 0xfefefefe;
    const HRESULT hr = q->GetData(&d, sizeof d, 0);
    return hr == S_OK ? exact("count", d, expect) : fail("GetData (4 bytes)", hr);
}
// BEGIN, body(), END, the wait, the count: exactly `expect`.
template <class F> static bool bracket(IDirect3DQuery9 *q, DWORD expect, F body) {
    return issue(q, D3DISSUE_BEGIN) && body() && issue(q, D3DISSUE_END) && waitFor(q) && countIs(q, expect);
}
// A quad over (x0,y0)-(x1,y1) at z 0.5 in a scene of its own.
static bool quadIn(float x0, float y0, float x1, float y1, DWORD c = KRUST) {
    return scene([&] { return zquad(x0, y0, x1, y1, 0.5f, c); });
}
static bool presentNow() {
    CK(Present, nullptr, nullptr, nullptr, nullptr);
    return true;
}
// GetData(&b, 4, 0) of an EVENT query: S_OK with b TRUE.
static bool signalled(IDirect3DQuery9 *q, const char *what) {
    BOOL b = 7;
    const HRESULT hr = q->GetData(&b, sizeof b, 0);
    if (hr == S_OK && b == TRUE) return true;
    return snprintf(why, sizeof why, "%s: GetData %08lx, BOOL %d", what, (unsigned long)hr, (int)b), false;
}

// qy-support (no draw, no until()): for EVENT and OCCLUSION, CreateQuery(t, NULL), the support probe, D3D_OK, and
// CreateQuery(t, &q) D3D_OK: the device's reference count one up while q lives (the query holds the device),
// GetType t, GetDataSize 4 (a BOOL, a DWORD count), GetDevice D3D_OK and the device (that reference given back at
// once), and after q's Release the count back where it was. For type 3 (no type), VCACHE, VERTEXSTATS, TIMESTAMP,
// TIMESTAMPDISJOINT and TIMESTAMPFREQ: both calls D3DERR_NOTAVAILABLE, the out pointer still 0xdeadbeef (Wine's
// test_query_support's answers; mtld3d makes EVENT and OCCLUSION alone).
static bool rowQySupport() {
    for (const D3DQUERYTYPE t : {D3DQUERYTYPE_EVENT, D3DQUERYTYPE_OCCLUSION}) {
        const char *name = t == D3DQUERYTYPE_EVENT ? "EVENT" : "OCCLUSION";
        char what[64];
        snprintf(what, sizeof what, "CreateQuery (%s, NULL)", name);
        HRESULT hr = dev->CreateQuery(t, nullptr);
        if (hr != D3D_OK) return fail(what, hr);
        const ULONG before = refs(dev);
        Com<IDirect3DQuery9> q;
        if (!newQuery(q, t)) return failIn(name);
        const ULONG with = refs(dev);
        if (with != before + 1)
            return snprintf(why, sizeof why, "%s: the device's references %lu with the query, %lu before", name,
                            (unsigned long)with, (unsigned long)before),
                   false;
        if (q.p->GetType() != t)
            return snprintf(why, sizeof why, "%s: GetType %d", name, (int)q.p->GetType()), false;
        if (q.p->GetDataSize() != 4)
            return snprintf(why, sizeof why, "%s: GetDataSize %lu", name, (unsigned long)q.p->GetDataSize()), false;
        IDirect3DDevice9 *got = nullptr;
        hr = q.p->GetDevice(&got);
        if (got) got->Release();
        if (hr != D3D_OK) return fail("GetDevice", hr), failIn(name);
        if (got != dev) return snprintf(why, sizeof why, "%s: GetDevice another device", name), false;
        q.p->Release(), q.p = nullptr;
        const ULONG after = refs(dev);
        if (after != before)
            return snprintf(why, sizeof why, "%s: the device's references %lu after the query's Release, %lu before",
                            name, (unsigned long)after, (unsigned long)before),
                   false;
    }
    for (const DWORD t : {(DWORD)3, (DWORD)D3DQUERYTYPE_VCACHE, (DWORD)D3DQUERYTYPE_VERTEXSTATS,
                          (DWORD)D3DQUERYTYPE_TIMESTAMP, (DWORD)D3DQUERYTYPE_TIMESTAMPDISJOINT,
                          (DWORD)D3DQUERYTYPE_TIMESTAMPFREQ}) {
        IDirect3DQuery9 *const sentinel = (IDirect3DQuery9 *)(ULONG_PTR)0xdeadbeef;
        IDirect3DQuery9 *q = sentinel;  // never a Com: a runtime that left it must not see a Release
        char what[64];
        snprintf(what, sizeof what, "CreateQuery (type %lu, NULL)", (unsigned long)t);
        HRESULT hr = dev->CreateQuery((D3DQUERYTYPE)t, nullptr);
        if (hr != D3DERR_NOTAVAILABLE) return fail(what, hr);
        snprintf(what, sizeof what, "CreateQuery (type %lu)", (unsigned long)t);
        hr = dev->CreateQuery((D3DQUERYTYPE)t, &q);
        if (SUCCEEDED(hr) && q && q != sentinel) q->Release();  // a query made where none may be
        if (hr != D3DERR_NOTAVAILABLE) return fail(what, hr);
        if (q != sentinel) return snprintf(why, sizeof why, "%s: the out pointer written", what), false;
    }
    return true;
}
// qy-event (no count, no until()): an EVENT query never issued: GetData(&b, 4, 0) S_OK, b TRUE. A quad over
// (16,16)-(48,48), Issue(END), the wait (FLUSH): GetData(&b, 4, 0) S_OK, b TRUE; GetData(NULL, 0, 0) S_OK. Another
// quad over (64,16)-(96,48), Issue(END), then GetData(&b, 4, 0) polled with no flag and no Present (Sleep(10) between
// polls): S_OK with b TRUE within 5 s (a poll submits what is recorded while the END is in it, flag or not: mtld3d's
// rule, which a title polling without the flag and without a Present needs). Nothing in the row presents or reads back:
// even the first wait finishes only because a poll submits (EndScene does not).
static bool rowQyEvent() {
    Com<IDirect3DQuery9> q;
    if (!newQuery(q, D3DQUERYTYPE_EVENT) || !signalled(q.p, "never issued")) return false;
    if (!quadIn(16, 16, 48, 48) || !issue(q.p, D3DISSUE_END) || !waitFor(q.p)) return failIn("the first END");
    if (!signalled(q.p, "the first END")) return false;
    const HRESULT hr = q.p->GetData(nullptr, 0, 0);
    if (hr != S_OK) return fail("the first END: GetData (NULL, 0, 0)", hr);
    if (!quadIn(64, 16, 96, 48) || !issue(q.p, D3DISSUE_END)) return failIn("the second END");
    for (const ULONGLONG end = GetTickCount64() + 5000;;) {
        BOOL b = 7;
        const HRESULT h = q.p->GetData(&b, sizeof b, 0);
        if (h == S_OK) return b == TRUE || (snprintf(why, sizeof why, "the second END: S_OK, BOOL %d", (int)b), false);
        if (h != S_FALSE) return fail("the second END: GetData (no flag)", h);
        if (GetTickCount64() >= end)
            return snprintf(why, sizeof why, "the second END: GetData (no flag, no Present) S_FALSE for 5 s"), false;
        Sleep(10);
    }
}
// qy-count: BEGIN, a quad over (32,32)-(96,64) (64x32: 2048), END, the wait: GetData(&d, 4, 0) S_OK, 2048; size 2: the
// low WORD 2048, the next WORD still 0xffff; size 8: 2048 and a high DWORD 0 (over 0xffffffff); size 0: S_OK, the
// DWORD still 0xffffffff. Then a 256x256 D24S8 depth-stencil bound, ZENABLE on (ZFUNC LESSEQUAL, Z writes on: the
// defaults), its depth cleared to 1: an occluder (mint) over (16,16)-(64,80) at z 0.1, drawn in a scene before BEGIN,
// then the same quad at z 0.5 inside the span: 1024 (its columns 32..63 fail the test, 64..95 pass: 32x32; the
// occluder's right edge, x 64, runs through the column of centres the top-left rule leaves to the quad, its other
// edges lie outside the quad; 2048 if the occluder was left out). Then the proxy draw, the target cleared steel with
// the depth: the occluder again, then, colour writes off (COLORWRITEENABLE 0) and Z writes off, the Z test on, BEGIN,
// the same quad in rust, END: 1024, and the back buffer still steel at (80,48) (the quad's passing half) and mint at
// (40,48) (behind it, the occluder's), 16 pixels or more from every edge. Each count its own until() attempt; each
// depth attempt, on every path, puts the colour and Z writes back on, binds no depth-stencil and turns ZENABLE off (as
// the device has it: no depth-stencil, depthBack).
static bool rowQyCount() {
    const bool sizes = until([&] {
        Com<IDirect3DQuery9> q;
        WORD w[2] = {0xffff, 0xffff};
        DWORD d8[2] = {0xffffffff, 0xffffffff}, z = 0xffffffff;
        if (!newQuery(q)) return false;
        if (!bracket(q.p, 2048, [] { return quadIn(32, 32, 96, 64); })) return failIn("size 4");
        HRESULT hr = q.p->GetData(w, 2, 0);
        if (hr != S_OK) return fail("GetData (2 bytes)", hr);
        if (!exact("size 2: the low WORD", w[0], 2048) || !exact("size 2: the next WORD", w[1], 0xffff)) return false;
        hr = q.p->GetData(d8, 8, 0);
        if (hr != S_OK) return fail("GetData (8 bytes)", hr);
        if (!exact("size 8: the low DWORD", d8[0], 2048) || !exact("size 8: the high DWORD", d8[1], 0)) return false;
        hr = q.p->GetData(&z, 0, 0);
        if (hr != S_OK) return fail("GetData (0 bytes)", hr);
        return exact("size 0: the buffer", z, 0xffffffff);
    });
    if (!sizes) return false;
    Com<IDirect3DSurface9> ds;
    if (!makeDs(ds, D3DFMT_D24S8)) return false;
    // In a scene: ds bound, Z on, the planes of `flags` cleared (the target steel, the depth 1), the occluder drawn.
    const auto occluder = [&](DWORD flags) {
        return scene([&] {
            return bindDs(ds.p) && rstate(D3DRS_ZENABLE, D3DZB_TRUE) && clearRects(0, nullptr, flags, KSTEEL) &&
                   zquad(16, 16, 64, 80, 0.1f, KMINT);
        });
    };
    return until([&] {
               Com<IDirect3DQuery9> q;
               const bool ok = newQuery(q) &&
                               ((occluder(D3DCLEAR_ZBUFFER) &&
                                 bracket(q.p, 1024, [] { return quadIn(32, 32, 96, 64); })) ||
                                failIn("behind an occluder"));
               depthBack(nullptr, D3DZB_FALSE);
               return ok;
           }) &&
           until([&] {
               Com<IDirect3DQuery9> q;
               const auto proxy = [] {
                   const bool drawn = scene([] {
                       return rstate(D3DRS_COLORWRITEENABLE, 0) && rstate(D3DRS_ZWRITEENABLE, FALSE) &&
                              zquad(32, 32, 96, 64, 0.5f, KRUST);
                   });
                   dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xf);
                   return drawn;
               };
               const bool ok = newQuery(q) &&
                               ((occluder(D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER) && bracket(q.p, 1024, proxy) &&
                                 rtHas("the back buffer", rtSurf, 256, X8, {{80, 48, KSTEEL}, {40, 48, KMINT}})) ||
                                failIn("the proxy draw"));
               dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xf);
               depthBack(nullptr, D3DZB_FALSE);
               return ok;
           });
}
// qy-empty: spans with no draw, each cell its own until() attempt and query. Never issued: GetData(NULL, 0, FLUSH)
// S_OK, then GetData(&d, 4, FLUSH) S_OK with d[0] 0xdddddddd and d[1] still 0xffffffff (Wine's). END first (a query
// never begun): D3D_OK, the wait, 0. BEGIN, END: 0. BEGIN|END in one Issue: 0. Last, 400 BEGIN/END pairs of one query
// in one scene and the wait right after them: 0 (400, not Wine's 1000: on mtld3d a BEGIN and an END each take one of a
// frame's 1024 result slots; the cell before presents, so the pairs start a frame).
static bool rowQyEmpty() {
    return until([&] {
               Com<IDirect3DQuery9> q;
               DWORD d[2] = {0xffffffff, 0xffffffff};
               if (!newQuery(q)) return false;
               HRESULT hr = q.p->GetData(nullptr, 0, D3DGETDATA_FLUSH);
               if (hr != S_OK) return fail("never issued: GetData (NULL, 0, FLUSH)", hr);
               hr = q.p->GetData(d, 4, D3DGETDATA_FLUSH);
               if (hr != S_OK) return fail("never issued: GetData (4 bytes, FLUSH)", hr);
               return exact("never issued: the DWORD", d[0], 0xdddddddd) &&
                      exact("never issued: the next DWORD", d[1], 0xffffffff);
           }) &&
           until([&] {
               Com<IDirect3DQuery9> q;
               return newQuery(q) &&
                      ((issue(q.p, D3DISSUE_END) && waitFor(q.p) && countIs(q.p, 0)) || failIn("END first"));
           }) &&
           until([&] {
               Com<IDirect3DQuery9> q;
               return newQuery(q) && (bracket(q.p, 0, [] { return true; }) || failIn("BEGIN, END"));
           }) &&
           until([&] {
               Com<IDirect3DQuery9> q;
               return newQuery(q) && ((issue(q.p, D3DISSUE_BEGIN | D3DISSUE_END) && waitFor(q.p) && countIs(q.p, 0)) ||
                                      failIn("BEGIN|END"));
           }) &&
           until([&] {
               Com<IDirect3DQuery9> q;
               const auto pairs = [&] {
                   for (int i = 0; i < 400; i++)
                       if (!issue(q.p, D3DISSUE_BEGIN) || !issue(q.p, D3DISSUE_END)) return false;
                   return true;
               };
               return newQuery(q) && ((scene(pairs) && waitFor(q.p) && countIs(q.p, 0)) || failIn("400 pairs"));
           });
}
// qy-open: BEGIN, a quad over (100,100)-(148,124) (48x24: 1152), then, the span still open, GetData(&d, 4, FLUSH):
// S_FALSE and d still 0xffffffff (its END is not recorded: nothing to wait for); END, the wait: 1152.
static bool rowQyOpen() {
    return until([&] {
        Com<IDirect3DQuery9> q;
        DWORD d = 0xffffffff;
        if (!newQuery(q) || !issue(q.p, D3DISSUE_BEGIN) || !quadIn(100, 100, 148, 124)) return false;
        const HRESULT hr = q.p->GetData(&d, sizeof d, D3DGETDATA_FLUSH);
        if (hr != S_FALSE) return fail("open: GetData (4 bytes, FLUSH)", hr);
        return exact("open: the buffer", d, 0xffffffff) && issue(q.p, D3DISSUE_END) && waitFor(q.p) &&
               countIs(q.p, 1152);
    });
}
// qy-twice: BEGIN, quad A over (16,16)-(80,48) (64x32: 2048), BEGIN again (the span restarts), quad B over
// (120,120)-(150,140) (30x20: 600), END, the wait: 600 (2648 if A stayed in); END again: D3D_OK, and GetData(&d, 4, 0),
// no wait, still 600.
static bool rowQyTwice() {
    return until([&] {
        Com<IDirect3DQuery9> q;
        return newQuery(q) && issue(q.p, D3DISSUE_BEGIN) && quadIn(16, 16, 80, 48) &&
               bracket(q.p, 600, [] { return quadIn(120, 120, 150, 140); }) &&
               ((issue(q.p, D3DISSUE_END) && countIs(q.p, 600)) || failIn("END again"));
    });
}
// qy-present: BEGIN, a quad over (20,20)-(60,60) (40x40: 1600), Present, then END with nothing drawn or cleared after
// the Present (on the core the END sits alone in the chunk after the Present's), the wait: 1600. Then BEGIN, quad A
// over (20,100)-(52,116) (32x16: 512), Present, quad B over (100,100)-(116,116) (16x16: 256), END, the wait: 768. Each
// Present after its scene's EndScene; each cell its own until() attempt.
static bool rowQyPresent() {
    return until([&] {
               Com<IDirect3DQuery9> q;
               return newQuery(q) &&
                      (bracket(q.p, 1600, [] { return quadIn(20, 20, 60, 60) && presentNow(); }) ||
                       failIn("END after a Present"));
           }) &&
           until([&] {
               Com<IDirect3DQuery9> q;
               return newQuery(q) && (bracket(q.p, 768,
                                              [] {
                                                  return quadIn(20, 100, 52, 116) && presentNow() &&
                                                         quadIn(100, 100, 116, 116);
                                              }) ||
                                      failIn("across a Present"));
           });
}
// qy-two, in one scene: Q1 BEGIN, quad A over (16,16)-(32,32) (16x16: 256), Q2 BEGIN, quad B over (48,16)-(80,32)
// (32x16: 512), Q1 END, quad C over (96,16)-(104,24) (8x8: 64), Q2 END; the waits: Q1 768 (A + B), Q2 576 (B + C).
static bool rowQyTwo() {
    return until([&] {
        Com<IDirect3DQuery9> q1, q2;
        return newQuery(q1) && newQuery(q2) && scene([&] {
                   return issue(q1.p, D3DISSUE_BEGIN) && zquad(16, 16, 32, 32, 0.5f, KRUST) &&
                          issue(q2.p, D3DISSUE_BEGIN) && zquad(48, 16, 80, 32, 0.5f, KMINT) &&
                          issue(q1.p, D3DISSUE_END) && zquad(96, 16, 104, 24, 0.5f, KGOLD) && issue(q2.p, D3DISSUE_END);
               }) &&
               ((waitFor(q1.p) && countIs(q1.p, 768)) || failIn("Q1")) &&
               ((waitFor(q2.p) && countIs(q2.p, 576)) || failIn("Q2"));
    });
}
// qy-pass. Draws among a Clear, a copy and a fill: T and U, 64x64 targets, S a 32x32 one ColorFill'd mint; in a scene,
// T bound and cleared steel, BEGIN, quad A over (4,4)-(36,20) (32x16: 512, rust), then at once a Clear of T's rect
// (40,4)-(59,13) to aqua; between scenes S whole into T's (40,24)-(53,35) (StretchRect, scaled, POINT: S is one colour)
// and a ColorFill of T's (4,24)-(15,31) gold; in a second scene quad B over (4,44)-(20,60) (16x16: 256, rose), U bound,
// quad C over (4,4)-(12,12) (8x8: 64, lilac), END; the wait: 832 (A + B + C: the Clear, the copy and the fill count
// nothing; the rect Clear comes right after A on purpose: on mtld3d it ends the counting pass, so the copy after it is
// not counted there either; their areas, 171, 143 and 77, make every other sum of the six differ from 832). T: rust at
// (20,12), aqua at (49,8), mint at (46,29), gold at (9,27), rose at (12,52), steel at (50,52), 3 texels or more from
// every rect's edge. Then a draw after END: new T and U (not cleared), in one scene, T bound, BEGIN, quad A on T over
// (4,4)-(36,20) (512), END, U bound, a quad on U over (8,8)-(40,40) (32x32: 1024), T bound, quad D on T over
// (40,40)-(56,48) (16x8: 128); the wait: 512, A alone (with nothing between A and D that commits or reads, no
// depth-stencil bound and nothing of T sampled, T's two passes merge around U's on the core, and D counts there, 640,
// unless D's pass starts with counting off). No cell puts a StretchRect right after a counted draw: mtld3d counts that
// copy's quad, the core does not (a stated divergence).
static bool rowQyPass() {
    return until([&] {
               Com<IDirect3DQuery9> q;
               Com<IDirect3DSurface9> t, u, s;
               const D3DRECT cleared = {40, 4, 59, 13};
               const RECT copied = {40, 24, 53, 35}, filled = {4, 24, 15, 31};
               if (!newQuery(q) || !target64(t, FALSE) || !target64(u, FALSE)) return false;
               CK(CreateRenderTarget, 32, 32, A8, D3DMULTISAMPLE_NONE, 0, FALSE, &s.p, nullptr);
               return colorFill(s.p, nullptr, KMINT) &&
                      ((scene([&] {
                            return clearRt(t.p, KSTEEL) && issue(q.p, D3DISSUE_BEGIN) &&
                                   zquad(4, 4, 36, 20, 0.5f, KRUST) &&
                                   clearRects(1, &cleared, D3DCLEAR_TARGET, KAQUA);
                        }) &&
                        stretchTo(s.p, nullptr, t.p, &copied, D3DTEXF_POINT) && colorFill(t.p, &filled, KGOLD) &&
                        scene([&] {
                            return zquad(4, 44, 20, 60, 0.5f, KROSE) && bindRt(0, u.p) &&
                                   zquad(4, 4, 12, 12, 0.5f, KLILAC) && issue(q.p, D3DISSUE_END) && backToScreen();
                        }) &&
                        waitFor(q.p) && countIs(q.p, 832)) ||
                       failIn("draws among a Clear, a copy and a fill")) &&
                      rtHas("T", t.p, 64, A8,
                            {{20, 12, KRUST}, {49, 8, KAQUA}, {46, 29, KMINT}, {9, 27, KGOLD}, {12, 52, KROSE},
                             {50, 52, KSTEEL}});
           }) &&
           until([&] {
               Com<IDirect3DQuery9> q;
               Com<IDirect3DSurface9> t, u;
               return newQuery(q) && target64(t, FALSE) && target64(u, FALSE) &&
                      ((scene([&] {
                            return bindRt(0, t.p) && issue(q.p, D3DISSUE_BEGIN) && zquad(4, 4, 36, 20, 0.5f, KRUST) &&
                                   issue(q.p, D3DISSUE_END) && bindRt(0, u.p) && zquad(8, 8, 40, 40, 0.5f, KMINT) &&
                                   bindRt(0, t.p) && zquad(40, 40, 56, 48, 0.5f, KGOLD) && backToScreen();
                        }) &&
                        waitFor(q.p) && countIs(q.p, 512)) ||
                       failIn("a draw after END"));
           });
}
// qy-release: BEGIN, a quad over (16,16)-(80,80) (64x64: 4096), the query released with its span open; a new query:
// BEGIN, quad B over (100,20)-(124,44) (24x24: 576), END, the wait: 576.
static bool rowQyRelease() {
    return until([&] {
        Com<IDirect3DQuery9> open, q;
        if (!newQuery(open) || !issue(open.p, D3DISSUE_BEGIN) || !quadIn(16, 16, 80, 80)) return false;
        open.p->Release(), open.p = nullptr;
        return newQuery(q) && bracket(q.p, 576, [] { return quadIn(100, 20, 124, 44); });
    });
}
// qy-reuse: one query, made once, each cell its own until() attempt (a retry BEGINs it again): BEGIN, a quad, END, the
// wait, the count, for quads over (20,20)-(28,28) (8x8: 64), (40,20)-(56,28) (16x8: 128) and (70,20)-(86,36) (16x16:
// 256); then BEGIN, a quad over (20,40)-(52,56) (32x16: 512), END, BEGIN, a quad over (70,40)-(102,72) (32x32: 1024),
// END, with no read between, the wait: 1024 (each area its own power of 2: a count carried over shows).
static bool rowQyReuse() {
    Com<IDirect3DQuery9> q;
    if (!newQuery(q)) return false;
    return until([&] { return bracket(q.p, 64, [] { return quadIn(20, 20, 28, 28); }) || failIn("the first"); }) &&
           until([&] { return bracket(q.p, 128, [] { return quadIn(40, 20, 56, 28); }) || failIn("the second"); }) &&
           until([&] { return bracket(q.p, 256, [] { return quadIn(70, 20, 86, 36); }) || failIn("the third"); }) &&
           until([&] {
               return (issue(q.p, D3DISSUE_BEGIN) && quadIn(20, 40, 52, 56) && issue(q.p, D3DISSUE_END) &&
                       bracket(q.p, 1024, [] { return quadIn(70, 40, 102, 72); })) ||
                      failIn("two spans, no read between");
           });
}
// qy-noflush: BEGIN, a quad over (60,60)-(84,76) (24x16: 384), END; GetData(&d, 4, 0) polled, a Present and Sleep(10)
// after each S_FALSE, for 5 s at most: S_OK, 384 (mtld3d publishes the count at a frame's start once the END's frame
// retired, the core once a Present committed the END's chunk and it retired). Without the flag and without a Present
// such a poll finishes on neither runtime: stated, not judged.
static bool rowQyNoflush() {
    return until([&] {
        Com<IDirect3DQuery9> q;
        if (!newQuery(q) || !issue(q.p, D3DISSUE_BEGIN) || !quadIn(60, 60, 84, 76) || !issue(q.p, D3DISSUE_END))
            return false;
        for (const ULONGLONG end = GetTickCount64() + 5000;;) {
            DWORD d = 0xffffffff;
            const HRESULT hr = q.p->GetData(&d, sizeof d, 0);
            if (hr == S_OK) return exact("count", d, 384);
            if (hr != S_FALSE) return fail("GetData (4 bytes, no flag)", hr);
            if (GetTickCount64() >= end)
                return snprintf(why, sizeof why, "GetData (no flag) S_FALSE for 5 s of Presents"), false;
            if (!presentNow()) return false;
            Sleep(10);
        }
    });
}

static void queriesRows(IDirect3DDevice9 *d, bool strict) {
    static const struct { const char *name; bool (*run)(); } rows[] = {
        {"qy-support", rowQySupport}, {"qy-event", rowQyEvent}, {"qy-count", rowQyCount},
        {"qy-empty", rowQyEmpty},     {"qy-open", rowQyOpen},   {"qy-twice", rowQyTwice},
        {"qy-present", rowQyPresent}, {"qy-two", rowQyTwo},     {"qy-pass", rowQyPass},
        {"qy-release", rowQyRelease}, {"qy-reuse", rowQyReuse}, {"qy-noflush", rowQyNoflush}};
    dev = d, strictMode = strict, budgetEnd = GetTickCount64() + 120000;
    const HRESULT hr = dev->GetRenderTarget(0, &rtSurf);
    for (const auto &r : rows) {
        why[0] = 0;
        bool ok;
        if (FAILED(hr)) ok = fail("GetRenderTarget", hr);
        else if (GetTickCount64() >= budgetEnd) ok = false, snprintf(why, sizeof why, "budget");
        else ok = reset() && r.run();
        row(r.name, ok, why);
    }
    if (rtSurf) rtSurf->Release();
}

// --- The "state" mode's rows of DXMTRS C4c-6a (in stateRows' table after state-fftexoff; defined here, after the query
// helpers their present cells use). Each cell wants what mtld3d, the default runtime, answers. The core refuses these
// calls (D3DERR_NOTAVAILABLE, with its refusal line) until DXMTRS C4c-6a's front end: a row stops at its first refused
// call and prints "FAIL <call> 8876086a". No out value is read before its call's answer is judged.

// One present cell, judged by a query (GetFrontBufferData reads the back buffer as it is now on both runtimes, so it
// cannot tell that a present happened): an OCCLUSION BEGIN, a quad over (x0,y0)-(x1,y1) (zquad's exact count `count`),
// END; a first GetData(&d, 4, 0) that must answer S_FALSE (nothing has presented since the END, and such a poll
// finishes on neither runtime: qy-noflush); then, while that poll answers S_FALSE and for 5 s at most, present() (the
// form under test, which must answer D3D_OK), Sleep(10) and the poll: S_OK and exactly `count` (mtld3d publishes the
// count at a frame's start once the END's frame retired, the core once a present committed the END's chunk and it
// retired). Nothing else submits from the first poll to the last: no other Present, no FLUSH poll, no read-back. A row
// runs it as one until() attempt, whose Present comes after it; `form` names the cell in `why`.
template <class F>
static bool presentCounts(const char *form, float x0, float y0, float x1, float y1, DWORD count, F present) {
    Com<IDirect3DQuery9> q;
    if (!newQuery(q) || !issue(q.p, D3DISSUE_BEGIN) || !quadIn(x0, y0, x1, y1) || !issue(q.p, D3DISSUE_END))
        return failIn(form);
    DWORD d = 0xffffffff;
    HRESULT hr = q.p->GetData(&d, sizeof d, 0);
    if (hr != S_FALSE)
        return snprintf(why, sizeof why, "%s: GetData (4 bytes, no flag) before a present %08lx, want S_FALSE", form,
                        (unsigned long)hr),
               false;
    for (const ULONGLONG end = GetTickCount64() + 5000;;) {
        if ((hr = present()) != D3D_OK) return fail(form, hr);
        Sleep(10);
        d = 0xffffffff;
        hr = q.p->GetData(&d, sizeof d, 0);
        if (hr == S_OK) return exact("count", d, count) || failIn(form);
        if (hr != S_FALSE) return fail("GetData (4 bytes, no flag)", hr) || failIn(form);
        if (GetTickCount64() >= end)
            return snprintf(why, sizeof why, "%s: GetData (no flag) S_FALSE for 5 s of them", form), false;
    }
}
// The mode main's IDirect3D9::GetAdapterDisplayMode reports, in `a`; with `smaller` (DXMTRS C4c-6d's mode rows),
// instead the first mode EnumAdapterModes lists for X8R8G8B8 that is smaller than that one both ways, a FAIL saying so
// when it lists none.
static bool adapterMode(D3DDISPLAYMODE &a, bool smaller = false) {
    const HRESULT hr = direct3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &a);
    if (hr != D3D_OK) return fail("IDirect3D9::GetAdapterDisplayMode", hr);
    if (!smaller) return true;
    const D3DDISPLAYMODE desk = a;
    for (UINT i = 0, n = direct3d->GetAdapterModeCount(D3DADAPTER_DEFAULT, D3DFMT_X8R8G8B8); i < n; i++)
        if (direct3d->EnumAdapterModes(D3DADAPTER_DEFAULT, D3DFMT_X8R8G8B8, i, &a) == D3D_OK && a.Width < desk.Width &&
            a.Height < desk.Height)
            return true;
    return snprintf(why, sizeof why, "EnumAdapterModes lists no X8R8G8B8 mode smaller than the desktop's %ux%u",
                    desk.Width, desk.Height),
           false;
}
// Whether a reported mode is `a` (size, refresh and format); `why` names both otherwise.
static bool sameMode(const char *what, UINT w, UINT h, UINT hz, D3DFORMAT f, const D3DDISPLAYMODE &a) {
    if (w == a.Width && h == a.Height && hz == a.RefreshRate && f == a.Format) return true;
    snprintf(why, sizeof why, "%s %ux%u@%u format %d, want the adapter's %ux%u@%u format %d", what, w, h, hz, (int)f,
             a.Width, a.Height, a.RefreshRate, (int)a.Format);
    return false;
}
// Whether an extended mode is `a`, its Size kept, progressive, with the identity rotation.
static bool sameModeEx(const char *what, const D3DDISPLAYMODEEX &m, D3DDISPLAYROTATION rot, const D3DDISPLAYMODE &a) {
    if (!sameMode(what, m.Width, m.Height, m.RefreshRate, m.Format, a)) return false;
    if (m.Size == sizeof m && m.ScanLineOrdering == D3DSCANLINEORDERING_PROGRESSIVE && rot == D3DDISPLAYROTATION_IDENTITY)
        return true;
    return snprintf(why, sizeof why, "%s Size %u, scan-line ordering %d, rotation %d", what, m.Size,
                    (int)m.ScanLineOrdering, (int)rot),
           false;
}

// state-getters: GetCreationParameters D3D_OK with what main made the device with (D3DADAPTER_DEFAULT, HAL, stateHwnd
// as the focus window, D3DCREATE_HARDWARE_VERTEXPROCESSING), a null pointer D3DERR_INVALIDCALL; GetDisplayMode(0)
// D3D_OK with GetAdapterDisplayMode's mode (a windowed device reports the desktop's, both read live) in X8R8G8B8 (the
// adapter's first format), GetDisplayMode(1) and (0, NULL) D3DERR_INVALIDCALL; the implicit swap chain's GetDisplayMode
// the same mode, a null pointer D3DERR_INVALIDCALL; GetRasterStatus of the device and of the chain D3DERR_INVALIDCALL
// (mtld3d's stubs); GetAvailableTextureMem above zero, and at least 2 MiB lower while a 1024x1024 A8R8G8B8 default-pool
// texture of one level (4 MiB, charged at its create) lives.
static bool rowStateGetters() {
    D3DDEVICE_CREATION_PARAMETERS cp = {};
    HRESULT hr = dev->GetCreationParameters(&cp);
    if (hr != D3D_OK) return fail("GetCreationParameters", hr);
    if (cp.AdapterOrdinal != D3DADAPTER_DEFAULT || cp.DeviceType != D3DDEVTYPE_HAL || cp.hFocusWindow != stateHwnd ||
        cp.BehaviorFlags != D3DCREATE_HARDWARE_VERTEXPROCESSING)
        return snprintf(why, sizeof why, "GetCreationParameters adapter %u, type %d, focus %p, flags %#lx",
                        cp.AdapterOrdinal, (int)cp.DeviceType, (void *)cp.hFocusWindow, (unsigned long)cp.BehaviorFlags),
               false;
    if (!answer("GetCreationParameters (NULL)", dev->GetCreationParameters(nullptr), D3DERR_INVALIDCALL)) return false;
    D3DDISPLAYMODE a = {}, m = {};
    if (!adapterMode(a) || !answer("GetDisplayMode(0)", dev->GetDisplayMode(0, &m), D3D_OK) ||
        !sameMode("GetDisplayMode(0)", m.Width, m.Height, m.RefreshRate, m.Format, a))
        return false;
    if (m.Format != D3DFMT_X8R8G8B8)
        return snprintf(why, sizeof why, "GetDisplayMode(0) format %d, want X8R8G8B8", (int)m.Format), false;
    if (!answer("GetDisplayMode(1)", dev->GetDisplayMode(1, &m), D3DERR_INVALIDCALL) ||
        !answer("GetDisplayMode(0, NULL)", dev->GetDisplayMode(0, nullptr), D3DERR_INVALIDCALL))
        return false;
    Com<IDirect3DSwapChain9> sc;
    CK(GetSwapChain, 0, &sc.p);
    D3DDISPLAYMODE cm = {};
    D3DRASTER_STATUS rs = {};
    if (!answer("IDirect3DSwapChain9::GetDisplayMode", sc.p->GetDisplayMode(&cm), D3D_OK) ||
        !sameMode("IDirect3DSwapChain9::GetDisplayMode", cm.Width, cm.Height, cm.RefreshRate, cm.Format, a) ||
        !answer("IDirect3DSwapChain9::GetDisplayMode (NULL)", sc.p->GetDisplayMode(nullptr), D3DERR_INVALIDCALL) ||
        !answer("GetRasterStatus", dev->GetRasterStatus(0, &rs), D3DERR_INVALIDCALL) ||
        !answer("IDirect3DSwapChain9::GetRasterStatus", sc.p->GetRasterStatus(&rs), D3DERR_INVALIDCALL))
        return false;
    const UINT before = dev->GetAvailableTextureMem();
    if (!before) return snprintf(why, sizeof why, "GetAvailableTextureMem 0"), false;
    Com<IDirect3DTexture9> t;
    CK(CreateTexture, 1024, 1024, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &t.p, nullptr);
    const UINT with = dev->GetAvailableTextureMem();
    if (with > before || before - with < 2u << 20)
        return snprintf(why, sizeof why, "GetAvailableTextureMem %u, then %u with a 4 MiB texture: want 2 MiB less at least",
                        before, with),
               false;
    return true;
}
// state-stubs: SetDialogBoxMode(FALSE), SetClipStatus and GetClipStatus D3DERR_INVALIDCALL (stubs);
// SetPaletteEntries(0) D3D_OK with every entry's peFlags 0xFF, D3DERR_INVALIDCALL with one entry's 0x80 (no
// D3DPTEXTURECAPS_ALPHAPALETTE without debug.capsAll, which no lane sets) and with a null pointer; GetPaletteEntries
// D3DERR_INVALIDCALL; SetCurrentTexturePalette(0) D3D_OK; GetCurrentTexturePalette D3DERR_INVALIDCALL;
// SetSoftwareVertexProcessing(FALSE) D3D_OK, GetSoftwareVertexProcessing FALSE; SetNPatchMode(2) and (0) D3D_OK,
// GetNPatchMode 0; DrawRectPatch, DrawTriPatch and DeletePatch D3DERR_INVALIDCALL; ValidateDevice at reset()'s state
// (POINT filters, no texture) D3D_OK and one pass, with a null pointer D3D_OK. Then EvictManagedResources over a managed
// 4x4 X8R8G8B8 texture on stage 0 (MODULATE TEXTURE DIFFUSE under a white quad over (64,64)-(192,192), blue clear): it
// is written steel and drawn, steel at (128,128) and blue at (16,16) through frames() (so the level was uploaded), then
// written rust under D3DLOCK_NO_DIRTY_UPDATE; EvictManagedResources D3D_OK; drawn again: rust (the eviction marks an
// uploaded level dirty, so the next draw publishes the CPU copy: Wine's add_dirty_rect_test). Nothing is judged between
// the write and the eviction (parity's dr-keep judges that a draw there shows the old colour). Stage 0 is unbound
// after.
static bool rowStateStubs() {
    D3DCLIPSTATUS9 cs = {D3DCS_ALL, 0};
    if (!answer("SetDialogBoxMode(FALSE)", dev->SetDialogBoxMode(FALSE), D3DERR_INVALIDCALL) ||
        !answer("SetClipStatus", dev->SetClipStatus(&cs), D3DERR_INVALIDCALL) ||
        !answer("GetClipStatus", dev->GetClipStatus(&cs), D3DERR_INVALIDCALL))
        return false;
    PALETTEENTRY pal[256];
    for (PALETTEENTRY &e : pal) e = {0x10, 0x20, 0x30, 0xff};
    if (!answer("SetPaletteEntries", dev->SetPaletteEntries(0, pal), D3D_OK)) return false;
    pal[5].peFlags = 0x80;
    UINT palette = 7;
    if (!answer("SetPaletteEntries (an entry's alpha 0x80)", dev->SetPaletteEntries(0, pal), D3DERR_INVALIDCALL) ||
        !answer("SetPaletteEntries (NULL)", dev->SetPaletteEntries(0, nullptr), D3DERR_INVALIDCALL) ||
        !answer("GetPaletteEntries", dev->GetPaletteEntries(0, pal), D3DERR_INVALIDCALL) ||
        !answer("SetCurrentTexturePalette", dev->SetCurrentTexturePalette(0), D3D_OK) ||
        !answer("GetCurrentTexturePalette", dev->GetCurrentTexturePalette(&palette), D3DERR_INVALIDCALL) ||
        !answer("SetSoftwareVertexProcessing(FALSE)", dev->SetSoftwareVertexProcessing(FALSE), D3D_OK))
        return false;
    const BOOL software = dev->GetSoftwareVertexProcessing();
    if (software != FALSE) return snprintf(why, sizeof why, "GetSoftwareVertexProcessing %d", (int)software), false;
    if (!answer("SetNPatchMode(2)", dev->SetNPatchMode(2.0f), D3D_OK) ||
        !answer("SetNPatchMode(0)", dev->SetNPatchMode(0.0f), D3D_OK))
        return false;
    const float segments = dev->GetNPatchMode();
    if (segments != 0.0f) return snprintf(why, sizeof why, "GetNPatchMode %g", (double)segments), false;
    const float segs[4] = {1, 1, 1, 1};
    const D3DRECTPATCH_INFO rect = {};
    const D3DTRIPATCH_INFO tri = {};
    DWORD passes = 0xdead;
    if (!answer("DrawRectPatch", dev->DrawRectPatch(1, segs, &rect), D3DERR_INVALIDCALL) ||
        !answer("DrawTriPatch", dev->DrawTriPatch(1, segs, &tri), D3DERR_INVALIDCALL) ||
        !answer("DeletePatch", dev->DeletePatch(1), D3DERR_INVALIDCALL) ||
        !answer("ValidateDevice", dev->ValidateDevice(&passes), D3D_OK) || !exact("ValidateDevice's passes", passes, 1) ||
        !answer("ValidateDevice (NULL)", dev->ValidateDevice(nullptr), D3D_OK))
        return false;
    Com<IDirect3DTexture9> t;
    CK(CreateTexture, 4, 4, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &t.p, nullptr);
    const auto paint = [&](DWORD c, DWORD flags, const char *call) {
        D3DLOCKED_RECT lr = {};
        HRESULT h = t.p->LockRect(0, &lr, nullptr, flags);
        if (FAILED(h)) return fail(call, h);
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++) ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] = c;
        h = t.p->UnlockRect(0);
        return SUCCEEDED(h) || fail("UnlockRect", h);
    };
    const DWORD white = D3DCOLOR_XRGB(255, 255, 255);
    const TV q[4] = {{64, 64, 0.5f, 1, white, 0, 0}, {192, 64, 0.5f, 1, white, 1, 0},
                     {64, 192, 0.5f, 1, white, 0, 1}, {192, 192, 0.5f, 1, white, 1, 1}};
    const auto draw = [&] {
        CK(SetFVF, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        CK(SetTexture, 0, t.p);
        CK(DrawPrimitiveUP, D3DPT_TRIANGLESTRIP, 2, q, sizeof *q);
        return true;
    };
    const Px old[] = {{128, 128, KSTEEL}, {16, 16, BLUE}}, fresh[] = {{128, 128, KRUST}, {16, 16, BLUE}};
    const bool ok = paint(KSTEEL, 0, "LockRect") && (frames(BLUE, old, 2, draw) || failIn("before the eviction")) &&
                    paint(KRUST, D3DLOCK_NO_DIRTY_UPDATE, "LockRect (NO_DIRTY_UPDATE)") &&
                    answer("EvictManagedResources", dev->EvictManagedResources(), D3D_OK) &&
                    (frames(BLUE, fresh, 2, draw) || failIn("after the eviction"));
    dev->SetTexture(0, nullptr);
    return ok;
}
// state-present: Present's other forms, each a presentCounts cell (one until() attempt) over one of qy-reuse's quads: a
// source rect (0,0)-(128,128), over (20,20)-(28,28) (8x8: 64); that destination rect, over (40,20)-(56,28) (16x8: 128); a
// dirty region of that one rect, over (70,20)-(86,36) (16x16: 256); a destination window override, a second (hidden)
// window the attempt makes before its BEGIN and destroys after (stateHwnd is the device's own), over (20,40)-(52,56)
// (32x16: 512); and IDirect3DSwapChain9::Present(.., 0) of an additional swap chain made before the BEGIN as
// state-swapchain makes one, over (70,40)-(102,72) (32x32: 1024). mtld3d warns each argument once, ignores it and
// presents the whole back buffer into the device window; an additional chain presents the device's back buffer. A
// present that does nothing leaves the poll at S_FALSE: what is judged is that a present happened, not where.
static bool rowStatePresent() {
    static const RECT r = {0, 0, 128, 128};
    static const struct { RGNDATAHEADER h; RECT r; } rgn = {
        {sizeof(RGNDATAHEADER), RDH_RECTANGLES, 1, sizeof(RECT), {0, 0, 128, 128}}, {0, 0, 128, 128}};
    const RGNDATA *const region = (const RGNDATA *)&rgn;
    return until([&] {
               return presentCounts("Present (a source rect)", 20, 20, 28, 28, 64,
                                    [&] { return dev->Present(&r, nullptr, nullptr, nullptr); });
           }) &&
           until([&] {
               return presentCounts("Present (a destination rect)", 40, 20, 56, 28, 128,
                                    [&] { return dev->Present(nullptr, &r, nullptr, nullptr); });
           }) &&
           until([&] {
               return presentCounts("Present (a dirty region)", 70, 20, 86, 36, 256,
                                    [&] { return dev->Present(nullptr, nullptr, nullptr, region); });
           }) &&
           until([&] {
               Window w;
               w.h = CreateWindowW(L"d3d9_triangle", L"d3d9_triangle override", WS_OVERLAPPEDWINDOW, 400, 100, 64, 64,
                                   nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
               if (!w.h) return snprintf(why, sizeof why, "CreateWindowW"), false;
               return presentCounts("Present (another window)", 20, 40, 52, 56, 512,
                                    [&] { return dev->Present(nullptr, nullptr, w.h, nullptr); });
           }) &&
           until([&] {
               D3DPRESENT_PARAMETERS pp = {};
               pp.Windowed = TRUE, pp.SwapEffect = D3DSWAPEFFECT_DISCARD, pp.hDeviceWindow = stateHwnd;
               Com<IDirect3DSwapChain9> sc;
               CK(CreateAdditionalSwapChain, &pp, &sc.p);
               return presentCounts("IDirect3DSwapChain9::Present (an additional chain)", 70, 40, 102, 72, 1024,
                                    [&] { return sc.p->Present(nullptr, nullptr, nullptr, nullptr, 0); });
           });
}
// state-ex: an IDirect3D9Ex of its own (Direct3DCreate9Ex) and an extended device (CreateDeviceEx, windowed, params256:
// D3DPRESENT_INTERVAL_IMMEDIATE) on a visible window of its own (a second Metal view on the launch's window is untried),
// everything released on every path (the chains and the back buffer, the device, the IDirect3D9Ex, then the window); a
// create that fails is a FAIL with its hr (d3d9.dll always exports Direct3DCreate9Ex). On it: GetDisplayModeEx(0) (Size
// set) D3D_OK with GetAdapterDisplayMode's mode, progressive, Size kept, D3DDISPLAYROTATION_IDENTITY; of swap chain 1
// and with a null mode D3DERR_INVALIDCALL; the implicit chain's IDirect3DSwapChain9Ex::GetDisplayModeEx the same;
// PresentEx(.., 0) as a presentCounts cell over (60,60)-(84,76) (24x16: 384), the device's globals swapped in for it
// (OnDevice, inside the attempt); WaitForVBlank(0) D3D_OK and (1) D3DERR_INVALIDCALL; GetLastPresentCount D3D_OK and 0
// (mtld3d counts no present), a null pointer D3DERR_INVALIDCALL; GetPresentStats D3D_OK with zero counts and times, a
// null pointer D3DERR_INVALIDCALL; CheckResourceResidency(NULL, 0) D3D_OK; ComposeRects of nothing D3D_OK (mtld3d reads
// no argument and composes nothing); SetGPUThreadPriority(0) D3D_OK; GetGPUThreadPriority D3D_OK and 0, a null pointer
// D3DERR_INVALIDCALL; SetConvolutionMonoKernel D3DERR_INVALIDCALL. GetAvailableTextureMem is not asked of it (an
// extended device's figure does not shrink).
static bool rowStateEx() {
    RECT wr = {0, 0, 256, 256};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    Window w;
    w.h = CreateWindowW(L"d3d9_triangle", L"d3d9_triangle ex", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 400, 100,
                        wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!w.h) return snprintf(why, sizeof why, "CreateWindowW"), false;
    Com<IDirect3D9Ex> d3d;
    HRESULT hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &d3d.p);
    if (hr != D3D_OK || !d3d.p) return fail("Direct3DCreate9Ex", hr);
    D3DPRESENT_PARAMETERS pp = params256(w.h, 0);
    Com<IDirect3DDevice9Ex> ex;
    hr = d3d.p->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, w.h, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, nullptr,
                               &ex.p);
    if (hr != D3D_OK || !ex.p) return fail("CreateDeviceEx", hr);
    Com<IDirect3DSurface9> bb;
    Com<IDirect3DSwapChain9> sc;
    Com<IDirect3DSwapChain9Ex> scx;
    if ((hr = ex.p->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb.p)) != D3D_OK)
        return fail("GetBackBuffer (the extended device)", hr);
    if ((hr = ex.p->GetSwapChain(0, &sc.p)) != D3D_OK) return fail("GetSwapChain (the extended device)", hr);
    if ((hr = sc.p->QueryInterface(__uuidof(IDirect3DSwapChain9Ex), (void **)&scx.p)) != S_OK || !scx.p)
        return fail("QueryInterface (IDirect3DSwapChain9Ex)", hr);
    D3DDISPLAYMODE a = {};
    D3DDISPLAYMODEEX m = {}, cm = {};
    m.Size = cm.Size = sizeof m;
    D3DDISPLAYROTATION rot = (D3DDISPLAYROTATION)0, crot = (D3DDISPLAYROTATION)0;
    if (!answer("GetDisplayModeEx(0)", ex.p->GetDisplayModeEx(0, &m, &rot), D3D_OK) || !adapterMode(a) ||
        !sameModeEx("GetDisplayModeEx(0)", m, rot, a) ||
        !answer("GetDisplayModeEx(1)", ex.p->GetDisplayModeEx(1, &m, &rot), D3DERR_INVALIDCALL) ||
        !answer("GetDisplayModeEx(0, NULL)", ex.p->GetDisplayModeEx(0, nullptr, &rot), D3DERR_INVALIDCALL) ||
        !answer("IDirect3DSwapChain9Ex::GetDisplayModeEx", scx.p->GetDisplayModeEx(&cm, &crot), D3D_OK) ||
        !sameModeEx("IDirect3DSwapChain9Ex::GetDisplayModeEx", cm, crot, a))
        return false;
    if (!until([&] {
            OnDevice on(ex.p, bb.p);
            return presentCounts("PresentEx", 60, 60, 84, 76, 384,
                                 [&] { return ex.p->PresentEx(nullptr, nullptr, nullptr, nullptr, 0); });
        }))
        return false;
    UINT presents = 7;
    D3DPRESENTSTATS st;
    memset(&st, 0xab, sizeof st);
    if (!answer("WaitForVBlank(0)", ex.p->WaitForVBlank(0), D3D_OK) ||
        !answer("WaitForVBlank(1)", ex.p->WaitForVBlank(1), D3DERR_INVALIDCALL) ||
        !answer("GetLastPresentCount", scx.p->GetLastPresentCount(&presents), D3D_OK) ||
        !exact("GetLastPresentCount", presents, 0) ||
        !answer("GetLastPresentCount (NULL)", scx.p->GetLastPresentCount(nullptr), D3DERR_INVALIDCALL) ||
        !answer("GetPresentStats", scx.p->GetPresentStats(&st), D3D_OK))
        return false;
    if (st.PresentCount || st.PresentRefreshCount || st.SyncRefreshCount || st.SyncQPCTime.QuadPart ||
        st.SyncGPUTime.QuadPart)
        return snprintf(why, sizeof why, "GetPresentStats counts %u %u %u, times %lld %lld, want zeros", st.PresentCount,
                        st.PresentRefreshCount, st.SyncRefreshCount, (long long)st.SyncQPCTime.QuadPart,
                        (long long)st.SyncGPUTime.QuadPart),
               false;
    INT priority = 7;
    float rows[4] = {1, 1, 1, 1}, columns[4] = {1, 1, 1, 1};
    return answer("GetPresentStats (NULL)", scx.p->GetPresentStats(nullptr), D3DERR_INVALIDCALL) &&
           answer("CheckResourceResidency", ex.p->CheckResourceResidency(nullptr, 0), D3D_OK) &&
           answer("ComposeRects", ex.p->ComposeRects(nullptr, nullptr, nullptr, 0, nullptr, D3DCOMPOSERECTS_COPY, 0, 0),
                  D3D_OK) &&
           answer("SetGPUThreadPriority(0)", ex.p->SetGPUThreadPriority(0), D3D_OK) &&
           answer("GetGPUThreadPriority", ex.p->GetGPUThreadPriority(&priority), D3D_OK) &&
           exact("GetGPUThreadPriority", (unsigned long long)priority, 0) &&
           answer("GetGPUThreadPriority (NULL)", ex.p->GetGPUThreadPriority(nullptr), D3DERR_INVALIDCALL) &&
           answer("SetConvolutionMonoKernel", ex.p->SetConvolutionMonoKernel(4, 4, rows, columns), D3DERR_INVALIDCALL);
}

// --- The "reset" mode (DXMTRS C4c-6c): Reset and ResetEx of a windowed device, TestCooperativeLevel and
// CheckDeviceState. Each row makes a device and a window of its own (rsDevice: a visible plain window, never maximized,
// whose client area is 256x256, and a 256x256 X8R8G8B8 device on it, DISCARD, IMMEDIATE, one back buffer, with the auto
// depth-stencil the row names; an extended row's from an IDirect3D9Ex of its own) and releases everything on every
// path; while it runs, the globals the helpers read name its device (OnDevice; rtSurf null, so until()'s rebind of it
// is an INVALIDCALL nothing reads). Every Reset is made outside until(), with no implicit surface held unless the cell
// holds one on purpose (a held one blocks a plain device's Reset). A draw after a Reset is judged by reading the back
// buffer (bbRead: GetRenderTargetData into a system-memory surface of its size, a multisampled one resolved first by
// StretchRect into a single-sample target of its size) in one until() attempt (mtld3d leaves a draw out while a new
// device's pipelines build); a present by presentCounts, its query made after the Reset, on a single-sample back
// buffer.
// A plain Reset turns D3DRS_ZENABLE on whether or not the device has an auto depth-stencil (mtld3d's default; native's
// follows the auto depth-stencil): every judged draw sets it, on with Z cleared to 1 where there is one, off elsewhere.
// Each cell wants what mtld3d, the default runtime, answers. The core refuses Reset, ResetEx, TestCooperativeLevel and
// CheckDeviceState (D3DERR_NOTAVAILABLE) until DXMTRS C4c-6c's front end: a row stops at its first refused call and
// prints its "FAIL ... 8876086a". The mode's budget is 200 s, the launches' watchdog 300 s.

// A row's device: its window, an extended row's IDirect3D9Ex, the device (`ex` the same one, extended), released in
// reverse: the device, the IDirect3D9Ex, then the window.
struct RsDevice {
    Window w;
    Com<IDirect3D9Ex> d3d;
    Com<IDirect3DDevice9Ex> ex;
    Com<IDirect3DDevice9> d;
};
// A visible plain window of its own at (x,100) whose client area is cw x ch (mtld3d gives a maximized window's back
// buffer its client area whatever the request, so none is maximized).
static bool rsWindow(Window &w, int cw, int ch, int x = 400) {
    RECT r = {0, 0, cw, ch};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    w.h = CreateWindowW(L"d3d9_triangle", L"d3d9_triangle reset", WS_OVERLAPPEDWINDOW | WS_VISIBLE, x, 100,
                        r.right - r.left, r.bottom - r.top, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    return w.h || (snprintf(why, sizeof why, "CreateWindowW"), false);
}
// Windowed present parameters on `w`: width x height X8R8G8B8, one back buffer, DISCARD, IMMEDIATE, `ms` samples, an
// auto depth-stencil of `depth` (none for D3DFMT_UNKNOWN); with `full` (DXMTRS C4c-6d), fullscreen ones (Windowed
// FALSE) at the refresh rate of the mode `full`.
static D3DPRESENT_PARAMETERS rsParams(HWND w, UINT width, UINT height, D3DFORMAT depth = D3DFMT_UNKNOWN,
                                      D3DMULTISAMPLE_TYPE ms = D3DMULTISAMPLE_NONE,
                                      const D3DDISPLAYMODE *full = nullptr) {
    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = width, pp.BackBufferHeight = height, pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1, pp.MultiSampleType = ms, pp.SwapEffect = D3DSWAPEFFECT_DISCARD, pp.hDeviceWindow = w;
    pp.Windowed = !full, pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    pp.FullScreen_RefreshRateInHz = full ? full->RefreshRate : 0;
    pp.EnableAutoDepthStencil = depth != D3DFMT_UNKNOWN, pp.AutoDepthStencilFormat = depth;
    return pp;
}
// r's window (rsWindow's, client area 256x256, unless the row made it first) and a device on it: 256x256 (rsParams,
// `depth`, `ms` samples) or, with `full`, fullscreen at that mode's size and refresh rate; behaviour flags
// D3DCREATE_HARDWARE_VERTEXPROCESSING and `flags`. From main's IDirect3D9, or, `extended`, CreateDeviceEx of an
// IDirect3D9Ex of its own. A create that fails is a FAIL with its hr.
static bool rsDevice(RsDevice &r, D3DFORMAT depth = D3DFMT_UNKNOWN, bool extended = false,
                     D3DMULTISAMPLE_TYPE ms = D3DMULTISAMPLE_NONE, const D3DDISPLAYMODE *full = nullptr,
                     DWORD flags = 0) {
    if (!r.w.h && !rsWindow(r.w, 256, 256)) return false;
    D3DPRESENT_PARAMETERS pp = rsParams(r.w.h, full ? full->Width : 256, full ? full->Height : 256, depth, ms, full);
    flags |= D3DCREATE_HARDWARE_VERTEXPROCESSING;
    HRESULT hr;
    if (!extended) {
        hr = direct3d ? direct3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, r.w.h, flags, &pp, &r.d.p)
                      : E_POINTER;
        return (hr == D3D_OK && r.d.p) || fail(full ? "CreateDevice (fullscreen)" : "CreateDevice", hr);
    }
    hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &r.d3d.p);
    if (hr != D3D_OK || !r.d3d.p) return fail("Direct3DCreate9Ex", hr);
    hr = r.d3d.p->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, r.w.h, flags, &pp, nullptr, &r.ex.p);
    if (hr != D3D_OK || !r.ex.p) return fail("CreateDeviceEx", hr);
    r.d.p = r.ex.p, r.d.p->AddRef();
    return true;
}
static bool resetIs(const char *what, D3DPRESENT_PARAMETERS &pp, HRESULT expect = D3D_OK) {
    return answer(what, dev->Reset(&pp), expect);
}
static bool coopIs(HRESULT expect) { return answer("TestCooperativeLevel", dev->TestCooperativeLevel(), expect); }
static bool presentIs(HRESULT expect) {
    return answer("Present", dev->Present(nullptr, nullptr, nullptr, nullptr), expect);
}
// `s`'s description: w x h of format f and `ms` samples.
static bool descIs(const char *name, IDirect3DSurface9 *s, UINT w, UINT h, D3DFORMAT f,
                   D3DMULTISAMPLE_TYPE ms = D3DMULTISAMPLE_NONE) {
    D3DSURFACE_DESC d = {};
    const HRESULT hr = s->GetDesc(&d);
    if (FAILED(hr)) return fail("GetDesc", hr) || failIn(name);
    if (d.Width == w && d.Height == h && d.Format == f && d.MultiSampleType == ms) return true;
    return snprintf(why, sizeof why, "%s %ux%u format %d, %d samples; want %ux%u format %d, %d samples", name, d.Width,
                    d.Height, (int)d.Format, (int)d.MultiSampleType, w, h, (int)f, (int)ms),
           false;
}
// GetBackBuffer's and GetDepthStencilSurface's descriptions (each surface released at once).
static bool bbIs(UINT w, UINT h, D3DMULTISAMPLE_TYPE ms = D3DMULTISAMPLE_NONE) {
    Com<IDirect3DSurface9> bb;
    CK(GetBackBuffer, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb.p);
    return (bb.p || fail("GetBackBuffer (no surface)", E_POINTER)) &&
           descIs("the back buffer", bb.p, w, h, D3DFMT_X8R8G8B8, ms);
}
static bool dsIs(UINT w, UINT h, D3DFORMAT f, D3DMULTISAMPLE_TYPE ms = D3DMULTISAMPLE_NONE) {
    Com<IDirect3DSurface9> ds;
    CK(GetDepthStencilSurface, &ds.p);
    return (ds.p || fail("GetDepthStencilSurface (no surface)", E_POINTER)) &&
           descIs("the auto depth-stencil", ds.p, w, h, f, ms);
}
// GetViewport (0,0) w x h, depth range [mn, mx]; GetScissorRect (0,0)-(w,h).
static bool extentIs(UINT w, UINT h, float mn = 0.0f, float mx = 1.0f) {
    D3DVIEWPORT9 v = {};
    RECT s = {};
    CK(GetViewport, &v);
    CK(GetScissorRect, &s);
    if (v.X || v.Y || v.Width != w || v.Height != h || v.MinZ != mn || v.MaxZ != mx)
        return snprintf(why, sizeof why, "the viewport (%lu,%lu) %lux%lu, %g..%g; want (0,0) %ux%u, %g..%g",
                        (unsigned long)v.X, (unsigned long)v.Y, (unsigned long)v.Width, (unsigned long)v.Height,
                        (double)v.MinZ, (double)v.MaxZ, w, h, (double)mn, (double)mx),
               false;
    if (s.left || s.top || s.right != (LONG)w || s.bottom != (LONG)h)
        return snprintf(why, sizeof why, "the scissor rect (%ld,%ld)-(%ld,%ld), want (0,0)-(%u,%u)", (long)s.left,
                        (long)s.top, (long)s.right, (long)s.bottom, w, h),
               false;
    return true;
}
// The back buffer read: GetRenderTargetData into a system-memory surface of its size (a multisampled one, square,
// resolved first by resolveInto, as ms-back reads it), check(bits, pitch) on it.
template <class F> static bool bbRead(F check) {
    Com<IDirect3DSurface9> bb;
    Rt one;
    CK(GetBackBuffer, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb.p);
    if (!bb.p) return fail("GetBackBuffer (no surface)", E_POINTER);
    D3DSURFACE_DESC d = {};
    const HRESULT hr = bb.p->GetDesc(&d);
    if (FAILED(hr)) return fail("GetDesc (the back buffer)", hr);
    IDirect3DSurface9 *src = bb.p;
    if (d.MultiSampleType != D3DMULTISAMPLE_NONE) {
        if (d.Width != d.Height)
            return snprintf(why, sizeof why, "a multisampled back buffer %ux%u, not square", d.Width, d.Height), false;
        if (!resolveInto(bb.p, one, d.Width, d.Format)) return false;
        src = one.s.p;
    }
    return readTarget(src, d.Width, d.Height, d.Format, check);
}
static bool bbHas(std::initializer_list<Tx> px) {
    return bbRead([&](const BYTE *b, int p) { return txHas("the back buffer", b, p, px); });
}
// The back buffer cleared blue; Z cleared to 1 and the depth test on when `z`, else the test off.
static bool clearBlue(bool z) {
    if (!rstate(D3DRS_ZENABLE, z ? D3DZB_TRUE : D3DZB_FALSE)) return false;
    CK(Clear, 0, nullptr, D3DCLEAR_TARGET | (z ? D3DCLEAR_ZBUFFER : 0), BLUE, 1.0f, 0);
    return true;
}
// A draw after a Reset (one until() attempt): clearBlue(z), a rust quad over (x0,y0)-(x1,y1) at z 0.5, the back buffer
// read: rust at the quad's centre, blue at (bx,by).
static bool lands(int x0, int y0, int x1, int y1, int bx, int by, bool z = false) {
    return clearBlue(z) && quadIn((float)x0, (float)y0, (float)x1, (float)y1, KRUST) &&
           bbHas({{(x0 + x1) / 2, (y0 + y1) / 2, KRUST}, {bx, by, BLUE}});
}
// A present after a Reset: one until() attempt of clearBlue(z) and presentCounts over the 16x8 quad at (8,8)-(24,16)
// (128), its query made now.
static bool presents(bool z = false) {
    return until([&] {
        return clearBlue(z) && presentCounts("Present", 8, 8, 24, 16, 128,
                                             [] { return dev->Present(nullptr, nullptr, nullptr, nullptr); });
    });
}
// The depth test (one until() attempt): clearBlue(true), red at z 0.25 over (32,96)-(160,160), then green at 0.75 over
// (96,96)-(304,160), which the red hides where they overlap: red at (64,128) and (128,128), green at (288,128) (past
// the creation size), blue at (288,32). The back buffer is 320 wide or more.
static bool depthLands() {
    return clearBlue(true) &&
           scene([] { return zquad(32, 96, 160, 160, 0.25f, TRED) && zquad(96, 96, 304, 160, 0.75f, TGRN); }) &&
           bbHas({{64, 128, TRED}, {128, 128, TRED}, {288, 128, TGRN}, {288, 32, BLUE}});
}
// GetDepthStencilSurface with no auto depth-stencil: D3DERR_NOTFOUND and the out pointer NULL.
static bool noDepth() {
    IDirect3DSurface9 *const mark = reinterpret_cast<IDirect3DSurface9 *>(static_cast<INT_PTR>(0x5a5a));
    IDirect3DSurface9 *ds = mark;
    const HRESULT hr = dev->GetDepthStencilSurface(&ds);
    if (SUCCEEDED(hr) && ds && ds != mark) ds->Release();
    if (hr != D3DERR_NOTFOUND) return fail("GetDepthStencilSurface (no auto depth-stencil)", hr);
    return !ds ||
           (snprintf(why, sizeof why, "GetDepthStencilSurface (no auto depth-stencil) left %p", (void *)ds), false);
}

// rs-size (no auto depth-stencil): Reset to 384x320 (past the creation size both ways) D3D_OK: the back buffer 384x320
// X8R8G8B8, the viewport (0,0) 384x320 over 0..1 and the scissor rect (0,0)-(384,320); a quad over (320,256)-(376,312)
// lands (rust at (348,284), blue at (300,300)). Reset to 0x0 D3D_OK: the struct reads the window's client area back
// (GetClientRect, 256x256), and the back buffer, viewport and scissor rect are that size; a quad 8 pixels in from its
// bottom-right corner lands (blue at (8,8)).
static bool rowRsSize() {
    RsDevice r;
    if (!rsDevice(r)) return false;
    const OnDevice on(r.d.p, nullptr);
    D3DPRESENT_PARAMETERS pp = rsParams(r.w.h, 384, 320);
    if (!resetIs("Reset (to 384x320)", pp)) return false;
    if (!bbIs(384, 320) || !extentIs(384, 320) || !until([] { return lands(320, 256, 376, 312, 300, 300); }))
        return failIn("384x320");
    RECT c = {};
    if (!GetClientRect(r.w.h, &c) || c.right <= 0 || c.bottom <= 0)
        return snprintf(why, sizeof why, "GetClientRect"), false;
    const UINT cw = (UINT)c.right, ch = (UINT)c.bottom;
    pp = rsParams(r.w.h, 0, 0);
    if (!resetIs("Reset (a zero size)", pp)) return false;
    if (pp.BackBufferWidth != cw || pp.BackBufferHeight != ch)
        return snprintf(why, sizeof why, "Reset (a zero size) wrote %ux%u back, want the client area's %ux%u",
                        pp.BackBufferWidth, pp.BackBufferHeight, cw, ch),
               false;
    return (bbIs(cw, ch) && extentIs(cw, ch) &&
            until([&] { return lands((int)cw - 40, (int)ch - 40, (int)cw - 8, (int)ch - 8, 8, 8); })) ||
           failIn("the client area");
}
// rs-depth (an auto D24S8): Reset to 320x240 with D24S8 D3D_OK: GetDepthStencilSurface 320x240 D24S8, depthLands.
// Reset at that size with no auto depth-stencil D3D_OK: GetDepthStencilSurface D3DERR_NOTFOUND and NULL; a quad over
// (240,160)-(304,224), the depth test off, lands (blue at (8,8)). Reset at that size with an auto D16 D3D_OK:
// GetDepthStencilSurface 320x240 D16, depthLands.
static bool rowRsDepth() {
    RsDevice r;
    if (!rsDevice(r, D3DFMT_D24S8)) return false;
    const OnDevice on(r.d.p, nullptr);
    D3DPRESENT_PARAMETERS pp = rsParams(r.w.h, 320, 240, D3DFMT_D24S8);
    if (!resetIs("Reset (to 320x240, D24S8)", pp)) return false;
    if (!bbIs(320, 240) || !dsIs(320, 240, D3DFMT_D24S8) || !until(depthLands)) return failIn("D24S8 at 320x240");
    pp = rsParams(r.w.h, 320, 240);
    if (!resetIs("Reset (no auto depth-stencil)", pp)) return false;
    if (!noDepth() || !until([] { return lands(240, 160, 304, 224, 8, 8); })) return failIn("no auto depth-stencil");
    pp = rsParams(r.w.h, 320, 240, D3DFMT_D16);
    if (!resetIs("Reset (an auto D16 at that size)", pp)) return false;
    return (dsIs(320, 240, D3DFMT_D16) && until(depthLands)) || failIn("D16 at 320x240");
}
// The white pretransformed triangle (0,0), (256,256), (0,256) over a black clear (Z 1, the depth test on): its
// hypotenuse runs through the diagonal pixels' centres (D3D9 puts a centre at integer coordinates), so 2 of a pixel's 4
// samples are in at 4 samples (ms-edge's band, 48..208, for the mean of (128,128)'s channels) and none or all at one
// (0 or 255). White at (32,224), black at (224,32). One until() attempt.
static bool edgeLands(bool four) {
    const RV tri[3] = {{0, 0, 0.5f, 1, TWHT}, {256, 256, 0.5f, 1, TWHT}, {0, 256, 0.5f, 1, TWHT}};
    if (!rstate(D3DRS_ZENABLE, D3DZB_TRUE)) return false;
    CK(Clear, 0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    if (!scene([&] { return rtri(tri); })) return false;
    int mean = -1;
    if (!bbRead([&](const BYTE *b, int p) {
            const DWORD d = texel(b, p, 128, 128);
            mean = ((int)(d >> 16 & 0xff) + (int)(d >> 8 & 0xff) + (int)(d & 0xff)) / 3;
            return txHas("the back buffer", b, p, {{32, 224, TWHT}, {224, 32, 0}});
        }))
        return false;
    if (four) return inBand("the diagonal's (128,128)", mean);
    return mean <= 1 || mean >= 254 ||
           (snprintf(why, sizeof why, "the diagonal's (128,128) %d, want 0 or 255 at one sample", mean), false);
}
// rs-msaa (an auto D24S8): Reset to 4 samples at 256x256 D3D_OK: the back buffer and GetDepthStencilSurface say 4
// samples; edgeLands(4 samples), its until() Present D3D_OK (the resolve at present, judged by its answer alone: a
// present's occlusion count of a multisampled back buffer is not pinned). Reset back to one sample D3D_OK: both say
// none; edgeLands(one), a present (presents).
static bool rowRsMsaa() {
    RsDevice r;
    if (!rsDevice(r, D3DFMT_D24S8)) return false;
    const OnDevice on(r.d.p, nullptr);
    const D3DMULTISAMPLE_TYPE four = D3DMULTISAMPLE_4_SAMPLES;
    D3DPRESENT_PARAMETERS pp = rsParams(r.w.h, 256, 256, D3DFMT_D24S8, four);
    if (!resetIs("Reset (to 4 samples)", pp)) return false;
    if (!bbIs(256, 256, four) || !dsIs(256, 256, D3DFMT_D24S8, four) || !until([] { return edgeLands(true); }))
        return failIn("4 samples");
    pp = rsParams(r.w.h, 256, 256, D3DFMT_D24S8);
    if (!resetIs("Reset (back to one sample)", pp)) return false;
    return (bbIs(256, 256) && dsIs(256, 256, D3DFMT_D24S8) && until([] { return edgeLands(false); }) &&
            presents(true)) ||
           failIn("one sample again");
}
// One blocker of rs-blockers: make(held), the resource the application then holds; Reset D3DERR_INVALIDCALL;
// TestCooperativeLevel and Present D3DERR_DEVICENOTRESET; the resource released; Reset and TestCooperativeLevel D3D_OK.
template <class T, class F> static bool blockedBy(const char *what, HWND w, F make) {
    D3DPRESENT_PARAMETERS pp = rsParams(w, 256, 256);
    {
        Com<T> held;
        if (!make(held) || !resetIs("Reset", pp, D3DERR_INVALIDCALL) || !coopIs(D3DERR_DEVICENOTRESET) ||
            !presentIs(D3DERR_DEVICENOTRESET))
            return failIn(what);
    }
    return (resetIs("Reset (released)", pp) && coopIs(D3D_OK)) || failIn(what);
}
// rs-blockers (no auto depth-stencil), a blocker each: a 64x64 default-pool texture, a 64x64 CreateRenderTarget
// surface, a 64-byte default-pool vertex buffer, the back buffer (GetBackBuffer). Then a managed texture and an
// OCCLUSION query alive: Reset and TestCooperativeLevel D3D_OK, a quad lands, until()'s Present D3D_OK.
static bool rowRsBlockers() {
    RsDevice r;
    if (!rsDevice(r)) return false;
    const OnDevice on(r.d.p, nullptr);
    const HWND w = r.w.h;
    if (!blockedBy<IDirect3DTexture9>("a default-pool texture", w,
                                      [](Com<IDirect3DTexture9> &t) {
                                          CK(CreateTexture, 64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &t.p,
                                             nullptr);
                                          return true;
                                      }) ||
        !blockedBy<IDirect3DSurface9>("a render target", w,
                                      [](Com<IDirect3DSurface9> &s) {
                                          CK(CreateRenderTarget, 64, 64, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE,
                                             &s.p, nullptr);
                                          return true;
                                      }) ||
        !blockedBy<IDirect3DVertexBuffer9>("a default-pool vertex buffer", w,
                                           [](Com<IDirect3DVertexBuffer9> &b) {
                                               CK(CreateVertexBuffer, 64, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &b.p,
                                                  nullptr);
                                               return true;
                                           }) ||
        !blockedBy<IDirect3DSurface9>("the back buffer held", w, [](Com<IDirect3DSurface9> &s) {
            CK(GetBackBuffer, 0, 0, D3DBACKBUFFER_TYPE_MONO, &s.p);
            return true;
        }))
        return false;
    Com<IDirect3DTexture9> managed;
    Com<IDirect3DQuery9> q;
    CK(CreateTexture, 64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &managed.p, nullptr);
    if (!newQuery(q)) return false;
    D3DPRESENT_PARAMETERS pp = rsParams(w, 256, 256);
    return (resetIs("Reset", pp) && coopIs(D3D_OK) && until([] { return lands(160, 160, 224, 224, 32, 32); })) ||
           failIn("a managed texture and a query alive");
}
// rs-state (an auto D24S8): a 64x64 render-target texture's level bound at target 0 and the application's references
// released (the device's binding blocks nothing); the viewport (8,8) 32x32; CULLMODE NONE, FILLMODE WIREFRAME,
// ALPHABLENDENABLE TRUE, ZENABLE FALSE, LIGHTING FALSE; a managed 4x4 texture written gold at stage 0 and a managed
// vertex buffer at stream 0, both held. Reset at 256x256 D3D_OK: CULLMODE CCW, FILLMODE SOLID, ALPHABLENDENABLE FALSE,
// ZENABLE TRUE (the device has an auto depth-stencil), LIGHTING TRUE; GetTexture(0) and GetStreamSource(0) D3D_OK and
// NULL; the viewport (0,0) 256x256 over 0..1 and the scissor rect the whole. One attempt, Z on: a rust quad over
// (160,160)-(224,224) and the managed texture under a white quad over (32,160)-(96,224) (MODULATE): rust at (192,192)
// and gold at (64,192), outside the old target and viewport, blue at (128,32) and (24,24) (the Clear reached the back
// buffer, not the old target).
static bool rowRsState() {
    RsDevice r;
    if (!rsDevice(r, D3DFMT_D24S8)) return false;
    const OnDevice on(r.d.p, nullptr);
    Com<IDirect3DTexture9> managed;
    Com<IDirect3DVertexBuffer9> vb;
    CK(CreateTexture, 4, 4, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &managed.p, nullptr);
    if (!underLevelLock(managed.p, 0, 0, [](BYTE *b, int p) { paint(b, p, 0, 0, 4, 4, KGOLD); })) return false;
    CK(CreateVertexBuffer, 4 * sizeof(RV), 0, FVF_RV, D3DPOOL_MANAGED, &vb.p, nullptr);
    {
        Com<IDirect3DTexture9> t;
        Com<IDirect3DSurface9> s;
        if (!makeRt(t) || !levelOf(t.p, 0, s)) return false;
        CK(SetRenderTarget, 0, s.p);
    }
    static const D3DVIEWPORT9 small = {8, 8, 32, 32, 0.0f, 1.0f};
    CK(SetViewport, &small);
    static const DWORD set[][2] = {{D3DRS_CULLMODE, D3DCULL_NONE},
                                   {D3DRS_FILLMODE, D3DFILL_WIREFRAME},
                                   {D3DRS_ALPHABLENDENABLE, TRUE},
                                   {D3DRS_ZENABLE, D3DZB_FALSE},
                                   {D3DRS_LIGHTING, FALSE}};
    for (const auto &s : set) CK(SetRenderState, (D3DRENDERSTATETYPE)s[0], s[1]);
    CK(SetTexture, 0, managed.p);
    CK(SetStreamSource, 0, vb.p, 0, sizeof(RV));
    D3DPRESENT_PARAMETERS pp = rsParams(r.w.h, 256, 256, D3DFMT_D24S8);
    if (!resetIs("Reset (states and bindings set)", pp)) return false;
    static const DWORD want[][2] = {{D3DRS_CULLMODE, D3DCULL_CCW},
                                    {D3DRS_FILLMODE, D3DFILL_SOLID},
                                    {D3DRS_ALPHABLENDENABLE, FALSE},
                                    {D3DRS_ZENABLE, D3DZB_TRUE},
                                    {D3DRS_LIGHTING, TRUE}};
    for (const auto &s : want) {
        DWORD v = 0xdeadbeef;
        CK(GetRenderState, (D3DRENDERSTATETYPE)s[0], &v);
        if (v != s[1])
            return snprintf(why, sizeof why, "GetRenderState(%lu) %lu, want %lu", (unsigned long)s[0],
                            (unsigned long)v, (unsigned long)s[1]),
                   false;
    }
    IDirect3DBaseTexture9 *tex = nullptr;
    HRESULT hr = dev->GetTexture(0, &tex);
    if (tex) tex->Release();
    if (hr != D3D_OK || tex) return fail(tex ? "GetTexture(0) a texture" : "GetTexture(0)", hr);
    IDirect3DVertexBuffer9 *stream = nullptr;
    UINT offset = 7, stride = 7;
    hr = dev->GetStreamSource(0, &stream, &offset, &stride);
    if (stream) stream->Release();
    if (hr != D3D_OK || stream) return fail(stream ? "GetStreamSource(0) a buffer" : "GetStreamSource(0)", hr);
    if (!extentIs(256, 256)) return false;
    const DWORD white = D3DCOLOR_XRGB(255, 255, 255);
    const TV q[4] = {{32, 160, 0.5f, 1, white, 0, 0}, {96, 160, 0.5f, 1, white, 1, 0},
                     {32, 224, 0.5f, 1, white, 0, 1}, {96, 224, 0.5f, 1, white, 1, 1}};
    return until([&] {
               if (!clearBlue(true) || !quadIn(160, 160, 224, 224, KRUST)) return false;
               const bool drawn = scene([&] {
                   CK(SetFVF, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
                   CK(SetTexture, 0, managed.p);
                   CK(DrawPrimitiveUP, D3DPT_TRIANGLESTRIP, 2, q, sizeof *q);
                   return true;
               });
               dev->SetTexture(0, nullptr);
               return drawn && bbHas({{192, 192, KRUST}, {64, 192, KGOLD}, {128, 32, BLUE}, {24, 24, BLUE}});
           }) ||
           failIn("after the Reset");
}
// rs-autogen (no auto depth-stencil): an AUTOGENMIPMAP 64x64 X8R8G8B8 render-target texture's level 0 at target 0 and a
// plain one's at target 1, cleared green, the application's references released while both are bound. Reset at
// 256x256 D3D_OK (the device's bindings block nothing); GetRenderTarget(1) D3DERR_NOTFOUND and NULL;
// GetRenderTarget(0) the back buffer (GetBackBuffer's object); a quad over (160,160)-(224,224) lands (blue at (32,32),
// inside the old targets' extent).
static bool rowRsAutogen() {
    RsDevice r;
    if (!rsDevice(r)) return false;
    const OnDevice on(r.d.p, nullptr);
    {
        Com<IDirect3DTexture9> a, b;
        Com<IDirect3DSurface9> a0, b0;
        if (!makeRt(a, 64, 0, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET | D3DUSAGE_AUTOGENMIPMAP) ||
            !makeRt(b, 64, 1, D3DFMT_X8R8G8B8) || !levelOf(a.p, 0, a0) || !levelOf(b.p, 0, b0))
            return false;
        CK(SetRenderTarget, 0, a0.p);
        CK(SetRenderTarget, 1, b0.p);
        if (!clearTo(TGRN)) return false;
    }
    D3DPRESENT_PARAMETERS pp = rsParams(r.w.h, 256, 256);
    if (!resetIs("Reset (targets 0 and 1 released while bound)", pp)) return false;
    IDirect3DSurface9 *const mark = reinterpret_cast<IDirect3DSurface9 *>(static_cast<INT_PTR>(0x5a5a));
    IDirect3DSurface9 *one = mark;
    HRESULT hr = dev->GetRenderTarget(1, &one);
    if (SUCCEEDED(hr) && one && one != mark) one->Release();
    if (hr != D3DERR_NOTFOUND) return fail("GetRenderTarget(1)", hr);
    if (one) return snprintf(why, sizeof why, "GetRenderTarget(1) left %p", (void *)one), false;
    {
        Com<IDirect3DSurface9> rt0, bb;
        CK(GetRenderTarget, 0, &rt0.p);
        CK(GetBackBuffer, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb.p);
        if (!rt0.p || rt0.p != bb.p)
            return snprintf(why, sizeof why, "GetRenderTarget(0) is not the back buffer"), false;
    }
    return until([] { return lands(160, 160, 224, 224, 32, 32); }) || failIn("after the Reset");
}
// rs-scene (no auto depth-stencil): BeginScene, Reset D3D_OK, EndScene D3DERR_INVALIDCALL (the Reset ended the scene),
// BeginScene and EndScene D3D_OK. A 64x64 default-pool texture alive, BeginScene, Reset D3DERR_INVALIDCALL, EndScene
// D3DERR_INVALIDCALL (the refused Reset ended it too); the texture released, Reset and TestCooperativeLevel D3D_OK, a
// quad lands.
static bool rowRsScene() {
    RsDevice r;
    if (!rsDevice(r)) return false;
    const OnDevice on(r.d.p, nullptr);
    D3DPRESENT_PARAMETERS pp = rsParams(r.w.h, 256, 256);
    CK(BeginScene);
    if (!resetIs("Reset (in a scene)", pp)) return dev->EndScene(), false;
    if (!answer("EndScene (after the Reset)", dev->EndScene(), D3DERR_INVALIDCALL)) return false;
    CK(BeginScene);
    CK(EndScene);
    {
        Com<IDirect3DTexture9> t;
        CK(CreateTexture, 64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &t.p, nullptr);
        CK(BeginScene);
        if (!resetIs("Reset (in a scene, a default-pool texture alive)", pp, D3DERR_INVALIDCALL))
            return dev->EndScene(), false;
        if (!answer("EndScene (after the refused Reset)", dev->EndScene(), D3DERR_INVALIDCALL)) return false;
    }
    return (resetIs("Reset (released)", pp) && coopIs(D3D_OK) &&
            until([] { return lands(160, 160, 224, 224, 32, 32); })) ||
           failIn("after the refused Reset");
}
// rs-invalid (no auto depth-stencil): Reset with swap effect 0 D3DERR_INVALIDCALL, nothing changed:
// TestCooperativeLevel D3D_OK, a quad lands, until()'s Present D3D_OK. Reset with MultiSampleType 3 (no power of two:
// no device makes it) and, after a valid one, Reset with an auto depth-stencil of X8R8G8B8 (no depth format): each
// D3DERR_INVALIDCALL, then TestCooperativeLevel and Present D3DERR_DEVICENOTRESET, a valid Reset and
// TestCooperativeLevel D3D_OK. A quad lands.
static bool rowRsInvalid() {
    RsDevice r;
    if (!rsDevice(r)) return false;
    const OnDevice on(r.d.p, nullptr);
    D3DPRESENT_PARAMETERS pp = rsParams(r.w.h, 256, 256);
    pp.SwapEffect = (D3DSWAPEFFECT)0;
    if (!resetIs("Reset (swap effect 0)", pp, D3DERR_INVALIDCALL)) return false;
    if (!coopIs(D3D_OK) || !until([] { return lands(160, 160, 224, 224, 32, 32); })) return failIn("swap effect 0");
    for (const bool samples : {true, false}) {
        const char *what = samples ? "Reset (MultiSampleType 3)" : "Reset (an auto depth-stencil of X8R8G8B8)";
        pp = rsParams(r.w.h, 256, 256);
        if (samples) pp.MultiSampleType = (D3DMULTISAMPLE_TYPE)3;
        else pp.EnableAutoDepthStencil = TRUE, pp.AutoDepthStencilFormat = D3DFMT_X8R8G8B8;
        if (!resetIs(what, pp, D3DERR_INVALIDCALL)) return false;
        pp = rsParams(r.w.h, 256, 256);
        if (!coopIs(D3DERR_DEVICENOTRESET) || !presentIs(D3DERR_DEVICENOTRESET) || !resetIs("Reset (valid)", pp) ||
            !coopIs(D3D_OK))
            return failIn(what);
    }
    return until([] { return lands(160, 160, 224, 224, 32, 32); }) || failIn("after the valid Reset");
}
// rs-refuse, under the launch's "strict" alone (the core lanes; elsewhere it does nothing and passes: mtld3d makes this
// Reset, with a back buffer of a format it draws). No auto depth-stencil, nothing held: a windowed 256x256 Reset to an
// R5G6B5 back buffer D3DERR_NOTAVAILABLE (the core refuses it by name, "IDirect3DDevice9::Reset (a back-buffer format
// the core does not make)", after mtld3d's validation, which R5G6B5 at one sample passes); TestCooperativeLevel D3D_OK
// (no reset owed); a quad lands, until()'s Present D3D_OK (the device still usable). Since DXMTRS C4c-6d the launch's
// one refusal: a Reset into fullscreen is the core's (the fullscreen launch).
static bool rowRsRefuse() {
    if (!strictMode) return true;
    RsDevice r;
    if (!rsDevice(r)) return false;
    const OnDevice on(r.d.p, nullptr);
    D3DPRESENT_PARAMETERS pp = rsParams(r.w.h, 256, 256);
    pp.BackBufferFormat = D3DFMT_R5G6B5;
    if (!resetIs("Reset (an R5G6B5 back buffer)", pp, D3DERR_NOTAVAILABLE)) return false;
    return (coopIs(D3D_OK) && until([] { return lands(160, 160, 224, 224, 32, 32); })) ||
           failIn("after the refused R5G6B5 Reset");
}
// rs-window (no auto depth-stencil): a second visible plain window, client area 192x160. Reset naming it, 256x256,
// D3D_OK: a quad lands, a present. Reset naming no window, 0x0, D3D_OK: the struct reads the second window's client
// area back (GetClientRect; a zero size resolves against the device's window, which the first Reset made the second)
// and keeps its NULL window; the back buffer is that size, a quad 8 pixels in from its corner lands, a present. Which
// window the presents reach no row can tell (a present into a hidden window commits too): stated, not judged.
static bool rowRsWindow() {
    Window b;
    if (!rsWindow(b, 192, 160, 700)) return false;
    RsDevice r;
    if (!rsDevice(r)) return false;
    const OnDevice on(r.d.p, nullptr);
    RECT c = {};
    if (!GetClientRect(b.h, &c) || c.right <= 0 || c.bottom <= 0)
        return snprintf(why, sizeof why, "GetClientRect (the second window)"), false;
    const UINT cw = (UINT)c.right, ch = (UINT)c.bottom;
    D3DPRESENT_PARAMETERS pp = rsParams(b.h, 256, 256);
    if (!resetIs("Reset (onto a second window)", pp)) return false;
    if (!bbIs(256, 256) || !until([] { return lands(160, 160, 224, 224, 32, 32); }) || !presents())
        return failIn("on the second window");
    pp = rsParams(nullptr, 0, 0);
    if (!resetIs("Reset (no window named, a zero size)", pp)) return false;
    if (pp.BackBufferWidth != cw || pp.BackBufferHeight != ch || pp.hDeviceWindow)
        return snprintf(why, sizeof why, "Reset (no window named) wrote %ux%u back and window %p, want %ux%u and NULL",
                        pp.BackBufferWidth, pp.BackBufferHeight, (void *)pp.hDeviceWindow, cw, ch),
               false;
    return (bbIs(cw, ch) &&
            until([&] { return lands((int)cw - 40, (int)ch - 40, (int)cw - 8, (int)ch - 8, 8, 8); }) && presents()) ||
           failIn("the second window's client area");
}
// rs-ex (an extended device, no auto depth-stencil): a 64x64 default-pool texture held and bound at stage 0; a 64x64
// CreateRenderTarget surface held and bound at target 0 (an extended device keeps it alive across the reset, so the
// target the draw below reaches tells the implicit one from it); the viewport (8,8) 32x32 over 0.25..0.75, CULLMODE
// NONE. ResetEx to 320x240, no mode, D3D_OK (a default-pool resource blocks no extended Reset): the back buffer
// 320x240, the viewport (0,0) 320x240 over 0.25..0.75 (its depth range kept), the scissor rect the whole, CULLMODE
// still NONE, GetTexture(0) still the texture; TestCooperativeLevel and CheckDeviceState D3D_OK; the texture unbound,
// a quad over (240,160)-(304,224) lands in the back buffer. Reset (the base slot) to 256x256 D3D_OK: the viewport
// (0,0) 256x256 over 0.25..0.75. ResetEx with swap effect 0 D3DERR_INVALIDCALL, nothing changed (the viewport as it
// was, TestCooperativeLevel and CheckDeviceState D3D_OK); ResetEx of windowed parameters with a mode
// D3DERR_INVALIDCALL; a quad lands, until()'s Present D3D_OK.
static bool rowRsEx() {
    RsDevice r;
    if (!rsDevice(r, D3DFMT_UNKNOWN, true)) return false;
    const OnDevice on(r.d.p, nullptr);
    Com<IDirect3DTexture9> t;
    Com<IDirect3DSurface9> rt;
    CK(CreateTexture, 64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &t.p, nullptr);
    if (!target64(rt, FALSE)) return false;
    CK(SetRenderTarget, 0, rt.p);
    static const D3DVIEWPORT9 v = {8, 8, 32, 32, 0.25f, 0.75f};
    CK(SetViewport, &v);
    CK(SetRenderState, D3DRS_CULLMODE, D3DCULL_NONE);
    CK(SetTexture, 0, t.p);
    IDirect3DDevice9Ex *const ex = r.ex.p;
    D3DPRESENT_PARAMETERS pp = rsParams(r.w.h, 320, 240);
    if (!answer("ResetEx (to 320x240, a default-pool texture alive)", ex->ResetEx(&pp, nullptr), D3D_OK)) return false;
    DWORD cull = 0;
    CK(GetRenderState, D3DRS_CULLMODE, &cull);
    if (cull != D3DCULL_NONE)
        return snprintf(why, sizeof why, "CULLMODE %lu after ResetEx", (unsigned long)cull), false;
    IDirect3DBaseTexture9 *bound = nullptr;
    HRESULT hr = dev->GetTexture(0, &bound);
    if (bound) bound->Release();
    if (hr != D3D_OK || bound != t.p) return fail(bound ? "GetTexture(0) another texture" : "GetTexture(0)", hr);
    if (!bbIs(320, 240) || !extentIs(320, 240, 0.25f, 0.75f) || !coopIs(D3D_OK) ||
        !answer("CheckDeviceState", ex->CheckDeviceState(r.w.h), D3D_OK))
        return failIn("after ResetEx");
    CK(SetTexture, 0, nullptr);
    if (!until([] { return lands(240, 160, 304, 224, 8, 8); })) return failIn("after ResetEx");
    pp = rsParams(r.w.h, 256, 256);
    if (!resetIs("Reset (the extended device, to 256x256)", pp)) return false;
    if (!extentIs(256, 256, 0.25f, 0.75f)) return failIn("after Reset");
    pp.SwapEffect = (D3DSWAPEFFECT)0;
    if (!answer("ResetEx (swap effect 0)", ex->ResetEx(&pp, nullptr), D3DERR_INVALIDCALL)) return false;
    if (!extentIs(256, 256, 0.25f, 0.75f) || !coopIs(D3D_OK) ||
        !answer("CheckDeviceState", ex->CheckDeviceState(r.w.h), D3D_OK))
        return failIn("after the rejected ResetEx");
    pp = rsParams(r.w.h, 256, 256);
    D3DDISPLAYMODEEX mode = {sizeof mode, 256, 256, 0, D3DFMT_X8R8G8B8, D3DSCANLINEORDERING_PROGRESSIVE};
    if (!answer("ResetEx (windowed, a mode)", ex->ResetEx(&pp, &mode), D3DERR_INVALIDCALL)) return false;
    return until([] { return lands(160, 160, 224, 224, 32, 32); }) || failIn("after the rejected ResetEx");
}
// rs-held (an extended device, an auto D24S8): the back buffer and the auto depth-stencil held (GetBackBuffer,
// GetDepthStencilSurface) across ResetEx to 320x240, no mode, D3D_OK: the held ones still 256x256 X8R8G8B8 and
// 256x256 D24S8; GetBackBuffer and GetDepthStencilSurface new objects, 320x240 X8R8G8B8 and D24S8; depthLands, the
// held ones still held (nothing is asked of them but GetDesc).
static bool rowRsHeld() {
    RsDevice r;
    if (!rsDevice(r, D3DFMT_D24S8, true)) return false;
    const OnDevice on(r.d.p, nullptr);
    Com<IDirect3DSurface9> hb, hd, nb, nd;
    CK(GetBackBuffer, 0, 0, D3DBACKBUFFER_TYPE_MONO, &hb.p);
    CK(GetDepthStencilSurface, &hd.p);
    if (!hb.p || !hd.p) return snprintf(why, sizeof why, "an implicit surface D3D_OK and NULL"), false;
    D3DPRESENT_PARAMETERS pp = rsParams(r.w.h, 320, 240, D3DFMT_D24S8);
    if (!answer("ResetEx (to 320x240, both implicit surfaces held)", r.ex.p->ResetEx(&pp, nullptr), D3D_OK))
        return false;
    if (!descIs("the held back buffer", hb.p, 256, 256, D3DFMT_X8R8G8B8) ||
        !descIs("the held depth-stencil", hd.p, 256, 256, D3DFMT_D24S8))
        return false;
    CK(GetBackBuffer, 0, 0, D3DBACKBUFFER_TYPE_MONO, &nb.p);
    CK(GetDepthStencilSurface, &nd.p);
    if (!nb.p || !nd.p || nb.p == hb.p || nd.p == hd.p)
        return snprintf(why, sizeof why,
                        "after ResetEx GetBackBuffer %p (held %p), GetDepthStencilSurface %p (held %p)", (void *)nb.p,
                        (void *)hb.p, (void *)nd.p, (void *)hd.p),
               false;
    return (descIs("the new back buffer", nb.p, 320, 240, D3DFMT_X8R8G8B8) &&
            descIs("the new depth-stencil", nd.p, 320, 240, D3DFMT_D24S8) && until(depthLands)) ||
           failIn("after ResetEx");
}
// rs-query (no auto depth-stencil): an EVENT query's END after a quad; an OCCLUSION query's BEGIN, a quad over
// (64,16)-(96,48) (32x32: 1024), END; a second OCCLUSION query's BEGIN, a quad over (16,64)-(48,80) (32x16: 512), its
// span left open. Reset to 320x240 (a resizing Reset: mtld3d's encoder cuts an open span at the drain and reopens it
// after) D3D_OK; a quad over (260,160)-(276,168) (16x8: 128, past the creation size), the second's END. Read after the
// Reset, each waitFor (FLUSH polls): the EVENT S_OK and TRUE, the first 1024, the second 640 (512 + 128). A present.
// Every BEGIN is ENDed on a passing path.
static bool rowRsQuery() {
    RsDevice r;
    if (!rsDevice(r)) return false;
    const OnDevice on(r.d.p, nullptr);
    Com<IDirect3DQuery9> e, c, o;
    if (!newQuery(e, D3DQUERYTYPE_EVENT) || !newQuery(c) || !newQuery(o)) return false;
    if (!quadIn(16, 16, 48, 48) || !issue(e.p, D3DISSUE_END) || !issue(c.p, D3DISSUE_BEGIN) ||
        !quadIn(64, 16, 96, 48) || !issue(c.p, D3DISSUE_END) || !issue(o.p, D3DISSUE_BEGIN) || !quadIn(16, 64, 48, 80))
        return failIn("before the Reset");
    D3DPRESENT_PARAMETERS pp = rsParams(r.w.h, 320, 240);
    if (!resetIs("Reset (to 320x240, queries in flight)", pp)) return false;
    if (!rstate(D3DRS_ZENABLE, D3DZB_FALSE) || !quadIn(260, 160, 276, 168) || !issue(o.p, D3DISSUE_END))
        return failIn("after the Reset");
    return ((waitFor(e.p) && signalled(e.p, "the EVENT")) || failIn("the EVENT ended before the Reset")) &&
           ((waitFor(c.p) && countIs(c.p, 1024)) || failIn("the OCCLUSION ended before the Reset")) &&
           ((waitFor(o.p) && countIs(o.p, 640)) || failIn("the OCCLUSION open across the Reset")) &&
           (presents() || failIn("after the Reset"));
}
// rs-twice (an auto D24S8): Resets in a row to 320x240, 192x288, 384x384 and 256x256 (with D24S8), each D3D_OK and
// followed by: the back buffer that size; Z on, a quad 8 pixels in from its bottom-right corner lands (blue at
// (8,8)); a present (Z cleared).
static bool rowRsTwice() {
    RsDevice r;
    if (!rsDevice(r, D3DFMT_D24S8)) return false;
    const OnDevice on(r.d.p, nullptr);
    static const UINT sizes[][2] = {{320, 240}, {192, 288}, {384, 384}, {256, 256}};
    for (const auto &s : sizes) {
        const int w = (int)s[0], h = (int)s[1];
        char what[48];
        snprintf(what, sizeof what, "Reset (to %dx%d)", w, h);
        D3DPRESENT_PARAMETERS pp = rsParams(r.w.h, s[0], s[1], D3DFMT_D24S8);
        if (!resetIs(what, pp)) return false;
        if (!bbIs(s[0], s[1]) || !until([&] { return lands(w - 40, h - 40, w - 8, h - 8, 8, 8, true); }) ||
            !presents(true))
            return failIn(what);
    }
    return true;
}

static void resetRows(IDirect3DDevice9 *d, bool strict) {
    static const struct { const char *name; bool (*run)(); } rows[] = {
        {"rs-size", rowRsSize},       {"rs-depth", rowRsDepth},   {"rs-msaa", rowRsMsaa},
        {"rs-blockers", rowRsBlockers}, {"rs-state", rowRsState}, {"rs-autogen", rowRsAutogen},
        {"rs-scene", rowRsScene},     {"rs-invalid", rowRsInvalid}, {"rs-refuse", rowRsRefuse},
        {"rs-window", rowRsWindow},   {"rs-ex", rowRsEx},         {"rs-held", rowRsHeld},
        {"rs-query", rowRsQuery},     {"rs-twice", rowRsTwice}};
    dev = d, strictMode = strict, budgetEnd = GetTickCount64() + 200000;
    for (const auto &r : rows) {
        why[0] = 0;
        bool ok;
        if (GetTickCount64() >= budgetEnd) ok = false, snprintf(why, sizeof why, "budget");
        else ok = r.run();
        row(r.name, ok, why);
    }
}

// --- The "extras" mode (DXMTRS C4c-7a): gamma, frame latency, the Ex creators, RESZ and the cursor calls. Each row
// makes a device and a window of its own (rsDevice's; an extended one where a row says so) and, while it runs, the
// globals the helpers read name that device and its back buffer (OnDevice: until()'s rebind and clearRt read rtSurf);
// no row makes a Reset. Each cell wants what mtld3d, the default runtime, answers, in every lane, but in two rows:
// rz-self and rz-refuse do nothing and pass unless "strict". A row that draws judges it in one until() attempt with
// fresh objects (mtld3d leaves a draw out while a new device's pipelines build); every judged draw sets ZENABLE
// itself, and stage 0 is left unbound on every path. No row calls SetCursorPosition or reads the pointer's position.
// The core before DXMTRS C4c-7a's front end refuses every call the mode is about (D3DERR_NOTAVAILABLE; the gamma calls
// do nothing, ShowCursor answers FALSE): each row but rz-refuse fails at its first such call (rz-refuse wants the
// refusal it already gives). The mode's budget is 200 s, the launches' watchdog 300 s.

// A row's device (rsDevice) and its back buffer, for OnDevice; declared after the RsDevice, released before it.
static bool exDevice(RsDevice &r, Com<IDirect3DSurface9> &bb, D3DFORMAT depth = D3DFMT_UNKNOWN, bool extended = false,
                     D3DMULTISAMPLE_TYPE ms = D3DMULTISAMPLE_NONE) {
    if (!rsDevice(r, depth, extended, ms)) return false;
    const HRESULT hr = r.d.p->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb.p);
    return (SUCCEEDED(hr) && bb.p) || fail("GetBackBuffer", hr);
}
// A ramp whose channel c (red, green, blue) holds level(c, i) at entry i.
template <class F> static D3DGAMMARAMP gammaRamp(F level) {
    D3DGAMMARAMP g;
    for (int i = 0; i < 256; i++) g.red[i] = level(0, i), g.green[i] = level(1, i), g.blue[i] = level(2, i);
    return g;
}
// GetGammaRamp(sc) into a ramp of 0xab bytes (a marker no channel of which rises), then all 768 levels against `want`;
// `why` names the first that differs.
static bool gammaIs(const char *what, UINT sc, const D3DGAMMARAMP &want) {
    D3DGAMMARAMP got;
    memset(&got, 0xab, sizeof got);
    dev->GetGammaRamp(sc, &got);
    static const char *const names[3] = {"red", "green", "blue"};
    const WORD *g[3] = {got.red, got.green, got.blue}, *w[3] = {want.red, want.green, want.blue};
    for (int c = 0; c < 3; c++)
        for (int i = 0; i < 256; i++)
            if (g[c][i] != w[c][i])
                return snprintf(why, sizeof why, "%s: %s[%d] %#x, want %#x", what, names[c], i, (unsigned)g[c][i],
                                (unsigned)w[c][i]),
                       false;
    return true;
}
// gm-ramp (a plain device): GetGammaRamp(0) into gammaIs's marker reads the identity (entry i of each channel i * 257,
// the ramp a device starts with: only a Get that ran writes it over the marker); SetGammaRamp(0) of a rising ramp
// (red i * 200, green i * 100 + 7, blue i * 256, which no device starts with), and Get reads it back (only a stored Set
// does); a flat ramp (every entry 0x8000: no channel rises) is ignored, Get unchanged; a null ramp to Set and to Get,
// and a Set on swap chain 1 of another rising ramp (i * 150), change nothing, Get unchanged; Get on swap chain 1 writes
// nothing (its marker stays). The null and swap-chain-1 cells alone would pass on a call that does nothing: they ride
// on the first two. A windowed device stores the ramp and applies none: nothing changes on screen.
static bool rowGmRamp() {
    RsDevice r;
    Com<IDirect3DSurface9> bb;
    if (!exDevice(r, bb)) return false;
    const OnDevice on(r.d.p, bb.p);
    const D3DGAMMARAMP identity = gammaRamp([](int, int i) { return (WORD)(i * 257); }),
                       rising = gammaRamp([](int c, int i) {
                           return (WORD)(c == 0 ? i * 200 : c == 1 ? i * 100 + 7 : i * 256);
                       }),
                       flat = gammaRamp([](int, int) { return (WORD)0x8000; }),
                       other = gammaRamp([](int, int i) { return (WORD)(i * 150); });
    D3DGAMMARAMP marker;
    memset(&marker, 0xab, sizeof marker);
    if (!gammaIs("GetGammaRamp (the device's own)", 0, identity)) return false;
    dev->SetGammaRamp(0, D3DSGR_NO_CALIBRATION, &rising);
    if (!gammaIs("GetGammaRamp (after a rising ramp)", 0, rising)) return false;
    dev->SetGammaRamp(0, D3DSGR_NO_CALIBRATION, &flat);
    if (!gammaIs("GetGammaRamp (after a flat ramp)", 0, rising)) return false;
    dev->SetGammaRamp(0, D3DSGR_NO_CALIBRATION, nullptr);
    dev->GetGammaRamp(0, nullptr);
    dev->SetGammaRamp(1, D3DSGR_NO_CALIBRATION, &other);
    return gammaIs("GetGammaRamp (after null ramps and swap chain 1's)", 0, rising) &&
           gammaIs("GetGammaRamp (swap chain 1)", 1, marker);
}
// GetMaximumFrameLatency into a marker: D3D_OK and exactly `expect`.
static bool latencyIs(IDirect3DDevice9Ex *ex, const char *what, UINT expect) {
    UINT v = 0xdeadbeef;
    return answer(what, ex->GetMaximumFrameLatency(&v), D3D_OK) && exact(what, v, expect);
}
// fl-latency (an extended device, no depth-stencil): Get 3 (the default); Set 30 D3D_OK, Get 30 (only a stored Set
// reads 30); Set 31 D3DERR_INVALIDCALL (past the limit), Get still 30; Set 0 D3D_OK (the default again), Get 3; Get
// (NULL) D3DERR_INVALIDCALL. Set 1, then three frames, each a quad 56 pixels right of the last, read back (blue where
// the last one was: this frame's back buffer) and presented (until()); Set 0 and one more frame. The presents show
// commits and no hang at a bound of 1 and after a Set 0, not the bound itself: no row can tell how far the CPU ran
// ahead.
static bool rowFlLatency() {
    RsDevice r;
    Com<IDirect3DSurface9> bb;
    if (!exDevice(r, bb, D3DFMT_UNKNOWN, true)) return false;
    const OnDevice on(r.d.p, bb.p);
    IDirect3DDevice9Ex *const ex = r.ex.p;
    if (!latencyIs(ex, "GetMaximumFrameLatency (the default)", 3) ||
        !answer("SetMaximumFrameLatency(30)", ex->SetMaximumFrameLatency(30), D3D_OK) ||
        !latencyIs(ex, "GetMaximumFrameLatency (after 30)", 30) ||
        !answer("SetMaximumFrameLatency(31)", ex->SetMaximumFrameLatency(31), D3DERR_INVALIDCALL) ||
        !latencyIs(ex, "GetMaximumFrameLatency (after the refused 31)", 30) ||
        !answer("SetMaximumFrameLatency(0)", ex->SetMaximumFrameLatency(0), D3D_OK) ||
        !latencyIs(ex, "GetMaximumFrameLatency (after 0)", 3) ||
        !answer("GetMaximumFrameLatency (NULL)", ex->GetMaximumFrameLatency(nullptr), D3DERR_INVALIDCALL) ||
        !answer("SetMaximumFrameLatency(1)", ex->SetMaximumFrameLatency(1), D3D_OK))
        return false;
    for (int i = 0; i < 4; i++) {
        if (i == 3 && !answer("SetMaximumFrameLatency(0) (after the frames)", ex->SetMaximumFrameLatency(0), D3D_OK))
            return false;
        const int x = 16 + 56 * i;
        char what[48];
        snprintf(what, sizeof what, i < 3 ? "frame %d at a latency of 1" : "frame %d, after a Set 0", i + 1);
        if (!until([&] { return lands(x, 96, x + 32, 160, i ? x - 40 : 200, i ? 128 : 32); })) return failIn(what);
    }
    return true;
}
// GetDesc's Usage and Pool of `s`: exactly `usage` and `pool`.
static bool usageIs(const char *name, IDirect3DSurface9 *s, DWORD usage, D3DPOOL pool) {
    D3DSURFACE_DESC d = {};
    const HRESULT hr = s->GetDesc(&d);
    if (FAILED(hr)) return fail("GetDesc", hr) || failIn(name);
    if (d.Usage == usage && d.Pool == pool) return true;
    return snprintf(why, sizeof why, "%s usage %#lx pool %d; want usage %#lx pool %d", name, (unsigned long)d.Usage,
                    (int)d.Pool, (unsigned long)usage, (int)pool),
           false;
}
// An Ex create into a slot holding noDepth's marker (a raw pointer, never a Com<>: its Release of the marker would end
// the launch): `expect`, and the slot as passed (`kept`) or NULL; a surface it made is released.
template <class F> static bool slotAfter(const char *what, HRESULT expect, bool kept, F create) {
    IDirect3DSurface9 *const mark = reinterpret_cast<IDirect3DSurface9 *>(static_cast<INT_PTR>(0x5a5a));
    IDirect3DSurface9 *s = mark;
    const HRESULT hr = create(&s);
    if (SUCCEEDED(hr) && s && s != mark) s->Release();
    if (hr != expect) return fail(what, hr);
    IDirect3DSurface9 *const slot = kept ? mark : nullptr;
    return s == slot || (snprintf(why, sizeof why, "%s left %p, want %p", what, (void *)s, (void *)slot), false);
}
// ex-rt (an extended device): CreateRenderTargetEx 64x64 A8R8G8B8, one sample, not lockable, usage 0: D3D_OK, its
// description (64x64 A8R8G8B8, one sample, Usage RENDERTARGET, Pool DEFAULT); bound, cleared steel, a rust quad over
// (16,16)-(48,48), read back through GetRenderTargetData: rust at (32,32), steel at (56,8) (only a made surface a pass
// reaches holds them). Usage RENDERTARGET (no extended usage): D3DERR_INVALIDCALL and the slot as passed;
// D3DFMT_UNKNOWN, usage 0: D3DERR_INVALIDCALL from the base creator and the slot NULL (a refusal that skipped the base
// creator would leave the marker).
static bool rowExRt() {
    RsDevice r;
    Com<IDirect3DSurface9> bb, s;
    if (!exDevice(r, bb, D3DFMT_UNKNOWN, true)) return false;
    const OnDevice on(r.d.p, bb.p);
    IDirect3DDevice9Ex *const ex = r.ex.p;
    const HRESULT hr = ex->CreateRenderTargetEx(64, 64, A8, D3DMULTISAMPLE_NONE, 0, FALSE, &s.p, nullptr, 0);
    if (hr != D3D_OK || !s.p) return fail("CreateRenderTargetEx (usage 0)", hr);
    if (!descIs("the Ex target", s.p, 64, 64, A8) ||
        !usageIs("the Ex target", s.p, D3DUSAGE_RENDERTARGET, D3DPOOL_DEFAULT))
        return false;
    if (!until([&] {
            return rstate(D3DRS_ZENABLE, D3DZB_FALSE) && clearRt(s.p, KSTEEL) && quadIn(16, 16, 48, 48) &&
                   backToScreen() && rtHas("the Ex target", s.p, 64, A8, {{32, 32, KRUST}, {56, 8, KSTEEL}});
        }))
        return failIn("drawn into");
    return slotAfter("CreateRenderTargetEx (usage RENDERTARGET)", D3DERR_INVALIDCALL, true,
                     [&](IDirect3DSurface9 **o) {
                         return ex->CreateRenderTargetEx(64, 64, A8, D3DMULTISAMPLE_NONE, 0, FALSE, o, nullptr,
                                                         D3DUSAGE_RENDERTARGET);
                     }) &&
           slotAfter("CreateRenderTargetEx (D3DFMT_UNKNOWN)", D3DERR_INVALIDCALL, false, [&](IDirect3DSurface9 **o) {
               return ex->CreateRenderTargetEx(64, 64, D3DFMT_UNKNOWN, D3DMULTISAMPLE_NONE, 0, FALSE, o, nullptr, 0);
           });
}
// ex-ds (an extended device, no auto depth-stencil): CreateDepthStencilSurfaceEx 256x256 D24S8, one sample, usage 0:
// D3D_OK, its description (256x256 D24S8, Usage DEPTHSTENCIL, Pool DEFAULT); bound, Z cleared to 1 and on, a red quad
// at z 0.25 over (32,96)-(160,160), then a green one at 0.75 over (96,96)-(224,160): red at (64,128) and (128,128)
// (only a bound, tested depth-stencil hides the later draw), green at (192,128), blue at (128,32). Usage DEPTHSTENCIL:
// D3DERR_INVALIDCALL and the slot as passed; D3DFMT_UNKNOWN: D3DERR_INVALIDCALL and the slot NULL; usage
// RESTRICTED_CONTENT: D3D_OK and GetDesc's Usage DEPTHSTENCIL | RESTRICTED_CONTENT (only the Ex create reports the
// extended bit).
static bool rowExDs() {
    RsDevice r;
    Com<IDirect3DSurface9> bb, ds, rc;
    if (!exDevice(r, bb, D3DFMT_UNKNOWN, true)) return false;
    const OnDevice on(r.d.p, bb.p);
    IDirect3DDevice9Ex *const ex = r.ex.p;
    HRESULT hr =
        ex->CreateDepthStencilSurfaceEx(256, 256, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &ds.p, nullptr, 0);
    if (hr != D3D_OK || !ds.p) return fail("CreateDepthStencilSurfaceEx (usage 0)", hr);
    if (!descIs("the Ex depth-stencil", ds.p, 256, 256, D3DFMT_D24S8) ||
        !usageIs("the Ex depth-stencil", ds.p, D3DUSAGE_DEPTHSTENCIL, D3DPOOL_DEFAULT))
        return false;
    CK(SetDepthStencilSurface, ds.p);
    if (!until([] {
            return clearBlue(true) &&
                   scene([] { return zquad(32, 96, 160, 160, 0.25f, TRED) && zquad(96, 96, 224, 160, 0.75f, TGRN); }) &&
                   bbHas({{64, 128, TRED}, {128, 128, TRED}, {192, 128, TGRN}, {128, 32, BLUE}});
        }))
        return failIn("bound");
    CK(SetDepthStencilSurface, nullptr);
    if (!slotAfter("CreateDepthStencilSurfaceEx (usage DEPTHSTENCIL)", D3DERR_INVALIDCALL, true,
                   [&](IDirect3DSurface9 **o) {
                       return ex->CreateDepthStencilSurfaceEx(256, 256, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, o,
                                                              nullptr, D3DUSAGE_DEPTHSTENCIL);
                   }) ||
        !slotAfter("CreateDepthStencilSurfaceEx (D3DFMT_UNKNOWN)", D3DERR_INVALIDCALL, false,
                   [&](IDirect3DSurface9 **o) {
                       return ex->CreateDepthStencilSurfaceEx(256, 256, D3DFMT_UNKNOWN, D3DMULTISAMPLE_NONE, 0, FALSE,
                                                              o, nullptr, 0);
                   }))
        return false;
    hr = ex->CreateDepthStencilSurfaceEx(256, 256, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &rc.p, nullptr,
                                         D3DUSAGE_RESTRICTED_CONTENT);
    if (hr != D3D_OK || !rc.p) return fail("CreateDepthStencilSurfaceEx (RESTRICTED_CONTENT)", hr);
    return usageIs("the restricted-content depth-stencil", rc.p, D3DUSAGE_DEPTHSTENCIL | D3DUSAGE_RESTRICTED_CONTENT,
                   D3DPOOL_DEFAULT);
}
// ex-plain (an extended device): CreateOffscreenPlainSurfaceEx 64x64 A8R8G8B8 with usage 0 in the default,
// system-memory and scratch pools, each D3D_OK: a texel written at (5,7) under LockRect(0) reads back under a second,
// READONLY lock (only a made, lockable surface holds it); the managed pool D3DERR_INVALIDCALL; usage DYNAMIC (no
// extended usage) D3DERR_INVALIDCALL and the slot as passed; usage RESTRICTED_CONTENT in the default pool D3D_OK, and
// GetDesc's Usage exactly RESTRICTED_CONTENT (a plain surface's own usage is 0: rt-plain) and Pool DEFAULT.
static bool rowExPlain() {
    RsDevice r;
    Com<IDirect3DSurface9> bb, rc;
    if (!exDevice(r, bb, D3DFMT_UNKNOWN, true)) return false;
    const OnDevice on(r.d.p, bb.p);
    IDirect3DDevice9Ex *const ex = r.ex.p;
    static const struct { D3DPOOL pool; const char *name; } pools[] = {
        {D3DPOOL_DEFAULT, "the default pool"}, {D3DPOOL_SYSTEMMEM, "the system-memory pool"},
        {D3DPOOL_SCRATCH, "the scratch pool"}};
    for (const auto &k : pools) {
        Com<IDirect3DSurface9> s;
        const DWORD v = 0x80402010;
        DWORD got = 0;
        const HRESULT hr = ex->CreateOffscreenPlainSurfaceEx(64, 64, A8, k.pool, &s.p, nullptr, 0);
        if (hr != D3D_OK || !s.p) return fail("CreateOffscreenPlainSurfaceEx (usage 0)", hr) || failIn(k.name);
        if (!underLock(s.p, "LockRect", nullptr, 0, [&](BYTE *b, int p) { paint(b, p, 5, 7, 6, 8, v); }) ||
            !underLock(s.p, "LockRect (READONLY)", nullptr, D3DLOCK_READONLY,
                       [&](BYTE *b, int p) { got = texel(b, p, 5, 7); }) ||
            !want("(5,7) locked again", got, v, 0xffffffff))
            return failIn(k.name);
    }
    IDirect3DSurface9 *m = nullptr;
    HRESULT hr = ex->CreateOffscreenPlainSurfaceEx(64, 64, A8, D3DPOOL_MANAGED, &m, nullptr, 0);
    if (m) m->Release();
    if (hr != D3DERR_INVALIDCALL) return fail("CreateOffscreenPlainSurfaceEx (the managed pool)", hr);
    if (!slotAfter("CreateOffscreenPlainSurfaceEx (usage DYNAMIC)", D3DERR_INVALIDCALL, true,
                   [&](IDirect3DSurface9 **o) {
                       return ex->CreateOffscreenPlainSurfaceEx(64, 64, A8, D3DPOOL_DEFAULT, o, nullptr,
                                                                D3DUSAGE_DYNAMIC);
                   }))
        return false;
    hr = ex->CreateOffscreenPlainSurfaceEx(64, 64, A8, D3DPOOL_DEFAULT, &rc.p, nullptr, D3DUSAGE_RESTRICTED_CONTENT);
    if (hr != D3D_OK || !rc.p) return fail("CreateOffscreenPlainSurfaceEx (RESTRICTED_CONTENT)", hr);
    return usageIs("the restricted-content plain surface", rc.p, D3DUSAGE_RESTRICTED_CONTENT, D3DPOOL_DEFAULT);
}

// The RESZ rows (rz-*), each on a device with a 1-sample 256x256 back buffer unless said. A source depth-stencil is
// bound beside a target of its sample count (a Clear or a draw reaches a depth-stencil only beside one: ms-dres) while
// bands() draws four flat depth bands into it; a destination made with D3DUSAGE_DEPTHSTENCIL is first cleared to Z 1
// through its level beside a 1-sample target, so a RESZ that did nothing reads 0xff there, never a band; then the
// destination at stage 0 and RESZ (SetRenderState(D3DRS_POINTSIZE, RESZ)) in the same scene, and the destination read
// with no depth-stencil bound by the fixed-function raw sample ds-intz proves in every lane (sampleAt), into a 1-sample
// target of its own. POINTSIZE is set to 1.0 at each attempt's start, so an attempt reads its own RESZ's store. A
// sample count the adapter does not offer is a FAIL, never a skip.
static const DWORD RESZ = 0x7fa05000;
// The bands' depths: 0.125, 0.375, 0.625 and 0.875 over x 0-63, 64-127, 128-191 and 192-255 (none 0, a fresh core
// depth texture's content, nor 1, the destinations' clear), and their raw INTZ samples (ds-intz's rounding: 0.25 reads
// 0x40).
static const DWORD BAND[4] = {0x202020, 0x606060, 0x9f9f9f, 0xdfdfdf};
static bool bands() {
    if (!rstate(D3DRS_ZENABLE, D3DZB_TRUE) || !rstate(D3DRS_ZFUNC, D3DCMP_ALWAYS)) return false;
    CK(Clear, 0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, BLUE, 1.0f, 0);
    for (int i = 0; i < 4; i++)
        if (!zquad(64.0f * i, 0, 64.0f * i + 64, 256, 0.125f + 0.25f * i, TRED)) return false;
    return true;
}
static bool resz(const char *what, HRESULT expect = D3D_OK) {
    return answer(what, dev->SetRenderState(D3DRS_POINTSIZE, RESZ), expect);
}
static bool pointSizeIs(const char *what, DWORD expect) {
    DWORD v = 0xdeadbeef;
    CK(GetRenderState, D3DRS_POINTSIZE, &v);
    return exact(what, v, expect);
}
// `read` alone at index 0, no depth-stencil bound, the depth test off, cleared blue: what a destination is read into.
static bool readInto(IDirect3DSurface9 *read) {
    return bindRt(0, read) && bindDs(nullptr) && rstate(D3DRS_ZENABLE, D3DZB_FALSE) && clearTo(BLUE);
}
// Whether `ms` samples are offered for an A8R8G8B8 target and a D24S8 depth-stencil (offers()); a FAIL otherwise.
static bool samplesOffered(D3DMULTISAMPLE_TYPE ms) {
    return (offers(ms) && offers(ms, D3DFMT_D24S8)) ||
           (snprintf(why, sizeof why, "%d samples of A8R8G8B8 and D24S8 not both offered (CheckDeviceMultiSampleType)",
                     (int)ms),
            false);
}
// One attempt of a band row: `dst` cleared to Z 1 first when `prefill`; the bands drawn into `src` beside `target`;
// `dst` at stage 0, RESZ D3D_OK and, when `point`, GetRenderState(POINTSIZE) the magic (the state stored); `dst` read:
// band i's centre texel sampled over the 64x64 quad at (64 i, 96), its band's grey at (64 i + 32, 128).
static bool reszBands(IDirect3DSurface9 *target, IDirect3DSurface9 *src, IDirect3DTexture9 *dst, bool prefill,
                      bool point) {
    Rt read;
    if (!mkRt(read, 256) || !rstate(D3DRS_POINTSIZE, fbits(1.0f))) return false;
    const bool drawn = scene([&] {
        if (prefill && !(bindRt(0, read.s.p) && bindDsLevel(dst) && clearZ())) return false;
        if (!bindRt(0, target) || !bindDs(src) || !bands()) return false;
        CK(SetTexture, 0, dst);
        if (!resz("SetRenderState(POINTSIZE, RESZ)") ||
            (point && !pointSizeIs("GetRenderState(POINTSIZE) after RESZ", RESZ)) || !readInto(read.s.p))
            return false;
        for (int i = 0; i < 4; i++)
            if (!sampleAt(dst, 64.0f * i, 96, (64 * i + 32.5f) / 256)) return false;
        return true;
    });
    dev->SetTexture(0, nullptr);
    return drawn && rtHas("the destination", read.s.p, 256, A8,
                          {{32, 128, BAND[0]}, {96, 128, BAND[1]}, {160, 128, BAND[2]}, {224, 128, BAND[3]}});
}
// rz-msaa: a 4-sample 256x256 D24S8 surface (the count ms-dres runs in every lane) beside a 4-sample target, into an
// INTZ texture: RESZ D3D_OK, POINTSIZE the magic (only a stored state reads it), the four bands in the texture (only a
// resolve writes them over its Z 1 clear).
static bool rowRzMsaa() {
    if (!samplesOffered(D3DMULTISAMPLE_4_SAMPLES)) return false;
    RsDevice r;
    Com<IDirect3DSurface9> bb;
    if (!exDevice(r, bb)) return false;
    const OnDevice on(r.d.p, bb.p);
    return until([] {
        Com<IDirect3DSurface9> t, s;
        Com<IDirect3DTexture9> d;
        return msRt(t, 256, A8, D3DMULTISAMPLE_4_SAMPLES) && makeDs(s, D3DFMT_D24S8, 256, D3DMULTISAMPLE_4_SAMPLES) &&
               makeDepthTex(d, FMT_INTZ) && reszBands(t.p, s.p, d.p, true, true);
    });
}
// rz-auto: a device whose back buffer and auto D24S8 have 2 samples (resz_test's second part): as rz-msaa from the auto
// depth-stencil beside the back buffer (the destination's clear and the read beside 1-sample targets of their own).
static bool rowRzAuto() {
    if (!samplesOffered(D3DMULTISAMPLE_2_SAMPLES)) return false;
    RsDevice r;
    Com<IDirect3DSurface9> bb, ads;
    if (!exDevice(r, bb, D3DFMT_D24S8, false, D3DMULTISAMPLE_2_SAMPLES)) return false;
    const OnDevice on(r.d.p, bb.p);
    CK(GetDepthStencilSurface, &ads.p);
    return until([&] {
        Com<IDirect3DTexture9> d;
        return makeDepthTex(d, FMT_INTZ) && reszBands(rtSurf, ads.p, d.p, true, true);
    });
}
// rz-single: a 1-sample 256x256 D24S8 surface beside the back buffer, into the INTZ texture: RESZ D3D_OK (a copy),
// POINTSIZE the magic, the four bands (only a copy writes them over the Z 1 clear).
static bool rowRzSingle() {
    RsDevice r;
    Com<IDirect3DSurface9> bb;
    if (!exDevice(r, bb)) return false;
    const OnDevice on(r.d.p, bb.p);
    return until([] {
        Com<IDirect3DSurface9> s;
        Com<IDirect3DTexture9> d;
        return makeDs(s, D3DFMT_D24S8, 256) && makeDepthTex(d, FMT_INTZ) && reszBands(rtSurf, s.p, d.p, true, true);
    });
}
// rz-plain: the destination an INTZ texture made without D3DUSAGE_DEPTHSTENCIL (no attachment, so never cleared: its
// content before is undefined on mtld3d and zero on core, and no band is either), a fresh texture a source: from the
// 4-sample source first (rz-msaa's), then another from a 1-sample one (rz-single's), each the four bands (only a RESZ
// that takes a plain depth texture writes them, and in the first half only one that resolves). POINTSIZE is not read:
// rz-msaa and rz-single read it.
static bool rowRzPlain() {
    if (!samplesOffered(D3DMULTISAMPLE_4_SAMPLES)) return false;
    RsDevice r;
    Com<IDirect3DSurface9> bb;
    if (!exDevice(r, bb)) return false;
    const OnDevice on(r.d.p, bb.p);
    if (!until([] {
            Com<IDirect3DSurface9> t, s;
            Com<IDirect3DTexture9> d;
            return msRt(t, 256, A8, D3DMULTISAMPLE_4_SAMPLES) &&
                   makeDs(s, D3DFMT_D24S8, 256, D3DMULTISAMPLE_4_SAMPLES) && makeDepthTex(d, FMT_INTZ, 1, 0) &&
                   reszBands(t.p, s.p, d.p, false, false);
        }))
        return failIn("from the 4-sample source");
    return until([] {
               Com<IDirect3DSurface9> s;
               Com<IDirect3DTexture9> d;
               return makeDs(s, D3DFMT_D24S8, 256) && makeDepthTex(d, FMT_INTZ, 1, 0) &&
                      reszBands(rtSurf, s.p, d.p, false, false);
           }) ||
           failIn("from the 1-sample source");
}
// rz-edges: a 1-sample D24S8 source holding the bands beside the back buffer; INTZ destinations made with
// D3DUSAGE_DEPTHSTENCIL and cleared to Z 1 first (A, B, the control C, and L, 512x512, cleared beside the 256x256
// target: a depth-stencil larger than the target, ds-size), and a 128x128 A8R8G8B8 render-target texture cleared gold
// (another size than the source: at the source's size mtld3d leaves it zeroed, a finding for the maintainer's list the
// first control run showed on the Mac and in the VM; the core skips a texture of no depth format at any size). Each
// RESZ D3D_OK and the destination still at its clear: no texture at stage 0 (A elsewhere); L at stage 0 (another size);
// the colour texture at stage 0 (no depth format, another size); B at stage 0 with no depth-stencil bound (last: it
// unbinds the source). "Unchanged" says something only if the source held the bands in this attempt: so before the last
// case, the control, C at stage 0 (the source's size), reads the band (mtld3d leaves the band draws out while their
// pipeline builds, and every other cell would then pass on a skip that never happened). Read: band 1's texel (96) of A,
// B, L and C over the quads at (0,96), (64,96), (128,96) and (192,96) (white, white, white, band 1's grey), the colour
// texture's (48,64) gold.
static bool rowRzEdges() {
    RsDevice r;
    Com<IDirect3DSurface9> bb;
    if (!exDevice(r, bb)) return false;
    const OnDevice on(r.d.p, bb.p);
    return until([] {
        Rt read, colour;
        Com<IDirect3DSurface9> s;
        Com<IDirect3DTexture9> a, b, c, l;
        if (!mkRt(read, 256) || !mkRt(colour, 128) || !makeDs(s, D3DFMT_D24S8, 256) || !makeDepthTex(a, FMT_INTZ) ||
            !makeDepthTex(b, FMT_INTZ) || !makeDepthTex(c, FMT_INTZ) ||
            !makeDepthTex(l, FMT_INTZ, 1, D3DUSAGE_DEPTHSTENCIL, 512))
            return false;
        const bool drawn = scene([&] {
            if (!clearRt(colour.s.p, KGOLD) || !bindRt(0, read.s.p)) return false;
            for (IDirect3DTexture9 *t : {a.p, b.p, c.p, l.p})
                if (!bindDsLevel(t) || !clearZ()) return false;
            if (!bindRt(0, rtSurf) || !bindDs(s.p) || !bands()) return false;
            CK(SetTexture, 0, nullptr);
            if (!resz("RESZ (no texture at stage 0)")) return false;
            CK(SetTexture, 0, l.p);
            if (!resz("RESZ (a 512x512 destination)")) return false;
            CK(SetTexture, 0, colour.t.p);
            if (!resz("RESZ (a colour texture at stage 0)")) return false;
            CK(SetTexture, 0, c.p);
            if (!resz("RESZ (the control, a destination of the source's size)")) return false;
            CK(SetTexture, 0, b.p);
            if (!bindDs(nullptr) || !resz("RESZ (no depth-stencil bound)")) return false;
            return readInto(read.s.p) && sampleAt(a.p, 0, 96, 96.5f / 256) && sampleAt(b.p, 64, 96, 96.5f / 256) &&
                   sampleAt(l.p, 128, 96, 96.5f / 512, 128.5f / 512) && sampleAt(c.p, 192, 96, 96.5f / 256);
        });
        dev->SetTexture(0, nullptr);
        return drawn &&
               readTarget(read.s.p, 256, 256, A8,
                          [](const BYTE *t, int p) {
                              return txHas("no texture at stage 0: A", t, p, {{32, 128, 0xffffff}}) &&
                                     txHas("no depth-stencil bound: B", t, p, {{96, 128, 0xffffff}}) &&
                                     txHas("another size: L", t, p, {{160, 128, 0xffffff}}) &&
                                     txHas("the control: C", t, p, {{224, 128, BAND[1]}});
                          }) &&
               rtHas("a colour texture at stage 0", colour.s.p, 128, A8, {{48, 64, KGOLD}});
    });
}
// rz-self, under the launch's "strict" alone (elsewhere it does nothing and passes: mtld3d sends Metal a copy of a
// texture onto itself there, a finding not exercised by a gate). An INTZ texture made with D3DUSAGE_DEPTHSTENCIL, its
// level bound as the depth-stencil beside the back buffer and cleared to Z 0.5, then at stage 0 too, with no draw
// between the bind and the RESZ: RESZ D3D_OK, POINTSIZE the magic (the state stored though nothing was copied); its
// level unbound, the texture read: 0.5's grey at (128,128), as it was (a self-copy would read the same: the answer
// and the stored state are this row's cells, and the validation-layer run is the cover of the core's own-level test).
static bool rowRzSelf() {
    if (!strictMode) return true;
    RsDevice r;
    Com<IDirect3DSurface9> bb;
    if (!exDevice(r, bb)) return false;
    const OnDevice on(r.d.p, bb.p);
    return until([] {
        Rt read;
        Com<IDirect3DTexture9> t;
        if (!mkRt(read, 256) || !makeDepthTex(t, FMT_INTZ) || !rstate(D3DRS_POINTSIZE, fbits(1.0f))) return false;
        const bool drawn = scene([&] {
            if (!bindRt(0, rtSurf) || !bindDsLevel(t.p) || !clearZ(0.5f)) return false;
            CK(SetTexture, 0, t.p);
            return resz("RESZ (the bound depth-stencil's own texture at stage 0)") &&
                   pointSizeIs("GetRenderState(POINTSIZE) after RESZ", RESZ) && readInto(read.s.p) &&
                   sampleAt(t.p, 96, 96, 128.5f / 256);
        });
        dev->SetTexture(0, nullptr);
        return drawn && rtHas("the texture, unbound", read.s.p, 256, A8, {{128, 128, 0x808080}});
    });
}
// rz-refuse, under the launch's "strict" alone (elsewhere it does nothing and passes): a 1-sample D24S8 surface bound
// beside the back buffer and a 256x256 D3DUSAGE_DYNAMIC D24S8 texture at stage 0: POINTSIZE 1.0 (0x3f800000) before;
// RESZ D3DERR_NOTAVAILABLE (the core refuses it by name, "IDirect3DDevice9::SetRenderState(RESZ) (into a dynamic depth
// texture)", before the state's store); POINTSIZE still 1.0 after (a refusal placed after the store reads the magic);
// the texture unbound, a quad lands, until()'s Present D3D_OK.
static bool rowRzRefuse() {
    if (!strictMode) return true;
    RsDevice r;
    Com<IDirect3DSurface9> bb, ds;
    Com<IDirect3DTexture9> t;
    if (!exDevice(r, bb)) return false;
    const OnDevice on(r.d.p, bb.p);
    if (!makeDs(ds, D3DFMT_D24S8, 256)) return false;
    CK(CreateTexture, 256, 256, 1, D3DUSAGE_DYNAMIC, D3DFMT_D24S8, D3DPOOL_DEFAULT, &t.p, nullptr);
    CK(SetDepthStencilSurface, ds.p);
    CK(SetTexture, 0, t.p);
    const bool refused = pointSizeIs("GetRenderState(POINTSIZE) before RESZ", fbits(1.0f)) &&
                         resz("RESZ (into a dynamic D24S8 texture)", D3DERR_NOTAVAILABLE) &&
                         pointSizeIs("GetRenderState(POINTSIZE) after the refused RESZ", fbits(1.0f));
    CK(SetTexture, 0, nullptr);
    return refused && (until([] { return lands(160, 160, 224, 224, 32, 32); }) || failIn("after the refused RESZ"));
}

// The cursor rows' bitmap: a size x size offscreen plain surface of format f in `pool`, painted opaque gold (alpha
// 0xff) under a lock: shown, it is a visible pointer, never a transparent one.
static bool cursorBitmap(Com<IDirect3DSurface9> &s, UINT size = 32, D3DFORMAT f = A8, D3DPOOL pool = D3DPOOL_DEFAULT) {
    CK(CreateOffscreenPlainSurface, size, size, f, pool, &s.p, nullptr);
    return underLock(s.p, "LockRect (the cursor bitmap)", nullptr, 0,
                     [&](BYTE *b, int p) { paint(b, p, 0, 0, (int)size, (int)size, 0xff000000 | KGOLD); });
}
// cu-props: SetCursorProperties with no bitmap D3DERR_INVALIDCALL; a 32x32 A8R8G8B8 bitmap of the default pool and one
// of the scratch pool D3D_OK; a 24x24 one (no power of two) and a 32x32 X8R8G8B8 one D3DERR_INVALIDCALL; the default
// pool's with the hot spot (40,40), outside it, D3D_OK (mtld3d checks no hot spot). Only a body that ran tells
// INVALIDCALL from D3D_OK by the bitmap (a refusal answers D3DERR_NOTAVAILABLE to all). The cursor is never shown, so
// no call sets the thread's cursor.
static bool rowCuProps() {
    RsDevice r;
    Com<IDirect3DSurface9> bb, def, scratch, odd, x8;
    if (!exDevice(r, bb)) return false;
    const OnDevice on(r.d.p, bb.p);
    if (!cursorBitmap(def) || !cursorBitmap(scratch, 32, A8, D3DPOOL_SCRATCH) || !cursorBitmap(odd, 24) ||
        !cursorBitmap(x8, 32, X8))
        return false;
    return answer("SetCursorProperties (no bitmap)", dev->SetCursorProperties(0, 0, nullptr), D3DERR_INVALIDCALL) &&
           answer("SetCursorProperties (32x32 A8R8G8B8, the default pool)", dev->SetCursorProperties(0, 0, def.p),
                  D3D_OK) &&
           answer("SetCursorProperties (32x32 A8R8G8B8, the scratch pool)", dev->SetCursorProperties(0, 0, scratch.p),
                  D3D_OK) &&
           answer("SetCursorProperties (24x24)", dev->SetCursorProperties(0, 0, odd.p), D3DERR_INVALIDCALL) &&
           answer("SetCursorProperties (X8R8G8B8)", dev->SetCursorProperties(0, 0, x8.p), D3DERR_INVALIDCALL) &&
           answer("SetCursorProperties (the hot spot (40,40))", dev->SetCursorProperties(40, 40, def.p), D3D_OK);
}
// cu-show: ShowCursor answers the visibility before the call: TRUE 0, TRUE 0 (no cursor set: nothing latches);
// SetCursorProperties (the opaque bitmap) D3D_OK; TRUE 0, TRUE 1, FALSE 1, FALSE 0, TRUE 0. The answers after the
// properties tell a body that keeps the state from one that answers FALSE to all (the first two alone do not). The row
// ends on ShowCursor(TRUE) on every path: a hide is not undone at the device's release.
static bool rowCuShow() {
    RsDevice r;
    Com<IDirect3DSurface9> bb, bmp;
    if (!exDevice(r, bb)) return false;
    const OnDevice on(r.d.p, bb.p);
    if (!cursorBitmap(bmp)) return false;
    static const struct { BOOL show, prev; } calls[] = {{TRUE, FALSE}, {TRUE, FALSE}, {TRUE, FALSE}, {TRUE, TRUE},
                                                        {FALSE, TRUE}, {FALSE, FALSE}, {TRUE, FALSE}};
    const bool ok = [&] {
        for (int i = 0; i < 7; i++) {
            if (i == 2 && !answer("SetCursorProperties", dev->SetCursorProperties(0, 0, bmp.p), D3D_OK)) return false;
            const BOOL got = dev->ShowCursor(calls[i].show);
            if (got != calls[i].prev)
                return snprintf(why, sizeof why, "ShowCursor(%s), call %d: %d, want %d",
                                calls[i].show ? "TRUE" : "FALSE", i + 1, (int)got, (int)calls[i].prev),
                       false;
        }
        return true;
    }();
    dev->ShowCursor(TRUE);
    return ok;
}
// cu-setcursor: the row's window's class given the arrow as its cursor (the class's window procedure is
// DefWindowProcW, which sets the class cursor at a client-area WM_SETCURSOR); SetCursorProperties (the opaque bitmap)
// D3D_OK, ShowCursor(TRUE): GetCursor() a cursor that is not the arrow (the Direct3D one); the row sends the window
// WM_SETCURSOR (HTCLIENT) itself, then GetCursor(): the same Direct3D cursor, in every lane (the window subclass
// consumes the message while its cursor shows; the core's device has it since DXMTRS C4c-6d: without it
// DefWindowProcW sets the class cursor, the arrow). No D3D call between the four. The row ends on ShowCursor(TRUE) and
// the class cursor as it was, on every path (the class is every d3d9_triangle window's).
static bool rowCuSetCursor() {
    RsDevice r;
    Com<IDirect3DSurface9> bb, bmp;
    if (!exDevice(r, bb)) return false;
    const OnDevice on(r.d.p, bb.p);
    if (!cursorBitmap(bmp)) return false;
    const HCURSOR arrow = LoadCursor(nullptr, IDC_ARROW);
    const ULONG_PTR before = SetClassLongPtrW(r.w.h, GCLP_HCURSOR, (LONG_PTR)arrow);
    const bool ok = [&] {
        if (!answer("SetCursorProperties", dev->SetCursorProperties(0, 0, bmp.p), D3D_OK)) return false;
        dev->ShowCursor(TRUE);
        const HCURSOR shown = GetCursor();
        SendMessageW(r.w.h, WM_SETCURSOR, (WPARAM)r.w.h, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
        const HCURSOR after = GetCursor();
        if (!shown || shown == arrow)
            return snprintf(why, sizeof why, "GetCursor() after ShowCursor(TRUE) %p: none or the arrow (%p)",
                            (void *)shown, (void *)arrow),
                   false;
        return after == shown ||
               (snprintf(why, sizeof why,
                         "GetCursor() after WM_SETCURSOR %p, want %p (the Direct3D cursor; the arrow %p)",
                         (void *)after, (void *)shown, (void *)arrow),
                false);
    }();
    dev->ShowCursor(TRUE);
    SetClassLongPtrW(r.w.h, GCLP_HCURSOR, (LONG_PTR)before);
    return ok;
}

static void extrasRows(IDirect3DDevice9 *d, bool strict) {
    static const struct { const char *name; bool (*run)(); } rows[] = {
        {"gm-ramp", rowGmRamp},   {"fl-latency", rowFlLatency}, {"ex-rt", rowExRt},
        {"ex-ds", rowExDs},       {"ex-plain", rowExPlain},     {"rz-msaa", rowRzMsaa},
        {"rz-auto", rowRzAuto},   {"rz-single", rowRzSingle},   {"rz-plain", rowRzPlain},
        {"rz-edges", rowRzEdges}, {"rz-self", rowRzSelf},       {"rz-refuse", rowRzRefuse},
        {"cu-props", rowCuProps}, {"cu-show", rowCuShow},       {"cu-setcursor", rowCuSetCursor}};
    dev = d, strictMode = strict, budgetEnd = GetTickCount64() + 200000;
    for (const auto &r : rows) {
        why[0] = 0;
        bool ok;
        if (GetTickCount64() >= budgetEnd) ok = false, snprintf(why, sizeof why, "budget");
        else ok = r.run();
        row(r.name, ok, why);
    }
}

// --- The "fullscreen" mode (DXMTRS C4c-6d): fullscreen devices, Reset into and out of fullscreen and the window
// subclass. Each row makes a plain device and a window of its own (rsDevice's; an extended device keeps the window's
// rect when it leaves fullscreen, which the rect cells below do not want; Wine's d3d9ex runs CreateDeviceEx's and
// ResetEx's fullscreen forms) and, while it runs, the globals the helpers read name its device (OnDevice, rtSurf null,
// as the reset mode's). A fullscreen device asks for the desktop's current mode (GetAdapterDisplayMode: its size and
// refresh rate), which sets no mode, unless the row tests a mode change. Each cell wants what mtld3d, the default
// runtime, answers, in every lane, but ws-size's under "strict". A draw is judged by reading the back buffer back in
// one until() attempt, a present by presentCounts (presents()) or by Present's answer. A row that judges what a release
// gives back ends OnDevice's scope first and wants the release's count 0 (a reference left would read as a window,
// procedure or mode not given back).
// The gates run this program on a person's working Mac, so: every fs-* row first asks modesEmulated() and FAILs, before
// it makes a window or touches a device, when Wine does not emulate mode sets for it (a mode set would then switch the
// display); only fs-cover makes a fullscreen device without D3DCREATE_NOWINDOWCHANGES (on the Mac a window covering a
// screen rises above other applications' windows while this program is active, and the Dock and the menu bar hide,
// for under a second in a strict lane); the two rows that set a mode put the registry's mode back themselves on every
// path (ModeBack, after their release cell); no row changes the foreground window, minimises a window or moves the
// pointer, and a row that waits for a posted message pumps the queue until a condition holds (pumpUntil), never for a
// fixed time. The core before DXMTRS C4c-6d's front end refuses a fullscreen device and a Reset into fullscreen
// (D3DERR_NOTAVAILABLE) and installs no subclass: each fs-* row FAILs at its first fullscreen create or Reset,
// fs-wndproc at its windowed half. The mode's budget is 200 s, the launches' watchdog 300 s.

// Whether Wine emulates mode sets for this program: EmulateModeset under HKCU\Software\Wine\AppDefaults\<this
// program's file name>\X11 Driver, else under HKCU\Software\Wine\X11 Driver, its first character one of yYtT1 (win32u's
// reading; d3d12_fullscreen.cpp's guard, by the module's own name: d3d9_triangle.exe, the guest's d3d9_triangle32.exe).
// Otherwise false, `why` saying so.
static bool modesEmulated() {
    char path[MAX_PATH] = {}, app[MAX_PATH + 64];
    const DWORD n = GetModuleFileNameA(nullptr, path, sizeof path);
    const char *exe = path;
    for (DWORD i = 0; i < n; i++)
        if (path[i] == '\\' || path[i] == '/') exe = path + i + 1;
    snprintf(app, sizeof app, "Software\\Wine\\AppDefaults\\%s\\X11 Driver", exe);
    for (const char *key : {(const char *)app, "Software\\Wine\\X11 Driver"}) {
        char v[256] = {};
        DWORD size = sizeof v;
        if (RegGetValueA(HKEY_CURRENT_USER, key, "EmulateModeset", RRF_RT_REG_SZ, nullptr, v, &size) != ERROR_SUCCESS)
            continue;
        if (v[0] && strchr("yYtT1", v[0])) return true;
        break;
    }
    return snprintf(why, sizeof why, "EmulateModeset is off for %s: a mode set would switch this machine's display",
                    exe),
           false;
}
// A window's placement: its style, extended style (GetWindowLongW) and rect (GetWindowRect).
struct Placement {
    LONG style, exstyle;
    RECT rect;
};
static Placement placement(HWND h) {
    Placement p = {GetWindowLongW(h, GWL_STYLE), GetWindowLongW(h, GWL_EXSTYLE), {}};
    GetWindowRect(h, &p.rect);
    return p;
}
// Whether h's placement is `want`; `why` names both, after `what`.
static bool placementIs(const char *what, HWND h, const Placement &want) {
    const Placement p = placement(h);
    if (p.style == want.style && p.exstyle == want.exstyle && EqualRect(&p.rect, &want.rect)) return true;
    snprintf(why, sizeof why,
             "%s: style %#lx, extended style %#lx, rect (%ld,%ld)-(%ld,%ld); want %#lx, %#lx, (%ld,%ld)-(%ld,%ld)",
             what, (unsigned long)p.style, (unsigned long)p.exstyle, (long)p.rect.left, (long)p.rect.top,
             (long)p.rect.right, (long)p.rect.bottom, (unsigned long)want.style, (unsigned long)want.exstyle,
             (long)want.rect.left, (long)want.rect.top, (long)want.rect.right, (long)want.rect.bottom);
    return false;
}
// h's window procedure (GWLP_WNDPROC, read with the W call: the class is registered W).
static LONG_PTR procOf(HWND h) { return GetWindowLongPtrW(h, GWLP_WNDPROC); }
// Drain this thread's queue (PeekMessageW, DispatchMessageW), then cond(); again after waiting at most 10 ms for the
// next message (MsgWaitForMultipleObjects), until cond() holds or 2 s have passed: false then, cond()'s `why` kept. The
// subclass's re-cover and re-activation are messages it posts to the window, whose thread is this one.
template <class F> static bool pumpUntil(F cond) {
    for (const ULONGLONG end = GetTickCount64() + 2000;;) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
        if (cond()) return true;
        if (GetTickCount64() >= end) return false;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
}
// The scope guard of a row that sets a display mode, made before its device: the registry's mode (EnumDisplaySettingsW,
// ENUM_REGISTRY_SETTINGS) read then; at its end, after the row's release cell, ChangeDisplaySettingsW(NULL, 0) when the
// desktop's current mode has another size or cannot be read (mtld3d's own restore compares the sizes alone).
// currentIs(what, w, h): the desktop's current mode (ENUM_CURRENT_SETTINGS) is w x h, else `why` names both.
struct ModeBack {
    DEVMODEW reg = {};
    ModeBack() { reg.dmSize = sizeof reg, EnumDisplaySettingsW(nullptr, ENUM_REGISTRY_SETTINGS, &reg); }
    ~ModeBack() {
        DEVMODEW c = {};
        c.dmSize = sizeof c;
        if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &c) || c.dmPelsWidth != reg.dmPelsWidth ||
            c.dmPelsHeight != reg.dmPelsHeight)
            ChangeDisplaySettingsW(nullptr, 0);
    }
    bool currentIs(const char *what, DWORD w, DWORD h) const {
        DEVMODEW c = {};
        c.dmSize = sizeof c;
        if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &c))
            return snprintf(why, sizeof why, "%s: EnumDisplaySettingsW (current) failed", what), false;
        if (c.dmPelsWidth == w && c.dmPelsHeight == h) return true;
        return snprintf(why, sizeof why, "%s: the desktop's current mode %lux%lu, want %lux%lu", what,
                        (unsigned long)c.dmPelsWidth, (unsigned long)c.dmPelsHeight, (unsigned long)w,
                        (unsigned long)h),
               false;
    }
};

// fs-create (D3DCREATE_NOWINDOWCHANGES): a fullscreen device at the desktop's mode on a window whose client area is
// 256x256: D3D_OK; GetPresentParameters Windowed 0; the back buffer the mode's size; CreateAdditionalSwapChain of a
// windowed 256x256 chain D3DERR_INVALIDCALL; a quad lands; Present D3D_OK; the window's placement as before the create.
// The cells that tell a fullscreen device from a windowed one: Windowed 0 (a windowed device says 1), the additional
// chain refused (a windowed device makes it: rt-chain), and the back buffer at the mode's size where the client area is
// 256x256 (a fullscreen request not taken as a display mode follows the client area). The placement says the flag was
// kept: no cover.
static bool rowFsCreate() {
    if (!modesEmulated()) return false;
    D3DDISPLAYMODE m = {};
    if (!adapterMode(m)) return false;
    RsDevice r;
    if (!rsWindow(r.w, 256, 256)) return false;
    const Placement p0 = placement(r.w.h);
    if (!rsDevice(r, D3DFMT_UNKNOWN, false, D3DMULTISAMPLE_NONE, &m, D3DCREATE_NOWINDOWCHANGES)) return false;
    const OnDevice on(r.d.p, nullptr);
    D3DPRESENT_PARAMETERS cp = rsParams(r.w.h, 256, 256), got = {};
    Com<IDirect3DSwapChain9> sc, extra;
    CK(GetSwapChain, 0, &sc.p);
    return answer("IDirect3DSwapChain9::GetPresentParameters", sc.p->GetPresentParameters(&got), D3D_OK) &&
           exact("GetPresentParameters' Windowed", (unsigned long long)got.Windowed, 0) &&
           (bbIs(m.Width, m.Height) || failIn("the mode's size, the client area 256x256")) &&
           answer("CreateAdditionalSwapChain (windowed, on a fullscreen device)",
                  dev->CreateAdditionalSwapChain(&cp, &extra.p), D3DERR_INVALIDCALL) &&
           (until([] { return lands(160, 160, 224, 224, 32, 32); }) || failIn("a quad")) && presentIs(D3D_OK) &&
           placementIs("the window (D3DCREATE_NOWINDOWCHANGES)", r.w.h, p0);
}
// fs-cover, the one row whose window covers the monitor (no D3DCREATE_NOWINDOWCHANGES): a fullscreen device at the
// desktop's mode: D3D_OK; the window borderless at the primary monitor's rect (style (before | WS_POPUP | WS_SYSMENU) &
// ~(WS_CAPTION | WS_THICKFRAME), extended style before & ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE): mtld3d's); a quad
// lands and a present. The window made half the monitor's size with SetWindowPos (SWP_NOZORDER | SWP_NOACTIVATE),
// GetWindowRect another rect than the monitor's, then the queue pumped: covering again (the subclass's WM_SIZE arm
// posts the re-cover to the window; the cell before the pump tells a re-cover from a SetWindowPos that never shrank
// it).
// Reset to a windowed 256x256: the placement as before the create, a quad lands. Reset into fullscreen: covering again.
// The release: the placement as before. Then a second device on the window, fullscreen at 333x222 (a size no display
// mode has): D3D_OK, its back buffer the monitor's size (such a request follows the client area, which the device
// reads after its window covers the monitor: 256x256 if it read it before), and its release gives the placement back.
static bool rowFsCover() {
    if (!modesEmulated()) return false;
    D3DDISPLAYMODE m = {};
    MONITORINFO mi = {};
    mi.cbSize = sizeof mi;
    if (!adapterMode(m)) return false;
    if (!GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi))
        return snprintf(why, sizeof why, "GetMonitorInfoW (the primary monitor)"), false;
    const RECT &mon = mi.rcMonitor;
    const LONG mw = mon.right - mon.left, mh = mon.bottom - mon.top;
    RsDevice r;
    if (!rsWindow(r.w, 256, 256)) return false;
    const Placement p0 = placement(r.w.h);
    const Placement cover = {(LONG)((p0.style | WS_POPUP | WS_SYSMENU) & ~(WS_CAPTION | WS_THICKFRAME)),
                             (LONG)(p0.exstyle & ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE)), mon};
    if (!rsDevice(r, D3DFMT_UNKNOWN, false, D3DMULTISAMPLE_NONE, &m)) return false;
    {
        const OnDevice on(r.d.p, nullptr);
        if (!placementIs("the window after the create", r.w.h, cover) ||
            !until([] { return lands(160, 160, 224, 224, 32, 32); }) || !presents())
            return failIn("covering");
        SetWindowPos(r.w.h, nullptr, mon.left + mw / 4, mon.top + mh / 4, mw / 2, mh / 2,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        RECT now = {};
        GetWindowRect(r.w.h, &now);
        if (EqualRect(&now, &mon))
            return snprintf(why, sizeof why, "made smaller: SetWindowPos left the window at the monitor's rect"), false;
        if (!pumpUntil([&] { return placementIs("made smaller, the queue pumped", r.w.h, cover); })) return false;
        D3DPRESENT_PARAMETERS pp = rsParams(r.w.h, 256, 256);
        if (!resetIs("Reset (to a windowed 256x256)", pp)) return false;
        if (!placementIs("after the windowed Reset", r.w.h, p0) ||
            !until([] { return lands(160, 160, 224, 224, 32, 32); }))
            return failIn("windowed");
        pp = rsParams(r.w.h, m.Width, m.Height, D3DFMT_UNKNOWN, D3DMULTISAMPLE_NONE, &m);
        if (!resetIs("Reset (into fullscreen)", pp) || !placementIs("after the Reset into fullscreen", r.w.h, cover))
            return false;
    }
    ULONG left = r.d.p->Release();
    r.d.p = nullptr;
    if (!exact("the device's Release", left, 0) || !placementIs("after the release", r.w.h, p0)) return false;
    D3DDISPLAYMODE odd = m;
    odd.Width = 333, odd.Height = 222;
    if (!rsDevice(r, D3DFMT_UNKNOWN, false, D3DMULTISAMPLE_NONE, &odd)) return failIn("333x222");
    {
        const OnDevice on(r.d.p, nullptr);
        if (!bbIs((UINT)mw, (UINT)mh)) return failIn("333x222 (no mode): the monitor's size");
    }
    left = r.d.p->Release();
    r.d.p = nullptr;
    return (exact("the device's Release", left, 0) && placementIs("after the release", r.w.h, p0)) ||
           failIn("333x222");
}
// fs-reject (D3DCREATE_NOWINDOWCHANGES): fullscreen creates at the desktop's mode with a zero width, with
// MultiSampleType 3 (no power of two: no device makes it) and with D3DFMT_UNKNOWN (resolved for a windowed device
// alone; a fullscreen one names no format, which the sample count's check refuses): each D3DERR_INVALIDCALL and no
// device, the window's placement as before. INVALIDCALL is mtld3d's answer to each, where a runtime that takes no
// fullscreen device answers D3DERR_NOTAVAILABLE at the first.
static bool rowFsReject() {
    if (!modesEmulated()) return false;
    D3DDISPLAYMODE m = {};
    if (!adapterMode(m)) return false;
    Window w;
    if (!rsWindow(w, 256, 256)) return false;
    const Placement p0 = placement(w.h);
    static const char *const cells[] = {"a zero width", "MultiSampleType 3", "D3DFMT_UNKNOWN"};
    for (int i = 0; i < 3; i++) {
        D3DPRESENT_PARAMETERS pp = rsParams(w.h, m.Width, m.Height, D3DFMT_UNKNOWN, D3DMULTISAMPLE_NONE, &m);
        if (i == 0) pp.BackBufferWidth = 0;
        if (i == 1) pp.MultiSampleType = (D3DMULTISAMPLE_TYPE)3;
        if (i == 2) pp.BackBufferFormat = D3DFMT_UNKNOWN;
        Com<IDirect3DDevice9> d;
        const HRESULT hr = direct3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, w.h,
                                                  D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_NOWINDOWCHANGES,
                                                  &pp, &d.p);
        if (hr != D3DERR_INVALIDCALL || d.p)
            return snprintf(why, sizeof why, "CreateDevice (fullscreen, %s) %08lx and device %p, want %08lx and none",
                            cells[i], (unsigned long)hr, (void *)d.p, (unsigned long)D3DERR_INVALIDCALL),
                   false;
        if (!placementIs("the window", w.h, p0)) return failIn(cells[i]);
    }
    return true;
}
// fs-reset-in (D3DCREATE_NOWINDOWCHANGES): a windowed 256x256 device; Reset into fullscreen at the desktop's mode
// D3D_OK: GetPresentParameters Windowed 0, the back buffer the mode's size (the client area is still 256x256), a quad
// lands; Reset to a windowed 256x256 D3D_OK: the back buffer 256x256, a quad lands. The fullscreen cells are
// fs-create's, reached by a Reset.
static bool rowFsResetIn() {
    if (!modesEmulated()) return false;
    D3DDISPLAYMODE m = {};
    if (!adapterMode(m)) return false;
    RsDevice r;
    if (!rsDevice(r, D3DFMT_UNKNOWN, false, D3DMULTISAMPLE_NONE, nullptr, D3DCREATE_NOWINDOWCHANGES)) return false;
    const OnDevice on(r.d.p, nullptr);
    D3DPRESENT_PARAMETERS pp = rsParams(r.w.h, m.Width, m.Height, D3DFMT_UNKNOWN, D3DMULTISAMPLE_NONE, &m);
    if (!resetIs("Reset (into fullscreen)", pp)) return false;
    {
        Com<IDirect3DSwapChain9> sc;
        D3DPRESENT_PARAMETERS got = {};
        CK(GetSwapChain, 0, &sc.p);
        if (!answer("IDirect3DSwapChain9::GetPresentParameters", sc.p->GetPresentParameters(&got), D3D_OK) ||
            !exact("GetPresentParameters' Windowed", (unsigned long long)got.Windowed, 0) || !bbIs(m.Width, m.Height) ||
            !until([] { return lands(160, 160, 224, 224, 32, 32); }))
            return failIn("into fullscreen");
    }
    pp = rsParams(r.w.h, 256, 256);
    if (!resetIs("Reset (to a windowed 256x256)", pp)) return false;
    return (bbIs(256, 256) && until([] { return lands(160, 160, 224, 224, 32, 32); })) || failIn("back to windowed");
}
// fs-reset-window (D3DCREATE_NOWINDOWCHANGES): window B (client area 192x160) and window A, A's procedure read; a
// fullscreen device at the desktop's mode on A: A's procedure another than its own (the subclass's); Reset into
// fullscreen naming B D3D_OK: a quad lands, a present; B's procedure the one A had after the create, A's its own again
// (the subclass followed the device). The cell after the create keeps the last two from passing with no subclass
// anywhere (A and B share their class's procedure). Which window the present reached is unjudged, as in rs-window.
static bool rowFsResetWindow() {
    if (!modesEmulated()) return false;
    D3DDISPLAYMODE m = {};
    if (!adapterMode(m)) return false;
    Window b;
    if (!rsWindow(b, 192, 160, 700)) return false;
    RsDevice r;
    if (!rsWindow(r.w, 256, 256)) return false;
    const LONG_PTR a0 = procOf(r.w.h);
    if (!rsDevice(r, D3DFMT_UNKNOWN, false, D3DMULTISAMPLE_NONE, &m, D3DCREATE_NOWINDOWCHANGES)) return false;
    const LONG_PTR a1 = procOf(r.w.h);
    if (a1 == a0)
        return snprintf(why, sizeof why, "after the create A's window procedure %p, its own", (void *)a1), false;
    const OnDevice on(r.d.p, nullptr);
    D3DPRESENT_PARAMETERS pp = rsParams(b.h, m.Width, m.Height, D3DFMT_UNKNOWN, D3DMULTISAMPLE_NONE, &m);
    if (!resetIs("Reset (into fullscreen, onto window B)", pp)) return false;
    if (!until([] { return lands(160, 160, 224, 224, 32, 32); }) || !presents()) return failIn("on window B");
    const LONG_PTR bp = procOf(b.h), ap = procOf(r.w.h);
    if (bp == a1 && ap == a0) return true;
    return snprintf(why, sizeof why, "after the Reset B's window procedure %p (want %p, A's after the create), A's %p "
                    "(want its own %p)", (void *)bp, (void *)a1, (void *)ap, (void *)a0),
           false;
}
// fs-wndproc: a windowed 256x256 device, then a fullscreen one at the desktop's mode (D3DCREATE_NOWINDOWCHANGES), each
// on a window of its own: the window's procedure after the create another than the one read before it (the subclass),
// after the release that one again. Both halves read GWLP_WNDPROC, which nothing but the subclass changes.
static bool rowFsWndproc() {
    if (!modesEmulated()) return false;
    D3DDISPLAYMODE m = {};
    if (!adapterMode(m)) return false;
    for (const bool full : {false, true}) {
        const char *const what = full ? "a fullscreen device" : "a windowed device";
        RsDevice r;
        if (!rsWindow(r.w, 256, 256)) return false;
        const LONG_PTR p0 = procOf(r.w.h);
        if (!rsDevice(r, D3DFMT_UNKNOWN, false, D3DMULTISAMPLE_NONE, full ? &m : nullptr,
                      full ? D3DCREATE_NOWINDOWCHANGES : 0))
            return failIn(what);
        const LONG_PTR p1 = procOf(r.w.h);
        if (p1 == p0)
            return snprintf(why, sizeof why, "%s: the window procedure %p after the create, its own", what, (void *)p1),
                   false;
        const ULONG left = r.d.p->Release();
        r.d.p = nullptr;
        if (!exact("the device's Release", left, 0)) return failIn(what);
        const LONG_PTR p2 = procOf(r.w.h);
        if (p2 != p0)
            return snprintf(why, sizeof why, "%s: the window procedure %p after the release, want its own %p", what,
                            (void *)p2, (void *)p0),
                   false;
    }
    return true;
}
// fs-reset-mode (D3DCREATE_NOWINDOWCHANGES): a fullscreen device at the desktop's mode; Reset to a smaller mode the
// adapter lists D3D_OK: the desktop's current mode (EnumDisplaySettingsW, ENUM_CURRENT_SETTINGS) that mode's size;
// after the release the registry's (ENUM_REGISTRY_SETTINGS, read before the device). Both cells read user32, not the
// device: the first holds only if the Reset set the mode, the second only if the release put it back. A real
// activation change during the row (the person switching applications) also restores the registry's mode.
static bool rowFsResetMode() {
    if (!modesEmulated()) return false;
    D3DDISPLAYMODE m = {}, s = {};
    if (!adapterMode(m) || !adapterMode(s, true)) return false;
    const ModeBack back;
    RsDevice r;
    if (!rsDevice(r, D3DFMT_UNKNOWN, false, D3DMULTISAMPLE_NONE, &m, D3DCREATE_NOWINDOWCHANGES)) return false;
    {
        const OnDevice on(r.d.p, nullptr);
        D3DPRESENT_PARAMETERS pp = rsParams(r.w.h, s.Width, s.Height, D3DFMT_UNKNOWN, D3DMULTISAMPLE_NONE, &s);
        if (!resetIs("Reset (to a smaller mode the adapter lists)", pp) ||
            !back.currentIs("after the Reset", s.Width, s.Height))
            return false;
    }
    const ULONG left = r.d.p->Release();
    r.d.p = nullptr;
    return exact("the device's Release", left, 0) &&
           back.currentIs("after the release (the registry's)", back.reg.dmPelsWidth, back.reg.dmPelsHeight);
}
// fs-activate (D3DCREATE_NOWINDOWCHANGES): a fullscreen device made at a smaller mode the adapter lists: the desktop's
// current mode that one; the queue drained once (an activation change pending from the person's own application switch
// is spent before the cells: it would restore the mode inside them). The row sends the window WM_ACTIVATEAPP(FALSE):
// the current mode the registry's at once; WM_ACTIVATEAPP(TRUE) and the queue pumped: the smaller one again (the
// subclass posts the re-activation). A sent message takes the subclass's arm as a real one does. The first cell keeps
// the second from passing on a device that set no mode; the third holds only if the subclass set it again. A real
// activation change during the row also restores the registry's mode.
static bool rowFsActivate() {
    if (!modesEmulated()) return false;
    D3DDISPLAYMODE s = {};
    if (!adapterMode(s, true)) return false;
    const ModeBack back;
    RsDevice r;
    if (!rsDevice(r, D3DFMT_UNKNOWN, false, D3DMULTISAMPLE_NONE, &s, D3DCREATE_NOWINDOWCHANGES)) return false;
    if (!back.currentIs("after the create (a real activation change also restores the mode)", s.Width, s.Height))
        return false;
    pumpUntil([] { return true; });
    SendMessageW(r.w.h, WM_ACTIVATEAPP, FALSE, 0);
    if (!back.currentIs("after WM_ACTIVATEAPP(FALSE) (the registry's)", back.reg.dmPelsWidth, back.reg.dmPelsHeight))
        return false;
    SendMessageW(r.w.h, WM_ACTIVATEAPP, TRUE, 0);
    return pumpUntil([&] {
        return back.currentIs("after WM_ACTIVATEAPP(TRUE), the queue pumped (a real activation change during the row "
                              "also restores the mode)",
                              s.Width, s.Height);
    });
}
// ws-size, last (a windowed device's window resized: DXMTRS C4c-6d's R6d-3): a windowed 256x256 device; its window
// resized with SetWindowPos (SWP_NOZORDER | SWP_NOACTIVATE) to the outer rect of a 320x240 client area, which sends
// WM_SIZE before it returns (no pump); the client area then read (GetClientRect) another than 256x256 (else no cell
// below tells a resize from none). On mtld3d the back buffer's description, the viewport and the scissor rect are that
// client area (its subclass's WM_SIZE arm re-makes the back buffer: only a resize that reached it gets there); under
// "strict" they keep 256x256 (a core device's back buffer does not follow its window). In both a quad lands and Present
// answers D3D_OK. GetPresentParameters follows on neither runtime: not judged.
static bool rowWsSize() {
    RsDevice r;
    if (!rsDevice(r)) return false;
    const OnDevice on(r.d.p, nullptr);
    RECT o = {0, 0, 320, 240}, c = {};
    AdjustWindowRect(&o, WS_OVERLAPPEDWINDOW, FALSE);
    SetWindowPos(r.w.h, nullptr, 400, 100, o.right - o.left, o.bottom - o.top, SWP_NOZORDER | SWP_NOACTIVATE);
    if (!GetClientRect(r.w.h, &c) || c.right <= 0 || c.bottom <= 0 || (c.right == 256 && c.bottom == 256))
        return snprintf(why, sizeof why, "after SetWindowPos the client area %ldx%ld, want another than 256x256",
                        (long)c.right, (long)c.bottom),
               false;
    const UINT w = strictMode ? 256 : (UINT)c.right, h = strictMode ? 256 : (UINT)c.bottom;
    if (!bbIs(w, h) || !extentIs(w, h))
        return failIn(strictMode ? "kept (strict: a core device does not follow its window)" : "the new client area");
    return (until([] { return lands(160, 160, 224, 224, 32, 32); }) && presentIs(D3D_OK)) || failIn("after the resize");
}

static void fullscreenRows(IDirect3DDevice9 *d, bool strict) {
    static const struct { const char *name; bool (*run)(); } rows[] = {
        {"fs-create", rowFsCreate},     {"fs-cover", rowFsCover},           {"fs-reject", rowFsReject},
        {"fs-reset-in", rowFsResetIn},  {"fs-reset-window", rowFsResetWindow}, {"fs-wndproc", rowFsWndproc},
        {"fs-reset-mode", rowFsResetMode}, {"fs-activate", rowFsActivate},  {"ws-size", rowWsSize}};
    dev = d, strictMode = strict, budgetEnd = GetTickCount64() + 200000;
    for (const auto &r : rows) {
        why[0] = 0;
        bool ok;
        if (GetTickCount64() >= budgetEnd) ok = false, snprintf(why, sizeof why, "budget");
        else ok = r.run();
        row(r.name, ok, why);
    }
}

// --- The "parity" mode (DXMTRS C4c-7d-1): the last differences Wine's tests see between D3D9's core runtime and
// mtld3d. sr-lsb, vo-one, dr-keep and the pv-* rows run on the launch's device (256x256 X8R8G8B8, no depth-stencil),
// reset() and bufStart() before each, and read back through frames(); rz-cross and gm-full make a device and a window
// of their own (reset's) and, while they run, the globals the helpers read name it (OnDevice). Each cell wants what
// mtld3d, the default runtime, answers, in every lane ("strict", the core's, only asks the first frame to match), and
// each row's comment says why its cells hold only if the feature works. The mode's budget is 200 s, the launches'
// watchdog 300 s.

// sr-lsb: a 1x1 X8R8G8B8 texture of grey 128 (an sRGB code whose linear value is 55) sampled by the fixed-function
// stage five times in one frame, D3DSAMP_SRGBTEXTURE 0, 1, 2, 3 and 0x7e41882a set before each draw, over the 64x64
// quads at (0,32), (64,32), (128,32), (192,32) and (0,160): mtld3d decodes when the value's low bit is set, so 128,
// 55, 128, 55, 128. The cells of 1 and 3 hold only if the decode shows at all (else the raw cells would pass on a
// runtime that never decodes); those of 2 and 0x7e41882a only if the view is picked by the low bit (a test of "not
// 0" decodes both).
static bool rowSrLsb() {
    Com<IDirect3DTexture9> t;
    if (!make2d(t, 1, 1, 1, D3DFMT_X8R8G8B8, 4, [](int, int) { return gray(128); })) return false;
    static const DWORD values[5] = {0, 1, 2, 3, 0x7e41882a};
    Px px[5];
    for (int i = 0; i < 5; i++) px[i] = {64 * (i % 4) + 32, i < 4 ? 64 : 192, gray(values[i] & 1 ? 55 : 128)};
    return frames(BLUE, px, 5, [&] {
               for (int i = 0; i < 5; i++) {
                   CK(SetSamplerState, 0, D3DSAMP_SRGBTEXTURE, values[i]);
                   if (!sampleAt(t.p, 64.0f * (i % 4), i < 4 ? 32.0f : 160.0f, 0.5f)) return false;
               }
               return true;
           }) ||
           failIn("SRGBTEXTURE 0, 1, 2, 3 at y 64 left to right, 0x7e41882a at (32,192)");
}
// vo-one: a 1x1x1 X8R8G8B8 volume texture of grey 128, managed (filled by LockBox), and a default-pool one filled by
// UpdateTexture from a system-memory one, each sampled by the fixed-function stage at (0.5, 0.5, 0.5) (three
// coordinates), D3DSAMP_SRGBTEXTURE off then on, over the quads at x 0, 64, 128 and 192: 128, 55, 128, 55. A volume of
// depth 1 is a 2D texture on mtld3d, and the stage declares its sampler 2D: only a 2D texture both reads the texel (a
// 3D one bound where 2D is declared reads nothing defined, and the validation layer reports it) and takes the sRGB
// view (the decoded cells).
static bool rowVoOne() {
    Com<IDirect3DVolumeTexture9> managed, sys, def;
    const auto volume = [&](Com<IDirect3DVolumeTexture9> &v, D3DPOOL pool) {
        CK(CreateVolumeTexture, 1, 1, 1, 1, 0, D3DFMT_X8R8G8B8, pool, &v.p, nullptr);
        if (pool == D3DPOOL_DEFAULT) return true;
        D3DLOCKED_BOX lb = {};
        HRESULT hr = v.p->LockBox(0, &lb, nullptr, 0);
        if (FAILED(hr)) return fail("LockBox", hr);
        *(DWORD *)lb.pBits = gray(128);
        hr = v.p->UnlockBox(0);
        return SUCCEEDED(hr) || fail("UnlockBox", hr);
    };
    if (!volume(managed, D3DPOOL_MANAGED) || !volume(sys, D3DPOOL_SYSTEMMEM) || !volume(def, D3DPOOL_DEFAULT))
        return false;
    CK(UpdateTexture, sys.p, def.p);
    const Px px[] = {{32, 128, gray(128)}, {96, 128, gray(55)}, {160, 128, gray(128)}, {224, 128, gray(55)}};
    return frames(BLUE, px, 4, [&] {
               for (int i = 0; i < 4; i++) {
                   CK(SetSamplerState, 0, D3DSAMP_SRGBTEXTURE, (DWORD)(i & 1));
                   if (!sampleAt(i < 2 ? managed.p : def.p, 64.0f * i, 96, 0.5f)) return false;
               }
               return true;
           }) ||
           failIn("left to right: managed raw, managed sRGB, the UpdateTexture twin raw, the twin sRGB");
}
// dr-keep: a managed 4x2 X8R8G8B8 texture of two levels (level 1 2x1), made once and never filled at its create. A
// level is shown by probe2d at u 1/8 and 7/8, v 1/4 (texels 0 and 3 of level 0's first row, texels 0 and 1 of level
// 1), MIPFILTER POINT and MAXMIPLEVEL 0 or 1, a frame a level; a write is every texel of a level, level 0 and level 1
// in colours of their own (so a level-1 probe that sampled level 0 reads the wrong one). The cells:
// (i) the texture bound and drawn before any lock (nothing judged), then a READONLY lock of level 0 writing steel:
// level 0 steel (a level's very first lock uploads, READONLY or not; a mark set for every level a texture's first
// upload carries keeps it out). Then rust (level 0) and gold (level 1) written with no flag: shown (both uploaded).
// (ii) READONLY locks writing aqua and lilac: rust and gold (an uploaded level's READONLY unlock marks nothing; the
// level-1 cell holds only if level 1 is marked uploaded too).
// (iii) NO_DIRTY_UPDATE locks writing mint and rose: rust and gold (a managed uploaded level's such unlock likewise).
// (iv) AddDirtyRect of (2,0)-(4,2), D3D_OK: level 0 rust at texel 0 and mint at texel 3, level 1 gold at texel 0 and
// rose at texel 1 (each level's clipped rect, halved with its edges rounded out, (1,0)-(2,1) on level 1, uploads from
// the CPU copy, and nothing else: texel 0 holds only if the upload is that rect, texel 3 only if it happens).
// (v) EvictManagedResources, D3D_OK: mint and rose at every texel (each uploaded level marked dirty again, so the CPU
// copy, the last written, uploads whole).
static bool rowDrKeep() {
    Com<IDirect3DTexture9> t;
    CK(CreateTexture, 4, 2, 2, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &t.p, nullptr);
    const auto write = [&](UINT l, DWORD flags, DWORD c) {
        return underLevelLock(t.p, l, flags, [&](BYTE *b, int p) { paint(b, p, 0, 0, 4 >> l, 2 >> l, c); });
    };
    const auto both = [&](DWORD flags, DWORD c0, DWORD c1) { return write(0, flags, c0) && write(1, flags, c1); };
    const auto shows = [&](UINT l, DWORD left, DWORD right) {
        CK(SetSamplerState, 0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
        CK(SetSamplerState, 0, D3DSAMP_MAXMIPLEVEL, l);
        const Probe pr[] = {{0.125f, 0.25f, left}, {0.875f, 0.25f, right}};
        const bool ok = probe2d(t.p, pr, 2);
        dev->SetSamplerState(0, D3DSAMP_MAXMIPLEVEL, 0);
        return ok || failIn(l ? "level 1" : "level 0");
    };
    const auto levels = [&](DWORD l0a, DWORD l0b, DWORD l1a, DWORD l1b) {
        return shows(0, l0a, l0b) && shows(1, l1a, l1b);
    };
    // (i)'s first draw: the texture sampled over one quad, no pixel judged.
    const auto drawn = [&] {
        Com<IDirect3DPixelShader9> ps;
        if (!usePs(ps, K2D, 0, false) || !pointClamp(0)) return false;
        CK(SetTexture, 0, t.p);
        TV3 q[4];
        tquad(q, 16, 96, 56, 136, 0.5f, 0.5f);
        return frames(BLUE, nullptr, 0, [&] { return tdraw(q); });
    };
    if (!drawn() || !write(0, D3DLOCK_READONLY, KSTEEL) || !shows(0, KSTEEL, KSTEEL))
        return failIn("(i) a READONLY first lock of a level drawn before any lock");
    if (!both(0, KRUST, KGOLD) || !levels(KRUST, KRUST, KGOLD, KGOLD)) return failIn("written with no flag");
    if (!both(D3DLOCK_READONLY, KAQUA, KLILAC) || !levels(KRUST, KRUST, KGOLD, KGOLD))
        return failIn("(ii) READONLY after the upload");
    if (!both(D3DLOCK_NO_DIRTY_UPDATE, KMINT, KROSE) || !levels(KRUST, KRUST, KGOLD, KGOLD))
        return failIn("(iii) NO_DIRTY_UPDATE after the upload");
    const RECT part = {2, 0, 4, 2};
    if (!answer("AddDirtyRect", t.p->AddDirtyRect(&part), D3D_OK) || !levels(KRUST, KMINT, KGOLD, KROSE))
        return failIn("(iv) AddDirtyRect (2,0)-(4,2)");
    return (answer("EvictManagedResources", dev->EvictManagedResources(), D3D_OK) &&
            levels(KMINT, KMINT, KROSE, KROSE)) ||
           failIn("(v) EvictManagedResources");
}

// The ProcessVertices rows (pv-*): the source is stream 0 read through SetFVF(XYZ | DIFFUSE) (FC, stride 16); the
// destination a vertex buffer made with FVF XYZRHW | DIFFUSE (one made with FVF 0 is D3DERR_INVALIDCALL on both
// runtimes). Under reset()'s identity transforms, LIGHTING off and its full 256x256 viewport, a vertex (x, y, z)
// becomes ((x + 1) * 128, (1 - y) * 128, z, rhw 1), its colour copied. Every call is made outside frames(), so a
// retried frame draws what one call wrote. pv-sysmem's and pv-dynsrc's vertices (dyadic, so each result is exact):
static const FC PV_SRC[4] = {{-0.5f, 0.5f, 0.25f, KRUST}, {0.75f, 0.25f, 0.5f, KSTEEL}, {-0.25f, -0.75f, 0.75f, KAQUA},
                             {0.5f, -0.5f, 1.0f, KMINT}};
// `src` at stream 0 as FC vertices, read through FVF_C.
static bool fcStream(IDirect3DVertexBuffer9 *src) {
    CK(SetFVF, FVF_C);
    CK(SetStreamSource, 0, src, 0, sizeof(FC));
    return true;
}
// Four vertices from `first` drawn as a strip.
static bool stripAt(UINT first) {
    CK(DrawPrimitive, D3DPT_TRIANGLESTRIP, first, 2);
    return true;
}
// ProcessVertices(0, 0, 4) of PV_SRC from `src` (bound as fcStream) into a new system-memory destination, read under
// a READONLY lock: D3D_OK and each vertex transformed (within 0.01 a coordinate, the colour exact), else `why` names
// the first that is not.
static bool pvRead(IDirect3DVertexBuffer9 *src) {
    Com<IDirect3DVertexBuffer9> dst;
    CK(CreateVertexBuffer, 4 * sizeof(RV), 0, FVF_RV, D3DPOOL_SYSTEMMEM, &dst.p, nullptr);
    if (!fcStream(src) || !answer("ProcessVertices", dev->ProcessVertices(0, 0, 4, dst.p, nullptr, 0), D3D_OK))
        return false;
    void *p = nullptr;
    HRESULT hr = dst.p->Lock(0, 0, &p, D3DLOCK_READONLY);
    if (FAILED(hr)) return fail("Lock (READONLY) of the destination", hr);
    RV got[4];
    memcpy(got, p, sizeof got);
    if (FAILED(hr = dst.p->Unlock())) return fail("Unlock (the destination)", hr);
    for (int i = 0; i < 4; i++) {
        const FC &s = PV_SRC[i];
        const RV w = {(s.x + 1) * 128, (1 - s.y) * 128, s.z, 1, s.c}, &g = got[i];
        if (fabsf(g.x - w.x) > 0.01f || fabsf(g.y - w.y) > 0.01f || fabsf(g.z - w.z) > 0.01f ||
            fabsf(g.rhw - w.rhw) > 0.01f || g.color != w.color)
            return snprintf(why, sizeof why, "vertex %d (%g, %g, %g, rhw %g) %08lx, want (%g, %g, %g, rhw 1) %08lx", i,
                            (double)g.x, (double)g.y, (double)g.z, (double)g.rhw, (unsigned long)g.color, (double)w.x,
                            (double)w.y, (double)w.z, (unsigned long)w.color),
                   false;
    }
    return true;
}
// pv-sysmem: pvRead from a system-memory source: only a call that ran the transform writes the four vertices.
static bool rowPvSysmem() {
    Com<IDirect3DVertexBuffer9> src;
    return mkVb(src, PV_SRC, sizeof PV_SRC, D3DPOOL_SYSTEMMEM) && pvRead(src.p);
}
// pv-dynsrc: pvRead from a default-pool D3DUSAGE_DYNAMIC source written through Lock(DISCARD), a buffer mtld3d and the
// core map directly (its bytes are where the GPU reads them; the core keeps no CPU copy of it): D3D_OK with the four
// vertices only from a call that reads the mapped bytes.
static bool rowPvDynsrc() {
    Com<IDirect3DVertexBuffer9> src;
    return mkDyn(src, sizeof PV_SRC, 0) && put(src.p, 0, PV_SRC, sizeof PV_SRC, D3DLOCK_DISCARD) && pvRead(src.p);
}
// pv-draw's and pv-dyndst's system-memory source, bound (fcStream): quad A at vertices 0-3, red, over pixels
// (16,64)-(112,192), and quad B at 4-7, green, over (144,64)-(240,192), each a clockwise strip once transformed.
static bool pvQuads(Com<IDirect3DVertexBuffer9> &src) {
    FC v[8];
    cquad(v, -0.875f, -0.5f, -0.125f, 0.5f, TRED), cquad(v + 4, 0.125f, -0.5f, 0.875f, 0.5f, TGRN);
    return mkVb(src, v, sizeof v, D3DPOOL_SYSTEMMEM) && fcStream(src.p);
}
// pv-draw: a default-pool D3DUSAGE_WRITEONLY destination of eight vertices, never locked (games' usual: the core
// releases its CPU copy once it is uploaded). ProcessVertices of A (source 0-3) into vertices 0-3; a frame drawing
// them as a strip: red at (64,128), blue at (192,128). Then the source's FVF and stream set again (rvAt replaced both)
// and ProcessVertices of B (source 4-7) into vertices 4-7; a frame drawing both strips: red at (64,128), green at
// (192,128), blue at (128,128). B's cell holds only if the second call's window reaches the GPU after the first draw
// made the buffer whole there; A's only if that window reaches it alone (one widened to the whole buffer would send
// the re-created CPU copy's zeros over A).
static bool rowPvDraw() {
    Com<IDirect3DVertexBuffer9> src, dst;
    if (!pvQuads(src)) return false;
    CK(CreateVertexBuffer, 8 * sizeof(RV), D3DUSAGE_WRITEONLY, FVF_RV, D3DPOOL_DEFAULT, &dst.p, nullptr);
    const Px a[] = {{64, 128, TRED}, {192, 128, BLUE}}, ab[] = {{64, 128, TRED}, {192, 128, TGRN}, {128, 128, BLUE}};
    if (!answer("ProcessVertices (A into 0-3)", dev->ProcessVertices(0, 0, 4, dst.p, nullptr, 0), D3D_OK) ||
        !(frames(BLUE, a, 2, [&] { return rvAt(dst.p) && stripAt(0); }) || failIn("A drawn")))
        return false;
    return fcStream(src.p) &&
           answer("ProcessVertices (B into 4-7)", dev->ProcessVertices(4, 4, 4, dst.p, nullptr, 0), D3D_OK) &&
           (frames(BLUE, ab, 3, [&] { return rvAt(dst.p) && stripAt(0) && stripAt(4); }) ||
            failIn("A and B drawn after the second ProcessVertices (A at (64,128), B at (192,128))"));
}
// pv-dyndst: ProcessVertices of A into a default-pool D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY destination (mapped
// directly, never locked): D3D_OK, and a frame drawing it: red at (64,128), blue at (192,128). Only vertices written
// where the GPU reads them draw.
static bool rowPvDyndst() {
    Com<IDirect3DVertexBuffer9> src, dst;
    if (!pvQuads(src)) return false;
    CK(CreateVertexBuffer, 4 * sizeof(RV), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, FVF_RV, D3DPOOL_DEFAULT, &dst.p,
       nullptr);
    const Px px[] = {{64, 128, TRED}, {192, 128, BLUE}};
    return answer("ProcessVertices (A into a dynamic buffer)", dev->ProcessVertices(0, 0, 4, dst.p, nullptr, 0),
                  D3D_OK) &&
           (frames(BLUE, px, 2, [&] { return rvAt(dst.p) && stripAt(0); }) || failIn("A drawn"));
}
// rz-cross (DXMTRS C4c-7a's ruling R7a-4: a one-sample RESZ between two depth formats), one until() attempt: a 1-sample
// 256x256 D24X8 surface holds bands() beside the back buffer; a DF24 and an INTZ texture made with
// D3DUSAGE_DEPTHSTENCIL, each cleared to Z 1 first; in one scene RESZ D3D_OK with the DF24 texture at stage 0, then
// with the INTZ one; both read by the raw sample, band i's centre texel over the quads at (64 i, 32) (DF24) and (64 i,
// 160) (INTZ), red alone judged (DF24 holds the depth in red). (a) DF24 (Depth32Float, the source's Metal format on
// both runtimes): the bands' depths (only a copy writes them over the clear's 0xff). (b) INTZ (Depth32Float_Stencil8):
// 0xff, its clear, **judged under the launch's "strict" alone** (the core skips a pair of two Metal formats and leaves
// the destination; mtld3d queues a blit between them, which Metal's side refuses, and the destination reads zero
// afterwards: the first control run, on the Mac and in the VM, a finding for the maintainer's list, not exercised by
// a gate). (a) is (b)'s control: (b)'s 0xff says something only when the source held the bands.
static bool rowRzCross() {
    RsDevice r;
    Com<IDirect3DSurface9> bb;
    if (!exDevice(r, bb)) return false;
    const OnDevice on(r.d.p, bb.p);
    return until([] {
        Rt read;
        Com<IDirect3DSurface9> s;
        Com<IDirect3DTexture9> df, intz;
        if (!mkRt(read, 256) || !makeDs(s, D3DFMT_D24X8) || !makeDepthTex(df, FMT_DF24) ||
            !makeDepthTex(intz, FMT_INTZ) || !rstate(D3DRS_POINTSIZE, fbits(1.0f)))
            return false;
        const bool drawn = scene([&] {
            if (!bindRt(0, read.s.p)) return false;
            for (IDirect3DTexture9 *t : {df.p, intz.p})
                if (!bindDsLevel(t) || !clearZ()) return false;
            if (!bindRt(0, rtSurf) || !bindDs(s.p) || !bands()) return false;
            CK(SetTexture, 0, df.p);
            if (!resz("RESZ ((a) D24X8 into DF24)")) return false;
            CK(SetTexture, 0, intz.p);
            if (!resz("RESZ ((b) D24X8 into INTZ)") || !readInto(read.s.p)) return false;
            for (int i = 0; i < 4; i++)
                if (!sampleAt(df.p, 64.0f * i, 32, (64 * i + 32.5f) / 256) ||
                    !sampleAt(intz.p, 64.0f * i, 160, (64 * i + 32.5f) / 256))
                    return false;
            return true;
        });
        dev->SetTexture(0, nullptr);
        return drawn && readTarget(read.s.p, 256, 256, A8, [](const BYTE *t, int p) {
                   return txHas("(a) D24X8 into DF24, the bands", t, p,
                                {{32, 64, BAND[0], 0xff0000}, {96, 64, BAND[1], 0xff0000},
                                 {160, 64, BAND[2], 0xff0000}, {224, 64, BAND[3], 0xff0000}}) &&
                          (!strictMode ||
                           txHas("(b) D24X8 into INTZ cleared to Z 1, unchanged", t, p,
                                 {{32, 192, 0xff0000, 0xff0000}, {96, 192, 0xff0000, 0xff0000},
                                  {160, 192, 0xff0000, 0xff0000}, {224, 192, 0xff0000, 0xff0000}}));
               });
    });
}
// gm-full (the fullscreen mode's rules: modesEmulated() first, a FAIL before any device without it; a fullscreen
// device with D3DCREATE_NOWINDOWCHANGES at the desktop's mode): SetGammaRamp(0) of gm-ramp's rising ramp, Get reads it
// back; a quad lands, read back unchanged (rust: the back buffer is read before the ramp, on both runtimes), and a
// present (presents()). Reset to a windowed 256x256 with the ramp still set: a quad lands. Reset into fullscreen again:
// a quad lands and a present. The identity set: Get reads it, a quad lands, Present D3D_OK. No cell sees the ramp on
// screen: under core the launch's log holds the line the core logs when a ramp first applies (the gates count it),
// and the validation layer watches the presenter's path.
static bool rowGmFull() {
    if (!modesEmulated()) return false;
    D3DDISPLAYMODE m = {};
    if (!adapterMode(m)) return false;
    RsDevice r;
    if (!rsDevice(r, D3DFMT_UNKNOWN, false, D3DMULTISAMPLE_NONE, &m, D3DCREATE_NOWINDOWCHANGES)) return false;
    const OnDevice on(r.d.p, nullptr);
    const D3DGAMMARAMP identity = gammaRamp([](int, int i) { return (WORD)(i * 257); }),
                       rising = gammaRamp([](int c, int i) {
                           return (WORD)(c == 0 ? i * 200 : c == 1 ? i * 100 + 7 : i * 256);
                       });
    const auto frame = [] { return until([] { return lands(160, 160, 224, 224, 32, 32); }); };
    D3DPRESENT_PARAMETERS win = rsParams(r.w.h, 256, 256),
                          full = rsParams(r.w.h, m.Width, m.Height, D3DFMT_UNKNOWN, D3DMULTISAMPLE_NONE, &m);
    dev->SetGammaRamp(0, D3DSGR_NO_CALIBRATION, &rising);
    if (!gammaIs("GetGammaRamp (a rising ramp set)", 0, rising) || !frame() || !presents())
        return failIn("fullscreen, the ramp set");
    if (!resetIs("Reset (to a windowed 256x256, the ramp still set)", win) || !frame()) return failIn("windowed");
    if (!resetIs("Reset (into fullscreen again)", full) || !frame() || !presents()) return failIn("fullscreen again");
    dev->SetGammaRamp(0, D3DSGR_NO_CALIBRATION, &identity);
    return (gammaIs("GetGammaRamp (the identity set)", 0, identity) && frame() && presentIs(D3D_OK)) ||
           failIn("the identity");
}

static void parityRows(IDirect3DDevice9 *d, bool strict) {
    static const struct { const char *name; bool (*run)(); } rows[] = {
        {"sr-lsb", rowSrLsb},       {"vo-one", rowVoOne},     {"dr-keep", rowDrKeep},
        {"pv-sysmem", rowPvSysmem}, {"pv-draw", rowPvDraw},   {"pv-dynsrc", rowPvDynsrc},
        {"pv-dyndst", rowPvDyndst}, {"rz-cross", rowRzCross}, {"gm-full", rowGmFull}};
    dev = d, strictMode = strict, budgetEnd = GetTickCount64() + 200000;
    HRESULT hr = dev->GetRenderTarget(0, &rtSurf);
    if (SUCCEEDED(hr))
        hr = dev->CreateOffscreenPlainSurface(256, 256, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sysSurf, nullptr);
    for (const auto &r : rows) {
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
    const bool blitMode = !bench && argc > 1 && !strcmp(argv[1], "blit");
    const bool queriesMode = !bench && argc > 1 && !strcmp(argv[1], "queries");
    const bool resetMode = !bench && argc > 1 && !strcmp(argv[1], "reset");
    const bool extrasMode = !bench && argc > 1 && !strcmp(argv[1], "extras");
    const bool fullscreenMode = !bench && argc > 1 && !strcmp(argv[1], "fullscreen");
    const bool parityMode = !bench && argc > 1 && !strcmp(argv[1], "parity");
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
    if (queriesMode) {
        queriesRows(dev, argc > 2 && !strcmp(argv[2], "strict"));
        dev->Release();
        d3d->Release();
        DestroyWindow(hwnd);
        return failures;
    }
    if (resetMode) {
        resetRows(dev, argc > 2 && !strcmp(argv[2], "strict"));
        dev->Release();
        d3d->Release();
        DestroyWindow(hwnd);
        return failures;
    }
    if (extrasMode) {
        extrasRows(dev, argc > 2 && !strcmp(argv[2], "strict"));
        dev->Release();
        d3d->Release();
        DestroyWindow(hwnd);
        return failures;
    }
    if (fullscreenMode) {
        fullscreenRows(dev, argc > 2 && !strcmp(argv[2], "strict"));
        dev->Release();
        d3d->Release();
        DestroyWindow(hwnd);
        return failures;
    }
    if (parityMode) {
        parityRows(dev, argc > 2 && !strcmp(argv[2], "strict"));
        dev->Release();
        d3d->Release();
        DestroyWindow(hwnd);
        return failures;
    }
    if (blitMode) {
        blitRows(dev, argc > 2 && !strcmp(argv[2], "strict"));
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
