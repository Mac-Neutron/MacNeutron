# Direct3D forwarding M4: D3D11 and D3D10 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A guest program's D3D11 and D3D10 run on native DXMT through forwarding, as its D3D12 does since M2: a
headless D3D11/D3D10 test set matches its macOS run natively, through the loopback and in the VM; dynamic maps are
answered in the guest (spec §4.10); no D3D11 call holds a vCPU waiting for the GPU or a drawable; and a forwarded
process's D3D11 device works (today guest DXMT's `d3d11` refuses the forwarded adapter: "Not a DXMT adapter").

**Architecture:** M2's generator, proxies, records and dispatcher carry D3D11 and D3D10 too: the headers join the
model, `rules.txt` annotates them, and `d3d11.dll` and `d3d10core.dll` become stand-ins forwarding to `mnd3d.dll`
like `d3d12.dll` and `dxgi.dll`. Four generator limits found by M4's research go first: the 128-interface answer mask,
getters returning arrays of objects (marshalled today as one handle), companions that answer only the interface asked
(DXGI, D3D11 and D3D10 share objects at different pointers), and string types. Initial data and `UpdateSubresource`
sizes come from the resource's description, which the guest keeps. Presentation is paced in the guest on an
`ID3D11Fence`, as M3's D3D12 swapchains are on an `ID3D12Fence`. Maps run in guest memory: first synchronously
(counted as allocating calls, so renames land in the guest's arenas), then answered in the guest from pieces the host
hands out ahead of time, with an asynchronous `Unmap` naming the piece now current (a fork extension).

**Tech Stack:** C (guest unix side, host dispatcher, the shared marshaller), C++17 (guest proxies, host stubs, the test
programs), Python 3 (the generator), DXMT's fork (meson, native and PE), POSIX sh.

**Spec:** `docs/superpowers/specs/2026-10-17-macneutron-vm-d3d-forwarding-design.md` (§1's D3D10/11 line, §2, §4.2 to
§4.6, §4.9, §4.10, §6, §7, §8 M4, §11). M2 and M3: research README §19 and §20.

## Global Constraints

- Gate (spec §8 M4): "DXMT's D3D11 and D3D10 test programs through forwarding match macOS", by `compare.py`'s rule
  against one macOS run each, with no host error and `coverage.py` ok over the traces. The programs are ours, in
  `dxmt/tests`, as the D3D12 ones are (DXMT's own `tests/dx11` are windowed demos and there is no D3D10 program).
- No host call waits for the GPU or a drawable on a vCPU: waits happen in the guest (M2's transport constraint). This
  includes D3D11's `Present` and staging `Map(READ)`.
- `d3d10.dll`'s effects framework stays Wine's, in the guest, on top of the forwarded `d3d10core` (spec §11);
  `d3d11on12` and video: `E_NOINTERFACE` (§11).
- `MACNEUTRON_VM_D3D=forward` stays opt-in (the default is `guest`) until spec §9; with it, `d3d12`, `dxgi`, `d3d11`
  and `d3d10core` are all forwarded.
- No games in M4 (its gate is the programs). Palworld's M3 run is apart, with the maintainer's go-ahead.
- Benches and reference runs run once (`MN_REF_RUNS=1`). The VM keeps every core and its RAM. Apple's vGIC stays.
- DXMT fork changes: native-only (the macOS runtime's PE build must not change behaviour), one commit each in
  `build/wine-arm64-src/dxmt` (the maintainer as author, `Assisted-by: Claude Code: claude-opus-5-5`), exported with
  `git format-patch -N` into `wine-arm64/patches/dxmt/` (0087 on), checked by `git am` of the whole series onto
  `DXMT_COMMIT` (`. dxmt/pins`) in a scratch worktree.
- Before every VM run, rebuild what changed and everything sharing its structs: native DXMT and the host together;
  both guest stages `guest-stages.sh dxmt d3d` (the guest unix side, `unix.c`, is built into `winemetal.so` by the
  dxmt stage) and `wine.erofs`. `eac-vm/host/build.sh` runs with `MACNEUTRON_SIGN_IDENTITY` and
  `MACNEUTRON_PROVISIONING_PROFILE` set; the builder starts with `VM_SHARES="out=$PWD/build/eac-vm-work/images"`.
- Never run two scripts that use `ref.sh` at once (one fixed work folder): `build-tests.sh`, `loopback-gate.sh`,
  `dxmt/gate.sh`, `present-gate.sh`, `d3d/gate.sh`.
- Scripts use `/usr/bin/grep`; commits end with the session's trailer lines; a privacy scan before committing docs; no
  push, tag or publish without the maintainer's yes. One test lane per task (lighter-task-testing); the full gates
  run in Task 9.

## Review Focus

- A dynamic buffer mapped `WRITE_DISCARD` more times in a frame than the guest holds pieces, then drawn each time:
  every draw sees its own data, no `Map` hands out memory the GPU still reads, and arena memory stays bounded over a
  long run (Task 8: `d3d11_dynamic`'s stress case, and its arena line).
- An immediate context used from two threads under `ID3D11Multithread` protection: calls stay in order across the
  threads and `Enter`/`Leave` never deadlock (Task 5: `d3d11_deferred`'s shared case through the loopback).
- Initial data and `UpdateSubresource` for 3D textures, mipped arrays and block-compressed formats, including a box
  at a mip tail smaller than a block: the host gets exactly the bytes D3D11 defines and reads none past the guest's
  buffer (Task 4: `d3d11_transfer`'s layout cases and the roundtrip size cases).
- Staging `Map(READ)` right after a GPU copy, without `DO_NOT_WAIT`: the copy's data comes back and no vCPU is held
  meanwhile (Task 7: `d3d11_transfer`'s readback line and the host's slow lines).
- A D3D11 swapchain made by `D3D11CreateDeviceAndSwapChain`, presenting faster than the GPU without a waitable
  object: at most the device's frame latency in flight, and no host `Present` over 4 ms (Task 6: smoke's
  `d3d11latency` line).

---

### Task 1: The D3D11 test programs, against macOS, in guest mode and natively

**Files:**
- Create: `dxmt/tests/d3d11_common.hpp` (device, readback, DXBC loading from `@S`, and on `_WIN32` or natively the
  same calls), `dxmt/tests/dxbc.cpp` (every program's HLSL; `dxbc.exe <folder>` compiles each with `D3DCompile` and
  writes `<shader>.<profile>.dxbc`), `dxmt/tests/dxbc/*.dxbc` (its output, committed), `dxmt/tests/d3d11_api.cpp`,
  `d3d11_render.cpp`, `d3d11_compute.cpp`, `d3d11_transfer.cpp`, `d3d11_dynamic.cpp`
- Modify: `dxmt/tests/d3d11_vsia.cpp` (its shaders from `@S`, no `D3DCompile` when it runs), the top `Makefile`
  (`dxmt-tests` builds `d3d1[01]_*.cpp` and `dxbc.exe`), `eac-vm/dxmt/invocations` (a row each),
  `eac-vm/dxmt-native/build-tests.sh` and `gate.sh` (the D3D11 rows natively, linking `d3d11.dylib`)
- Test: `eac-vm/dxmt/ref.sh` (macOS), `sh eac-vm/dxmt/gate.sh` (guest mode), `sh eac-vm/dxmt-native/gate.sh`

**Interfaces:**
- Produces: a program per row printing `<program> <check> <value>` lines whose values are deterministic (counts of
  wrong texels or bytes, read-back values); rows `name|exe|@S args|prefix` as the D3D12 ones. Every check named below
  prints a line in every mode; a step the runtime can't take prints `<check> skip <why>` (compared too).
- `d3d11_common.hpp`: `Dev d3d11(D3D_FEATURE_LEVEL)`, `std::vector<uint8_t> dxbc(const char *name)` (from `@S`),
  `template <class T> std::vector<T> readback(ID3D11Resource *, UINT subresource)`.

- [ ] **Step 1:** Write the programs. `d3d11_api`: `D3D11CreateDevice` at 11_1, 11_0 and 10_0; `CheckFeatureSupport`
  (threading, doubles, D3D11_OPTIONS, ARCHITECTURE_INFO); `CheckFormatSupport` and `CheckMultisampleQualityLevels` for
  eight formats; private data and `SetDebugObjectName`; `IDXGIDevice → GetAdapter → GetParent(IDXGIFactory1)`;
  `QueryInterface` from `ID3D11Device` to `IDXGIDevice1`, `ID3D10Device1` and `ID3D11Multithread` and back, each
  pair's `IUnknown` the same; `IDXGIResource::GetUsage` on a texture. `d3d11_render`: an instanced draw into two
  render targets with depth and stencil, blend and rasterizer states, a 4x MSAA target resolved, `GenerateMips` on a
  256x256 texture; each target read back and summed per channel. `d3d11_compute`: a compute shader over a structured
  and a raw UAV; `OMSetRenderTargetsAndUnorderedAccessViews` with an append UAV's initial count and with
  `D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL`; `ClearUnorderedAccessViewUint`/`Float`; `ClearView`.
  `d3d11_transfer`: `CopyResource`, `CopySubresourceRegion` with boxes, `UpdateSubresource` and `UpdateSubresource1`
  on buffers and on 2D, 3D, mipped-array and BC1/BC7 textures (a box at a 2x2 mip tail included); `CreateTexture*`
  with initial data in the same layouts; staging `Map(READ)` right after a copy, with and without `DO_NOT_WAIT`
  (retried until it answers). `d3d11_dynamic`: 1,000 `Map(WRITE_DISCARD)` of a 256-byte constant buffer a frame for
  4 frames, a draw after each reading it (a 1-texel target per draw, read back: wrong count); `WRITE_NO_OVERWRITE`
  appends to a dynamic vertex buffer; `VSSetConstantBuffers1` offsets; a dynamic 64x64 texture mapped `DISCARD` with
  its row pitch honoured; a stress case of 10,000 maps of a 64 KiB buffer in one frame.
- [ ] **Step 2:** Run `dxbc.exe dxmt/tests/dxbc` under the macOS runtime (as `ref.sh` runs programs), commit its
  output; `make dxmt-tests`. Run `MN_DXMT_ONLY="d3d11_vsia d3d11_api d3d11_render d3d11_compute d3d11_transfer
  d3d11_dynamic" MN_REF_RUNS=1 sh eac-vm/dxmt/ref.sh`. Expected: each prints all its lines.
- [ ] **Step 3:** Run the guest-mode gate (`MN_DXMT_ONLY` as above, `sh eac-vm/dxmt/gate.sh`) and the native gate
  with the rows (`sh eac-vm/dxmt-native/gate.sh`). Expected: each row as on macOS. A difference is a DXMT bug in that
  build: fix it in the fork (native-only if the PE build is right) with the row as its test, or rule.
- [ ] **Step 4:** Commit.

### Task 2: The rest of the set: queries, deferred contexts, pipeline stages, getters, D3D10

**Files:**
- Create: `dxmt/tests/d3d11_query.cpp`, `d3d11_deferred.cpp`, `d3d11_pipeline.cpp`, `d3d11_getters.cpp`,
  `d3d10_core.cpp`, `d3d10_wine.cpp`
- Modify: `dxmt/tests/dxbc.cpp` (their shaders), `eac-vm/dxmt/invocations`, `eac-vm/dxmt-native/build-tests.sh`
  (`d3d10core.dylib`; not `d3d10_wine`)
- Test: as Task 1

**Interfaces:**
- `d3d10_core` makes its device with `D3D10CoreCreateDevice(factory, adapter, 0, D3D_FEATURE_LEVEL_10_0, &device)`
  (from `d3d10core.dll` by `GetProcAddress` on `_WIN32`, linked natively); `d3d10_wine` with `d3d10.dll`'s
  `D3D10CreateDevice` (guest and macOS rows only).

- [ ] **Step 1:** Write them. `d3d11_query`: event, occlusion (a known pixel count), timestamp and disjoint queries
  polled with `GetData` until ready (the poll count isn't printed); `ID3D11Device5::CreateFence`,
  `ID3D11DeviceContext4::Signal`, `GetCompletedValue`, `SetEventOnCompletion` waited with a 5 s timeout.
  `d3d11_deferred`: a deferred context recorded on a second thread (draws, a `Map(WRITE_DISCARD)` inside, state)
  executed on the immediate context with and without `RestoreContextState`; and the shared case: two threads issuing
  draws on the immediate context in turn under `ID3D11Multithread::Enter`/`Leave` (`SetMultithreadProtected(TRUE)`),
  the target read back in the order they took turns. `d3d11_pipeline`: a geometry shader with stream output
  (`CreateGeometryShaderWithStreamOutput`, `SOSetTargets`, `DrawAuto`), and hull and domain shaders tessellating a
  patch. `d3d11_getters`: after setting them, `PSGetShaderResources`, `VSGetConstantBuffers`, `PSGetSamplers`,
  `IAGetVertexBuffers`, `OMGetRenderTargets`, `OMGetBlendState` and `SOGetTargets` return the same objects (pointer
  equality, then released). `d3d10_core`: buffers and textures with initial data, `Map` on a dynamic buffer and a
  staging texture, an occlusion query, a draw read back. `d3d10_wine`: `D3D10CreateDevice`, a
  `D3D10CreateStateBlock` capturing all state, a state change, `Apply` restoring it (read back through the getters).
- [ ] **Step 2:** As Task 1's Steps 2 and 3 with these rows (natively: not `d3d10_wine`). Expected: as on macOS.
- [ ] **Step 3:** Commit.

### Task 3: The wire for more interfaces, arrays of objects out, strings and declared exports

**Files:**
- Modify: `eac-vm/d3d/gen.py`, `eac-vm/d3d/mn_d3d.h`, `eac-vm/metal/wm/calls.h` and `marshal.c` (a new field kind),
  `eac-vm/d3d/guest/proxy.cpp` and `proxy.h`, `eac-vm/d3d/host/native.cpp`, `eac-vm/d3d/host/dispatch.c`
- Test: `eac-vm/d3d/tests/test_gen.py`, `eac-vm/d3d/tests/identity.sh`, `make -s -C eac-vm/d3d test dispatch-test`,
  `sh eac-vm/d3d/loopback-gate.sh`

**Interfaces:**
- Produces (mn_d3d.h): `MN_D3D_MASK_WORDS 4` (256 interfaces); every reply's interfaces block is `uint64_t
  ifaces[2 * MN_D3D_MASK_WORDS + 1]`: answers in words 0-3, moved in 4-7, the host's first handle for the object in 8
  (today `ifaces[5]`); `mn_obj::answers[4]`, `moved[4]`; `mn_proxy_out(uint64_t handle, int iface, const IID *riid,
  const uint64_t ifaces[9])`.
- Produces (calls.h): `WMF_HANDLES_OUT`, an array of `count` objects out (count as `WMF_ARRAY`'s): the guest passes an
  array of `struct mn_d3d_out_handle { uint64_t handle; uint64_t ifaces[2 * MN_D3D_MASK_WORDS + 1]; }`, the host binds
  each non-NULL pointer as a creation's `WMF_HANDLE_OUT` (identity included) and fills its block; the proxy turns each
  into a proxy with `mn_proxy_out` into the caller's array. Rule: `<Method> <param> out count=<arg>` on a `T **`.
- gen.py: `iface_index` allows 256 and stops above; a `T **` argument with no rule, in a method with a `UINT` parameter
  named `Num…` or `…Count`, stops the build ("needs a rule"); `LPCSTR` is `const char *` (`WMF_STR_IN`), `LPCWSTR`
  `const WCHAR *` (`WMF_WSTR_IN`), `LPSTR` needs a rule; `::<Name> declare <dll> <ret> (<params>)` declares an export
  no header has.

- [ ] **Step 1:** test_gen: `test_masks` (an interface index of 200 encodes in word 3; `iface_index` raises at 257
  interfaces), `test_handles_out` (a test-only interface declared in a rules snippet, as rules.txt declares DXMT's
  private ones, with a `Get(UINT NumViews, ITest **views)` method and the rule `views out count=NumViews`: emits
  `WMF_HANDLES_OUT` and the proxy's loop), `test_array_out_needs_a_rule`, `test_strings` (`LPCSTR`, `LPCWSTR`), `test_declare` (a declared
  export's proxy and stub). identity.sh with the 9-word block and an interface index above 128. Run them. Expected
  RED: each fails (no such kinds; identity's mask too narrow).
- [ ] **Step 2:** Implement; D3D12 and DXGI behave as before.
- [ ] **Step 3:** Run test_gen, identity.sh, `make -s -C eac-vm/d3d test dispatch-test` and the loopback gate.
  Expected: all pass, the loopback 19/19.
- [ ] **Step 4:** Commit.

### Task 4: D3D11 and D3D10 in the model and the rules

**Files:**
- Modify: `eac-vm/d3d/gen.py` (the headers `directx/d3d11.h` to `d3d11_4.h`, `d3d10.h`, `d3d10_1.h` from native DXMT's
  include set; EXPORTS gains `d3d11` and `d3d10core`), `eac-vm/d3d/rules.txt`, `eac-vm/d3d/tests/coverage.py`
  (`AS_DXMT`)
- Create: `eac-vm/d3d/guest/formats.cpp` (`struct mn_format { uint8_t block_w, block_h, bytes; }
  mn_format_of(DXGI_FORMAT)` for every format)
- Modify: `eac-vm/d3d/guest/proxy.cpp` (the resource's description kept on its `mn_obj` at creation; the own code's
  guest half), `eac-vm/d3d/host/native.cpp` (its host half)
- Test: `eac-vm/d3d/tests/test_gen.py`, `make -s -C eac-vm/d3d layout roundtrip`, `eac-vm/d3d/tests/roundtrip.c`

**Interfaces:**
- Consumes: Task 3's kinds.
- Produces (rules.txt): every D3D11 and D3D10 method a mode; `::D3D11CoreCreateDevice declare d3d11 HRESULT
  (IDXGIFactory *factory, IDXGIAdapter *adapter, UINT flags, const D3D_FEATURE_LEVEL *feature_levels, UINT levels,
  ID3D11Device **device)` and `::D3D10CoreCreateDevice declare d3d10core HRESULT (IDXGIFactory *factory, IDXGIAdapter
  *adapter, UINT flags, D3D_FEATURE_LEVEL feature_level, ID3D10Device **device)`; `ID3D10Multithread` dropped from the
  model (its IID is `ID3D11Multithread`'s, the same vtable).
- Refused: the video interfaces, `ID3D11CryptoSession`, `ID3D11AuthenticatedChannel`, `ID3D11Debug`,
  `ID3D11InfoQueue`, `ID3D11On12*`, `IDXGIDevice4`, `IDXGISurface*`; whole methods DXMT aborts on (the tiled methods,
  `GetResourceTiling`, `SwapDeviceContextState`, `ID3D11Device3::WriteToSubresource`/`ReadFromSubresource`,
  `IDXGIDevice::CreateSurface`, a deferred context's `ExecuteCommandList`) and shared handles
  (`OpenSharedResource*`, `ID3D11Fence::CreateSharedHandle`, `OpenSharedFence`, `IDXGIResource1::CreateSharedHandle`,
  `CreateSubresourceSurface`). `AS_DXMT` gains the E_NOTIMPL ones: `CreateCounter`, `CheckCounter` (D3D11 and D3D10),
  `ID3D11ClassLinkage::CreateClassInstance`, `GetClassInstance`, `D3D10CoreRegisterLayers`, `D3D11On12CreateDevice`.
- Own code (guest and host halves, one blob for data past a record): `CreateBuffer`, `CreateTexture1D/2D/3D` (D3D11
  and D3D10): `pInitialData` for each subresource packed with its pitches, its size `(d - 1) * slice_pitch + (rows -
  1) * pitch + ceil(w / block_w) * bytes` with `rows = ceil(h / block_h)` for the subresource's mip size;
  `UpdateSubresource` and `UpdateSubresource1`: the same from the destination's kept description and the box (the whole
  subresource when NULL); `OMSetRenderTargetsAndUnorderedAccessViews`: `D3D11_KEEP_*` counts carry no array;
  `CreateInputLayout` and `CreateGeometryShaderWithStreamOutput`: their `SemanticName` strings (struct field rules).

- [ ] **Step 1:** test_gen: `test_d3d11_counts` (205 interfaces at most, the manifest's rows and every method a mode),
  `test_d3d11_getters_are_arrays` (`PSGetShaderResources` is `WMF_HANDLES_OUT`), `test_d3d11_own` (the initial-data and
  update methods are own code); roundtrip: a size case each for a 3D texture's subresource, a mipped array's mip 3,
  BC7 at a 2x2 mip tail with a box, and `R32G32B32_FLOAT`. Run. Expected RED: no D3D11 in the model.
- [ ] **Step 2:** Implement (the headers, the rules, formats.cpp, the own code). Expected: test_gen, `make layout
  roundtrip` pass; no method without a rule.
- [ ] **Step 3:** Commit.

### Task 5: D3D11 and D3D10 through the loopback, and a forwarded process's D3D11 (fork 0087)

**Files:**
- Fork: `src/d3d11/d3d11_private.h` (native: `UNIMPLEMENTED` throws `std::logic_error`, as 0084 made d3d12's
  `IMPLEMENT_ME`; the PE build keeps its abort)
- Create: `eac-vm/d3d/guest/d3d11.def` (`D3D11CoreCreateDevice @18`, `D3D11CreateDevice @22`,
  `D3D11CreateDeviceAndSwapChain @23`, `D3D11On12CreateDevice @24`, `D3D11CoreRegisterLayers`, forwarding to
  `mnd3d`), `eac-vm/d3d/guest/d3d10core.def`
- Modify: `eac-vm/d3d/build-guest.sh` (the two stand-ins), `eac-vm/d3d/guest/prefix.sh` (`d3d12,dxgi,d3d11,d3d10core,
  mnd3d=n`, links in `system32`), `eac-vm/d3d/build-loopback.sh` (the exports), `eac-vm/d3d/host/dispatch.c`
  (`mn_d3d_load` opens `d3d11.dylib` and `d3d10core.dylib`; exports by their DLL), `eac-vm/d3d/host/native.cpp`
  (a companion's reply carries its whole interfaces block, computed at its pointer as a creation's is),
  `eac-vm/d3d/guest/proxy.cpp` (companions with their whole masks; `ID3D11Multithread::Enter`/`Leave` in the guest;
  `D3D11CreateDeviceAndSwapChain` a wrap), `eac-vm/d3d/gen.py` (`mn_d3d_record` for `ID3D11DeviceContext*` and
  `ID3D10Device*` methods too)
- Test: `eac-vm/d3d/tests/dispatch-test.c`, `sh eac-vm/d3d/loopback-gate.sh` with the D3D11/D3D10 rows

**Interfaces:**
- Guest: `ID3D11Multithread::Enter`/`Leave` (and through `ID3D10Multithread`) take a recursive critical section on
  the device's `mn_obj` and aren't forwarded (the host's lock is a host thread's; a guest thread moves between vCPUs);
  `SetMultithreadProtected` and `GetMultithreadProtected` are forwarded.
- Guest: `D3D11CreateDeviceAndSwapChain` pushes the window (op 7), calls the generated proxy, binds the window and
  replays actions, as M3's `create_swapchain` (pacing comes in Task 6).

- [ ] **Step 1:** dispatch-test cases: `d3d11device` (`D3D11CoreCreateDevice` with an adapter handle from
  `EnumAdapters`), `d3d11identity` (`ID3D11Device → IDXGIDevice1 → ID3D10Device1 → ID3D11Device` comes back to the
  first proxy's object, each companion answering its whole family), `d3d11getters` (two views set by
  `PSSetShaderResources` come back from `PSGetShaderResources` as the same proxies), `d3d11notimpl` (`Map` of a
  DEFAULT buffer, DXMT's `UNIMPLEMENTED`, fails and the host goes on). Run `make -s -C eac-vm/d3d dispatch-test`.
  Expected RED: no D3D11 export loads.
- [ ] **Step 2:** Implement (fork 0087 and the series check, the stand-ins, the load, companions, Multithread, the
  wrap, the record rule).
- [ ] **Step 3:** `make -s -C eac-vm/d3d dispatch-test`; `MN_DXMT_ONLY="<the D3D11/D3D10 rows but d3d10_wine>" sh
  eac-vm/d3d/loopback-gate.sh`. Expected: dispatch-test ok; each row as on macOS (the loopback shares one address
  space, so host memory behind a map works there; Task 7 makes the VM's right).
- [ ] **Step 4:** Commit.

### Task 6: D3D11 fences and presentation paced in the guest (fork 0088)

**Files:**
- Fork: `src/d3d11/d3d11_swapchain.cpp` (native, with window-system hooks set: `PresentBoundary`'s and `SyncFrame`'s
  waits skipped, the guest paces; the 31-chunk backpressure stays), and if `MTLD3D11Fence` doesn't let the host relay
  its events and mirror its value as M2 does `ID3D12Fence`'s, what it needs
- Modify: `eac-vm/d3d/rules.txt` (`ID3D11Device5::CreateFence`, `ID3D11Fence::GetCompletedValue`,
  `SetEventOnCompletion`, `ID3D11DeviceContext4::Signal` and `Wait` as their D3D12 counterparts;
  `IDXGIDevice1::SetMaximumFrameLatency` a wrap), `eac-vm/d3d/guest/proxy.cpp` (pacing on either fence),
  `eac-vm/d3d/host/native.cpp` and `events.c` (the D3D11 fence's mirror and relays), `eac-vm/dxmt/present-gate.sh`
  (`present_loop` forwarded)
- Test: `d3d11_query` through the loopback, `eac-vm/d3d/tests/smoke.cpp` (`d3d11latency`), `present-gate.sh forward`

**Interfaces:**
- Guest: `pacing` holds either a D3D12 queue and fence or a D3D11 `ID3D11DeviceContext4` and `ID3D11Fence`, behind
  `signal(pacing *, uint64_t)`, `completed(pacing *)` and `set_event(pacing *, uint64_t, HANDLE)`; `start_pacing` takes
  a D3D11 device when it answers `ID3D11Device5`; frames in flight are the device's latency (`SetMaximumFrameLatency`,
  3 by default), a waitable swapchain's object the guest semaphore as in M3.

- [ ] **Step 1:** smoke's `d3d11latency` line: a swapchain from `D3D11CreateDeviceAndSwapChain` (no waitable object)
  presents 60 frames each heavier on the GPU than on the CPU; the fence never trails the presents by more than 3, and
  d3d-smoke.sh's slow-line rule holds for the D3D11 presents. present-gate.sh runs `present_loop` forwarded with M3's
  checks. Run `sh eac-vm/tests/d3d-smoke.sh` and `present-gate.sh forward` (after Task 5's VM build). Expected RED: the
  host's `Present` held its vCPU (`mn-d3d: slow: IDXGISwapChain::Present` over the rule).
- [ ] **Step 2:** Implement (fork 0088, the fence rules and host half, the pacer, the wrap). `d3d11_query` through the
  loopback as on macOS.
- [ ] **Step 3:** Rebuild for the VM; run the smoke and the present gate. Expected: `d3d11latency ok`; `present_loop`
  presented, probed and counted as in guest mode, no held vCPU.
- [ ] **Step 4:** Commit.

### Task 7: Maps in guest memory, and no map waiting on a vCPU

**Files:**
- Modify: `eac-vm/d3d/gen.py` and `rules.txt` (a rule word `arena`: the call allocates through the memory hooks, so the
  host serves those allocations from the guest's arenas, as it does creations'), `eac-vm/d3d/host/memory.c` and
  `dispatch.c`, `eac-vm/d3d/host/native.cpp` (`Map`'s host half), `eac-vm/d3d/guest/proxy.cpp` (`Map`'s guest half)
- Test: `d3d11_dynamic` and `d3d11_transfer` forwarded in the VM (`eac-vm/d3d/gate.sh` with those rows), dispatch-test

**Interfaces:**
- `ID3D11DeviceContext::Map` and `ID3D10Buffer::Map`/`ID3D10Texture*::Map` are `arena`. Their host half checks that
  the arenas hold the resource's allocation size before calling DXMT (a map that renamed can't be repeated) and
  answers `MN_D3D_NEED_ARENA` with it otherwise, so the guest adds an arena and resends; the address it answers is the
  guest's (as M2's `ID3D12Resource::Map`).
- A map that would wait (staging or default `Map` without `DO_NOT_WAIT`) goes to the host with `DO_NOT_WAIT`; while
  it answers `DXGI_ERROR_WAS_STILL_DRAWING` the guest flushes, signals its device's internal `ID3D11Fence` (Task 6)
  and waits for it in the guest, then resends. A program that asked for `DO_NOT_WAIT` gets the host's answer.

- [ ] **Step 1:** dispatch-test's `d3d11maparena`: a dynamic buffer over 16 KiB mapped `DISCARD` twice; both addresses
  inside a guest arena. Run it, and the d3d gate with `d3d11_dynamic d3d11_transfer`. Expected RED: the second map's
  address is host memory; in the VM `d3d11_dynamic` reads wrong data or faults.
- [ ] **Step 2:** Implement.
- [ ] **Step 3:** dispatch-test; rebuild for the VM; the d3d gate with the two rows. Expected: as on macOS, and no
  `ID3D11DeviceContext::Map` in the host's slow lines.
- [ ] **Step 4:** Commit.

### Task 8: Dynamic maps answered in the guest (spec §4.10, fork 0089)

**Files:**
- Fork: `src/d3d11/` (native): `IMTLD3D11ContextExt` on the immediate context, a private interface, with
  `HRESULT GetPieces(ID3D11Resource *resource, UINT subresource, UINT count, struct MTLD3D11Piece *pieces)` and `void
  UsePiece(ID3D11Resource *resource, UINT subresource, UINT64 piece)`; `struct MTLD3D11Piece { UINT64 id; void *cpu;
  UINT row_pitch, depth_pitch; }`
- Modify: `eac-vm/d3d/rules.txt` (the interface a companion of `ID3D11DeviceContext`; `GetPieces` sync and `arena`,
  its `cpu` the guest's address; `UsePiece` asynchronous), `eac-vm/d3d/guest/proxy.cpp` (`Map`/`Unmap`'s guest path),
  `eac-vm/d3d/guest/unix.c` (the trace's `# arenas` line), `dxmt/tests/d3d11_dynamic.cpp` (an optional frame count)
- Test: `d3d11_dynamic` forwarded, a trace count, smoke

**Interfaces:**
- Fork: `GetPieces` hands out `count` allocations of the resource's subresource not in use (DXMT's recycled ones first,
  then new ones through the hooks), owned by the caller until `UsePiece` makes one current (DXMT's rename, as a
  `WRITE_DISCARD` map does, and for a dynamic texture without a linear layout the upload `Unmap` does) and returns
  the one it replaces to DXMT's recycling; `count == 0` answers the current one; it answers `E_NOTIMPL` for a resource
  the guest must map on the host (not dynamic, or a texture whose `NO_OVERWRITE` DXMT refuses). Pieces left with
  the guest go with the resource.
- Guest: a dynamic resource on the immediate context keeps a stash per subresource. `Map(WRITE_DISCARD)` takes a
  piece (refilling with `GetPieces(8)`, doubling up to 64 while a frame empties it), `WRITE_NO_OVERWRITE` answers the
  current piece; `Unmap` after a discard sends `UsePiece` (asynchronous). Deferred contexts and other maps stay
  Task 7's host maps (a command list holds its maps' memory until it's released).

- [ ] **Step 1:** Through the loopback with `MN_D3D_LOOPBACK_TRACE`: `d3d11_dynamic`'s guest trace counts at least 40,000
  `ID3D11DeviceContext::Map` calls and the host trace at most 1,000; and the guest's arena total (unix.c writes
  `# arenas <bytes>` with its counts) at most 4 times the total after the program's first frame (its own run with
  one frame, `d3d11_dynamic 1`). Expected RED: the host counts every map.
- [ ] **Step 2:** Implement (fork 0089 and the series check, the rules, the guest path).
- [ ] **Step 3:** The loopback and the VM's d3d gate with `d3d11_dynamic`, `d3d11_deferred`, `d3d11_transfer`.
  Expected: the counts; each row as on macOS.
- [ ] **Step 4:** Commit.

### Task 9: The gate

- [ ] **Step 1:** In one lane, one after the other: `sh eac-vm/d3d/gate.sh` (the 28 D3D12 rows and every D3D11/D3D10
  row forwarded; `d3d10_wine` in the VM only), `sh eac-vm/dxmt/gate.sh` (guest mode, every row),
  `sh eac-vm/dxmt-native/gate.sh` (natively, every row but `d3d10_wine`), `sh eac-vm/dxmt/present-gate.sh forward`
  (`present_loop` forwarded), `sh eac-vm/tests/d3d-smoke.sh`, `vm-setup.sh`, `turn.sh`, `modes.sh`, `tpm-host.sh`,
  `audio-host.sh`, `dxmt/check.sh` (the series' macOS check). Expected: each row as on macOS, coverage ok over every
  forwarded run's traces, the rest as M3 left them.
- [ ] **Step 2:** For each method the traces mark (refused but called, unhandled, or no host count): a rule or code,
  with a dispatch-test or loopback case that fails first. Then Step 1's affected runs again.
- [ ] **Step 3:** Commit.

### Task 10: Record

- [ ] **Step 1:** Research README §21 (what M4 built, the generator's new limits, the maps' two layers, the table: each
  D3D11/D3D10 row natively, through the loopback, guest mode and forwarded, with times); the spec's "(M4: …)" notes
  in §4.2, §4.3, §4.5, §4.9, §4.10 and §8's row; the memory files that change. Privacy scan; commit.
