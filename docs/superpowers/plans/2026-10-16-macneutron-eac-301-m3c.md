# EAC microVM, milestone 3c: EAC's client running for SMITE 2

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (native, the maintainer's choice).

**Goal:** SMITE 2 under our Wine in the microVM runs with EAC's Linux client loaded (spec §8, milestone 3c).

**Architecture:** the game folder comes read-only from the Mac's Steam library (virtiofs `games`), the EAC runtime
and Valve's `steamclient64.dll` from the share (never committed), Steam through the forwarder, rendering in software.

**Spec:** `docs/superpowers/specs/2026-10-15-macneutron-eac-vm-route-design.md` (§5.5, §5.7, §8, §10)

## Global Constraints

- The EAC runtime (app 1826330), Valve's DLLs and game files are never committed or shipped.
- Never start or quit Mac Steam; never write to the Mac's Steam library (the guest sees it read-only).
- Ask before any download.

## Ruling on the spec's pass condition

The spec's `Launcherfinishedwith:301` is not enough for an EOS title: SMITE 2's bootstrapper logs 301 in a prefix
without `PROTON_EAC_RUNTIME` too, because under Wine it only launches the game, whose EOS SDK loads the client later.
The gate is `easyanticheat_x64.so` mapped in a Wine process, with 301 as a prerequisite row (`eac-vm/tests/eac.sh`).
Cost if wrong: none; the stricter row includes the spec's.

The reference run under Valve's Proton (spec 3c, first step) is deferred: the builder has no Proton (a download) and
the direct gate does not depend on it. It becomes the next step if the client never loads under our Wine.

---

### Task 1: The bootstrapper reaches 301 — done (66a3983)

Game share, prefix as MacNeutron's launcher builds it, `PROTON_EAC_RUNTIME`, Steam for app 2437170.

### Task 2: The game reaches its lobby — done (97e75bf)

A null default ALSA device (XAudio2 crashed without one); `GALLIUM_OVERRIDE_CPU_CAPS=sse4.1` (llvmpipe under FEX
picked AVX2 paths LLVM could not select). SMITE 2 loads `L_MainLobby_P` and keeps running.

### Task 3: Guest network

**Files:** `eac-vm/host/macneutron-vm.swift` (a virtio-net device, `krun_net_device_new_unixgram_path` to the
network helper's socket), the helper itself (pinned, never committed if it is a binary), `eac-vm/tests/net.sh`.

- [ ] **Step 1 (RED):** `net.sh`: `getent hosts demo.rally-here.io` resolves and an HTTPS request to it answers.
- [ ] **Step 2:** The helper, started and stopped by the host like `mn-steam-host` (`--parent`).
- [ ] **Step 3:** GREEN; commit.

### Task 4: EAC's client loads

- [ ] **Step 1:** `eac.sh` with the network (RED until now: `FAIL client: easyanticheat_x64.so never mapped`).
- [ ] **Step 2:** GREEN, results in the research README §6, commit, one fresh reviewer.
