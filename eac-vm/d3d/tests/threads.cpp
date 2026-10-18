// Direct3D forwarding's threads (M2 Task 9), a native program run natively and through the loopback (threads.sh):
//   record    two threads each record 256 small copies into their half of a buffer and close their list; a third
//             executes both on the queue and waits: the buffer reads back both halves (each thread's ring runs before
//             the queue operation that uses its list)
//   order     one thread executes a list that fills a buffer and goes idle (alive, its ring unflushed); another
//             then executes a list that copies that buffer and waits: the copy has the fill (a queue operation runs
//             every thread's published records first, MN_REC_SUBMIT)
//   release   one thread records copies from a buffer and, before closing, lets another thread release that buffer's
//             last reference (and make a call that reaches the host): the copies still run on a live buffer, and
//             the list is closed and never executed (its buffer is gone)
//   view      (the final review of M2) one thread makes a render target's view and goes idle (alive, its ring
//             unflushed); another clears through that view and closes its list, which is then executed: the target
//             reads back cleared (a view runs when it's made, so a later recording on any thread finds it)
//   exits     (after M2) 512 threads, one after another, each make one call and end: the memory they took is given
//             back (the unix side's per-thread buffers)
// Prints "threads ..." lines.
#include "d3d12_common.hpp"
#include <malloc/malloc.h>
#include <condition_variable>
#include <mutex>
#include <thread>

static Gpu *gpu;

static ID3D12Resource *Upload(const std::vector<uint8_t> &bytes) {
    ID3D12Resource *b = gpu->Buffer(D3D12_HEAP_TYPE_UPLOAD, bytes.size(), D3D12_RESOURCE_STATE_GENERIC_READ);
    void *p;
    CHECK(b->Map(0, nullptr, &p));
    memcpy(p, bytes.data(), bytes.size());
    b->Unmap(0, nullptr);
    return b;
}

struct Recorder {
    ID3D12CommandAllocator *allocator;
    ID3D12GraphicsCommandList *list;
    Recorder() {
        CHECK(gpu->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                                  (void **)&allocator));
        CHECK(gpu->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
                                             __uuidof(ID3D12GraphicsCommandList), (void **)&list));
    }
};

