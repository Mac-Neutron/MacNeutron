# DXMTRS step 2: winemetal in Rust Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** winemetal's Unix side (all 158 unix-call slots, both tables) and the Wine `winemetal.dll`'s PE thunks are
Rust, every slot pinned first by a native probe that ran against the C.

**Architecture:** A native Mac program, `wm_probe` (MacNeutron), loads one `winemetal.so` beside the VM host's
stand-ins (no Wine, no VM) and drives every slot: canonical handles, 0xA5-filled out-structs, retain-count deltas, and
recording proxies that print the exact messages and descriptor fields a handler sends. Its machine-independent lines
become per-group goldens recorded from the saved C build. A Rust staticlib, `winemetal` (mtld3d/unix, objc2), is linked
into `winemetal.so` and the native `winemetal.dylib` behind the unchanged C table; shared helpers move first, then ten
slot groups move in parallel lanes (each C handler replaced by a Rust export of the same symbol), then the tables. The
PE thunks move to a second staticlib, `winemetal-pe` (mtld3d/windows), for the Wine DLL only; both crates share the
wire mirrors in `winemetal-wire`.

**Tech Stack:** Rust 1.99.0, objc2 0.6.5 and its 0.3.2 framework crates, block2, dispatch2, meson, llvm-mingw,
Objective-C (the probe), system SQLite, Wine's winecrt0.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` (§5 step 2, §6, §7, §8). Inputs: the step 2 map,
`build/step2/map/0-porting-map.md` (synthesis), `1-unix-side.md` .. `5-tests.md` (readers), `6-critic.md`
(corrections), `7-decisions.md` (D1-D20, binding). Format follows `2026-10-29-dxmtrs-step1b-nvapi.md`.

Paths: `$D` = `build/wine-arm64-src/dxmt` (DXMTRS checkout), `$S` = `build/step2`, `U` =
`$D/src/winemetal/unix/winemetal_unix.c`. Commands run from MacNeutron's root unless a step says otherwise.

## Decisions

The maintainer's defaults (`7-decisions.md`, binding), short form:

- **D1** The Unix side stands on objc2 0.6 / objc2-metal, -metal-fx, -quartz-core, -foundation 0.3.2 (plus
  objc2-core-foundation, objc2-core-graphics, block2, dispatch2, libc: all already in `mtld3d/unix/Cargo.lock`), not on
  `mtld3d-mtl` (a client of this table). A new LGPL-2.1-or-later staticlib crate `winemetal` in `mtld3d/unix`, under
  mtld3d's CONVENTIONS and audit. SQLite through raw FFI. Cargo runs `--offline`.
- **D2** "The PE side onto mtld3d's transport" means Wine's unix-call handshake and typed calls inside
  `winemetal.dll`, with winemetal's slots, layouts, module name and exports; not `Thunks`, `mtld3d.dll`, `arp!` or a
  per-call pool. Spec §5 is corrected.
- **D3** The Mac WoW64 table keeps its 153 entries; `thunk32_*` keep their behaviour; the Rust table has the same
  entries.
- **D4** Objective-C exceptions behave as today: the crate builds with `panic = "unwind"` (profile
  `production-unwind`, passed by a profile argument to `tools/cargo-staticlib.sh`), handlers are `extern "C-unwind"`
  (objc2 0.6.5's sends are `C-unwind`: `ffi/message.rs:23`). No handler may panic on PE or guest data (P11).
- **D5** SharedEventListener: the use-after-free goes (destroy before start, stop before run become safe); success
  paths unchanged; DXMT's 10 ms sleep and the VM host's `events.m` stay.
- **D6** Code under OS gates below macOS 27 is not ported: slot 56's signal swap, the pre-27 HUD class, the
  `@available` 14-26 branches. Recorded in the spec.
- **D7** Behaviour is the C's: the same statuses (non-zero only from 121, 122, 153-155), the same ownership per field,
  the same outputs. Memory-safety fixes only where no output changes; leaks tied to ownership stay.
- **D8** Same-pin MacNeutron edits are allowed (licences, notices, cargo remaps, gate rows, probe programs).
- **D9** Every one of the 158 slots gets a probe case, dark slots included.
- **D10** Goldens are recorded on this Mac (macOS 27) from the C before any port. Gates: the native probe, check.sh
  x64 per group task, both lanes at milestones, facade and loopback tests, d3d9-gate, present-gate, the VM metal and
  DXMT gates against frozen C references. No new D3D11 rows for 84/85/88.
- **D11** No game bench; performance is C against Rust on present_loop, the DLSS cost row, check.sh's timing lines and
  the probe's call rate. The game bench is parked for the maintainer.
- **D12** The native dylib keeps `winemetal_thunks.c` until step 6 and keeps exporting the 12 `SM50*`; only the Wine
  `winemetal.dll`'s thunks become Rust (Task 18). One Rust staticlib per binary.
- **D13** Wire mirrors are hand-written; their size/offset asserts come from a compiled facts file. No bindgen.
- **D14** Slots 96, 97, 101 read AppKit off the main thread as the C does: `NSScreen`/`NSView`/`NSWindow` getters are
  re-declared `AnyThread` with objc2's extern macros under a written audit exception. No main-thread hop.
- **D15** The translator key moved with step 1's pin (e1ac8f03 → 71a7e5f2), accepted. Step 2's meson edits stay in
  `src/winemetal`, `src/winemetal/unix`, `src/nativemetal`; `airconv_thunks.[ch]` untouched; the key stays
  `71a7e5f22c50…`, computed before and after.
- **D16** `wmt.rs`'s mirrors stay in `mtld3d-mtl`; winemetal has its own; the duplicate is listed for §7.
- **D17** Helpers several lanes use land before the lanes fan out.
- **D18** The SQLite cache format is a seam: byte-identical files and lookups.
- **D19** The forwarder's reliances hold: a non-zero status drops the reply (statuses fixed); `NtSetEvent` runs on the
  slot-109 thread; `events.m`'s listener polling; handlers called with params the forwarder built.
- **D20** Review items: the presenter's swizzled selectors, `Retained` drop semantics (+0 handles never released),
  objc2 class-name collisions. Gates include d3d9-gate, present-gate and the loopback runs.

Rulings this plan adds (each in the ledger and, where it changes the spec, in Task 19):

- **P1** One pure-data crate, `winemetal-wire` (`mtld3d/unix/winemetal-wire`), holds the mirrors, the layout asserts,
  the slot numbers and the macro encodings; `winemetal` uses it, and so does `winemetal-pe` as a path dependency (as
  the windows workspace uses `mtld3d-shared`). D16's duplicate is then one copy, not two.
- **P2** `DXMT_NATIVE` code is behind a cargo feature `native`; `cargo-staticlib.sh` takes optional `<profile>` and
  `<features>` arguments.
- **P3** The guest's three extra exports are chosen by the `.def` (`winemetal.def`, 154 names; `winemetal-guest.def`,
  157), not a cargo feature: the crate always defines them.
- **P4** `thunk32_*` and the argument-chain converter (`U:2061-2388`) move verbatim into
  `src/winemetal/unix/sm50_wow64.c` (Wine build only) instead of being ported: they are unreachable, no gate reaches
  them, and D3 gives the SM50 converters to step 4. The Rust WoW64 table points at them.
- **P5** The Rust PE thunks zero-initialise every params struct. The one observable effect: slot 54's
  `MTLCaptureManager_startCapture` returned uninitialised stack (its handler never writes `ret`), now `false`.
