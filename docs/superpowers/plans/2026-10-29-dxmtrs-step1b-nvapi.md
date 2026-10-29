# DXMTRS step 1b: nvapi in Rust Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** DXMT's `nvapi64.dll` is Rust, every one of its answers pinned first by a probe that ran against the C++ DLL.

**Architecture:** A probe (`d3d11_nvapi`, MacNeutron), built against NVIDIA's own `nvapi.h`, calls all 54 IDs
`nvapi_QueryInterface` answers, with valid and invalid inputs, and prints what each call returns and writes. Its
machine-independent lines become a golden file and check.sh rows; its full dumps are diffed between the C++ DLL (saved
in Task 1) and the Rust one. nvngx's logger, COM helpers, float casts and NVIDIA-extension check move to a shared crate,
`dxmt-util`, which the new `nvapi` crate uses; meson links `nvapi64.dll` from the crate's static library as step 1 does
for nvngx (stub RED, port GREEN).

**Tech Stack:** Rust 1.99.0, meson, llvm-mingw, NVIDIA's NVAPI headers (`external/nvapi`, MIT), winemetal's C API,
DXMT's private D3D11 interfaces.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` (§5 step 1, §7, §8); step 1's plan
`2026-10-29-dxmtrs-step1-nvngx.md` for the Rust plumbing this plan reuses.

## Decisions the maintainer approves with this plan

1. **§5 step 1's count is wrong:** nvapi answers 46 functions behind 54 IDs (8 IDs answer null on purpose), and names
   any other of `nvapi_interface.h`'s 511 IDs in its error log. Not 94.
2. **Null pointers the C++ dereferences** get `NVAPI_INVALID_ARGUMENT` (-5) in Rust instead of a crash:
   `GetErrorMessage`'s string, `D3D11_IsNvShaderExtnOpCodeSupported`'s out pointer, `D3D11_SetNvShaderExtnSlot`'s
   device, `DISP_GetDisplayIdByDisplayName`'s name and id, `D3D_GetObjectHandleForResource`'s handle,
   `Disp_GetHdrCapabilities`'s and `Disp_HdrColorControl`'s struct. That is the only behaviour change. Every other
   quirk stays as it is: `EnumNvidiaDisplayHandle` and `DISP_GetGDIPrimaryDisplayId` return OK without writing,
   `GPU_GetConnectedDisplayIds` never writes the count, `GetErrorMessage` writes an empty string.
3. **A shared crate before §7:** `dxmt-util` (`mtld3d/windows/dxmt-util`) holds what nvngx and nvapi both need, so
   nvapi doesn't copy it. §7's merge folds it into the shared crates.

## Global Constraints

- Never push, create a repository or fetch without the maintainer's yes; each such step says STOP.
- Never open MacNeutron.app, start or quit Steam, run a game, or touch `~/Library/Application Support/MacNeutron`.
- Kill by exact PID. Benches once. Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- DXMTRS work goes on branch `step1b-nvapi`, cut from `step1-nvngx` (be6fd36, unpushed), in
  `build/wine-arm64-src/dxmt`. MacNeutron work goes on `dxmtrs`.
- Rust and toolchain:
  - Build and test on 1.99.0 (`PATH=~/.rustup/toolchains/1.99.0-aarch64-apple-darwin/bin:$PATH`).
  - Lint with stable's clippy for `aarch64-apple-darwin`, pedantic and nursery, `-D warnings`.
  - Follow mtld3d's CONVENTIONS.md and `scripts/audit.sh` (filtered to the new crates).
  - No `#[allow]`/`#[expect]`.
  - `unsafe` only at FFI, one operation per block, each with `SAFETY:`.
  - No external crates.
  - `extern "C"` for `nvapi_QueryInterface` and the `NvAPI_*` functions (`NVAPI_INTERFACE` is `__cdecl`);
    `extern "system"` for Win32 imports.
  - `panic = "abort"`.
