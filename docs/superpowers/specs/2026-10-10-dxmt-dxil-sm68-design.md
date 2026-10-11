# MacNeutron — DXMT Fork: DXIL Shader Model 6.8

- **Date:** 2026-10-10
- **Status:** Approved by ruling. The maintainer pre-authorised implementation and waived the spec and plan review
  gates ("Start implementing missing shader model features as soon as the research return",
  `.superpowers/brainstorm-sm/rulings.md`).
- **Builds on:** `2026-09-29-macneutron-dxil-translator-design.md` (the translator, its failure model, `dxil-translate`,
  the behaviour tests) and DXMT patch 0039, which reports Shader Model 6.7 by default
  (`docs/testing/acceptance-arm64-release.md`, "Shader Model 6.7 by default").
- **Research:** `.superpowers/brainstorm-sm/research-result.json` (workflow wf_e27ecbcf-dde): the synthesis (matrix,
  tiers, infeasible, recommendation, corrections) and five explorers. Where they disagree, the synthesis's corrections
  win.
- **Rulings:** S-R1 to S-R8, `.superpowers/brainstorm-sm/rulings.md`.
- **Line references** are at DXMT tree `build/wine-arm64-src/dxmt` HEAD 0593068 (patch 0039) and repo HEAD c8dc702.
  Translator paths are under `src/airconv/dxil/`: L `dxil_lower.cpp`, R `dxil_lower_resources.cpp`, M
  `dxil_lower_math.cpp`, S `dxil_signature.cpp`, I `dxil_initialize.cpp`. D is `src/d3d12/d3d12_device.cpp`.
- **Scope:**
  - **In (Tier 0a + Tier 1, S-R1):** every Shader Model 6.1-6.8 operation and rule that a device reporting DXMT's caps
    must support in vertex, pixel, compute and geometry shaders, and every 6.8 piece Metal can do. Then report 6.8.
  - **Out, next spec (Tier 0b):** hull and domain shaders (ops 103-107), `BufferUpdateCounter` (70: append, consume and
    UAV counters), `EvalSnapped`/`EvalSampleIndex`/`EvalCentroid` (87-89), VS clip and cull distance outputs and PS
    inputs. They are Feature Level 11_0 debt under every shader model number, not 6.x items.
  - **Out, deferred (Tier 2):** Shader Model 6.9's feasible parts: `Native16BitShaderOpsSupported` TRUE, half
    `IsSpecialFloat` (8-11), DXIL native and long vectors (303, 304, 309-311).
  - **Out, infeasible on Metal (synthesis "infeasible"):**
    - Honest Shader Model 6.9. It makes `Int64ShaderOps` mandatory (`HLSL_ShaderModel6_9.md:23`), which then requires
      every 64-bit atomic on `RWByteAddressBuffer` (`HLSL_SM_6_6_Int64_and_Float_Atomics.md:484-495`). The Metal 32023
      headers have only `atomic_max_explicit`/`atomic_min_explicit` on `device ulong*`, with no return value
      (`metal_atomic:3670-3700`, `:3752`). Hand-written AIR for the rest was rejected at pipeline creation on an M5 Pro.
    - 64-bit groupshared atomics: no `threadgroup ulong` atomics in Metal. The translator refuses them by name (§6).
    - `TextureStoreSample` (225, writable MSAA): Metal MS textures are read-only, and `air.write_texture_2d_ms` lowers
      to an undefined AGX symbol.
    - `AdvancedTextureOps` (OPTIONS14): needs `RelaxedFormatCasting`, `CreateCommittedResource3` castable formats and
      `D3D12_SAMPLER_DESC2`; DXMT stops at `ID3D12Device8`. So `TextureGatherRaw` (223) and `SampleCmpLevel` (224)
      stay untranslated: translating them would be dead code while the cap is FALSE.
    - Work graphs (238-253): no device-side enqueue; a `critical_section` kernel failed pipeline creation.
    - DXR, RayQuery, SER, OMM: `RaytracingTier` stays NOT_SUPPORTED; macOS 27's Metal has no micromap API.
  - **Out, optional caps that stay off:** mesh and amplification shaders, sampler feedback, barycentrics and
    `AttributeAtVertex` (per-vertex values are Apple10 only), view instancing beyond view 0, doubles.

## 1. Goal

DXMT reports Shader Model 6.8 for Direct3D 12 and means it. A vertex, pixel, compute or geometry shader that uses a 6.0-6.8
operation DXMT's caps require translates and behaves as the D3D specs say.

**Done when** §8 passes:
- every new behaviour group matches its reference, in both `make dxmt-check` lanes;
- the SMITE 2 corpus translates with no new failures after every translator task (S-R5);
- the device answers `0x68`, OPTIONS21's two 6.8 flags TRUE and OPTIONS22;
- one maintainer SMITE 2 session on the final build shows nothing new in the log.

Plainly: nothing here changes SMITE 2. Its 11,101 captured shaders use none of the missing operations and only 32-bit
atomics (correction 11). The work makes the reported shader model honest and readies DXMT for other titles: Unreal
Engine 5 bindless, Nixxes ports, compute-shader sampling.

### Decisions

| Decision | Choice |
|---|---|
| Scope | Tier 0a + Tier 1 in one spec and one plan (S-R1) |
| OPTIONS9 64-bit atomic bits | Stay TRUE: a documented over-report for Unreal Engine 5's SM6 check (S-R2, §4) |
| Where the code goes | DXMT patches 0040-0054 on `macneutron`, one per task, each exported with a fresh-fetch proof (S-R3) |
| Translator key | Changes once for the release: no release or tag between the first and last task (S-R7) |
| Real-game check | Offline corpus translation before and after every translator task; one SMITE 2 session at the end (S-R5) |
| Reported shader model | `min(requested, 0x68)`, flipped last, after every feature task passes |
| 2D compute quads | Remap the thread IDs into 2×2 quads (§3.3), not lane shuffles or threadgroup memory |
| Dynamic resources | A heap is a descriptor table at the heap's base; two qwords after the root arguments (§3.4) |
| WaveSize out of range | The translator refuses the shader at `SM50Initialize`; D3D12 answers `E_FAIL` |
| New `E_NOTIMPL` canary | A 64-bit `InterlockedAdd64` on a `RWByteAddressBuffer`: infeasible on Metal, so it stays out |
| References for new tests | CPU references and self-references; D3DMetal only where an existing group already uses it |
| `CycleCounterLegacy` (109) | Not translated: DXC emits it from no HLSL intrinsic, so nothing can test it; it fails by name as today |
| Float atomics on typed `R32_FLOAT` UAVs | Implemented (§3.7): the translator declares such a UAV `uint` and reads an `R32Uint` view the runtime keeps in the descriptor's spare qword. No second over-report |
| 64-bit groupshared atomics | Refused by name at `SM50Initialize` (fail closed), not left to Metal's pipeline creation |

