# 32-bit games in the VM, M2: the WoW64 guest Wine — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The guest's Wine runs 32-bit Windows programs (new WoW64 on M1's FEX), and the launcher sends 32-bit games and installers to the VM.

**Architecture:** The guest Wine is configured `--enable-archs=i386,x86_64`, so `wine.erofs` gains `i386-windows`; the
image's `base.erofs` carries M1's FEX. Prefixes get `syswow64` from `wineboot`. A 32-bit process's Direct3D is Wine's
builtins until M4 (forwarded overrides become `n,b`). The launcher lets a 32-bit game through when it runs in the VM.

**Tech Stack:** POSIX sh (guest scripts, build scripts), Swift (launcher, swift-testing), the Fedora builder VM, the
microVM (`eac-vm/run-guest.sh`).

**Spec:** `docs/superpowers/specs/2026-10-20-macneutron-vm-32bit-games-design.md` §5.

## Global Constraints

- No gate per milestone (maintainer, 2026-10-20): each task's runs are development runs; every gate, new and
  regression, runs once after M4 (spec §8).
- The root filesystem, the image recipes and `vm-setup` don't change (spec §5).
- 32-bit games on the macOS path stay refused (spec §3, out of scope).
- Builder: `eac-vm/builder.sh`; it already has the `out` and `outp` shares. Never open MacNeutron.app, never start or
  quit Steam, never touch `~/Library/Application Support/MacNeutron` or `steamapps/compatdata` (maintainer rules).
- `dxmt` stage builds need `MACNEUTRON_SIGN_IDENTITY` exported (memory: signing-env).

## Review Focus

