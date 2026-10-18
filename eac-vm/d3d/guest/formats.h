// A D3D11 or D3D10 resource as its creation described it, and the bytes of a subresource's data in it (M4 Task 4): what
// initial data and UpdateSubresource carry, which the forwarding sizes itself (the API passes only pointers and pitches).
#pragma once
#include <cstdint>
#include <windows.h>
#include <d3d11_4.h>

struct mn_format {
    uint8_t block_w, block_h, bytes; // a block's texels and its bytes (1x1 for an uncompressed format)
};
mn_format mn_format_of(DXGI_FORMAT format);

// dim: D3D11_RESOURCE_DIMENSION's (D3D10's is the same), 0: not known yet. mips: the chain's length, never 0.
struct mn_res {
    int dim = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT width = 0, height = 1, depth = 1, mips = 1, array = 1;
};
// A description's resource, its mips counted when MipLevels is 0 (the whole chain).
mn_res mn_res_of(int dim, DXGI_FORMAT format, UINT width, UINT height, UINT depth, UINT mips, UINT array);
// Its subresources: mips, times the array (not a 3D texture's).
UINT mn_subresources(const mn_res &r);
// The bytes of subresource sub's data laid out with pitch and slice (box: within it, or the whole subresource):
// (depth - 1) * slice + (rows - 1) * pitch + a row's blocks, rows counted in blocks; a buffer's, its width (or box's).
uint64_t mn_subresource_bytes(const mn_res &r, UINT sub, const D3D11_BOX *box, UINT pitch, UINT slice);
