// See formats.h.
#include "formats.h"

mn_format mn_format_of(DXGI_FORMAT f) {
    if (f == (DXGI_FORMAT)191) return {1, 1, 2}; // DXGI_FORMAT_A4B4G4R4_UNORM, newer than the headers
    switch (f) {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
        return {1, 1, 16};
    case DXGI_FORMAT_R32G32B32_TYPELESS: case DXGI_FORMAT_R32G32B32_FLOAT: case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32B32_SINT:
        return {1, 1, 12};
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT: case DXGI_FORMAT_R16G16B16A16_SNORM: case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32G32_TYPELESS: case DXGI_FORMAT_R32G32_FLOAT: case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT: case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT: case DXGI_FORMAT_Y416:
    case DXGI_FORMAT_Y210: case DXGI_FORMAT_Y216:
        return f == DXGI_FORMAT_Y210 || f == DXGI_FORMAT_Y216 ? mn_format{2, 1, 8} : mn_format{1, 1, 8};
    case DXGI_FORMAT_R8G8_TYPELESS: case DXGI_FORMAT_R8G8_UNORM: case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SNORM: case DXGI_FORMAT_R8G8_SINT: case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_D16_UNORM: case DXGI_FORMAT_R16_UNORM: case DXGI_FORMAT_R16_UINT: case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_SINT: case DXGI_FORMAT_B5G6R5_UNORM: case DXGI_FORMAT_B5G5R5A1_UNORM:
    case DXGI_FORMAT_B4G4R4A4_UNORM: case DXGI_FORMAT_A8P8:
        return {1, 1, 2};
    case DXGI_FORMAT_R8_TYPELESS: case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_R8_UINT: case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT: case DXGI_FORMAT_A8_UNORM: case DXGI_FORMAT_AI44: case DXGI_FORMAT_IA44:
    case DXGI_FORMAT_P8:
        return {1, 1, 1};
    case DXGI_FORMAT_R1_UNORM:
        return {8, 1, 1};
    case DXGI_FORMAT_R8G8_B8G8_UNORM: case DXGI_FORMAT_G8R8_G8B8_UNORM: case DXGI_FORMAT_YUY2:
        return {2, 1, 4};
    case DXGI_FORMAT_BC1_TYPELESS: case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS: case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM:
        return {4, 4, 8};
    case DXGI_FORMAT_BC2_TYPELESS: case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC5_TYPELESS: case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS: case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS: case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB:
        return {4, 4, 16};
    case DXGI_FORMAT_UNKNOWN:
        return {1, 1, 0};
    default:
        // ponytail: the planar video formats (NV12, P010, ...) count their first plane only; a game's initial data
        // in one would come up short. Their whole size when one shows up.
        return {1, 1, 4};
    }
}

mn_res described(const D3D11_BUFFER_DESC &d) { return mn_res_of(1, DXGI_FORMAT_UNKNOWN, d.ByteWidth, 1, 1, 1, 1); }
mn_res described(const D3D11_TEXTURE1D_DESC &d) { return mn_res_of(2, d.Format, d.Width, 1, 1, d.MipLevels, d.ArraySize); }
mn_res described(const D3D11_TEXTURE2D_DESC &d) {
    return mn_res_of(3, d.Format, d.Width, d.Height, 1, d.MipLevels, d.ArraySize);
}
mn_res described(const D3D11_TEXTURE2D_DESC1 &d) {
    return mn_res_of(3, d.Format, d.Width, d.Height, 1, d.MipLevels, d.ArraySize);
}
mn_res described(const D3D11_TEXTURE3D_DESC &d) { return mn_res_of(4, d.Format, d.Width, d.Height, d.Depth, d.MipLevels, 1); }
mn_res described(const D3D11_TEXTURE3D_DESC1 &d) { return mn_res_of(4, d.Format, d.Width, d.Height, d.Depth, d.MipLevels, 1); }
mn_res described(const D3D10_BUFFER_DESC &d) { return mn_res_of(1, DXGI_FORMAT_UNKNOWN, d.ByteWidth, 1, 1, 1, 1); }
mn_res described(const D3D10_TEXTURE1D_DESC &d) { return mn_res_of(2, d.Format, d.Width, 1, 1, d.MipLevels, d.ArraySize); }
mn_res described(const D3D10_TEXTURE2D_DESC &d) {
    return mn_res_of(3, d.Format, d.Width, d.Height, 1, d.MipLevels, d.ArraySize);
}
mn_res described(const D3D10_TEXTURE3D_DESC &d) { return mn_res_of(4, d.Format, d.Width, d.Height, d.Depth, d.MipLevels, 1); }

