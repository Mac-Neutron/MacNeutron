# DXMTRS Step 6: Replay Port & C++ Tree Retirement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete Step 6 of the DXMTRS migration (`docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5 Step 6, §6, §8): port `dxmt-replay` (`dxmt-replay.exe` and macOS native `dxmt-replay`) to Rust, delete `d3d12_pipeline_record.{hpp,cpp}`, retire `sm50_wow64.c` and `winemetal_thunks.c` into Rust, delete `libs/DXBCParser/` and all unreferenced `src/util/` and `include/` C/C++ files while preserving the Spec §6 external consumer header closure, and verify all Rust, Wine, Native, and VM gates.

**Architecture:**
1. **`dxmt-replay` (`src/replay/`):** Rust crate `dxmt-replay` (`crate-type = ["staticlib", "rlib"]`) exporting `wmain` on Windows PE and `main` on macOS native. Dynamically loads `d3d12.dll` / `@executable_path/d3d12.dylib` *after* setting `DXMT_CACHE_EXE=<stem>` and clearing `DXMT_PIPELINE_RECORD`, parses `DXMTPRC1` `.pipelines` streams (identical format to `dxmt-d3d12::pipeline::record`), and replays root signatures, graphics PSOs, and compute PSOs via `ID3D12Device` COM vtable calls.
2. **WoW64 SM50 & Native Winemetal Thunks (`src/winemetal/`):** Move the 9 `thunk32_SM50*` functions and recursive `sm50_compilation_argument32_convert` from `src/winemetal/unix/sm50_wow64.c` into `src/winemetal/unix/src/g10.rs` (`#[cfg(not(feature = "native"))]`). Enable `src/winemetal/src/lib.rs` (`winemetal-pe`) on non-Windows targets to dispatch directly through `__wine_unix_call_funcs` and link it into `src/nativemetal/meson.build`, retiring `src/winemetal/winemetal_thunks.c` and updating `tools/winemetal-layouts.sh`. Keep `src/winemetal/airconv_thunks.{c,h}` intact so `translator_key` (`6c9fa750ccc52b6e052aee6f867b869495575c7382258a01bb2c53760006dbe4`) does not turn.
3. **C++ Tree Retirement & Meson Cleanup:** Remove `libs/DXBCParser/`, `libs/meson.build`, `src/util/meson.build`, `src/dxmt/meson.build`, `src/d3d12/d3d12_pipeline_record.{hpp,cpp}`, `include/{adt.hpp,ftl.hpp,tl/generator.hpp}`, and all unreferenced `src/util/` files, retaining only the Spec §6 external header/source closure required by `eac-vm/d3d`, `eac-vm/dxmt-native`, and `src/nativemetal`.

**Tech Stack:** Rust (`edition = "2024"`), Meson, Clang/LLVM (`air-23`), Cargo (`aarch64-pc-windows-msvc`, `arm64ec-pc-windows-msvc`, `x86_64-pc-windows-msvc`, `aarch64-apple-darwin`).

---

### Task 1: Port `dxmt-replay` to Rust and delete `d3d12_pipeline_record.{hpp,cpp}`

**Files:**
- Create: `src/replay/Cargo.toml`
- Create: `src/replay/src/lib.rs`
- Modify: `Cargo.toml`
- Modify: `src/replay/meson.build`
- Modify: `tools/rust-check.sh`
- Delete: `src/replay/dxmt_replay.cpp`
- Delete: `src/replay/native_main.cpp`
- Delete: `src/d3d12/d3d12_pipeline_record.hpp`
- Delete: `src/d3d12/d3d12_pipeline_record.cpp`

