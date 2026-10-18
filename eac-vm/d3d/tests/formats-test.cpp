// The bytes initial data and UpdateSubresource carry (M4 Task 4: guest/formats.cpp), in the layouts the plan's
// Review Focus names: a 3D texture's box, a mipped array's mip 3, BC7 at a 2x2 mip tail with a box, a wide format,
// a whole mip chain, a buffer's box. Prints "formats: ok" or each wrong size.
#include "../guest/formats.h"
#include <cstdio>

static int failed;
#define WANT(got, want) do { uint64_t g_ = (got); if (g_ != (uint64_t)(want)) { \
    printf("formats: %s is %llu, not %llu\n", #got, (unsigned long long)g_, (unsigned long long)(want)); failed = 1; } } while (0)

int main() {
    const mn_res tex3d = mn_res_of(D3D11_RESOURCE_DIMENSION_TEXTURE3D, DXGI_FORMAT_R8_UNORM, 16, 16, 8, 1, 1);
    const D3D11_BOX box3 = {2, 2, 1, 10, 10, 5};
    WANT(mn_subresource_bytes(tex3d, 0, &box3, 12, 116), 3 * 116 + 7 * 12 + 8);
    const mn_res arr = mn_res_of(D3D11_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8B8A8_UNORM, 32, 32, 1, 6, 3);
    WANT(mn_subresources(arr), 18);
    WANT(mn_subresource_bytes(arr, D3D11CalcSubresource(3, 1, 6), nullptr, 16, 0), 3 * 16 + 16);
    const mn_res bc7 = mn_res_of(D3D11_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC7_UNORM, 8, 8, 1, 4, 1);
    const D3D11_BOX tail = {0, 0, 0, 2, 2, 1};
    WANT(mn_subresource_bytes(bc7, 2, &tail, 16, 0), 16);
    WANT(mn_subresource_bytes(bc7, 0, nullptr, 32, 0), 32 + 32);
    const mn_res wide = mn_res_of(D3D11_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R32G32B32_FLOAT, 16, 4, 1, 1, 1);
    WANT(mn_subresource_bytes(wide, 0, nullptr, 200, 0), 3 * 200 + 16 * 12);
    const mn_res chain = mn_res_of(D3D11_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8B8A8_UNORM, 256, 256, 1, 0, 1);
    WANT(chain.mips, 9);
    const mn_res line = mn_res_of(D3D11_RESOURCE_DIMENSION_TEXTURE1D, DXGI_FORMAT_R8_UNORM, 64, 1, 1, 0, 2);
    WANT(line.mips, 7);
    WANT(mn_subresources(line), 14);
    WANT(mn_subresource_bytes(line, 6, nullptr, 0, 0), 1);
    const mn_res buf = mn_res_of(D3D11_RESOURCE_DIMENSION_BUFFER, DXGI_FORMAT_UNKNOWN, 512, 1, 1, 1, 1);
    const D3D11_BOX bbox = {100, 0, 0, 300, 1, 1};
    WANT(mn_subresource_bytes(buf, 0, nullptr, 0, 0), 512);
    WANT(mn_subresource_bytes(buf, 0, &bbox, 0, 0), 200);
    if (!failed) printf("formats: ok\n");
    return failed;
}
