# Direct3D forwarding M0: measure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Numbers for SMITE 2 and Palworld, on macOS and in today's VM, that the forwarding work is measured against: the
VM's CPU, the GPU's busy share and the frame rate, plus where the guest's CPU goes and how much of it is DXMT's.

**Architecture:** One launch option, `MACNEUTRON_PERF=1`, turns on DXMT's frame statistics on both paths and, in the
VM, the guest profiler (`top -H` into the run's share). `eac-vm/tests/perf-compare.sh` samples the process tree's CPU,
the GPU (`ioreg`) and, in the VM, a host `sample`, over a window while the maintainer plays; `perf.py` turns that into a
JSON record and compares two records against the spec's gate.

**Tech Stack:** Swift (launcher), POSIX sh (game script, scripts), Python 3 stdlib (`perf.py`, `unittest`).

**Spec:** `docs/superpowers/specs/2026-10-17-macneutron-vm-d3d-forwarding-design.md` (§1, §5.1, §5.2, §8 M0).

## Global Constraints

- The gate the comparison checks (spec §1): GPU busy 95% or more; frame rate within 10% of the macOS run; the VM's
  CPU (`macneutron-vm`, `mn-steam-host`, `gvproxy`) within 1.5 times the macOS run's (the game's Wine processes).
- The profiler's variable is `MACNEUTRON_VM_PROFILE=1` (spec §5.1); `MACNEUTRON_PERF=1` sets it and `DXMT_STATS=1`.
  A value the user set themselves for `DXMT_STATS` or `DXMT_DXIL_DUMP` wins.
- Guest output goes to the run's share: `/mnt/share/traces/profile.txt` and `/mnt/share/traces/dxmt/stats.txt` (the
  share's `traces/` is the guest user `mn`'s). On macOS, stats go to `~/Library/Logs/MacNeutron/perf/<appid>/`.
- The scripts read the maintainer's run folders only, never write there; they never start or stop Steam or a game.
- No SteamID or account name in any output or record; the home folder is written `~` in docs.
- Use `/usr/bin/grep` in scripts. Commits end with the session's two trailer lines.

## Review Focus

- A thread name with spaces (`RHI Thread`, `Foreground Worker`): top's COMMAND column is the rest of the line, not one
  field. Pinned in Task 3's `test_threads_names_with_spaces`.
- DXMT's last statistics window (`, exit`) is partial: it must not count toward the frame rate. Pinned in Task 3's
  `test_fps_skips_exit`.
- `ps` CPU time in every format it prints (`0:01.23`, `12:34.56`, `1:02:03.45`, `2-01:02:03`). Pinned in Task 3's
  `test_parse_time_formats`.
- A process that starts during the window counts from zero; one that ends during it is lost and the record says how many.
  Pinned in Task 3's `test_cpu_delta_new_and_gone`.
- `perf-compare.sh vm` with no VM running stops with a message instead of measuring macOS. Pinned in Task 3 Step 6.

---

### Task 1: The launch option

**Files:**
- Modify: `Sources/MacNeutronCore/Launcher.swift` (after `LaunchEnvironment.build` in the macOS path; at the top of
  `runInMicroVM`)
- Modify: `Sources/MacNeutronCore/MicroVM.swift` (`passed`)
- Test: `Tests/MacNeutronCoreTests/LauncherTests.swift`, `Tests/MacNeutronCoreTests/MicroVMTests.swift`

**Interfaces:**
- Produces: `Launcher.perfFolder(appID: String) -> URL` = `log.directory/perf/<appID>`; on macOS with
  `MACNEUTRON_PERF=1` and the DXMT backend, the game's environment has `DXMT_STATS=1` and
  `DXMT_DXIL_DUMP=<perfFolder path>` (created). In the VM, the guest header carries `MACNEUTRON_VM_PROFILE='1'` and
  `DXMT_STATS='1'` (both added to `MicroVM.passed`).

- [ ] **Step 1: Write the failing tests**

In `LauncherTests.swift`, `perfTurnsOnDXMTStatsOnMacOS`: a launch with `MACNEUTRON_VM=0` and `MACNEUTRON_PERF=1` of a
library game; the game's `wine` call has `DXMT_STATS == "1"` and `DXMT_DXIL_DUMP == launcher.perfFolder(appID:).path`,
and that folder exists. With `DXMT_DXIL_DUMP=/elsewhere` also set, it stays `/elsewhere`.

