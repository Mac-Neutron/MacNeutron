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
// transform) before each draw; the line names the mode: "d3d9 bench: mode ff|prog frames ...".
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
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

// Every state a row touches, back to D3D9's defaults (the viewport the whole back buffer).
static bool reset() {
    CK(SetVertexShader, nullptr);
    CK(SetPixelShader, nullptr);
    CK(SetViewport, &FULL);
    static const DWORD rs[][2] = {
        {D3DRS_ALPHATESTENABLE, FALSE}, {D3DRS_ALPHAFUNC, D3DCMP_ALWAYS}, {D3DRS_ALPHAREF, 0},
        {D3DRS_FOGENABLE, FALSE}, {D3DRS_FOGVERTEXMODE, D3DFOG_NONE}, {D3DRS_FOGTABLEMODE, D3DFOG_NONE},
        {D3DRS_FOGCOLOR, 0}, {D3DRS_COLORWRITEENABLE, 0xf}, {D3DRS_FILLMODE, D3DFILL_SOLID},
        {D3DRS_CULLMODE, D3DCULL_CCW}, {D3DRS_CLIPPLANEENABLE, 0}, {D3DRS_SRGBWRITEENABLE, FALSE}};
    for (const auto &s : rs) CK(SetRenderState, (D3DRENDERSTATETYPE)s[0], s[1]);
    // Textures, the cascade and the samplers at D3D9's defaults (DXMTRS C4b-3b's rows set them).
    CK(SetRenderState, D3DRS_TEXTUREFACTOR, 0xffffffff);
    for (DWORD stage = 0; stage < 8; stage++) {
        CK(SetTexture, stage, nullptr);
        CK(SetTextureStageState, stage, D3DTSS_COLOROP, stage ? D3DTOP_DISABLE : D3DTOP_MODULATE);
        CK(SetTextureStageState, stage, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        CK(SetTextureStageState, stage, D3DTSS_COLORARG2, D3DTA_CURRENT);
        CK(SetTextureStageState, stage, D3DTSS_ALPHAOP, stage ? D3DTOP_DISABLE : D3DTOP_SELECTARG1);
        CK(SetTextureStageState, stage, D3DTSS_CONSTANT, 0);
        for (DWORD m = D3DTSS_BUMPENVMAT00; m <= D3DTSS_BUMPENVMAT11; m++) CK(SetTextureStageState, stage, (D3DTEXTURESTAGESTATETYPE)m, 0);
        static const DWORD ss[][2] = {{D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP}, {D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP},
                                      {D3DSAMP_ADDRESSW, D3DTADDRESS_WRAP}, {D3DSAMP_MAGFILTER, D3DTEXF_POINT},
                                      {D3DSAMP_MINFILTER, D3DTEXF_POINT}, {D3DSAMP_MIPFILTER, D3DTEXF_NONE},
                                      {D3DSAMP_MIPMAPLODBIAS, 0}, {D3DSAMP_MAXMIPLEVEL, 0}, {D3DSAMP_SRGBTEXTURE, 0}};
        for (const auto &s : ss) CK(SetSamplerState, stage, (D3DSAMPLERSTATETYPE)s[0], s[1]);
    }
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
// constant: D3D_OK and drawn (red), or D3DERR_NOTAVAILABLE (the core refuses it until C4c) and undrawn.
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
    Px px[] = {{128, 128, 0xff0000, 0xff00ff}, {16, 16, BLUE}};
    return frames(BLUE, px, 2, [&] {
        if (!use(p)) return false;
        HRESULT hr = dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, 16);
        if (hr != D3D_OK && hr != D3DERR_NOTAVAILABLE) return fail("DrawPrimitiveUP", hr);
        px[0].want = hr == D3D_OK ? 0xff0000 : BLUE, px[0].mask = hr == D3D_OK ? 0xff00ff : 0xffffff;
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
// reset() and leaving what it set back at D3D9's defaults. Rows whose answer differs by runtime (a lock, a draw the
// core refuses) take either answer; d3d9-check proves which runtime gave it from the launch's refusal lines.
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
// lock: a vertex buffer's Lock answers D3D_OK (then unlocked) or D3DERR_NOTAVAILABLE (core: locks are C4c's).
static bool rowLock() {
    Com<IDirect3DVertexBuffer9> vb;
    CK(CreateVertexBuffer, 64, D3DUSAGE_DYNAMIC, 0, D3DPOOL_DEFAULT, &vb.p, nullptr);
    void *data = nullptr;
    HRESULT hr = vb.p->Lock(0, 0, &data, D3DLOCK_DISCARD);
    if (hr == D3D_OK) return vb.p->Unlock(), true;
    return hr == D3DERR_NOTAVAILABLE || fail("Lock", hr);
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
// drawn green (D3D_OK) or refused (D3DERR_NOTAVAILABLE, the core: textures are C4b-3b's) and blue. fftexoff: the same
// with stage 0 disabled, drawn white in every lane (the cascade reads no texture, so nothing refuses it).
struct TV { float x, y, z, rhw; DWORD color; float u, v; };
static bool texturedQuad(bool stageOn) {
    Com<IDirect3DTexture9> t;
    CK(CreateTexture, 4, 4, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &t.p, nullptr);
    D3DLOCKED_RECT lr = {};
    if (SUCCEEDED(t.p->LockRect(0, &lr, nullptr, 0))) {  // the core refuses texture locks until C4b-3b
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
// r0.wwww`). Rows past the core's slice (a depth, fetch4, render-target or vertex texture) answer D3D_OK and draw
// under mtld3d, and D3DERR_NOTAVAILABLE without drawing under core ("textures strict").
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
// LockRect on a render-target texture, past the core's slice (C4b-3d's): D3DERR_NOTAVAILABLE under core (strict); in
// the other lanes any answer but an abort (a lock that succeeds is unlocked).
static bool rowTexRtLock() {
    Com<IDirect3DTexture9> t;
    CK(CreateTexture, 4, 4, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &t.p, nullptr);
    D3DLOCKED_RECT lr = {};
    const HRESULT hr = t.p->LockRect(0, &lr, nullptr, D3DLOCK_READONLY);
    if (SUCCEEDED(hr)) t.p->UnlockRect(0);
    if (strictMode && hr != D3DERR_NOTAVAILABLE) return fail("LockRect", hr);
    return true;
}
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
static bool rowTexDepthTex() {
    Com<IDirect3DTexture9> t;
    CK(CreateTexture, 4, 4, 1, D3DUSAGE_DEPTHSTENCIL, D3DFMT_D24S8, D3DPOOL_DEFAULT, &t.p, nullptr);
    return refusedRow([&] { return ffTexDraw(t.p); });
}
static bool rowTexFetch4() {
    Com<IDirect3DTexture9> t;
    if (!make2d(t, 2, 2, 1, D3DFMT_L8, 1, [](int, int) { return 0x80u; })) return false;
    CK(SetSamplerState, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    CK(SetSamplerState, 0, D3DSAMP_MIPMAPLODBIAS, MAKEFOURCC('G', 'E', 'T', '4'));
    const bool ok = refusedRow([&] { return ffTexDraw(t.p); });
    CK(SetSamplerState, 0, D3DSAMP_MIPMAPLODBIAS, MAKEFOURCC('G', 'E', 'T', '1'));
    CK(SetSamplerState, 0, D3DSAMP_MIPMAPLODBIAS, 0);
    return ok;
}
static bool rowTexRtTex() {
    Com<IDirect3DTexture9> t;
    CK(CreateTexture, 4, 4, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &t.p, nullptr);
    return refusedRow([&] { return ffTexDraw(t.p); });
}
// vs_3_0 { dcl_position v0; dcl_position o0; dcl_2d s0; mov o0, v0 } with ps_3_0 { mov oC0, c0 }: a vertex sampler.
static bool rowTexVtx() {
    static const DWORD vs[] = {0xfffe0300, 0x0200001f, 0x80000000, 0x900f0000, 0x0200001f, 0x80000000, 0xe00f0000,
                               0x0200001f, 0x90000000, 0xa00f0800, 0x02000001, 0xe00f0000, 0x90e40000, 0x0000ffff};
    static const DWORD ps[] = {0xffff0300, 0x02000001, 0x800f0800, 0xa0e40000, 0x0000ffff};
    Prog p;
    if (!make(p, DECL_XYZ, vs, ps) || !use(p)) return false;
    CK(SetPixelShaderConstantF, 0, RED, 1);
    V3 q[4];
    quad(q, -0.5f, -0.5f, 0.5f, 0.5f);
    return refusedRow([&] { return dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof *q); });
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
        {"tex-rttex", rowTexRtTex},     {"tex-vtx", rowTexVtx},         {"tex-rtlock", rowTexRtLock},
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
    IDirect3DDevice9 *dev = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp,
                                   &dev);
    if (FAILED(hr)) return printf("d3d9 clear FAIL CreateDevice %08lx\n", (unsigned long)hr), failures + 3;

    struct Vertex { float x, y, z, rhw; DWORD color; };
    const Vertex tri[3] = {{128, 16, 0.5f, 1, D3DCOLOR_XRGB(255, 0, 0)},
                           {240, 240, 0.5f, 1, D3DCOLOR_XRGB(255, 0, 0)},
                           {16, 240, 0.5f, 1, D3DCOLOR_XRGB(255, 0, 0)}};
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
        QueryPerformanceCounter(&t0);
        for (int i = 0; i < benchFrames; i++) {
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 255), 1.0f, 0);
            dev->BeginScene();
            for (int d = 0; d < benchDraws; d++) {
                if (prog) {
                    const float m[4][4] = {{1, 0, 0, (float)(d & 7) * 0.125f / 128}, {0, 1, 0, 0}, {0, 0, 1, 0},
                                           {0, 0, 0, 1}};
                    dev->SetVertexShaderConstantF(0, &m[0][0], 4);
                    failedDraws += FAILED(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, ctri, sizeof *ctri));
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
               prog ? "prog" : texBench ? "tex" : "ff", benchFrames, benchDraws,
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