- MacNeutron's build scripts pass no DXMTRS-only meson option.
- Behaviour is the C++'s, decision 2 aside: the same statuses, the same bytes written (and left unwritten) and the same
  log lines.
- Host unit tests reach only pure functions. Imports link on the host (checked: dead code is stripped), but a test that
  calls one fails to link. Version dispatch, scaling, LUIDs, name lookup and string copies live in plain-value
  functions.

## Rules check (mtld3d's CLAUDE.md)

- **Statics:**
  - nvapi's `INIT_COUNT: AtomicU32` is `NvAPI_Initialize`/`Unload`'s process-wide count. The API has no object to hold
    it, and it is the C++'s `s_initialization_count`.
  - `static LOG: Logger` in each DLL is the process-wide logger, as DXMT's `Logger::s_instance` is.
  - The logger's state moves from nvngx's `LazyLock` static into the `Logger`. Each doc block says so.
- **Config keys:** none new (`DXMT_LOG_LEVEL`, `DXMT_LOG_PATH`, `MACNEUTRON_DLSS` exist).
- **Wire fields:** none.
- **Dependencies:** `dxmt-util`, a workspace path crate. No external crates.
- **Derives:** none beyond nvngx's.
- **Lint suppressions:** none new. Exception 9 (`float_cast.rs`) moves to `windows/dxmt-util/src/float_cast.rs` and
  gains `f32_to_u16` for nvapi's HDR scaling (C++'s float to `NvU16`). CONVENTIONS.md's text and `audit.sh`'s
  ALLOW_SITES path change in the same commit.
- **Duplicates:**
  - `dxmt-util`'s `Guid` duplicates `mtld3d-types`' D3D9 `Guid`, as nvngx's did since step 1. §7's COM merge makes
    them one.
  - The ID→name table is read from NVIDIA's `nvapi_interface.h` (`include_str!`, scanned on the error path), not
    copied. A drift test pins its 511 rows.

## Review Focus

1. **Bytes past a smaller struct version.** A game passes V1 to a function that knows V3. `all`'s dumps fill a
   largest-version buffer with 0xA5 and print all of it, so a Rust port that writes past V1's size (or zeroes what the
   C++ leaves) differs in Task 6's diff (Task 1 makes the dumps).
2. **Fields the C++ leaves unwritten versus zeroed.** `= {}` in `GetHdrCapabilities` and `GetConnectedDisplayIds`;
   listed fields only in the SLI state and sleep status. Caught by the same sentinel dumps.
3. **Vtable slots nobody observes.** `IMTLD3D11ContextExt::BeginUAVOverlap`/`EndUAVOverlap` (slots 4 and 5),
   `IMTLD3D11DeviceExt::SetShaderExtensionSlot` (slot 3), `ID3D11Device::GetImmediateContext`. Fake COM objects in the
   probe count the calls and record the slot. The C++ run validates the probe's copy of the interfaces, and the real
   device and its context cover the device path (Task 1).
4. **The guest's x86_64 `nvapi64.dll`.** Its first Rust run is the guest gate's `d3d11_nvapi` row, whose lines
   Task 1 checks on the C++ against macOS first (Task 7).
5. **Error log lines:**
   - the 8 null-on-purpose IDs log nothing;
   - `NvAPI_GPU_GetThermalSettings` (0xe3640a56) logs `err:   nvapi: function NvAPI_GPU_GetThermalSettings not
     implemented`;
   - 0x12345678 logs `err:   nvapi: function id 305419896 not implemented`.

   These are check.sh rows (Task 1) that pin the table and the shared logger in a second DLL.

---

### Task 1: d3d11_nvapi, characterizing the C++ nvapi64 (MacNeutron)

