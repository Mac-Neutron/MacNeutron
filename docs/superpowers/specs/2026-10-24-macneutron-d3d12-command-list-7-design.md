# DXMT's D3D12 command lists up to ID3D12GraphicsCommandList7, with render passes and enhanced barriers

Status: design approved in sections 2026-10-24; spec awaiting the maintainer's review.
Branch: `vm-mixed-arch-games` (DXMT patches from 0124 in `wine-arm64/patches/dxmt`).
Related: `2026-09-29-macneutron-d3d12-stubs-design.md` (silent stubs break scenes), `2026-10-01-macneutron-gpu-overlap-design.md`
(barriers order encoders), `2026-10-02-macneutron-gpu-efficiency-design.md` (B6 merges, E7 barrier names),
`2026-10-17-macneutron-vm-d3d-forwarding-design.md` (the VM's forwarder).

## 1. Goal

3DMark's Steel Nomad (231350, D3D12) stops at its first command list: it asks CreateCommandList for
ID3D12GraphicsCommandList4, 5 and 6 (`dx_command_buffer.cpp:37`, E_NOINTERFACE). DXMT's command list stops at List2,
and the VM's forwarder refuses List3 to List7 and Device9/10 whole. This blocks Steel Nomad on macOS and in the VM.

Done when:
- Every ID3D12GraphicsCommandList3 to 7 method exists in DXMT and does what the D3D12 spec requires at the tiers DXMT
  reports. Render passes, ResolveSubresourceRegion and enhanced barriers really work; ray tracing, VRS, mesh shaders,
  meta commands and protected sessions stay reported unsupported, and their methods are logged no-ops.
- DXMT's device is ID3D12Device10 and reports `EnhancedBarriersSupported`.
- The forwarder forwards all of it (VM, 64-bit; the default 32-bit lane runs guest DXMT).
- The §7 rows pass in the Mac loopback gate and in d3d-smoke's four lanes, and the full gates pass.
- Steel Nomad's workload runs past CreateCommandList in the VM, and on to its score or to the next blocker, named.

## 2. Decisions (maintainer, 2026-10-23 and 2026-10-24)

| Question | Decision |
|---|---|
| Scope | Full List3 to List7 parity, not only what Steel Nomad calls. |
| GPU features behind the new methods | Interfaces only: ray tracing, VRS and mesh shaders stay reported unsupported (their caps stay off). |
| Enhanced barriers | Real (approach A+): `EnhancedBarriersSupported = TRUE`, Barrier translated onto DXMT's barrier model, Device9/10 for layout-based creation. |
| Where render passes are emulated | Once, in DXMT's command list: one source serves the macOS PE build, the VM host's native DXMT and the guest's. Not in the guest proxy (VM only). |

## 3. Where it changes

| Piece | Files | Change |
|---|---|---|
| Command list | DXMT `src/d3d12/d3d12_command_list.cpp`, `d3d12_device.hpp` | `MTLD3D12GraphicsCommandList : ID3D12GraphicsCommandList7`; QueryInterface answers List3 to 7. §4, §5, §6. |
| Device | DXMT `src/d3d12/d3d12_device.cpp`, `d3d12_device.hpp` | `MTLD3D12Device : ID3D12Device10`; QueryInterface answers 9 and 10. §5.2. |
| Forwarder | `eac-vm/d3d/rules.txt`, `guest/proxy.cpp` if a special is needed | §6.2. |
| Gates | `eac-vm/d3d/tests/smoke.cpp` | §7. |

All three DXMT builds compile against `include/native/d3d12/d3d12.h`, which declares List4 to List7, Device9/10, the
render-pass structs and the barrier groups. No header work.

Order, each step with its smoke row first (RED):
1. Interfaces and logged stubs in DXMT, the forwarder's blanket refusals replaced by per-method rules. Then Steel
   Nomad's workload in a test VM with `MACNEUTRON_VM_D3D_TRACE=1`: which of the new methods it really calls.
2. ResolveSubresourceRegion (§4.1). Render passes resolve through it.
3. Render passes (§4.2).
4. Enhanced barriers and Device9/10 (§5).
5. Full gates, Steel Nomad again.

## 4. Render passes

### 4.1 ResolveSubresourceRegion

- The rectangle covers the whole source subresource (or is null) and DstX = DstY = 0: the existing `ResolveSubresource`
  (a resolve pass: Metal's store-action resolve).
- A part of it: resolved into a scratch single-sample texture the source's size, then a blit copies the rectangle to
  (DstX, DstY). The destination's pixels outside the region stay as they were. The scratch texture lives until the
  allocator resets, as MetalFX's scratch textures do.
- Modes:
  - AVERAGE on a colour format: as above.
  - DECOMPRESS: nothing (Metal keeps no compressed state to expand).
  - ENCODE/DECODE_SAMPLER_FEEDBACK: nothing (SamplerFeedbackTier NOT_SUPPORTED).
  - MIN, MAX, and any depth/stencil resolve: WARN and skip. DXMT's resolve pass has a colour attachment only and
    winemetal no depth resolve filter. A `ponytail:` comment names the ceiling; added when a trace shows a game using
    them.

### 4.2 BeginRenderPass and EndRenderPass

BeginRenderPass(NumRenderTargets, pRenderTargets, pDepthStencil, Flags):
1. The beginning accesses:
   - CLEAR: a full-view ClearRenderTargetView, or ClearDepthStencilView with the depth and stencil planes each from
     their own access (D3D12_CLEAR_FLAG_DEPTH / _STENCIL). `FoldClears` makes them the pass's load actions at its first
     draw: no clear encoder of their own.
   - PRESERVE, DISCARD, NO_ACCESS, PRESERVE_LOCAL_RENDER/SRV/UAV, and a type DXMT doesn't know: nothing.
2. OMSetRenderTargets with the descs' `cpuDescriptor`s. The depth-stencil view is bound unless both of its planes'
   beginning accesses are NO_ACCESS.
3. The ending accesses (with each resolve's subresource parameters copied) are kept in the list.

EndRenderPass: each RESOLVE ending access's subresource parameters, one ResolveSubresourceRegion each (its source and
destination, subresources, rectangle, offset, Format, ResolveMode). DISCARD, PRESERVE, NO_ACCESS and the rest: nothing
(DiscardResource already does nothing).

Every flag is accepted and changes nothing. SUSPENDING_PASS and RESUMING_PASS only say "a logical flow of passes is
straddling command list boundaries": the same surface-flow rules apply across them as between any two passes, so
both passes' accesses take effect (DirectX-Specs, RenderPasses, "Suspend and Resume"; its HLK test: "no rendering
difference" against ENDING_PRESERVE/BEGINNING_PRESERVE). Corrected 2026-10-24 from a first version that skipped the
resuming pass's beginning and the suspending pass's ending accesses. ALLOW_UAV_WRITES and BIND_READ_ONLY_DEPTH/STENCIL:
read-only depth comes from the view's own flags, as today. RenderPassesTier stays TIER_0 (emulated).

## 5. Enhanced barriers

### 5.1 Barrier(NumBarrierGroups, pBarrierGroups)

One Barrier call is one DXMT barrier call, as one ResourceBarrier call is: the same join, write-ended resources
(`transitioned_`), B6 opacity (`barrier_opaque`) and E7 names (`barrier_names`). ResourceBarrier's per-barrier decision
moves into one helper both use, so both classify alike.

Access bits to the legacy state with the same meaning (`LegacyState`):

| Access | State |
|---|---|
| COMMON (0) | COMMON |
| VERTEX_BUFFER, CONSTANT_BUFFER | VERTEX_AND_CONSTANT_BUFFER |
| INDEX_BUFFER | INDEX_BUFFER |
| RENDER_TARGET | RENDER_TARGET |
| UNORDERED_ACCESS | UNORDERED_ACCESS |
| DEPTH_STENCIL_WRITE | DEPTH_WRITE |
| DEPTH_STENCIL_READ | DEPTH_READ |
| SHADER_RESOURCE | PIXEL_SHADER_RESOURCE + NON_PIXEL_SHADER_RESOURCE |
| STREAM_OUTPUT | STREAM_OUT |
| INDIRECT_ARGUMENT (= PREDICATION) | INDIRECT_ARGUMENT |
| COPY_DEST, COPY_SOURCE, RESOLVE_DEST, RESOLVE_SOURCE | the same |
| SHADING_RATE_SOURCE | SHADING_RATE_SOURCE |
| any other bit (acceleration structures, video, sampler feedback) | unknown: a join, opacity kOpaqueUnknown |

Each barrier:
- SyncBefore NONE or AccessBefore NO_ACCESS: nothing earlier to wait on; skipped.
- A texture or buffer barrier names its resource and is classified as a transition between the two states: read to
  read skipped; out of a write state encoders list (RENDER_TARGET, DEPTH_WRITE, COPY_DEST, RESOLVE_DEST) a
  write-ended resource; into or out of UNORDERED_ACCESS a join (kOpaqueUavState; UAV to UAV kOpaqueUavBarrier, a UAV
  barrier); into or out of COMMON a join (kOpaqueCommon); otherwise a join naming the resource. A resource with
  ALLOW_SIMULTANEOUS_ACCESS joins (kOpaqueSimultaneous), as in ResourceBarrier.
- A global barrier names no resource: AccessBefore read-only, skipped; only UNORDERED_ACCESS, a join
  (kOpaqueUavBarrier); otherwise a join (kOpaqueUnknown).
- Ignored: sync stages (DXMT orders whole encoders, at least as strict), layouts (Metal has none), the DISCARD flag,
  subresource ranges (per resource, as ResourceBarrier).

ResourceBarrier and Barrier mix freely in a list.

### 5.2 Device9 and Device10

- Device9: CreateCommandQueue1 is CreateCommandQueue (the creator ID ignored). CreateShaderCacheSession and
  ShaderCacheControl answer E_NOTIMPL (DXMT's shader cache isn't the application's to control).
- Device10:
  - CreateCommittedResource3 is CreateCommittedResource2, CreatePlacedResource2 is CreatePlacedResource1. The initial
    layout becomes the state ValidateResourceStates wants: GENERIC_READ on an upload heap, COPY_DEST on a readback
    heap, otherwise its legacy equivalent (UNDEFINED and COMMON: COMMON; RENDER_TARGET, DEPTH_STENCIL_WRITE/READ,
    SHADER_RESOURCE, UNORDERED_ACCESS, COPY_SOURCE/DEST, RESOLVE_SOURCE/DEST, GENERIC_READ as their states; the
    DIRECT_QUEUE_ and COMPUTE_QUEUE_ layouts as the layout they specialise; the video layouts COMMON).
  - CreateReservedResource2 answers E_NOTIMPL, as CreateReservedResource1 does today.
  - Castable formats: a WARN, ignored (RelaxedFormatCastingSupported stays FALSE). A protected session: E_NOTIMPL, as
    the Device4 creates.
- OPTIONS12: EnhancedBarriersSupported TRUE; the rest unchanged.

## 6. The rest of List3 to List7, and the forwarder

### 6.1 Logged no-ops in DXMT

Each WARNs (once per method if DXMT has a once helper, else per call as OMSetDepthBounds does):

| Methods | Why nothing is right |
|---|---|
| SetProtectedResourceSession | Null is the default; no session can exist (none can be created). |
| InitializeMetaCommand, ExecuteMetaCommand | EnumerateMetaCommands reports none. |
| BuildRaytracingAccelerationStructure, EmitRaytracingAccelerationStructurePostbuildInfo, CopyRaytracingAccelerationStructure, SetPipelineState1, DispatchRays | RaytracingTier NOT_SUPPORTED. |
| RSSetShadingRate, RSSetShadingRateImage | VariableShadingRateTier NOT_SUPPORTED: every draw is 1x1. |
| DispatchMesh | MeshShaderTier NOT_SUPPORTED. |

### 6.2 Forwarder rules

- `ID3D12GraphicsCommandList3::* refuse` to `…7::* refuse` and `ID3D12Device9::* refuse`, `ID3D12Device10::* refuse` go.
- The §6.1 methods and the shader-cache pair: `refuse` (the guest logs; nothing, or E_NOTIMPL).
- BeginRenderPass: `pRenderTargets in count=NumRenderTargets`, `pDepthStencil in one`, union rules on the beginning
  access (CLEAR) and ending access (RESOLVE) unions, `pSubresourceParameters in count=SubresourceCount`. The resolve
  parameters' resources travel as handles, as in any struct.
- Barrier: `pBarrierGroups in count=NumBarrierGroups`, a `union=Type` rule whose arms are `in count=NumBarriers`.
- Device9/10: generated (`pCastableFormats in count=NumCastableFormats`).
- Where gen.py can't express a nesting (a count outside the union it counts), a hand-written special in
  `guest/proxy.cpp` flattens it instead.
- 32-bit forwarded D3D12 (opt-in) keeps the generator's rule: a struct holding a union with pointer members is refused
  for 32-bit callers. The default 32-bit lane runs guest DXMT, which has it all.

## 7. Tests

RED first, in `eac-vm/d3d/tests/smoke.cpp`: the Mac loopback gate and d3d-smoke's four lanes (forward and guest, 64
and 32-bit; a row the 32-bit forward lane refuses by §6.2 expects that).

| Row | Checks |
|---|---|
| `list4` | CreateCommandList for List4, 5, 6 and 7 succeeds and QueryInterface answers each (today E_NOINTERFACE); the §6.1 methods can be called. |
| `renderpass` | A CLEAR into a 4x MSAA target ending in a RESOLVE reads back the clear colour. A SUSPENDING pass in one list (CLEAR red, ending in a RESOLVE) and a RESUMING pass in the next (CLEAR green, ending PRESERVE, then a resolve to a second texture): the first texture red, the second green (both passes' accesses happened). |
| `resolveregion` | A 2x2 rectangle resolved to (2,2): those pixels the source's colour, (0,0) still the destination's earlier colour. |
| `barrier` | OPTIONS12 reports TRUE. A compute UAV write, a buffer Barrier (UNORDERED_ACCESS to COPY_SOURCE), a copy to a readback buffer: the written values; again with a global barrier. CreateCommittedResource3 of an upload buffer with layout UNDEFINED succeeds. |

If DXMT has a unit-test target for its barrier bookkeeping, `LegacyState` and the shared classification get rows
there too (ordering races are hard to catch in a smoke run).

Gates: one lane per step; at the end the full gates (`eac-vm/tests/d3d-smoke.sh`, `make -C eac-vm/d3d test
dispatch-test`, the loopback gate). Then Steel Nomad's workload run directly in a test VM, and a 3DMark launch through
Steam (the maintainer allowed starting and stopping 3DMark).

## 8. Not in this work

- List1's IMPLEMENT_ME methods (SetSamplePositions, SetViewInstanceMask, AtomicCopyBufferUINT/64) and List2's
  WriteBufferImmediate: their tiers aren't reported, but IMPLEMENT_ME aborts a program that calls them anyway.
- Depth/stencil and MIN/MAX resolves (§4.1).
- Ray tracing, VRS, mesh shaders: their caps stay off.
- List8 and later, Device11 and later.