- [ ] **Step 1: Create `src/replay/Cargo.toml` and `src/replay/src/lib.rs`**
  - Implement `DXMTPRC1` record header & payload parsing (`RecordKind::Graphics = 1`, `RecordKind::Compute = 2`, FNV-1a checksum verification, bounds-checked `Reader` matching `dxmt-d3d12::pipeline::record::Writer`).
  - Implement `D3D12_GRAPHICS_PIPELINE_STATE_DESC` and `D3D12_COMPUTE_PIPELINE_STATE_DESC` C-ABI layout structs matching `src/d3d12/src/com/desc.rs` and `d3d12.h`.
  - Implement dynamic `D3D12CreateDevice` loader (`LoadLibraryW` + `GetProcAddress` on Windows after `_wputenv_s`; `dlopen` + `dlsym` on Unix after `setenv`/`unsetenv`).
  - Export `wmain` (`#[cfg(windows)]`) and `main` (`#[cfg(not(windows))]`).
  - Add unit tests in `src/replay/src/lib.rs` covering `DXMTPRC1` header validation, checksum mismatch detection, truncation handling, and graphics/compute payload deserialization round-tripping.

- [ ] **Step 2: Wire `dxmt-replay` into `Cargo.toml`, `src/replay/meson.build`, and `tools/rust-check.sh`**
  - Add `"src/replay"` to `Cargo.toml` `members` and `default-members`.
  - Add `-p dxmt-replay` to `PE_PACKAGES` in `tools/rust-check.sh`.
  - Update `src/replay/meson.build` to build `libdxmt_replay.a` via `cargo_staticlib` (`rust_pe_targets` when `not dxmt_native`, `rust_native_target` when `dxmt_native`) and link `executable('dxmt-replay', ...)` with `rust_runtime_dep` (and `-municode` on PE, `-Wl,-dead_strip` on native).
  - Delete `src/replay/dxmt_replay.cpp`, `src/replay/native_main.cpp`, `src/d3d12/d3d12_pipeline_record.hpp`, and `src/d3d12/d3d12_pipeline_record.cpp`.

---

### Task 2: Retire `sm50_wow64.c` and `winemetal_thunks.c` into Rust

**Files:**
- Modify: `src/winemetal/unix/src/g10.rs`
- Modify: `src/winemetal/unix/meson.build`
- Delete: `src/winemetal/unix/sm50_wow64.c`
- Modify: `src/winemetal/src/lib.rs`
- Modify: `src/nativemetal/meson.build`
- Modify: `tools/winemetal-layouts.sh`
- Delete: `src/winemetal/winemetal_thunks.c`

- [ ] **Step 1: Port `sm50_wow64.c` into `src/winemetal/unix/src/g10.rs`**
  - Define the 32-bit WoW64 SM50 ABI structs (`SM50_SHADER_COMMON_DATA_ARGS32`, `SM50_SHADER_COMPILATION_ARGUMENT_DATA32`, `SM50_SHADER_IA_INPUT_LAYOUT_DATA32`, `SM50_SHADER_PSO_GEOMETRY_SHADER_DATA32`, `SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA32`, `SM50_SHADER_DEBUG_IDENTITY_DATA32`, and the 9 ` wmt_sm50_*_params32` parameter structs) with `#[repr(C)]` and compile-time size assertions.
  - Implement `sm50_compilation_argument32_convert` and the 9 `#[unsafe(no_mangle)] pub unsafe extern "C" fn thunk32_SM50*` functions under `#[cfg(not(feature = "native"))]`.
  - Remove `'sm50_wow64.c'` from `src/winemetal/unix/meson.build` and delete `src/winemetal/unix/sm50_wow64.c`.

- [ ] **Step 2: Enable `winemetal-pe` (`src/winemetal/src/lib.rs`) for `nativemetal` and delete `src/winemetal/winemetal_thunks.c`**
  - In `src/winemetal/src/lib.rs`, support `#[cfg(not(windows))]` in `unix_call` by calling `__wine_unix_call_funcs[code as usize](args)` (matching `winemetal_thunks.c`), gating `DllMain` / `__wine_unix_call_dispatcher` / `load_so_dll` on `#[cfg(windows)]`.
  - In `src/nativemetal/meson.build`, build `libwinemetal_pe.a` via `cargo_staticlib` for `rust_native_target` and link it into `winemetal.dylib` in place of `../winemetal/winemetal_thunks.c`.
  - Update `tools/winemetal-layouts.sh` to verify the 158 slot numbers against `src/winemetal/src/lib.rs` (`pub mod slot` and `unix_call(slot::*, ...)`).
  - Delete `src/winemetal/winemetal_thunks.c`.