- **P6** `main.c`'s `DllMain` becomes Rust (the handshake of D2); `winemetal_thunks.c` stays for the native build.
- **P7** Ownership is measured, not read from `eac-vm/metal/wm/calls.c`: where calls.c's `P1/AR/BR` disagrees with the
  C (e.g. slot 25's `ret_error`, retained by the C, `AR` in calls.c), the C wins and the line goes to the ledger.
- **P8** No `-C target-cpu`: the Rust side builds for `aarch64-apple-darwin`'s default (apple-m1) while the C had
  `-mcpu=$MACNEUTRON_CPU`; the probe's call rate decides (Task 9), and a gap goes to the maintainer.
- **P9** `cargo-staticlib.sh` passes the `--remap-path-prefix` set itself (the set `dxmt/mtld3d-build.sh:42-47`
  writes), so a clean release build of any DXMT Rust has no home or build path (the ordering the map flags).
- **P10** `objc2-app-kit` is not a dependency: D14's re-declarations are the only AppKit the crate uses.
- **P11** Every handler and C-named export holds `ffi::AbortOnPanic`: a Rust panic aborts, as `panic = "abort"` did,
  and never unwinds into the VM host's `catch (...)` (`eac-vm/d3d/host/native.cpp`); a foreign Objective-C unwind
  passes (D4).
- **P12** The listener's state (unstarted, running, destroyed) lives beside its first word, which holds only null or a
  retained run loop: a notification after destroy gives back its reference instead of spinning (D5's way, Task 13).
- **P13** `cfg(windows)` Rust gets rustc's `-Dwarnings` checks, not clippy: 1.99.0 has no clippy and stable no msvc
  standard library; clippy for 1.99.0 is a download that waits for the maintainer.
- **P14** The native dylib links with `b_asneeded` false and both images with `-weak_framework AppKit`: objc2 looks
  classes up by name, so the static references that kept MetalFX and AppKit linked go away; the dylib gains the
  QuartzCore and Cocoa entries its link line always named.

## Global Constraints

- Agents never push, merge to `main`, fetch, or download. Task 19's push, pin and the pinned build's re-fetch run
  under the maintainer's standing authorization, by the controller only.
- Never open MacNeutron.app, start or quit Steam, run a game, or touch `~/Library/Application Support/MacNeutron`.
  Kill by exact PID. Benches once. Home folder written `~`. Commits end with
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- DXMTRS work: branch `step2-winemetal` from `main` a7df84b in `$D`; lanes branch from it. MacNeutron work: branch
  `dxmtrs`, fast-forwarded to `main` (e0b0c0d) first.
- `make wine-arm64`, `eac-vm/dxmt/build.sh` and `eac-vm/host/build.sh` need `MACNEUTRON_SIGN_IDENTITY` and
  `MACNEUTRON_PROVISIONING_PROFILE` exported. The builder starts with `VM_SHARES` as the builder-out-share note says.
- Rust (three exact forms; `R=~/.rustup/toolchains`. `cargo +<toolchain>` works only through rustup's proxy, and
  1.99.0 has no clippy while stable has no msvc standard library, both checked):
  - Build and test: `PATH=$R/1.99.0-aarch64-apple-darwin/bin:$PATH cargo … --offline`. `cargo test -p winemetal`
    links an executable while the crate references `NtSetEvent`, `sqlite3_*`, `SM50*`, `thunk32_*` and
    `wmt_native_*`, which only the meson link provides: it runs with `--config
    'target.aarch64-apple-darwin.rustflags=["-C","link-arg=-Wl,-undefined,dynamic_lookup"]'` on the command line, never
    a committed config and never a stub in the crate (`cover.sh ported` checks the archive).
  - Lint: `PATH=$R/stable-aarch64-apple-darwin/bin:$PATH cargo clippy --offline --target aarch64-apple-darwin … --
    -D warnings` (pedantic, nursery; the target written out, since the windows workspace's config defaults to i686),
    for `aarch64-apple-darwin` only and, in `winemetal`, with and without `--features native`. Each msvc
    target (`aarch64-`, `arm64ec-`, `x86_64-`, `i686-pc-windows-msvc`) gets rustc's checks instead: `cargo build
    --offline --target <t> -p <crate> --config 'target.<t>.rustflags=["-Dwarnings"]'` on 1.99.0 (cargo appends it to
    the workspace's target rustflags). The ledger records that `cfg(windows)` code is checked by rustc only; clippy
    for 1.99.0 (`rustup component add clippy --toolchain 1.99.0`) is a download and waits for the maintainer's yes.
  - Anything that runs `tools/cargo-staticlib.sh` (`cargo "+<toolchain>"`):
    `PATH=<MacNeutron>/build/mtld3d-tools/bin:$PATH`, which `$S/lane-so.sh` and meson's build set themselves. Task 1 runs `sh dxmt/mtld3d-tools.sh` once; lanes never
    re-run it (parallel `ln -sf` races).
  - Docs: `RUSTDOCFLAGS="-D warnings" cargo doc --no-deps -p <crate>`. No rustfmt is installed: the fmt leg is skipped.
  - Audit: `git -C <tree> add -A mtld3d` first (the audit and `--update-derives` list files with `git ls-files`, so
    an untracked `.rs` is skipped unread), then `sh scripts/audit.sh` from `<tree>/mtld3d` must print `audit: clean
    (<n> files)` and exit 0, `<n>` grown by the number of new `.rs` files since the last audit.
  - mtld3d's CONVENTIONS: no `#[allow]`/`#[expect]` outside listed sites, `unsafe` one operation a block with
    `SAFETY:`, no `msg_send!`/`sel!`/`class!`, no lowercase `dxmt` in comments, no `pub(crate)`, tests in
    `<stem>/tests.rs`, derive inventory current.
  - Dependencies are D1's list only.
- Behaviour (D7, D19): same statuses, ownership, written and untouched bytes, and messages. No `unwrap`, `expect`,
  indexing or overflowing arithmetic on PE or guest values (D4).
- **Nil and raises** (D19: in the VM a raise becomes 0xC000000D and the call is refused, so a handler that answers 0
  where the C raised turns a refusal into a success):
  - A `None` receiver (`handle::obj` of 0) writes the zeros the C's nil send would and still writes every other field
    the C writes. A `None` argument is sent as nil, re-declaring the method with `Option<&T>` through
    `extern_methods!` where objc2 types the parameter non-null. Never skip a send, never make a null `&T`.
  - No `downcast`, `isKindOfClass` or `respondsToSelector` except where the C has them; no `NSArray::get`/`first`/
    `iter` on a guest index (the C's `objectAtIndex:` raises); no `extern_class!`/`class!`-style cached lookup for a
    class the C looks up by name (`AnyClass::get`, nil gives 0).
  - `mem_slice` answers `None` for (null, n>0); each caller does what the C does with that pointer.
- **Gate references**: from Task 1 Step 5 on, every gate run exports `MN_REF_FROZEN=$S/ref-vm` (present-gate:
  `$S/ref-present`), loopback and native gates included: they run `ref.sh` from `build/wine-arm64/wine.app`, which
  carries the Rust `.so` after Task 6c.
- Seams that do not move: slot numbers and the 158/153 table lengths; `winemetal.h`, `winemetal_thunks.h`,
  `airconv_thunks.h`, `airconv_public.h` byte-identical at their paths; `airconv_thunks.c`; `winemetal.so`'s install
  name, load commands, rpaths, minos 27.0 and the single `NtSetEvent` import; the native dylib's 142 C thunks, 5 hooks,
  12 `SM50*` and table; Objective-C class names per build; the cache database; both constructors' variables.
- Translator key: no edit to `include/`, the top-level `meson.build`/`meson.options`, `src/airconv`. The key is read
  with `sh -c 'ROOT=$PWD; . dxmt/pins; . wine-arm64/lib.sh; PATH="$(sh dxmt/toolchain.sh):$PATH"; translator_key
  build/wine-arm64-src/dxmt release'` and must print `71a7e5f22c50f03e79c56344f7a11819f2f922e67fe9ebe4f84ed9761d763311`.

## Execution

**Roles.** The controller runs the plan, creates worktrees, merges lanes and keeps the ledger
(`.superpowers/sdd/2026-10-29-dxmtrs-step2-winemetal/`: rulings, calls.c disagreements, flaky re-runs, deferred
Minors). One **pipeline owner** at a time (the controller by default) runs everything that uses the shared build or a
quiet machine: `make wine-arm64`, `dxmt/check.sh`, `eac-vm/*` builds and gates, `presenter/check.sh`, the facade test,
the probe's `rate` mode. **Lanes** use host tools only: cargo, clippy, audit, `$S/lane-so.sh`, `wm_probe` in
`fixed`/`full`/`table`/`native` modes, `cover.sh`.

**Shared state** (D17). Lanes run the one probe binary the owner built before fan-out, `WM_PROBE=$S/probe/wm_probe`,
against the goldens snapshot `$S/goldens/` (`wm-probe-check.sh --goldens $S/goldens`), and never build the probe or
edit a golden, a mask or a case. Goldens change only at a checkpoint merge, by the controller (G7's `returned` lands at
CP3). A missing case or a golden that must change goes to the controller. Lanes never touch
`mtld3d/docs/CONVENTIONS.md` or `mtld3d/scripts/audit.sh`: Task 6c writes every exception the lanes need.

**Lane procedure** (Tasks 7-16, and Task 18's host part). A group task's "Slots and exports" list is both the C
definitions the lane deletes and the Rust functions it exports under the same symbols:
`#[unsafe(export_name = "<symbol>")] pub unsafe extern "C-unwind" fn`, of type `ffi::Handler`, whose first statement
takes `ffi::AbortOnPanic`.
1. `git -C $D worktree add $S/wt-<lane> -b step2-<lane> step2-winemetal`, then point
   `submodule.external/nvapi.url` of the worktree at `$D/external/nvapi` (no network).
2. RED: export a stub for every handler of the group (`return 0`, writes nothing) and delete the C as the C rule below
   says. `$S/lane-so.sh $S/wt-<lane> $S/host-<lane>` then `WM_PROBE=$S/probe/wm_probe sh dxmt/wm-probe-check.sh
   --goldens $S/goldens --dir $S/host-<lane>`: every case of the group differs but those its list marks
   `stub-same` (the C writes nothing there either; any further one the controller ledgers), and no other group's
   row FAILs (g0's
   `aniso-env` goes through slot 19 and fails with G2's; the setup rule keeps every other case to its own group's
   slots).
3. GREEN: the port. The same check: every group ok; `wm-probe-check.sh … --full <group>` (the `full` output against
   `$S/goldens/<group>.full`, masked by `<group>.mask`) passes; `sh dxmt/tests/wm_probe/cover.sh ported
   $S/wt-<lane> <the lane's libwinemetal.a> <group>` passes.
4. Unit tests, clippy (both feature sets), the msvc builds where the crate has them, doc and audit, in the Global
   Constraints' forms: clean. The reviewer greps the lane for the Nil and raises rule's forbidden forms and for
   `unwrap|expect|\[` on params-derived values.
5. Commit; hand to the controller, who rebases the lane on `step2-winemetal`, fast-forwards, and runs the lane's
   checkpoint when all its lanes are in.

**The C rule.** A lane replaces each C handler it ports with one line, `NTSTATUS <symbol>(void *obj);` (the symbol
from `slots.txt`: `_<Name>`, or `thunk_SM50*` for G10; G8's five are already declared at `U:3550-3554`), where the
`static NTSTATUS` line stood, deletes the rest of the definition, and leaves the blank line after it and every
`#if`/`#endif` line alone. Lanes never touch the table, another group's lines, `Cargo.toml`, `lib.rs`'s module list,
the derive inventory, CONVENTIONS, the audit script, the probe or another lane's files. A missing derive,
dependency, float cast, exception or probe case goes to the controller.

**Order and parallelism.**

| Task | Runs | Depends on |
|---|---|---|
| 1 references, kit | owner | — |
| 2 probe driver | one agent (Opus) | 1 |
| 3, 4, 5 probe cases | parallel, MacNeutron worktrees `$S/wt-mn-a`..`c` | 2 |
| 3-5 merge: probe binary, goldens snapshot, kit control | controller | 3, 4, 5 |
| 6a plumbing | one agent (Opus), owner for its gate | 1 |
| 6b wire crate and layout generator | one agent (Opus) | 6a |
| 6c shared helpers, constructors, exceptions | one agent (Opus), owner for its gate | 6b, the 3-5 merge |
| 7-16 slot groups | parallel lanes | 6c |
| 18 PE thunks, host part | parallel lane | 6b |
| CP1 (7, 10, 14, 15), CP2 (8, 11, 16), CP3 (9, 12, 13) | owner | their lanes |
| 17 tables | owner (Opus) | CP1-CP3 |
| 18 integration | owner | 17 |
| 19 notices, spec, push, pin | controller | 18 |

Models: Opus for 2, 4, 5, 6a-6c, 9, 12, 13, 16, 17, 18 and every review; Sonnet for 3, 7, 8, 10, 11, 14; Haiku for
15 and for export/import/layout diffs.

**Checkpoints.** `make wine-arm64`; the translator key; `sh dxmt/check.sh` (x64: 412 ok, 0 FAIL; the flaky rows
`d3d11 fullscreen bg-leave` and `ts-many` get one re-run, ledgered); `WM_PROBE=$S/probe/wm_probe sh
dxmt/wm-probe-check.sh --goldens $S/goldens` on the built `winemetal.so`, `fixed` and `--full`; `wm_probe native` on
the native build's `winemetal.dylib` (CP2, CP3); then each merged lane's Integration list, under
`MN_REF_FROZEN=$S/ref-vm`. The controller then applies the goldens edits the checkpoint's lanes asked for to
`$S/goldens` and the MacNeutron goldens in one ledgered commit. The checkpoint's x64 run is the x64 gate of each of
its lanes (D10). A failing checkpoint is bisected by lane.

## Rules check (mtld3d's CLAUDE.md)

- **Statics:**
  - `FORCED_MAX_ANISOTROPY: AtomicU32` (exported as `forced_max_anisotropy` until Task 17): process-wide, read from the
    environment once at load, the C's static.
  - Two `#[used]` constructor pointers in `__DATA,__mod_init_func`: not state.
  - G6's display-setting table (2 entries behind a `Mutex`): process-wide, no object reaches it; the C's
    `g_display_settings`.
  - The native hooks (`native` feature): memory alloc/free/context (G2), present hook (G3), WSI hooks (G6), set once by
    the host before any device: process-wide, the C's statics.
  - The two tables (Task 17): the image's exports, not state.
  - The lazy `compression_mode` selector is not kept: `Sel::register` per texture.
- **Config keys:** none new (`DXMT_MAX_ANISOTROPY`, `MACNEUTRON_PRESENT` exist).
- **Wire fields:** none new; `winemetal-wire` mirrors existing layouts.
- **Dependencies:** `winemetal`: objc2, objc2-foundation, objc2-metal, objc2-metal-fx, objc2-quartz-core,
  objc2-core-foundation, objc2-core-graphics, block2, dispatch2, libc, `winemetal-wire` (all in the lock; MIT, Zlib or
  dual). `winemetal-pe`: `winemetal-wire` only. Path crates: the three new ones.
- **Derives:** `Clone, Copy` on the wire mirrors (plain data), added to the inventory in Task 6b. Lanes add none.
- **Lint suppressions:** none: the layout generator maps C names to Rust casing, so no `non_snake_case` or
  `non_upper_case_globals` allow is needed. A new allow site, `unix/winemetal/src/float_cast.rs` (the float and
  narrowing conversions the C does implicitly), with its CONVENTIONS exception, in Task 6c.
- **CONVENTIONS exceptions**, all written in Task 6c before the lanes fan out (lanes never edit CONVENTIONS or the
  audit script): D4 (panic policy, `production-unwind`, the `AbortOnPanic` guard); D14 (AnyThread re-declarations of
  `NSScreen`/`NSView`/`NSWindow` getters, used by Task 12); private selectors declared with `extern_methods!` on local
  re-declarations (`#[name = "…"]`) anywhere in `unix/winemetal/src/` (Tasks 6c, 10, 12, 13), with the audit's
  ALLOW_SITES to match.
- **Duplicates:** `winemetal-wire` against `mtld3d-mtl`'s `wmt.rs` (D16, for §7); `winemetal_thunks.c` (native)
  against `winemetal-pe` (D12, until step 6); the D14 re-declarations against objc2-app-kit's types.

## Review Focus

1. **A guest hands a handle of the wrong class, or an index past the end.** Expected: Metal's exception leaves the
   Rust handler (no `AbortOnPanic` abort: a foreign unwind doesn't set `panicking()`) and the VM host's `@catch`
   answers 0xC000000D; the VM survives. A Rust handler that answers 0 with nil instead is a bug (Global Constraints,
   Nil and raises). Tests: probe `x-*` cases (Task 2 prints `raised <name>`; `x-array-past-end` in Task 7, one per
   group whose handlers cast a handle in Tasks 8-13; each, once its group is Rust, shows a raise passing
   `AbortOnPanic` to the probe's `@catch`), Task 6c's abort unit test, the lane grep, and `memory-test` at CP2 and
   CP3.
2. **Out-of-range guest values** (count 0 with a null array, `start` past the end, an unknown command type, more
   attachments than Metal has). Expected: what the C does (skip, zero), never a Rust panic (a panic aborts under
   `AbortOnPanic`, never unwinds into the VM host's `catch (...)`). Tests: Task 8's `residency-empty`, Task 9's
   `enumerate-past-end` and `*-unknown-type`, and the reviewer's grep of each lane for `unwrap|expect|\[` on
   params-derived values.
3. **A +0 handle dropped by Rust, or an input over-released.** Expected: `ret_autoreleased`/`ret_borrowed` as each
   group's Hazards name per field, never a `Retained` dropped after taking its pointer, never `Retained::from_raw` on
   an input. Tests: `full` mode runs with `NSZombieEnabled=YES` (Task 2); the `own=`, `pool=` and `in<k>=` fields on
   every handle-out and probe-made input (Task 2's ownership rule; every group).
4. **Two winemetal images in one process** (`macneutron-vm` loads the Wine `.so` and the native dylib). Expected: no
   class-name collision, both constructors run. Test: Task 14's `pair` run and the loopback gate at CP1.
5. **i686 callers of the by-value-struct thunks** (`MTLTexture_replaceRegion`, `MTLTexture_getBytes`,
   `MTLTexture_newTextureView`, `MTLDevice_heap{Buffer,Texture}SizeAndAlign`), and every narrow or enum argument and
   return (`uint16_t`, `uint8_t`, `bool`, `enum … : uint64_t`). Expected: the C's stack layout and return registers.
   Test: Task 18's generated per-target prototype asserts (every `WINEMETAL_API` function), its i686 disassembly
   comparison, then `smoke32` in both WoW64 modes.

---

### Task 1: C references, frozen gates and the lane kit (owner)

**Files:**
- Modify (MacNeutron): `eac-vm/dxmt/gate.sh`, `eac-vm/dxmt-native/gate.sh`, `eac-vm/dxmt/present-gate.sh`: with
  `MN_REF_FROZEN=<dir>`, that folder is the macOS reference and `ref.sh` is not run. `eac-vm/dxmt/ref.sh`:
  `MN_REF_WINE_APP=<wine.app>` overrides `build/wine-arm64/wine.app` (default unchanged), so `$S/ref-wine.app` can
  re-record the reference after the `.so` is Rust.
- Commit (MacNeutron `dxmtrs`): this plan, `docs/superpowers/plans/2026-10-29-dxmtrs-step2-winemetal.md`.
- Create (uncommitted): `$S/lane-so.sh`, `$S/kit/`, `$S/ref-*`.

**Interfaces:**
- Produces:
  - `$S/ref-mac/`: build-tree `winemetal.so` (unstripped) as `winemetal.unstripped.so`, the installed
    `aarch64-unix/winemetal.so` and `aarch64-windows/winemetal.dll`, `winemetal.dll.a`, copies of
    `winemetal_thunks.c` and `main.c`. Also `exports.txt` (`llvm-readobj --coff-exports winemetal.dll`) and
    `imports.txt` (`--coff-imports` of `winemetal.dll`, `d3d11.dll`, `d3d12.dll`, `dxgi.dll`, `nvapi64.dll`).
  - `$S/ref-guest/{x86_64,i386}-windows/`: the same for the guest build.
  - `$S/ref-native/`: `winemetal.dylib`, `nm -gU` and `otool -L` of it, `nm -u` of the native `d3d11`/`d3d12`
    dylibs.
  - `$S/ref-wine.app` (`cp -cR` of `build/wine-arm64/wine.app`).
  - `$S/ref-host/`: `ref-mac`'s unstripped `winemetal.so` beside the stand-ins
    (`make -s -C eac-vm/metal/wm metal-host HOST=$S/ref-host WINEMETAL=…`).
  - `$S/ref-vm`, `$S/ref-present`, `$S/ref-metal/metal-wm-host.txt`, `$S/ref-perf/`.
  - `$S/lane-so.sh <dxmtrs-tree> <out-dir>`: sets `PATH=<MacNeutron>/build/mtld3d-tools/bin:$PATH` and builds
    `<tree>`'s `winemetal` crate with `<tree>`'s own `tools/cargo-staticlib.sh 1.99.0 aarch64-apple-darwin winemetal
    $S/cargo-<basename of tree> <lib> production-unwind` (skipped while the crate doesn't exist). It compiles
    `<tree>`'s remaining `src/winemetal/unix/*.c` with the kit's flags and links `<out-dir>/winemetal.so` with the
    kit's link line plus the Rust library, always appending `-Wl,-u,___wine_unix_call_funcs
    -Wl,-u,___wine_unix_call_wow64_funcs -weak_framework AppKit` (harmless while the C defines both tables and
    references AppKit; once the C is gone, nothing else pulls the archive's members). Then it adds the stand-ins (the
    `metal-host` rule) and prints the `.so`'s path.

- [ ] **Step 1: Branches.** `git -C $D switch -c step2-winemetal`; MacNeutron `git switch dxmtrs && git merge --ff-only
  main`, then commit this plan on `dxmtrs` (`Plan: DXMTRS step 2, winemetal in Rust`). `sh dxmt/mtld3d-tools.sh` once.
  Record `git -C $D rev-parse HEAD` (a7df84b) and the translator key (Global Constraints) in the ledger.
- [ ] **Step 2: Save the C.** After `make wine-arm64 dxmt-tests dxmt-tests-arm64ec presenter`, `sh
  eac-vm/dxmt/build.sh` and `sh eac-vm/dxmt-native/build.sh`, copy the artifacts above. Expected: every listed file
  exists; `exports.txt` lists 154 names (guest: 157).
- [ ] **Step 3: The kit.** From `$D/../dxmt-build/compile_commands.json` take the commands for `winemetal_unix.c` and
  `cache.c`. From `ninja -C $D/../dxmt-build -t commands src/winemetal/unix/winemetal.so | tail -1` take the link line.
  Copy the libraries it names from `dxmt-build` (airconv's) and the two Wine `.so`s into `$S/kit/`, so lanes never
  read `dxmt-build`; LLVM's stay in `llvm-arm64`.
  Test: `sh $S/lane-so.sh $D $S/kit-check`, then `otool -L` and `otool -l | grep -A2 LC_RPATH` of the result equal
  `ref-mac`'s unstripped copy's, and `nm -gU` lists `___wine_unix_call_funcs` and `___wine_unix_call_wow64_funcs`.
  (The kit's compile flags are checked against the goldens at the 3-5 merge: a lost `-DNDEBUG` alone makes the
  unknown-type cases abort.)
- [ ] **Step 4: Frozen references.**
  - The edits: each gate takes `REF=${MN_REF_FROZEN:-…}` and skips `ref.sh` when it is set; `ref.sh` takes
    `MN_REF_WINE_APP`.
  - Record them: `sh eac-vm/dxmt/share.sh` with `MN_DXMT_ONLY` unset (the share's invocations are whatever the last
    `share.sh` filtered: today step 1b's 7), then `MN_REF_RUNS=2 MN_REF_OUT=$S/ref-vm sh eac-vm/dxmt/ref.sh`.
    Expected: the 44 names of `eac-vm/dxmt/invocations` × runs {1,2} × {txt,status,time} in `$S/ref-vm` (264
    files); a non-zero status is ledgered.
  - After `sh eac-vm/host/build.sh` and `sh eac-vm/metal/build-guest.sh` (the metal gate's header), run
    `sh eac-vm/metal/gate.sh` once and copy `metal-wm-host.txt` to `$S/ref-metal/`.
  - Run `sh eac-vm/dxmt/present-gate.sh` once and copy its `present-ref` to `$S/ref-present`.
- [ ] **Step 5: Baselines on C**, each once, with `MN_REF_FROZEN=$S/ref-vm` exported, outputs kept in `$S/ref-perf/`
  and the ledger:
  - both check.sh lanes (x64 `sh dxmt/check.sh`; ARM64EC as `make dxmt-check`'s third line). Expected: 412 ok,
    0 FAIL each;
  - `sh eac-vm/dxmt/gate.sh` (`MN_DXMT_ONLY` unset: all 44) after the guest stages (`guest-stages.sh dxmt`, image
    build `wine`);
  - `sh eac-vm/dxmt-native/gate.sh` and `eac-vm/dxmt-native/tests/{build,hook,win32}.sh`;
  - `eac-vm/d3d/loopback-gate.sh`, `eac-vm/dxmt/d3d9-gate.sh`, `eac-vm/tests/d3d-smoke.sh`;
  - `dxmt/mtld3d-facade-test.sh`, `presenter/check.sh`.

  A row that fails on C is ledgered as pre-existing, not blamed on the port.
- [ ] **Step 6: Commit** (MacNeutron `dxmtrs`): `eac-vm gates: MN_REF_FROZEN, a saved macOS reference`.

### Task 2: wm_probe's driver, the slot manifest and the coverage script (MacNeutron)

**Files:**
- Create: `dxmt/tests/wm_probe/{probe.h,probe.m,recorder.m,swizzle.m,cases_g0.m … cases_g10.m,g0.fixed … g10.fixed,
  slots.txt,cover.sh}`, `dxmt/wm-probe-check.sh`.
- Modify: `Makefile` (target `wm-probe`).

Task 2 provides every helper the case lists below name; Tasks 3-5 touch only `cases_gN.m`, their goldens, masks and
`cache-c.db`, and ask the controller for anything else.

**Interfaces:**
- Produces:
  - `make wm-probe [DXMT_SRC=<dxmtrs>/src] [WM_PROBE_OUT=build/wm-probe]` builds `$WM_PROBE_OUT/wm_probe`: clang,
    `-fno-objc-arc -O1 -g`, no sanitizer, with `eac-vm/metal/wm/host/loader.c`, `-I$DXMT_SRC/winemetal
    -I$DXMT_SRC/airconv`, Foundation, Metal, MetalFX, QuartzCore, AppKit, CoreGraphics, libsqlite3.
  - `wm_probe <host-dir> fixed|full [g0 … g10]`: loads `<host-dir>/ntdll.so` then `winemetal.so` (loader.c), copies
    its `__wine_unix_call_funcs` into a probe-held table, and runs the groups' cases (all when none is named). One line
    a call result: `<group> <slot> <case> <status-hex> [<fields>]`.
  - `wm_probe native <dylib> fixed|full [g0 … g10]`: `dlopen`s the native `winemetal.dylib` directly (its table and
    its `WMTSet*` hooks), runs every case but the `WM_WINE_ONLY` ones, and the native-only ones (`WM_NATIVE` flag:
    memory hooks for 18, the present hook before `presentDrawable:` on a recording command buffer, WSI set/get, 104
    with an `mn_win32` event and its reference count restored after the set, the cache class names). Golden:
    `<group>.native`, the complete `fixed` output of the native run, recorded from `$S/ref-native/winemetal.dylib`.
  - `wm_probe <host-dir> table`: `table <slot> <offset-from-image-base|null>` for 0-157, and `wow64 <slot> …` for the
    WoW64 table when the image exports one.
  - `wm_probe <host-dir> rate`: `rate <slot> <ns per call>` for 36, 37 and 38. Each is the median of 5 runs of 10 000
    calls on a fixed 64-command list with real encoders.
  - `wm_probe <host-dir> pair <native-dylib>`: loads both images in one process and runs slot 117 through each table.
  - `wm_probe --list`: `<group> <slot> <case>` for every case, without loading.
  - Case tables: each `cases_gN.m` exports `const struct wm_case wm_cases_gN[]`. An entry is `{group, slot, name,
    flags, run}`, the flags being `WM_ISOLATE`, `WM_ENV_ANISO`, `WM_ENV_PRESENT`, `WM_FULL_ONLY`, `WM_NATIVE`,
    `WM_WINE_ONLY` (the case needs a Wine stand-in the native dylib doesn't have: the stand-in `NtSetEvent` hook, the
    macdrv stand-in; `native` mode skips it), `WM_GMALLOC` (an isolate child under
    `DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib`) and `WM_NOT_ON_C` (a case kept out of the C goldens:
    nondeterministic or unsafe on C; its lines are printed, and compared only once the golden holds a line for that
    case); the list ends with a zero entry. Task 2 leaves them empty.
  - `probe.h` helpers:
    - `wm_call(slot, params)`: through the probe-held table. `WM_STUB=<slot>[,<slot>…]` replaces those entries with a
      stub that returns 0 and writes nothing. It counts calls per slot (`--counts` prints `calls <slot> <n>` per case),
      prints `cross <slot>` when a case calls a slot outside its group, and prints `in<k>=<delta>` for each probe-made
      input registered with `wm_in(obj)` whose retain count the call changed;
    - `wm_h(handle)`: `h<n>`, numbered by first appearance in each group; a handle equal to the fill pattern, or 0
      where the case expects one, stops the case with `<group> <slot> <case> bad-handle`;
    - `wm_fill(p, size)`: 0xA5;
    - `wm_bytes(p, size)`: prints which bytes changed;
    - `wm_own(obj)`, `wm_in(obj)`, `wm_send_refs(port)` (`send-refs=<n>`, `mach_port_get_refs` of the send right);
    - `wm_rec(target, protocol)`, `wm_rec_ret(recorder, selector, value)` (a canned return for one selector on a
      recorder without a target: a fresh `NSError`, an array, a number);
    - `wm_dump(descriptor)`;
    - `wm_word(handle)`: the first 8 bytes behind a handle;
    - `wm_heap_growth(block, cycles)`: `malloc_zone_statistics(NULL, …)`'s `size_in_use` before and after running
      `block` `cycles` times: `heap-growth=<bytes per cycle>` in `full`, `freed` or `leaked` (64 bytes a cycle) in
      `fixed`;
    - `wm_window()`: a window on the main screen holding a layer-backed `NSView` with a `CAMetalLayer`, made on main,
      also handed to the winemac stand-in (`eac-vm/metal/wm/host/winemac.m`) for 72/73;
    - `wm_readback(texture|buffer)`: bytes through a blit into a Shared buffer;
    - `wm_nsarray(n)`: a probe `NSArray` of `n` fresh objects;
    - `wm_tmpdir()`: a `mkdtemp` folder under `$TMPDIR`, removed at exit; every case that writes files (g8's
      databases and locks, g9's capture, g4's archive URL) uses it, so parallel probes never collide.
  - **Ownership rule.** Each call runs inside a probe pool (`objc_autoreleasePoolPush`):
    - `own=<delta>`, raw: for a fresh object (a constructor) the count left after the pool pops, minus the probe's own
      retain; for the same object from every call (a getter) the growth over two calls. A tagged pointer or a count
      ≥ 2^31 (a singleton): `own=n/a`. Slot 10 is excluded (`NSAutoreleasePool` raises on retain; `pool-drain`
      covers it).
    - `pool=+1|+0`: the result's retain count just before the probe pops its pool minus just after. `+1` is
      `ret_autoreleased`, `+0` `ret_borrowed` or owned: `own=` alone can't tell them apart once the pool popped.
    - `in<k>=<delta>` on inputs (above), `send-refs=` after 120, 123 and 124.
    - Every handle-out field gets `own=` and `pool=`, every `ret_error` included.
  - **Setup** comes from Metal directly in the probe (`MTLCreateSystemDefaultDevice`, its queues, buffers, textures;
    a library by `newLibraryWithData:` on a `dispatch_data_create`; an event's value by `signaledValue`; a release by
    `[obj release]`), never from another group's slot, so a group's rows depend only on its own handlers. The one
    listed exception is g0's `aniso-env` (slot 19). Fixtures are read from `dxmt/tests/wm_probe/` (`WM_PROBE_DIR`
    overrides).
  - **Recorder** (`recorder.m`): an `NSProxy` that prints `msg@<thread> <selector> <args>` for every message
    (`@main`, `@worker` or `@other`) and forwards it to its target when it has one; without one it returns the canned
    value or zero. Signatures come from `protocol_getMethodDescription` on the protocol given. Arguments print as
    integers, `%a` doubles, struct bytes in hex; pointer arguments are dereferenced by the paired count or length (a
    per-selector table: `setBytes:length:atIndex:`, `setVertexBytes:length:atIndex:`,
    `setFragmentBytes:length:atIndex:`, `setViewports:count:`, `setScissorRects:count:`, `addAllocations:count:`,
    `removeAllocations:count:` and every other `…:count:`/`…:withRange:` form the command lists send); objects print
    by kind (`NSString` quoted, `NSURL` by `absoluteString`, `NSNumber` by value, `NSData` by sha256, arrays and
    dictionaries recursively, `MTL*`/`MTLFX*` descriptors through `wm_dump`, anything else through `wm_h`).
  - **Descriptor dump** (`wm_dump`): every readable property along the class chain, by `class_copyPropertyList`
    and `valueForKey:` in `@try` (`name=!` on a raise), sorted by name. It recurses into object-valued properties of
    `MTL*`/`MTLFX*` classes (cycle guard, depth 3: depth/stencil and front/back stencil descriptors, vertex
    descriptors), into attachment and layout arrays by `objectAtIndexedSubscript:` 0-7 (31 for vertex descriptors),
    and reads an explicit private-key list (`logicOperationEnabled`, `logicOperation`, `compressionMode`, and any key
    a case names) by `valueForKey:` in `@try`.
  - **Swizzles** (`swizzle.m`, on when a case asks): `-[MTLFXTemporalScalerDescriptor newTemporalScalerWithDevice:]`,
    `-[MTLFXSpatialScalerDescriptor newSpatialScalerWithDevice:]` and
    `-[MTLFXFrameInterpolatorDescriptor newFrameInterpolatorWithDevice:]` `wm_dump` the descriptor, then call
    through; `-[MTLFunctionConstantValues setConstantValue:type:atIndex:]` prints the bytes by type, the type and the
    index, then calls through.
  - **Threads**: `main` runs `CFRunLoopRun()`, and the cases run on one worker thread, so `dispatch_sync` to the main
    queue (70, 93) and the winemac stand-in work. `WM_ISOLATE` cases run in a child (`posix_spawn` of the probe with
    `--case <group> <name>`) with a 10 s limit that prints `hang`. `wm_main_blocked(block)` runs `block` on the
    worker while main waits on a semaphore (10 s: `hang`).
  - **Env cases** re-spawn the probe with `DXMT_MAX_ANISOTROPY` or `MACNEUTRON_PRESENT` set: the constructors run at
    load.
  - **Modes**: `fixed` prints machine-independent fields and relations (`nonnull`, `=contents`, `count>=1`, `<=`, and
    oracle equalities against the returned object's own getters, e.g. `gpu_address=gpuAddress`); `full` prints raw
    values and runs with `NSZombieEnabled=YES`; `raised <exception name>` when a call raises in the probe's `@try`.
  - `slots.txt`: 158 lines `<slot> <group> <symbol>`; 83 has symbol `-`. The groups:
    - g0: 83, 153-155;
    - g1: 0-8, 10, 24, 27, 28, 49-52, 60, 61, 86, 87, 111, 114, 125, 148, 149;
    - g2: 18-23, 39-46, 107, 132-143, 145;
    - g3: 9, 11-14, 17, 30-33, 36-38, 47, 48, 89-91, 102, 127-130, 147;
    - g4: 25, 26, 29, 34, 35, 98, 112, 113, 131, 144, 156, 157;
    - g5: 56-59, 68, 69, 150-152;
    - g6: 62-67, 70-73, 92-97, 99-101;
    - g7: 15, 16, 103-106, 108-110, 120-124, 126, 146;
    - g8: 115-119;
    - g9: 53-55;
    - g10: 74-82, 84, 85, 88.
  - `cover.sh slots` (each of 0-157 once; group counts 4, 26, 28, 24, 12, 9, 19, 16, 5, 3, 12).
  - `cover.sh cases <wm_probe> <host-dir> [<group>]`: each slot has a case; by `--counts`, every slot of the group is
    called at least once; one `blit-`/`compute-`/`render-` case per value of `WMTBlitCommandType`,
    `WMTComputeCommandType` and `WMTRenderCommandType` in `winemetal.h`; no `cross` line but the listed exception.
  - `cover.sh bites <wm_probe> <host-dir> <group>`: for each slot of the group, `WM_STUB=<slot> wm_probe <host-dir>
    fixed <group>` differs from the golden in at least one line naming that slot. A slot it can't bite is ledgered with
    the reason; frees bite through `heap-growth`.
  - `cover.sh table <host-dir> <image>` (the table's offsets, named by `nm -a <image>`, equal `slots.txt`).
  - `cover.sh ported <dxmtrs-tree> <libwinemetal.a> <group…>`: every symbol of those groups is `T` in the archive
    and has no definition in the tree's `src/winemetal/unix/*.c`; every other group the reverse; and `nm` of the
    archive shows `U`, never `T`, for `_NtSetEvent`, `_SM50*`, `_thunk32_*`, `_macdrv_functions`, `_sqlite3_*` and
    `_wmt_native_*` (a stub defined in the crate would end up inside `winemetal.so`).
  - `dxmt/wm-probe-check.sh [--dir <host-dir> | --so <winemetal.so> | --native <dylib>] [--goldens <dir>]
    [--full <group…>]`: by default `build/wine-arm64/wine.app`'s `.so` beside fresh stand-ins, and the committed
    `fixed` goldens. With `WM_PROBE=<path>` it runs that binary and never builds; otherwise it builds with `make
    wm-probe`. It runs one process per group: `fixed` for g0-g10 against `<group>.fixed` (`<group>.native` with
    `--native`), skipping the lines of a `WM_NOT_ON_C` case the golden doesn't hold yet, or with
    `--full` the `full` output against `<goldens>/<group>.full` with `<group>.mask` applied (the keys listed there read
    `<var>` on both sides and must be present). It prints `ok   wm-probe <group>`, `FAIL wm-probe <group>: <first
    differing line>` or `FAIL wm-probe <group>: crashed (signal <n>)`; it exits 1 on a FAIL.

- [ ] **Step 1: Failing check.** `sh dxmt/tests/wm_probe/cover.sh slots` with `slots.txt` absent. Expected: FAIL.
- [ ] **Step 2: Manifest and table mode.** Write `slots.txt` (symbols from `U:3556-3715`'s table: `_<Name>`,
  `_MTLCopyAllDevices` at 4, `thunk_SM50*` at 74-88, `_MacNeutron_guestOnly` at 153-155) and the table mode. Run
  `cover.sh slots` and `cover.sh table $S/ref-host $S/ref-mac/winemetal.unstripped.so`, then the same on
  `$S/kit-check`. Expected: pass.
- [ ] **Step 3: Mutation.** Swap two symbols in `slots.txt`. Expected: `cover.sh table` FAILs naming both slots.
  Restore.
- [ ] **Step 4: The driver.** Write a scratch case of each kind outside the case tables:
  - a recorder on a real `MTLDevice` creating a sampler (the `msg@worker` lines and the descriptor dump);
  - `wm_own` on slot 4's array (`own=+1 pool=+0`) and on slot 7's name (`own=+0`, `pool=` as the C does);
  - an `x-` call of slot 39 on a buffer (`raised NSInvalidArgumentException`);
  - an isolate case that hangs (`hang`), one that crashes (`crashed (signal 11)` from `wm-probe-check.sh`), and one
    that gets a 0xA5 handle (`bad-handle`);
  - the same sampler case under `WM_STUB=19` (it differs) and with a call of slot 0 (`cross 0`);
  - `wm_probe native $S/ref-native/winemetal.dylib fixed g0`.

  Expected: those lines, twice identical. Then delete the scratch case.
- [ ] **Step 5: Empty goldens.** `sh dxmt/wm-probe-check.sh --dir $S/ref-host` and `--native
  $S/ref-native/winemetal.dylib`. Expected: 11 `ok` lines each.
- [ ] **Step 6: Commit:** `dxmt tests: wm_probe, winemetal's slots driven natively; the slot manifest`.

### Tasks 3-5: the probe cases, recorded from the C (parallel, MacNeutron worktrees)

Each task writes the **Probe cases** of its groups, listed in the group's task below and in Task 6c for g0. Each
runs in its own worktree (`git worktree add $S/wt-mn-<a|b|c> -b step2-probe-<a|b|c> dxmtrs`), builds the probe there
with `make wm-probe DXMT_SRC=$D/src WM_PROBE_OUT=$S/probe-<a|b|c>`, and runs against `$S/ref-host` (and, for
`WM_NATIVE` cases, `$S/ref-native/winemetal.dylib`) only. It touches only its groups' `cases_gN.m`, goldens
(`gN.fixed`, `gN.native`), `$S/ref-probe/gN.{full,mask}` and `cache-c.db`; a missing helper goes to the controller
(Task 2's driver).

- **Task 3** (Sonnet): g0 (Task 6c), g1 (Task 7), g4 (Task 10), g8 (Task 14), g9 (Task 15). It also commits
  `dxmt/tests/wm_probe/cache-c.db`, the database the C writer leaves after Task 14's `writer-reader` sequence (a
  few KB), for Task 14's `cross-read`.
- **Task 4** (Opus): g2 (Task 8), g3 (Task 9). The owner then records `wm_probe $S/ref-host rate >
  $S/ref-probe/rate.txt` once, on a quiet machine.
- **Task 5** (Opus): g5 (Task 11), g6 (Task 12), g7 (Task 13), g10 (Task 16).

Steps, per group:
- [ ] **Step 1: Cases.** Write them. `WM_NOT_ON_C` cases (e.g. G7's race) are written but not recorded.
- [ ] **Step 2: Two runs on C.** Run `fixed` twice and `full` twice. Expected: `fixed` identical; a line that differs
  becomes a relation (an oracle equality where the probe holds the object). The `full` keys (case and field) that
  differ between the two runs go to `$S/ref-probe/<group>.mask`, kept small by relations (ledgered: the mask's size
  per group); `full` with the mask applied is then identical over both runs.
- [ ] **Step 3: Coverage.** `cover.sh cases` for the group. Expected: every slot of the group has a case and is
  called; every command type has its case; no unlisted `cross` line.
- [ ] **Step 4: Goldens.** `fixed` to `dxmt/tests/wm_probe/<group>.fixed`; `wm_probe native
  $S/ref-native/winemetal.dylib fixed <group>` to `<group>.native`; `full` to `$S/ref-probe/<group>.full`.
- [ ] **Step 5: Bites.** `cover.sh bites $S/probe-<a|b|c>/wm_probe $S/ref-host <group>`. Expected: every slot of the
  group, stubbed alone, changes at least one line naming it; a slot that doesn't is ledgered with its reason, or gets
  the case that bites it (frees: `heap-growth`).
- [ ] **Step 6: Commit** (one per task): `dxmt tests: wm_probe cases for <groups>, recorded from DXMT's C`.

**The 3-5 merge (controller).** The controller merges the three branches into `dxmtrs` (disjoint files), then:
- builds the one probe the lanes use, `make wm-probe DXMT_SRC=$D/src WM_PROBE_OUT=$S/probe`, and snapshots
  `$S/goldens/` (the committed `*.fixed`, `*.native` and `$S/ref-probe`'s `*.full`, `*.mask`);
- the harness control: `WM_PROBE=$S/probe/wm_probe sh dxmt/wm-probe-check.sh --goldens $S/goldens --dir
  $S/kit-check`, `fixed` and `--full` for every group. Expected: 11 `ok` each: the kit's flags (`-DNDEBUG`, `-O3`,
  `-fblocks`, `-ObjC`) build a C that matches the goldens, so a later difference is the port's;
- `wm-probe-check.sh --native $S/ref-native/winemetal.dylib --goldens $S/goldens`: 11 `ok`.

### Task 6a (2.1a): the build plumbing and an empty crate (Opus; owner for its gate)

**Files (DXMTRS `step2-winemetal`):**
- Create: `mtld3d/unix/winemetal/{Cargo.toml,src/lib.rs}` with every module declared and each module file empty but
  for its doc block: `src/{ffi,handle,format,texture,init,float_cast,table}.rs`, `src/g1.rs` … `src/g10.rs`
  (`staticlib`, LGPL-2.1-or-later, feature `native`; lanes never touch the module list).
- Modify:
  - `mtld3d/unix/Cargo.toml` (member; `[profile.production-unwind]` inheriting `production` with
    `panic = "unwind"`), `Cargo.lock`;
  - `tools/cargo-staticlib.sh`;
  - `src/winemetal/unix/meson.build`, `src/nativemetal/meson.build`.

**Interfaces:**
- Produces:
  - `cargo-staticlib.sh <toolchain> <rust-target> <crate> <cargo-target-dir> <out-file> [<profile> [<features>]]`.
    The usage check accepts 5 to 7 arguments; the profile defaults to `production`, and the copied file is
    `<cargo-target-dir>/<rust-target>/<profile>/<lib>` (today's line hard-codes `production`). For `*-apple-darwin`
    targets it exports `MACOSX_DEPLOYMENT_TARGET=27.0` unless set, and it always passes the remaps of P9 through
    `--config "target.'cfg(all())'.rustflags=[…]"` (which cargo joins with the config's).
  - `Cargo.toml` lists every dependency the lanes need (D1's list). Features beyond the workspace's go on the crate's
    own lines (no download: the crates are in the registry cache).
  - Meson: in `src/winemetal/unix/meson.build`, a `custom_target('winemetal-unix-rust')` calling `cargo_staticlib`
    with `native_compiler_arch == 'arm64' ? 'aarch64-apple-darwin' : 'x86_64-apple-darwin'`, `winemetal` and
    `production-unwind`, linked into `winemetal.so` by `link_with` (if meson refuses a host target on a `native: true`
    library, the path in `link_args` and the target in `link_depends`). `src/nativemetal/meson.build` gets the same
    with features `native`, and `override_options : {'b_asneeded': false}`: meson's default is `-dead_strip_dylibs`
    on ld64, which already drops QuartzCore and Cocoa from the dylib, and objc2 looks classes up by name at run time
    (panicking on a missing one), so MetalFX would go too once G5 is Rust. Both link lines gain `-weak_framework
    AppKit` (today AppKit comes in implicitly through the C's static `NSScreen`/`NSView` references, which objc2
    doesn't make).

- [ ] **Step 1: Failing check.** `tools/cargo-staticlib.sh 1.99.0 aarch64-apple-darwin winemetal <dir> <out>
  production-unwind`. Expected: the usage error (exit 2).
- [ ] **Step 2: The script and the crate.** Write them. The same command builds
  `<dir>/aarch64-apple-darwin/production-unwind/libwinemetal.a`, with and without `native`; `strings -a` of it
  `| grep -c "$HOME"` = 0 (P9). Clippy, docs and audit clean.
- [ ] **Step 3: Meson.** The edits above.
- [ ] **Step 4: Integration (owner).**
  - `make wine-arm64`; the translator key; x64 check.sh 412/0.
  - `otool -L` of `winemetal.so`, sorted, equals `ref-mac`'s (the explicit AppKit may move in the load order:
    ledgered); `LC_RPATH` as `ref-mac`; minos 27.0.
  - `sh eac-vm/dxmt-native/build.sh`: `otool -L` of `winemetal.dylib`, sorted, equals `ref-native`'s plus QuartzCore
    and Cocoa (the explicit frameworks `b_asneeded` used to strip: ledgered), saved as
    `$S/ref-native/otool-L-6a.txt` for CP2 and Task 17; `nm -gU` equals `ref-native`'s.
- [ ] **Step 5: Commit:** `winemetal: an empty Rust crate in winemetal.so and the native dylib; cargo-staticlib takes
  a profile and features`.

### Task 6b (2.1b): the wire crate and its layout generator (Opus)

**Files (DXMTRS `step2-winemetal`):**
- Create: `mtld3d/unix/winemetal-wire/{Cargo.toml,src/lib.rs,src/layouts.rs,src/slot.rs}` (LGPL-2.1-or-later);
  `tools/winemetal-layouts.sh`.
- Modify: `mtld3d/unix/Cargo.toml` (member), `Cargo.lock`, `mtld3d/unix/winemetal/Cargo.toml` (the path dependency),
  `mtld3d/scripts/derive_inventory.txt`.

**Interfaces:**
- Produces:
  - `winemetal_wire`:
    - `#[repr(C)]` mirrors of every struct in `winemetal_thunks.h` and `airconv_thunks.h` (with `*_params32`, and
      `unixcall_wmtmemory`, which exists only under `DXMT_GUEST_VM`), and of the `winemetal.h` structs they reach
      (info structs, `wmtcmd_*` nodes), in Rust casing;
    - `MemPtr { ptr: u64 }` on 64-bit, `{ ptr: u32, high: u32 }` on 32-bit;
    - every bitfield struct (`WMTTextureInfo`'s four 8-bit fields; `WMTIndirectCommandBufferInfo`'s 11 one-bit flags
      and 21 padding bits) as its storage integer plus generated `<FIELD>_SHIFT`/`<FIELD>_WIDTH` constants and a
      getter, asserted per target from the facts' `byte:bit-bit` lines;
    - enum values and the swizzle macro encodings (`winemetal.h:463-480`) as constants;
    - `SM50_BINDING_INDEX_*`;
    - `slot::<NAME>: u32` for every numbered slot.
  - `tools/winemetal-layouts.sh <llvm-mingw-bin>` (the folder `sh dxmt/toolchain.sh` prints) writes `layouts.rs` from
    clang's `-fdump-record-layouts` (and `-E -dM` for macro values), always with `-DDXMT_GUEST_VM` (it only adds
    declarations): Apple clang for `aarch64-apple-darwin`; llvm-mingw's clang with `-ffreestanding` for
    `x86_64-`, `i686-`, `aarch64-` and `arm64ec-w64-mingw32`. It holds `#[cfg]`-gated `const _: () = assert!(…)`
    lines for each target the crates build for, `aarch64-pc-windows-msvc` included: each struct's size, each field's
    offset, each bitfield's shift and width, each macro constant. It maps C names to Rust casing (`originX` →
    `origin_x`, `WMTPixelFormat…` → `WMT_PIXEL_FORMAT_…`), so no lint allow is needed. It fails when a struct named
    in `winemetal_thunks.h` or `airconv_thunks.h` is missing from the dump, or on any bitfield it doesn't emit.

- [ ] **Step 1: The mirrors and the generator.** Write them; run `tools/winemetal-layouts.sh "$(sh
  dxmt/toolchain.sh)" > layouts.rs`.
- [ ] **Step 2: Builds.** `-p winemetal-wire` for `aarch64-apple-darwin`, `aarch64-`, `arm64ec-`, `x86_64-` and
  `i686-pc-windows-msvc` (the msvc ones as the Global Constraints' rustc check). Expected: builds.
  Mutations, each restored: change one offset in `layouts.rs` (that target's build fails); move one ICB flag's shift
  by one (the build fails); delete `unixcall_wmtmemory`'s mirror (the generator fails).
- [ ] **Step 3: Lint, docs, audit.** Expected: clean; the derive inventory lists the mirrors' `Clone, Copy`.
- [ ] **Step 4: Commit:** `winemetal-wire: winemetal's wire structures in Rust, asserted per target`.

### Task 6c (2.1c): the shared helpers, the constructors and the audit exceptions (Opus; owner for its gate)

**Files (DXMTRS `step2-winemetal`):**
- Modify:
  - `mtld3d/unix/winemetal/src/{ffi,handle,format,texture,init,float_cast}.rs`, create `src/format/tests.rs`,
    `src/init/tests.rs`, `src/ffi/tests.rs`, `src/handle/tests.rs`;
  - `U` (the helpers below);
  - `mtld3d/docs/CONVENTIONS.md` and `mtld3d/scripts/audit.sh` (ALLOW_SITES): every exception of the Rules check,
    so no lane edits either.

**Interfaces:**
- Produces:
  - `winemetal::ffi`:
    - `pub type Handler = unsafe extern "C-unwind" fn(*mut c_void) -> i32`;
    - `pub struct AbortOnPanic`: every handler and every C-named export takes one first; its `Drop` calls
      `std::process::abort()` when `std::thread::panicking()`. A foreign Objective-C unwind doesn't set
      `panicking()`, so D4's propagation is unchanged, while a Rust panic never unwinds into the VM host's
      `catch (...)` blocks around native DXMT calls (`eac-vm/d3d/host/native.cpp`);
    - `pub unsafe fn params<'a, T>(args: *mut c_void) -> &'a mut T`;
    - `pub unsafe fn mem_ref<'a, T>(p: MemPtr) -> Option<&'a T>` and `mem_mut`;
    - `pub unsafe fn mem_slice<'a, T>(p: MemPtr, count: u64) -> Option<&'a [T]>`: empty for count 0, `None` for a
      null pointer with count > 0 (the caller does what the C does with it);
    - `pub unsafe fn mem_cstr<'a>(p: MemPtr) -> Option<&'a CStr>`;
    - `STATUS_SUCCESS`, `STATUS_UNSUCCESSFUL`, `STATUS_NOT_IMPLEMENTED`.
  - `winemetal::handle` (R1's three modes; map critic 11; the Nil and raises rule):
    - `pub unsafe fn obj<'a, T: Message>(h: u64) -> Option<&'a T>`;
    - `pub fn ret_owned<T: Message>(o: Option<Retained<T>>) -> u64` (+1, `Retained::into_raw`);
    - `pub fn ret_autoreleased<T: Message>(o: Option<Retained<T>>) -> u64` (+0, `Retained::autorelease_ptr`);
    - `pub fn ret_borrowed<T: Message>(o: Option<&T>) -> u64` (+0, owned elsewhere);
    - `pub fn lookup_class(name: &CStr) -> Option<&'static AnyClass>` (`AnyClass::get`: nil, never a panic, for a
      class the C looks up by name).
  - `winemetal::format`:
    - `pub fn pixel_format(f: u32) -> u64` (custom-swizzle bits stripped);
    - `pub fn swizzle(f: u32) -> [u8; 4]`;
    - exported as `extern "C-unwind"` `to_metal_pixel_format` until Task 17.
  - `winemetal::texture::fill_texture_descriptor(desc: &MTLTextureDescriptor, info: &TextureInfo)`, exported under
    the C name with the C signature as `extern "C-unwind"` until Task 17 (C handlers call it until their groups
    move). `setCompressionMode:` comes through `extern_methods!` on a local re-declaration of
    `MTLTextureDescriptor`, checked with `respondsToSelector:` (as the C does).
  - `winemetal::init`:
    - `FORCED_MAX_ANISOTROPY: AtomicU32` (C name `forced_max_anisotropy` until Task 17);
    - `pub fn parse_max_anisotropy(v: Option<&CStr>) -> u32`, which keeps `atoi`'s reading and 1..=16;
    - two constructors in `__DATA,__mod_init_func`: the anisotropy read, and `load_presenter` (with
      `MACNEUTRON_PRESENT=1`, `dlopen("@loader_path/libmacneutron-present.dylib", RTLD_NOW | RTLD_LOCAL)`, and the C's
      stderr line on failure).
  - `float_cast`: every float↔integer and narrowing conversion `U` does implicitly. The implementer greps `U` for
    them before writing; lanes ask the controller for more.
  - CONVENTIONS and ALLOW_SITES: D4 (`production-unwind`, `AbortOnPanic`), the `float_cast` allow site, private
    selectors through `extern_methods!` on local re-declarations anywhere in `unix/winemetal/src/`, and D14's
    `AnyThread` re-declarations of the `NSScreen`/`NSView`/`NSWindow` getters (stating today's C behaviour).

- [ ] **Step 1: Failing tests.**
  - `format::tests::pixel_format_strips_swizzle`: for three formats and three custom swizzles, `pixel_format` and
    `swizzle` equal `ORIGINAL_FORMAT`/`DECODE_FORMAT_COMPONENT_SWIZZLE`. The vectors come from a throwaway C program
    over `winemetal.h`, its output pasted into the test.
  - `init::tests::anisotropy_reads_like_atoi`: `"8"`→8, `"8x"`→8, `" 4"`→4, `"0"`→0, `"17"`→0, `"-3"`→0,
    `"abc"`→0, unset→0.
  - `ffi::tests::panic_under_guard_aborts`: the test re-runs its own binary as a child with an environment switch,
    where a function holding `AbortOnPanic` panics; the child ends by `SIGABRT`, not an unwind. And
    `mem_slice` answers `Some(&[])` for (null, 0), `None` for (null, 3).
  - `handle::tests::missing_class_is_none`: `lookup_class(c"WMProbeNoSuchClass")` is `None`.

  Run them in the Global Constraints' test form. Expected: FAIL.
- [ ] **Step 2: Helpers in Rust; the C uses them.** Delete from `U` `to_metal_pixel_format`, `fill_texture_descriptor`,
  `forced_max_anisotropy`, `read_max_anisotropy`, `load_presenter`. Add their declarations
  (`MTLPixelFormat to_metal_pixel_format(enum WMTPixelFormat);`, `void
  fill_texture_descriptor(MTLTextureDescriptor *, struct WMTTextureInfo *);`, `extern uint32_t
  forced_max_anisotropy;`). Expected: Step 1's tests pass.
- [ ] **Step 3: Exceptions.** CONVENTIONS and ALLOW_SITES as above. Expected: the audit is clean on a scratch file
  holding one example of each (deleted after).
- [ ] **Step 4: Host gate** (after the 3-5 merge).
  - `$S/lane-so.sh $D $S/host-6` then `WM_PROBE=$S/probe/wm_probe sh dxmt/wm-probe-check.sh --goldens $S/goldens
    --dir $S/host-6`, `fixed` and `--full` for every group. Expected: 11 `ok` each.
  - g0's `aniso-env` and `presenter-env` lines are the C's: the constructors run from Rust.
  - P9 on the linked images (`winemetal.so` in the build folder before install's strip, and `winemetal.dylib`): `LC_ALL=C /usr/bin/grep -ac "$HOME" <image>` = 0 and `nm -ap <image> | /usr/bin/grep -c ' OSO '` = 0 (`strings -a` misses `__LINKEDIT`; the C images give 0 and 0).
- [ ] **Step 5: Lint, docs, audit** for both crates and both feature sets. Expected: clean.
- [ ] **Step 6: Integration (owner).**
  - `make wine-arm64`; the translator key; x64 check.sh 412/0.
  - `sh dxmt/mtld3d-facade-test.sh`; `presenter/check.sh`.
  - `otool -l` of `winemetal.so`: minos 27.0, `LC_RPATH` as `ref-mac`.
  - `sh eac-vm/dxmt-native/build.sh`, then `sh eac-vm/dxmt-native/gate.sh` and `tests/{build,hook,win32}.sh`
    (`MN_REF_FROZEN` exported); `wm-probe-check.sh --native build/dxmt-native/install/lib/winemetal.dylib
    --goldens $S/goldens`.

  Expected: as Task 1's baselines.
- [ ] **Step 7: Commit:** `winemetal: pixel formats, texture descriptors and the constructors in Rust; the audit
  exceptions the port needs`.

**Probe cases (g0)**, written in Task 3:
- `slot83-null`: the table entry is null; never called;
- `guest-only`: 153, 154, 155 return `c0000002`;
- `aniso-env` (`WM_ENV_ANISO`, values 8, 0, 17 and 8x): slot 19 through a recording device. The sampler descriptor's
  `maxAnisotropy` is floored only for trilinear, non-comparison samplers whose own value is below the floor: one
  sampler meeting all five conditions, one per condition flipped (min, mag, mip filter, compare function, own value
  ≥ floor), and own values equal to and above the forced one (the strict `<`);
- `presenter-env` (`WM_ENV_PRESENT`): without the dylib beside the `.so`, the C's stderr line; with
  `build/presenter/libmacneutron-present.dylib` copied beside it, the image is loaded (`_dyld_get_image_name`).

### Task 7 (G1): objects, strings, pools, dispatch data, device queries, OS (Sonnet)

**Files:** `mtld3d/unix/winemetal/src/g1.rs`, `src/g1/tests.rs` (if pure helpers appear); `U` (the C rule).

**Slots and exports:**
0 `_NSObject_retain`, 1 `_NSObject_release`, 2 `_NSArray_object`, 3 `_NSArray_count`, 4 `_MTLCopyAllDevices`,
5 `_MTLDevice_recommendedMaxWorkingSetSize`, 6 `_MTLDevice_currentAllocatedSize`, 7 `_MTLDevice_name`,
8 `_NSString_getCString`, 10 `_NSAutoreleasePool_alloc_init`, 24 `_MTLDevice_minimumLinearTextureAlignmentForPixelFormat`,
27 `_NSString_lengthOfBytesUsingEncoding`, 28 `_NSObject_description`, 49 `_MTLDevice_supportsFamily`,
50 `_MTLDevice_supportsBCTextureCompression`, 51 `_MTLDevice_supportsTextureSampleCount`, 52 `_MTLDevice_hasUnifiedMemory`,
60 `_NSString_string`, 61 `_NSString_alloc_init`, 86 `_MTLCommandEncoder_setLabel`,
87 `_MTLDevice_setShouldMaximizeConcurrentCompilation`, 111 `_WMTGetOSVersion`, 114 `_DispatchData_alloc_init`,
125 `_MTLDevice_registryID`, 148 `_MTLDevice_sampleTimestamps`, 149 `_DispatchData_copyBytes` (26).

**Hazards:**
- Ownership: +1 from 4, 10, 61, 114; +0 from 2 (`ret_borrowed`), 7, 28, 60 (`ret_autoreleased`), each +0 helper
  confirmed by the C's `pool=` line (a mismatch goes to the controller, not the golden).
- Slot 10 returns an `NSAutoreleasePool` object, never `objc_autoreleasePoolPush`'s token: slot 1 drains it with
  `release`, on the thread that made it, spanning calls. There is no per-call pool.
- 8 writes through a bare `u64` buffer pointer: `getCString:maxLength:encoding:` writes, Rust computes nothing.
- 86 sends `setLabel:` to any handle.
- 114's handle is a byte pointer and the data is copied.
- 149 copies at most `capacity` and returns the full size.
- Exceptions: none beyond D4.
- Threads: any; 0/1 must not assume the pool's thread is the caller's.

**Probe cases (g1):**
- `retain-release` (retain count +1, -1: `in1=+1`, `in1=-1`);
- `pool-drain` (an object autoreleased in a slot-10 pool is gone after slot 1: weak reference nil);
- `devices` (4 `own=+1`; 3 `count>=1`; 2 `own=+0` with `pool=`);
- `x-array-past-end` (2 with the index equal to the count: `raised NSRangeException`, never 0);
- `device-sizes` (5 `=recommendedMaxWorkingSetSize`; 6 between two direct reads of `currentAllocatedSize`);
- `name` (7 `own=+0` with `pool=`; read back with 27 and 8);
- `getcstring` (8 exact fit and short buffer: return value and untouched tail);
- `description` (28, `own=` and `pool=`);
- `strings` (60 `own=+0`, 61 `own=+1`, each with `pool=`; strings of 16 or more characters, ASCII and with non-ASCII
  UTF-8, so neither is a tagged pointer; read back);
- `min-align` (24 for three formats, with and without swizzle bits);
- `supports` (49 for every `WMTGPUFamily` value, 50, 51 for 1, 2, 3, 4, 8, 52);
- `label` (86 on a buffer and a queue: `label` reads back);
- `max-concurrent` (87, both values: `shouldMaximizeConcurrentCompilation` reads back each);
- `os-version` (111: major ≥ 27; `full` exact);
- `dispatch-data` (114 copies: the source changed after; 149 with capacity < size returns size, tail untouched);
- `registry` (125 `=registryID`);
- `timestamps` (148 nonzero, second ≥ first);
- `nil` (every getter of the group on handle 0: 0 written, every other field the C writes written);
- `nil-args` (86 with each object argument 0, where the C sends it on as nil).

**Integration (CP1):** the facade test (most of G1's slots are facade slots); `make -C eac-vm/metal/wm host-test`
diffed with `$S/ref-metal/metal-wm-host.txt`.

- [ ] Steps: the lane procedure. **Commit:** `winemetal: objects, strings, pools and device queries in Rust (G1)`.

### Task 8 (G2): resources, heaps, residency, indirect command buffers (Sonnet)

**Files:** `src/g2.rs`, `src/g2/tests.rs`; `U`.

**Slots and exports:** 18 `_MTLDevice_newBuffer`, 19 `_MTLDevice_newSamplerState`, 20 `_MTLDevice_newDepthStencilState`,
21 `_MTLDevice_newTexture`, 22 `_MTLBuffer_newTexture`, 23 `_MTLTexture_newTextureView`, 39 `_MTLTexture_pixelFormat`,
40 `_MTLTexture_width`, 41 `_MTLTexture_height`, 42 `_MTLTexture_depth`, 43 `_MTLTexture_arrayLength`,
44 `_MTLTexture_mipmapLevelCount`, 45 `_MTLTexture_replaceRegion`, 46 `_MTLBuffer_didModifyRange`,
107 `_MTLBuffer_updateContents`, 132 `_MTLDevice_newResidencySet`, 133 `_MTLResidencySet_addAllocations`,
134 `_MTLResidencySet_removeAllocations`, 135 `_MTLResidencySet_removeAllAllocations`, 136 `_MTLResidencySet_commit`,
137 `_MTLCommandQueue_addResidencySet`, 138 `_MTLDevice_newHeap`, 139 `_MTLDevice_heapBufferSizeAndAlign`,
140 `_MTLDevice_heapTextureSizeAndAlign`, 141 `_MTLHeap_newBuffer`, 142 `_MTLHeap_newTexture`,
143 `_MTLDevice_newIndirectCommandBuffer`, 145 `_MTLTexture_getBytes` (28).
Also deleted: `to_metal_swizzle`, `hooked_buffer`, and under `native` `WMTSetMemoryHooks`, `WMTMemoryHooked` with the
hook statics (`U:156-210`). The last two are exported from Rust by those names with the C signatures.

**Hazards:**
- Ownership: +1 from 18-23, 132, 138, 141-143, and 132's retained `ret_error`.
- 18:
  - NoCopy over `info->memory.ptr` (the VM host's blob memory; no deallocator), else `newBufferWithLength:options:`;
  - writes `memory.ptr = contents` (null for Private) and `gpu_address`;
  - natively, with hooks set and a non-Private, non-Memoryless buffer: 16 KiB pages from `memory_alloc` and a
    deallocator calling `memory_free`;
  - the host calls it with a page-rounded length and a stack `WMTBufferInfo` (D19), so no pointer is kept after
    return but the blob.
- 19 reads `FORCED_MAX_ANISOTROPY` and writes `gpu_resource_id` only with argument buffers.
- 21, 22, 142 use `fill_texture_descriptor` and write `gpu_resource_id` and `mach_port = 0`.
- 23 decodes the swizzle with `format::swizzle`.
- 46 reads its length from `ret` (the PE thunk passes `length` there, `winemetal_thunks.c`'s `params.ret = length`):
  `ret` is an input and stays unchanged; the range is `(arg, ret)`.
- 133/134 pass the handle array as `id<MTLAllocation> const *` with its count; count 0 with a null pointer is legal.
- 141/142: an offset of `!0` means none.
- 143 reads exactly the four flags the C reads (`inheritBuffers`, `inheritPipelineState`,
  `supportDynamicAttributeStride`, `supportRayTracing`) through `winemetal-wire`'s bitfield getters; the `#if 0`
  macOS 26 flags stay unread.
- Exceptions: a texture getter on a non-texture raises (Review Focus 1).
- Threads: any; the native hooks are set before any device exists.

**Probe cases (g2):**
- `buffer-shared`, `buffer-private`, `buffer-nocopy` (18: `memory.ptr=contents` (null for Private),
  `gpu_address=gpuAddress` of the returned buffer, `own=+1`);
- `sampler` (19: descriptor dump through a recording device; `gpu_resource_id=gpuResourceID`, written only with
  argument buffers);
- `depth-stencil` (20: dump; stencils only when enabled);
- `texture-2d`, `texture-3d`, `texture-array`, `texture-msaa`, `texture-lossless` (21: dump; 39-44 and
  `compressionType` read back; `gpu_resource_id=gpuResourceID`; written and untouched fields);
- `buffer-texture` (22, `gpu_resource_id=gpuResourceID`);
- `view-swizzle` (23: `pixelFormat`, `swizzle` and `gpu_resource_id=gpuResourceID` for non-identity requested
  swizzles crossed with the custom-swizzle formats, chosen so the sums `(base+off)%6` wrap);
- `getters-nil` (39-44 on 0);
- `replace-getbytes` (45/145 round trip; bytes outside the region untouched);
- `did-modify-range` (46 on a recording buffer: `didModifyRange:` with `(arg, ret)`; `ret` unchanged);
- `update-contents` (107: the buffer's bytes after, by `wm_readback`);
- `residency` (132-137: `allocationCount` after add 2, remove 1, remove all, commit; 137 on a queue);
- `residency-empty` (133/134 with count 0 and a null array);
- `heap` (138 dump; 139/140 values; 141/142 with `!0` and with an offset: `heapOffset`);
- `icb` (143: `size`, `gpu_resource_id=gpuResourceID`);
- `icb-flags` (143 through a recording device, once per flag set alone: the four flags the C reads, and one `#if 0`
  flag that must stay unread; the descriptor dump);
- `nil-args` (each object argument of the group's sends as 0 where the C sends it on as nil);
- `native-hooks` (`WM_NATIVE`: 18 with memory hooks set; the buffer's memory is the hook's, its free runs on release);
- `x-pixelformat-on-buffer` (39 on a buffer handle: `raised`).

**Integration (CP2):**
- `make -C eac-vm/metal/wm memory-test` (the real `.so`; buffers, regions, emulated heaps, a wrong-class heap);
- check.sh's lanes D, compress, heaps and icb rows (part of the x64 run);
- native build plus `eac-vm/dxmt-native/tests/hook.sh` (memory hooks), `wm-probe-check.sh --native`, and `otool -L`
  of `winemetal.dylib`, sorted, equal to `$S/ref-native/otool-L-6a.txt` (MetalFX still linked: Task 6a).

- [ ] Steps: the lane procedure, clippy also `--features native`. **Commit:** `winemetal: resources, heaps, residency
  and ICBs in Rust (G2)`.

### Task 9 (G3): queues, command buffers, encoders, counters (Opus)

**Files:** `src/g3.rs`, `src/g3/{blit,compute,render,tests}.rs`; `U`.

**Slots and exports:** 9 `_MTLDevice_newCommandQueue`, 11 `_MTLCommandQueue_commandBuffer`, 12 `_MTLCommandBuffer_commit`,
13 `_MTLCommandBuffer_waitUntilCompleted`, 14 `_MTLCommandBuffer_status`, 17 `_MTLCommandBuffer_encodeSignalEvent`,
30 `_MTLCommandBuffer_blitCommandEncoder`, 31 `_MTLCommandBuffer_computeCommandEncoder`,
32 `_MTLCommandBuffer_renderCommandEncoder`, 33 `_MTLCommandEncoder_endEncoding`,
36 `_MTLBlitCommandEncoder_encodeCommands`, 37 `_MTLComputeCommandEncoder_encodeCommands`,
38 `_MTLRenderCommandEncoder_encodeCommands`, 47 `_MTLCommandBuffer_presentDrawable`,
48 `_MTLCommandBuffer_presentDrawableAfterMinimumDuration`, 89 `_MTLCommandBuffer_error`, 90 `_MTLCommandBuffer_logs`,
91 `_MTLLogContainer_enumerate`, 102 `_MTLCommandBuffer_encodeWaitForEvent`,
127 `_MTLCounterSampleBuffer_newTimestampBuffer`, 128 `_MTLCounterSampleBuffer_resolveCounterRange`,
129 `_MTLCommandBuffer_blitCommandEncoderWithSampleBuffers`, 130 `_MTLCommandBuffer_property`,
147 `_MTLCommandBuffer_computeCommandEncoderWithSampleBuffers` (24). Under `native`: `WMTSetPresentHook` and its static.

**Hazards:**
- Ownership: +1 from 9 and 127; `ret_autoreleased` for 11, 30-32, 89, 90, 129, 147; `ret_borrowed` for 91's items;
  each confirmed by the C's `pool=` line.
- 9's `arg` is the maximum command-buffer count (`newCommandQueueWithMaxCommandBufferCount:`); 31's `arg` picks the
  dispatch type (0 serial, else concurrent).
- Hot path: 36-38 walk the PE's `wmtcmd_*` list in place, following `next`, with 13, 13 and 47 cases.
  - Type 0 and unknown types are skipped and the walk goes on (no assert, no panic).
  - Compute's threadgroup size comes from the last SetPSO of the same call and starts at zero on every call.
  - Single viewports are bit-cast `WMTViewport` → `MTLViewport`.
  - The 8 geometry and tessellation draws bind `SM50_BINDING_INDEX_*` from `winemetal-wire`.
- 32's `arg` is a bare `u64` `WMTRenderPassInfo*` (8 colours, depth, stencil, 4 sample buffers, visibility, tile
  size).
- 11, 47, 48 send exactly `commandBuffer`, `presentDrawable:` and `presentDrawable:afterMinimumDuration:` (the
  presenter swizzles them, D20).
- Natively, the present hook runs before the present.
- 91: start and limit within `buffer_size`; items +0.
- 128 keeps its own pool.
- 130 turns seconds into nanoseconds with `float_cast`.
- Threads: 13 blocks.
- Exceptions: a command list on the wrong encoder kind raises.

**Probe cases (g3):**
- `queue` (9 on a recording device: the selector and its count; `own=+1`);
- `cmdbuf-lifecycle` (11, 12, 13, 14: status 4);
- `signal-event` (17, then the event's `signaledValue` read directly);
- `encoders` (30-33: `own=`/`pool=` of each; 31 with `arg` 0 and 1, `dispatchType` read back; 33 on a recording
  encoder: `endEncoding`);
- `blit-<type>` for each of the 13 blit types, and `compute-<type>` for each of the 13 compute types, on a recording
  encoder: every message and argument (pointer arguments by their bytes);
- `compute-threadgroup` ([SetPSO A, Dispatch, SetPSO B, DispatchThreads], then a second call with a Dispatch only:
  its threadgroup size is zero again);
- `render-<type>` for each of the 47 handled render types, including the 8 geometry/tessellation draws, mesh and tile;
- `blit-unknown-type`, `compute-unknown-type`, `render-unknown-type` (type 999 between two commands: both run);
- `blit-gpu`, `compute-gpu`, `render-gpu` (copy and fill bytes, a doubled word array, a triangle's centre pixel);
- `renderpass` (32 through a recording command buffer: the descriptor dump);
- `present-selectors` (47/48 on a recording command buffer);
- `error-logs` (89, 90 on a completed buffer: 0; and on a recording command buffer whose `error` and `logs` answer a
  fresh `NSError` and array by `wm_rec_ret`: `own=` and `pool=`);
- `enumerate` (91 on `wm_nsarray(5)`, start 1, size 2: `read=2`, the two items in order, the tail untouched, `own=`
  and `pool=` of the items);
- `enumerate-past-end` (91 with start past the count: 0 read);
- `wait-event` (102 on a recording command buffer: `encodeWaitForEvent:value:` and its arguments);
- `present-hook` (`WM_NATIVE`: with a present hook set, 47 on a recording command buffer: the hook's line comes before
  `presentDrawable:`);
- `nil-args` (each object argument of the group's sends as 0 where the C sends it on as nil: fences, sample
  buffers, optional textures);
- `timestamps` (127, 128 resolve monotonic, 129/147 dumps, 130's four properties as relations);
- `x-blit-on-compute-encoder` (`raised`).

**Integration (CP3):**
- check.sh's hazards, overlap, timestamp and indirect rows (the x64 run);
- `presenter/check.sh`;
- the owner's `wm_probe <built host-dir> rate` once against `$S/ref-probe/rate.txt`. Expected: each slot within 5% of
  the C or faster; a larger loss stops the lane and goes to the maintainer (P8);
- `wm-probe-check.sh --native` on the native build (the present hook: `hook.sh` sets only memory hooks).

- [ ] Steps: the lane procedure (a recording encoder's transcript first, then GPU cases). **Commit:**
  `winemetal: queues, command buffers and the three command-list interpreters in Rust (G3)`.

### Task 10 (G4): libraries, pipelines, archives (Sonnet)

**Files:** `src/g4.rs`; `U`.

**Slots and exports:** 25 `_MTLDevice_newLibrary`, 26 `_MTLLibrary_newFunction`, 29 `_MTLDevice_newComputePipelineState`,
34 `_MTLDevice_newRenderPipelineState`, 35 `_MTLDevice_newMeshRenderPipelineState`,
98 `_MTLLibrary_newFunctionWithConstants`, 112 `_MTLDevice_newBinaryArchive`, 113 `_MTLBinaryArchive_serialize`,
131 `_MTLDevice_newTileRenderPipelineState`, 144 `_MTLDevice_newLibraryWithSource`,
156 `_MTLDevice_newRenderPipelineStateWithVertexDescriptor`, 157 `_MTLDevice_newLibraryWithSourceOptions` (12).
Also deleted: `render_pipeline_descriptor`, `render_pipeline_state` and the logic-op categories (`U:587-601`).

**Hazards:**
- Ownership: +1 from every new object and every `ret_error` (P7: the C retains even where calls.c says `AR`).
- `setLogicOperationEnabled:`/`setLogicOperation:` come through `extern_methods!` on local re-declarations.
- Archives: lookup through `binaryArchives` and FailOnBinaryArchiveMiss; after success, `add*FunctionsWithDescriptor:
  error:` with its error dropped.
- 35/131's macOS 15 gates are gone (D6).
- 26/98 names are bare `char*`; 144 is ASCII and MSL 3.1.
- 156: 31 attributes and layouts, skipping format 0 and stride 0.
- Threads: any. Exceptions: none beyond D4.

**Probe cases (g4):**
- `library-data` (25 on a `dispatch_data_create` the probe made, not slot 114; junk data: `ret_error` `own=+1` and its
  text);
- `function` (26; a missing name gives 0);
- `compute-pso` (29: dump with and without archives);
- `render-pso` (34: dump with the logic op and attachments 0-7);
- `mesh-pso` (35);
- `tile-pso` (131);
- `vertex-pso` (156: dump; the C's failure without a vertex function input);
- `constants` (98 under the `setConstantValue:type:atIndex:` swizzle: each constant's bytes by type, type and index;
  the values object is built inside the handler and has no getters);
- `archive` (112 empty and with a URL under `wm_tmpdir()`, printed by `absoluteString`; 29 adds to it; 113
  serialises; a bad URL gives an error `own=+1`);
- `source` (144: ok, and an error text; the source string as the recorder prints it);
- `source-options` (157: the options dump on a recording device);
- `nil-args` (each object argument of the group's sends as 0 where the C sends it on as nil);
- `x-library-on-device` (26 with a device handle as the library: `raised`).

**Integration (CP1):** check.sh lane C rows (x64 run); the facade test (156/157); `MTLD3D_SO=<loopback mtld3d.so>
sh dxmt/d3d9-check.sh` (`dxmt/mtld3d-build.sh --loopback` first).

- [ ] Steps: the lane procedure. **Commit:** `winemetal: libraries, pipelines and binary archives in Rust (G4)`.

### Task 11 (G5): MetalFX (Sonnet)

**Files:** `src/g5.rs`; `U`.

**Slots and exports:** 56 `_MTLDevice_newTemporalScaler`, 57 `_MTLDevice_newSpatialScaler`,
58 `_MTLCommandBuffer_encodeTemporalScale`, 59 `_MTLCommandBuffer_encodeSpatialScale`,
68 `_MTLDevice_supportsFXSpatialScaler`, 69 `_MTLDevice_supportsFXTemporalScaler`,
150 `_MTLDevice_temporalScalerScaleRange`, 151 `_MTLDevice_newFrameInterpolator`,
152 `_MTLCommandBuffer_encodeFrameInterpolate` (9). Also deleted: `temp_handler`, `SIGNALS` and the signal swap
(D6).

**Hazards:**
- Ownership: +1 from 56, 57, 151.
- 151/152's macOS 26 gates are gone (D6); 151 answers 0 without `supportsDevice:`.
- 151 forces `uiTextureFormat` Invalid; the scaler is optional.
- 56's reactive-mask field is conditional.
- 58 sets 26 scaler properties and 152 sets 18, exactly.
- Threads, exceptions: none beyond D4.

**Probe cases (g5):**
- `supports` (68, 69);
- `scale-range` (150);
- `temporal` (56 under the descriptor swizzle: all 17 descriptor fields the handler sets, `autoExposure`, the
  jittered and output-resolution motion vectors, `inputContentPropertiesEnabled`, `reactiveMaskTextureEnabled` and
  `requiresSynchronousInitialization` included, which the scaler can't answer; then the scaler's readable
  properties);
- `spatial` (57, the same way);
- `temporal-encode` (58 on a recording scaler that forwards to a real one: 26 setters and `encodeToCommandBuffer:`);
- `spatial-encode` (59);
- `interpolator` (151 under the descriptor swizzle, with and without a scaler);
- `interpolate-encode` (152: 18 setters);
- `nil-args` (each optional texture argument as 0 where the C sends it on as nil);
- `x-scaler-on-device` (58 with a device handle as the scaler: `raised`).

**Integration (CP2):** check.sh's upscale, XeSS, DLSS and frame generation rows (x64 run, `MACNEUTRON_FRAMEGEN` modes
as today).

- [ ] Steps: the lane procedure. **Commit:** `winemetal: MetalFX in Rust (G5)`.

### Task 12 (G6): layer, view, HUD, display, HDR (Opus)

**Files:** `src/g6.rs`, `src/g6/{appkit,display,tests}.rs`; `U`. (D14's CONVENTIONS exception and its ALLOW_SITES
are Task 6c's.)

**Slots and exports:** 62 `_DeveloperHUDProperties_instance`, 63 `_DeveloperHUDProperties_addLabel`,
64 `_DeveloperHUDProperties_updateLabel`, 65 `_DeveloperHUDProperties_remove`, 66 `_MetalDrawable_texture`,
67 `_MetalLayer_nextDrawable`, 70 `_MetalLayer_setProps`, 71 `_MetalLayer_getProps`, 72 `_CreateMetalViewFromHWND`,
73 `_ReleaseMetalView`, 92 `_CGColorSpace_checkColorSpaceSupported`, 93 `_MetalLayer_setColorSpace`,
94 `_WMTGetPrimaryDisplayId`, 95 `_WMTGetSecondaryDisplayId`, 96 `_WMTGetDisplayDescription`,
97 `_MetalLayer_getEDRValue`, 99 `_WMTQueryDisplaySetting`, 100 `_WMTUpdateDisplaySetting`,
101 `_WMTQueryDisplaySettingForLayer` (19).
Also deleted: `execute_on_main`, `developer_hud_uses_metric_service` (D6: `MTLHUDService` only),
`GetColorSpaceName`, `GetChromaticity_xy`, `GetDisplayColorGamut`, `GetNSScreenForDisplayID`, `g_display_settings`,
the macdrv declarations, and under `native` `WMTSetWsiHooks`/`WMTGetWsiHooks` with their static (exported by those
names).

**Hazards:**
- Ownership: 62 `n/a`; `ret_autoreleased` for 67; `ret_borrowed` for 66 and 72's `ret_layer`; 72's `ret_view` +1
  (macdrv's, released by 73); each +0 helper confirmed by the C's `pool=` line.
- 70/93 run their sets through `dispatch_sync` to the main queue, directly when already on it.
- 96/97/101 read AppKit on the calling thread through D14's `AnyThread` re-declarations: no `MainThreadMarker`, no
  hop. 101 runs on every present.
- 67 may block and runs Wine's overridden `nextDrawable`: send exactly that selector.
- 72/73: `dlsym(RTLD_DEFAULT, "macdrv_functions")`, entries 1, 2, 6, 7 and 8 only, `client_cocoa_view` at offset 24,
  and the loose-symbol fallback.
- HUD: the 8-argument insert selector through `extern_methods!`; 62's class through `handle::lookup_class` (the C's
  `objc_lookUpClass` gives nil for a missing class; objc2's cached lookup would panic). This Mac has `MTLHUDService`,
  so no probe case reaches the nil path: Task 6c's `missing_class_is_none` and the lane grep cover it.
- Display settings: index 1 for the main display, else 0; version 0 means unset; null metadata clears.
- ColorSync through raw FFI; 96 keeps its own pool; 95 lists displays into a `Vec` sized by CoreGraphics' count.
- Threads: a deadlock if the main thread waits on a caller of 70/93 (as today).
- Exceptions: a raise inside 70/93's main-queue block ends the process, as in the C (libdispatch doesn't carry it
  back); elsewhere D4.

**Probe cases (g6):**
- `hud` (62 non-nil; 63-65 on a recording instance: selectors and all 8 arguments);
- `layer-props` (70 from the worker thread on a recording layer that forwards to a real `CAMetalLayer`: the sets are
  `msg@main`; 71 into a 0xA5 struct);
- `drawable` (67 `own=+0`, 66 `own=+0`, each with `pool=`);
- `view` (`WM_WINE_ONLY`: 72/73 through the winemac stand-in, with `wm_window()`);
- `colorspace` (92 for every `WMTColorSpace` value; 93's colorspace name and `wantsExtendedDynamicRangeContent`);
- `displays` (94 equals `CGMainDisplayID`; 95 is 0 or online);
- `display-description` (96: relations in `fixed`, values in `full`);
- `edr` (97 on a layer without a view; then on the layer of `wm_window()`'s view, with
  `wantsExtendedDynamicRangeContent` YES and NO: the values equal the screen's, read on main);
- `display-settings` (100 then 99 for the main and another id; null clears; 101 for a windowless layer; then 101 on
  the layer of `wm_window()`'s view after 100 set the main display's setting, with `wantsExtendedDynamicRangeContent`
  YES and NO: `version=1`, `metadata=same`, `colorspace=set`, the EDR values equal the screen's);
- `main-blocked` (`WM_ISOLATE`, `wm_main_blocked`: 96, 97 and 101 on the view's layer return; D14's no-hop rule);
- `main-blocked-70` (`WM_ISOLATE`: 70 with main blocked: `hang` on C, recorded; the hop to main stays);
- `wsi-hooks` (`WM_NATIVE`: WSI hooks set, then read back);
- `nil-args` (each object argument of the group's sends as 0 where the C sends it on as nil).

**Integration (CP3):**
- `sh wine-arm64/check.sh dxmt dxmt-present` (presented pixels);
- `memory-test`'s `present`, `hud` and `props` cases;
- the x64 run's present rows and `d3d11_nvapi` rows (99/100);
- `wm-probe-check.sh --native` on the native build (the WSI hooks: neither `hook.sh` nor `win32.sh` reaches them).

- [ ] Steps: the lane procedure, clippy also `--features native`. **Commit:** `winemetal: layers, views, the HUD and
  displays in Rust (G6)`.

### Task 13 (G7): events, the listener, Mach ports (Opus)

**Files:** `src/g7.rs`, `src/g7/{listener,tests}.rs`; `U`; create `src/nativemetal/win32_event.c` (`int
wmt_native_set_event(void *)`, `void wmt_native_ref(void *)`, `int wmt_native_unref(void *)` around
`src/util/win32_native_event.h`'s inline functions) and add it to `src/nativemetal/meson.build`.

**Slots and exports:** 15 `_MTLDevice_newSharedEvent`, 16 `_MTLSharedEvent_signaledValue`,
103 `_MTLSharedEvent_signalValue`, 104 `_MTLSharedEvent_setWin32EventAtValue`, 105 `_MTLDevice_newFence`,
106 `_MTLDevice_newEvent`, 108 `_SharedEventListener_create`, 109 `_SharedEventListener_start`,
110 `_SharedEventListener_destroy`, 120 `_MTLDevice_newSharedTexture`, 121 `_WMTBootstrapRegister`,
122 `_WMTBootstrapLookUp`, 123 `_MTLSharedEvent_createMachPort`, 124 `_MTLDevice_newSharedEventWithMachPort`,
126 `_MTLSharedEvent_waitUntilSignaledValue`, 146 `_MTLSharedEvent_setWin32EventAtValues` (16).
Also deleted: `extract_texture_descriptor`, the listener struct, the `NtSetEvent`/`EVENT_HOLD`/`EVENT_DROP` block
(`U:2739-2750`), and the SPI categories for `MTLSharedTextureHandle`, `MTLDeviceSPI` and `MTLSharedEventHandle`.

**Interfaces:**
- Produces: `#[repr(C)] struct Listener`, the handle `Box::into_raw`:
  - first field `AtomicPtr<CFRunLoop>` (8 bytes; `events.m` polls it for non-null before it calls 110): only null or
    a run loop the listener has `CFRetain`ed, published by the start that wins; the last owner `CFRelease`s it;
  - second, the state (unstarted, running, destroyed) behind the listener's lock, with the owner count and the
    `MTLSharedEventListener`.
- Consumes: `NtSetEvent(*mut c_void, *mut c_void) -> i32`, undefined, from `@rpath/ntdll.so` (Wine build), or the
  three `wmt_native_*` functions (`native`).

**Hazards:**
- Statuses: 121/122 return 0xC0000001 when the bootstrap fails; 122 still writes `mach_port`. Nothing else is
  non-zero (D7, D19).
- Ownership: +1 from 15, 105, 106, 120, 124; 108 is not an object.
- 109 parks the caller in `CFRunLoopRun` with a source.
- D19: Metal's notification thread never sets the event. It `CFRunLoopPerformBlock`s onto the listener's loop and
  wakes it; `NtSetEvent` runs on the 109 thread (the VM stand-in drops a set from any other thread, `events.m`'s
  thread-local queue, and Wine doesn't know Metal's thread). Natively the event is held from registration to the set.
- D5 (the C spins on the first word while it is null, so with the listener kept alive a late notification would spin
  forever on Metal's queue; the state, not the first word, decides):
  - a notification, under the lock: unstarted, it unlocks, yields and looks again (the C's wait); running, it queues
    its block (`NtSetEvent`, then the hold dropped and its owner reference given back) and wakes the loop; destroyed,
    it gives back its owner reference and natively `wmt_native_unref`s its hold, never calling `NtSetEvent`;
  - destroy, under the lock: destroyed; when running, it queues its stop as a block on the loop (blocks run in
    order, so every notification queued before it runs, and a stop that comes before run isn't lost); it gives back
    the handle's reference;
  - a count of owners frees the listener after the last: the handle, the one start each listener gets (counted from
    create: DXMT's two callers and `events.m` always start it), and each pending notification;
  - start: the one that wins publishes the retained loop, sets running, runs, and gives back the start reference when
    the loop returns; one that finds the listener destroyed gives it back and returns at once; any other start
    returns at once without touching the count (the C returns at the failed CAS).

  Success paths are unchanged. A listener nobody starts stays allocated, as in the C.
- Exceptions: none beyond D4.
- 146 fires after `needed` of `count` (needed > count or count 0: the hold stays, D7).
- 126 stores Metal's `BOOL` in `ret_timeout`.
- 120 has two modes by `mach_port`, and `extract_texture_descriptor` writes the whole info.
- 123 uses `eventPort` and `mach_port_mod_refs`.
- The allocation pairs (108/110, 146's countdown) move in one commit.

**Probe cases (g7):**
- `shared-event` (15 `own=+1`, 103 then 16);
- `fence-event` (105, 106);
- `win32-event` (`WM_WINE_ONLY`: 108, 109 on a probe thread, 104, 103: the stand-in's hook gets the handle on the 109
  thread, `same-thread`; 110; the thread returns);
- `first-word` (108, 109 on a probe thread T; after it publishes and before 110: `first-word=runloop(T)`);
- `win32-events` (`WM_WINE_ONLY`: 146, count 3, needed 2: the hook on the 109 thread, `same-thread`);
- `listener-notify-before-start` (`WM_ISOLATE`, `WM_WINE_ONLY`: 104, signal, then 109: the hook gets the handle on the 109 thread; the
  C's success path, recorded from the C into `g7.fixed`);
- `listener-destroy-first` (`WM_ISOLATE`: create, destroy, then start: `hang` on C, recorded `full`-only in Task 5);
- `listener-stop-race` (`WM_ISOLATE`, `WM_NOT_ON_C`: start and destroy racing 100 times; no `hang`; nondeterministic
  on C);
- `listener-late-notify` (`WM_ISOLATE`, `WM_NOT_ON_C`, `WM_WINE_ONLY`: 104, start, 110, then signal: returns, the hook is never
  called, the child's CPU time under 1 s; on C a spin on freed memory);
- `listener-destroy-first-notify` (`WM_ISOLATE`, `WM_NOT_ON_C`, `WM_WINE_ONLY`: create, 104, destroy, start, then signal: the same);
- `wait` (126: reached gives 1, 10 ms timeout gives 0);
- `bootstrap` (121 then 122 for a unique name; an unknown name gives `c0000001` and `mach_port` written);
- `mach-event` (123's port into 124: a signal on one is seen on the other; `send-refs=` after 123 and 124);
- `shared-texture` (120 export: port nonzero, `send-refs=`, and info written; import: same size and format);
- `win32-event-native` (`WM_NATIVE`: 104 with an `mn_win32` event; its reference count is back where it was after
  the set);
- `nil-args` (each object argument of the group's sends as 0 where the C sends it on as nil);
- `x-signal-on-fence` (103 with a fence handle: `raised`).

Every g7 `WM_ISOLATE` case runs under `libgmalloc` (Task 2's `WM_GMALLOC`), so a use after free crashes.

The lane's GREEN expects exactly one line to differ from the snapshot: `listener-destroy-first`'s `hang` becomes
`returned` (D5). The lane says so in its hand-off; the controller lands it at CP3 in one ledgered edit: `returned` into
`g7.fixed`, `$S/ref-probe/g7.full` and `$S/goldens`, and the `WM_NOT_ON_C` cases' Rust lines into `g7.fixed`.

**Integration (CP3):** the x64 run's fence rows (hazards `fence-cpu-late`, `d3d12_api` multifence and sharedfence);
`host-test` against `$S/ref-metal`; `make -C eac-vm/metal/wm memory-test` (the Mac's one run of `events.m`'s listener
path); native build, the native gate, `tests/win32.sh` (the `win32_native_event.h` functions `win32_event.c` wraps)
and `wm-probe-check.sh --native` (104 with a native event).

- [ ] Steps: the lane procedure, clippy also `--features native`. **Commit:** `winemetal: shared events, the listener
  and Mach ports in Rust (G7)`.

### Task 14 (G8): the caches (Sonnet)

**Files:** `src/g8.rs`, `src/g8/tests.rs`; `$D/src/winemetal/unix/cache.c` (the five functions and both classes
deleted; its includes stay for Task 17); `U`'s five declarations (`U:3550-3554`) stay as they are.

**Slots and exports:** 115 `_CacheReader_alloc_init`, 116 `_CacheReader_get`, 117 `_CacheWriter_alloc_init`,
118 `_CacheWriter_set`, 119 `_WMTSetMetalShaderCachePath` (5).

**Hazards:**
- Ownership: +1 from 115 and 117 (objects of classes made with `define_class!`, released by slot 1); 116's data +1,
  or 0.
- D18:
  - table `cache_<version>` (`key BLOB PRIMARY KEY, value BLOB NOT NULL`);
  - the other `cache_*` tables dropped, then `VACUUM` if any was;
  - `flock(LOCK_EX)` on `<db>-lock` while opening and migrating;
  - WAL and `synchronous=NORMAL`;
  - the reader `READONLY|NOMUTEX`; `INSERT OR REPLACE`;
  - a relative path under `confstr(_CS_DARWIN_USER_CACHE_DIR)`, its parent created.
- One prepared statement per object on a NOMUTEX connection: the object is not `Sync`.
- Class names: `CacheReader`/`CacheWriter`, or `DXMTNativeCacheReader`/`DXMTNativeCacheWriter` with `native`
  (`define_class!` panics on a duplicate, D20).
- A failed init answers nil with the C's `NSLog` line. The half-built object no longer leaks (no output changes).
- 119 calls the private `MTLSetShaderCachePath`/`MTLGetShaderCachePath` and answers their equality.
- Exceptions: none beyond D4.

**Probe cases (g8):**
Every database, lock and folder is under `wm_tmpdir()` (or, for `relative-path`, a `mkdtemp` name under the user
cache folder), so parallel probes never collide.
- `writer-reader` (117, 118 ×3, `[writer release]` directly, not slot 1; 115, 116 hit with bytes and miss with 0;
  `own=` and `pool=` of each);
- `version-migrate` (writers at versions 1 then 2: `sqlite_master` lists `cache_2` only);
- `db-bytes` (`full`: sha256 of the closed database after a fixed write sequence, D18);
- `cross-read` (the reader reads the C's `cache-c.db` fixture: same values);
- `relative-path` (a probe-unique folder under the user cache folder, removed after);
- `lock-file` (117 on a fresh path: `<db>-lock` exists after; a second process holding `flock(LOCK_EX)` on it makes
  the open wait until it lets go);
- `bad-path` (nil);
- `metal-cache-path` (119 to a temp folder gives 1);
- `class-names` (`WM_NATIVE`: the native image's writer is a `DXMTNativeCacheWriter`, the reader a
  `DXMTNativeCacheReader`);
- `nil` (116 on handle 0: the zeros the C's nil send writes; 118 on handle 0, `stub-same`: it writes nothing).

**Integration (CP1):**
- the x64 run's lane A cache rows and lane E's cache table;
- native build, then the native replay rows of `eac-vm/dxmt-native/build-tests.sh`;
- `wm_probe <built host-dir> pair build/dxmt-native/install/lib/winemetal.dylib` (both images make a writer:
  Review Focus 4);
- `eac-vm/d3d/loopback-gate.sh` (`MN_REF_FROZEN=$S/ref-vm` exported: it runs the native gate, which would otherwise
  record its reference from the Rust `.so`).

- [ ] Steps: the lane procedure, clippy also `--features native`. **Commit:** `winemetal: the shader caches in Rust
  (G8)`.

### Task 15 (G9): capture (Haiku)

**Files:** `src/g9.rs`; `U`.

**Slots and exports:** 53 `_MTLCaptureManager_sharedCaptureManager`, 54 `_MTLCaptureManager_startCapture`,
55 `_MTLCaptureManager_stopCapture` (3).

**Hazards:**
- 53 is `n/a` (shared).
- 54 passes `error:nil` and never writes `ret` (kept).
- 54 builds the descriptor and an `NSURL` from `output_url`.
- Threads, exceptions: none beyond D4.

**Probe cases (g9):**
- `manager` (53 non-nil, the same twice);
- `start-unsupported` (`stub-same`: 54 without `MTL_CAPTURE_ENABLED`: `ret` untouched);
- `start-recorded` (54 on a recording manager, `output_url` under `wm_tmpdir()`: the descriptor dump, the URL by
  `absoluteString`, so `fileURLWithPath:` and `URLWithString:` differ);
- `stop` (55 on a recording manager);
- `nil` (`stub-same`: 54 and 55 on handle 0: `ret` untouched, as the C's nil send leaves it).

**Integration (CP1):** the x64 run (no row reaches these slots).

- [ ] Steps: the lane procedure. **Commit:** `winemetal: capture in Rust (G9)`.

### Task 16 (G10): the translator thunks (Opus)

**Files:** `src/g10.rs`; `U`; create `$D/src/winemetal/unix/sm50_wow64.c` (P4: `U:2061-2388` moved verbatim but
for `static` dropped from the nine `thunk32_*`, its includes added; `U` keeps a declaration for each where it stood,
for its WoW64 table) and add it to `src/winemetal/unix/meson.build`'s sources.

**Slots and exports:** 74 `thunk_SM50Initialize`, 75 `thunk_SM50Destroy`, 76 `thunk_SM50Compile`,
77 `thunk_SM50GetCompiledBitcode`, 78 `thunk_SM50DestroyBitcode`, 79 `thunk_SM50GetErrorMessage`, 80 `thunk_SM50FreeError`,
81 `thunk_SM50CompileGeometryPipelineVertex`, 82 `thunk_SM50CompileGeometryPipelineGeometry`,
84 `thunk_SM50CompileTessellationPipelineHull`, 85 `thunk_SM50CompileTessellationPipelineDomain`,
88 `thunk_SM50GetArgumentsInfo` (12; `export_name` without the leading underscore, as the C names them).

**Hazards:**
- The params carry raw PE pointers at natural width; the thunks forward field for field into `extern "C"` `SM50*`
  from the static libairconv (`sysv_abi` is a no-op on arm64).
- airconv is `-fno-exceptions`: no unwind path. Threads: any, as airconv allows today.
- `airconv_thunks.[ch]` untouched (D15).
- Shader, bitcode and error handles are raw pointers.

**Probe cases (g10):**
- `initialize` (74 on `dxmt/tests/shaders/triangle.vs.dxil`: shader non-null, reflection fields);
- `compile` (76 with a COMMON and a PSO_PIXEL_SHADER argument: the bitcode's sha256 in `fixed`);
- `bitcode` (77 size > 0);
- `destroy` (75, 78: each freed, by `heap-growth` over 100 make/free cycles);
- `error` (74 on junk: 79's text; 80 freed, by `heap-growth` over 100 cycles);
- `geometry` (81/82 with `triangle2.vs/gs.dxil`);
- `tessellation` (84/85 with `shaders/d3d11/patch.{vs,hs,ds}.dxbc`);
- `arguments` (88: both arrays).

**Integration (CP2):** the x64 run's lanes C-E pipeline rows, the `d3d11_vsia` and canary rows; the translator key.

- [ ] Steps: the lane procedure. **Commit:** `winemetal: the translator thunks in Rust; the WoW64 converters in their
  own C file (G10)`.

### Task 17 (2.11): the tables in Rust (owner, Opus)

**Files:**
- Modify: `src/table.rs` (and the 2.1 exports below).
- Delete: `U`, `$D/src/winemetal/unix/cache.c`.
- Modify: `src/winemetal/unix/meson.build` (sources: `sm50_wow64.c` only; `link_args` gain
  `-Wl,-u,___wine_unix_call_funcs` and `-Wl,-u,___wine_unix_call_wow64_funcs`), `src/nativemetal/meson.build`
  (sources: `../winemetal/winemetal_thunks.c`, `win32_event.c`).

**Interfaces:**
- Produces:
  - `#[unsafe(export_name = "__wine_unix_call_funcs")] pub static UNIX_CALL_FUNCS: [Option<ffi::Handler>; 158]`:
    slot order as `slots.txt`, `None` at 83, `_MacNeutron_guestOnly` (a Rust export answering
    `STATUS_NOT_IMPLEMENTED`) at 153-155.
  - `#[cfg(not(feature = "native"))] #[unsafe(export_name = "__wine_unix_call_wow64_funcs")] pub static
    UNIX_CALL_WOW64_FUNCS: [Option<ffi::Handler>; 153]`: entries 0-152 of the 64-bit table, with `sm50_wow64.c`'s
    `thunk32_SM50*` at 74, 76, 77, 79, 81, 82, 84, 85, 88 (D3).
- The C names `to_metal_pixel_format`, `fill_texture_descriptor` and `forced_max_anisotropy` stop being exported.

- [ ] **Step 1: Failing check.** Delete `U`. `$S/lane-so.sh $D $S/host-17`. Expected: no `__wine_unix_call_funcs`;
  the link or `cover.sh table` fails.
- [ ] **Step 2: Tables.** Write them. Expected:
  - `cover.sh slots` and `cover.sh table $S/host-17 $S/host-17/winemetal.so` pass: 158 entries, and the `wow64`
    lines show 153 entries equal to `ref-mac`'s by symbol;
  - `WM_PROBE=$S/probe/wm_probe sh dxmt/wm-probe-check.sh --goldens $S/goldens --dir $S/host-17`, `fixed` and
    `--full` (masked) for every group: 11 `ok` each. g0's `aniso-env` and `presenter-env` rows are the check that the
    constructors survived: with the C gone only `-u` pulls archive members, and a `#[used]` constructor in a member
    nothing pulls would be dropped silently.
- [ ] **Step 3: Lint, docs, audit**, both feature sets. Expected: clean.
- [ ] **Step 4: The Mac image (owner).** `make wine-arm64`, then on the built `winemetal.so`:
  - `otool -L`, sorted, equals `ref-mac`'s (install name, `@rpath/winemac.so`, `@rpath/ntdll.so`, the weak
    frameworks, AppKit, strong MetalFX, libsqlite3);
  - `LC_RPATH` as `ref-mac`'s; minos 27.0;
  - `nm -m … | grep -c 'from ntdll'` = 1 (`NtSetEvent`);
  - bundle.sh's checks (x18 scan, signing) passed in the build;
  - the translator key unchanged.
- [ ] **Step 5: Native image.** `sh eac-vm/dxmt-native/build.sh`. Expected:
  - `nm -gU` of `winemetal.dylib` contains every `ref-native` name of the 142 thunks, the 5 hooks, the 12 `SM50*`
    and `___wine_unix_call_funcs`;
  - `nm -u` of the native `d3d11`/`d3d12` dylibs resolves against it;
  - `cover.sh table` passes on it, copied as `winemetal.so` into a stand-in folder (no `wow64` lines);
  - `otool -L`, sorted, equals `$S/ref-native/otool-L-6a.txt` (MetalFX and AppKit still linked);
  - `wm-probe-check.sh --native build/dxmt-native/install/lib/winemetal.dylib --goldens $S/goldens`: 11 `ok`.
- [ ] **Step 6: Unix-side full gates (owner)**, `MN_REF_FROZEN=$S/ref-vm` exported.
  - both check.sh lanes (412/0 each);
  - `sh wine-arm64/check.sh dxmt dxmt-present`;
  - `presenter/check.sh`;
  - `dxmt/mtld3d-facade-test.sh`;
  - `sh eac-vm/host/build.sh` then `sh eac-vm/metal/gate.sh` (and its `metal-wm-host.txt` diffed with
    `$S/ref-metal`);
  - `sh eac-vm/dxmt-native/gate.sh` and `tests/{build,hook,win32}.sh`;
  - `eac-vm/d3d/loopback-gate.sh`;
  - the VM, with Task 1's guest image (unchanged: the PE side is still C) and the host's Rust `.so` (`host/build.sh`
    restages it from `wine.app`): `sh eac-vm/dxmt/gate.sh` (all 44; the only gate running the 12 D3D11/D3D10
    programs through the host's `winemetal.so`) and `sh eac-vm/dxmt/d3d9-gate.sh`, so a VM failure in Task 18 is the
    PE side's;
  - the loopback D3D9 runs (`MTLD3D_SO=…` with `dxmt/d3d9-check.sh` and `dxmt/mtld3d-conformance.sh`);
  - `sh dxmt/mtld3d-layout32-check.sh`.

  Expected: as Task 1's baselines.
- [ ] **Step 7: Commit:** `winemetal: both unix-call tables in Rust; winemetal_unix.c and cache.c go`.

### Task 18 (2.12): the PE thunks in Rust (lane, then owner; Opus)

**Files (DXMTRS):**
- Create:
  - `mtld3d/windows/winemetal-pe/{Cargo.toml,src/lib.rs,src/transport.rs}` and one file per `winemetal.h` group:
    `src/{objects,device,resources,pipelines,commands,access,events,display,metalfx,capture,cache,guest}.rs`
    (`staticlib`, LGPL-2.1-or-later), and `src/protos.rs` (generated);
  - `src/winemetal/winemetal.def` (154 names), `src/winemetal/winemetal-guest.def` (157).
- Modify:
  - `mtld3d/windows/Cargo.toml` (member; `winemetal-wire = { path = "../unix/winemetal-wire" }`), `Cargo.lock`;
  - `tools/winemetal-layouts.sh` (the `--protos` mode below);
  - `src/winemetal/meson.build`: sources `airconv_thunks.c` only; a `custom_target('winemetal-pe-' + t)` for each
    of `rust_pe_targets`; `link_with` them; `rust_runtime_dep`; `vs_module_defs` by `get_option('guest_vm')`; the
    builtin postprocess kept.
- Delete: `src/winemetal/main.c`. `winemetal_thunks.c` stays (native, D12).
- Create (uncommitted): `$S/thunk-diff/`.

**Interfaces:**
- Produces:
  - every `WINEMETAL_API` function of `winemetal.h` outside its `DXMT_NATIVE` blocks (142 and the 3 guest ones),
    `#[unsafe(no_mangle)] pub unsafe extern "C"` with the C signature: by-value `WMTOrigin`, `WMTSize`,
    `WMTTextureSwizzleChannels`, `WMTMemoryPointer`, and a returned `WMTSizeAndAlign`;
  - `transport::unix_call<T>(slot: u32, params: &mut T) -> i32`, split as the shim does
    (`mtld3d/windows/shim/src/lib.rs`): `unsafe extern "C" { static __wine_unixlib_handle: u64; static
    __wine_unix_call_dispatcher: unsafe extern "system" fn(u64, u32, *mut c_void) -> i32; }` and `unsafe extern
    "system" { fn __wine_init_unix_call() -> i32; fn DisableThreadLibraryCalls(module: *mut c_void) -> i32; }` (Wine
    declares `__wine_init_unix_call` `WINAPI`: stdcall on i686, `___wine_init_unix_call@0`; from `libwinecrt0.a`,
    which meson already links);
  - `DllMain` (`extern "system"`): `DisableThreadLibraryCalls`, then `__wine_init_unix_call() == 0`;
  - `src/protos.rs`, written by `tools/winemetal-layouts.sh --protos <llvm-mingw-bin>`: clang's `-ast-dump=json` of
    every `WINEMETAL_API` prototype per target (`-DDXMT_GUEST_VM`, enums resolved to their underlying types:
    `enum WMTColorSpace` and `enum WMTCommandBufferStatus` are `u64`), as `#[cfg(windows)]` per-target lines such as
    `const _: unsafe extern "C" fn(u64, u32, u32, u16, u16, u16, u16, WmtTextureSwizzleChannels, *mut u64) -> u64 =
    crate::MTLTexture_newTextureView;`. A width the Rust gets wrong (`uint16_t`, `uint8_t sample_count`, the `bool`
    arguments, `enum WMTColorSpace` mid-list in `WMTUpdateDisplaySetting`, `MTLCommandBuffer_status`'s 64-bit return
    in `edx:eax` on i686) fails that target's build; the aarch64-apple-darwin harness can't see them (its caller
    widens small arguments).
- Behaviour:
  - params zero-initialised (P5);
  - the status is ignored except by `WMTBootstrapRegister`/`LookUp` (true when 0) and `WMTForward` (only 155,
    else 0xC000000D);
  - names copied with `strncpy`'s 127-byte truncation;
  - `WMT_MEMPTR_SET`'s zero high half on i686.

- [ ] **Step 1: The transcript harness** (`$S/thunk-diff/`; lane, host). A C driver calls each of the 145 functions
  with fixed arguments. The recording dispatcher records `(slot, params bytes)`, then fills every params byte with a
  slot-keyed pattern (never 0 or 0xA5) and returns 0; a second pass returns 0xC0000001 for 121, 122 and 155 (the
  status paths: `WMTBootstrapRegister`/`LookUp`'s `!ret`, `WMTForward`'s pass-through). The driver passes every
  out-pointer a buffer twice the C type's size, filled 0xA5, and prints it whole after the call, with the return
  value; each out-pointer the C guards (`err_out`, `WMTGetOSVersion`'s three) is also called once with NULL (the
  unguarded ones, `*colorspace`, `*version`, `*edr_value`, `*layer`, `*mach_port` and the rest, never get NULL). It
  is built twice:
  - with `$S/ref-mac/winemetal_thunks.c` (`WINE_UNIX_CALL` through the recording dispatcher, `-DDXMT_GUEST_VM`);
  - with `winemetal-pe` built for `aarch64-apple-darwin`, the driver providing `__wine_unixlib_handle` and the
    recording `__wine_unix_call_dispatcher`.

  The C run goes twice, its stack poisoned 0x11 then 0x22 before each call: the params bytes the C writes are those
  equal in both runs. Expected: on the C build alone, 145 functions with slots as `winemetal_thunks.c`'s, the 26
  copy-backs showing the pattern in their out buffers, and both passes' returns.
- [ ] **Step 2: RED.** The crate with every function a stub that makes no call. Expected: the diff names every
  function.
- [ ] **Step 3: GREEN.** Expected, for every function: the same slot; the same params bytes where the C writes, zero
  elsewhere; the out buffers equal byte for byte (the copied field's bytes, and the bytes past each type's size
  still 0xA5: an 8-byte `enum WMTColorSpace *` written with 4 bytes fails); NULL guarded exactly where the C guards;
  return values equal in both passes.
- [ ] **Step 4: Builds and lint, per target.** `src/protos.rs` regenerated and unchanged. The Global Constraints'
  forms: clippy on `aarch64-apple-darwin`, and the rustc `-Dwarnings` build for `aarch64-`, `arm64ec-`, `x86_64-`
  and `i686-pc-windows-msvc` (clippy never sees `cfg(windows)` code: ledgered). Mutation: change one `u16` of
  `MTLTexture_newTextureView` to `u32`; the builds fail. Restore. `.def` names: `diff` with
  `$S/ref-mac/exports.txt`'s names (and the guest's for `winemetal-guest.def`). Audit. Expected: clean, no diff.
- [ ] **Step 5: i686 ABI (Review Focus 5).** `llvm-objdump -d` of the i686 Rust thunks for the five by-value
  functions beside the C's (compile `$S/ref-mac/winemetal_thunks.c` for `i686-w64-mingw32`). Expected: each argument
  read at the C's stack offset; `WMTSizeAndAlign` returned in `edx:eax` as the C does. `llvm-nm` of the i686 `.lib`:
  `U ___wine_init_unix_call@0`, `U _DisableThreadLibraryCalls@4`, and `T _DllMain@12`, the forms the shim's i686
  `mtld3d.lib` shows today (a cdecl call to a stdcall callee that pops 4 bytes would unbalance the i386 guest's stack
  in `DllMain`).
- [ ] **Step 6: Mac integration (owner, after Task 17).** `make wine-arm64`, then:
  - `llvm-readobj --coff-exports winemetal.dll` names equal `ref-mac` (154, and the ARM64EC `#Name` forms);
  - `--coff-imports` equals `ref-mac`'s plus at most `ntdll.dll`, `api-ms-win-core-synch-l1-2-0.dll` and
    `api-ms-win-crt-private-l1-1-0.dll` (spec §4, E1);
  - the importers' (`d3d11`, `d3d12`, `dxgi`, `nvapi64`) import tables equal `ref-mac/imports.txt`, `__imp_aux_`
    forms included;
  - bytes 64-79 read `Wine builtin DLL`;
  - both check.sh lanes 412/0.
- [ ] **Step 7: Guest integration (owner).** `sh eac-vm/dxmt/build.sh`, then:
  - `sh eac-vm/dxmt/check-build.sh`;
  - exports and the importers' imports equal `ref-guest` for x86_64 and i386 (157 names);
  - `sh eac-vm/metal/layout32-check.sh`;
  - `sh eac-vm/d3d/build-guest.sh` (mnd3d links `winemetal.dll.a`);
  - `sh eac-vm/image/guest-stages.sh dxmt`, then the image build (`builder.sh sync eac-vm/image mn-image`,
    `builder.sh ssh 'sh ~/mn-image/build.sh wine'`);
  - `MN_REF_FROZEN=$S/ref-vm sh eac-vm/dxmt/gate.sh` (all 44 programs; flaky rows one re-run; Task 17 Step 6 ran it
    with the C PE side, so a new failure is this task's);
  - `sh eac-vm/tests/d3d-smoke.sh` (the `32:` lines in both WoW64 modes: the first i686 Rust that runs);
  - `sh eac-vm/dxmt/d3d9-gate.sh`;
  - `MN_REF_FROZEN=$S/ref-present sh eac-vm/dxmt/present-gate.sh`.

  Expected: every gate as Task 1's baseline.
- [ ] **Step 8: Commit:** `winemetal.dll: the thunks and DllMain in Rust (Wine builds); the native dylib keeps the C`.

### Task 19: notices, the spec, performance, push and pin (controller)

**Files (MacNeutron `dxmtrs`):**
- `wine-arm64/licenses/README` (DXMT's entry: `winemetal.so` and `winemetal.dll` are Rust and link Rust's standard
  library and objc2), `wine-arm64/tests/licences_test.sh`;
- `dxmt/mtld3d-notices.py` (`LEGS`: `winemetal` for `aarch64-apple-darwin`; `winemetal-pe` for the msvc targets);
- `dxmt/pins`;
- `docs/superpowers/specs/2026-10-29-dxmtrs-design.md`.

- [ ] **Step 1: Notices, test first.**
  - A `licences_test.sh` row requires the entry to name `winemetal.so` and `winemetal.dll` as Rust. Expected: FAIL;
    write the entry; PASS.
  - `python3 dxmt/mtld3d-notices.py`'s output lists objc2 and its framework crates, block2 and dispatch2 with their
    texts.
  - `cargo metadata` shows no Apache-2.0-only licence in either crate's tree.
- [ ] **Step 2: Performance (D11).** Task 18 Step 6's x64 run's `timing` and `compress clear` lines, `present_loop`'s frame
  time, the DLSS cost row and `wm_probe … rate`, against `$S/ref-perf` and `$S/ref-probe/rate.txt`, each once.
  Expected: none worse than 5%; otherwise STOP and report to the maintainer before Step 4.
- [ ] **Step 3: Spec.**
  - §5 step 2 marked done, with what it did.
  - The corrections: D1 (objc2, not `mtl`), D2 (the transport's meaning), D3, D4 (`production-unwind`), D6, D11 (the
    parked bench), D12 (native C thunks until step 6), D14, D15 (the key moved at step 1's pin), D16, and P1, P4, P5,
    P11-P14.
  - Privacy scan of the spec, the plan and the ledger: no home path, account name, SteamID or e-mail beyond commit
    trailers.
  - Commit: `Spec: DXMTRS step 2, winemetal in Rust`.
- [ ] **Step 4: DXMTRS main (standing authorization).**
  - Privacy-scan `git -C $D log -p main..step2-winemetal`.
  - `git -C $D switch main && git -C $D merge --ff-only step2-winemetal`.
  - `git -C $D push --dry-run origin main`. Expected: a fast-forward of `main` only.
  - Push.
  - Remove the DXMTRS worktrees (`$S/wt-<lane>`) with `git -C $D worktree remove`, and the step and lane branches.
    Expected: `git -C $D worktree list` one line, `git -C $D branch` `main` only, `git -C $D stash list` empty.
  - From MacNeutron's root, `git worktree remove $S/wt-mn-a` (and `-b`, `-c`) and `git branch -d step2-probe-a`
    (and `-b`, `-c`): they are MacNeutron's, which `git -C $D` can't remove.
- [ ] **Step 5: Pin and pinned build.**
  - `DXMT_COMMIT=<main's sha>` in `dxmt/pins`; commit `DXMT: winemetal in Rust (DXMTRS <sha>)`.
  - `git -C $D checkout -q --detach $(cat build/wine-arm64-src/dxmt.applied)`, so `build_mode` answers `reapply`.
  - `make wine-arm64` re-fetches the pin from GitHub (the log says so).
  - Both check.sh lanes 412/0, `wm-probe-check.sh`, then the VM gates of Task 18 Step 7 and the native gate on the
    pinned build (`MN_REF_FROZEN` exported).
  - P9 on what ships: `LC_ALL=C /usr/bin/grep -ac "$HOME"` = 0 on the pinned build's `winemetal.so` (before and after
    install's strip), the VM's `winemetal.dylib` (Helpers/vm/dxmt, unstripped) and each `winemetal.dll` (aarch64-windows,
    and the guest's x86_64 and i386); `nm -ap | /usr/bin/grep -c ' OSO '` = 0 on the `.so` and the `.dylib`.
  - Re-set the push URL: `git -C $D remote set-url --push origin git@github.com:Mac-Neutron/DXMTRS.git`.
- [ ] **Step 6: MacNeutron main (standing authorization).** `git switch main && git merge --ff-only dxmtrs`,
  `git push --dry-run origin main` (a fast-forward), push. If the fast-forward fails (MacNeutron `main` moved), STOP
  and report to the maintainer; never rebase `main` or `dxmtrs` onto it unasked. The same holds for Step 4's DXMTRS
  fast-forward. Ledger and program ledger
  (`.superpowers/sdd/dxmtrs-program.md`): step 2 done, with the pinned sha.

## Review record

Plan review of 2026-10-30, each finding checked against the source first; Minors not applied. One line a finding:

1. Critical, Task 18 copy-backs unchecked: applied (pattern-filled params, 2x 0xA5 out buffers, NULL calls); with 27.
2. Critical, no per-target PE prototype check: applied (`--protos`, `src/protos.rs`, the `u16`→`u32` mutation).
3. Critical, late notification spins after destroy: applied (P12; state field, start rule, three listener cases).
4. Critical, ICB 1-bit flags unmirrored: applied (6b storage integer, shifts, widths; G2 hazard and `icb-flags`).
5. Important, layouts miss `DXMT_GUEST_VM`, aarch64-msvc: applied (6b: every target with the define; missing fails).
6. Important, objc2 forms turning raises into nil: applied (Nil and raises rule, grep, x- cases); `hud-missing-class` adapted to a unit test (this Mac has `MTLHUDService`).
7. Important, panics unwind into native.cpp: applied (P11 `AbortOnPanic`, its abort test, `C-unwind` C helpers).
8. Important, `own=` blind to autorelease: applied (`pool=`; +0 helpers named per field in G1, G3, G6).
9. Important, no nil receiver/argument rule: applied (Global Constraints; `nil`/`nil-args` per group, none in G10).
10. Important, `cargo test` link and stub leaks: applied (`dynamic_lookup` test form; `cover.sh ported` `U` check).
11. Important, `__wine_init_unix_call` cdecl: applied (shim's split; Step 5 `llvm-nm` forms verified); with 37.
12. Important, RED and mutations too coarse: applied (`WM_STUB`, `cover.sh bites`, call counts, enum coverage).
13. Important, cross-group cases: applied (Metal setup, `cross`, per-group processes, `bad-handle`); with 34.
14. Important, flaky `full` comparisons: applied (`<group>.mask`, masked `--full` mode); with 38.
15. Important, weak relations where oracles exist: applied (G1 5, 6, 125; G2 `gpu_address`, `gpu_resource_id`).
16. Important, ownership gaps: applied (raw `own=`, `in<k>=`, slot 10 out, 16+ char strings, `send-refs=`).
17. Important, threads unrecorded: applied (`msg@<thread>`, `main-blocked`, `main-blocked-70`, 146 `same-thread`).
18. Important, 97/101 fallback only: applied (`wm_window()`; `edr` and `display-settings` on its view).
19. Important, 89/90/91 measured on nil: applied (`wm_rec_ret`, `enumerate` on `wm_nsarray(5)`).
20. Important, recorder prints pointers, skips nesting: applied (Task 2 recorder and `wm_dump`).
21. Important, MetalFX/constant descriptors unreadable: applied (Task 2 swizzles; G4 `constants`, G5 cases).
22. Important, cases without inputs: applied (matrices and observables; 46 hazard corrected, `ret` is an input).
23. Important, G7 goldens/race/first word/UAF: applied (CP3 goldens edit, `WM_NOT_ON_C`, `first-word`, gmalloc, CP3 `memory-test`).
24. Important, no slot-level native gate: applied (`wm_probe native`, `<group>.native`, `WM_WINE_ONLY`; labels fixed).
25. Important, gates re-record `ref.sh` from Rust: applied (`MN_REF_FROZEN` exported from Task 1 Step 5); with 32.
26. Important, VM DXMT gate first runs with the PE port: applied (Task 17 Step 6 runs it and `d3d9-gate.sh`).
27. Important, Task 18 harness writes nothing back: applied with 1 (0xA5 kept, the probe's fill, over 0x5A).
28. Important, kit flags never checked alone: applied (the 3-5 merge's harness control on `$S/kit-check`).
29. Critical, PATH forms for clippy and cargo-staticlib fail: applied (three exact forms, each run here).
30. Critical, no msvc clippy: applied as option (a), P13 (rustc `-Dwarnings`); (b) waits for the maintainer.
31. Critical, nothing pulls the archive after the C goes: applied (`lane-so.sh` always passes both `-u`).
32. Critical, `ref.sh` reads a filtered share: applied (`share.sh` first, 264 files, `MN_REF_WINE_APP`, no placeholder).
33. Important, audit skips untracked files: applied (`git add -A mtld3d` first; `<n>` grows).
34. Important, cross-group setup: applied, merged with 13.
35. Important, lanes share probe/goldens/CONVENTIONS/temp: applied (`$S/probe`, `$S/goldens`, 6c exceptions, `wm_tmpdir()`).
36. Important, dead-stripped frameworks: applied (P14; `b_asneeded` false, `-weak_framework AppKit`, sorted `otool -L`).
37. Important, `__wine_init_unix_call` stdcall: applied, merged with 11.
38. Important, `full` byte for byte: applied, merged with 14.
39. Important, Task 6 too big, generator vague: applied (6a/6b/6c; llvm-mingw clang, `-ffreestanding`, Rust casing).
40. Important, 3-5 editing the driver in parallel: applied (Task 2 owns every helper; 3-5 touch only their cases).
41. Important, Task 19 cleanup/commit/ff/P9: applied (MacNeutron worktrees, plan commit, STOP on ff, pinned `strings`).

Rejected: none.
