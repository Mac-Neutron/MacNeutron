# 32-bit games in the VM, M4a: Direct3D on DXMT in the guest — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A 32-bit game's Direct3D 10/11/12 runs on DXMT built for i386 in the guest, through a WoW64 table in the guest
`winemetal.so`, in both prefix modes (forwarded prefixes fall back to the builtins for 32-bit processes since M2).

**Architecture:** DXMT's Unix-call params are width-neutral (`WMTMemoryPointer` is 8 bytes; `WMT_MEMPTR_SET` zeroes the
upper half on i386), so the WoW64 table reuses the 64-bit forwarders, except: the nine SM50 slots DXMT itself thunks
(74, 76, 77, 79, 81, 82, 84, 85, 88: 32-bit params and argument lists, converted to the 64-bit ones); memory the guest
hands to 32-bit code (galloc regions for slots 18, 138, 141 and 153; slot 77's bitcode copy), placed below 2 GiB in a
WoW64 process the way Wine's winevulkan places mappings (reserve with `NtAllocateVirtualMemory`, then `mmap(MAP_FIXED)`);
and slot 155 (Direct3D forwarding), refused for 32-bit callers until M4b.

**Tech Stack:** C (guest winemetal.so, builder's x86-64 sysroot compiler), DXMT's meson with `build-win32.txt` and
MacNeutron's llvm-mingw, POSIX sh, the microVM.

**Spec:** `docs/superpowers/specs/2026-10-20-macneutron-vm-32bit-games-design.md` §7 (guest mode).

## Global Constraints

- No gate per milestone: development runs only; the final gates run after M4b (spec §8).
- The host side does not change.
- DXMT is LGPL and this repository is MIT: the WoW64 conversions are our own code against DXMT's ABI (its `*_params32`
  structs in `airconv_thunks.h`, its 32-bit argument layouts), not copies of `winemetal_unix.c`.
- No NVIDIA DLLs for i386 (`nvapi64`/`nvngx` are x64 only; NGX has no 32-bit runtime).
- `dxmt` stage builds need `MACNEUTRON_SIGN_IDENTITY` exported.

## Review Focus

- Any pointer the guest `winemetal.so` writes for 32-bit code (galloc regions, bitcode) must be below 4 GiB, else DXMT's
  i386 `get()` asserts on the high half: dev run of a 32-bit D3D11 program that creates buffers and compiles shaders.
- A 32-bit SM50 argument list (every argument type DXMT defines) converts field for field: the unit test.
- A WoW64 process never mixes: the switch to low placement happens at its first WoW64 call, before any allocation for
  32-bit code.
- 64-bit processes are unchanged (the 64-bit table and allocations untouched): the 64-bit smoke at the final gates.

---

### Task 1: The guest winemetal.so's WoW64 table and low memory

**Files:**
- Create: `eac-vm/metal/wm/guest/wow64.c` (the WoW64 table's SM50 entries and the argument-list conversion)
- Modify: `eac-vm/metal/wm/guest/winemetal.c` (the table), `eac-vm/metal/guest/transport.c` + `transport.h` (low
  blobs), `eac-vm/metal/wm/guest/galloc.c` (low blobs in a WoW64 process), `eac-vm/metal/wm/slots.c` (slot 77's buffer),
  `eac-vm/metal/build-guest.sh` (wow64.c), a unit test in `eac-vm/metal/wm/tests/`

**Interfaces:**
- Produces: `extern int mn_wm_wow64;` (set by every WoW64 entry before it forwards); `int mn_blob_alloc_low(uint64_t
  size, struct mn_blob *b);` (the blob mapped `MAP_FIXED` into a range `NtAllocateVirtualMemory` reserved and committed
  with zero-bits mask `0x7fffffff`; `struct mn_blob` gains `uint8_t low`, and its close releases the range with
  `NtFreeVirtualMemory(MEM_RELEASE)` instead of `munmap`); `void *mn_wm_low_alloc(size_t n); void mn_wm_low_free(void
  *p);` (the same placement for slot 77's copy); `struct SM50_SHADER_COMPILATION_ARGUMENT_DATA
  *mn_wm_sm50_args32(uint32_t args32);` returning a malloc'd 64-bit chain (NULL for none) and `void
  mn_wm_sm50_args_free(struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *)`. `NtAllocateVirtualMemory`/`NtFreeVirtualMemory` are declared weak (the test
  binaries link no ntdll.so): missing, low placement fails with `-ENOSYS`.
- The WoW64 table: `const unixlib_entry_t __wine_unix_call_wow64_funcs[MN_WM_D3D + 1]`, entry `n` = set `mn_wm_wow64`,
  then `forward(n, p)`, except slots 74, 76, 77, 79, 81, 82, 84, 85, 88 (convert `struct sm50_*_params32` to the
  64-bit params, call the 64-bit entry, copy `ret`/`ret_size` back, free the list), 153/154 (their 64-bit entries,
  after setting the flag) and 155 (`MN_STATUS_NOT_IMPLEMENTED` until M4b).

- [ ] **Step 1: Write the failing unit test** (`wm/tests/sm50-args32-test.c`, built and run in the builder like the
  existing wm tests): a 32-bit chain in memory below 4 GiB (`mmap` at `0x10000000`) holding one argument of each type
  (`SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT`, `COMMON`, `PSO_PIXEL_SHADER`, `IA_INPUT_LAYOUT`, `GS_PASS_THROUGH`,
  `PSO_GEOMETRY_SHADER`, `PSO_TESSELLATOR`, `ROOT_SIGNATURE`, `ROOT_SIGNATURE2`) converts to a 64-bit chain of the same
  length and order with every field equal and every 32-bit pointer zero-extended; an empty chain gives NULL.
- [ ] **Step 2: Run it.** Expected: fails to link (`mn_wm_sm50_args32` undefined).
- [ ] **Step 3: Implement** wow64.c, the table, low blobs, galloc's switch (`mn_wm_wow64` → `mn_blob_alloc_low` for
  region blobs; the notify queue blob stays ordinary) and slot 77's buffer (`mn_wm_low_alloc` when `mn_wm_wow64`, freed
  by the same rule in `release_pack`).
- [ ] **Step 4: Run** the unit test and the existing wm tests in the builder, and `sh eac-vm/metal/build-guest.sh`
  (winemetal.so builds and links). Expected: pass; builds.
- [ ] **Step 5: Commit** — `git commit -m "metal guest: winemetal.so's WoW64 table, SM50's 32-bit arguments and memory below 4 GiB for 32-bit DXMT"`

### Task 2: DXMT for i386 in wine.erofs

**Files:**
- Modify: `eac-vm/dxmt/build.sh` (a second build, `build-win32.txt`, into the same prefix's `i386-windows`; the guest
  SDK's i386 import libraries), `eac-vm/dxmt/check-build.sh` (i386 rows: PE32 i386, builtin marker), `eac-vm/dxmt/stage.sh`
  (`i386-windows/{d3d10core,d3d11,d3d12,dxgi,winemetal}.dll`), `eac-vm/image/build.sh` (`wine_image` installs them over
  Wine's i386 DLLs)

- [ ] **Step 1: Implement**: the i386 build uses `$W/dxmt-guest-build32`, `-Denable_nvapi=false -Denable_nvngx=false`,
  the rest of the 64-bit options, its own `.setup-inputs`; installs without removing the x86-64 tree; SDK libs from the
  builder's `wine-stage/opt/mn-wine/lib/wine/i386-windows`.
- [ ] **Step 2: Build** — `guest-stages.sh dxmt d3d` (signing variables exported), `image/build.sh wine` in the builder.
  Expected: exit 0; check-build passes for both; `wine.erofs`'s `i386-windows/d3d11.dll` imports `winemetal.dll`.
- [ ] **Step 3: Commit** — `git commit -m "eac-vm dxmt: DXMT for i386 in wine.erofs (32-bit Direct3D on DXMT in the guest)"`

### Task 3: A 32-bit Direct3D program in the VM

**Files:**
- Modify: `eac-vm/tests/d3d-smoke.sh` (`smoke32.exe`, built for i686, run beside `smoke.exe` in each mode with the same
  checks, rows prefixed `32:`), the root `Makefile` (`dxmt-tests32`: DXMT's d3d11/d3d10 tests for i686 as `*32.exe`)

- [ ] **Step 1: Build** smoke32.exe and the i686 tests. Expected: PE32 executables.
- [ ] **Step 2: Development run** — `d3d-smoke.sh` (both modes). Expected: the 64-bit rows as before; the `32:` rows the
  same as the 64-bit ones in guest mode, and in forwarded mode (a 32-bit process on the builtin i386 DXMT).
  Record the rows. If DXMT asserts on a high pointer, the address in its log names the source: fix in Task 1's code.
- [ ] **Step 3: Commit** — `git commit -m "eac-vm tests: 32-bit Direct3D programs (smoke32.exe, DXMT's tests for i686) in both modes"`
