# MetalFX upscaling acceptance

Historical: the Rosetta runtime was removed in 0.1.0; reproduce with the frozen reference (`tools/freeze-rosetta-reference.sh`).

Spec: `docs/superpowers/specs/2026-09-28-macneutron-metalfx-design.md`.

## Feasibility gate (plan task 1), 2026-09-28: stopped

- GPTK 4.0b2 (D3DMetal), runtime runtime-v4.7.3, macOS 27.0, SMITE 2 (Unreal Engine 5.5, DLSS-SR plugin 4.0.0 with NGX SDK 3.10.1, Streamline 2.7.3).
- Wired by hand: runtime `nvngx.dll` (Apple's `nvngx-on-metalfx.dll` under its export name) and `nvngx.so` link; SMITE 2's prefix already had `system32\nvngx.dll` (a Wine placeholder `wineboot` made from the bridge's export name) and `nvapi64.dll`.
- Run 1, `D3DM_ENABLE_METALFX=1 D3DM_MTL4=1`: `nvngx.dll` and `nvapi64.dll` load as builtins, but Unreal found adapter "AMD Compatibility Mode" (VendorId 1002: D3DMetal's default), so no DLSS option.
- Run 2, plus `D3DM_VENDOR_ID=0x10DE D3DM_DEVICE_ID=0x2206`: the adapter becomes VendorId 10de / DeviceId 2206 (D3DMetal honours both). Unreal's DLSS module starts and finds `nvngx_dlss.dll`. Then the NGX SDK fails to load NGX core: it tries `_nvngx.dll`/`nvngx.dll` in `Hemingway\Binaries\Win64`, then logs "failed to locate NGX core path via registry key - error 2". `NVSDK_NGX_D3D12_Init_with_ProjectID` → `NVSDK_NGX_Result_FAIL_FeatureNotSupported`. Streamline: "NVAPI failed to initialize" (Apple's `nvapi64` loads but `NvAPI_Initialize` fails), no NGX context.
- Run 3, plus `HKLM\SOFTWARE\NVIDIA Corporation\Global\NGXCore\FullPath=C:\windows\system32`: unchanged. This NGX SDK doesn't use that key; it asks the driver through D3DKMT, which Wine leaves unhandled (`NtGdiDdDDIQueryAdapterInfo type 70 not handled`).
  - Corrected 2026-10-12 (DLSS spike, Palworld): the reading above is wrong. NVIDIA's published loader (`nvsdk_ngx_loader.h`, NVIDIA/DLSS) tries the EXE folder, then D3DKMT QueryAdapterInfo type 48 (`KMTQAITYPE_QUERYREGISTRY`, which Wine leaves unimplemented), then the `nvlddmkm` NGXPath keys, then this `FullPath` key; its "error 2" means the value was never found where the game looked, likely written to the wrong place on GPTK's Wine. "Type 70" (`KMTQAITYPE_WDDM_2_7_CAPS`) is Streamline's hardware-scheduling check, not the core lookup.
- Not tried: a copy of the bridge in the game's own `Hemingway\Binaries\Win64` (writes into the game install); D3DMetal's undocumented `D3DM_NVNGX_PATH`.
- Decision: stop (maintainer chose the spatial upscaler instead). The hand-installed runtime files and the registry key were removed; SMITE 2's test launch options are reverted separately.
- Useful findings: D3DMetal reads `D3DM_VENDOR_ID`, `D3DM_DEVICE_ID`, `D3DM_DEVICE_DESCRIPTION`, `D3DM_NVNGX_PATH`, `D3DM_MAX_FPS`, `D3DM_LOD_BIAS`, `D3DM_SUPPORT_DXR`, `D3DM_SHOW_HUD_STATS`, `D3DM_MTL4` and `D3DM_ENABLE_METALFX` (strings in `D3DMetal.framework`). SMITE 2 also ships vendor-neutral temporal upscalers, FSR 3 and XeSS, which run on D3DMetal without any of this.