**Files:**
- Create: `dxmt/tests/d3d11_nvapi.cpp`, `dxmt/tests/d3d11_nvapi.fixed`
- Modify:
  - `Makefile`: both lanes' `d3d11_nvapi.exe` get a target-specific
    `NVAPI_INC = -Ibuild/wine-arm64-src/dxmt/external/nvapi`, which the two pattern rules append.
  - `dxmt/check.sh`: rows after the `dlss11` rows.
  - `eac-vm/dxmt/invocations`.
  - `eac-vm/tests/dxmt.sh:20` and `eac-vm/dxmt/ref.sh:30`: `d3d1[12]_dlss*|d3d11_nvapi`, so MACNEUTRON_DLSS is set.

**Interfaces:**
- Produces: `d3d11_nvapi.exe <nvapi64 path> fixed|all`.
  - Each line's first word is its family: `nvsys`, `nvgpu`, `nvdisp`, `nvd3d`, `nvquery`. The rest of the line is
    `<function>[(<case>)] <status> [<fields>]`.
  - `all` adds `nvdump <function>(<case>) <status> <hex of the whole buffer>` after each struct-writing call.
  - A function `nvapi_QueryInterface` doesn't answer prints `<family> <function> missing`.

- [ ] **Step 1: Write the program.** It loads the DLL by its path, takes `nvapi_QueryInterface`, makes a D3D11 device
  on the default adapter and reads that adapter's LUID through DXGI. Calls, in order:
  - **Initialisation:** Unload (before any Initialize), Initialize twice, Unload three times (the third past zero),
    Initialize.
  - **All 46 functions:** a valid call, each null pointer the C++ checks (none of decision 2's), each accepted struct
    version, and one wrong version (accepted size with version 9).
  - **Struct buffers:** the largest version's size, filled with 0xA5, `version` set. `fixed` prints the fields the
    C++ writes; for display- and GPU-dependent values, only the status and the relations below.
  - **Relations (`fixed`):**
    - physical and logical GPU counts are equal and at least 1, with equal handles;
    - `GetAdapterIdFromPhysicalGpu`'s LUID equals the DXGI adapter's, `bswap64(handle)`, and `GetLogicalGpuInfo`'s;
    - `DISP_GetDisplayIdByDisplayName(<the primary monitor's szDevice>)` equals
      `GPU_GetConnectedDisplayIds[0].displayId`;
    - `GetPhysicalGPUsFromDisplay(<a non-null handle>)`'s count equals `EnumPhysicalGPUs`'.
  - **HDR round trip** (`WMTUpdateDisplaySetting` writes winemetal's per-process table; no display changes):
    `Disp_HdrColorControl` SET with mode UHDA and fixed mastering data on the primary ID, then GET returns them (V1 and
    V2, with V2's format, range and bpc fields); SET OFF, then GET reports OFF.
  - **Fake COM objects**, with IUnknown and the GUIDs and slot order of `d3d11_interfaces.hpp`:
    - a context extension whose `BeginUAVOverlap`/`EndUAVOverlap` count their calls;
    - a device extension whose `SetShaderExtensionSlot` records its slot (7).

    The probe prints the counts and the slot, then calls `BeginUAVOverlap`/`EndUAVOverlap` on the real device and its
    immediate context.
  - **IDs:**
    - the 8 null-on-purpose IDs (`nvapi.cpp`'s "unknown functions"), 0xe3640a56 and 0x12345678 answer null;
    - the other 46 answer non-null;
    - `IsNvShaderExtnOpCodeSupported` is called with the three named opcodes and opcode 1 (its WARN line).
- [ ] **Step 2: Save the C++ DLLs** before anything replaces them: `build/wine-arm64/wine.app/.../nvapi64.dll` (the
  path `find build/wine-arm64/wine.app -name nvapi64.dll` prints) to `build/step1b/ref-mac/`, and
  `build/eac-vm-work/dxmt-guest/x86_64-windows/nvapi64.dll` (after `sh eac-vm/dxmt/build.sh`) to
  `build/step1b/ref-guest/`.
- [ ] **Step 3: Run on the C++.**
  - Build with `make dxmt-tests dxmt-tests-arm64ec`.
  - `build/step1b/probe.sh <dll> <mode> <out>` (uncommitted) runs one program as check.sh's `run` does, in check.sh's
    `ours` tool folder with its environment, MACNEUTRON_DLSS=1 and `DXMT_LOG_PATH` set to a folder of its own.
  - Run `fixed` and `all` twice each, against `C:\windows\system32\nvapi64.dll`.

  Expected: exit 0; the two runs of each mode are identical (any non-determinism in `all` is found now, not blamed on
  the port); the relations, round trip, counts and slot hold. `fixed`'s output becomes `d3d11_nvapi.fixed`.
- [ ] **Step 4: check.sh rows**, under MACNEUTRON_DLSS=1 (`run ours nvapi dxmt "$TESTS/d3d11_nvapi.exe"
  'C:\windows\system32\nvapi64.dll' fixed`):
  - "nvapi64 answers as DXMT's C++ did": the first line differing from `d3d11_nvapi.fixed`, or `same`;
  - the two log lines of Review Focus 5;
  - no `err:` or `warn:` line naming any of the 8 null IDs.

  Plus one run without MACNEUTRON_DLSS: `nvsys Initialize -6` (nvapi has no DllMain refusal). Run
  `sh dxmt/check.sh`. Expected: the new rows ok, every other row as before.
- [ ] **Step 5: Mutation.** Edit one value in `d3d11_nvapi.fixed`. Expected: that row FAILs, naming the line. Restore.
- [ ] **Step 6: Guest, on the C++.**
  - Add the invocation `d3d11_nvapi|d3d11_nvapi.exe|C:\windows\system32\nvapi64.dll fixed|nvsys nvgpu nvdisp nvd3d
    nvquery`.
  - Stage and image as `eac-vm/dxmt/gate.sh`'s header says.
  - Run `MN_DXMT_ONLY="d3d11_nvapi" sh eac-vm/dxmt/gate.sh`.

  Expected: ok. If a family differs in the guest on the C++, it leaves the invocation's prefixes, as a ledgered
  ruling naming the lines.
- [ ] **Step 7: Commit** (MacNeutron `dxmtrs`): `dxmt tests: d3d11_nvapi, every NVAPI answer DXMT's nvapi64 gives`.

### Task 2: dxmt-util, nvngx's shared half (DXMTRS)

**Files:**
- Create: `mtld3d/windows/dxmt-util/{Cargo.toml,src/lib.rs}` (LGPL-2.1-or-later, as nvngx). Move nvngx's `log.rs` (and `log/tests.rs`),
  `float_cast.rs` and the generic part of `com.rs` there, plus `nvext.rs` from nvngx's `lib.rs`.
- Modify: nvngx's `com.rs`, `lib.rs`, `Cargo.toml`, its users of `log`; `mtld3d/windows/Cargo.toml` (member);
  `mtld3d/docs/CONVENTIONS.md` (exception 9's path and text); `mtld3d/scripts/audit.sh:38`.

**Interfaces:**
- Produces:
  - `dxmt_util::log::Logger`: `pub const fn new(file: &'static str) -> Self`, `pub fn err(&self, &str)`,
    `pub fn warn(&self, &str)`. It opens its state on the first message (`Mutex<Option<State>>`), with
    `log.cpp`'s levels, prefixes, Wine output, and the file `<exe base>_<file>` under `DXMT_LOG_PATH`.
  - `dxmt_util::com::{Guid, Hresult, IUnknownVtbl, Object, query, release, vtbl}`, as nvngx's today.
  - `dxmt_util::float_cast::{f32_to_i32, f32_to_u32, f32_to_u16, f64_to_f32, u32_to_f32, i32_to_f32}`.
  - `dxmt_util::nvext::enabled() -> bool`: `DXGIGetDebugInterface1(0, &DXMT_NVEXT_GUID, null) >= 0`.
  - nvngx's `static LOG: Logger = Logger::new("nvngx.log")`.

- [ ] **Step 1: Failing tests.**
  - `log::tests::file_name_names_the_dll`: `file_name(Some("/tmp/x"), false, "C:\\game.exe", "nvapi.log") ==
    Some("/tmp/x/game_nvapi.log")`.
  - `float_cast::tests::f32_to_u16_truncates`: `f32_to_u16(0.708 * 50000.0) == 35400`, and
    `f32_to_u16(70000.0) == 65535`.

  Run `cargo test -p dxmt-util --target aarch64-apple-darwin`. Expected: FAIL (no crate yet, then no name parameter).
- [ ] **Step 2: Move and adapt.** nvngx's unit tests, clippy and audit pass for both crates (the `Expected:` of each:
  clean).
- [ ] **Step 3: nvngx unchanged in use.** `make wine-arm64 && sh dxmt/check.sh`. Expected: every `dlss`/`dlss11` row
  and Task 1's rows ok (nvapi64 is still C++).
- [ ] **Step 4: Commit:** `dxmt-util: nvngx's logger, COM helpers, float casts and NVEXT check, shared`.

### Task 3: nvapi as a stub crate (RED)

**Files:**
- Create: `mtld3d/windows/nvapi/{Cargo.toml,src/lib.rs}` (crate `nvapi`, `staticlib`, LGPL-2.1-or-later).
- Modify:
  - `src/nvapi/meson.build`: per-target `custom_target('nvapi-' + t)` as nvngx's;
    `shared_library('nvapi64', link_with: <them>, dependencies: [rust_runtime_dep, winemetal_dep], link_args: ['-ldxgi',
    '-luser32'], vs_module_defs: 'nvapi64.def', install_tag: 'nvext')`; the postprocess block kept; no C++ sources.
  - `mtld3d/windows/Cargo.toml` (member).

- [ ] **Step 1: Stub.** `nvapi_QueryInterface(id: u32) -> *mut c_void` answers null for every ID.
- [ ] **Step 2: Exports.** Run `make wine-arm64`, then `llvm-objdump -p` on the installed `nvapi64.dll`. Expected:
  `nvapi_QueryInterface` at ordinal 2 alone, as the C++'s.
- [ ] **Step 3: RED.** `sh dxmt/check.sh`. Expected:
  - the vendor row FAILs with `dlss vendor 10de nvapi -998`;
  - the `d3d11_nvapi` rows FAIL (every function `missing`; no log lines);
  - the DLSS rows and every other row as before.
- [ ] **Step 4: Commit:** `nvapi: a Rust crate behind nvapi_QueryInterface (stub)`.

### Task 4: NVAPI definitions and the ID table

**Files:** Create `mtld3d/windows/nvapi/src/{nv.rs,ffi.rs}` and `src/tests.rs`; modify `src/lib.rs`.

**Interfaces:**
- Produces:
  - **`nv.rs`:**
    - the statuses the C++ returns, as `i32`;
    - `const fn version(size: usize, v: u32) -> u32`, NVIDIA's `MAKE_NVAPI_VERSION`;
    - `#[repr(C)]` mirrors of every struct the 46 functions write, each with `const _: () = assert!(size_of == N)`;
    - `NVAPI_SDK_VERSION` = 57218.
  - **`ffi.rs`:**
    - winemetal's `WMTCopyAllDevices`, `NSArray_count`, `NSArray_object`, `MTLDevice_registryID`,
      `NSObject_release`, `WMTGetPrimaryDisplayId`, `WMTGetSecondaryDisplayId`, `WMTGetDisplayDescription`,
      `WMTQueryDisplaySetting` and `WMTUpdateDisplaySetting`, with `WMTDisplayDescription` and `WMTHDRMetadata`;
    - user32's `EnumDisplayMonitors` and `GetMonitorInfoW`.
  - **`lib.rs`:** `fn name_of(id: u32) -> Option<&'static str>`, which scans `include_str!` of
    `external/nvapi/nvapi_interface.h`. The unknown-ID log lines, as the C++'s.

- [ ] **Step 1: Facts.** A throwaway C++ file, built for `x86_64` and `arm64ec` with the tree's headers, prints:
  - `sizeof` of each struct version the functions accept, and the field offsets the Rust writes;
  - `ID3D11Device::GetImmediateContext`'s vtable index.

  Its numbers go into `nv.rs`'s asserts. Test: build the crate for `arm64ec-pc-windows-msvc` and
  `x86_64-pc-windows-msvc`. Expected: it builds (an assert fails the build on a wrong size).
- [ ] **Step 2: Failing tests** in `src/tests.rs`:
  - `name_of(0x0150e828) == Some("NvAPI_Initialize")`;
  - `name_of(0xe3640a56) == Some("NvAPI_GPU_GetThermalSettings")`;
  - `name_of(0x12345678) == None`;
  - the table holds 511 distinct IDs;
  - `version(size_of::<NvHdrCapabilitiesV3>(), 3)` equals the facts' `NV_HDR_CAPABILITIES_VER3`.

  Expected: FAIL.
- [ ] **Step 3: Implement; tests pass.** `sh dxmt/check.sh`. Expected: Review Focus 5's two log rows ok; the
  `fixed` row still FAILs.
- [ ] **Step 4: Commit:** `nvapi: NVAPI's structs, winemetal's display API and the ID names`.

### Task 5: system, GPU and stub functions

**Files:** Create `mtld3d/windows/nvapi/src/{sys.rs,gpu.rs}`; modify `src/lib.rs` (their IDs), `src/tests.rs`.

The 25 functions:
- `Initialize`, `Unload` (`INIT_COUNT`; Initialize answers -6 unless `dxmt_util::nvext::enabled()`);
- `SYS_GetDriverAndBranchVersion`, `GetDisplayDriverVersion`, `GetInterfaceVersionString`, `GetErrorMessage`;
- `EnumPhysicalGPUs`, `EnumLogicalGPUs`, `GPU_GetArchInfo`, `GPU_GetFullName`, `GPU_GetAdapterIdFromPhysicalGpu`,
  `GPU_GetLogicalGpuInfo`;
- the `NOT_SUPPORTED`/`NO_IMPLEMENTATION` GPU queries (`GetAllClockFrequencies`, `GetGpuCoreCount`, `GetBusId`,
  `GetBusType`, `GetMemoryInfo`, `GetMemoryInfoEx`, `GetPstates20`);
- `Mosaic_GetDisplayViewportsByResolution`, `Stereo_IsEnabled`, `Stereo_SetDriverMode`, `DRS_CreateSession`,
  `DRS_LoadSettings`.

Each does what `nvapi.cpp` does, with decision 2 applied.

- [ ] **Step 1: Failing tests** for the pure helpers:
  - `luid(0x0102_0304_0506_0708) == 0x0807_0605_0403_0201_u64.to_ne_bytes()`;
  - `short_string("r57218_000")` is NUL-terminated, with the rest of the 64 bytes untouched where the C++'s `memcpy`
    leaves them (the helper writes `len + 1` bytes into a given buffer).

  Expected: FAIL; then implement, PASS.
- [ ] **Step 2: GREEN for these families.** `sh dxmt/check.sh`.
  Expected:
  - the `fixed` row's first difference is a `nvdisp` or `nvd3d` line;
  - the vendor row ok again;
  - the DLSS rows ok.
- [ ] **Step 3: Commit:** `nvapi: system, GPU and stub functions in Rust`.

### Task 6: display and Direct3D functions (GREEN), and the C++ removed

**Files:** Create `mtld3d/windows/nvapi/src/{disp.rs,d3d.rs}`; modify `src/lib.rs`, `src/tests.rs`; delete
`src/nvapi/nvapi.cpp`.

The 21 functions:
- `GetPhysicalGPUsFromDisplay`, `DISP_GetDisplayIdByDisplayName`, `Disp_GetHdrCapabilities`, `Disp_HdrColorControl`,
  `DISP_GetMonitorCapabilities`, `EnumNvidiaDisplayHandle`, `GPU_GetConnectedDisplayIds`,
  `DISP_GetGDIPrimaryDisplayId`;
- `D3D_GetCurrentSLIState`, `D3D11_BeginUAVOverlap`, `D3D11_EndUAVOverlap`, `D3D11_SetDepthBoundsTest`,
  `D3D11_IsNvShaderExtnOpCodeSupported`, `D3D11_SetNvShaderExtnSlot`, `D3D_GetObjectHandleForResource`,
  `D3D_SetResourceHint`;
- the five Reflex calls and `D3D_SetVerticalSyncMode`.

The COM vtables come from `dxmt_util::com`, with IIDs from `d3d11_interfaces.hpp` and `ID3D11Device`'s.

- [ ] **Step 1: Failing tests** for the pure helpers:
  - `primaries(&WMTDisplayDescription)` scales by 50000 and luminance by 100, truncating as C++ does (a P3
    description's numbers, from Task 1's `all` dump of this Mac's display);
  - `display_count(primary, secondary)` gives 0, 1 or 2 as the C++'s ternary;
  - `hdr_mode(colorspace)` maps scRGB to UHDA, PQ to UHDA_PASSTHROUGH and anything else to OFF.

  Expected: FAIL; then implement, PASS.
- [ ] **Step 2: GREEN, both lanes.** `make dxmt-check` (`build_test.sh`'s two known `main` failures aside).
  Expected: the `fixed` row `same`, every nvapi row ok, and every other row as Task 1 Step 4's run.
- [ ] **Step 3: Differential.** `build/step1b/probe.sh` with `all`, run against `ref-mac/nvapi64.dll` (`Z:` path) and
  against `C:\windows\system32\nvapi64.dll`. Expected: `diff` of the two outputs is empty, and so is `diff` of the two
  `DXMT_LOG_PATH` folders' `d3d11_nvapi_nvapi.log`.
- [ ] **Step 4: Lint and audit.** Clippy for both crates, `scripts/audit.sh` filtered to them. Expected: clean.
- [ ] **Step 5: Commit:** `nvapi: display and Direct3D functions in Rust; the C++ goes`.

### Task 7: the guest, notices, the spec, and the push question

**Files (MacNeutron `dxmtrs`):** `wine-arm64/licenses/README` (DXMT's entry: nvngx.dll and nvapi64.dll are Rust),
`wine-arm64/tests/licences_test.sh`, `docs/superpowers/specs/2026-10-29-dxmtrs-design.md`.

- [ ] **Step 1: Notices**, test first. A `licences_test.sh` row requires the entry to name `nvapi64.dll` as Rust.
  FAIL; write the entry; PASS.
- [ ] **Step 2: Guest.**
  - Run `sh eac-vm/dxmt/build.sh`, then stage and image.
  - Run `MN_DXMT_ONLY="d3d11_nvapi d3d12_dlss_vendor d3d12_dlss_quality d3d12_dlss_performance d3d12_dlss_inverted
    d3d12_dlss_refuse d3d11_dlss" sh eac-vm/dxmt/gate.sh`.

  Expected: all 7 pass. This is the guest's first Rust `nvapi64.dll`.
- [ ] **Step 3: Spec.** Add the three decisions above, and step 1b as done in §5. Commit: `Spec: DXMTRS step 1b, nvapi
  in Rust`.
- [ ] **Step 4: STOP — ask the maintainer** to push `step1-nvngx` and `step1b-nvapi` to DXMTRS `main` (a
  fast-forward) and pin MacNeutron's `DXMT_COMMIT` to it. On a yes:
  - push;
  - set `DXMT_COMMIT`;
  - run `make wine-arm64` and the x64 lane once;
  - merge `dxmtrs` with the pin.