In `MicroVMTests.swift`, `perfTurnsOnTheGuestProfiler`: a VM launch with `MACNEUTRON_PERF=1`; the guest script contains
`"\nMACNEUTRON_VM_PROFILE='1'\n"` and `"\nDXMT_STATS='1'\n"`. Without it, neither.

- [ ] **Step 2: Run them to see them fail**

Run: `swift test --filter 'perfTurnsOn'`
Expected: both FAIL on the missing variables.

- [ ] **Step 3: Implement it**

`perfFolder(appID:)` as above. macOS path: only when `backend == .dxmt`, `DXMT_STATS` from the user wins, the folder is
made with `createDirectory(withIntermediateDirectories: true)`. VM path: set both in the environment `script` reads,
and add `"MACNEUTRON_VM_PROFILE"` and `"DXMT_STATS"` to `MicroVM.passed`.

- [ ] **Step 4: Run the suite**

Run: `swift test`
Expected: all pass (412 before this task, 414 after).

- [ ] **Step 5: Commit**

```bash
git add Sources/MacNeutronCore/Launcher.swift Sources/MacNeutronCore/MicroVM.swift \
  Tests/MacNeutronCoreTests/LauncherTests.swift Tests/MacNeutronCoreTests/MicroVMTests.swift
git commit -m "Launcher: MACNEUTRON_PERF=1, DXMT's frame statistics and the guest profiler"
```

### Task 2: The guest profiler and DXMT's statistics in the guest

**Files:**
- Modify: `eac-vm/guest/run-game.sh` (after Xvfb starts)
- Create: `eac-vm/tests/profile.sh`

**Interfaces:**
- Consumes: Task 1's header variables.
- Produces: with `MACNEUTRON_VM_PROFILE=1`, `/mnt/share/traces/profile.txt` holds `top -b -H -d 5 -w 512` batches for
  the whole run; with `DXMT_STATS=1`, `DXMT_DXIL_DUMP=/mnt/share/traces/dxmt` (made by the script) so DXMT writes
  `stats.txt` there.

- [ ] **Step 1: Write the failing test `eac-vm/tests/profile.sh`**

A host script in the shape of `eac-vm/tests/modes.sh`: header
`VERB=run APPID=0 GAME_DIR=/mnt/share/dxmt-tests EXE=present_loop.exe MACNEUTRON_VM_PROFILE=1 DXMT_STATS=1`, then
`set -- 1280 720 0 0 1500 0` (1500 frames, over 10 s), then `run-game.sh`; run with `eac-vm/run-guest.sh`
(`MN_AUDIO_OUTPUT=none MN_RUN_TIMEOUT=300`). Then, from the share (`build/eac-vm-work/share`, or `MN_SHARE`):
- `traces/profile.txt` has at least two lines starting `top - ` and a thread line whose COMMAND is
  `present_loop.exe` or starts `dxmt-` (`fail=" profile"` otherwise);
- `traces/dxmt/stats.txt` has a line matching `^# [0-9:]+ [0-9.]+ s, [0-9]+ frames \(` (`fail=" stats"`).
Prints `profile: ok` or `profile: FAIL<what>`.

- [ ] **Step 2: Run it to see it fail**

Run: `sh eac-vm/tests/profile.sh`
Expected: `profile: FAIL profile stats`

- [ ] **Step 3: Implement it in `run-game.sh`**

```sh
# MACNEUTRON_PERF (the launcher's): DXMT's frame statistics, and every thread's CPU every 5 s, in the share's traces/.
[ "${DXMT_STATS:-}" = 1 ] && mkdir -p /mnt/share/traces/dxmt && export DXMT_STATS DXMT_DXIL_DUMP=/mnt/share/traces/dxmt
[ "${MACNEUTRON_VM_PROFILE:-}" = 1 ] && top -b -H -d 5 -w 512 > /mnt/share/traces/profile.txt 2>&1 &
```

- [ ] **Step 4: Run the test, and the gates that run `run-game.sh`**

Run: `sh eac-vm/tests/profile.sh && sh eac-vm/tests/modes.sh && sh eac-vm/tests/turn.sh`
Expected: `profile: ok`, `modes: ok`, `turn: ok 10000`

- [ ] **Step 5: Commit**

```bash
git add eac-vm/guest/run-game.sh eac-vm/tests/profile.sh
git commit -m "VM: the guest profiler and DXMT's frame statistics in the run's share"
```

### Task 3: The measurements and the comparison

