// A D3D11 or D3D10 resource as its creation described it, and the bytes of a subresource's data in it (M4 Task 4): what
// initial data and UpdateSubresource carry, which the forwarding sizes itself (the API passes only pointers and pitches).
#pragma once
#include <cstdint>
#include <windows.h>
#include <d3d11_4.h>
#include <d3d10.h>

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
// A creation's description's resource (D3D11's and D3D10's), guest and host alike.
mn_res described(const D3D11_BUFFER_DESC &d);
mn_res described(const D3D11_TEXTURE1D_DESC &d);
mn_res described(const D3D11_TEXTURE2D_DESC &d);
mn_res described(const D3D11_TEXTURE2D_DESC1 &d);
mn_res described(const D3D11_TEXTURE3D_DESC &d);
mn_res described(const D3D11_TEXTURE3D_DESC1 &d);
mn_res described(const D3D10_BUFFER_DESC &d);
mn_res described(const D3D10_TEXTURE1D_DESC &d);
mn_res described(const D3D10_TEXTURE2D_DESC &d);
mn_res described(const D3D10_TEXTURE3D_DESC &d);
// Its subresources: mips, times the array (not a 3D texture's).
UINT mn_subresources(const mn_res &r);
// The bytes of subresource sub's data laid out with pitch and slice (box: within it, or the whole subresource):
// (depth - 1) * slice + (rows - 1) * pitch + a row's blocks, rows counted in blocks; a buffer's, its width (or box's).
uint64_t mn_subresource_bytes(const mn_res &r, UINT sub, const D3D11_BOX *box, UINT pitch, UINT slice);
// The host's check (M4's final review): subresource sub of r exists, box (or none) lies inside it, and len bytes cover
// what DXMT reads of its data with pitch and slice.
bool mn_data_fits(const mn_res &r, UINT sub, const D3D11_BOX *box, UINT pitch, UINT slice, uint64_t len);
