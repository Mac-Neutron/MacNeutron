# 32-bit games in the VM, M4b: Direct3D forwarding for 32-bit games — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A 32-bit process's Direct3D 10/11 and DXGI calls reach the host's native DXMT through i386 forwarding proxies,
over the existing 64-bit wire, the host unchanged.

**Architecture:** The i386 proxies are the generated ones (`proxies.cpp`, `proxy.cpp`) compiled for i686: they already
write every pointer and handle as a 64-bit params member. What differs is the guest structs they point at: the census
(2026-10-20, gen.py's own model with i386 sizes) finds 78 of 928 forwarded calls carrying one of 57 structs whose i386
layout differs (pointers, `SIZE_T`, `HWND`/`HMONITOR`, by-value descriptor handles). Those are converted on the guest's
Unix side (64-bit code, winemetal.so's WoW64 slot 155) before the existing marshaller packs them, and back for outputs,
by converters gen.py generates from its two layouts. D3D12 from a 32-bit process needs more than the generated
converters (its pipeline-state stream lays subobjects out at the pointer size; root signatures and state objects
nest unions of pointer arrays; CreatePipelineState is a hand-written special): until that stage exists, 32-bit
forwarding is opt-in (`MACNEUTRON_VM_D3D32=forward`) and the default stays M4a's (32-bit processes on guest DXMT in both
modes), so no 32-bit D3D12 game regresses.

**Tech Stack:** Python 3 (eac-vm/d3d/gen.py), C (guest unix side), C++ (proxies, i686 llvm-mingw), POSIX sh.

**Spec:** `docs/superpowers/specs/2026-10-20-macneutron-vm-32bit-games-design.md` §7 (forwarded mode).

## Global Constraints

- The host's stubs and the wire don't change (spec §7). The 64-bit tables gen.py writes stay byte-identical.
- No gate per milestone: development runs only; the final gates run after this plan (spec §8).
- 32-bit forwarding is opt-in until D3D12's converters exist (a ruling against spec §7's "forwarded mode", recorded in
  the ledger with its reason).

## Review Focus

- A struct converted in must come back converted out for every out/inout pointer (descs filled by Get* calls).
- Nested pointers (an input layout's semantic names, subresource data's `pSysMem`) stay valid: the converted copies
  live until the call returns.
- Memory the Unix side hands back to 32-bit code (map addresses, the fence mirror) is below 4 GiB.
- 64-bit processes take none of this path (their slot 155 is the 64-bit table's).

---

### Task 1: gen.py's i386 layouts and generated converters

**Files:**
- Modify: `eac-vm/d3d/gen.py` (pointer size threaded through `size_align`/`struct_layout`; `calls` also writes
  `convert32.c`), `eac-vm/metal/build-guest.sh` (compiles it into winemetal.so)
- Create: `eac-vm/d3d/tests/convert32-test.c` (generated cases in `convert32-cases.c`)

**Interfaces:**
- Produces in `convert32.c`: per i386-different struct `S`, `void mn_c32_in_S(const uint8_t *src32, uint8_t *dst64,
  struct mn_c32_arena *a)` and `void mn_c32_out_S(const uint8_t *src64, uint8_t *dst32)`; `const struct mn_c32_call
  mn_c32_calls[MN_D3D_CALLS]` (per call: its params members that point at, or hold by value, an i386-different struct:
  member offset, struct, by value / one / array with its count member and width, in / out / inout).
- `struct mn_c32_arena` (malloc'd blocks freed after the call).

- [ ] Step 1: failing test — `convert32-test.c` runs gen.py's generated cases: for each struct, i386 bytes with a
  distinct value per field (pointer-sized fields 32-bit values with the top bit set where signed), converted in, every
  field read back at its x86-64 offset (zero- or sign-extended), and out again equal to the input.
- [ ] Step 2: run it in the builder. Expected: fails to build (no convert32.c).
- [ ] Step 3: implement; the 64-bit outputs compared byte for byte with the previous run's.
- [ ] Step 4: run it. Expected: every struct ok.
- [ ] Step 5: commit — `git commit -m "d3d gen.py: i386 layouts and the guest's 32-bit struct converters"`

### Task 2: Slot 155 for 32-bit callers

**Files:**
- Modify: `eac-vm/metal/wm/guest/wow64.c` (slot 155), `eac-vm/d3d/guest/unix.c` (`mn_d3d_unix_call32`; the fence
  mirror below 4 GiB in a WoW64 process)

- [ ] Steps: `mn_d3d_unix_call32(void *args)` converts the call's listed members in (pointers swapped to the arena's
  copies, by-value members rewritten in place), calls `mn_d3d_unix_call`, converts outputs back and restores the
  pointers; the mirror blob through `mn_blob_alloc_low` when `mn_wm_wow64`. Test: Task 3's dev run (it needs the
  proxies). Commit.

### Task 3: i386 proxies, opt-in

**Files:**
- Modify: `eac-vm/d3d/build-guest.sh` (i686 builds of mnd3d.dll and its stand-ins into `d3d-guest/i386-windows`, linked
  against the i386 `winemetal.dll.a`), `eac-vm/d3d/guest/prefix.sh` (with `MACNEUTRON_VM_D3D=forward` and
  `MACNEUTRON_VM_D3D32=forward`, the i386 set linked into `syswow64`), `eac-vm/image/build.sh` (installs
  `mn-d3d/i386-windows`), `Sources/MacNeutronCore/MicroVM.swift` (`passed` gains `MACNEUTRON_VM_D3D32`),
  `eac-vm/tests/d3d-smoke.sh` (smoke32 forwarded when opted in)
- [ ] Steps: build; fix what the hand-written guest code assumes about 64-bit; dev run of d3d-smoke with the opt-in:
  smoke32's rows in forwarded mode through the host (its guest trace shows its calls). Commit.

### Task 4 (later stage): D3D12 for 32-bit processes

Converters for the pipeline-state stream, versioned root signatures and state objects, the specials' i386 paths; then
32-bit forwarding becomes the default with `MACNEUTRON_VM_D3D=forward`. Its own plan when Tasks 1-3 are in.
