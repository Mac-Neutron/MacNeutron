# D3D12 stubs acceptance test (DXMT fork, sub-project 3, first slice)

Historical: the Rosetta runtime was removed in 0.1.0; reproduce with the frozen reference (`tools/freeze-rosetta-reference.sh`).

Spec: `docs/superpowers/specs/2026-09-29-macneutron-d3d12-stubs-design.md` §7. Manual, on a Mac with D3DMetal (GPTK
imported) and SMITE 2 installed. Record results at the bottom.

## Steps

1. **Tests:** `make dxmt-check` passes, including every D3D12 stubs comparison with D3DMetal (`d3d12_api`,
   `d3d12_depth` `depth2`, `d3d12_copy`, `d3d12_null`) and the `d3d12_timestamp` rules.
2. **Install:** `target/release/macneutron install-dxmt build/dxmt`.
3. **SMITE 2 on our DXMT:** launch options `/usr/bin/env DXMT_DXIL_DUMP=<capture folder> %command%` (capture mode is
   still how SMITE 2 passes Unreal's SM6 check). Reach the lobby; it renders as on D3DMetal. Play a match.
4. **Logs:** this run's lines in `~/Library/Logs/MacNeutron/steam-2437170.log` have no `unhandled feature` and no
   `is not implemented`.

## Results

| Date | Steps passed | Notes |
|---|---|---|
| 2026-09-29 | 1–4 pass | Fork `419e563`; `make dxmt-check` 85/85. The lobby renders as on D3DMetal (the occlusion-query fix). A practice match plays, with visible glitches: an F9 pass dump of one shows hedges as black silhouettes, their depth written by the prepass and their G-buffer never, while the game log counts 150 PSO creation hitches and 119 precaching misses in the first minute. Unreal skips a draw whose pipeline is still compiling, and our DXMT compiles every pipeline cold (no disk cache); the frame rate being the same at 2560×1440 and 1280×720 fits the same CPU-side cost. Both are sub-project 4's (performance: a persistent pipeline cache). One GPU address fault during the return to the lobby (Metal discarded that command buffer and two queued behind it): not reproduced, to investigate. No `unhandled feature` or `is not implemented` lines. |
