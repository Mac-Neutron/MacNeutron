// Many indirect draws in one render pass, as Unreal draws grass and GPU particles:
//   d3d12_indirect.exe <vs.dxil> <ps.dxil> <cs.dxil> [<vsid.dxil> <psid.dxil> [<vsvid.dxil> [<vsstart.dxil>]]]
//   (shaders/indirect.hlsl)
// Each frame clears a 64x64 target and paints its 1024 cells with one ExecuteIndirect each (one draw command, its
// StartInstanceLocation picks the cell from a per-instance vertex stream, as Unreal's instance ids), then runs 1024
// indirect dispatches of 1 to 4 groups whose threads count themselves (as Niagara's GPU simulation). Prints "indirect ok <frames> <cells left unpainted over all frames>" and
// "indirect dispatch <threads counted> <threads dispatched>".
// With vsid and psid, also the cells (x,y:tag) that ExecuteIndirect calls paint on a 32x8 target (Draws below):
// "indirect native-draw", "indirect counted", "indirect native-indexed", "indirect aliased-indexed" and
// "indirect vbv-gap", "indirect vbv-null" and "indirect direct" (the same draws as DrawInstanced and DrawIndexedInstanced).
// With vsvid and vsstart (DXIL SM 6.8 plan, Task 12), the same lines again, each drawn with that vertex shader and named
// "indirect vid-<run>" and "indirect start-<run>".
#include "d3d12_common.hpp"
#include <string>

