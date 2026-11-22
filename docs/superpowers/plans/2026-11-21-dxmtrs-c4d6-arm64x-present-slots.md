# DXMTRS C4d-6: ARM64X Core Runtime & Presentation Slots (158..164)

## Goal

Close the final two structural gaps between DXMTRS's `d3d9.dll` core runtime and MacNeutron's VM and Wine/Native host stack:

1. **ARM64X Dual-Half Parity**: Build both the ARM64EC and native `aarch64` halves of `winemetal.dll` and core `d3d9.dll` with `core-runtime`, verifying dual-half exports/imports in `wine-arm64/bundle.sh` and native `aarch64` core execution in `dxmt/d3d9-check.sh`.
2. **Typed Presentation & Latency Slots (`158..=164`)**: Add `winemetal.dll` / `winemetal.so` Unix-call slots `158..=164`, wire them through DXMTRS (`winemetal` PE/Unix + `d3d9`), forward them cleanly in `vm/metal/wm/` (with guest-throttled steady-state polling and local VM-only answers where applicable), and cover every slot in `dxmt/tests/wm_probe/`.

## Architecture & Slot Summary

| Slot | Symbol | Wire Mode | Guest (VM) | Host / Native (`winemetal.so`) |
| --- | --- | --- | --- | --- |
| `158` | `WMTDeclareLatencyCriticalActivity` | `MN_WM_LOCAL` | Explicit `slot158`: sets `ret = 1`, returns `0` | Latches `NSProcessInfo` interactive latency-critical activity |
| `159` | `WMTAttachMetalView` | `MN_WM_SYNC` | Allocates `MN_HK_ATTACHED_VIEW` (+1) & `MN_HK_OBJC` layer (+1) | Attaches/reuses Metal view, configures `CAMetalLayer`, populates `WMTViewPollOut` |
| `160` | `WMTRetireMetalView` | `MN_WM_QUEUED` (`MN_SP_VIEW`) | Drops `161` cache entry, releases guest handle (`mn_gh_release`) | Deferred table release triggers `invoke.m` -> slot `160` with `park = 0` |
| `161` | `WMTViewSyncAndPoll` | `MN_WM_SYNC` | Cached/throttled in `guest/winemetal.c` (1st call, every 32nd call, or `WMTViewSyncFlagForceRefresh`) | Syncs `drawableSize`, polls headroom/occlusion/display metrics |
| `162` | `WMTPresentClientSurface` | `MN_WM_LOCAL` | Explicit `slot162`: local no-op (`0`) | Re-presents newest Win32 client surface on view's window |
| `163` | `MTLDrawable_setPresentedEvent` | `MN_WM_QUEUED` (`MN_SP_NONE`) | Translates `drawable`, optional `view` (`MN_HK_ATTACHED_VIEW`), `shared_event` | Signals `MTLSharedEvent` on presentation/discard and tracks paced presents |
| `164` | `WMTSetCursorOverlay` | `MN_WM_LOCAL` | Explicit `slot164`: local no-op (`0`) | Updates main-thread cursor overlay window/sprite |

## Key Invariants & Wire Layouts

- `MN_WM_SLOTS` and `wm_probe` `NSLOTS` advance from `158` to `165` (`0..=164`); WoW64 tables advance cleanly across all `165` entries (`W(158)..W(164)` in `vm/metal/wm/guest/wow64.c`, all `165` entries in `cover.sh`).
- Slot `83` remains `MN_WM_REFUSED` (`MN_STATUS_NOT_SUPPORTED`) / null in `slots.txt`.
- Exact C wire struct sizes verified in `vm/metal/wm/tests/marshal-test.m` against `winemetal.h` / `winemetal_thunks.h`:
  - `WMTViewAttachDesc`: 16 bytes
  - `WMTViewPollOut`: 32 bytes
  - `WMTCursorOverlayParams`: 64 bytes
  - `unixcall_wmt_attach_metal_view`: 48 bytes (slot `159`)
  - `unixcall_wmt_view_sync_and_poll`: 32 bytes (slot `161`)
  - `unixcall_mtldrawable_setpresentedevent`: 40 bytes (slot `163`)
  - Slots `158`, `160`, `162`, `164`: `unixcall_generic_obj_ret` (8 B), `unixcall_generic_obj_uint64_noret` (16 B), `unixcall_generic_obj_constptr_noret` (16 B).
- `MN_HK_ATTACHED_VIEW` isolates `WMTAttachMetalView` views from slot `72`'s `MN_HK_VIEW` lifecycle: `WMTRetireMetalView` (`160`) and context teardown (`mn_ht_free` after guest crash) both invoke slot `160` with `park = 0`, removing all views (`live == 0`).
- `MacdrvFunctionsTable` carries 10 fields including trailing `on_main_thread` (`dxmt/tests/d3d12_probe.cpp`, `vm/metal/wm/host/winemac.m`).

## Modified Deliverables (MacNeutron Repository)

- `wine-arm64/bundle.sh`: ARM64X verification + `production_asserts` extended to `winemetal.dll`; dual-half `WMTCopyAllDevices` export check; guarded dual-half `winemetal.dll` import check on core `d3d9.dll`.
- `dxmt/d3d9-check.sh`: native `aarch64` core verification (`loaded at .* as aarch64`, zero `mtld3d.so` lines, zero refusals in `refusals_ok`, optional `MTLD3D_PE/winemetal.dll` override).
- `dxmt/tests/d3d12_probe.cpp`: 10-field `macdrv_functions` table with trailing `nullptr`.
- `vm/metal/wm/`: `calls.h`, `handles.h`, `calls.c`, `marshal.c`, `host/present.m`, `host/memory.m`, `host/invoke.m`, `guest/winemetal.c`, `guest/wow64.c`, `tests/marshal-test.m`, `tests/memory-test.m`, `tests/window-test.m`.
- `dxmt/tests/wm_probe/`: `probe.h`, `probe.m`, `slots.txt`, `cover.sh`, `cases_g6.m`, `g6.fixed`, `g6.native`.

## Verification Gates (Run by Orchestrator)

- `make -C vm/metal/wm marshal-test memory-test window-test` (after DXMTRS install builds `winemetal.so`)
- `sh dxmt/tests/wm_probe/cover.sh slots`, `table`, `cases ... g6`, `bites ... g6`
- `sh dxmt/wm-probe-check.sh g6` and `sh dxmt/wm-probe-check.sh --native <dylib> g6`
- `sh wine-arm64/bundle.sh` and `D3D9_RUNTIME=core sh dxmt/d3d9-check.sh`
