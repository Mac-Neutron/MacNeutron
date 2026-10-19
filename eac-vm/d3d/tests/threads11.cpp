// Direct3D forwarding's D3D11 and D3D10 threads (M4's deferred findings), a native program run natively and through
// the loopback (both.sh threads11):
//   map10  one thread updates a D3D10 buffer and copies it to a staging one, then goes idle (alive, its ring
//          unflushed); another maps the staging buffer for reading: it reads the update (a D3D10 map runs every
//          thread's published records first, as a context's map after another thread's calls does)
//   enter  multithread protection on, one thread enters ID3D11Multithread, updates a buffer and lets another thread
//          go, which updates the same buffer; the first then copies it to staging and leaves: staging has the first
//          thread's bytes (natively the second blocks in its call until Leave)
// Prints "threads11 ..." lines.
#include "d3d11_common.hpp"
#include <d3d10_1.h>
#include <dxgi.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#ifndef _WIN32
extern "C" HRESULT WINAPI D3D10CoreCreateDevice(IDXGIFactory *, IDXGIAdapter *, UINT, D3D_FEATURE_LEVEL, ID3D10Device **);
#endif

static const UINT kSize = 256;

static void Map10() {
    IDXGIFactory *factory;
    IDXGIAdapter *adapter;
    CHECK(CreateDXGIFactory(__uuidof(IDXGIFactory), (void **)&factory));
    CHECK(factory->EnumAdapters(0, &adapter));
    ID3D10Device *d;
    CHECK(D3D10CoreCreateDevice(factory, adapter, 0, D3D_FEATURE_LEVEL_10_0, &d));
    D3D10_BUFFER_DESC bd = {kSize, D3D10_USAGE_DEFAULT, D3D10_BIND_VERTEX_BUFFER, 0, 0};
    D3D10_BUFFER_DESC sd = {kSize, D3D10_USAGE_STAGING, 0, D3D10_CPU_ACCESS_READ, 0};
    ID3D10Buffer *b, *staging;
    CHECK(d->CreateBuffer(&bd, nullptr, &b));
    CHECK(d->CreateBuffer(&sd, nullptr, &staging));
    std::vector<uint8_t> data(kSize);
    for (UINT i = 0; i < kSize; i++) data[i] = (uint8_t)(i * 3 + 1);
    std::mutex m;
    std::condition_variable cv;
    int stage = 0;
    std::thread writer([&] {
        d->UpdateSubresource(b, 0, nullptr, data.data(), 0, 0);
        d->CopyResource(staging, b);
        std::unique_lock<std::mutex> l(m);
        stage = 1;
        cv.notify_all();
        cv.wait(l, [&] { return stage == 2; }); // idle until the map: its exit would flush its ring
    });
    int bad = -1;
    {
        std::unique_lock<std::mutex> l(m);
        cv.wait(l, [&] { return stage == 1; });
    }
    void *p;
    CHECK(staging->Map(D3D10_MAP_READ, 0, &p));
    bad = 0;
    for (UINT i = 0; i < kSize; i++) bad += ((uint8_t *)p)[i] != data[i];
    staging->Unmap();
    {
        std::unique_lock<std::mutex> l(m);
        stage = 2;
        cv.notify_all();
    }
    writer.join();
    printf("threads11 map10 %d of %u bytes wrong\n", bad, kSize);
    staging->Release(), b->Release(), d->Release(), adapter->Release(), factory->Release();
}

static void Enter() {
    Dev dev;
    ID3D11Multithread *mt;
    CHECK(dev.ctx->QueryInterface(__uuidof(ID3D11Multithread), (void **)&mt));
    mt->SetMultithreadProtected(TRUE);
    D3D11_BUFFER_DESC bd = {kSize, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
    D3D11_BUFFER_DESC sd = {kSize, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0};
    ID3D11Buffer *b, *staging;
    CHECK(dev.device->CreateBuffer(&bd, nullptr, &b));
    CHECK(dev.device->CreateBuffer(&sd, nullptr, &staging));
    std::vector<uint8_t> first(kSize, 0x11), second(kSize, 0x22);
    std::atomic<int> go{0};
    mt->Enter();
    dev.ctx->UpdateSubresource(b, 0, nullptr, first.data(), 0, 0);
    std::thread other([&] {
        go = 1;
        dev.ctx->UpdateSubresource(b, 0, nullptr, second.data(), 0, 0); // natively: blocked until Leave
    });
    while (!go) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // the other thread's call, if it isn't blocked
    dev.ctx->CopyResource(staging, b);
    mt->Leave();
    other.join();
    D3D11_MAPPED_SUBRESOURCE m;
    CHECK(dev.ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m));
    int bad = 0;
    for (UINT i = 0; i < kSize; i++) bad += ((uint8_t *)m.pData)[i] != first[i];
    dev.ctx->Unmap(staging, 0);
    printf("threads11 enter %d of %u bytes not the first thread's\n", bad, kSize);
    mt->Release(), staging->Release(), b->Release();
}

int main() {
    Map10();
    Enter();
    return 0;
}