## 2. Evidence (research, 2026-10-10)

1. **The 6.7 claim is ahead of the translator** (synthesis tier 0a):
   - `IsHelperLane` (221) is in the op enum but has no case (L:490). `QuadVote` (222), `WaveMatch` (165),
     `WaveMultiPrefixOp` (166) and `WaveMultiPrefixBitCount` (167) aren't lowered. Each is required while `WaveOps` is
     TRUE (D:813; `HLSL_SM_6_7_QuadAny_QuadAll.md:250`).
   - `createHandleFromHeap` (218) is refused (L:62-63) and `heap.dxil` pins that as the `E_NOTIMPL` canary
     (`dxmt/check.sh:464`). Tier 3 with SM 6.6 makes it mandatory (`HLSL_SM_6_6_DynamicResources.md:190-192`), and
     `SetDescriptorHeaps` is a no-op (`d3d12_command_list.cpp:1364-1367`).
   - Compute derivatives are required on SM 6.6 devices (`HLSL_SM_6_6_Derivatives.md:228-230`). Metal rejects
     `air.dfdx` in kernels (`cd_dfdx.err`), and an implicit-LOD sample in a kernel reads LOD 0 (metal-map probe).
   - Float atomics on typed resources fail. Shader Model 6.6 makes float `InterlockedExchange` and
     `Interlocked{CompareStore,CompareExchange}FloatBitwise` mandatory, typed `R32_FLOAT` resources included
     (`HLSL_SM_6_6_Int64_and_Float_Atomics.md:122-124`, "Float Capabilities"). Research correction 12 ("they take the
     i32 path, no gap") holds for raw, structured and groupshared memory only. On `RWTexture2D<float>` or
     `RWBuffer<float>`, DXC emits `atomicBinOp.i32`/`atomicCompareExchange.i32` on a handle whose element type is F32;
     the UAV is declared float (I:119); `AIRBuilder::CreateAtomicRMW`/`CreateAtomicCmpXchg` return null for a
     non-integer texture (`nt/air_builder.cpp:110-113`, `:213-216`) and R:336-337 makes that `Unsupported`
     (`E_NOTIMPL`), under today's 6.7 claim too.
2. **Silent-wrong paths today:**
   - PS wave ops count helper lanes. D3D excludes them by default
     (`HLSL_SM_6_7_Wave_Ops_Include_Helper_Lanes.md:41-43`); Metal includes them (probe2: `simd_sum(1u)` = 4 for one
     covered pixel). Correction 6.
   - Non-constant texture offsets become 0 (R:277-280).
   - `[WaveSize]` is ignored: only entry tags 4 and 1 are read (`dxil_metadata.cpp:177-191`).
   - f16/i16 signature elements move raw half bits through a 32-bit register (S:35-44, L:117-138), so the rasterizer
     interpolates a denormal (correction 9).
   - 2D compute quads: Metal's kernel quads are four consecutive lanes; D3D's are 2×2 blocks
     (`Derivatives.md:112-137`).
3. **SMITE 2's corpus** (`~/dxil-smite2`, 11,101 `.dxil`: 3,343 cs, 6,292 ps, 1,462 vs, 4 gs; all 6.6 but 5): no op
   above, all 1,169 dx.op atomics i32, no f16/i16/i64 overloads, no helper-lane attribute. Its 10 compute `quadOp`
   shaders have 1D `numthreads` (64 or 256). Six pixel shaders call `waveReadLaneFirst`; helper masking changes them.
4. **Metal mechanisms, probed** (metal-map, `/private/tmp/claude-501/sm-probe`): `air.simd_is_helper_thread`,
   `air.quad_any`, `air.quad_all`, `air.simd_ballot.i64`, `air.simd_active_threads_mask.i64`,
   `air.sample_compare_depth_{2d,2d_array,cube,cube_array}_grad.f32`, `air.base_vertex`, `air.base_instance`,
   `air.get_num_samples_texture_2d_ms`. A runtime `<2 x i32>` offset is accepted; the sampler kept its low 4 bits
   sign-extended on Apple10. Simdgroup width is 32 (D:813-815).
   Atomics (`xcrun -sdk macosx metal -std=metal3.1 -S`, metal 32023, 2026-10-11, `/private/tmp/claude-501/sm-probe/fix`):
   texture atomics exist only on integer textures, `air.atomic_exchange_explicit_texture_2d.u.v4i32`,
   `air.atomic_compare_exchange_weak_explicit_texture_2d.u.v4i32` and `air.atomic_exchange_explicit_texture_buffer_1d.u.v4i32`;
   `texture2d<float, access::read_write>` has no `atomic_exchange` ("no member named"). A device `atomic_float`
   exchanges (`air.atomic.global.xchg.f32`); a `threadgroup atomic_float` has no exchange overload. Every D3D12 texture
   has `PixelFormatView` usage (`d3d12_texture.cpp:110`), so an `R32Uint` view of an `R32Float` texture is legal; buffer
   views in `R32Uint` get `ShaderAtomic` usage (`dxmt_buffer.cpp:141-143`).
5. **DXC v1.9.2607 facts** the translator reads:
   - `[WaveOpsIncludeHelperLanes]` is the entry function attribute `"waveops-include-helper-lanes"`
     (`DxilModule.cpp:89`; `DxilPreparePasses.cpp:191-199`), not an SFI0 bit (correction 5).
   - WaveSize: entry tag 11 (`kDxilWaveSizeTag`, one value, SM 6.6-6.7) and tag 23 (`kDxilRangedWaveSizeTag`,
     `min, max, preferred`, SM 6.8+; max 0 means the single size `min`) (`DxilMetadataHelper.h:310`, `:325`;
     `DxilMetadataHelper.cpp:1612-1622`, `:1894-1906`).
   - `MemoryTypeFlag`: UAV 1, GroupShared 2, NodeInput 4, NodeOutput 8. `BarrierSemanticFlag`: GroupSync 1,
     GroupScope 2, DeviceScope 4 (`DxilConstants.h:2284-2305`).
   - `WaveMultiPrefixOpKind`: Sum 0, And 1, Or 2, Xor 3, Product 4 (`:2179-2185`). `QuadVoteOpKind`: Any 0, All 1
     (`:2206-2209`).
   - `annotateHandle`'s `ResourceProperties` (`DxilResourceProperties.h`, fetched 2026-10-10): dword 0 bits 0-7
     `ResourceKind`, bits 8-11 `BaseAlignLog2`, bit 12 `IsUAV`, bit 13 `IsROV`, bit 14 `IsGloballyCoherent`, bit 15
     `SamplerCmpOrHasCounter`; dword 1 is `{CompType u8, CompCount u8, SampleCount u8}` for typed kinds,
     `StructStrideInBytes` for structured buffers, `CBufferSizeInBytes` for constant buffers. Class: kind CBuffer →
     CBuffer, Sampler → Sampler, else `IsUAV` ? UAV : SRV (`DxilResourceProperties.cpp:38-49`). `heap.dxil`'s
     `{4107, 0}` is kind 11 (RawBuffer) with `IsUAV`.
6. **Headers:** llvm-mingw's `d3d12.h` stops at `D3D_SHADER_MODEL_6_7` and OPTIONS18. DirectX-Headers (main):
   `D3D_SHADER_MODEL_6_8 = 0x68`, `D3D12_FEATURE_D3D12_OPTIONS22 = 65` with
   `{BOOL ShaderExecutionReorderingActuallyReorders; BOOL CreateByteOffsetViewsSupported; UINT Max1DDispatchSize;
   UINT Max1DDispatchMeshSize;}`, root flags `CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED = 0x400` and
   `SAMPLER_HEAP_DIRECTLY_INDEXED = 0x800`, `D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION = 65535`.
   `constexpr (D3D12_FEATURE)65` doesn't compile (the enum's range ends at 63).