**Files:**
- Create: `eac-vm/tests/perf.py`, `eac-vm/tests/perf_test.py`, `eac-vm/tests/perf-compare.sh`
- Move: the scratch sample parser into `perf.py` (`sample_split`)

**Interfaces:**
- Consumes: Task 1's folders, Task 2's files.
- Produces (`perf.py`):
  - `parse_time(text: str) -> float`: `ps`'s TIME in seconds.
  - `cpu_delta(before: str, after: str, match: Callable[[str], bool]) -> tuple[float, int]`: CPU seconds the matching
    processes used between two `ps -A -o pid=,time=,comm=` listings, and how many matching processes ended between them.
  - `gpu_busy(texts: list[str]) -> float`: mean `"Device Utilization %"` over `ioreg -r -d 1 -w 0 -c IOAccelerator` outputs.
  - `fps(stats: str, offset: int) -> float | None`: mean fps of the `, every 5 s` windows past byte `offset`.
  - `threads(profile: str, offset: int) -> list[tuple[str, float]]`: per thread name, the mean over batches past
    `offset` of the summed %CPU of the threads with that name, highest first.
  - `sample_split(sample: str) -> dict[str, float]`: cores per category, from each thread's leaf frames with the
    blocking ones (`__psynch_cvwait`, `kevent`, `mach_msg2_trap`, `poll`, `read`, ...) left out, each thread's leaf
    samples divided by that thread's total (a thread busy the whole window is one core). `guest`: `hv_trap`. `wake`:
    a `fc_vcpu` thread's other busy leaves (`pthread_mutex_lock`, `VcpuStateManager::sync*`, `__psynch_cvsignal`).
    `spin`: other threads in `consume`, `mn_ring_next`, `mach_absolute_time`, `clock_gettime_nsec_np`. `sound`:
    `serve`. `other`: the rest.
  - CLI: `perf.py record <json out> --mode <macos|vm> ...` and `perf.py compare <macos.json> <vm.json>`.
- Produces (`perf-compare.sh <macos|vm> <appid> [seconds=60]`): writes `build/perf/<appid>-<mode>-<YYYYmmdd-HHMMSS>.json`
  and prints its summary; `perf-compare.sh compare <a.json> <b.json>` prints the three ratios and PASS or FAIL for each
  of §1's numbers.

- [ ] **Step 1: Write the failing tests `perf_test.py`** (`unittest`, fixtures as strings)

```python
def test_parse_time_formats(self):
    for text, s in [("0:01.23", 1.23), ("12:34.56", 754.56), ("1:02:03.45", 3723.45), ("2-01:02:03", 176523.0)]:
        self.assertAlmostEqual(perf.parse_time(text), s)

def test_cpu_delta_new_and_gone(self):
    before = "  10   0:05.00 /x/wine.app/Contents/MacOS/wine\n  11   0:01.00 /x/wine.app/Contents/MacOS/wine\n"
    after = "  10   0:15.00 /x/wine.app/Contents/MacOS/wine\n  12   0:02.00 /x/wine.app/bin/wineserver\n"
    self.assertEqual(perf.cpu_delta(before, after, lambda c: "/wine.app/" in c), (12.0, 1))

def test_gpu_busy_mean(self):
    self.assertAlmostEqual(perf.gpu_busy(['"Device Utilization %"=90', '"Device Utilization %"=100']), 95.0)

def test_fps_skips_exit(self):
    stats = ("# 10:00:05 5.0 s, 600 frames (120.0 fps), every 5 s\n  x\n"
             "# 10:00:10 5.0 s, 500 frames (100.0 fps), every 5 s\n# 10:00:12 2.0 s, 10 frames (5.0 fps), exit\n")
    self.assertAlmostEqual(perf.fps(stats, 0), 110.0)
    self.assertAlmostEqual(perf.fps(stats, stats.index("# 10:00:10")), 100.0)

def test_threads_names_with_spaces(self):
    batch = ("top - 10:00:00 up 1 min\n\n    PID USER      PR  NI    VIRT    RES    SHR S  %CPU  %MEM     TIME+ COMMAND\n"
             "   41 mn        20   0  100000  50000  1000 R  90.0   1.0   0:10.00 RHI Thread\n"
             "   42 mn        20   0  100000  50000  1000 R  40.0   1.0   0:10.00 dxmt-encode-thr\n"
             "   43 mn        20   0  100000  50000  1000 S  20.0   1.0   0:10.00 RHI Thread\n")
    self.assertEqual(perf.threads(batch + batch.replace("90.0", "50.0"), 0)[0], ("RHI Thread", 90.0))
```

