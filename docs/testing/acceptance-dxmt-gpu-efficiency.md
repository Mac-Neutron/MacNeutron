# GPU efficiency acceptance test (DXMT fork, sub-project 4, third slice)

Historical: the Rosetta runtime was removed in 0.1.0; reproduce with the frozen reference (`tools/freeze-rosetta-reference.sh`).

Spec: `docs/superpowers/specs/2026-10-02-macneutron-gpu-efficiency-design.md`. Manual, once per milestone, on a Mac with
SMITE 2 installed. Record results at the bottom.

## Steps

1. **Tests:** `make dxmt-check` and `make test` pass.
2. **Install:** with SMITE 2 closed, `.build/release/macneutron install-dxmt build/dxmt`.
3. **Run:** SMITE 2 with
   `/usr/bin/env DXMT_D3D12_SM6=1 DXMT_STATS=1 DXMT_DXIL_DUMP=/Users/<you>/dxil-smite2 %command%` (pass labels for
   the per-pass table), at the GPU efficiency spot (the M4/M5 spot of the GPU overlap acceptance test).
   - Standing still: `python3 dxmt/tools/gpu-trace.py $(pgrep -f Hemingway-Win64-Shipping | head -1)`.
   - Play about two minutes, quit, and read the newest `PEX_Timeline_*.csv` with `gpu-trace.py`.
4. **Pass:** the picture looks the same; GPU busy per frame is lower than the previous milestone's. A milestone that
   measures no gain is recorded as such.

## Results

| Milestone | Date | Run | Control | Notes |
|---|---|---|---|---|
| Baseline | 2026-10-02 | DXMT (fork 72193e9, labelled): period 14.25 ms (70 fps), GPU busy 13.84 ms, idle 0.37 ms | D3DMetal at the same spot: period 10.14 ms (99 fps), GPU busy 9.87 ms | Per pass (ms per frame): base pass 7c+d 4.77, 1c 2.38, 1c+d 1.95, 0c+d 1.40, compute 1.31, clear 0.78, blit 0.29. Reporting D3DMetal's adapter identity: 14.19 ms (no change). |
| E1 | 2026-10-02 | Fork d2ec0b0, labelled: **period 12.05 ms (83 fps), GPU busy 11.60 ms**, idle 0.39 ms; channels Compute 12.9%, Fragment 51.9%, Vertex 28.5%. PEX: median 12.43 ms, p90 16.02 ms (GPU 11.94 ms, RHI 11.19 ms) | The baseline row | **2.2 ms less GPU work per frame, 18% more frames.** Per pass: base pass 4.77 → 3.96, 1c 2.38 → 2.06, 1c+d 1.95 → 1.45, clear 0.78 → 0.22, 3c+d 0.22 → 0.09; depth-only unchanged (depth was already compressed). The picture looked the same. |
| E2 | 2026-10-02 | Fork b2371d5 (plus the E3/E5 experiment switches, off), labelled: period 12.12 ms (83 fps), GPU busy 11.65 ms | E1 | Level with E1, as expected: E2 keeps the timestamp fix (one counter buffer per encoder) and not the base-pass merge (measured-small after E1). |
| E3 | 2026-10-02 | Experiment runs at the spot: `DXMT_DXIL_VS_FAST=late` 11.68 ms (busy 11.34), `=1` 11.92 ms (busy 11.56); base-pass vertex 2.45 and 2.35 ms | E2's run: base-pass vertex 2.43 ms | **No gain** from fused vertex math, even unsafe; not built. No flicker in any run. |
| E4 + E5 | 2026-10-02 | Fork 7edf8f2, labelled, clean trace: **period 11.78 ms (85 fps), GPU busy 11.33 ms** | E2's run (12.12 / 11.65 ms) | About 0.3 ms less GPU work per frame: blits 0.23 → 0.08 ms (E4), the rest E5. A first E5 build (64-bit bounds math) raised vertex time by 0.5 ms (base pass 2.43 → 2.79 ms): Apple GPUs have no 64-bit integer ALU; the 32-bit wrap-safe form is back at 2.42 ms. |
| E9 (measured) | 2026-10-02 | D3DMetal at the spot: `D3DM_MTL4=1` 10.55 ms (busy 10.08), `D3DM_MTL4=0` **9.26 ms (busy 9.07; fragment 5.6, vertex 2.8, compute 0.9 ms)** | D3DMetal's default (this morning): 10.14 ms | D3DMetal's default is its Metal 4 back end (its GPU work shows unattributed in Instruments), and it is slower on SMITE 2 than its Metal 3 path: a Metal 4 back end isn't built. The target is D3DMetal on Metal 3, 9.26 ms, against our 11.78 ms. |
| E7 | 2026-10-02 | Fork 9acb430, labelled: period 11.99 ms (83 fps), GPU busy 11.42 ms; DXMT_STATS per frame: 9 clears folded at execute, 1 refused (barrier), 1 clear pass left of 10 | E4 + E5 (per-frame medians: all GPU work 11.35 ms) | **No net gain**: clear passes 0.17 → 0 ms, but the base pass's fragment time rose 1.54 → 1.68 ms (all work 11.35 → 11.43 ms, within noise). The cleared stores of the G-buffer targets that E1 leaves uncompressed (UAV-capable RG11B10Float and RGBA16Unorm) moved into the base pass instead of going away. |
| E10 | 2026-10-02 | Fork 1fba8d2, labelled: **period 11.56 ms (87 fps), GPU busy 11.08 ms**, idle 0.45 ms; DXMT_STATS per frame: about 152 ExecuteIndirect calls, all native, no resolver passes; 0.2 render passes merged | E7 (11.99 / 11.42 ms) | **0.34 ms less GPU work per frame** (all work 11.43 → 11.09 ms): the resolver kernels and their compute passes are gone. The base pass is still 6 Metal passes (vertex 2.33, fragment 1.68 ms): something besides resolvers splits it. The picture looked the same; no flicker. |
| B6 (built) | 2026-10-13 | DXMT 0061 and 0062, from a read-only review of the gap to D3DMetal's 9.07 ms. **0061** merges render passes across one query heap's timestamps when the ring takes them (the default) and, in strict order, out of a pass after its list's start timestamp: SMITE 2's base-pass segments are such lists. **0062** compresses UAV textures too, but for 32-bit texels of more than one channel (a Metal probe on M5 Pro: R11G11B10 and RG16Float written through an R32 view lose 9% and 3% of texels compressed; views of one layout and another type read back exact): a 2560x1440 RGBA16Unorm UAV target clears in 0.008 ms instead of 0.060 and loads, draws and stores in 0.034 ms instead of 0.060. Gates: `make dxmt-check` all passed in both lanes (742 `ok`); the new rows' REDs were recorded first (merge counts 1 → 3; UAV clear 197 → 48 us). | E10 | **Not yet measured in SMITE 2.** Expected from the records: at most 0.14 ms from 0061 (E2's ceiling for S3-S6), a few tenths of a ms from 0062. The next run at the spot, with `DXMT_STATS=1`, reads `render passes merged` and `render pass merges refused (…)` per frame. |