## 3. Features

Argument numbers below are `getArgOperand` indices (0 is the opcode), from DXC's `hctdb.py` at v1.9.2607.

### 3.1 Single operations

| Op | Arguments → result | Required semantics | Metal mechanism |
|---|---|---|---|
| `IsHelperLane` 221 | → i1 | True for a helper lane; false outside pixel shaders | `air.simd_is_helper_thread()` in PS; `false` elsewhere |
| `QuadVote` 222 | (1 i1 cond, 2 i8 op) → i1 | op 0 Any, 1 All across the quad (`QuadAny_QuadAll.md`) | `air.quad_any(i1)` / `air.quad_all(i1)`, with `Simd()`'s attributes |
| `Msad` 50 | (1 ref, 2 src, 3 accum) → i32 | Masked sum of absolute byte differences | `dxmt.msad.i32`, linked by `linkMSAD` when `use_msad` is set |
| `Texture2DMSGetSamplePosition` 75 | (1 srv, 2 index) → {f32, f32} | D3D's standard positions for the texture's count | `Converter::GetSamplePos` (`dxmt.sample_pos.i32`) with `air.get_num_samples_texture_2d_ms` |
| `RenderTargetGetSamplePosition` 76 | (1 index) → {f32, f32} | The same for the render target (PS) | `GetSamplePos` with `air.get_num_samples` |
| `RenderTargetGetSampleCount` 77 | → i32 | The render target's sample count (PS) | `air.get_num_samples` |
| `ViewID` 138 | → i32 | The view index; `ViewInstancingTier` is NOT_SUPPORTED (D:805), so one view | constant 0 |

76 and 77 carry no handle, so they leave the `LowerResource` range (58-79) and stop failing with "resource handle through
phi/select" (L:37-38).

### 3.2 Wave operations and helper lanes

