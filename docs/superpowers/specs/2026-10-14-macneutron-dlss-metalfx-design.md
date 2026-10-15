# MacNeutron: DLSS answered by MetalFX

Status: draft for review (2026-10-14). Implementation started the same day (§8); the decisions in §3 were made by the
implementer and are the maintainer's to confirm or overturn.

## 1. Goal

When a Direct3D 12 game asks for NVIDIA DLSS super resolution on MacNeutron's arm64 runtime, it gets MetalFX's temporal
upscaler, under DLSS's name, the way XeSS already does (2026-10-05 spec). The first target is Palworld (Unreal Engine
5, NVIDIA's DLSS plugin, `nvngx_dlss.dll` in `Pal/Plugins/DLSS`).

## 2. Why this and not the translator's own temporal upscale

The 2026-10-13/14 experiment upscaled at present time in DXMT, from depth and velocity textures it guessed out of the
game's resources (Palworld). It is removed. What it showed:

- Unreal's transient allocator places scene depth and velocity in heap memory that later passes of the same frame
  reuse; at the present they hold other pixels (Palworld's depth read back sane for a few seconds, then garbage).
  Copying them where a barrier ended their writes fixed that, but:
- Unreal's velocity texture leaves static geometry out (70-80 % zero while moving): its own TAA/TSR rebuilds camera
  motion from depth and the view matrices, which the translator never sees. MetalFX got no motion for the world, which
  is the blur and blockiness the maintainer saw.
- The colour it upscaled already had post-processing and UI in it.

A game's DLSS path has none of these problems: Unreal's DLSS plugin hands NGX dilated motion vectors with camera motion
in them, the jitter, the pre-UI colour and its own reset flags, at the right point in the frame.

## 3. Decisions (implementer, 2026-10-14; to confirm)

- **D1. Upstream DXMT's NVIDIA extension, not a new one.** DXMT already has `nvapi64.dll`, a D3D11 `nvngx.dll` on
  MetalFX and `DXMT_ENABLE_NVEXT=1`, which reports vendor 0x10DE and writes the registry keys NVIDIA's NGX loader reads
  for the core's path (`HKLM\SOFTWARE\NVIDIA Corporation\Global\NGXCore\FullPath` = `C:\Windows\System32`). The new
  work is NGX's D3D12 exports on DXMT patch 0004's private interface (the one libxess uses). DXMT patch 0065.
- **D2. Opt-in per game** (`MACNEUTRON_DLSS=1`, "DLSS" in the Games window), unlike XeSS's silent default: the game is
  told the GPU is NVIDIA's, which changes more than its upscaler menu (NVAPI paths, vendor-specific code in engines,
  Streamline's plugins). Default-on is for after Palworld and one more game have run clean.
- **D3. Super resolution only.** Frame generation (feature 11, Streamline's `sl.dlss_g`), ray reconstruction and the
  rest are refused at CreateFeature and reported unsupported by GetFeatureRequirements.
- **D4. Native DLLs in system32**: `nvapi64.dll` and `nvngx.dll` join the four DXMT DLLs every prefix gets. Without
  `DXMT_ENABLE_NVEXT`, `nvngx.dll` refuses to load and `NvAPI_Initialize` reports no NVIDIA device, so games see what
  they see today.

## 4. Architecture

1. **DXMT patch 0065, `src/nvngx/nvngx_d3d12.cpp`.** NGX's D3D12 exports: the four Init forms (succeed where the device
   has `IMTLD3D12DeviceExt`), Shutdown/Shutdown1, GetCapabilityParameters/GetParameters/AllocateParameters/
   PopulateParameters_Impl/DestroyParameters (the capability map shared with D3D11: SuperSampling.Available, the
   optimal-settings callback with DLSS's ratios), GetScratchBufferSize (0), CreateFeature, EvaluateFeature,
   ReleaseFeature, GetFeatureRequirements. `nvngx.dll` installs natively when DXMT isn't a Wine builtin.
2. **wine-arm64**: DXMT built with `-Denable_nvapi=true -Denable_nvngx=true`; `bundle.sh` ships both DLLs in
   `DXMT/aarch64-windows`; NOTICES carries NVAPI's headers' MIT notice.
3. **Launcher**: `ToolLayout.dxmtDLLs` adds both; `MACNEUTRON_DLSS=1` sets `DXMT_ENABLE_NVEXT=1` on DXMT (a user's own
   value wins); `GameSettings.dlss` and the Games window toggle set `MACNEUTRON_DLSS`.

## 5. Per frame

- `CreateFeature(list, SuperSampling, params)`: Width/Height (render), OutWidth/OutHeight, PerfQualityValue,
  DLSS.Feature.Create.Flags; refused outside MetalFX's 1.0x-3.0x. Logs one `macneutron-dlss: DLSS <w>x<h> to <W>x<H>`.
- `EvaluateFeature(list, handle, params)`: Color, Output, Depth, MotionVectors (required), ExposureTexture (unless
  AutoExposure), the subrect bases (`DLSS.Input.Color.Subrect.Base.X/Y`, Depth, `DLSS.Input.MV.SubrectBase.X/Y`,
  `DLSS.Output.Subrect.Base.X/Y`), `DLSS.Render.Subrect.Dimensions.Width/Height` (the feature's size when absent),
  Jitter.Offset, MV.Scale, DLSS.Pre.Exposure (1.0 when absent or not positive), Reset. Flags: MVLowRes off means
  output-resolution motion vectors; MVJittered; DepthInverted; AutoExposure. The feature's upscaler is made at its
  first call from the textures' formats and again when they change, sized to what the textures hold (dynamic
  resolution moves only the content size; XeSS's rule). One `TemporalUpscale` into the game's list.
- Conventions: NGX's are MetalFX's (motion from current to previous frame in pixels after MV.Scale, the jitter as the
  sample offset in input pixels); everything passes through, as DXMT's D3D11 NGX does. **First step in a game: the
  logging run.** The first Evaluate's parameters go to the game log once, before any pixel is trusted.

## 6. Errors and edge cases

- Every refusal returns an NGX failure code without recording anything and is logged once (`macneutron-dlss:`); a
  command list that isn't DXMT's (a Streamline interposer wrapping it) is one of them.
- Released features free their upscaler reference; DXMT keeps up to 4 released ones for reuse (patch 0004's pool).
- Not handled yet: `DLSS.Input.Bias.Current.Color.Mask` (MetalFX's reactive mask means something else), sharpness
  (MetalFX has none), HDR flag (MetalFX takes the colour as it is), the D3D11 path's features on D3D12.

## 7. Testing

Automated (no game), `dxmt/check.sh` lane E, `d3d12_dlss.exe` loading `C:\windows\system32\nvngx.dll` by full path:
vendor 10de and `NvAPI_Initialize` OK with `MACNEUTRON_DLSS=1`; quality/balanced/performance/ultra performance/DLAA at
the callback's render sizes (1707x960, 1485x836, 1280x720, 854x480, 2560x1440 for 2560x1440), 64 jittered frames
each beating bilinear; inverted depth with offset inputs; frame generation, a 4x feature and a call without motion
vectors refused; Metal validation clean; without it, `nvngx.dll` doesn't load and the vendor is 106b. Launcher unit
tests for the setting and the variable.

In Palworld: (1) the logging run: DLSS offered in the menu, `macneutron-dlss:` lines, Streamline's behaviour with an
NVIDIA adapter; (2) the maintainer's eyes in motion against TSR; (3) GPU time per frame.

## 8. State (2026-10-14)

Implemented and passing the automated checks above: patch 0065, the build and bundle wiring, the launcher opt-in and
toggle, the test program and its 10 check rows. Not yet run in a game.

## 9. Risks

- **Unreal with an NVIDIA adapter:** NVAPI calls DXMT's `nvapi64` doesn't implement return errors; an engine path keyed
  on the vendor could change behaviour. Palworld decides.
- **Streamline (Palworld ships it):** its interposer may wrap the device or command lists; then `QueryInterface` for
  DXMT's command-list interface fails and Evaluate refuses (logged). The fix would be unwrapping through Streamline's
  native-interface query.
- **NGX loader:** the core is found through the registry key DXMT writes (D3DKMT query type 48 is unimplemented in
  Wine, so the loader falls through to it); a newer SDK that drops the key needs Wine's D3DKMT answer.
- **Anti-cheat:** a non-NVIDIA `nvngx.dll` in a game with active anti-cheat is untested.
- **Upstream:** DXMT doesn't take AI-written changes; patch 0065 stays in our series.
