# DXMTRS step 1: nvngx in Rust Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** DXMT's `nvngx.dll` (DLSS answered by MetalFX, D3D11 and D3D12) is Rust, built by DXMTRS's meson for the Mac
(ARM64EC) and the VM guest (x86_64), with the Rust plumbing on DXMTRS's `main`, Rust's licence notices shipping, and
MacNeutron pinned to it.

**Architecture:** The C++ in `src/nvngx` is replaced by a crate, `nvngx`, in the PE workspace. Meson links the DLL from
the crate's static library with a `.def` of its exports (the Rust plumbing of Phase 0's E1). Behaviour is pinned by
tests that already pass on the C++ (`d3d12_dlss`, and a new `d3d11_dlss` written against the C++ first): a stub crate
makes them fail, the port makes them pass.

**Tech Stack:** Rust 1.99.0, meson, llvm-mingw, DXMT's private D3D11/D3D12 COM interfaces, NVIDIA's NGX C ABI.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` (§3, §5 step 1, §8).

## Spec changes proposed (the maintainer approves them with this plan)

1. **The build-script move and the `pe/`/`unix/` rename wait for §7.** E1 showed DXMT's Rust builds inside DXMTRS's
   meson, so port steps 1 to 6 change no MacNeutron script; mtld3d's scripts first change at the merge. Until then the
   crates stay in `mtld3d/windows` and `mtld3d/unix`.
2. **§3:** a DLL with no C++ left is "linked by meson from its crate's static library and a `.def`, until meson goes",
   not a `cdylib` (mtld3d links static libraries too).
3. **§5 step 1 is split:** nvngx here; nvapi in its own plan, which starts with a characterization harness (94 NVAPI
   functions, one tested today).
4. **§5 step 1's gate:** the guest runs the x86_64 shim (`__chkstk`); the i686 ones (`__tls_array`, `__aulldiv`) wait
   for step 3, the first with i386 DLLs.

## Global Constraints

- Never push, create a repository or fetch without the maintainer's yes; each such step says STOP.
- Never open MacNeutron.app, start or quit Steam, run a game, or touch `~/Library/Application Support/MacNeutron`.
- Kill by exact PID. Benches once. Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- DXMTRS work on branch `step1-nvngx` from `main` in `build/wine-arm64-src/dxmt`; MacNeutron work on `dxmtrs`.
- Rust: 1.99.0, mtld3d's CONVENTIONS.md and `scripts/audit.sh` (its "competing implementation" rule doesn't apply to
  DXMT's own names); `unsafe` only at FFI, each block with a `SAFETY:` comment; no crate dependencies; `extern "system"`
  for every Windows-ABI entry point; `panic = "abort"` (the production profile).
- MacNeutron's build scripts pass no DXMTRS-only meson option (the pinned tree must keep configuring).
- Behaviour is DXMT's C++ behaviour: same results, same log lines (`err:   `, `warn:  ` … prefixes), same refusals.

## Review Focus

1. **A game's NGX SDK built with MSVC calls the parameter object's virtuals** in the order DXMT declares them (8 `Set`,
   8 `Get`, `Reset`, no destructor slot); a wrong slot is silent corruption — the DLSS rows set and read every type
   through it (Task 4).
2. **The DLL loaded must be the Rust one:** a stale C++ `nvngx.dll` in an install folder would pass every row — Task 2's
   stub RED proves the Rust exports are what the rows reach.
3. **`DllMain` refusing to load** without DXMT's NVIDIA extension (`DXGIGetDebugInterface1(DXMT_NVEXT_GUID)`): the
   check's "without it, nvngx.dll refuses to load" row stays green (Task 2 keeps the refusal in the stub).
4. **The guest's x86_64 build** links Rust's `-msvc` std with the `__chkstk` shim and has never run: the VM gate's five
   DLSS rows are its first run (Task 7).
5. **A feature handle freed twice or never** (`ReleaseFeature`, `Shutdown1`): the refuse row and a create/release/create
   sequence in `d3d11_dlss` (Task 1) cover both.

---

### Task 1: d3d11_dlss, characterizing the C++ D3D11 path

**Files:**
- Create (MacNeutron): `dxmt/tests/d3d11_dlss.cpp`
- Modify (MacNeutron): `dxmt/check.sh` (rows next to the d3d12 DLSS rows, line ~1245), `eac-vm/dxmt/invocations`

**Interfaces:**
- Produces: `d3d11_dlss.exe <nvngx path> <mode>`, modes `quality`, `refuse`, `cycle`; prints `dlss11 <mode> render
  <w>x<h>` and `dlss11 <mode> ok` or `dlss11 <mode> FAIL <why>`, as `d3d12_dlss` prints for D3D12.

- [ ] **Step 1: Write the program** after `d3d12_dlss.cpp`'s structure (its parameter vtable mirror, NGX entry points by
  `GetProcAddress`): `quality` creates a DLSS feature at the quality render size for a 1280x720 output and evaluates
  one frame of a known gradient with zero motion and depth, then reads back the output and requires it non-black and
  of output size; `refuse` requires `CreateFeature` without motion vectors to fail; `cycle` creates, releases and
  creates again, then `Shutdown1`.
- [ ] **Step 2: Check rows.** `expect "D3D11 DLSS quality upscales on MetalFX" … "dlss11 quality render 853x480:yes"` (the
  render size the C++ capability callback picks; read it from the first run), `refuse`, `cycle`, each under
  `MACNEUTRON_DLSS=1`; one row without it requiring nvngx to refuse to load.
- [ ] **Step 3: Run on the C++ nvngx** (DXMTRS `main`): `make dxmt-tests && sh dxmt/check.sh > build/step1/t1.txt 2>&1`.
  Expected: the new rows `ok`, every other row as before.
- [ ] **Step 4: Mutation.** The same rows with `MACNEUTRON_DLSS` unset in their `run` lines. Expected: the three `dlss11`
  rows FAIL. Restore.
- [ ] **Step 5: Commit** (MacNeutron `dxmtrs`): `dxmt tests: d3d11_dlss, DLSS through D3D11's NGX entry points`.