bool mn_described_ok(const mn_res &r) {
    if (r.dim == D3D11_RESOURCE_DIMENSION_BUFFER) return true;
    UINT longest = r.width > r.height ? r.width : r.height, chain = 0;
    if (r.dim == D3D11_RESOURCE_DIMENSION_TEXTURE3D && r.depth > longest) longest = r.depth;
    for (; longest; longest >>= 1) chain++;
    return r.mips <= chain && r.array <= D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
}

mn_res mn_res_of(int dim, DXGI_FORMAT format, UINT width, UINT height, UINT depth, UINT mips, UINT array) {
    mn_res r;
    r.dim = dim, r.format = format, r.width = width, r.height = height ? height : 1, r.depth = depth ? depth : 1;
    r.array = array ? array : 1;
    if (!mips) // the whole chain, to 1x1x1
        for (UINT m = width > r.height ? width : r.height, n = m > r.depth ? m : r.depth; n; n >>= 1) mips++;
    r.mips = mips ? mips : 1;
    return r;
}

UINT mn_subresources(const mn_res &r) {
    return r.dim == D3D11_RESOURCE_DIMENSION_BUFFER ? 1 : r.mips * (r.dim == D3D11_RESOURCE_DIMENSION_TEXTURE3D ? 1 : r.array);
}

uint64_t mn_subresource_bytes(const mn_res &r, UINT sub, const D3D11_BOX *box, UINT pitch, UINT slice) {
    if (r.dim == D3D11_RESOURCE_DIMENSION_BUFFER)
        return box ? (box->right > box->left ? box->right - box->left : 0) : r.width;
    UINT mip = sub % r.mips;
    uint64_t w = r.width >> mip, h = r.height >> mip, d = r.depth >> mip;
    w = w ? w : 1, h = h ? h : 1, d = d ? d : 1;
    if (r.dim == D3D11_RESOURCE_DIMENSION_TEXTURE1D) h = d = 1;
    if (r.dim != D3D11_RESOURCE_DIMENSION_TEXTURE3D) d = 1;
    if (box) {
        w = box->right > box->left ? box->right - box->left : 0;
        h = box->bottom > box->top ? box->bottom - box->top : 0;
        d = box->back > box->front ? box->back - box->front : 0;
    }
    mn_format f = mn_format_of(r.format);
    if (!w || !h || !d || !f.bytes) return 0;
    uint64_t rows = (h + f.block_h - 1) / f.block_h, row = (w + f.block_w - 1) / f.block_w * f.bytes;
    return (d - 1) * slice + (rows - 1) * pitch + row;
}

bool mn_data_fits(const mn_res &r, UINT sub, const D3D11_BOX *box, UINT pitch, UINT slice, uint64_t len) {
    if (!r.dim || sub >= mn_subresources(r)) return false;
    if (box) {
        UINT mip = r.dim == D3D11_RESOURCE_DIMENSION_BUFFER ? 0 : sub % r.mips;
        UINT w = r.width >> mip, h = r.height >> mip, d = r.depth >> mip;
        w = w ? w : 1, h = h ? h : 1, d = d ? d : 1;
        if (r.dim == D3D11_RESOURCE_DIMENSION_BUFFER) h = d = 1;
        if (box->left > box->right || box->top > box->bottom || box->front > box->back || box->right > w ||
            box->bottom > h || box->back > d)
            return false;
    }
    return mn_subresource_bytes(r, sub, box, pitch, slice) <= len;
}