- **Helper lanes.** In a pixel shader whose entry function lacks `"waveops-include-helper-lanes"`, only non-helper lanes
  take part in wave operations (`Wave_Ops_Include_Helper_Lanes.md:41-43`). The translator masks with the ballot of
  `!air.simd_is_helper_thread()`:
  - the first participating lane is `ctz(mask)`: `WaveIsFirstLane` (110) is `lane == ctz(mask)`, `WaveReadLaneFirst`
    (118) reads that lane with `air.simd_shuffle`;
  - `WaveAnyTrue` (113) is `any(v && !helper)`, `WaveAllTrue` (114) `all(v || helper)`, `WaveActiveAllEqual` (115)
    compares with the first participating lane and is `all(equal || helper)`;
  - `WaveActiveBallot` (116), `WaveAllBitCount` (135) and `WavePrefixBitCount` (136) ballot `v && !helper`;
  - `WaveActiveOp`/`WavePrefixOp` (119/121) and `WaveActiveBit` (120) feed helpers the identity: sum, or, xor 0;
    product 1; and `~0`; min the type's largest value; max its smallest;
  - unchanged: `WaveGetLaneIndex`, `WaveGetLaneCount` (the wave's width), `WaveReadLaneAt` (an explicit lane), and
    every quad operation (D3D's quad operations include helpers).

  With the attribute, or outside pixel shaders, nothing is masked. Vertex, compute and geometry shaders have no
  helpers.
- **`WaveMatch` 165** (1 value) → `%dx.types.fouri32`: the lanes whose value equals this lane's, as
  `{lanes 0-31, 0, 0, 0}` (`WaveActiveBallot`'s shape). Mechanism: a loop over distinct values,
  `f = simd_broadcast_first(v)`, `m = ballot(v == f)`, lanes with `v == f` take `m` and leave. Floats compare with `==`.
- **`WaveMultiPrefixOp` 166** (1 value, 2-5 mask0-3, 6 i8 op, 7 i8 sign) → T: an exclusive prefix over the lanes below
  this one that are set in this lane's mask; ops Sum 0, And 1, Or 2, Xor 3, Product 4. Metal has no prefix and/or/xor,
  so a 32-step loop shuffles each source lane (`air.simd_shuffle`) and folds the ones in `mask & lanes-below`. Its
  ceiling is 32 shuffles per call; nobody uses it today (SMITE 2: 0).
- **`WaveMultiPrefixBitCount` 167** (1 i1, 2-5 masks) → i32: `popcount(ballot(v) & mask & lanes-below)`.
- **Helper lanes in 165-167** follow the masking rule above.
- **WaveSize.** Parse tag 11 (one value N: range [N, N]) and tag 23 (`min, max, preferred`; max 0: [min, min]). The
  device's only wave size is 32, so a shader whose range excludes 32 is refused naming its range; `preferred` is a
  hint (`HLSL_SM_6_6_WaveSize.md:29-31`: the runtime must refuse it; DXMT is the runtime).

### 3.3 Compute derivatives and quads

`HLSL_SM_6_6_Derivatives.md:48-52`: `numthreads` with X divisible by 4 and Y = Z = 1 uses 1D quads; X and Y both even uses
2D quads; anything else may not use derivatives (DXC refuses it).

- **1D quads** are four consecutive `SV_GroupIndex` values: lane 0 top left, 1 top right, 2 bottom left, 3 bottom right
  (`:76-98`). That is Metal's kernel quad, four consecutive lanes, so `air.quad_shuffle` addresses D3D's quad.
- **2D quads** are `(x, y), (x+1, y), (x, y+1), (x+1, y+1)` for even x and y of `SV_GroupThreadID` (`:112-137`), and the
  spec defines no relation between quads and waves. So when a compute shader's `numthreads` is 2D and it calls a
  derivative, implicit-LOD sample, `CalculateLOD` or quad operation (60, 61, 64, 81, 83-86, 122, 123, 222, 255), the
  translator gives Metal thread `t` (its flat index in the group) the D3D IDs below, and Metal's lane quads become
  D3D's 2×2 quads. `SV_GroupIndex` stays D3D's flattening of the new ID; `SV_DispatchThreadID` is
  `SV_GroupID * numthreads + SV_GroupThreadID`. 1D shaders and shaders without those operations keep today's IDs.
  ```
  slice = X * Y;  z = t / slice;  r = t % slice;  q = r >> 2;  l = r & 3
  x = 2 * (q % (X / 2)) + (l & 1);  y = 2 * (q / (X / 2)) + (l >> 1)
  ```
- **Derivatives in compute** (83-86), from Metal's quad lane `l = simd lane & 3` (`air.thread_index_in_simdgroup`, the
  wave ops' lane input, S:413; never D3D's flattened ID, which the 2D remap makes differ) and
  `s(v, i) = air.quad_shuffle(v, i)`:
  - coarse: `ddx = s(v, 1) - s(v, 0)`, `ddy = s(v, 2) - s(v, 0)`;
  - fine: `ddx = s(v, l | 1) - s(v, l & 2)`, `ddy = s(v, l | 2) - s(v, l & 1)`.
- **Implicit-LOD sampling in compute** (60, 61, 64, and 255 in §3.5) takes coarse gradients of its coordinates and goes
  through the gradient path: `SampleGrad`'s (patch 0037: gradients scaled by 2^(sampler bias)), with
  `SampleBias`'s bias folded into that scale; `SampleCmp` through the new comparison-gradient builder.
- **`CalculateLOD` in compute** (81): `air.calculate_unclamped_lod` has no derivatives in a kernel. Unclamped LOD is
  `0.5 * log2(max(|ddx · size|², |ddy · size|²))` with `size` mip 0's width and height; the clamped one is that value
  clamped to `[0, mip levels - 1]` of the view.
- Pixel shaders keep `air.dfdx`/`air.dfdy` and Metal's own LOD.

### 3.4 Dynamic resources (`createHandleFromHeap` 218)

`(1 i32 index, 2 i1 samplerHeap, 3 i1 nonUniform)`, always followed by `annotateHandle` (216) whose `ResourceProperties`
give the class, kind, element type and stride (§2 item 5).

- **Translator.** Each distinct `(class, properties)` pair a shader reaches through `annotateHandle(createHandleFromHeap)`
  becomes a resource range of its own: register space `kHeapSpace = ~0u` (no root parameter matches it), lower bound 0,
  unbounded, range ID from `kHeapRangeBase = 1 << 24`. The heap index is the register. The binding map points those
  ranges at their heap's root-argument field with descriptor offset 0, so a heap load is the existing table load,
  `base[index - 0 + 0]`, with the descriptor kind from the properties. Heap descriptors are volatile: they are read
  where used, and a non-uniform index is a per-lane load as for tables.
- **Root-argument layout (the contract between translator and runtime).** When the root signature's
  `Flags & (0x400 | 0x800)`, two qwords follow the last parameter's: first the bound CBV/SRV/UAV heap's GPU descriptor
  base, then the sampler heap's (0 when none is bound). The translator declares those two fields only in a shader that
  indexes a heap; the runtime always writes them when the flags are set. A heap-indexing shader whose root signature
  lacks the matching flag (0x400 for views, 0x800 for samplers) is refused, naming the flag.
- **Runtime.** `SetDescriptorHeaps` records both heaps' GPU bases; setting a root signature or the heaps writes them into
  the root-argument staging and marks the root arguments dirty. Residency needs nothing: every heap and resource is in
  the device's residency set.

### 3.5 Shader Model 6.8

- **`BarrierByMemoryType` 244** (1 MemoryTypeFlags, 2 SemanticFlags) and **`BarrierByMemoryHandle` 245** (1 handle, 2
  SemanticFlags; the handle is a UAV) become the op-80 lowering (L:464-489) with mode bits
  `GroupSync ? 1 : 0 | (UAV ? (DeviceScope ? 2 : 4) : 0) | (GroupShared ? 8 : 0)`; 245 counts as UAV memory. Node
  memory bits are ignored (no node shader reaches the translator). DXC emits 244/245 only for the new `Barrier()`
  builtin; the legacy barriers still emit op 80 (translator explorer, `HLOperationLower.cpp`).
- **`StartVertexLocation` 256 and `StartInstanceLocation` 257** (→ i32) are `resource_map.base_vertex_id` and
  `base_instance_id`: `[[base_vertex]]`/`[[base_instance]]` in a vertex function (`dxil_converter.cpp:356-386`), and
  `draw_arguments` fields 2/3 (non-indexed) or 3/4 (indexed) in a geometry pipeline's object function
  (`dxbc_converter_gs.cpp:771-775`). D3D: `StartVertexLocation` of `DrawInstanced` or `BaseVertexLocation` of
  `DrawIndexedInstanced` (negative allowed), and `StartInstanceLocation` (`HLSL_ShaderModel6_8.md:36`). Whether Metal's
  `[[base_vertex]]` equals `vertexStart` on a non-indexed draw is unverified; the tests decide it before the cap flips.
- **`SampleCmpBias` 255** (1 srv, 2 sampler, 3-6 coords, 7-9 offsets, 10 compare, 11 bias, 12 clamp): `sample_compare`
  with bias (bias + the sampler's LOD bias) and the min-LOD clamp, as `SampleBias`; pixel and compute shaders.
- **`SampleCmpGrad` 254** (1 srv, 2 sampler, 3-6 coords, 7-9 offsets, 10 compare, 11-13 ddx, 14-16 ddy, 17 clamp): a new
  builder emitting, as the probes declare them:
  - `air.sample_compare_depth_2d_grad.f32(depth_2d, sampler, i32, <2 x float> coord, float ref, <2 x float> ddx,
    <2 x float> ddy, float clamp, i1, <2 x i32> offset, i32)`;
  - `_2d_array_grad.f32` the same with an `i32` slice after the coordinates;
  - `_cube_grad.f32(…, <3 x float> coord, float ref, <3 x float> ddx, <3 x float> ddy, float clamp, i32)`: no offset;
  - `_cube_array_grad.f32` the cube form with a slice. D3D allows no CubeArray form (proposal 0014).

  Gradients are scaled by 2^(sampler bias), as `SampleGrad`.
- **`CalculateLOD` (81) with a comparison sampler** (proposal 0014): the same op on a comparison sampler; the same
  lowering.
- **WaveSizeRange** (tag 23): §3.2.
- 244-257 and 75-77 join the op enum (`dxil_types.hpp:47-69`); 254 and 255 mark their texture compared and sampled in
  `ScanUsage` (I:84-90), so it binds as a depth texture.

### 3.6 f16/i16 signature elements and programmable offsets

- **16-bit varyings.** Signature registers stay 32-bit. `loadInput` (4) of a `half` reads a float and `fptrunc`s it; of
  an `i16`, truncates. `storeOutput` (5) of a `half` `fpext`s it to a float; of an `i16`, sign-extends (`I16`) or
  zero-extends (`U16`), from the element's component type. This covers min precision (`min16float`, no
  `-enable-16bit-types`) and native 16-bit types, IA inputs and render-target outputs.
- **Programmable offsets** (SM 6.7). A non-constant offset of `TextureLoad` or the `Sample*` family passes through as a
  runtime vector, each component `(x << 28) >> 28`: D3D's low 4 bits, sign-extended ([-8, 7]). Constant offsets keep
  today's path. `AdvancedTextureOpsSupported` stays FALSE, so a conforming app doesn't send these; this replaces a
  silent 0 with the right answer (synthesis tier 0a item 8).
- **Gather offsets** (`TextureGather`/`GatherCmp`) could be non-constant before 6.7 (`gather4_po`), with D3D's 6-bit
  range [-32, 31]. They pass through unwrapped, as the DXBC path's `gather4_po` does
  (`nt/dxbc_converter_base.cpp:1906-1950`). Metal's gather keeps [-8, 7]: an offset outside it is a gap both paths
  share, recorded here and not fixed.

### 3.7 Shader Model 6.6 float atomics

`InterlockedExchange` on a float, `InterlockedCompareStoreFloatBitwise` and `InterlockedCompareExchangeFloatBitwise`
(`HLSL_SM_6_6_Int64_and_Float_Atomics.md:122-124`) on every resource that may hold them. DXC emits each as an i32
atomic on the float's bits.

- **Raw, structured, groupshared:** already the i32 paths (R:547-566 for buffers; LLVM `atomicrmw`/`cmpxchg` on
  addrspace(3), which the translator passes to Metal). Tested, not changed. If DXC turns out to emit a groupshared
  float exchange as a float `atomicrmw` (Metal has no threadgroup float exchange, §2 item 4), the translator bitcasts
  its pointer and value to i32 first.
- **Typed `R32_FLOAT` (`RWBuffer<float>`, `RWTexture*<float>`):** Metal's texture atomics take integer textures only
  (§2 item 4). So:
  - **Translator.** A typed UAV with element type F32 that an `AtomicBinOp` (78) or `AtomicCompareExchange` (79)
    reaches is declared `uint` (`UnorderedAccessViewInfo::atomic_view`): its handle is read from the descriptor's third
    qword instead of the first, and every access the shader makes to it goes through that view. Loads and stores of it
    bitcast between i32 and f32 (`Cast`, same width); the atomics take the existing integer path.
  - **Runtime.** A texture or texel-buffer UAV descriptor of format `R32_FLOAT` also carries, in its third qword (today
    `padding[0]` of `UAVTextureGPUStorage`/`UAVTexelBufferGPUStorage`, `d3d12_descriptor_heap.cpp:29-41`), the GPU
    resource ID of an `R32Uint` view of the same subresources. `CopyDescriptors` copies all 32 bytes, so heap copies
    keep it. Other formats leave it 0: D3D defines float atomics only on `R32_FLOAT`, so another format reads 0 and
    drops writes (a null texture), as undefined as on D3D12.
  - Root descriptors are buffers only, so no root-descriptor path changes.

## 4. Feature reporting

After the last feature task, with `SM6Caps()` (the default since 0039):

| Cap | Before | After |
|---|---|---|
| `HighestShaderModel` | `min(requested, 0x67)` | `min(requested, (D3D_SHADER_MODEL)0x68)` |
| OPTIONS21 `SampleCmpGradientAndBiasSupported` | FALSE | TRUE |
| OPTIONS21 `ExtendedCommandInfoSupported` | FALSE | TRUE, only if §5's start-location rows pass |
| OPTIONS21 `WorkGraphsTier`, `ExecuteIndirectTier` | 0, 10 | unchanged |
| OPTIONS22 (65) | `E_NOTIMPL` | `S_OK`: reordering FALSE, byte-offset views FALSE, `Max1DDispatchSize` and `Max1DDispatchMeshSize` 65535 |
| OPTIONS9 `AtomicInt64OnTypedResource`, `OnGroupShared` | TRUE | TRUE, documented (below) |
| OPTIONS14, `Native16BitShaderOpsSupported`, `Int64ShaderOps`, mesh, sampler feedback, ray tracing, barycentrics, view instancing | off | unchanged |

- The OPTIONS21 flags follow `SM6Caps()` like the other Shader Model 6 answers: `DXMT_D3D12_SM6=0` reports them FALSE
  with Shader Model 5.1. OPTIONS22 is answered either way.
- OPTIONS22 is matched with `(UINT)Feature == 65` before the switch, with a local struct and a `static_assert` on its
  16 bytes, as `d3d12_feature_data.hpp:23-48` does for OPTIONS19 and OPTIONS21.
- **OPTIONS9 (S-R2, Option A).** The two 64-bit atomic bits stay TRUE although Metal can't back them: Unreal Engine 5's
  SM6 check needs them (translator spec §2 item 6), SMITE 2 issues no 64-bit atomic, and a shader that issues one is
  refused by name: a 64-bit dx.op atomic at `SM50Compile` (`E_NOTIMPL`), a 64-bit groupshared `atomicrmw`/`cmpxchg` at
  `SM50Initialize` (`E_FAIL`, §6). FALSE would make Unreal Engine 5 refuse its Shader Model 6 path. This is a
  deliberate over-report, like feature level 12_1 (D:726), and the maintainer is told so in the acceptance record. A
  comment at the answer says the same.
- **Float atomics** have no cap bit: Shader Model 6.6 requires them, and §3.7 implements every case, typed `R32_FLOAT`
  included. So S-R2 stays the only atomic over-report.
- Not changed here, recorded for honesty (synthesis infeasible §9): feature level 12_1 without tiled resources tier 2 or
  conservative rasterization; `ROVsSupported` TRUE with ROVs unordered on D3D12; `FORMAT_SUPPORT` 0xffffffff.

## 5. Tests and tools (MacNeutron repo)

- **Behaviour groups** (`dxmt/tests/dxil`, `common.hlsli`, 64 threads, `d3d12_dxil_exec.exe`). New groups run only on our
  DXMT, in one invocation by name; the D3DMetal comparison loop (`check.sh:520`) and `all[]` keep their 13 groups.
  `dxmt/tests/expect.py <exec output> <group>` holds one CPU reference per group (or a self-reference) and prints
  `match`.

  | Group | Target | Covers | Reference |
  |---|---|---|---|
  | `helperquad` | cs_6_7 | 221 (false in CS), 222 | CPU |
  | `msad` | cs_6_6 | 50 (`msad4`) | CPU |
  | `wavematch` | cs_6_6 | 165, 166 (all five ops), 167 | CPU, 32-lane waves of consecutive threads |
  | `wavesize32`, `wavesize` | cs_6_6 `[WaveSize(32)]`, cs_6_8 `[WaveSize(16, 64, 32)]` | tags 11, 23 accepted | CPU: lane count 32 |
  | `offsets` | cs_6_7 | 66 and 62 with runtime offsets, wrapped; 73 with runtime offsets in [-8, 7] | CPU from the texture formula |
  | `deriv` | cs_6_6, `numthreads(64,1,1)` | 83-86, 60, 61, 64, 81 in CS | CPU (power-of-two gradients give exact LODs) |
  | `deriv2d`, `deriv2dwide`, `deriv2dodd` | cs_6_6, `(8,8,1)`, `(32,2,1)`, `(6,6,1)` | 2D quads: 83-86, 81, 122, 123 | CPU |
  | `heap`, `heap-swap` | cs_6_6 | 218 for SRV, UAV, CBV, typed, structured, non-uniform, sampler heap; a heap rebound between dispatches | self: heap path equals table path; the heap-written typed UAV also against the CPU |
  | `barrier68` | cs_6_8 | 244, 245 | self: equals `barriers` word for word |
  | `cmp68` | cs_6_8 | 254, 255, 81 with a comparison sampler, 64/65 | CPU (point comparison: 0 or 1) |
  | `floatatomics` | cs_6_6 | float `InterlockedExchange`, `InterlockedCompareStoreFloatBitwise`, `InterlockedCompareExchangeFloatBitwise` on `RWByteAddressBuffer`, `RWStructuredBuffer<float>`, groupshared `float`, an `R32_FLOAT` `RWBuffer<float>` and an `R32_FLOAT` `RWTexture2D<float>` | CPU |

- **Harness changes:** `common.hlsli` and `d3d12_dxil_exec.cpp` gain `Texture2D<float> Depth : register(t6)` (an 8×8
  `R32_TYPELESS` depth resource, 4 mips, mip m all `0.125 + 0.25 m`, SRV `R32_FLOAT`) and a static
  `SamplerComparisonState Cmp : register(s2)` (point comparison, `LESS_EQUAL`, clamp, `MaxLOD` `D3D12_FLOAT32_MAX`). The
  heap groups use the same root signature with `Flags` 0x400 | 0x800 and a 3-DWORD root-constants parameter after the
  table (so the heap qwords follow a parameter rounded up to whole qwords), a CBV of the constant buffer at heap slot 12,
  and a two-sampler shader-visible sampler heap (`[0]` linear clamp, `[1]` point clamp); they are created only when a
  heap group runs, so the D3DMetal run never sees them. `floatatomics` adds three UAVs in a third table range at heap
  slots 13-15: `RWStructuredBuffer<float> AtomicSF : register(u5)` (a stride-4 view of `AtomicS`'s buffer),
  `RWBuffer<float> RWTypedF : register(u6)` (an `R32_FLOAT` view of `RWTyped`'s buffer) and
  `RWTexture2D<float> AtomicTexF : register(u7)` (a new 8×8 `R32_FLOAT` texture), all zeroed before each group.
- **Render tests** (`dxmt/tests/shaders`, `d3d12_triangle.exe`, `d3d12_indirect.exe`):
  - `helper.hlsl`: a default pixel shader that paints green only if helpers are excluded, a `[WaveOpsIncludeHelperLanes]`
    one (ps_6_7) that paints green only if they're included, and an always-green one. Each partial-quad pixel decides
    for itself, so the reference is the image hash of the always-green shader: no CPU image, and D3DMetal (which
    includes helpers) is not a reference.
  - `sm6x.hlsl`: a pixel shader that adds `SV_ViewID`, `GetRenderTargetSampleCount() - 1` and
    `GetRenderTargetSamplePosition(0)` to `triangle2`'s colour (all zero on a one-sample target): same pixels as
    `triangle2`. Op 75 is translated offline only (`dxil-translate` creates its pipeline).
  - `varying16.hlsl`: one triangle three ways: float, `min16float`/`min16uint` varyings, and `float16_t`/`uint16_t`
    with `-enable-16bit-types` (IA input, varyings and render-target output 16-bit). Both 16-bit forms draw the float
    form's pixels within 1/255.
  - `indirect.hlsl`'s `vsvid` (vs_6_6, tags from `SV_VertexID`) is compared with D3DMetal under direct and indirect
    draws with `StartVertexLocation` ≠ 0 and `BaseVertexLocation` < 0; `vsstart` (vs_6_8) paints
    `1000 + 100 * (SV_StartVertexLocation + 10) + SV_StartInstanceLocation`, against exact expected cells. `triangle2`'s
    `vsstart` draws with `--start 5 3` in the plain and geometry pipelines and must draw `triangle2`'s pixels.
  - `triangle2.psheap` (ps_6_6) is `triangle2`'s pixel shader reading `Tex` through `ResourceDescriptorHeap` and its
    sampler through `SamplerDescriptorHeap`; `d3d12_triangle.exe --heap` gives it a root signature with flags 0xC00 and
    shader-visible view and sampler heaps. It must draw `triangle2`'s pixels: the heap bases reach the graphics root
    arguments, not only the compute ones.
- **Refused shaders** move to `dxmt/tests/dxil/reject/`: `wavesize64` (cs_6_6 `[WaveSize(64)]`), `wavesizerange`
  (cs_6_8 `[WaveSize(64, 128)]`), `gsatomic64` (cs_6_6, `InterlockedAdd` on a groupshared `uint64_t`) and the new canary
  `atomic64` (cs_6_6, `RWByteAddressBuffer.InterlockedAdd64`). `dxil-translate` refuses all four naming why;
  `d3d12_dxil.exe`'s fourth shader becomes `atomic64.dxil` (label `canary`), still `0x80004001` with
  `DXIL: dx.op.atomicBinOp.i64` named in the log. `heap.dxil` becomes a behaviour group and `dxil-translate` accepts
  every shader in `dxmt/tests/dxil`. Heap indexing without root flag 0x400, and with 0x400 but without 0x800, is refused
  with each missing flag's name in the log.
- **Corpus (S-R5).** `dxil-translate` appends ` air=<16 hex>` (FNV-1a 64 of each translated metallib, in translation
  order) to its plain `ok` line; the `--flags` line, which `check.sh` matches to its end, is unchanged. Before and after
  every translator task, `build/wine-arm64/dxil-translate` runs over `~/dxil-smite2` and over `dxmt/tests/dxbc` (the
  D3D11 converter shares `nt/` and the `dxbc_*` binding code) into `build/sm68/corpus/` (never committed), and
  `dxmt/tools/corpus-diff.py` reports new failures, fixed, changed and unchanged shaders. Each task allows 0 new failures
  and explains every changed one. Expected: only the helper-lane task changes outputs, the six pixel shaders with
  `waveReadLaneFirst`; DXBC never changes.
- **Compilation.** `dxmt/tests/shaders/compile.sh` (pinned DXC 1.9.2607 under the frozen Rosetta reference's Wine;
  S-R4). A task commits only its new or rewritten `.dxil` files.
- **The SMITE 2 session** (S-R5): one maintainer session on the final build, §8.

## 6. Errors

| Condition | Behaviour |
|---|---|
| An op this spec leaves out (Tier 0b, 223-225, 238-253, 109, 64-bit dx.op atomics, …) | `SM50Compile` fails naming it; D3D12 logs it and returns `E_NOTIMPL` (as today) |
| A 64-bit groupshared atomic (`atomicrmw`/`cmpxchg` i64 on addrspace(3); no dx.op, so no path above sees it) | `SM50Initialize` fails: `DXIL: 64-bit groupshared atomic (<instruction>) not supported`; `E_FAIL`. Before the plan's Task 9 it reached Metal: `newComputePipelineState` failed with Metal's text and `E_FAIL` (`d3d12_pipeline_compute.cpp:89-92`), the op unnamed (unverified, research) |
| A float atomic on a typed UAV whose format isn't `R32_FLOAT` | Undefined, as on D3D12: the descriptor has no `R32Uint` view, so it acts as a null UAV (reads 0, writes dropped) |
| `[WaveSize]` range without 32 | `SM50Initialize` fails: `DXIL: [WaveSize(<min>, <max>)] excludes this device's 32-lane waves`; `E_FAIL` |
| Heap indexing without root flag 0x400 or 0x800 | `SM50Compile` fails naming the missing flag; `E_NOTIMPL` |
| Heap indexing with no root signature | `SM50Compile` fails: `DXIL: ResourceDescriptorHeap needs a root signature`; `E_NOTIMPL` |
| An out-of-range heap index | Undefined, as on D3D12: it reads whatever descriptor lies there |
| `SV_Start*` in a stage without them | Only vertex shaders may use them (DXC validates); otherwise `Unsupported` |

## 7. Build order

One task per patch; the reported shader model flips last. The plan's order follows the synthesis's, with its last
"tests" item spread into each task:

1. Tooling and the baseline (`air=` hashes, `corpus-diff.py`, `expect.py`, the acceptance section), then the enum and the
   single ops of §3.1 (0040).
2. Helper-lane masking (0041). 3. `WaveMatch`/`WaveMultiPrefix*` (0042). 4. WaveSize (0043). 5. 16-bit varyings (0044).
   6. Programmable offsets (0045).
7. Compute derivatives, 1D, the comparison-gradient builder and the depth test resources (0046). 8. 2D quads (0047).
9. Dynamic resources in the translator, the canary move, groupshared 64-bit atomics refused (0048). 10. Dynamic
   resources in the runtime (0049).
11. `Barrier()` (0050). 12. `SV_Start*` (0051). 13. `SampleCmpBias`/`Grad`, comparison `CalculateLOD` (0052).
14. Float atomics on typed `R32_FLOAT` UAVs, translator and runtime (0053).
15. Caps: `0x68`, OPTIONS21, OPTIONS22, the OPTIONS9 comment (0054).
16. README, the acceptance record, the SMITE 2 session request.

## 8. Acceptance on the maintainer's Mac

Recorded in `docs/testing/acceptance-arm64-release.md`, section "DXIL Shader Model 6.8 (DXMT 0040-0054)":

1. `make dxmt-check`: `dxmt-check: all passed` in both lanes, with every row of §5.
2. Per translator task: the corpus diff (SMITE 2 and `dxmt/tests/dxbc`), 0 new failures, every changed shader
   explained.
3. Per DXMT task: the export shows only its patch, and a fresh fetch applies N/N, `tree-equal`.
4. `make test` passes.
5. `d3d12_clear` reads `shader model 0x68` for a 0x69 request, `0x67` for a 0x67 request, and OPTIONS21/OPTIONS22's
   answers.
6. The translator key, old (`d6863ecc…`) and new.
7. **SMITE 2**, one session on a development build of the final tree (the maintainer launches it): the launcher rebuilds
   the recorded pipelines once; no `not supported` line and no new error in the log; the game plays as before, with
   attention to whatever uses the six changed pixel shaders.
8. One line for the maintainer on what rests on offline translation or has no row: op 109 dropped (no HLSL intrinsic
   emits it); op 75 and the pixel-shader `SampleCmpBias`/`Grad` lowering checked offline only, so OPTIONS21's
   comparison flag rests on compute behaviour; a WaveSize refusal answers `E_FAIL` where Microsoft's runtime likely
   answers `E_INVALIDARG`; no row for `WaveMatch`/`WaveMultiPrefix*` masking in pixel shaders, `QuadVote` in 2D compute,
   or the indexed geometry-shader start-vertex path.

## 9. Release consequences

- The translator key changes (any byte in `src/airconv`), so every player's shader cache is rebuilt once and the
  launcher replays recorded pipelines before the game starts (6-46 s for SMITE 2,
  `docs/testing/acceptance-dxmt-pipeline-cache.md:38`). S-R7: one key change for this whole spec, so no release or tag
  between its first and last task.
- The next release's notes (`build/release-notes-<version>.md`, Graphics) say: DXMT reports Shader Model 6.8; dynamic
  resources, compute-shader derivatives, float atomics and the 6.5-6.8 wave and quad operations are translated; each
  game's shaders are rebuilt once at its next start.
- README's Graphics section and its `DXMT_D3D12_SM6=0` row name 6.8 and the new not-translated list.

## 10. Risks

- **Helper-lane masking changes six SMITE 2 pixel shaders** (`waveReadLaneFirst`): the first lane is now a covered one.
  The D3D default makes that correct, but helper inclusion was measured on one M5 Pro. The SMITE 2 session watches for
  it.
- **2D remap** changes which hardware thread runs which D3D thread for those shaders: memory access order and
  coalescing may cost time; results don't change.
- **`[[base_vertex]]` on non-indexed draws** may not equal `vertexStart`. The start-location rows decide it; if a
  non-indexed row reads 0, `ExtendedCommandInfoSupported` stays FALSE and the finding goes to the maintainer.
- **Comparison-gradient sampling in a kernel** compiled in the probes for colour textures (`cd_grad`), not for depth
  compare; `deriv` and `cmp68` are the first runs.
- **Typed float atomics through an `R32Uint` view** are compile-probed only (§2 item 4); `floatatomics` is their first
  run. D3D12 textures get no `ShaderAtomic` usage (`d3d12_texture.cpp:110-125`), yet `R32_UINT` texture atomics pass
  today (the `atomics` group, against D3DMetal); if the typed-texture case fails where they pass, that usage is the
  first suspect. Every `R32_FLOAT` UAV descriptor creates one extra view, used or not.
- **Heap limits:** the sampler heap may hold 2048 samplers; argument buffers on Apple7/8 allow 996 (metal-map). DXMT's
  sampler heap is a plain buffer of sampler IDs, so it is untested past 996.
- **D3DMetal can't be the reference** for 222, 254-257 (its converter has no such strings), so those groups rest on
  CPU references and self-references.
- **DXC emits 244/245 only for `Barrier()`**; the research read a third of `HLOperationLower.cpp`. `barrier68` compiles
  it explicitly.
- **WaveMultiPrefix** costs 32 shuffles a call. Fine while no shader uses it; a partitioned prefix is the upgrade.
- **The key change** costs every player one rebuild (§9).