### Task 2: the Rust plumbing on main, and nvngx as a stub (RED)

**Files (DXMTRS `step1-nvngx`):**
- From `phase0` (cherry-pick, then edit): `meson.build`'s Rust block, `meson.options` (`cargo_toolchain`),
  `tools/cargo-staticlib.sh` (copy only when the library changed: `cmp -s "$lib" "$out" || cp …`, so an unchanged
  crate relinks nothing), `src/util/rust_pe_rt.c`. Not `rust_probe.c`, not the probe crates, not `phase0/`.
- Create: `mtld3d/windows/nvngx/{Cargo.toml,src/lib.rs}` (crate `nvngx`, `staticlib`), `src/nvngx/nvngx.def`
- Modify: `src/nvngx/meson.build` (the DLL from the crate's library, the `.def`, `-ldxgi`, no C++ sources),
  `mtld3d/windows/Cargo.toml` (member)

**Interfaces:**
- Produces: `cargo-staticlib.sh <toolchain> <rust-target> <crate> <target-dir> <out>` (Phase 0's), and meson's
  `rust_pe_libs` built per crate (`custom_target('<crate>-<target>')`), so later steps add crates the same way.
  `nvngx.def`: the 32 `NVSDK_NGX_*` exports of today's DLL (`llvm-objdump -p` of the C++ build lists them), and
  `DllMain` as the entry.

- [ ] **Step 1: Stub crate.** Every export returns `NVNGX_RESULT_FAIL` (`0xbad00000`) and does nothing else; `DllMain`
  keeps the C++'s refusal (returns FALSE unless `DXGIGetDebugInterface1(0, &DXMT_NVEXT_GUID, null)` succeeds).
- [ ] **Step 2: Build and export check.** `make wine-arm64`; `llvm-objdump -p` of the installed `nvngx.dll`. Expected:
  the same 32 export names as the C++ build's listing (diff empty).
- [ ] **Step 3: RED.** `sh dxmt/check.sh`. Expected: every row that needs a working feature FAILs (the d3d12 quality
  modes, `inverted`, Metal validation; `dlss11 quality` and `cycle`); the rows that expect a refusal (`refuse`,
  `dlss11 refuse`, the two "refuses to load") and the NVAPI vendor row (nvapi64, not nvngx) still pass; nothing else
  changes.
- [ ] **Step 4: Commit** (DXMTRS): `nvngx: a Rust crate behind the same exports (stub)`.

### Task 3: logging, matching DXMT's

**Files:** Create `mtld3d/windows/nvngx/src/log.rs`; its tests in `mtld3d/windows/nvngx/src/log/tests.rs`.

**Interfaces:**
- Produces: `log::err(&str)`, `log::warn(&str)`, `log::trace(&str)` with `util/log/log.cpp`'s behaviour: the level from
  `DXMT_LOG_LEVEL` (its names and default), the prefixes `trace: `, `debug: `, `info:  `, `warn:  `, `err:   `, each line
  to Wine's `__wine_dbg_output` when ntdll has it, and to the file `log.cpp`'s `getFileName` names under
  `DXMT_LOG_PATH`.

- [ ] **Step 1: Unit tests** (host target): the prefix and level filter for each level; the file name `getFileName`
  gives for an executable and module name. Run them: FAIL (no module).
- [ ] **Step 2: Implement; tests pass.** Commit: `nvngx: DXMT's logging, in Rust`.

### Task 4: COM and NGX definitions, and the parameter object

**Files:** Create `mtld3d/windows/nvngx/src/{com.rs,ngx.rs,parameter.rs}`; `src/parameter/tests.rs`.

**Interfaces:**
- Produces: `#[repr(C)]` vtables, hand-written in mtld3d's style, for exactly what nvngx calls: `IUnknown`,
  `IMTLD3D12DeviceExt` (`GetTemporalScalerScaleRange`, `CreateTemporalScaler`), `IMTLD3D12CommandListExt`
  (`TemporalUpscale`), `IMTLD3D12TemporalScaler`, `IMTLD3D11ContextExt`/`ContextExt1` and `IMTLD3D11DeviceExt` (the
  slots nvngx uses, the earlier ones as opaque pointers), `ID3D12Resource::GetDesc` and `ID3D11Texture2D::GetDesc`
  slots, with the GUIDs of `d3d12_interfaces.hpp` and `d3d11_interfaces.hpp`; `MTL_TEMPORAL_SCALER_D3D12_DESC` and its
  D3D11 twin with const size asserts. `Parameter`: the 17-slot vtable in `nvngx.hpp`'s declaration order (8 `Set`:
  `void*`, `ID3D12Resource*`, `ID3D11Resource*`, `int`, `unsigned`, `double`, `float`, `unsigned long long`; 8 `Get` in
  its order; `Reset`), `this` first, `extern "system"`, over a name → value map with `nvngx_parameter.hpp`'s conversion
  rules (which `Get` type reads which stored type).
- [ ] **Step 1: Size and offset facts.** A throwaway C++ file built with the DXMT tree's headers prints `sizeof` and
  `offsetof` of each struct the crate mirrors; they go in as `const _: () = assert!(…)`. Test: build the crate for
  `arm64ec-pc-windows-msvc` and `x86_64-pc-windows-msvc` (fails if a size differs).
- [ ] **Step 2: Parameter unit tests** (host): every `Set` type read back by every `Get` type gives
  `nvngx_parameter.hpp`'s answer (the `castable` table); `Reset` empties. FAIL first, then implement.
- [ ] **Step 3: Commit:** `nvngx: COM and NGX definitions, the parameter object`.

### Task 5: the NGX entry points, D3D12 (GREEN for d3d12_dlss)

**Files:** Create `mtld3d/windows/nvngx/src/d3d12.rs`, `src/entry.rs`; modify `src/lib.rs`.

- [ ] **Step 1:** `Init*`, `Shutdown*`, `AllocateParameters`, `GetParameters`, `GetCapabilityParameters` (the C++'s
  capability map and its render-size callback), `DestroyParameters`, `GetFeatureRequirements`,
  `GetScratchBufferSize`, `PopulateParameters_Impl`, `UpdateFeature` and the D3D12 `CreateFeature`, `EvaluateFeature`,
  `ReleaseFeature`, each doing what `nvngx.cpp` and `nvngx_d3d12.cpp` do, with their log lines.
- [ ] **Step 2: GREEN.** `make wine-arm64 && sh dxmt/check.sh`. Expected: every `dlss` row ok in the x64 lane;
  `dlss11` rows still FAIL (D3D11 is Task 6). Then the ARM64EC lane: the `dlss` rows ok.
- [ ] **Step 3: Commit:** `nvngx: the D3D12 path in Rust`.

### Task 6: D3D11 (GREEN for d3d11_dlss), and the C++ removed

**Files:** Create `mtld3d/windows/nvngx/src/d3d11.rs`; delete `src/nvngx/*.cpp`, `*.hpp`.

- [ ] **Step 1:** the D3D11 `CreateFeature`, `EvaluateFeature`, `ReleaseFeature` as `nvngx.cpp` does them.
- [ ] **Step 2: GREEN, both lanes:** `make dxmt-check` (its two lanes; `build_test.sh`'s two known `main` failures
  aside). Expected: every `dlss` and `dlss11` row ok, no other row changed against Task 1 Step 3's run.
- [ ] **Step 3: Lint:** `cargo +stable clippy -p nvngx --target aarch64-apple-darwin -- -D warnings` if the crate builds
  for the host, else for `x86_64-pc-windows-msvc` after `rustup target add --toolchain stable` (a download: STOP and
  ask); `scripts/audit.sh --file` on each new file. Expected: clean.
- [ ] **Step 4: Commit:** `nvngx: the D3D11 path in Rust; the C++ goes`.

### Task 7: notices, the guest, and MacNeutron pinned

**Files (MacNeutron `dxmtrs`):** `wine-arm64/licenses/README` (DXMT's entry: Rust's standard library in its DLLs, the
texts `bundle.sh` already copies for mtld3d), `wine-arm64/bundle.sh` if the texts need a second place,
`wine-arm64/tests/licences_test.sh` (a row for it), the VM image's licence list in `eac-vm/image/build.sh`, and
`dxmt/pins` (`DXMT_COMMIT`).

- [ ] **Step 1: Notices**, test first: a `licences_test.sh` row requiring DXMT's README entry to name Rust's standard
  library and its texts. FAIL, write, pass.
- [ ] **Step 2: Guest.** `sh eac-vm/dxmt/build.sh`, stage and image as `eac-vm/dxmt/gate.sh`'s header says, then
  `MN_DXMT_ONLY="d3d12_dlss_vendor d3d12_dlss_quality d3d12_dlss_performance d3d12_dlss_inverted d3d12_dlss_refuse"
  sh eac-vm/dxmt/gate.sh` (and the `d3d11_dlss` invocations). Expected: every row matches macOS — the first run of the
  guest's Rust and its `__chkstk` shim.
- [ ] **Step 3: STOP — ask the maintainer** to push `step1-nvngx` to DXMTRS `main` (fast-forward). On a yes: push, then
  `DXMT_COMMIT` = its head, `make wine-arm64` (no development build: the tree is the pin), the x64 lane once.
- [ ] **Step 4: Spec** (MacNeutron): the four changes above, as approved; §4 E1's line on the i686 shims. Commit:
  `DXMT: nvngx in Rust (DXMTRS <short>)`.