- A prefix made by a 64-bit-only Wine (every player's today) upgrades through `run-game.sh`'s `wineboot -u`: test row 5.
- A 32-bit process in forwarded mode (the VM's default) finds no forwarding DLLs in `syswow64` and must fall back to
  Wine's builtin d3d11 rather than fail to load it: test row 6.
- FEX's own mappings in the low 4 GiB would starve WoW64's 32-bit allocations: test row 7.
- A 32-bit game outside a Steam library, or with `MACNEUTRON_VM=0`, runs on macOS and must still be refused there:
  launcher test.
- A 32-bit redistributable installer started with `run` outside the VM is still skipped: the existing launcher test.

---

### Task 1: The WoW64 test, red on today's image

**Files:**
- Create: `eac-vm/tests/wow64.sh` (guest script, style of `eac-vm/tests/wine.sh`: `check name got want` rows, exit 1
  on any miss)

**Interfaces:**
- Produces: `eac-vm/tests/wow64.sh`, run as `MN_STATE=$PWD/build/eac-vm-work/state/wow64.img sh eac-vm/run-guest.sh
  eac-vm/tests/wow64.sh`. The state disk keeps `/home/mn/pfx64`, a prefix made by the 64-bit-only Wine, which the
  final gate reuses.

Rows, after the same environment and Xvfb lines as `wine.sh` (`WINEDLLOVERRIDES="mscoree,mshtml="` on every
`wineboot`):
- Preparation, not a row: when `/home/mn/pfx64` is missing and `/opt/mn-wine/lib/wine/i386-windows` is absent,
  `WINEPREFIX=/home/mn/pfx64 wineboot -i` (a 64-bit-only prefix, as players have today). Print whether it was made.
- `1 i386-windows`: `/opt/mn-wine/lib/wine/i386-windows/kernel32.dll` exists → `present`.
- `2 fresh-syswow64`: `rm -rf /home/mn/wow64-fresh`, `wineboot -i` there, `drive_c/windows/syswow64/kernel32.dll`
  exists → `present`.
- `3 cmd32`: `wine 'C:\windows\syswow64\cmd.exe' /c 'echo %PROCESSOR_ARCHITECTURE%'` in the fresh prefix → `x86`.
- `4 cmd32-wow`: the same with `%PROCESSOR_ARCHITEW6432%` → `AMD64`.
- `5 upgrade`: `rm -rf /home/mn/pfx64-up; cp -a /home/mn/pfx64 /home/mn/pfx64-up` (the original stays 64-bit-only for
  the next run), `wineboot -u` there, then its `syswow64/kernel32.dll` exists → `present`; `missing-pfx64` when
  `/home/mn/pfx64` doesn't exist.
- `6 forward-fallback`: in the fresh prefix, `MACNEUTRON_VM_D3D=forward . /opt/mn-wine/lib/wine/mn-d3d/prefix.sh`, then
  `WINEDEBUG=+loaddll wine 'C:\windows\syswow64\rundll32.exe' d3d11.dll,MnNoSuchEntry 2>&1 | grep -c
  'syswow64\\\\d3d11.dll.*builtin'` → `1` (rundll32 loads the DLL, then fails to find the entry without a message box).
- `7 low-fex`: a 32-bit `cmd.exe` held open (`sleep 20 | wine 'C:\windows\syswow64\cmd.exe' &`), its Linux pid found
  with `pgrep -f 'syswow64.cmd.exe'`, and `awk` over `/proc/<pid>/maps` counting mappings whose start is below
  `100000000` and whose name contains `FEXMem` → `0`. Also print every mapping below 4 GiB to the log (not a row).
- `wineserver -k` at the end.

- [ ] **Step 1: Write `eac-vm/tests/wow64.sh`** with the header comment (spec §5, how to run it, that the state disk's
  pfx64 is made on the first run with a 64-bit-only image).
- [ ] **Step 2: Run it on today's image**

Run: `MN_STATE=$PWD/build/eac-vm-work/state/wow64.img sh eac-vm/run-guest.sh eac-vm/tests/wow64.sh`
Expected: pfx64 made; rows 1-6 FAIL (no `i386-windows`, no `syswow64`), row 7 FAIL or `0` with no 32-bit process
found (then FAIL: the pid lookup is part of the row). Exit 1.

- [ ] **Step 3: Commit**

```bash
git add eac-vm/tests/wow64.sh
git commit -m "eac-vm tests: wow64.sh, 32-bit programs and prefix upgrades in the guest's Wine"
```

### Task 2: Forwarded overrides fall back, llvmpipe's CPU caps

**Files:**
- Modify: `eac-vm/d3d/guest/prefix.sh` (`mn_d3d_o` in forwarded mode)
- Modify: `eac-vm/guest/run-game.sh` (export beside `WINEDEBUG`)

- [ ] **Step 1: In forwarded mode `mn_d3d_o="d3d12,dxgi,d3d11,d3d10core,mnd3d=n,b"`**, and the header comment says a
  32-bit process finds no forwarding DLLs in `syswow64` and falls back to Wine's builtins (M4 adds 32-bit ones).
- [ ] **Step 2: `run-game.sh` exports `GALLIUM_OVERRIDE_CPU_CAPS=sse4.1`** with `eac.sh`'s comment (under FEX Mesa's
  CPU detection picks AVX2 paths LLVM then fails to select): a 32-bit game's Direct3D is llvmpipe until M4.
- [ ] **Step 3: `sh -n` both files.** Expected: no output. (Row 6 turns green in Task 3's image.)
- [ ] **Step 4: Commit**

```bash
git add eac-vm/d3d/guest/prefix.sh eac-vm/guest/run-game.sh
git commit -m "eac-vm guest: forwarded D3D overrides fall back to the builtins (32-bit processes); llvmpipe's CPU caps for games"
```

### Task 3: The WoW64 guest Wine in the image

**Files:**
- Modify: `eac-vm/wine/build.sh` (the `wine)` configure line and header)

- [ ] **Step 1: Configure `--enable-archs=i386,x86_64` with `i386_CC="clang --target=i686-windows -fuse-ld=lld"`**;
  under the p profile `i386_CFLAGS="$F"` beside `x86_64_CFLAGS="$F"`. Header: "for x86-64 Linux with new WoW64 (32-bit
  PE code over the 64-bit Unix side)".
- [ ] **Step 2: Build the stages** — `export MACNEUTRON_SIGN_IDENTITY=… ; sh eac-vm/image/guest-stages.sh wine dxmt
  d3d` (dxmt and d3d for Task 2's prefix.sh; FEX was built in M1). Expected: exit 0, and
  `eac-vm/builder.sh ssh 'ls wine-stage/opt/mn-wine/lib/wine/i386-windows/kernel32.dll'` lists it.
- [ ] **Step 3: Build the images** — `sh eac-vm/builder.sh sync eac-vm/image mn-image && sh eac-vm/builder.sh ssh 'sh
  mn-image/build.sh'` (the full build: base.erofs takes M1's FEX). Expected: exit 0; `build/eac-vm-work/images/*.erofs`
  dated now. Record `wine.erofs`'s size against today's 342810624 bytes in the commit message.
- [ ] **Step 4: Run the test** — Task 1's command. Expected: rows 1-7 `ok`, exit 0.
- [ ] **Step 5: Commit**

```bash
git add eac-vm/wine/build.sh
git commit -m "eac-vm wine: build i386 and x86_64 (new WoW64); wine.erofs <old> -> <new> bytes"
```

### Task 4: The launcher sends 32-bit games and installers to the VM

**Files:**
- Modify: `Sources/MacNeutronCore/Launcher.swift:97-108` (the VM decision before Preflight; the `run` skip)
- Modify: `Sources/MacNeutronCore/Preflight.swift` (`check`'s signature; `.thirtyTwoBit`'s text)
- Test: `Tests/MacNeutronCoreTests/MicroVMTests.swift`, `Tests/MacNeutronCoreTests/LauncherTests.swift`

**Interfaces:**
- Produces: `Preflight.check(_ layout: ToolLayout, request: LaunchRequest, inMicroVM: Bool = false) throws(PreflightError)
  -> String` — an i386 game passes when `inMicroVM`.
- `PreflightError.thirtyTwoBit`'s text: `"This game is 32-bit. On macOS MacNeutron runs 64-bit games only; 32-bit games
  run in its game VM."`

- [ ] **Step 1: Write the failing tests** in `MicroVMTests.swift` (an i386 PE from `peBytes(machine: PEImage.i386)`
  written at `<temp>/steamapps/common/Old Game/Game.exe`):

```swift
@Test func a32BitGameRunsInTheMicroVM() throws {
    let f = try makeVMFixture()
    #expect(f.launcher.launch(["waitforexitandrun", try i386GameInLibrary()], environment: f.env) == 0)
    #expect(f.runner.calls.map(\.tool) == ["run-guest.sh"])
    #expect(f.notifier.posted.isEmpty)
}

@Test func a32BitInstallerRunsInTheMicroVM() throws {
    let f = try makeVMFixture()
    #expect(f.launcher.launch(["run", try i386GameInLibrary(named: "Setup.exe"), "/quiet"], environment: f.env) == 0)
    #expect(f.runner.calls.map(\.tool) == ["run-guest.sh"])
    #expect(!f.launcherLog.contains("skipped 32-bit installer"))
}

@Test func a32BitGameOptedOutOfTheVMIsRefused() throws {
    let f = try makeVMFixture()
    var env = f.env
    env["MACNEUTRON_VM"] = "0"
    #expect(f.launcher.launch(["waitforexitandrun", try i386GameInLibrary()], environment: env) == 1)
    #expect(f.runner.calls.isEmpty)
    #expect(f.notifier.posted == ["This game is 32-bit. On macOS MacNeutron runs 64-bit games only; 32-bit games run in its game VM."])
}
```

  and `LauncherTests.swift`'s `thirtyTwoBitText` becomes the new text.
- [ ] **Step 2: Run them** — `swift test --filter 'MicroVMTests|LauncherTests' 2>&1 | tail -20`. Expected: the three new
  tests FAIL (refused / skipped), the LauncherTests text tests FAIL on the old message.
- [ ] **Step 3: Implement.** In `Launcher.launch`, before Preflight: `let inMicroVM = MicroVM.wanted(environment:
  environment, verb: request.verb) && MicroVM.guestPath(request.target) != nil`; pass it to `check`; the `run` skip
  only when `!inMicroVM` (comment: in the VM Proton's way, every installer runs). `check` lets `PEImage.i386` through
  when `inMicroVM`. The VM-not-ready refusal stays where it is.
- [ ] **Step 4: Run them** — the Step 2 command. Expected: all pass.
- [ ] **Step 5: Commit**

```bash
git add Sources/MacNeutronCore/Launcher.swift Sources/MacNeutronCore/Preflight.swift Tests/MacNeutronCoreTests/MicroVMTests.swift Tests/MacNeutronCoreTests/LauncherTests.swift
git commit -m "launcher: 32-bit games and their installers run in the VM; on macOS they stay refused"
```

### Task 5: `eac.sh` takes the game as parameters

**Files:**
- Modify: `eac-vm/tests/eac.sh`

**Interfaces:**
- Produces: variables read at the top, SMITE 2's values as defaults: `EAC_APPID` (2437170), `EAC_PREFIX`
  (`/home/mn/smite2`), `EAC_DIR` (`/mnt/games/SMITE 2/Windows`), `EAC_EXE` (`start_protected_game.exe`), `EAC_ARGS`
  (SMITE 2's arguments as one string, split by the shell), `EAC_EXPECT` (`301`; empty: report only). A run for another
  game prepends its assignments to the script (`run-guest.sh` passes no arguments to it), e.g. for Rogue Company:
  `EAC_APPID=872200 EAC_PREFIX=/home/mn/roguecompany EAC_DIR=… EAC_EXE=RogueCompanyEAC.exe EAC_EXPECT=`.

- [ ] **Step 1: Replace SMITE 2's literals with the variables**; the bootstrapper row prints the launcher's own result
  (`bootstrapper: Launcher finished with: N`, or `none`) and is `ok`/`FAIL` against `EAC_EXPECT` only when set; the
  client row (`easyanticheat_x64.so` mapped in a Wine process) stays as it is; the game-log cleanup stays SMITE 2's
  only when `EAC_APPID` is 2437170. Header: how to run it for another game.
- [ ] **Step 2: `sh -n eac-vm/tests/eac.sh`**, and `{ echo 'EAC_APPID=1'; sed -n '1,/^RUN=/p' eac-vm/tests/eac.sh; echo
  'echo "$SteamAppId $EAC_EXE"'; } | sh` prints `1 start_protected_game.exe`. Expected: as stated.
- [ ] **Step 3: Commit**

```bash
git add eac-vm/tests/eac.sh
git commit -m "eac-vm tests: eac.sh takes the game as parameters (SMITE 2's by default) and reports EAC's own result"
```