---

### Task 3: Delete `libs/DXBCParser/`, `src/util/meson.build`, and unreferenced C/C++ files

**Files:**
- Delete: `libs/DXBCParser/*`, `libs/meson.build`
- Delete: `src/util/meson.build`, `src/dxmt/meson.build`, `src/d3d10/d3d10.sym`
- Delete: unreferenced `src/util/**` and `include/{adt.hpp,ftl.hpp,tl/generator.hpp}`
- Modify: `meson.build`, `src/meson.build`, `tools/audit.sh`

- [ ] **Step 1: Delete unreferenced C/C++ trees and headers while keeping Spec §6 closure**
  - Delete `libs/` (`libs/DXBCParser/*`, `libs/meson.build`).
  - Delete `src/util/meson.build`, `src/dxmt/meson.build`, `src/d3d10/d3d10.sym`.
  - Delete `include/adt.hpp`, `include/ftl.hpp`, `include/tl/generator.hpp`.
  - Delete all files in `src/util/` EXCEPT:
    - `src/util/com/com_guid.hpp`
    - `src/util/win32_native.h`
    - `src/util/win32_native_event.h`
    - `src/util/wsi_window.hpp`
    - `src/util/wsi_monitor.hpp`
    - `src/util/wsi_platform_win32.hpp`
    - `src/util/util_d3dkmt.h`
    - `src/util/wsi_window_win32.cpp`
    - `src/util/wsi_monitor_win32.cpp`
    - `src/util/rust_pe_rt.c`
  - Clean up `meson.build` (remove `'./libs'` from `dxmt_include_path`, remove `subdir('libs')`) and `src/meson.build` (remove `subdir('util')` and `subdir('dxmt')`).
  - Update `tools/audit.sh` to also assert that no `.cpp` files exist in `src/` outside the Spec §6 retained files (`src/nativemetal/win32_native.cpp`, `src/util/wsi_window_win32.cpp`, `src/util/wsi_monitor_win32.cpp`, `src/airconv/airconv_bridge.cpp`).

---

### Task 4: Verify all Rust, Wine, Native, and VM Gates & Finalize Spec/Program Ledgers

- [ ] **Step 1: Run `tools/rust-check.sh` in the Step 6 worktree**
  - Verify `fmt`, `gen` (`tools/winemetal-layouts.sh`), `clippy`, `doc`, `test`, and `audit` all pass with 0 warnings.
- [ ] **Step 2: Run full MacNeutron pipeline gates under `build/pipeline.lock`**
  - Verify `translator_key` is unchanged (`6c9fa750ccc52b6e052aee6f867b869495575c7382258a01bb2c53760006dbe4`).
  - Build `make wine-arm64`, verify `ident-diff.py`, `dxmt/check.sh` (`x64` and `arm64ec`), `wine-arm64/check.sh`, and `dxmt/wm-probe-check.sh`.
  - Build and verify `eac-vm/dxmt-native/build.sh` + `eac-vm/dxmt-native/gate.sh` (including loopback and `dxmt-replay` binary check).
  - Build and verify `eac-vm/dxmt/build.sh`, `eac-vm/dxmt/check-build.sh`, `eac-vm/d3d/gate.sh`, and `eac-vm/dxmt/gate.sh`.
- [ ] **Step 3: Update Spec & Program Ledger, Push `DXMTRS` and `MacNeutron`**
  - Update `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` and `.superpowers/sdd/dxmtrs-program.md` marking Step 6 and the DXMTRS program complete.
  - Fast-forward `DXMTRS` `main`, pin `DXMT_COMMIT` in `MacNeutron`, run final verification, and push `DXMTRS` and `MacNeutron`.
