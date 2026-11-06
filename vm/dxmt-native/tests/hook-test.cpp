// Native DXMT's memory hooks (WMTSetMemoryHooks, fork 0076; Direct3D forwarding M2): with hooks set, a CPU-visible
// buffer's memory comes from them, so the VM host can put a game's mappable resources in memory the guest shares; the
// hook's free runs once Metal lets the buffer go. Without hooks, Metal's own memory. Prints "hook ..." lines;
// hook-test [nohooks]. Built and run by hook.sh.
#include "d3d12_common.hpp"
#include <sys/mman.h>
#include <atomic>

extern "C" void WMTSetMemoryHooks(void *(*alloc)(uint64_t length, void *ctx), void (*free)(void *ptr, void *ctx),
                                  void *ctx);

static const uint64_t kArena = 64ull << 20, kPage = 16384;
static uint8_t *arena;
static std::atomic<uint64_t> used{0}, allocs{0}, frees{0};

static void *hook_alloc(uint64_t length, void *) {
    uint64_t len = (length + kPage - 1) / kPage * kPage, at = used.fetch_add(len);
    if (at + len > kArena) return nullptr;
    allocs++;
    return arena + at;
}
static void hook_free(void *, void *) { frees++; }

int main(int argc, char **argv) {
    bool hooked = !(argc > 1 && !strcmp(argv[1], "nohooks"));
    arena = (uint8_t *)mmap(nullptr, kArena, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (hooked) WMTSetMemoryHooks(hook_alloc, hook_free, nullptr);
    ID3D12Device *device = nullptr;
    CHECK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void **)&device));
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width = 1 << 20; desc.Height = 1; desc.DepthOrArraySize = 1;
    desc.MipLevels = 1; desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource *buffer = nullptr;
    CHECK(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                          nullptr, __uuidof(ID3D12Resource), (void **)&buffer));
    void *p = nullptr;
    D3D12_RANGE none = {0, 0};
    CHECK(buffer->Map(0, &none, &p));
    bool inside = (uint8_t *)p >= arena && (uint8_t *)p < arena + kArena;
    memset(p, 0x5a, 1 << 20);
    buffer->Unmap(0, nullptr);
    buffer->Release();
    device->Release();
    printf("hook %s mapped %s the arena, %llu allocations, %llu freed\n", hooked ? "set" : "unset",
           inside ? "inside" : "outside", (unsigned long long)allocs.load(), (unsigned long long)frees.load());
    return hooked ? !(inside && allocs > 0 && frees > 0) : inside;
}
