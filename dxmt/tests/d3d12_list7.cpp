// ID3D12GraphicsCommandList3 to 7 (DXMT command list 7 spec §7):
//   d3d12_list7.exe
// Prints one line a row, "<row> ok" or "<row> FAIL <what>"; a failed row doesn't end the program.
//   list4  CreateCommandList answers ID3D12GraphicsCommandList4, 5, 6 and 7; the List7 one answers List3 to 6, and the
//          methods behind caps DXMT reports off (protected sessions, VRS, mesh shaders, ray tracing) can be called
#include "d3d12_common.hpp"

static char fail_text[256];

#define TRY(expr, what) do { HRESULT hr_ = (expr); if (FAILED(hr_)) { \
    snprintf(fail_text, sizeof fail_text, "FAIL %s %08x", what, (unsigned)hr_); return fail_text; } } while (0)

static const char *list4(Gpu &gpu) {
    ID3D12CommandAllocator *allocator;
    TRY(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                           (void **)&allocator), "CreateCommandAllocator");
    static const struct { REFIID iid; const char *name; } lists[] = {
        {__uuidof(ID3D12GraphicsCommandList4), "CreateCommandList(ID3D12GraphicsCommandList4)"},
        {__uuidof(ID3D12GraphicsCommandList5), "CreateCommandList(ID3D12GraphicsCommandList5)"},
        {__uuidof(ID3D12GraphicsCommandList6), "CreateCommandList(ID3D12GraphicsCommandList6)"},
        {__uuidof(ID3D12GraphicsCommandList7), "CreateCommandList(ID3D12GraphicsCommandList7)"},
    };
    ID3D12GraphicsCommandList7 *list7 = nullptr;
    for (auto &l : lists) {
        ID3D12GraphicsCommandList *list;
        TRY(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, l.iid, (void **)&list),
            l.name);
        if (&l != &lists[3]) {
            TRY(list->Close(), "Close");
            list->Release();
        } else {
            list7 = (ID3D12GraphicsCommandList7 *)list;
        }
    }
    static const struct { REFIID iid; const char *name; } older[] = {
        {__uuidof(ID3D12GraphicsCommandList3), "QueryInterface(ID3D12GraphicsCommandList3)"},
        {__uuidof(ID3D12GraphicsCommandList4), "QueryInterface(ID3D12GraphicsCommandList4)"},
        {__uuidof(ID3D12GraphicsCommandList5), "QueryInterface(ID3D12GraphicsCommandList5)"},
        {__uuidof(ID3D12GraphicsCommandList6), "QueryInterface(ID3D12GraphicsCommandList6)"},
    };
    for (auto &o : older) {
        IUnknown *u;
        TRY(list7->QueryInterface(o.iid, (void **)&u), o.name);
        u->Release();
    }
    list7->SetProtectedResourceSession(nullptr);
    list7->RSSetShadingRate(D3D12_SHADING_RATE_1X1, nullptr);
    list7->RSSetShadingRateImage(nullptr);
    list7->DispatchMesh(1, 1, 1);
    list7->SetPipelineState1(nullptr);
    TRY(list7->Close(), "Close(ID3D12GraphicsCommandList7)");
    list7->Release();
    allocator->Release();
    return "ok";
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    Gpu gpu;
    printf("list4 %s\n", list4(gpu));
    return 0;
}
