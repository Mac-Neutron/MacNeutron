# Shader pre-caching acceptance test (DXMT fork, sub-project 4, first slice)

Historical: the Rosetta runtime was removed in 0.1.0; reproduce with the frozen reference (`tools/freeze-rosetta-reference.sh`).

Spec: `docs/superpowers/specs/2026-09-30-macneutron-pipeline-cache-design.md` §7. Manual, on a Mac with D3DMetal
(GPTK imported) and SMITE 2 installed. Record results at the bottom.

## Steps

1. **Tests:** `make dxmt-check` and `make test` pass.
2. **Install:** `target/release/macneutron install-dxmt build/dxmt`.
3. **First launch:** SMITE 2 with launch options `/usr/bin/env DXMT_D3D12_SM6=1 %command%` (no capture mode).
   - Play the lobby, then a practice match of about five minutes. Keep `Hemingway.log`
     (`…/compatdata/2437170/pfx/drive_c/users/crossover/AppData/Local/SMITE2Alpha/Saved/Logs`).
   - `…/compatdata/2437170/dxmt-pipelines/Hemingway-Win64-Shipping.exe.pipelines` exists, and so does `replayed`.
4. **Second launch,** the same route and length.
   - No `precache:` line in `~/Library/Logs/MacNeutron/launcher.log`.
   - From `Hemingway.log`: the last `LogPSOHitching: Encountered N PSO creation hitches` line, and the count of
     `PSO PRECACHING MISS:` blocks.
   - From `steam-2437170.log` (with `MACNEUTRON_LOG=1` added to the launch options), the last `d3d12 shader cache` line.
5. **Simulated update.**
   - Delete `$(getconf DARWIN_USER_CACHE_DIR)dxmt/Hemingway-Win64-Shipping.exe/shaders_*.db` and the `com.apple.metal`
     folder beside it, and write `old` into `dxmt-pipelines/replayed`.
   - Launch the same route and length. The notification appears, and `launcher.log` has
     `precache: Hemingway-Win64-Shipping.exe.pipelines exit=0 replay: …` with the time it took.
   - Take step 4's numbers again.
6. **Pass:**
   - Step 4's hitch count is well under step 3's and under 150, and step 5's is close to step 4's.
   - The shader cache line shows function hits outnumbering misses in steps 4 and 5.
   - The replay line reports 0 failed.
   - The lobby and match render as before, the hedges included.
   - Precaching misses count pipelines Unreal didn't predict; they are recorded, not judged.

## Results

| Date | Steps passed | Notes |
|---|---|---|
| 2026-10-01 | 1-6 pass | SMITE 2, fork b31e856, macOS 26A434, `/usr/bin/env DXMT_D3D12_SM6=1 MACNEUTRON_LOG=1 %command%`; lobby then a jungle practice match each run. **First launch:** recording made (14,709 graphics, 3,119 compute pipelines, 10,219 shader blobs, 120 MB after two minutes), stamp written at exit; shader cache 16,191 hit / 17,271 missed; no `LogPSOHitching` line (under 50 hitches; the 2026-09-29 run logged 150); 126 precaching misses; frame time median 13.3 ms, p90 17.3, p99 75.8. **Second launch:** no replay; shader cache 33,407 hit / 4 missed; under 50 hitches; 130 precaching misses; median 12.4 ms, p90 16.7, p99 71.1. **Simulated update** (shaders_310.db and com.apple.metal moved to the Trash, stamp `old`): notification shown, `precache: Hemingway-Win64-Shipping.exe.pipelines exit=0 replay: 17862 pipelines (14743 graphics, 3119 compute), 17862 created, 0 failed, 0 bad records, 46050 ms`; shader cache 35,176 hit / 1,254 missed, the misses from content the earlier runs never reached (the recording grew by 580 shaders and 1,526 pipelines); under 50 hitches; 140 precaching misses; median 12.7 ms, p90 16.4, p99 62.2. The notification disappears during the 46 s replay: progress notifications are a follow-up. |
