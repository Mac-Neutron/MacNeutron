# MacNeutron: frame generation on MetalFX's frame interpolator

Status: draft for review (2026-10-14). Feasibility measured; a first implementation is in (§8). Builds on DLSS answered by MetalFX
(2026-10-14 spec).

## 1. Goal

A game running DLSS on MetalFX shows a generated frame between each pair it renders, from MetalFX's frame
interpolator (macOS 26+), opt-in per game. Palworld at DLSS Performance renders ~72 fps GPU-bound; the target is
~110-120 fps displayed.

## 2. What we measured

- Palworld, DLSS Performance (1280x720 to 2560x1440), M5 Pro, Metal System Trace: 13.8 ms a frame, the GPU 99.6 %
  busy (MetalFX's upscale 2.8 ms of it, the Metal HUD 0.7 ms). No idle the interpolator could hide in.
- `/tmp/fi_spike` (native, MetalFX alone, same shape): the temporal upscale 1.45-1.47 ms of GPU span; the frame
  interpolator 3.59 ms on its own (output-size depth and motion), **3.16 ms paired with the upscaler**
  (`MTLFXFrameInterpolatorDescriptor.scaler`, render-size depth and motion), 3.20 ms paired with output-size motion
  vectors (Palworld's).
- Estimate: (13.8 + 3.2) ms per two displayed frames: ~118 fps displayed from 72 rendered, latency up by about half a
  frame plus the interpolation.

## 3. Decisions (implementer; to confirm)

- **F1. MetalFX at the present, in DXMT, paired with the DLSS upscaler, not DLSS frame generation through
  Streamline.** Palworld's DLSS-G runs in Streamline's `sl.dlss_g` plugin, which gates on an Ada GPU (NVAPI), hardware
  scheduling (D3DKMT type 70), a driver version, its own swapchain proxy and NGX's undocumented DLSSG parameter set.
  DXMT already sees what the interpolator needs: the frame's depth and motion vectors and the upscaler (each DLSS
  evaluation), and the finished frame (the present).
- **F2. Opt-in** (`MACNEUTRON_FRAMEGEN=1`, a Games window toggle), and only when the game runs DLSS on MetalFX;
  otherwise presents are untouched.
- **F3. No separate UI.** The finished frame has the HUD drawn in, and DXMT never sees it alone: HUD text over a moving
  scene may wobble in generated frames (as driver-level frame generation does). A game's UI-less frame (DLSS-G's
  HUD-less input) would fix it and is out of reach without F1's other path.
- **F4. Pacing:** each present shows the generated frame at once and the real one half the measured frame interval
  later (`presentAfterMinimumDuration`); a frame interval above 50 ms, a resize, a reset or a scene cut (DLSS reset)
  presents the real frame alone.
- **F5. Camera:** near/far/field of view aren't in DLSS super resolution's parameters; MetalFX gets defaults (0.1,
  10000, 90°, the output's aspect) unless the game passes DLSS-G's camera parameters (`DLSSG.Camera*`, not seen in
  Palworld's call). `deltaTime` is the measured frame interval.

## 4. Architecture

1. **winemetal**: create a frame interpolator (descriptor fields above, the paired scaler) and encode it with the
   per-frame properties; three unix calls, as the temporal scaler's.
2. **DXMT queue**: each `TemporalUpscale` it encodes leaves the frame's depth, motion vectors, upscaler and output in
   the queue's "last upscale" slot (held until the next present).
3. **DXMT swap chain**: with the slot filled and `MACNEUTRON_FRAMEGEN=1`, the present encodes the interpolation
   between the previous finished frame (kept in a texture of the swap chain's) and this one into a second drawable,
   presents it, and presents the real frame after the pacing delay (F4).
4. **nvngx**: unchanged, but CreateFeature makes the upscaler with the frame interpolator's requirements when frame
   generation is on (the pairing needs the scaler's descriptor to allow it).
5. **Launcher and app**: the variable and the toggle; the README row.

## 5. Testing

Automated: a D3D12 test presenting a moving synthetic scene through DLSS on MetalFX with frame generation on: twice the
drawables per present, the generated frame's PSNR against the scene's midpoint beating both neighbours'; off without the
variable or without DLSS. In Palworld: the maintainer's eyes (HUD wobble, artefacts), displayed fps (Metal HUD), latency
feel; a trace for the interpolation's cost in the real frame.

## 6. Risks

- The pairing's input sizes: Palworld's DLSS motion vectors are output-size (dilated). The spike paired an upscaler
  made with `outputResolutionMotionVectorsEnabled` and output-size motion vectors without error, at 3.20 ms; whether the
  pictures are right is for the automated test. The unpaired interpolator (3.6-3.7 ms) is the fallback.
- Frame pacing through Wine's Mac driver and CAMetalLayer's present timing; the presenter (MetalFX spatial/CMAA2) must
  stand aside for generated frames.
- Default camera parameters may cost quality; the maintainer judges.

## 8. State (2026-10-14)

First implementation, passing its automated rows (`dxmt/check.sh` lane E, `d3d12_dlss.exe framegen`: 108 of 120
presents with a generated frame, the first and the ones before the interval settles without; Metal validation clean;
none without the variable):

- winemetal: `MTLDevice_newFrameInterpolator`, `MTLCommandBuffer_encodeFrameInterpolate` (unix calls 151, 152).
- DXMT: `TemporalScaler` keeps its description; the queue keeps the last temporal upscale's upscaler, depth and motion
  vectors (`g_framegen_slot`, process-wide); `PresentFrame` interpolates between its copy of the last frame and this
  one, presents the generated frame, then the real one, each `presentDrawableAfterMinimumDuration` half the smoothed
  present interval (1-25 ms). F5's camera defaults: near 0.1, far 10000, 70 degrees.
- Launcher: `GameSettings.frameGen`, the Games window toggle (enabled with DLSS), the README row.

Not yet: a generated frame's picture checked against the scene (the test's frames are black), the game's camera
parameters, the content offsets of macOS 27's interpolator, the maintainer's eyes in Palworld.

### Palworld, first runs (2026-10-14)

- Run 1, the generated and the real frame each presented after half the measured interval: Metal rounds a minimum
  duration up to whole refreshes, so a pace a hair over one refresh held each drawable for two: 36.0 game fps at
  144 Hz, no gain.
- Run 2, the real frame at 0.75 of a refresh after the generated one: 37-45 game fps, Present 13.7 ms a frame on the
  game's thread with stalls to 480 ms: two drawables a frame taken on the game's thread wait on the compositor, which
  returns one per display refresh (it composites the window even with display sync off).
- Now: a pacing thread on a queue of its own presents the pairs behind an event; the game's thread hands over a ring of
  three and waits only when the thread is two frames behind. Both drawables are presented as soon as possible (a
  minimum duration on top held a pair for 34 ms in the test, against 18). The display's refresh is a hard cap: at
  144 Hz, 72 rendered and 144 shown at best, and the interpolation's ~3 ms puts Palworld's GPU-bound 13.8 ms frame
  at ~59 rendered.