And `test_sample_split`: a trimmed real `sample` call graph (one vCPU thread of 100 samples, 60 in `hv_trap` and 40 in
`__psynch_cvwait`; one unnamed thread of 100 in `consume` > `mach_absolute_time`) gives `guest` 0.6 and `spin` 1.0.

- [ ] **Step 2: Run them to see them fail**

Run: `python3 -I eac-vm/tests/perf_test.py`
Expected: errors, `perf` has no such functions.

- [ ] **Step 3: Implement `perf.py`** to the signatures above (stdlib only; `sample_split` from the scratch parser of
2026-10-17: a thread's leaf counts, blocking leaves excluded, divided by that thread's total for cores).

- [ ] **Step 4: Run them to see them pass**

Run: `python3 -I eac-vm/tests/perf_test.py`
Expected: `OK`

- [ ] **Step 5: Write `perf-compare.sh`**

`macos` mode: the process match is a command path containing `/wine.app/`. `vm` mode: basenames `macneutron-vm`,
`mn-steam-host`, `gvproxy`; the share is `~/Library/Application Support/MacNeutron/vm/runs/<appid>` (or `MN_SHARE`);
offsets of `traces/profile.txt` and `traces/dxmt/stats.txt` at the start; one `sample <pid> 10` of the exact
`pgrep -x macneutron-vm` pid in the middle of the window. `macos` mode reads `~/Library/Logs/MacNeutron/perf/<appid>/stats.txt`.
`ps` at the start and end, `ioreg` once a second. Then `perf.py record` with all of it.

- [ ] **Step 6: Check it end to end on the dev VM**

Run `profile.sh`'s `present_loop.exe` run in the background with `MN_SHARE` pointing at the dev share, and
`MN_SHARE=build/eac-vm-work/share sh eac-vm/tests/perf-compare.sh vm 0 15` during it.
Expected: a JSON with `cpu_cores` > 0, `gpu_busy` between 0 and 100, `fps` a number, `threads` listing
`present_loop.exe` or a `dxmt-` thread, `host` with `guest` > 0. And with no VM running,
`sh eac-vm/tests/perf-compare.sh vm 0 5` prints `perf-compare: no macneutron-vm is running` and exits 1.

- [ ] **Step 7: Commit**

```bash
git add eac-vm/tests/perf.py eac-vm/tests/perf_test.py eac-vm/tests/perf-compare.sh
git commit -m "perf-compare: the VM against macOS, CPU, GPU and frame rate (spec §5.2)"
```

### Task 4: The baseline

**Files:**
- Modify: `docs/research/2026-10-15-vm-route/README.md` (a new section, "§15 Direct3D forwarding: M0 baseline")
- Rebuild: `build/MacNeutron-next.app` (`make app APP=build/MacNeutron-next.app`, with the signing environment), only
  while it isn't running

- [ ] **Step 1: The session, with the maintainer**

The maintainer sets SMITE 2's launch options to `/usr/bin/env MACNEUTRON_PERF=1 MACNEUTRON_VM=0 %command%`, opens the
app, plays a match and says when; `sh eac-vm/tests/perf-compare.sh macos 2437170`. Then without `MACNEUTRON_VM=0`, the
same scene and settings in the VM; `sh eac-vm/tests/perf-compare.sh vm 2437170`. The same for Palworld (1623730). Note
each run's in-game resolution and upscaler.

- [ ] **Step 2: Compare**

Run: `sh eac-vm/tests/perf-compare.sh compare build/perf/2437170-macos-*.json build/perf/2437170-vm-*.json`, and the
same for 1623730.
Expected: the three ratios (today's VM fails the gate; that is the baseline).

- [ ] **Step 3: Record**

§15 holds, per game: both runs' CPU (cores), GPU busy, fps, settings; the VM's host split (`guest`, `wake`, `spin`,
`sound`, `other`); the guest's top 10 threads; DXMT's share: its own threads' (`dxmt-*`) CPU, stated as a lower bound
(D3D12 recording runs on the game's own threads, which are listed beside it). Privacy scan the section before the
commit.

- [ ] **Step 4: Commit**

```bash
git add docs/research/2026-10-15-vm-route/README.md
git commit -m "docs: Direct3D forwarding M0, the baseline on macOS and in today's VM"
```
