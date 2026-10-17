# Direct3D forwarding M5: the VM's overheads Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cut the VM's own CPU (5.2 cores on the host, 0.67 in wineserver, SMITE 2 at M0) without losing frame rate:
fewer vCPUs, fewer wake-ups, no idle spinning, a calmer sound loop, a native wineserver, and whatever else the host
sample shows polling.

**Architecture:** Each change is a setting first (an environment variable with today's behaviour as its default),
measured A/B on a repeatable scene (SMITE 2's lobby in the dev VM, `bench-smite2.sh`), then given the default the
numbers pick. wineserver is a spike that becomes a build step only if it runs the gates. The gate is taken in a match
with the maintainer.

**Tech Stack:** C and Objective-C (macneutron-vm's host code, the executor, the sound server), Swift (macneutron-vm's
options), POSIX sh (benchmark, game scripts), Wine's server (a patch), Python (perf.py, unchanged).

**Spec:** `docs/superpowers/specs/2026-10-17-macneutron-vm-d3d-forwarding-design.md` (§5.3, §8 M5); the baseline in
`docs/research/2026-10-15-vm-route/README.md` §15.

## Global Constraints

- M5's gate (spec §8): SMITE 2 in a match at M0's settings (1440p, DLSS Performance): the host's overheads (wake-ups,
  spinning, sound, other) from 5.2 cores to 2.0 or less, wineserver from 0.67 to 0.2 or less, and the frame rate and
  GPU busy no lower than M0's VM run (108.6 fps, 91.6%).
- A change is kept only if the lobby benchmark shows less CPU at no lower frame rate (within run-to-run noise: two
  runs of the same setting set the noise first).
- The guest keeps Fedora's signed kernel, `lockdown=integrity`, `module.sig_enforce=1` and kexec off; command-line
  changes only. Anti-cheat: comply, never bypass. No Reduced Security, SIP changes or kexts.
- `bench-smite2.sh` starts SMITE 2 on the maintainer's Steam account: run it only with their go-ahead and while they
  aren't playing. Never start or quit Steam. Valve's files are never committed.
- Downloads and new packages (`packages.lock`) need the maintainer's yes.
- Use `/usr/bin/grep` in scripts; commits end with the session's trailer lines; a privacy scan before committing docs.

## Review Focus

- A setting given a bad value (`MN_VM_CPUS=0`, `=abc`, more than the Mac's cores; a negative spin): it falls back to
  the default and says so, never boots with nonsense. Pinned in Task 2's and Task 3's tests.
- Fewer vCPUs than the game's busy threads: the frame rate must not fall; the benchmark's fps check is the guard.
  Pinned in Task 2 Step 4.
- The sound stream's position still reaches the guest while nothing new is queued (a paused game, the end of a sound):
  ALSA's pointer must keep moving or the guest stalls. Pinned in Task 4's `audio-test` case.
- The native wineserver with a 32-bit (WoW64) process, a crash dump or a debugger's GetThreadContext: contexts other
  than the native one keep their machine. Pinned in Task 6 Step 4.
- A Mac with other core counts (an M4 with Efficiency cores, a Max): the vCPU default is a rule over sysctl's levels,
  not a number. Pinned in Task 2 Step 5's `vcpus.sh` line, which checks the default against this Mac's levels (other
  Macs' layouts are read, not run).

---

### Task 1: The lobby benchmark

**Files:**
- Create: `eac-vm/tests/bench-smite2.sh`
- Modify: `eac-vm/tests/smite2-render.sh` (the guest profiler, as `run-game.sh` has it)

**Interfaces:**
- Produces: `bench-smite2.sh <label> [measure-seconds=60]` runs `smite2-render.sh` in the dev VM
  (`build/eac-vm-work`, its own share) with `MACNEUTRON_VM_PROFILE=1` in the guest and `MN_PRESENT_STATS` on the host,
  waits until the host's frame file shows the lobby (two windows of 30 fps or more), runs
  `perf-compare.sh vm 2437170 <seconds>` with `MN_SHARE` set, and copies the record to
  `build/perf/bench-<label>-<time>.json`. Any `MN_VM_CPUS`, `MN_EXECUTOR_SPIN_US` or other variable in its environment
  reaches `macneutron-vm`.

- [ ] **Step 1: Write it**, with `SMITE2_RUN_SECONDS` long enough for load plus the window (300).
- [ ] **Step 2: Run it twice with today's settings** (the maintainer's go-ahead first)

Run: `sh eac-vm/tests/bench-smite2.sh base-a && sh eac-vm/tests/bench-smite2.sh base-b`
Expected: two records with `fps` > 0, `cpu_cores` > 0 and a `host` split; their difference is the noise every later
comparison is held to.

- [ ] **Step 3: Commit**

```bash
git add eac-vm/tests/bench-smite2.sh eac-vm/tests/smite2-render.sh
git commit -m "bench-smite2: SMITE 2's lobby in the dev VM, measured with perf-compare"
```

### Task 2: The vCPU count

**Files:**
- Modify: `eac-vm/host/macneutron-vm.swift` (`defaultCPUs`, the `--cpus` default)
- Create: `eac-vm/tests/vcpus.sh`

**Interfaces:**
- Produces: `MN_VM_CPUS=<n>` (1 to the Mac's physical cores) sets the vCPUs when `--cpus` doesn't; anything else
  prints `macneutron-vm: MN_VM_CPUS=<v> isn't a core count, <default> instead`. The default becomes the rule Step 4's
  numbers pick, over `hw.perflevel<N>.name`/`physicalcpu`.

- [ ] **Step 1: Write the failing test `vcpus.sh`**: `macneutron-vm` with `MN_VM_CPUS=6`, `=0`, `=abc` and `=999` and an
  images folder that makes it stop right after it prints its vCPU line (a missing kernel); the lines say `6 vCPUs`,
  then the default three times with the message.
- [ ] **Step 2: Run it**: Expected FAIL (the variable is ignored).
- [ ] **Step 3: Implement it** in `macneutron-vm.swift`.
- [ ] **Step 4: Measure**: `MN_VM_CPUS=18`, `12`, `6` and `9` with `bench-smite2.sh`. Expected: CPU and the `wake` share
  fall with fewer vCPUs; the frame rate holds until the game's busy threads outnumber them.
- [ ] **Step 5: Set the default** to the best rule (for example "the Super, or Performance, cores alone") with a line
  in `vcpus.sh` for it, and re-run `vcpus.sh` and `eac-vm/tests/vm-setup.sh`.
- [ ] **Step 6: Commit**, the numbers in the message.

### Task 3: The executor's spin

**Files:**
- Modify: `eac-vm/metal/host/executor.c` (`SPIN_NS`, `consume`)
- Test: `eac-vm/metal/tests/executor-test.c`

**Interfaces:**
- Produces: `MN_EXECUTOR_SPIN_US=<0-2000>` (default: Step 4's pick; 2000 today) sets how long a consumer spins after
  its last record before it parks.

- [ ] **Step 1: Write the failing test** in `executor-test.c`: with the spin set to 0, a ring that gets one record every
  5 ms parks between them (its park count after ten records is ten); with `MN_EXECUTOR_SPIN_US=-5` or `=abc` the
  default stands and the executor says so.
- [ ] **Step 2: Run it**: `make -C eac-vm/metal executor-test`. Expected FAIL.
- [ ] **Step 3: Implement it** (read once at start, bad values keep the default with a message).
- [ ] **Step 4: Measure**: 2000, 200, 50 and 0 µs with `bench-smite2.sh` (with Task 2's default). Expected: `spin` falls;
  pick the smallest window that keeps the frame rate.
- [ ] **Step 5: Set the default, run `executor-test` and `eac-vm/metal/gate.sh`, commit.**

### Task 4: The sound loop

**Files:**
- Modify: `eac-vm/host/audio.c` (`serve`, `play`, `report`)
- Test: `eac-vm/host/audio-test.c`, `eac-vm/tests/audio.sh`

**Interfaces:**
- Produces: a stream's `serve` waits for the guest's data or for the player to cross a period of frames (a pipe the
  `play` thread writes to), not a 2 ms poll; it still reports the position whenever it changed.

- [ ] **Step 1: Write the failing test** in `audio-test.c`: over 2 s of playing with nothing new queued, the serve
  thread wakes at most (2 s ÷ the period) + a few times, and the reported position reaches the frames played.
- [ ] **Step 2: Run it, expected FAIL** (about 1000 wake-ups).
- [ ] **Step 3: Implement it.**
- [ ] **Step 4: Run `audio-test`, `tests/audio.sh` in the guest (2.0 s for 2 s), and `bench-smite2.sh`.** Expected:
  `sound` near zero, the same frame rate.
- [ ] **Step 5: Commit.**

### Task 5: The guest's wake-ups (command line)

**Files:**
- Modify (if kept): `eac-vm/image/build.sh` (the kernel command line), the dev images' `cmdline.txt` while measuring

- [ ] **Step 1: Measure** with Tasks 2-4's defaults: the command line as it is, then with `nohz_full=1-<n-1>` (every
  vCPU but the first). Expected: `wake` falls if the tick is part of it.
- [ ] **Step 2: Keep it only if it helps:** then `build.sh`'s command line, the identity gate and `vm-setup.sh` (the
  setup builds the same images), and a commit with the numbers. Otherwise ledger it and leave the command line.

### Task 6: A native wineserver

**Files:**
- Create: `eac-vm/wine/patches/00NN-server-Serve-another-machine-as-native.patch` (a Wine patch: `WINESERVER_MACHINE`
  names the machine the server takes as native, x86_64 here)
- Modify: `eac-vm/wine/build.sh` (a `server-arm64` step: Wine's `server/` built for aarch64 in the builder),
  `eac-vm/image/build.sh` (`wine.erofs` gets it as `/opt/mn-wine/bin/wineserver`), `eac-vm/guest/run-game.sh`
  (`WINESERVER_MACHINE=x86_64`)

- [ ] **Step 1: The spike**: patch, build, and run `eac-vm/tests/wine.sh` with the native server.
  Go/no-go: if Wine doesn't start, or a context error shows, ledger why and stop the task here.
- [ ] **Step 2: Run** the 4b-2 DXMT gate (`eac-vm/dxmt/gate.sh`) and `bench-smite2.sh` with it.
  Expected: the gate passes; `wineserver` in the profile falls from about 0.67 to 0.2 or less.
- [ ] **Step 3: Make it the build** (`build.sh`, stamps: `eac-vm/image/stamps.sh wine` gains the server's tree).
- [ ] **Step 4: The contexts' machines**: a test program (`eac-vm/guest/win/context.c`, built 64-bit and 32-bit) that
  suspends a thread and reads and writes its context with GetThreadContext and SetThreadContext (Wow64GetThreadContext
  in the 32-bit build), run with the native server, prints the same as with the x86-64 one.
- [ ] **Step 5: Commit.**

### Task 7: The host's other polling

- [ ] **Step 1: Read** the benchmark's latest `.sample.txt` for the virtio-gpu worker, vsock and virtiofs threads:
  which run without a guest request (a timed poll, a spin).
- [ ] **Step 2: For each one found:** a setting or a fix in its file (libkrun patch, or macneutron-vm), measured with
  `bench-smite2.sh`, kept if it lowers `other`. None found: ledger it.
- [ ] **Step 3: Commit** each kept fix with its numbers.

### Task 8: The gate

**Files:**
- Modify: `docs/research/2026-10-15-vm-route/README.md` (a new section, "§16 Direct3D forwarding, M5: the VM's
  overheads")
- Rebuild: the dev app (`make app APP=build/MacNeutron-next.app`, with the signing environment), only while it isn't
  running

- [ ] **Step 1: The session, with the maintainer**: SMITE 2 in a match at 1440p and DLSS Performance with
  `/usr/bin/env MACNEUTRON_PERF=1 %command%`; `sh eac-vm/tests/perf-compare.sh vm 2437170`.
- [ ] **Step 2: Compare** against M0's VM record (11.64 cores, 91.6%, 108.6 fps) and the gate's three lines.
- [ ] **Step 3: Record** in §16: each change's lobby numbers, the match's numbers, what's left of the 5.2 cores and
  wineserver's 0.67. Privacy scan, commit.