// GPU efficiency E10: draw signatures that set nothing but the draw. Each call clears the 32x8 target, runs one
// ExecuteIndirect and prints the painted cells: vertex v (its index in a per-vertex stream of 0, 1, 2...) of instance i
// paints cell (v / 3, i) with the instance's tag, 100 + its index in the per-instance stream.
static void Draws(Gpu &gpu, ID3D12RootSignature *root, const std::vector<char> &vs, const std::vector<char> &ps,
                  const char *prefix) {
    // The pipeline, its tags in IA slot `tag_slot`.
    auto pipeline = [&](UINT tag_slot) {
        D3D12_INPUT_ELEMENT_DESC streams[2] = {
            {"VERT", 0, DXGI_FORMAT_R32_UINT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TAG", 0, DXGI_FORMAT_R32_UINT, tag_slot, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1}};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC gd = {};
        gd.pRootSignature = root;
        gd.VS = {vs.data(), vs.size()};
        gd.PS = {ps.data(), ps.size()};
        gd.InputLayout = {streams, 2};
        gd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        gd.SampleMask = UINT_MAX;
        gd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        gd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        gd.RasterizerState.DepthClipEnable = TRUE;
        gd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        gd.NumRenderTargets = 1;
        gd.RTVFormats[0] = DXGI_FORMAT_R32_UINT;
        gd.SampleDesc.Count = 1;
        ID3D12PipelineState *pso;
        CHECK(gpu.device->CreateGraphicsPipelineState(&gd, __uuidof(ID3D12PipelineState), (void **)&pso));
        return pso;
    };
    ID3D12PipelineState *pso = pipeline(1);
    auto signature = [&](D3D12_INDIRECT_ARGUMENT_TYPE type, UINT stride) {
        D3D12_INDIRECT_ARGUMENT_DESC arg = {type};
        D3D12_COMMAND_SIGNATURE_DESC sd = {stride, 1, &arg, 0};
        ID3D12CommandSignature *sig;
        CHECK(gpu.device->CreateCommandSignature(&sd, nullptr, __uuidof(ID3D12CommandSignature), (void **)&sig));
        return sig;
    };
    auto upload = [&](const void *data, UINT size) {
        ID3D12Resource *b = gpu.Buffer(D3D12_HEAP_TYPE_UPLOAD, size, D3D12_RESOURCE_STATE_GENERIC_READ);
        void *p;
        CHECK(b->Map(0, nullptr, &p));
        memcpy(p, data, size);
        b->Unmap(0, nullptr);
        return b;
    };
    UINT verts[64], tags[16];
    for (UINT i = 0; i < 64; i++) verts[i] = i;
    for (UINT i = 0; i < 16; i++) tags[i] = 100 + i;
    ID3D12Resource *vert_buffer = upload(verts, sizeof(verts)), *tag_buffer = upload(tags, sizeof(tags));
    D3D12_VERTEX_BUFFER_VIEW vbv[2] = {{vert_buffer->GetGPUVirtualAddress(), sizeof(verts), 4},
                                       {tag_buffer->GetGPUVirtualAddress(), sizeof(tags), 4}};
    ID3D12Resource *target = gpu.Texture(Tex2D(32, 8, DXGI_FORMAT_R32_UINT, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
                                         D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12DescriptorHeap *rtv_heap;
    D3D12_DESCRIPTOR_HEAP_DESC rhd = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1};
    CHECK(gpu.device->CreateDescriptorHeap(&rhd, __uuidof(ID3D12DescriptorHeap), (void **)&rtv_heap));
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    gpu.device->CreateRenderTargetView(target, nullptr, rtv);
    ID3D12Resource *readback = gpu.Buffer(D3D12_HEAP_TYPE_READBACK, 256 * 8, D3D12_RESOURCE_STATE_COPY_DEST);
    // Draws with `draw`, then prints the painted cells.
    auto run = [&](const char *name, auto draw) {
        ID3D12GraphicsCommandList *list = gpu.list;
        const float zero[4] = {0, 0, 0, 0};
        D3D12_VIEWPORT viewport = {0, 0, 32, 8, 0, 1};
        D3D12_RECT scissor = {0, 0, 32, 8};
        list->ClearRenderTargetView(rtv, zero, 0, nullptr);
        list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        list->RSSetViewports(1, &viewport);
        list->RSSetScissorRects(1, &scissor);
        list->SetGraphicsRootSignature(root);
        list->SetPipelineState(pso);
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list->IASetVertexBuffers(0, 2, vbv);
        draw(list);
        gpu.Barrier(target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src = {target, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX}; src.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION dst = {readback, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        dst.PlacedFootprint = {0, {DXGI_FORMAT_R32_UINT, 32, 8, 1, 256}};
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        gpu.Barrier(target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        gpu.Submit();
        uint8_t *r; D3D12_RANGE whole = {0, 256 * 8}, none = {0, 0};
        CHECK(readback->Map(0, &whole, (void **)&r));
        std::string cells;
        for (UINT y = 0; y < 8; y++)
            for (UINT x = 0; x < 32; x++) {
                UINT v;
                memcpy(&v, r + y * 256 + x * 4, 4);
                if (v) cells += " " + std::to_string(x) + "," + std::to_string(y) + ":" + std::to_string(v);
            }
        readback->Unmap(0, &none);
        printf("indirect %s%s%s\n", prefix, name, cells.c_str());
    };

    // Two records at offset 64: 3 vertices from 6, 2 instances from 7 (cell 2, tags 107 108); 6 vertices from 9, 1
    // instance from 0 (cells 3 and 4, tag 100).
    ID3D12CommandSignature *draw = signature(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW, sizeof(D3D12_DRAW_ARGUMENTS));
    D3D12_DRAW_ARGUMENTS records[18] = {};
    records[16] = {3, 2, 6, 7};
    records[17] = {6, 1, 9, 0};
    ID3D12Resource *args = upload(records, sizeof(records));
    run("native-draw", [&](ID3D12GraphicsCommandList *l) { l->ExecuteIndirect(draw, 2, args, 64 * sizeof(UINT), nullptr, 0); });
    // A count buffer of 1 caps the same call at its first record.
    UINT one = 1;
    ID3D12Resource *count = upload(&one, 4);
    run("counted", [&](ID3D12GraphicsCommandList *l) { l->ExecuteIndirect(draw, 2, args, 64 * sizeof(UINT), count, 0); });

    // Indexed, 32 bytes a record (the rest filled with junk), at offset 32, an index buffer view 16 bytes into its
    // buffer: 3 indices from 0 (cell 0, tag 100); 6 from 3, base vertex -1, 2 instances from 7 (cells 4 and 5, tags 107
    // 108); no instance (drawn, it would paint cell 1, tag 105); 3 from 9, base vertex 2, instance 2 (cell 7, tag 102).
    ID3D12CommandSignature *indexed = signature(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED, 32);
    UINT indices[16] = {0xdead, 0xdead, 0xdead, 0xdead, 0, 1, 2, 13, 14, 15, 16, 17, 18, 19, 20, 21};
    ID3D12Resource *ib = upload(indices, sizeof(indices));
    D3D12_INDEX_BUFFER_VIEW ibv = {ib->GetGPUVirtualAddress() + 16, 48, DXGI_FORMAT_R32_UINT};
    UINT words[40];
    for (UINT &w : words) w = 0xdeadbeef;
    D3D12_DRAW_INDEXED_ARGUMENTS indexed_records[4] = {{3, 1, 0, 0, 0}, {6, 2, 3, -1, 7}, {3, 0, 0, 3, 5}, {3, 1, 9, 2, 2}};
    for (UINT i = 0; i < 4; i++) memcpy(words + 8 + i * 8, &indexed_records[i], sizeof(indexed_records[i]));
    ID3D12Resource *indexed_args = upload(words, sizeof(words));
    run("native-indexed", [&](ID3D12GraphicsCommandList *l) {
        l->IASetIndexBuffer(&ibv);
        l->ExecuteIndirect(indexed, 4, indexed_args, 32, nullptr, 0);
    });

    // The same indices in a buffer placed over another one (at the same heap offset, as transient heaps alias), which
    // is released once the index buffer is bound, before the list runs: the same cells.
    ID3D12Heap *heap;
    D3D12_HEAP_DESC hd = {65536, {D3D12_HEAP_TYPE_UPLOAD}, 0, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS};
    CHECK(gpu.device->CreateHeap(&hd, __uuidof(ID3D12Heap), (void **)&heap));
    D3D12_RESOURCE_DESC bd = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, sizeof(indices), 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
                              D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    ID3D12Resource *first, *placed;
    CHECK(gpu.device->CreatePlacedResource(heap, 0, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                           __uuidof(ID3D12Resource), (void **)&first));
    CHECK(gpu.device->CreatePlacedResource(heap, 0, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                           __uuidof(ID3D12Resource), (void **)&placed));
    void *p;
    CHECK(placed->Map(0, nullptr, &p));
    memcpy(p, indices, sizeof(indices));
    placed->Unmap(0, nullptr);
    D3D12_INDEX_BUFFER_VIEW placed_ibv = {placed->GetGPUVirtualAddress() + 16, 48, DXGI_FORMAT_R32_UINT};
    run("aliased-indexed", [&](ID3D12GraphicsCommandList *l) {
        l->IASetIndexBuffer(&placed_ibv);
        first->Release();
        l->ExecuteIndirect(indexed, 4, indexed_args, 32, nullptr, 0);
    });

    // A vertex buffer argument for slot 2 of a layout using slots 0 and 2 (DXMT's shaders read a table of the used
    // slots only), slot 2 bound to other tags (200 + index) beforehand: two records, each a view of slot 2, a view of
    // slot 5 (above the highest slot the layout uses: it binds nothing the draw reads, and must not reach the next
    // record's table) on the stale tags, then a draw. 3 vertices from 6, 2 instances from 7 on the tags (cell 2, tags
    // 107 108); 3 from 9, 1 instance from 0 on 300 + index (cell 3, tag 300).
    ID3D12PipelineState *gap = pipeline(2);
    UINT stale[16], later[16];
    for (UINT i = 0; i < 16; i++) { stale[i] = 200 + i; later[i] = 300 + i; }
    ID3D12Resource *stale_buffer = upload(stale, sizeof(stale)), *later_buffer = upload(later, sizeof(later));
    D3D12_VERTEX_BUFFER_VIEW stale_vbv = {stale_buffer->GetGPUVirtualAddress(), sizeof(stale), 4};
    D3D12_INDIRECT_ARGUMENT_DESC vb_draw[3] = {{D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW},
                                               {D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW},
                                               {D3D12_INDIRECT_ARGUMENT_TYPE_DRAW}};
    vb_draw[0].VertexBuffer.Slot = 2;
    vb_draw[1].VertexBuffer.Slot = 5;
    D3D12_COMMAND_SIGNATURE_DESC vsd = {2 * sizeof(D3D12_VERTEX_BUFFER_VIEW) + sizeof(D3D12_DRAW_ARGUMENTS), 3, vb_draw, 0};
    ID3D12CommandSignature *vb_signature;
    CHECK(gpu.device->CreateCommandSignature(&vsd, nullptr, __uuidof(ID3D12CommandSignature), (void **)&vb_signature));
    struct { D3D12_VERTEX_BUFFER_VIEW view, above; D3D12_DRAW_ARGUMENTS draw; } vb_records[2] = {
        {{tag_buffer->GetGPUVirtualAddress(), sizeof(tags), 4}, stale_vbv, {3, 2, 6, 7}},
        {{later_buffer->GetGPUVirtualAddress(), sizeof(later), 4}, stale_vbv, {3, 1, 9, 0}}};
    ID3D12Resource *vb_args = upload(vb_records, sizeof(vb_records));
    run("vbv-gap", [&](ID3D12GraphicsCommandList *l) {
        l->SetPipelineState(gap);
        l->IASetVertexBuffers(2, 1, &stale_vbv);
        l->ExecuteIndirect(vb_signature, 2, vb_args, 0, nullptr, 0);
    });
    // The same with a null view in the first record (Task F2): its draw reads tag 0, not the stale tags (cell 2 stays
    // clear), and the second record still draws (cell 3, tag 300).
    vb_records[0].view = {};
    ID3D12Resource *null_args = upload(vb_records, sizeof(vb_records));
    run("vbv-null", [&](ID3D12GraphicsCommandList *l) {
        l->SetPipelineState(gap);
        l->IASetVertexBuffers(2, 1, &stale_vbv);
        l->ExecuteIndirect(vb_signature, 2, null_args, 0, nullptr, 0);
    });
    // The draws of native-draw and native-indexed's drawn records, direct (Task 12): cells 2, 3 and 4 (tag 100), then
    // cells 4 and 5 (over 4) and 7.
    run("direct", [&](ID3D12GraphicsCommandList *l) {
        l->DrawInstanced(3, 2, 6, 7);
        l->DrawInstanced(6, 1, 9, 0);
        l->IASetIndexBuffer(&ibv);
        l->DrawIndexedInstanced(6, 2, 3, -1, 7);
        l->DrawIndexedInstanced(3, 1, 9, 2, 2);
    });
}

int main(int argc, char **argv) {
    if (argc != 4 && (argc < 6 || argc > 8)) {
        printf("usage: d3d12_indirect.exe <vs.dxil> <ps.dxil> <cs.dxil> [<vsid.dxil> <psid.dxil> [<vsvid.dxil> [<vsstart.dxil>]]]\n");
        return 2;
    }
    std::vector<char> vs = Load(argv[1]), ps = Load(argv[2]), cs = Load(argv[3]);
    if (vs.empty() || ps.empty() || cs.empty()) { printf("can't read the shaders\n"); return 1; }
    const UINT cells = 1024, frames = 8, size = 64, pitch = size * 4;
    Gpu gpu;
    D3D12_ROOT_SIGNATURE_DESC rd = {0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    ID3D12RootSignature *root = gpu.RootSignature(rd);
    D3D12_INPUT_ELEMENT_DESC cell = {"CELL", 0, DXGI_FORMAT_R32_UINT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC gd = {};
    gd.pRootSignature = root;
    gd.VS = {vs.data(), vs.size()};
    gd.PS = {ps.data(), ps.size()};
    gd.InputLayout = {&cell, 1};
    gd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    gd.SampleMask = UINT_MAX;
    gd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    gd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    gd.RasterizerState.DepthClipEnable = TRUE;
    gd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    gd.NumRenderTargets = 1;
    gd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    gd.SampleDesc.Count = 1;
    ID3D12PipelineState *pso;
    CHECK(gpu.device->CreateGraphicsPipelineState(&gd, __uuidof(ID3D12PipelineState), (void **)&pso));
    D3D12_INDIRECT_ARGUMENT_DESC arg = {D3D12_INDIRECT_ARGUMENT_TYPE_DRAW};
    D3D12_COMMAND_SIGNATURE_DESC sd = {sizeof(D3D12_DRAW_ARGUMENTS), 1, &arg, 0};
    ID3D12CommandSignature *sig;
    CHECK(gpu.device->CreateCommandSignature(&sd, nullptr, __uuidof(ID3D12CommandSignature), (void **)&sig));

    ID3D12Resource *args = gpu.Buffer(D3D12_HEAP_TYPE_UPLOAD, cells * sizeof(D3D12_DRAW_ARGUMENTS),
                                      D3D12_RESOURCE_STATE_GENERIC_READ);
    D3D12_DRAW_ARGUMENTS *a; D3D12_RANGE none = {0, 0};
    CHECK(args->Map(0, &none, (void **)&a));
    for (UINT i = 0; i < cells; i++) a[i] = {6, 1, 0, i};
    args->Unmap(0, nullptr);
    ID3D12Resource *ids = gpu.Buffer(D3D12_HEAP_TYPE_UPLOAD, cells * 4, D3D12_RESOURCE_STATE_GENERIC_READ);
    UINT *id;
    CHECK(ids->Map(0, &none, (void **)&id));
    for (UINT i = 0; i < cells; i++) id[i] = i;
    ids->Unmap(0, nullptr);
    D3D12_VERTEX_BUFFER_VIEW vbv = {ids->GetGPUVirtualAddress(), cells * 4, 4};

    ID3D12Resource *target = gpu.Texture(Tex2D(size, size, DXGI_FORMAT_R8G8B8A8_UNORM, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
                                         D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12DescriptorHeap *rtv_heap;
    D3D12_DESCRIPTOR_HEAP_DESC rhd = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1};
    CHECK(gpu.device->CreateDescriptorHeap(&rhd, __uuidof(ID3D12DescriptorHeap), (void **)&rtv_heap));
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    gpu.device->CreateRenderTargetView(target, nullptr, rtv);
    ID3D12Resource *readback = gpu.Buffer(D3D12_HEAP_TYPE_READBACK, frames * size * pitch + 4, D3D12_RESOURCE_STATE_COPY_DEST);

    D3D12_ROOT_PARAMETER uav = {};
    uav.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_ROOT_SIGNATURE_DESC crd = {1, &uav, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ID3D12RootSignature *croot = gpu.RootSignature(crd);
    D3D12_COMPUTE_PIPELINE_STATE_DESC cd = {croot, {cs.data(), cs.size()}};
    ID3D12PipelineState *cpso;
    CHECK(gpu.device->CreateComputePipelineState(&cd, __uuidof(ID3D12PipelineState), (void **)&cpso));
    D3D12_INDIRECT_ARGUMENT_DESC darg = {D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH};
    D3D12_COMMAND_SIGNATURE_DESC dsd = {sizeof(D3D12_DISPATCH_ARGUMENTS), 1, &darg, 0};
    ID3D12CommandSignature *dsig;
    CHECK(gpu.device->CreateCommandSignature(&dsd, nullptr, __uuidof(ID3D12CommandSignature), (void **)&dsig));
    ID3D12Resource *dargs = gpu.Buffer(D3D12_HEAP_TYPE_UPLOAD, cells * sizeof(D3D12_DISPATCH_ARGUMENTS) + 4,
                                       D3D12_RESOURCE_STATE_GENERIC_READ);
    D3D12_DISPATCH_ARGUMENTS *d;
    UINT threads = 0;
    CHECK(dargs->Map(0, &none, (void **)&d));
    for (UINT i = 0; i < cells; i++) { d[i] = {i % 4 + 1, 1, 1}; threads += (i % 4 + 1) * 4; }
    memset(d + cells, 0, 4);  // the counter's starting value
    dargs->Unmap(0, nullptr);
    ID3D12Resource *counter = gpu.Buffer(D3D12_HEAP_TYPE_DEFAULT, 4, D3D12_RESOURCE_STATE_COPY_DEST,
                                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    gpu.list->CopyBufferRegion(counter, 0, dargs, cells * sizeof(D3D12_DISPATCH_ARGUMENTS), 4);
    gpu.Barrier(counter, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    D3D12_VIEWPORT viewport = {0, 0, (float)size, (float)size, 0, 1};
    D3D12_RECT scissor = {0, 0, (LONG)size, (LONG)size};
    const float black[4] = {0, 0, 0, 0};
    for (UINT f = 0; f < frames; f++) {
        ID3D12GraphicsCommandList *list = gpu.list;
        list->ClearRenderTargetView(rtv, black, 0, nullptr);
        list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        list->RSSetViewports(1, &viewport);
        list->RSSetScissorRects(1, &scissor);
        list->SetGraphicsRootSignature(root);
        list->SetPipelineState(pso);
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list->IASetVertexBuffers(0, 1, &vbv);
        for (UINT i = 0; i < cells; i++)
            list->ExecuteIndirect(sig, 1, args, i * sizeof(D3D12_DRAW_ARGUMENTS), nullptr, 0);
        gpu.Barrier(target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src = {target, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX}; src.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION dst = {readback, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        dst.PlacedFootprint = {(UINT64)f * size * pitch, {DXGI_FORMAT_R8G8B8A8_UNORM, size, size, 1, pitch}};
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        gpu.Barrier(target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        list->SetComputeRootSignature(croot);
        list->SetPipelineState(cpso);
        list->SetComputeRootUnorderedAccessView(0, counter->GetGPUVirtualAddress());
        for (UINT i = 0; i < cells; i++)
            list->ExecuteIndirect(dsig, 1, dargs, i * sizeof(D3D12_DISPATCH_ARGUMENTS), nullptr, 0);
        gpu.Submit();
    }
    gpu.Barrier(counter, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu.list->CopyBufferRegion(readback, frames * size * pitch, counter, 0, 4);
    gpu.Submit();
    uint8_t *r; D3D12_RANGE whole = {0, (SIZE_T)frames * size * pitch + 4};
    CHECK(readback->Map(0, &whole, (void **)&r));
    UINT missing = 0;
    for (UINT f = 0; f < frames; f++)
        for (UINT c = 0; c < cells; c++) {
            UINT x = c % 32 * 2, y = c / 32 * 2;
            bool painted = true;
            for (UINT dy = 0; dy < 2; dy++)
                for (UINT dx = 0; dx < 2; dx++)
                    painted &= r[f * size * pitch + (y + dy) * pitch + (x + dx) * 4] == 255;
            missing += !painted;
        }
    UINT counted;
    memcpy(&counted, r + frames * size * pitch, 4);
    readback->Unmap(0, &none);
    printf("indirect ok %u %u\n", frames, missing);
    printf("indirect dispatch %u %u\n", counted, threads * frames);
    // vsid, then vsvid and vsstart when given, each with psid.
    const int vs_arg[3] = {4, 6, 7};
    const char *prefixes[3] = {"", "vid-", "start-"};
    std::vector<char> psid = argc >= 6 ? Load(argv[5]) : std::vector<char>();
    for (int k = 0; k < 3 && vs_arg[k] < argc; k++) {
        std::vector<char> draws_vs = Load(argv[vs_arg[k]]);
        if (draws_vs.empty() || psid.empty()) { printf("can't read the shaders\n"); return 1; }
        Draws(gpu, root, draws_vs, psid, prefixes[k]);
    }
    return 0;
}