static void Record() {
    const UINT64 half = 4096;
    ID3D12Resource *out = gpu->Buffer(D3D12_HEAP_TYPE_DEFAULT, 2 * half, D3D12_RESOURCE_STATE_COPY_DEST);
    std::vector<uint8_t> a(half), b(half);
    for (UINT64 i = 0; i < half; i++) a[i] = (uint8_t)(i * 7 + 1), b[i] = (uint8_t)(i * 13 + 5);
    ID3D12Resource *ua = Upload(a), *ub = Upload(b);
    Recorder r1, r2;
    auto record = [&](Recorder &r, ID3D12Resource *src, UINT64 at) {
        for (UINT64 i = 0; i < 256; i++) r.list->CopyBufferRegion(out, at + i * 16, src, i * 16, 16);
        CHECK(r.list->Close());
    };
    std::thread t1(record, std::ref(r1), ua, 0), t2(record, std::ref(r2), ub, half);
    t1.join();
    t2.join();
    std::thread t3([&] {
        ID3D12CommandList *lists[] = {r1.list, r2.list};
        gpu->queue->ExecuteCommandLists(2, lists);
        CHECK(gpu->queue->Signal(gpu->fence, ++gpu->value));
        HANDLE done = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        CHECK(gpu->fence->SetEventOnCompletion(gpu->value, done));
        WaitForSingleObject(done, 10000);
        CloseHandle(done);
    });
    t3.join();
    ID3D12Resource *rb = gpu->Buffer(D3D12_HEAP_TYPE_READBACK, 2 * half, D3D12_RESOURCE_STATE_COPY_DEST);
    gpu->Barrier(out, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu->list->CopyBufferRegion(rb, 0, out, 0, 2 * half);
    gpu->Submit();
    uint8_t *v;
    CHECK(rb->Map(0, nullptr, (void **)&v));
    int bad = 0;
    for (UINT64 i = 0; i < half; i++) bad += v[i] != a[i] || v[half + i] != b[i];
    printf("threads record %d of %llu bytes wrong\n", bad, (unsigned long long)(2 * half));
}

static void Order() {
    const UINT64 size = 4096;
    ID3D12Resource *mid = gpu->Buffer(D3D12_HEAP_TYPE_DEFAULT, size, D3D12_RESOURCE_STATE_COMMON);
    ID3D12Resource *last = gpu->Buffer(D3D12_HEAP_TYPE_DEFAULT, size, D3D12_RESOURCE_STATE_COPY_DEST);
    std::vector<uint8_t> data(size);
    for (UINT64 i = 0; i < size; i++) data[i] = (uint8_t)(i * 3 + 11);
    ID3D12Resource *up = Upload(data);
    Recorder fill, copy;
    fill.list->CopyBufferRegion(mid, 0, up, 0, size);
    CHECK(fill.list->Close());
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, b.Transition.pResource = mid;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST, b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    copy.list->ResourceBarrier(1, &b);
    copy.list->CopyBufferRegion(last, 0, mid, 0, size);
    CHECK(copy.list->Close());
    std::mutex m;
    std::condition_variable cv;
    int stage = 0;
    std::thread first([&] {
        ID3D12CommandList *lists[] = {fill.list};
        gpu->queue->ExecuteCommandLists(1, lists);
        std::unique_lock<std::mutex> l(m);
        stage = 1;
        cv.notify_all();
        cv.wait(l, [&] { return stage == 2; }); // idle until the other is done: its exit would flush its ring
    });
    std::thread second([&] {
        {
            std::unique_lock<std::mutex> l(m);
            cv.wait(l, [&] { return stage == 1; });
        }
        ID3D12CommandList *lists[] = {copy.list};
        gpu->queue->ExecuteCommandLists(1, lists);
        CHECK(gpu->queue->Signal(gpu->fence, ++gpu->value));
        HANDLE done = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        CHECK(gpu->fence->SetEventOnCompletion(gpu->value, done));
        WaitForSingleObject(done, 10000);
        CloseHandle(done);
        std::unique_lock<std::mutex> l(m);
        stage = 2;
        cv.notify_all();
    });
    second.join();
    first.join();
    ID3D12Resource *rb = gpu->Buffer(D3D12_HEAP_TYPE_READBACK, size, D3D12_RESOURCE_STATE_COPY_DEST);
    gpu->Barrier(last, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu->list->CopyBufferRegion(rb, 0, last, 0, size);
    gpu->Submit();
    uint8_t *v;
    CHECK(rb->Map(0, nullptr, (void **)&v));
    int bad = 0;
    for (UINT64 i = 0; i < size; i++) bad += v[i] != data[i];
    printf("threads order %d of %llu bytes wrong\n", bad, (unsigned long long)size);
}

static void Release() {
    ID3D12Resource *out = gpu->Buffer(D3D12_HEAP_TYPE_DEFAULT, 4096, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource *src = Upload(std::vector<uint8_t>(4096, 0x5a));
    Recorder r;
    std::mutex m;
    std::condition_variable cv;
    bool recorded = false, released = false;
    std::thread recorder([&] {
        for (int i = 0; i < 200; i++) r.list->CopyBufferRegion(out, (i % 256) * 16, src, (i % 256) * 16, 16);
        {
            std::unique_lock<std::mutex> l(m);
            recorded = true;
            cv.notify_all();
            cv.wait(l, [&] { return released; });
        }
        CHECK(r.list->Close());
        CHECK(r.allocator->Reset());
    });
    std::thread releaser([&] {
        std::unique_lock<std::mutex> l(m);
        cv.wait(l, [&] { return recorded; });
        ULONG left = src->Release();
        HRESULT removed = gpu->device->GetDeviceRemovedReason(); // a call that reaches the host
        printf("threads release refs %lu removed %08lx\n", (unsigned long)left, (unsigned long)(unsigned)removed);
        released = true;
        cv.notify_all();
    });
    releaser.join();
    recorder.join();
    printf("threads release closed\n");
}

static void View() {
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D, d.Width = 8, d.Height = 8, d.DepthOrArraySize = 1, d.MipLevels = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM, d.SampleDesc.Count = 1, d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ID3D12Resource *target = gpu->Texture(d, D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_DESCRIPTOR_HEAP_DESC hd = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1};
    ID3D12DescriptorHeap *heap;
    CHECK(gpu->device->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void **)&heap));
    D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart();
    Recorder clear;
    std::mutex m;
    std::condition_variable cv;
    int stage = 0;
    std::thread maker([&] {
        gpu->device->CreateRenderTargetView(target, nullptr, h);
        std::unique_lock<std::mutex> l(m);
        stage = 1;
        cv.notify_all();
        cv.wait(l, [&] { return stage == 2; }); // idle until the other is done: its exit would flush its ring
    });
    std::thread user([&] {
        {
            std::unique_lock<std::mutex> l(m);
            cv.wait(l, [&] { return stage == 1; });
        }
        const float red[4] = {1, 0, 0, 1};
        clear.list->ClearRenderTargetView(h, red, 0, nullptr);
        CHECK(clear.list->Close());
        ID3D12CommandList *lists[] = {clear.list};
        gpu->queue->ExecuteCommandLists(1, lists);
        CHECK(gpu->queue->Signal(gpu->fence, ++gpu->value));
        HANDLE done = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        CHECK(gpu->fence->SetEventOnCompletion(gpu->value, done));
        WaitForSingleObject(done, 10000);
        CloseHandle(done);
        std::unique_lock<std::mutex> l(m);
        stage = 2;
        cv.notify_all();
    });
    user.join();
    maker.join();
    const UINT pitch = 256; // D3D12_TEXTURE_DATA_PITCH_ALIGNMENT
    ID3D12Resource *rb = gpu->Buffer(D3D12_HEAP_TYPE_READBACK, pitch * 8, D3D12_RESOURCE_STATE_COPY_DEST);
    gpu->Barrier(target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION to = {}, from = {};
    to.pResource = rb, to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 8, 8, 1, pitch};
    from.pResource = target, from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    gpu->list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    gpu->Submit();
    uint8_t *v;
    CHECK(rb->Map(0, nullptr, (void **)&v));
    int bad = 0;
    for (UINT y = 0; y < 8; y++)
        for (UINT x = 0; x < 8; x++) {
            const uint8_t *px = v + y * pitch + x * 4;
            bad += px[0] != 255 || px[1] != 0 || px[2] != 0 || px[3] != 255;
        }
    printf("threads view %d of 64 pixels wrong\n", bad);
}

static void Exits() {
    auto used = [] { return mstats().bytes_used; };
    std::thread([] { gpu->device->GetNodeCount(); }).join(); // what the first thread takes for good
    size_t before = used();
    for (int i = 0; i < 512; i++) std::thread([] { gpu->device->GetNodeCount(); }).join();
    size_t kept = used() > before ? used() - before : 0;
    if (kept < (16u << 20)) printf("threads exits ok\n");
    else printf("threads exits kept %zu MiB\n", kept >> 20);
}

int main() {
    Gpu g;
    gpu = &g;
    Record();
    Order();
    Release();
    View();
    Exits();
    return 0;
}
