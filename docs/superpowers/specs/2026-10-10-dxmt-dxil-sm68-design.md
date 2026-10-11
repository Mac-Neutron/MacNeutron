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
- **Rulings:** S-R1 to S-R8, `.superpowers/brainstorm-sm/rulings.md`. R-A64 (2026-10-11, the ledger
  `.superpowers/sdd/2026-10-10-dxmt-dxil-sm68/progress.md`; the maintainer: "Can we also fix the 64-bit atomic instead
  of disabling it ?") replaces S-R2: 64-bit atomics are implemented (§3.8), from the spike
  `.superpowers/brainstorm-sm/i64-spike-result.json` (workflow wf_0544f378-89a: GPU probes, an integration survey and a
  design).
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
      every 64-bit atomic on `RWByteAddressBuffer` (`HLSL_SM_6_6_Int64_and_Float_Atomics.md:484-495`). That 64-bit-atomic
      link is closed by §3.8's lock path (R-A64). `Int64ShaderOps` still stays FALSE until an i64 audit, the next spec:
      the 64-bit `cbufferLoad` is refused (R:517) and the i64 wave overloads are untested on Metal. So 6.9 stays out.
    - ~~64-bit groupshared atomics: no `threadgroup ulong` atomics in Metal.~~ Superseded by §3.8 (R-A64): true of Metal's
      own atomics (no `threadgroup ulong` atomics; the LLVM i64 forms crash its compiler service), not of a lock path
      built from 32-bit atomics. The translator refuses them by name only until the plan's Task 14c.
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
- the device answers `0x68`, OPTIONS21's two 6.8 flags TRUE and OPTIONS22, and OPTIONS9 and OPTIONS11's 64-bit
  atomic bits TRUE with every 64-bit atomic implemented (§3.8);
- one maintainer SMITE 2 session on the final build shows nothing new in the log.

Plainly: nothing here changes SMITE 2. Its 11,101 captured shaders use none of the missing operations and only 32-bit
atomics (correction 11). The work makes the reported shader model honest and readies DXMT for other titles: Unreal
Engine 5 bindless, Nixxes ports, compute-shader sampling.

### Decisions

| Decision | Choice |
|---|---|
| Scope | Tier 0a + Tier 1 in one spec and one plan (S-R1) |
| OPTIONS9 64-bit atomic bits | TRUE and implemented: every 64-bit atomic goes through §3.8's lock path (R-A64, §4) |
| OPTIONS11 `AtomicInt64OnDescriptorHeapResourceSupported` | TRUE (`SM6Caps()`), once the heap-indexed 64-bit row passes (§4) |
| `Int64ShaderOps` | Stays FALSE until an i64 audit, the next spec (§4) |
| 64-bit atomics on Metal | One lock path built from 32-bit atomics, fences and SIMD-group votes, the same on M1 and M2+; Metal's ulong atomics are never emitted (§3.8) |
| Where the code goes | DXMT patches 0040-0057 on `macneutron`, one per task, each exported with a fresh-fetch proof (S-R3) |
| Translator key | Changes once for the release: no release or tag between the first and last task (S-R7) |
| Real-game check | Offline corpus translation before and after every translator task; one SMITE 2 session at the end (S-R5) |
| Reported shader model | `min(requested, 0x68)`, flipped last, after every feature task passes |
| 2D compute quads | Remap the thread IDs into 2×2 quads (§3.3), not lane shuffles or threadgroup memory |
| Dynamic resources | A heap is a descriptor table at the heap's base; two qwords after the root arguments (§3.4) |
| WaveSize out of range | The translator refuses the shader at `SM50Initialize`; D3D12 answers `E_FAIL` |
| New `E_NOTIMPL` canary | `TextureGatherRaw` (223) on a `Texture2D<uint>` (cs_6_7): it stays untranslated while `AdvancedTextureOps` is FALSE (Scope) |
| References for new tests | CPU references and self-references; D3DMetal only where an existing group already uses it |
| `CycleCounterLegacy` (109) | Not translated: DXC emits it from no HLSL intrinsic, so nothing can test it; it fails by name as today |
| Float atomics on typed `R32_FLOAT` UAVs | Implemented (§3.7): the translator declares such a UAV `uint` and reads an `R32Uint` view the runtime keeps in the descriptor's spare qword. No cap bit |
| 64-bit groupshared atomics | Implemented through a threadgroup lock array (§3.8). Until then (the plan's Tasks 9-14b) refused by name at `SM50Initialize`, not left to Metal, whose compiler service crashes on them |

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
7. **64-bit atomics** (the i64 spike, 2026-10-11, M5 Pro, Apple10, macOS 27.0.1, Metal toolchain 32023; every probe
   watchdogged; scratch work in `/private/tmp/claude-501/i64-spike/`, results in `results.jsonl`):
   - **Native.** The only 64-bit atomics that build a pipeline are unsigned max/min without a return value, on
     `device atomic_ulong*` and `RG32Uint` `texture2d<ulong, access::read_write>`. Every other `air.atomic.*.i64` form
     fails at pipeline creation ("unlowered function call"); the returning umax, and LLVM `atomicrmw add/umax i64` and
     `cmpxchg i64` on device memory and `atomicrmw add i64` on threadgroup memory, crash Metal's compiler service
     (`XPC_ERROR_CONNECTION_INTERRUPTED` after multiple retries, 4 of 4). M1 (Apple7) has no ulong atomics.
   - **Emulated.** A hashed table of 32-bit spinlocks gives every op correct return values on buffers, threadgroup memory
     and `RG32Uint` textures, with seq_cst fences on acquire and release (without them 397 to 1,600 of 1,600 dispatches
     were bad), `mem_texture` in the texture fences (device-only fences: 160 of 1,600 bad, all in the test that re-reads a
     texel) and a loop shape in which no lane spins while a lane of its own simdgroup holds a lock: the naive loop
     deadlocked (31 of 32 lanes capped). The hybrid (§3.8) had 0 bad in about 40,000 checked dispatches.
   - **Mixing.** Native 32-bit atomics on a half of an emulated value lost 25,287 to 182,053 updates; native 64-bit umax
     on emulated addresses left 356 and 1,688 addresses below their max.
   - **SMITE 2** issues none: 0 of 11,101 files set an SFI0 64-bit bit (Int64Ops 0x8000, typed 0x400000, groupshared
     0x800000, heap 0x10000000).

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
    drops writes (a null texture), as undefined as on D3D12. The texel-buffer descriptor's fourth qword (`padding[1]`)
    is reserved for §3.8's typed 64-bit atomics.
  - Root descriptors are buffers only, so no root-descriptor path changes.

### 3.8 Shader Model 6.6 64-bit atomics (R-A64)

Ops 78 and 79 with the i64 overload on raw, structured, root-descriptor, heap-indexed and typed buffers and on
`R32G32_UINT` textures, and LLVM `atomicrmw`/`cmpxchg` on i64 addrspace(3) pointers (groupshared: no dx.op). All ten
binary ops (Add, And, Or, Xor, IMin, IMax, UMin, UMax, Exchange; DXC's order, R:229-234), CompareExchange and
CompareStore. Evidence §2 item 7. Line references here are at DXMT b1c67c0 (0042), where the spike read them.

- **Not Metal's atomics.** Native ulong atomics are never emitted: they fail or crash the compiler service on Apple10
  except a void umax/umin, M1 has none, and native umax mixed with the lock path on one address loses updates while the
  translator can't see other shaders. So there is no Apple8 gate and no device-init pipeline probe.
- **One uniform lock path** built from 32-bit atomics, fences and SIMD-group votes, the same on M1 and M2+.
  - **Device lock table** (buffers and textures): one per-device buffer of `kAtomic64Locks = 1 << 16` u32 locks (256
    KiB, shared storage, zeroed, in the device's residency set like `null_buffer_`, D:1855-1862). Its GPU address
    travels in one more trailing root-argument qword: after §3.4's two heap qwords when the root signature has flags
    0x400 or 0x800, otherwise right after the last parameter. The runtime always writes it, in
    `SetComputeRootSignature` and `SetGraphicsRootSignature`, into the matching staging array; the translator declares
    the field only in a shader with a 64-bit dx.op atomic (after the two heap fields when the flags are set, declared
    then even without heap indexing), so every other shader's output is unchanged. One table per device covers every
    pipeline, command buffer and queue, so atomicity holds across queues.
  - **Groupshared lock array:** a translator-added `[N x i32] addrspace(3)` global. N = 256 (1 KiB); if 32 KiB minus the
    shader's groupshared bytes is smaller, the largest power of two that fits; if not one lock fits, the shader is
    refused by name. Zeroed at function entry (thread i zeroes slots i, i + groupsize, …), then one threadgroup barrier
    before any user code; the entry block is uniform, so the barrier is legal.
  - **Keys** (32-bit math only; the same bytes always map to the same lock, and a collision only serialises):
    - raw, structured, root-descriptor and heap-indexed buffers: `a = trunc32(ptrtoint(element pointer)); h = a >> 3;
      h ^= h >> 16; lock = h & 0xFFFF`;
    - typed buffers (`RWBuffer<uint64_t>` on `R32G32_UINT`; DXIL has no 64-bit typed load or store): routed through the
      raw path, not `LowerTexture`. The texel-buffer UAV descriptor's fourth qword (`padding[1]`,
      `d3d12_descriptor_heap.cpp:29-41`) carries the buffer's GPU base; the address is
      `base + 8 * (index + DecodeTextureBufferOffset(md))`, out of bounds when `index >= DecodeTextureBufferElement(md)`.
      So a raw and a typed view of the same bytes share a lock, with no texture fence and no `RG32Uint` texture-buffer
      read_write access (Apple lists only Read and Write for it);
    - textures (`RWTexture1D/2D/2DArray/3D<uint64_t>` on `R32G32_UINT`, i.e. `RG32Uint`): a hash of `(x, y)` only,
      ignoring slice, z, mip and resource. Every UAV view of a texel agrees on `(x, y)`; view-relative slices or view
      objects would break atomicity across views, and ignoring them only adds collisions;
    - groupshared: `(ptrtoint i32 >> 3) & (N - 1)` (an addrspace(3) `ptrtoint` is 0x10000 + the byte offset).
  - **Loop shape** (the probe's hybrid, with a lane-index-free election):
    - guards: a pixel shader's helper lanes skip the whole path and return 0 (D3D gives helper lanes no UAV side
      effects, and a helper whose lock atomic were discarded could spin forever); an out-of-bounds buffer element skips
      it too and returns 0 (`ElementPointer` returns null, R:43-53, so the path never hashes or dereferences null);
    - phase 1: every lane does ONE `cmpxchg i32 lock, 0, 1`; winners run the critical section and release inside that
      `if`, with no loop;
    - phase 2: `pending = !won`;
      ```
      loop: if (!simd_any(pending)) exit;
            if (pending && simd_is_first()) { spin cmpxchg until acquired; critical section; release; pending = false; }
      ```
      `air.simd_is_first` (WaveMatch emits it, L:513-517), evaluated only by pending lanes (a branch, not an `and`: it
      names the first active lane), instead of the probe's `lane == ctz(ballot)`, because Metal rejects
      `[[thread_index_in_simdgroup]]` in vertex functions while `simd_ballot` and `simd_is_first` compile there;
    - no spin cap: a 50M-spin cap still dropped 9 operations in a legitimate one-hot-address storm, so a cap would
      silently corrupt results.
  - **Critical section**, from forms DXMT already ships or the spike proved: acquire is a relaxed `cmpxchg i32`, then
    `air.atomic.fence(flags, 5 = seq_cst, scope)` (DXMT emits that fence at Metal 3.1 for D3D12, L:565-584). Buffer data
    is two relaxed 32-bit atomic halves (D3D12 compiles at Metal 3.1, `d3d12_pipeline_compute.cpp:63`, so MSL 3.2's
    `coherent(device)` and 4.1's acquire/release are unavailable); texture data the texel through the existing
    `<4 x i32>` texel type (lo `.x`, hi `.y`) and `air.fence_texture` after the write; groupshared data plain threadgroup
    i64. `new = OP(old, v)` in i64 ALU; CompareExchange stores only if `old == compare` (64-bit), CompareStore likewise
    with the result unused. Release: the fence, then an atomic store of 0 to the lock. The atomic `load`/`store` i32 form
    is checked in DXMT's AIR 2.6 module first (the plan's Task 14a, Step 1); its fallback is DXMT's 5-argument
    `air.atomic.global.xchg.i32` for stores and `or 0` for loads.
  - **Fence flags** `(flags, order, scope)`: buffers `(1, 5, 2)`; textures device|texture `(5, 5, 2)`; groupshared
    `(2, 5, 1)`.
  - **Never emitted:** an i64 `atomicrmw` or `cmpxchg` instruction, any `air.atomic.*.i64` call, the naive spin loop, the
    per-lane done-flag loop (correct only while the compiler keeps the critical section inside the loop), or a
    test-and-test-and-set spin (one dispatch ran 147.8 s and the macOS GPU watchdog never fired).
  - **Stale locks:** the runtime re-zeroes the lock table whenever a command buffer completes with an error (the queue's
    retire path, `d3d12_command_queue.cpp:1182-1183`): a fault inside a critical section would otherwise leave its lock
    held and hang every later operation on it. Re-zeroing can break one critical section still running on another
    queue, accepted over a permanent hang.
- **Correctness argument.**
  - **Atomicity.** Every 64-bit atomic on given bytes derives the same lock from the absolute location (a buffer
    element's device address, whichever view; a texel's `(x, y)`; the group-memory offset), and one table per device
    covers every pipeline, command buffer and queue. So critical sections on one location exclude each other, and the
    operations are linearizable; collisions only serialise.
  - **Return values.** The value read under the lock is the exact old value for every op; CompareExchange returns it
    and writes only on a 64-bit match. It is never torn: each half is a relaxed 32-bit atomic load or store inside the
    lock (the probe's no-lock control was torn, e.g. `53972300ffffffff`, 400 of 400 bad). The checks: the linearizability
    multiset `{init} + {new_i} == {old_i} + {final}`, the final value, and a CAS-counter permutation.
  - **Memory ordering.** The release side's seq_cst fence before the relaxed unlock store and the acquire side's
    seq_cst fence after the relaxed lock `cmpxchg` make each critical section happen-before the next on that lock. That
    is stronger than D3D's relaxed atomics, which is harmless. Plain loads see the final value after a barrier and in
    later dispatches (0 bad in 100 reps per mode).
  - **Forward progress inside a SIMD group.** Phase 1 has no loop: winners run a straight-line critical section and
    release before the `if` rejoins. In phase 2 the loop condition `simd_any(pending)` is uniform and exactly one lane
    spins per iteration, while no lane of its own simdgroup holds a lock (phase-1 holders released before the join, the
    previous phase-2 holder before the latch), so a spinner only waits on a lane in another simdgroup. Each lane holds
    at most one lock and a critical section has no barrier, no second lock and no wait, so neither lock-order nor
    barrier deadlock is possible. A holder has executed its `cmpxchg`, so it is resident, and AGX interleaves resident
    simdgroups: holders finish. The CAS lock is unfair, but a lane starves only for a bounded time, since the number of
    operations is finite. The shape doesn't depend on loop rotation.
- **Costs** (M5 Pro, GPU ns per operation amortized; uncontended / one address per simdgroup / all threads on one
  address):

  | Kind | Lock path | Native 32-bit | Slowdown |
  |---|---|---|---|
  | Buffers (atomic halves, seq_cst fences: the Metal 3.1 form) | 0.131 / 3.06 / 421 | 0.012-0.017 / 0.25 / 0.69 | ~8-11x / 12x / ~600x |
  | Typed buffers (the raw path) | 0.131 / 3.06 / 421 | 0.012-0.017 / 0.25 / 0.69 | ~8-11x / 12x / ~600x |
  | Textures (`RG32Uint`) | 0.161 / 1.87 / 628 | 0.021 / 0.30 / 0.71 | ~5-8x / 6x / ~900x |
  | Groupshared (uncontended / per simdgroup / 256 lanes on 1 address / 1,024 lanes) | 0.059 / 0.875 / 7.5 / 19.2 | 0.011 / 0.042 / 0.10 / 0.21 | 5x / 21x / 75x / 90x |

  Against native 64-bit max, buffers are about 7x slower uncontended. The cliff: thousands of threads on one address
  or lock take seconds per dispatch (65,536 threads into 1-16 locks: 1.4-2.5 s), and the macOS GPU watchdog doesn't
  bound it; a global 64-bit counter bumped by every thread is the bad case. Fixed costs: 256 KiB per device, one qword
  per root-argument upload, at most 1 KiB of threadgroup memory plus one entry barrier and N / groupsize stores per
  thread for groupshared, about 40 AIR instructions per atomic site. Shaders without 64-bit atomics cost nothing.
- **Not supported** (known non-conformance, documented):
  1. A 32-bit atomic on either half while a 64-bit atomic runs on the same 8 bytes is not atomic with respect to it (the
     probe lost 25,287 to 182,053 updates). D3D11.3 §7.14.2's 32-bit granularity arguably expects it to work; fixing it
     would route every i32 atomic through the lock (SMITE 2 has 1,169).
  2. A plain 32- or 64-bit store racing a 64-bit atomic on the same bytes can be overwritten by the read-modify-write
     (inferred from the same mechanism, not measured).
  3. A plain 64-bit load during an atomic can see one half updated; each half is old or new, which D3D's 32-bit
     granularity allows.
  4. Two `ID3D12Device`s sharing memory have separate lock tables.
  5. A shader whose groupshared leaves no room for one lock and that has 64-bit groupshared atomics is refused by name.
  6. Out of bounds: a buffer operation is skipped and returns 0; textures as today's 32-bit texture atomics, with no
     extra check.
- **Unmeasured:** M1 (Apple7): G13 scheduling, which forward progress rests on, and `RG32Uint` read_write texture access
  (outside Apple's documented tier-2 formats on every family; DXMT already relies on it through
  `TypedUAVLoadAdditionalFormats = TRUE`, D:771; typed buffers avoid it). The acceptance record doesn't claim M1 until
  the 64-bit groups run on one.

## 4. Feature reporting

After the last feature task, with `SM6Caps()` (the default since 0039):

| Cap | Before | After |
|---|---|---|
| `HighestShaderModel` | `min(requested, 0x67)` | `min(requested, (D3D_SHADER_MODEL)0x68)` |
| OPTIONS21 `SampleCmpGradientAndBiasSupported` | FALSE | TRUE |
| OPTIONS21 `ExtendedCommandInfoSupported` | FALSE | TRUE, only if §5's start-location rows pass |
| OPTIONS21 `WorkGraphsTier`, `ExecuteIndirectTier` | 0, 10 | unchanged |
| OPTIONS22 (65) | `E_NOTIMPL` | `S_OK`: reordering FALSE, byte-offset views FALSE, `Max1DDispatchSize` and `Max1DDispatchMeshSize` 65535 |
| OPTIONS9 `AtomicInt64OnTypedResource`, `OnGroupShared` | TRUE, but every 64-bit atomic refused | TRUE, implemented (§3.8's lock path) |
| OPTIONS11 `AtomicInt64OnDescriptorHeapResourceSupported` | FALSE (zero-filled, D:922-925) | TRUE |
| OPTIONS1 `Int64ShaderOps` | FALSE | FALSE, pending an i64 audit (below) |
| OPTIONS14, `Native16BitShaderOpsSupported`, mesh, sampler feedback, ray tracing, barycentrics, view instancing | off | unchanged |

- The OPTIONS21 flags follow `SM6Caps()` like the other Shader Model 6 answers: `DXMT_D3D12_SM6=0` reports them FALSE
  with Shader Model 5.1. OPTIONS22 is answered either way.
- OPTIONS22 is matched with `(UINT)Feature == 65` before the switch, with a local struct and a `static_assert` on its
  16 bytes, as `d3d12_feature_data.hpp:23-48` does for OPTIONS19 and OPTIONS21.
- **OPTIONS9 and OPTIONS11 (R-A64).** OPTIONS9's two bits keep `SM6Caps()` and are now honest: typed resources once the
  typed and texture task passes, groupshared once the groupshared task passes (§3.8). A comment at the answer says:
  "64-bit atomics: every kind through the translator's lock path (DXMT 0054-0056); Metal's ulong atomics are never
  used." OPTIONS11's `AtomicInt64OnDescriptorHeapResourceSupported` becomes `SM6Caps()` once the raw and structured
  task's heap-indexed row passes: heap handles resolve to the same pointers as table handles (§3.4). The answers are
  the same on M1 and M2+: the path uses only i32 atomics, `air.atomic.fence`, `simd_any` and `simd_is_first`. Unreal
  Engine 5's Shader Model 6 check, which needs the OPTIONS9 bits, is met without an over-report.
- **`Int64ShaderOps` stays FALSE.** Once TRUE, Shader Model 6.6 makes every 64-bit atomic on root-descriptor raw and
  structured buffers mandatory (`HLSL_SM_6_6_Int64_and_Float_Atomics.md:484-495`), which §3.8 now meets. But the flag
  also promises general i64 support, and two gaps remain: the translator refuses the 64-bit op 58 `cbufferLoad` (R:517),
  and the i64 wave overloads (`air.simd_*.i64`, from `SimdSuffix`, L:250-254 at 0593068) are untested on Metal. Flipping
  it needs an i64 audit, outside Shader Model 6.8: the next spec, and the remaining link to an honest 6.9. Until then
  OPTIONS9 TRUE beside `Int64ShaderOps` FALSE is an odd pair under the SM 6.6 spec's structure (the optional 64-bit
  atomics without the baseline flag), which that audit closes; DXMT checks no shader feature bits against these caps.
- **Float atomics** have no cap bit: Shader Model 6.6 requires them, and §3.7 implements every case, typed `R32_FLOAT`
  included. No atomic cap is over-reported.
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
  | `atomic64` | cs_6_6, under a root signature with flags 0xC00 and a root UAV | 78/79 i64 on raw and structured buffers: thread-owned locations for every op (CompareExchange and CompareStore, signed min/max, a carry across the halves); all 64 threads on one qword through raw, structured, root-UAV and heap views; a UMax with tied high halves; a CAS counter; out of bounds | CPU: replayed sequences, the linearizability multiset, finals, the counter's permutation |
  | `atomic64c` | cs_6_6, `Dispatch(64, 1, 1)` | 4,096 threads (128 simdgroups) on one address; 1,024 addresses four threads each from four groups; from §3.8's typed and texture task, a typed element and a texel likewise | CPU: each address's olds are `k * V` with distinct `k` (bitmaps); the storm's final |
  | `atomic64typed` | cs_6_6 | 78/79 i64 on an `R32G32_UINT` typed buffer and texture: every op thread-owned; a raw and a typed view of one qword; a Nanite-shaped max of `depth << 32 \| id`; a CAS counter that re-reads one texel; out of the view | CPU, as `atomic64` |
  | `gsatomic64`, `gsatomic64c` | cs_6_6, `(64,1,1)` and `(1024,1,1)` | i64 `atomicrmw`/`cmpxchg` on groupshared: every op thread-owned, a carry-crossing sum, tied UMax, CAS counter; 1,024 lanes on one address twice | CPU, as `atomic64` and `atomic64c` |

  The 64-bit groups run in an invocation of their own, last in their lane, so a deadlock fails their rows at the
  harness's 120 s watchdog instead of hanging the gate; the contended groups run last. Offline rows check each lock
  path's fences (`dxil-translate --flags`: `1:2` buffers, `5:2` textures, `2:1` groupshared) and the groupshared entry
  barrier.

- **Harness changes:** `common.hlsli` and `d3d12_dxil_exec.cpp` gain `Texture2D<float> Depth : register(t6)` (an 8×8
  `R32_TYPELESS` depth resource, 4 mips, mip m all `0.125 + 0.25 m`, SRV `R32_FLOAT`) and a static
  `SamplerComparisonState Cmp : register(s2)` (point comparison, `LESS_EQUAL`, clamp, `MaxLOD` `D3D12_FLOAT32_MAX`). The
  heap groups use the same root signature with `Flags` 0x400 | 0x800 and a 3-DWORD root-constants parameter after the
  table (so the heap qwords follow a parameter rounded up to whole qwords), a CBV of the constant buffer at heap slot 12,
  and a two-sampler shader-visible sampler heap (`[0]` linear clamp, `[1]` point clamp); they are created only when a
  heap group runs, so the D3DMetal run never sees them. `floatatomics` adds three UAVs in a third table range at heap
  slots 13-15: `RWStructuredBuffer<float> AtomicSF : register(u5)` (a stride-4 view of `AtomicS`'s buffer),
  `RWBuffer<float> RWTypedF : register(u6)` (an `R32_FLOAT` view of `RWTyped`'s buffer) and
  `RWTexture2D<float> AtomicTexF : register(u7)` (a new 8×8 `R32_FLOAT` texture), all zeroed before each group. The
  64-bit groups add, in the same range: `Raw64` (u8), `AtomicS64` (u9, `uint64_t`) and `AtomicS64S` (u10, `int64_t`),
  three views of a new 16 KiB buffer `a64`; `RWTyped64`/`RWTyped64S` (u11, u12: `R32G32_UINT` views of `a64`'s qwords
  128-191); `AtomicTex64`/`AtomicTex64S` (u13, u14: a new 8×8 `R32G32_UINT` texture), all zeroed before each group, at
  heap slots 16-22. `atomic64` runs under `root_a64`: the heap groups' root signature plus a root UAV (u20) on `a64`, so
  its lock qword follows the heap qwords; the other groups' plain root signature puts it right after the table.
  `atomic64c` dispatches 64 groups; every other group one.
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
  - `triangle2.vsatomic64` (vs_6_6) and `triangle2.psatomic64` (ps_6_6): `d3d12_triangle.exe --atomic64` binds a root
    UAV (no heap flags: the lock qword right after it). The pixel shader adds 1 per pixel with derivatives in play, and
    the sum must equal the drawn pixels (helper lanes left out); the vertex shader's 64-bit max must be the largest of
    its three vertices' values, whose high halves tie.
- **Refused shaders** move to `dxmt/tests/dxil/reject/`: `wavesize64` (cs_6_6 `[WaveSize(64)]`), `wavesizerange`
  (cs_6_8 `[WaveSize(8, 16)]`), `gsatomic64` (cs_6_6, `InterlockedAdd` on a groupshared `uint64_t`; refused until
  §3.8's groupshared task, which moves it to the behaviour groups and puts `gsatomic64full` in its place: 32 KiB of
  groupshared and a 64-bit groupshared atomic, no room for a lock) and the new canary `gatherraw` (cs_6_7,
  `Texture2D<uint>.GatherRaw`, op 223). `dxil-translate` refuses all four naming why; `d3d12_dxil.exe`'s fourth shader
  becomes `gatherraw.dxil` (label `canary`; its root signature gains a t0 table and a static s0, so only the op is
  refused), still `0x80004001` with `DXIL: dx.op.textureGatherRaw` named in the log. `heap.dxil` becomes a behaviour group and `dxil-translate` accepts
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
| An op this spec leaves out (Tier 0b, 223-225, 238-253, 109, …) | `SM50Compile` fails naming it; D3D12 logs it and returns `E_NOTIMPL` (as today) |
| A 64-bit groupshared atomic (`atomicrmw`/`cmpxchg` i64 on addrspace(3); no dx.op, so no path above sees it) | Translated (§3.8) from the plan's Task 14c. Between Tasks 9 and 14c, `SM50Initialize` fails: `DXIL: 64-bit groupshared atomic (<instruction>) not supported`; `E_FAIL`. Before Task 9 it reached Metal, whose compiler service crashes on it (measured: `XPC_ERROR_CONNECTION_INTERRUPTED` after multiple retries, slowly), the op unnamed |
| 64-bit groupshared atomics with no room for one lock in 32 KiB | `SM50Initialize` fails: `DXIL: 64-bit groupshared atomics need 4 bytes of threadgroup memory for a lock; the shader uses <bytes> of 32768`; `E_FAIL` |
| A 64-bit atomic on a buffer element out of bounds | Skipped; returns 0 (§3.8) |
| A 32-bit atomic or a plain store racing a 64-bit atomic on the same bytes | Not atomic with respect to it: known non-conformance (§3.8, "Not supported") |
| A command buffer completes with an error | The runtime re-zeroes the 64-bit lock table, so a lock held by a faulted critical section can't hang later work (§3.8) |
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
9. Dynamic resources in the translator, the canary move to `TextureGatherRaw`, groupshared 64-bit atomics refused
   until 14c (0048). 10. Dynamic resources in the runtime (0049).
11. `Barrier()` (0050). 12. `SV_Start*` (0051). 13. `SampleCmpBias`/`Grad`, comparison `CalculateLOD` (0052).
14. Float atomics on typed `R32_FLOAT` UAVs, translator and runtime (0053).
14a. 64-bit atomics on raw, structured, root and heap-indexed buffers: the per-device lock table, its trailing
   root-argument qword, the re-zero on error (0054). 14b. Typed buffers through the raw path and `R32G32_UINT` textures
   (0055). 14c. Groupshared: the threadgroup lock array (0056).
15. Caps: `0x68`, OPTIONS21, OPTIONS22, OPTIONS9 honest and its comment, OPTIONS11's heap bit, `Int64ShaderOps`' comment
   (0057).
16. README, the acceptance record, the SMITE 2 session request.

## 8. Acceptance on the maintainer's Mac

Recorded in `docs/testing/acceptance-arm64-release.md`, section "DXIL Shader Model 6.8 (DXMT 0040-0057)":

1. `make dxmt-check`: `dxmt-check: all passed` in both lanes, with every row of §5.
2. Per translator task: the corpus diff (SMITE 2 and `dxmt/tests/dxbc`), 0 new failures, every changed shader
   explained. The 64-bit atomic tasks predict 0 changed: SMITE 2 has only i32 atomics.
3. Per DXMT task: the export shows only its patch, and a fresh fetch applies N/N, `tree-equal` (57/57 at the end).
4. `make test` passes.
5. `d3d12_clear` reads `shader model 0x68` for a 0x69 request, `0x67` for a 0x67 request, OPTIONS9 `typed 1
   groupshared 1`, OPTIONS11 `heap 1`, and OPTIONS21/OPTIONS22's answers (with `DXMT_D3D12_SM6=0` the OPTIONS9, OPTIONS11 and OPTIONS21 flags read 0;
   OPTIONS22 is answered either way).
6. The translator key, old (`d6863ecc…`) and new.
7. **SMITE 2**, one session on a development build of the final tree (the maintainer launches it): the launcher rebuilds
   the recorded pipelines once; no `not supported` line and no new error in the log; the game plays as before, with
   attention to whatever uses the six changed pixel shaders.
8. One line for the maintainer on what rests on offline translation or has no row: op 109 dropped (no HLSL intrinsic
   emits it); op 75 and the pixel-shader `SampleCmpBias`/`Grad` lowering checked offline only, so OPTIONS21's
   comparison flag rests on compute behaviour; a WaveSize refusal answers `E_FAIL` where Microsoft's runtime likely
   answers `E_INVALIDARG`; no row for `WaveMatch`/`WaveMultiPrefix*` masking in pixel shaders, `QuadVote` in 2D compute,
   the indexed geometry-shader start-vertex path, 64-bit atomics in a geometry pipeline, or the lock table's re-zero on
   a command-buffer error (a GPU fault can't be forced safely).
9. **64-bit atomics**, for the maintainer: OPTIONS9 and OPTIONS11 are honest (no atomic cap is over-reported);
   `Int64ShaderOps` stays FALSE until the i64 audit; the known non-conformance (a 32-bit atomic or a plain store racing a
   64-bit atomic on the same bytes isn't atomic with respect to it); §3.8's costs table and its cliff (seconds per
   dispatch for thousands of threads on one address); and "64-bit atomics: not measured on M1 (Apple7)": the 14a-14c
   groups ran on an M5 Pro only, and M1 is not claimed until they run on one.

## 9. Release consequences

- The translator key changes (any byte in `src/airconv`), so every player's shader cache is rebuilt once and the
  launcher replays recorded pipelines before the game starts (6-46 s for SMITE 2,
  `docs/testing/acceptance-dxmt-pipeline-cache.md:38`). S-R7: one key change for this whole spec, so no release or tag
  between its first and last task.
- The next release's notes (`build/release-notes-<version>.md`, Graphics) say: DXMT reports Shader Model 6.8; dynamic
  resources, compute-shader derivatives, float and 64-bit atomics and the 6.5-6.8 wave and quad operations are
  translated; each game's shaders are rebuilt once at its next start.
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
- **64-bit atomics' forward progress has no OS backstop.** It rests on AGX scheduling resident simdgroups, which Metal
  doesn't promise, and on the emitted control-flow shape; the macOS GPU watchdog never fired on a 147.8 s livelock.
  Mitigations: the 64-bit groups run under the test harness's watchdog, and phase 1 stays loop-free.
- **Hot-address storms.** One 64-bit counter hit by tens of thousands of threads stalls a dispatch for seconds (the
  CAS lock is unfair). There is deliberately no spin cap. Upgrade path, if a game shows it: backoff or a ticket lock,
  or wave-level aggregation on a uniform address.
- **Stale locks.** The re-zero on a command-buffer error (§3.8) has no automated test, and it can break one critical
  section still running on another queue.
- **M1 (Apple7) is unmeasured**: G13 scheduling, and `RG32Uint` read_write texture access for 64-bit texture atomics.
  The 64-bit groups must run on an M1 before the acceptance record claims it.
- **AIR forms in DXMT's 2.6 module.** The atomic `load`/`store` i32 form is unproven there until the plan's Task 14a
  checks it (a fallback is named); `cmpxchg i32` and `air.atomic.fence` are already in production.
- **The texture key** `(x, y)` collides across textures and slices: contention, not errors; the 64Ki table size is the
  knob.
- **Root-argument layout coupling.** The lock qword's offset depends on §3.4's flag-conditional heap qwords; translator
  and runtime must compute it identically, which `atomic64` (heap flags) and `tri-atomic64` (none) test.
