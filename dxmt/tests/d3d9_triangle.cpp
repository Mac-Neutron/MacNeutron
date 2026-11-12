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
// the number of failed module and imports rows as the exit status. "--bench <draws> <frames> prog" (DXMTRS C4b2-12)
// draws the triangle through a vs_2_0/ps_2_0 pair over an XYZ declaration instead, setting four VS float rows (the
// transform) before each draw; the line names the mode: "d3d9 bench: mode ff|prog frames ...".
// With "shaders" (DXMTRS C4b-2's gate rows, C4b2-11): 22 rows of programmable SM1-3 draws (shaders as DWORD token
// arrays, their assembly beside them), each printing one line "d3d9 <row> ok|FAIL <why>" (stdout unbuffered). Each row
// starts from reset()'s state; a failing Create, Set or Draw prints "FAIL <call> <hr>" at once; a drawing row asserts,
// in one frame (Clear, draws, GetRenderTargetData, Present), a drawn pixel in a colour other than the clear and an
// undrawn one that keeps it, retrying frames for up to 10 s (mtld3d leaves a draw out while its pipeline builds) unless
// "shaders strict" (the core compiles synchronously: the first frame must match). The mode has a 90 s budget; rows past
// it print "FAIL budget". Expected colours are D3DCOLOR_XRGB values, each channel within 1.
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
    const bool vsync = !bench && argc > 1 && !strcmp(argv[1], "vsync");
    const bool shaders = !bench && argc > 1 && !strcmp(argv[1], "shaders");
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
        if (!prog) dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        QueryPerformanceCounter(&t0);
        for (int i = 0; i < benchFrames; i++) {
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 255), 1.0f, 0);
            dev->BeginScene();
            for (int d = 0; d < benchDraws; d++) {
                if (prog) {
                    const float m[4][4] = {{1, 0, 0, (float)(d & 7) * 0.125f / 128}, {0, 1, 0, 0}, {0, 0, 1, 0},
                                           {0, 0, 0, 1}};
                    dev->SetVertexShaderConstantF(0, &m[0][0], 4);
                    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, ctri, sizeof *ctri);
                    continue;
                }
                Vertex v[3] = {tri[0], tri[1], tri[2]};
                for (Vertex &p : v) p.x += (float)(d & 7) * 0.125f;
                dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, v, sizeof *v);
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
        printf("d3d9 bench: mode %s frames %d draws %d wall_ms %.1f cpu_ms %.1f vm_used_mb %llu\n",
               prog ? "prog" : "ff", benchFrames, benchDraws,
               (double)(t1.QuadPart - t0.QuadPart) * 1e3 / (double)f.QuadPart, ms(user) + ms(kernel),
               (unsigned long long)((mem.ullTotalVirtual - mem.ullAvailVirtual) >> 20));
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
