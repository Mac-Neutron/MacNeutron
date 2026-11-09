# DXMTRS C3b: D3D9's runtime switch between MSL and the translator's AIR — Implementation Plan

> **For agentic workers:** execution is **subagent-driven** (`superpowers:subagent-driven-development`), as the DXMTRS
> ledger's per-step loop runs it: one implementer per task (Sonnet for mechanical tasks, Opus for Task 5's runtime
> pair rule), each task reviewed as the Execution table says. Steps use checkbox (`- [ ]`) syntax for tracking.

**Go-ahead.** The maintainer's "Start C3" (chat, 2026-11-07) covers all of C3; the standing DXMTRS authorization
(ledger, 2026-10-29) self-approves this plan after three adversarial Opus reviews. C3b-2 amends C3 ruling C3-13 (its
mechanism is not buildable, `build/c3b/map.md`); the amendment is ledgered and reported to the maintainer, not waited on.

**Goal:** `shader.backend = msl | air` in `mtld3d.conf` chooses, per VS/PS pair, D3D9's MSL emitters or Metal AIR from
DXMTRS's translator (`airconv::dxso::translate` in-process on the Mac, the facade's SM50 slots in the VM guest), with
AIR records in `mtld3d_shaders.bin`, every D3D9 gate runnable on both backends, default `msl` (C3-12), and `msl`
behaviour byte-identical to C3a's.

**Split.** C3b is this plan (outline items 1-7 of the C3 plan, rebased on the code). Not here: fixed function through
the translator (C3c, so a pair with a fixed-function stage stays MSL), the default flip and the emitters' retirement
(C3d), game caches and benches (outward, parked).

**Architecture.** The config's new `ShaderBackend` crosses the PE/Unix wire like `shader.asyncCompile`. The Mac
`mtld3d.so` links the `airconv` rlib (Apple target only; uses under `cfg(not(mtl_facade))`); the facade keeps Task 9's
`sm50::translate`. `crate::metal` gains one cfg-split pair, `translate_dxso` and `new_library_with_data`, so the compile
path is cfg-free. At the pair point (`draw.rs`'s `emit_draw_view`, the one place holding the VS, the PS and the PS
variant) a pair under `air` is AIR when both stages are programmable and both lower (`airconv_ir::dxso::lower::{vertex,
pixel}`, IR-only, memoized per stage key), and neither program has failed as AIR; otherwise both stages are MSL. The
pair's backend rides in `ShaderRef` (core's `ShaderBackend`, Task 1's enum) to every index key, memo and recipe. AIR
libraries sit in the existing indexes under keys carrying the backend and under their own `CachedKind`s (10-15),
stamped with the translator key that meson now hands mtld3d.so; prewarm builds them from their metallib, regenerates
stale ones from their DXSO source, and skips them under `msl`. `src/airconv` does not change, so the translator key
stays `c298383e…`.

**Tech Stack:** Rust 1.99.0 (workspace lints, `docs/CONVENTIONS.md`), LLVM 23 (airconv, linked into `mtld3d.so`),
Metal (objc2-metal + dispatch2 on the Mac; winemetal slots 74-77, 114, 25 in the guest), meson, sh.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5 (C3), §7, §8. **C3 plan:**
`docs/superpowers/plans/2026-11-07-dxmtrs-c3-dxso-translator.md` (rulings C3-1…C3-24, the C3b outline). **Ledgers:**
`.superpowers/sdd/dxmtrs-program.md`, C3a's `.superpowers/sdd/2026-11-07-dxmtrs-c3-dxso-translator/progress.md` (R1-R23,
deferred minors). **Map:** `build/c3b/map.md` (index + contradictions table) over `build/c3b/map-runtime.md` and
`build/c3b/map-build.md`. **Pipeline rules:** `build/pipeline.md` 1-9 (its `eac-vm/` is today's `vm/`, `## dxmtrs` is
`## main`).

**Bases.** DXMTRS `main` aa02c4b1 (C3a), MacNeutron `main` 8d7db77 (pins it). Rebase before running if either moved.

---

## Rulings

Standing authorization plus "Start C3": each default below is self-approved after the three reviews. A later fact that
breaks one goes to the step ledger and, when it changes the plan, into the report for the maintainer.

- **C3b-1 (scope):** outline items 1-7, with C3a's deferred minors that C3b's switch reaches: depth readback in the
  parity harness (C3b-15), DXSO reflection never read (Global Constraints), the facade's dropped library error
  (C3b-10), `expand_subroutine`'s missing bound (C3b-16), the harness's two re-review minors (self-mode stale triage,
  a triaged `nodraw` not counted; Task 7). Every other C3a minor waits (they touch `src/airconv` or are C3d's). Cost if
  wrong: a minor found again at C3d.
- **C3b-2 (the pair rule; amends C3-13):** under `shader.backend=air` a pair is AIR iff both stages are programmable and
  `airconv_ir::dxso::lower::vertex(&program, &VsSpec)` and `lower::pixel(&program, &VariantKey)` both succeed with the
  pair's real specialization; else both stages are MSL. The lowering result is memoized as a bool per stage key (the
  keys `StageLibraries` already uses: `(ProgramId, u16, u8, VsSamplerKinds)` and `(ProgramId, VariantKey)`), on the
  encoder thread, only under `air`. The lowering is a prefilter, not a proof: `translate` can still fail after it
  (`W src/airconv/src/dxso.rs:69-75, 100-200`) and Metal can refuse the library or the pipeline; C3b-3 is the backstop. C3-13's "decided once, at first sight, by `SM50Initialize`" is unbuildable
  (`SM50Initialize` only parses DXSO; the Mac never calls it; refusals depend on the specialization). C3-13's (a)
  sticky per-VS backend cannot link an AIR VS with the MSL-only fixed-function PS before C3c, and (b) needs Metal's
  undocumented varying names; (c) is what remains, and C3c retires its one cost (a VS paired with a fixed-function PS
  is built by both compilers). C3a measured `pos-ulps 0` (the same VS's clip-space position under both backends) on all
  301 corpus pairs. Cost if wrong: multi-pass `EQUAL`/`LESSEQUAL` z-fighting in the `air` lane until C3c, for a VS
  drawn both with a fixed-function PS and a programmable one; the upgrade is (d), a per-VS sticky MSL demotion (one
  `FxHashSet<ProgramId>`).
- **C3b-3 (residual failure; amends C3-13's retry):** `retry_lost_builds` (`compile.rs:1517`) is no failure path, and
  there is no synchronous retry. Any AIR failure after the stages lowered (LLVM, `newLibraryWithData` or the
  pipeline refusing it; parity saw 0 of 301) is reported like any failed build: `install_library` adds the failed
  stage's `ProgramId`, `install_pipeline` (`W unix/src/encoder/compile.rs:1757-1773`, which records the failure for
  good) adds both of the pair's, to a session set `air_failed`; the pair rule answers MSL for any pair naming one. That
  frame's draws of the pair are dropped as any failed build's are today, under either `shader.asyncCompile` setting
  (a non-waiting draw is deferred to the submission and removed there, `draw.rs:178-182`, `compile.rs:1584-1596`; an
  `rt0_drop` draw's wait ends in `Failed` and returns `None`, `draw.rs:137-157`); later pairs go MSL. Because the
  backend sits in the index keys (Task 5), an AIR failure never lands in the MSL entry of the same stage key.
  `air_failed` is not persisted. A demoted VS drawn both demoted and (before demotion) as AIR carries C3b-2's z-fight
  cost. Cost if wrong: one dropped frame of draws per such program per launch under `air` (C3b-12's AIR lane may count
  those draws' sites as failures; investigated as C3b-12 says).
- **C3b-4 (config):** `shader.backend` (`msl` | `air`, case-insensitive, default `msl`, an unknown value warns and keeps
  `msl`) in `Mtld3dConfig` beside `shader_async_compile`, carried by `encoder_config.rs`'s wire, logged by
  `log_options` on the PE side at `Direct3DCreate9` (outline item 1's "logged at device creation" is read as that
  line). Task 5 adds one Unix-side info line at encoder start, `shader backend: <msl|air>, translator <8 hex|none>`,
  so a log shows the backend the Unix side actually received (C3b-12's lanes grep it). Cost if wrong: one log line's
  place.
- **C3b-5 (AIR records; amends C3-11's body):** `CachedKind` gains `AirSm1Vs = 10, AirSm1Ps = 11, AirSm2Vs = 12,
  AirSm2Ps = 13, AirSm3Vs = 14, AirSm3Ps = 15`; 8 and 9 stay free for C3c's fixed-function twins. Entry names
  `mtld3d_{vs|ps}_air_{sm1|sm2|sm3}_{key:08x}`. A record's body is `key [u8; 32] | ShaderSource | metallib` (no emitter
  version, no UTF-8), so `CacheEntry` gets a body enum (`Msl { emitter, msl }` / `Air { key, metallib }`). The u64
  record key is the MSL twin's (same program and specialization). Duplicate AIR records are last-wins in core's
  `push_record` (`W core/src/shader_cache.rs:1517-1527`; core has no build key, so the MSL rule "fresh wins" cannot
  apply): an append is never older than the bundle before it, so prewarm's regenerated append beats its stale twin
  through compaction; an older C3b build running later costs one regeneration. No `SHADER_CACHE_SCHEMA_VERSION` or
  `CACHE_FORMAT_VERSION` bump; a pre-C3b build sharing a game folder skips AIR records and drops them at its next
  compaction (cache-only loss), and drops recipes naming them. Cost if wrong: one D3D9 cache wipe later.
- **C3b-6 (key):** meson configures `dxmt_translator_key_h` before `subdir('d3d9')` and hands it to mtld3d.so's cargo
  run as `DXMT_TRANSLATOR_KEY_H` (Mac and guest targets); `src/d3d9/unix/build.rs` reads it as `src/d3d11/build.rs`
  does, before its non-Apple early return, into `DXMT_TRANSLATOR_KEY`. The unix crate parses it at each call
  (`translator_key() -> Option<[u8; 32]>`: 64 hex digits of `core::hint::black_box(env!("DXMT_TRANSLATOR_KEY"))`,
  else `None`; called twice per process, at encoder start and prewarm spawn), so no static (the audit confines
  `static … : OnceLock`, `W docs/CONVENTIONS.md:17`) and the literal survives LTO for C3b-13's grep. The encoder
  and prewarm take the key as an argument (`LibraryJob`, `shader_prewarm::spawn`), so tests pass `None` or another
  key. `None` (cargo-only builds: tests, loopback, rust-check) means no AIR record is written, and AIR records on disk
  are neither built nor regenerated (they stay in the file). A record whose key differs from the build's is
  regenerated at prewarm from its source by `translate_dxso`, as `refresh_msl` regenerates MSL (core's
  `needs_regeneration()` keeps its signature and answers `false` for AIR; prewarm compares keys). `vm/dxmt/build.sh` needs nothing (meson already gets
  `-Dtranslator_key_file`). Cost if wrong: stale metallibs after a partial rebuild, which the lock forbids.
- **C3b-7 (prewarm):** prewarm gets the backend. `msl`: AIR records and recipes naming AIR kinds are skipped silently
  (kept in the file; no "recipe skipped" warning, `W unix/src/shader_prewarm.rs:189-203`); `air`: every kind is built
  (fixed-function and MSL-fallback pairs still need MSL records), stale AIR records regenerated inside
  `startup_work::map` with the library builds, not in the serial `refresh_msl` loop. Its summary line gains the AIR
  library count.
  Cost if wrong: wasted prewarm work in one lane.
- **C3b-8 (linking; realizes C3-4):** `airconv` joins mtld3d-unix's `[target.'cfg(target_vendor = "apple")'
  .dependencies]` with `dispatch2` and objc2-metal's `dispatch2` feature (all in `Cargo.lock`); meson's mtld3d.so
  target gets `airconv_cargo_env` (Mac branch only); `M dxmt/mtld3d-build.sh` sets `AIRCONV_LLVM`/`AIRCONV_XCRUN` as
  `M dxmt/tools.sh` does (the loopback is an Apple target and compiles the dependency, unused under `mtl_facade`);
  `src/d3d9/unix` leaves the workspace's `default-members` (a bare `cargo test` would die in airconv's build script,
  the reason airconv is out of the list, `W Cargo.toml:31-34`). `mtld3d.so` grows by about the airconv share of
  `winemetal.so` (~38 MB, C3-4) and imports `libz` and `libxml2` besides today's twelve (`map-build.md` §3), a listed
  difference (§8). Cost if wrong: a larger `aarch64-unix` until C5.
- **C3b-9 (exports):** the Apple `mtld3d.so` exports today's six symbols plus airconv's 14 `export_name` items (12
  `SM50*`, 2 `DXIL*`, `W src/airconv/src/api.rs:61-359`), recorded as a listed difference (§8). No unexported list:
  rustc passes its own `-exported_symbols_list` for an Apple cdylib, and ld64 refuses both together (its message
  "-exported_symbol*, -unexported_symbol* and -no_exported_symbols cannot be used together", found in Xcode's `ld`).
  Not an airconv feature (it moves the key) and not `.cargo/config.toml`'s hashed sections. Checked as `nm -gU` = the
  six names of `map-build.md` §3 plus the 14, listed. Cost if wrong: 14 extra exported symbols in a two-level
  namespace image (harmless at runtime; a post-link `nmedit` is the upgrade if they ever matter).
- **C3b-10 (one translate, one library-from-bytes):** `crate::metal::translate_dxso(tokens: &[u32], spec: &StageSpec,
  name: &CStr) -> Result<Vec<u8>, String>` (Mac: `airconv::dxso::translate`, its `Error::text()` lossily to a
  `String`; facade: `mtld3d_mtl::sm50::translate`) and `crate::metal::compile_metallib_library(device, metallib: &[u8],
  entry: &str, label: &str) -> Option<StageLibHandles-shaped pair>` (Mac: `DispatchData::from_bytes` +
  `newLibraryWithData_error` + `newFunctionWithName`; facade: `sm50::new_library` + its `newFunctionWithName`), each the
  twin of `compile_shader_library` (`unix/src/metal/shader.rs:32-89`: autorelease pool, label, error logged and
  `None`). The facade's `new_library` returns its NSError's text and checks slot 25's and slot 114's status (C3a Task
  9 minor); slot 25's error is +1 (`ret_owned`, `W src/winemetal/unix/src/g4.rs:538-539`), so its text is read and the
  error released once, not through `NSError::from_autoreleased_or` (which retains again).
  `airconv::dxso::translate` makes a fresh LLVM `Context` per call (`W src/airconv/src/dxso.rs:69-75`), so the four
  compile workers may call it at once; Task 3's test proves it. Cost if wrong: a lock around translate.
- **C3b-11 (`src/airconv` untouched):** C3b changes nothing under `src/airconv` outside `tests/`; the translator key stays
  `c298383ed535cde593fbce84c046f0615952bb82125d1365336db9c689095f32`, checked after every task that edits a manifest
  (`translator_key <wt> release`) and by the identity run at Task 10. A task that finds it must edit `src/airconv`
  (an AIR-lane bug in the lowering) does so test-first, runs ID, and ledgers the key move (C3-9: unreleased). Cost if
  wrong: an unplanned key move, harmless while unreleased.
- **C3b-12 (AIR lanes, and the proof they ran AIR):** `D3D9_BACKEND=air` selects the AIR lane in three MacNeutron
  scripts; no runner change. `M dxmt/mtld3d-conformance.sh` exports `DXMT_CONFIG=shader.backend=air` (a documented
  D3D9 config source below `MTLD3D_CONFIG`, `W src/d3d9/src/config.rs:87`; the runner sets only `MTLD3D_CONFIG`,
  `W tests/d3d9/conformance/src/run.rs:298-301`, so the key survives) and passes `--keep-shader-cache
  $B/mtld3d-conformance/air-caches` (emptied first; the runner already has the flag, `cli.rs:159-163`, which turns
  `shaderCache.enable` on for that lane only). The proof is the kept caches: Wine's d3d9 tests hold ~200 programmable
  shaders in all six programmable kinds (C3a's `conformance-x64.bin`: 211 shaders, 149 recipes), and Task 4's
  `emit_corpus --kinds <file>...` prints per-kind record counts; the lane fails unless every programmable kind seen
  (MSL + AIR count > 0) has ≥ 1 AIR record. `M dxmt/d3d9-check.sh` under `air` sets `MTLD3D_CONFIG="<caller's;>
  log.dir=Z:$WORK/mtld3d-logs;shader.backend=air"` (the launcher appends the caller's after its own `log.dir`, `M
  crates/macneutron-core/src/launcher.rs:216-221`, so the run's logs land in its work folder) and requires the Unix
  info line `shader backend: air, translator c298383e` (Task 5) in them; d3d9_triangle is fixed-function (`M
  dxmt/tests/d3d9_triangle.cpp:66`), so its pairs stay MSL by design and d3d9-check is a smoke in the AIR lane, not
  AIR proof. The conformance runner's gating runs log nothing (`RUST_LOG=off`, no `log.dir` without
  `MTLD3D_CONFORMANCE_RAW_DIR`), so the info line is not its proof. `M vm/dxmt/d3d9-gate.sh` writes
  `$W/share/d3d9/backend` (beside `wow64`), which `M vm/tests/d3d9.sh` reads: the `tri` rows get
  `shader.backend=air` after their `log.dir`, conformance gets `DXMT_CONFIG` and `--keep-shader-cache
  /mnt/share/traces/d3d9/caches` (both lanes), and the host counts kinds per leg (`$G/caches/arm64x-*.bin` and
  `$G/caches/i686-arm64-*.bin`, each held to the rule, so a dead i386 AIR lane cannot pass on x64's records) with the
  same reader (built as `mtld3d-conformance.sh` builds the runner; `env-vm.sh` keeps Apple's toolchain first) and greps the info
  line in `$G/logs`. Under `air`, AIR libraries are built in the guest through the facade and the records written
  there carry the guest build's key. The AIR lane's Expected: every row runs to completion; the script's verdict is
  `conformance: no regressions vs baseline` or `… fail anew: passed`, with any `STALE BASELINE` site tabled in the
  ledger (an AIR improvement, never a baseline update from the AIR lane); each subtest's failure count at most the
  MSL lane's (the two triaged NaN sites fail in MSL and pass in AIR); the kinds row as above. A difference beyond that
  is investigated (Opus) and fixed test-first, or, when the cause is in the lowering and the fix is not small,
  ledgered as C3d's parity input with its reason. Cost if wrong: an AIR-only regression rides to C3d.
- **C3b-13 (key equality row; C3 review 2 M5):** the key is a 64-hex string in every mtld3d.so built by meson (C3b-6's
  `black_box` keeps the literal); `vm/dxmt/d3d9-gate.sh` gains a host-side row that `/usr/bin/grep -c` finds
  wine.app's `DXMT/translator` key in the guest staging copies `$B/vm-work/dxmt-guest/{x86_64,i386}-unix/mtld3d.so`
  (valid because the VM job runs `guest-stages.sh` and the image build before the gate) and in wine.app's
  `aarch64-unix/mtld3d.so` (`ok   key guest=host`); `D3D9_GATE_TRACES` re-check mode skips the row. Cost if wrong: a stale
  guest AIR record goes unseen until a draw renders wrong.
- **C3b-14 (e2e programs; C3-17):** one time-boxed attempt (Task 9) to link `tests/d3d9/programs`' e2e binary for
  `x86_64-pc-windows-msvc` with llvm-mingw's `lld-link` and its `lib*.a` (the recipe `map-build.md` §5 gives). If it
  links, the functional suite runs once per backend as information (not a gate) and the benches stay parked (they
  are benches: maintainer). If it does not link within the box, the suite stays parked and xwin is the outward ask.
  Cost if wrong: one fewer pixel oracle in C3b.
- **C3b-15 (depth parity):** the parity harness writes and reads depth (compare `Always`, writes on, `Store`, a
  blit of the depth plane into a shared buffer, `MTLBlitOption::DepthFromDepthStencil` for `Depth32Float_Stencil8`,
  compared as F32 under C3-15's float tolerance, with `depth-ulps <file> <max>` printed as information as `pos-ulps`
  is); `emit_corpus`'s four ps_1_4 depth writers (`m3x2depth`, `texdepth` families) gain depth recipes with
  `HAS_DEPTH` and without `NO_DEPTH_ATTACHMENT`, and three new synthetic programs write `oDepth` the way games do
  (`ps_2_0` and `ps_3_0` `mov oDepth, r0.x` from a varying, one with `_sat`), same flags, outside `--digest`'s set;
  `synthetic.bin` is regenerated (`--digest` unchanged). With writes on, every `HAS_DEPTH` recipe of
  `conformance-x64.bin` also becomes a rasterized-z oracle. An objc2 feature the harness needs goes in the root
  `[workspace.dependencies]` (unhashed: `toml_sections` reads `[profile.*]` only, `M wine-arm64/lib.sh:226-227`),
  never `src/airconv/Cargo.toml` (hashed). Cost if wrong: oDepth parity unproven until C3d.
- **C3b-16 (`expand_subroutine` bound):** `emit.rs`'s inliner (`W src/d3d9/core/src/dxso/emit.rs:2378-2415`, cycle
  check only) gets the AIR inliner's call depth, 32 (`W src/airconv/ir/src/dxso/lower.rs:667`, checked at
  `lower/inline.rs:277-279`), and an expansion cap of `1 << 20` expanded DXSO instructions (`call` and `nop`
  counted, as AIR counts them, `lower/ops.rs:66-70`), and refuses past either as an emit error (the library fails;
  draws dropped, as any emit error). The cap is not AIR's 65536 (`MAX_DXSO_INSTRUCTIONS`, `lower.rs:665`): that one
  refuses legal programs (C3a's deferred minor: a 16k-instruction subroutine called from 5 branches, 81920), which the
  MSL emitter emits today and must keep emitting (C3b-19). 2^20 is 16× AIR's budget and 32× D3D9's
  `MaxVShaderInstructionsExecuted` floor, so it only stops the doubling-call bomb (32 labels each calling the next
  twice: 2^31 instructions, which today expands without bound on the encoder thread). It moves `SHADER_EMITTER_VERSION` once
  (C3-20's fingerprint), so players' MSL records regenerate once at prewarm, with identical MSL for every program
  that emits today below the cap (`emit_corpus --digest` unchanged); unreleased (C3-9). Under C3b-2 programs AIR
  refuses for size (≥ 2^16) go MSL, which accepts them up to 2^20. Cost if wrong: a program expanding past 2^20
  instructions fails on both backends (none known; its MSL would be tens of MB).
- **C3b-17 (gates):** per the lighter-task-testing memory, each task runs its own tests; every build gate runs once, at
  Task 10, on the final tips, the spec §8 set plus each config-driven D3D9 gate on both backends, then the final
  review (with the results), one fix wave, a scoped re-review and a rerun of the rows the wave's files feed (C3a's
  R18 put review first because a fix wave moved the key; C3b-11 forbids a key move). Task 5 has one early lock job (a
  `make wine-arm64`, `d3d9-check` on both backends, and an AIR conformance subset with its kept caches counted)
  because the pair rule is the plan's risk. Cost if wrong: a late find costs one more gate run.
- **C3b-18 (release):** C3-9 holds: no tag, no release until C3d. Pushing and pinning are not releasing.
- **C3b-19 (`msl` is C3a's runtime):** with `shader.backend=msl` the D3D9 runtime does exactly what C3a's does: the same
  MSL (C3b-16's version bump aside), the same cache bytes and recipes, the same draw path; the pair rule and the AIR
  memo are not consulted (`pair_backend` returns `Msl` on its first compare). New under `msl`: the one info line at
  encoder start and prewarm's silent skip of AIR records and recipes (none exist unless an `air` run wrote them). Checked by `emit_corpus --digest` (= `msl-digest 730b0a5c1332e44f 131 shaders`) and the MSL
  lanes' figures equal to C3a's (job3/job4). Cost if wrong: a players' regression in the default.

## Global Constraints

- **Who pushes.** Nobody in this plan pushes, merges to `main`, fetches or downloads; Task 10 stops before the push
  steps (Push order, below), which the parent runs. No MacNeutron.app, Steam or game; nothing under `~/Library/
  Application Support/MacNeutron` or `steamapps/compatdata`; kill by exact PID or process group, never `pkill -f`;
  home written `~` in anything committed; no SteamID or account name printed; no Valve binary or game shader cache
  committed; no hook bypass; no PR or contribution to DXMT upstream. Commits end with `Co-Authored-By: Claude Opus 5.5
  <noreply@anthropic.com>`; DXMTRS subjects `<area>: sentence`. Never delete a branch; worktrees go with `git worktree
  remove` (after `chmod -R u+w` of the filled submodules); never `git submodule deinit`.
- **Hold.** If the parent says the maintainer is playing: stop the running lock job by its process group, run nothing
  heavy (no gate, build, VM boot, GPU work), ledger where it stopped; a killed gate run is void and reruns in full.
- **Usage stop rule:** weekly usage checked between tasks; at 95% stop (state in the ledgers); at 90%+ only work that
  lands something already done.
- **Names.** `$M` = `~/Documents/MacProton`, `$D` = `$M/build/wine-arm64-src/dxmt`, `$S` = `$M/build/c3b`, `$B` =
  `$M/build`. DXMTRS integration worktree `$S/wt` (branch `c3b` from aa02c4b1), MacNeutron worktree `$S/wt-mn` (branch
  `c3b-mn` from 8d7db77). Lanes: `sh $S/lane.sh <lane>` cuts `$S/wt-<lane>` on `c3b-<lane>` from `c3b` (submodules
  filled read-only); `sh $S/integrate.sh <lane>` rebases, fast-forwards `c3b`, checks `git cherry` empty. A conflict in
  `Cargo.lock` or `tools/derive_inventory.txt` is resolved by regenerating them, never by hand. `$S/wt-mn` is one
  worktree shared by Tasks 0, 2, 6 and 10: MacNeutron commits add explicit owned paths only (`git add <paths>`, never
  `-A`/`-a`), and none is made while another lane's MacNeutron edit is uncommitted.
- **The lock.** Every build or install into the shared folders, every gate reading installed outputs and every checkout
  in `$D` runs as one job: `/usr/bin/lockf -k $B/pipeline.lock sh <job>.sh`, the job logging its `$D` sha and
  wine.app's CDHash, its trap restoring `$D` to `$(cat $B/wine-arm64-src/dxmt.applied)` (fa3bc094). Task 5's early
  job runs from the main checkout at `main`; Task 10's Mac and VM jobs run with the main checkout detached at `c3b-mn`
  for the whole job (`M dxmt/d3d9-check.sh:20-26` reads `$ROOT/build` and `$ROOT/target`, so the scripts cannot run
  from `$S/wt-mn`; `git -C $M status -sb` shows `## main` before, restored by `trap 'git -C $M checkout -q main' EXIT`).
  Templates: `$B/c3/gates/job6.sh` (its `start=$(cat …dxmt.applied)` restore) and `job4.sh` (VM order), sourcing
  `$B/c3/gates/env.sh` / `env-vm.sh` by full path (not `$S/gates/…`), no hard-coded sha; VM jobs use `env-vm.sh`
  (C3a R23: llvm-mingw's bin first on PATH shadows Apple clang and `vm/host/build.sh` dies on `string.h`). While a
  lock job runs, lanes run no GPU work (parity, Mac GPU tests): timing rows (`d3d9 vsync`) flake beside it.
- **Builder.** `BUILDER_MAC=52:54:00:ea:c0:03` exported for every `vm/builder.sh` call and VM job; reuse a running
  builder when `sh vm/builder.sh ssh true` answers (`pgrep -x vmhost`; never a second); else start
  `VM_SHARES="out=$B/vm-work/images,outp=$B/p/vm-work/images" sh vm/builder.sh start`.
- **Signing env** for `make wine-arm64`, `vm/dxmt/build.sh`, `vm/host/build.sh`, `vm/image/guest-stages.sh dxmt`:
  `MACNEUTRON_SIGN_IDENTITY="Developer ID Application: Chad Cormier Roussel (49QMZXLR8S)"`,
  `MACNEUTRON_PROVISIONING_PROFILE=$B/Mac_Neutron.provisionprofile`.
- **Rust.** Every cargo line runs offline, with the pinned toolchain on PATH (`cargo` is not on this shell's PATH and
  rustup's default is `stable`): `PATH=~/.rustup/toolchains/1.99.0-aarch64-apple-darwin/bin:$PATH` as
  `$B/c3/gates/env.sh:6` sets it, and `MACOSX_DEPLOYMENT_TARGET=27.0` for Apple-target links (as
  `W tools/cargo-staticlib.sh:22`). `AIRCONV_LLVM=$B/wine-arm64-src/llvm23-arm64-nolto
  AIRCONV_XCRUN=$M/wine-arm64/tools/xcrun-metal.sh` for any cargo run that builds mtld3d-unix or airconv for an Apple
  target (from Task 2 on, that includes `-p mtld3d-unix`). **UT/CK/LX/RL/Manifests** as the C3 plan's Global
  Constraints define them (CK: nightly `cargo fmt --all --check`, clippy `-D warnings` on `aarch64-apple-darwin` and
  each shipped `-pc-windows-msvc` target, `cargo doc` with `-D warnings`, UT, `git add -A && sh tools/audit.sh` →
  `audit: clean (<n> files)`; LX: `tools/linux-cargo.sh` builds of `mtld3d-core`, `mtld3d-mtl` and `mtld3d-unix` for
  the guest's `x86_64` and `i686`; RL: `tools/rust-check.sh` → `rust-check: ok`). **KEY:** `( . $M/wine-arm64/lib.sh;
  translator_key <wt> release )` prints `c298383e…` (C3b-11); run as `$B/c3/xlate.sh` runs it.
- **DXSO reflection is never read** (C3a Task 2 minor: its bind indices are 0): no C3b code calls
  `SM50GetArgumentsInfo` or reads a reflection for D3D9; bindings stay C3-7's fixed slots.
- **`msl` unchanged** (C3b-19): under `msl` no new code path runs beyond reading the config field, the one info line
  and prewarm's silent skip of AIR records and recipes.
- Conventions (`docs/CONVENTIONS.md`): no `#[allow]`/`#[expect]`; one-operation `unsafe` blocks with `SAFETY:`; no
  `pub(crate)`; tests in `<stem>/tests.rs`; `foo.rs` + `foo/`; `FxHashMap`; derives through `tools/audit.sh
  --update-derives` (each task that adds a derive owns `tools/derive_inventory.txt` for its regeneration). zsh:
  gating scans use `/usr/bin/grep`.

## Rules check (`W CLAUDE.md`)

| Introduced | Where | Why allowed |
|---|---|---|
| config key `shader.backend` + one wire byte | Task 1 | the outline's switch; carried as `shader.asyncCompile` is; `decode_settings` refuses a short or trailing payload (`W unix/src/encoder_service.rs:277-290`), so no wire version |
| `enum ShaderBackend` (derives `Clone, Copy, Default, Debug, PartialEq, Eq, Hash`) | Task 1, reused by Tasks 4-5 | one enum for config, `ShaderRef` and index keys (no second unix-side `Backend`); inventory regenerated |
| `enum EntryBody` (derives as `CacheEntry`'s) | Task 4 | an AIR body is not UTF-8 MSL; inventory regenerated |
| six `CachedKind` arms 10-15 | Task 4 | C3-11; 8/9 kept for C3c |
| `ShaderRef::backend`, a `PipelineIdentity` program pair | Task 5 | the one carrier the closures already hold (`compile.rs:928-942, 1443-1450`); `install_pipeline` needs the programs for `air_failed` |
| dependencies `airconv`, `dispatch2` (+ objc2-metal `dispatch2` feature) on mtld3d-unix, Apple only | Task 2 | C3-4; all in `Cargo.lock`; the guest links none |
| statics | none | the key is parsed per call (C3b-6); no `OnceLock`/`LazyLock` |
| lint suppressions, duplicates | none | `translate_dxso`/`compile_metallib_library` are cfg twins of one signature, not copies |

## Review Focus

1. C3b-2/C3b-3: does every pair the encoder can form (programmable/FF × programmable/FF, a PS whose variant depends on
   the VS's `linked_input_mask`, `SetPixelShader(NULL)`, no-colour sibling pipelines, deferred and `rt0_drop` waiting
   draws) end with both stages from one backend, the one `ShaderRef::backend` names? Can an AIR VS ever meet an MSL PS
   (Metal's link would fail on `d3d9_<code>` names)? Does every AIR failure (library, pipeline) reach `air_failed`?
2. The memo and index keys: the backend wherever an AIR and an MSL library of the same stage key could be confused
   (`StageLibraries`' keys, `LibraryMemo`, `lib_cache`'s `ShaderRecordRef`, `pending_libs`, `pipeline_shader_refs`,
   recipes, deferred templates); an AIR failure never recorded under the MSL key.
3. C3b-5/C3b-6: every exhaustive and `matches!` site on `CachedKind` (`map-runtime.md` §5's list), `is_programmable` on
   AIR kinds, the body enum's decode bounds (a short or hostile file, the `disk_key` check kept), the empty-key rule,
   stale-key regeneration and AIR last-wins dedupe through compaction.
4. C3b-8/C3b-9: exports (six + 14, listed), imports, size; every Apple cargo run of mtld3d-unix has `AIRCONV_*`; the
   key unchanged.
5. C3b-10: `translate_dxso` from four workers at once; the facade's library error path; no reflection read.
6. C3b-12/C3b-13: the AIR lanes really run AIR (conformance's kept caches hold AIR records per kind; the info line
   shows the config reached the Unix side; d3d9-check is a smoke only); a lane that silently stayed MSL fails; the
   guest key row reads the staged binaries.
7. C3b-19: nothing new runs under `msl`; the MSL lanes' figures equal C3a's.

## Execution

| Task | Lane | Starts from | Implementer | Reviews | Depends on | Owns | Lock |
|---|---|---|---|---|---|---|---|
| 0 base, ledgers, plan commit | owner | aa02c4b1 / 8d7db77 | owner | — | — | `$S/{base}`, ledgers, plan commit | no |
| 1 config | `cfg` | `c3b` after 0 | Sonnet | Sonnet | 0 | `src/d3d9/core/src/{config,encoder_config}.rs` + tests, `src/d3d9/src/config.rs` if the log needs it, `mtld3d.conf`, `docs/d3d9/STATUS.md`, `tools/derive_inventory.txt` | no |
| 2 linking, key | `link` | `c3b` after 0 | Sonnet | Sonnet + Opus | 0 | `Cargo.toml` (default-members, objc2-metal feature if workspace-level), `src/d3d9/unix/{Cargo.toml,build.rs,src/key.rs,src/key/tests.rs}`, `src/meson.build`, `src/dxmt/meson.build`, `src/d3d9/meson.build`, `src/d3d9/unix/meson.build`, `Cargo.lock`, `$S/wt-mn` `dxmt/mtld3d-build.sh` | no |
| 3 translate and library from bytes | `lib` | `c3b` after 2 | Sonnet | Sonnet + Opus | 2 | `src/d3d9/unix/src/metal/{shader.rs,shader/tests.rs}`, `src/d3d9/unix/src/metal.rs`, `src/d3d9/mtl/src/{sm50.rs,sm50/tests.rs,metal.rs}` | Step 3 |
| 4 AIR records, `--kinds` | `cache` | `c3b` after 0 | Sonnet | Sonnet + Opus | 0 | `src/d3d9/core/src/{shader_key.rs,shader_key/tests.rs,shader_cache.rs,shader_cache/{source.rs,tests.rs}}`, `core/src/perf/compilation.rs`, `core/examples/emit_corpus.rs` (kind lists, `--kinds`) + `emit_corpus/tests.rs`, one line each in `unix/src/shader_prewarm.rs:305` and `tests/d3d9/programs/tests/e2e/shaders.rs:521` (the body enum's accessor), `tools/derive_inventory.txt` | no |
| 5 the pair rule, AIR builds, prewarm, info line | `rt` | `c3b` after 1-4 | Opus | Sonnet + Opus | 1, 2, 3, 4 | `src/d3d9/core/src/draw_data.rs` (`ShaderRef::backend`), `src/d3d9/unix/src/{draw.rs,encoder.rs,encoder/compile.rs,encoder/compile/{libraries.rs,libraries/tests.rs,tests.rs},shader_prewarm.rs,encoder_service.rs}`, `tools/derive_inventory.txt` if derives move | Step 4 |
| 6 AIR gate lanes | `$S/wt-mn` only | `c3b-mn` after 0 | Sonnet | Sonnet | 1, 4 (names only) | `dxmt/{d3d9-check.sh,mtld3d-conformance.sh}`, `vm/dxmt/d3d9-gate.sh`, `vm/tests/d3d9.sh` | no |
| 7 depth parity | `pt` | `c3b` after 0 | Sonnet | Sonnet + Opus | 0 | `src/airconv/tests/dxso_parity.rs`, `src/d3d9/core/examples/emit_corpus.rs` (depth recipes), `tests/d3d9/dxso-corpus/{synthetic.bin,README.md,triaged.txt}` | no |
| 8 MSL inliner bound | `msl` | `c3b` after 0 | Sonnet | Sonnet + Opus | 0 | `src/d3d9/core/src/dxso/{emit.rs,emit/tests.rs}` | no |
| 9 e2e link attempt | `e2e` | `c3b` after 0 | Sonnet | Sonnet | 0 | `tools/` (one link wrapper, if it links), `tests/d3d9/programs/README` or `COVERAGE.md` note | no (its run is in Task 10's Mac job) |
| 10 gates, final review | owner | `c3b` with 1-9 | Sonnet (jobs) | Opus final | 1-9 | ledgers, spec line on `c3b-mn` | yes |

Tasks 1, 2, 4, 6, 7, 8, 9 start together after Task 0 (disjoint files; Task 4 and Task 7 both touch `emit_corpus.rs`,
in different functions: whichever integrates second rebases; Task 2 and Task 6 share `$S/wt-mn` under the explicit-path
rule). Task 3 starts after 2, Task 5 after 1-4 (Task 4's two one-line edits in Task 5's and the e2e files land first).
Reviews per the lighter-task-testing memory: a Sonnet spec+gate lens on every task and an Opus correctness lens on 2-5,
7 and 8; Minors get no fix round (batched into the ledger). Task 4's and Task 7's `emit_corpus.rs` edits are kept to
their functions so the rebase is mechanical.

---

### Task 0: base, ledgers, plan commit (owner)

- [ ] **Step 1:** worktrees exist (`$S/wt` on `c3b` at aa02c4b1 with submodules filled read-only, `$S/wt-mn` on
      `c3b-mn` at 8d7db77; done at planning). The plan, `build/c3b/map*.md` evidence and the three reviews are in `$S`;
      the plan is committed on `c3b-mn` (`docs: the DXMTRS C3b plan`). Program ledger line "C3b STARTED …"; step ledger
      `.superpowers/sdd/2026-11-09-dxmtrs-c3b-runtime-switch/progress.md` opened with a preflight scan (as C3a's).
- [ ] **Step 2:** base references, host-only, no lock: KEY on `$S/wt` (= `c298383e…`); `cargo run --offline --release -p
      mtld3d-core --features disk-cache --example emit_corpus -- --iters 1 --digest` in `$S/wt` (target dir
      `$S/cargo-base`) → `$S/base/msl.txt` = `msl-digest 730b0a5c1332e44f 131 shaders`; parity at the base
      (`AIRCONV_*` set, `cargo test --offline -p airconv --test dxso_parity -- --nocapture`) = C3a's two lines; the C3a
      gate baselines (`map-build.md` §4) copied into the step ledger as Task 10's targets.

**Expected:** key, digest and parity equal to C3a's final figures; plan committed; ledgers opened.

### Task 1: `shader.backend` (lane `cfg`)

**Files:** `src/d3d9/core/src/config.rs` (+`config/tests.rs`), `src/d3d9/core/src/encoder_config.rs`
(+`encoder_config/tests.rs`), `mtld3d.conf` (a commented block beside `shader.asyncCompile`, `:200-232`),
`docs/d3d9/STATUS.md` (the key list), `tests/d3d9/conformance/CONFORMANCE.md` if it lists config keys,
`tools/derive_inventory.txt`. `ShaderBackend` is also Task 5's backend type (`ShaderRef`, index keys), so it derives
`Hash`/`Eq` too.

**Interfaces:** `pub enum ShaderBackend { Msl, Air }` (`Default` = `Msl`, `#[repr(u8)]` wire bytes 0/1) in
`mtld3d_core::config`; `Mtld3dConfig::shader_backend: ShaderBackend`; key `shader.backend`; label fn and warn on a bad
value as `assign_cursor_software` (`config.rs:740-751`); `log_options` prints `shader.backend = msl|air` beside
`shader.asyncCompile`; `WireValue` for `ShaderBackend` as `AdapterSpoof`'s (`encoder_config.rs:92-108`), refusing an
unknown byte.

- [ ] **Step 1 (RED):** `config::tests::shader_backend_defaults_to_msl`, `…::shader_backend_air_parses`,
      `…::shader_backend_bad_value_warns_and_keeps_msl`, `…::mtld3d_config_env_overrides_the_file_for_shader_backend`;
      `encoder_config::tests` round trip flips the new field (the existing flip-every-field test, `:20-52`) and
      `…::unknown_shader_backend_byte_is_refused`.
- [ ] **Step 2:** the field, parse, log, wire, sample conf, docs.
- [ ] **Step 3:** CK(`mtld3d-core`, `mtld3d`, `mtld3d-unix`), LX.

**Expected:** the new tests pass, every existing config and wire test passes. Commit: `d3d9(core): shader.backend
chooses MSL or the translator's AIR, default msl`.

### Task 2: mtld3d.so links airconv and gets the key (lane `link`)

**Files:** `Cargo.toml` (`default-members` without `src/d3d9/unix`, the comment `:31-34` naming it beside airconv; the
objc2-metal `dispatch2` feature where mtld3d-unix's dependency line can enable it, at the crate if possible),
`src/d3d9/unix/Cargo.toml` (Apple deps `airconv = { workspace or path }`, `dispatch2`), `src/d3d9/unix/build.rs` (key
read before the non-Apple return, as `src/d3d11/build.rs`), `src/d3d9/unix/src/key.rs` (+`key/tests.rs`), `src/meson.build` (the
key header configured before the crossbuild branch, `fs` already imported), `src/dxmt/meson.build` (uses it, no second
configure), `src/d3d9/unix/meson.build` (Mac target `env: d3d9_env + airconv_cargo_env + {key}`, guest target `env:
d3d9_env + {key}`), `Cargo.lock`, `tools/derive_inventory.txt` if derives move; MacNeutron `$S/wt-mn/dxmt/mtld3d-build.sh`
(`AIRCONV_LLVM=$B/wine-arm64-src/llvm23-arm64-nolto` unless set, `AIRCONV_XCRUN=$ROOT/wine-arm64/tools/xcrun-metal.sh`).

**Interfaces:** `mtld3d_unix::key::translator_key() -> Option<[u8; 32]>` (parses
`core::hint::black_box(env!("DXMT_TRANSLATOR_KEY"))` at each call, no static: C3b-6), `fn parse_key(&str) ->
Option<[u8; 32]>` (64 hex digits, either case, else `None`). The `black_box` keeps the 64-hex literal in the shipped
binary for C3b-13's grep (a `const` parse would drop it).

- [ ] **Step 1 (RED):** `key::tests::{parses_64_hex_digits, empty_is_none, short_is_none, non_hex_is_none}`.
- [ ] **Step 2:** manifests, build.rs, meson; Manifests line (`cargo metadata --offline`,
      `tools/audit.sh --update-derives`).
- [ ] **Step 3 (host checks, no lock):** in the lane, with `AIRCONV_*`, `MACOSX_DEPLOYMENT_TARGET=27.0` and
      `DXMT_TRANSLATOR_KEY_H` pointing at a header written like meson's (`#define DXMT_TRANSLATOR_KEY
      "<c298383e…>"`) under `$S/cargo-link/` (not in the lane, where CK's `git add -A` would commit it), `cargo build
      --offline --profile production --target aarch64-apple-darwin -p mtld3d-unix` (target dir `$S/cargo-link`), then
      on the dylib: `nm -gU` → the six names of `map-build.md` §3 plus airconv's 14 (C3b-9), listed in the ledger;
      `otool -L` → today's list plus `libz.1` and `libxml2.2`; size logged; `/usr/bin/grep -c c298383e` → ≥ 1. These
      are the lane's figures (`llvm23-arm64-nolto`); the shipped ones (meson's ThinLTO `llvm23-arm64`) are Task 10's. The
      loopback form (`--features mtld3d-unix/winemetal`) also builds. `meson setup` is not run here (Task 10's `make
      wine-arm64` is its check); `meson.build` edits are read by the Opus reviewer against `src/airconv/meson.build:31-33`
      (where `airconv_cargo_env` exists) and the guest branch.
- [ ] **Step 4:** CK(`mtld3d-unix`), LX (the guest build still links: no airconv on Linux), KEY.

**Expected:** key tests pass; the host checks as stated; KEY unchanged; `audit: clean`. Commit: `d3d9(unix): mtld3d.so
links the translator on the Mac and carries the translator key` (meson in the same commit); MacNeutron (`git add
dxmt/mtld3d-build.sh` only): `dxmt: the loopback mtld3d.so build gets the translator's LLVM`.

### Task 3: translate and library from metallib, both cfgs (lane `lib`)

**Files:** `src/d3d9/unix/src/metal/shader.rs` (+`shader/tests.rs`), `src/d3d9/unix/src/metal.rs` (re-exports),
`src/d3d9/mtl/src/sm50.rs` (+tests: `new_library` returns `Result<Library, String>` with the NSError text, slot 25's
and slot 114's status checked, the +1 error released once: C3b-10), `src/d3d9/mtl/src/metal.rs` (whatever `newFunctionWithName` on a facade library needs, if not there).

**Interfaces:** C3b-10's two functions with the same signatures on both cfgs; they log as `compile_shader_library`
does (`target: LOG_TARGET`) and return `None`/`Err` on failure.

- [ ] **Step 1 (RED):** `metal::shader::tests::translates_and_loads_a_vs_2_0_and_ps_2_0` (Mac, objc2; a pipeline from
      both functions with a vertex descriptor, as C3a fix2's facade test), `…::four_threads_translate_at_once` (32
      translations on 4 threads, each library loads), `…::a_bad_metallib_is_none` (bytes `b"nope"`);
      `mtld3d_mtl::sm50::tests::new_library_reports_a_bad_metallib` (runs where `MTLD3D_WINEMETAL` names a winemetal,
      else `skipped: no winemetal`, as C3a's facade test).
- [ ] **Step 2:** the functions; the facade fixes.
- [ ] **Step 3 (lock, short):** `$D` is not checked out. wine.app's `winemetal.so` cannot load in place (it links
      `@rpath/ntdll.so` and `winemac.so`), so the job does what `M dxmt/mtld3d-facade-test.sh:10-18` does, from the lane:
      `make -s -C $M/vm/metal/wm metal-host HOST=$B/mtld3d-facade-host WINEMETAL=$B/wine-arm64-src/dxmt-build/src/
      winemetal/unix/winemetal.so`, copy that `winemetal.so` into `$B/mtld3d-facade-host/`, then
      `PATH="$(sh $M/dxmt/mtld3d-tools.sh):$PATH" MACOSX_DEPLOYMENT_TARGET=27.0
      MTLD3D_WINEMETAL=$B/mtld3d-facade-host/winemetal.so CARGO_TARGET_DIR=$S/cargo-lib cargo +1.99.0 test --offline
      -p mtld3d-mtl --target aarch64-apple-darwin --lib sm50::tests`; logs that `winemetal.so`'s `shasum` and the
      `$D`'s HEAD and `dxmt.applied` at run time (C3a's translator either way).
- [ ] **Step 4:** CK(`mtld3d-unix`, `mtld3d-mtl`), LX, KEY.

**Expected:** the Mac tests pass on this Mac's GPU; the facade test passes against wine.app's winemetal; LX builds.
Commit: `d3d9(unix): DXSO to a Metal library through the translator, in-process or through winemetal`.

### Task 4: AIR records in `mtld3d_shaders.bin`, and a kinds reader (lane `cache`)

**Files:** `src/d3d9/core/src/shader_key.rs` (+tests), `src/d3d9/core/src/shader_cache.rs`,
`src/d3d9/core/src/shader_cache/{source.rs,tests.rs}`, `src/d3d9/core/src/perf/compilation.rs` (kind arms),
`src/d3d9/core/examples/emit_corpus.rs` (kind lists `:237-244, 629, 651`; the `--kinds` mode) +
`examples/emit_corpus/tests.rs`; the two lines the body enum forces outside core: `src/d3d9/unix/src/shader_prewarm.rs:305`
(`&entry.msl` → the accessor) and `tests/d3d9/programs/tests/e2e/shaders.rs:521` (`entry.msl = …` → the setter);
`tools/derive_inventory.txt`.

**Interfaces** (chosen so `compile.rs` needs no edit: `from_programmable`, `CacheEntry::new`, `needs_regeneration()`
and `refresh_msl` keep their signatures):
- `CachedKind::{AirSm1Vs = 10 … AirSm3Ps = 15}`; `CachedKind::air(self) -> Option<CachedKind>` (programmable MSL kind
  → its twin, FF → `None`), `is_air(self) -> bool`, `msl_twin(self) -> CachedKind`; `from_programmable(major, pixel)`
  unchanged (AIR kinds are `from_programmable(..)?.air()`); `is_programmable` true for AIR kinds; `is_vertex`/
  `is_pixel` cover them; `entry_name` gives C3b-5's names; `compile_bucket` maps AIR kinds as their twins.
- `CacheEntry { kind, key, source, body: EntryBody }`, `enum EntryBody { Msl { emitter: u64, msl: String }, Air {
  translator: [u8; 32], metallib: Vec<u8> } }`; `CacheEntry::new` as today (MSL), `CacheEntry::new_air(kind, key,
  translator, metallib, ShaderSource)`; `msl(&self) -> Option<&str>`, `set_msl(&mut self, String)` (MSL only),
  `air(&self) -> Option<(&[u8; 32], &[u8])>`; `needs_regeneration()` as today for MSL, `false` for AIR (the key
  comparison is prewarm's, C3b-6); decode refuses an AIR body shorter than `32 + source`, an empty metallib, a source
  whose kind is not the record's twin, and a source whose `disk_key()` is not the record key (as MSL's,
  `shader_cache.rs:495-497`); the parser comment that says kinds are `0..=7` (`:968-972`) is fixed.
- `push_record`: a duplicate AIR record replaces the earlier one (last wins, C3b-5); MSL keeps "fresh wins".
- `ShaderSource::decode` accepts AIR kinds by their twin; `PipelineRecipe` round-trips AIR refs, and `is_valid` holds
  for an AIR VS + AIR PS; a recipe mixing an AIR and an MSL stage is invalid (C3b-2: never written).
- `load` / `compact` / `write_bundle` keep AIR records; `read_stats` counts them.
- `emit_corpus --kinds <file>...`: reads each cache with `read_records` and prints one line summed over the files,
  `kinds <n> files: sm1vs=<n> sm1ps=<n> … sm3ps=<n> air_sm1vs=<n> … air_sm3ps=<n> ffvs=<n> ffps=<n>`; exits 1 on an
  unreadable file. The AIR lanes' check (C3b-12) is: for each programmable kind with `sm…` + `air_sm…` > 0,
  `air_sm…` ≥ 1.

- [ ] **Step 1 (RED):** `shader_key::tests::air_kinds_round_trip_and_name` (bytes 10-15, names, twins, buckets,
      `from_byte(8)`/`(9)` refused), `shader_cache::tests::{air_record_round_trips, air_record_survives_compaction,
      air_record_with_a_short_body_is_dropped, air_record_whose_source_kind_mismatches_is_dropped,
      air_record_whose_key_mismatches_its_source_is_dropped, a_later_air_record_beats_its_earlier_twin_across_compaction,
      air_record_never_needs_msl_regeneration, a_recipe_of_two_air_stages_round_trips,
      a_recipe_mixing_air_and_msl_is_invalid, a_bundle_of_msl_and_air_records_loads}`;
      `emit_corpus::tests::kinds_counts_msl_and_air_records` (a written bundle of two MSL and one AIR record).
- [ ] **Step 2:** the kinds, the body enum (the two outside lines moved to the accessors), the decode bounds,
      `push_record`'s AIR rule, `append_shader`'s capacity from the body's length, `--kinds`.
- [ ] **Step 3:** CK(`mtld3d-core`, `mtld3d-unix`), clippy `-p mtld3d-tests --all-targets` on
      `x86_64-pc-windows-msvc` and `i686-pc-windows-msvc` (rust-check's lint of the e2e file), LX;
      `emit_corpus --iters 1 --digest` = `$S/base/msl.txt`.

**Expected:** new tests pass; every existing cache test passes unchanged; digest equal. Commit: `d3d9(core): AIR
shader records in mtld3d_shaders.bin, keyed by the translator`.

### Task 5: the pair rule, AIR builds and prewarm (lane `rt`)

**Files:** `src/d3d9/core/src/draw_data.rs` (`ShaderRef::backend`), `src/d3d9/unix/src/draw.rs`,
`src/d3d9/unix/src/encoder.rs`, `src/d3d9/unix/src/encoder/compile.rs`,
`src/d3d9/unix/src/encoder/compile/{libraries.rs,libraries/tests.rs,tests.rs}`, `src/d3d9/unix/src/shader_prewarm.rs`,
`src/d3d9/unix/src/encoder_service.rs`, `tools/derive_inventory.txt` if derives move.

**Interfaces** (the backend type is Task 1's `mtld3d_core::config::ShaderBackend`; no unix-side enum):
- `ShaderRef { vs, ps, variant, backend: ShaderBackend }` (core, `draw_data.rs:905-909`), set once at the pair point
  (`draw.rs:799, 811`, the only constructions), so `pipeline_identity`/`pipeline_shader_refs`, the closures of
  `get_or_create_pipeline`, `queue_no_color_sibling`, `defer_pipeline`, `defer_pending_libraries` and
  `PipelineRetry.shaders` (`compile.rs:928-942, 996, 1420, 1443-1450`, `draw.rs:208-215`) read one value; recipes of AIR
  pairs, deferred or direct, name AIR kinds.
- `FrameEncoder` holds the configured backend, the build key (`Option<[u8; 32]>`, from `key::translator_key()` in
  `FrameEncoder::new`), `air_ok_vs: FxHashMap<(ProgramId, u16, u8, VsSamplerKinds), bool>`, `air_ok_ps:
  FxHashMap<(ProgramId, VariantKey), bool>`, `air_failed: FxHashSet<ProgramId>`; `fn pair_backend(&mut self, vs:
  VsSourceView<'_>, ps: PsSourceView<'_>, variant: &VariantKey) -> ShaderBackend` (`Msl` on its first compare unless
  configured `Air`; else `Air` iff both programmable, neither in `air_failed`, both lowerings ok); called in
  `emit_draw_view` before `lookup_libraries`, its answer passed to `lookup_libraries` and into the `ShaderRef`.
- `FrameEncoder::new` logs once, info: `shader backend: <msl|air>, translator <first 8 hex|none>` (the line C3b-12's
  lanes grep).
- Index keys carry the backend: `prog_vs: (ProgramId, u16, u8, VsSamplerKinds, ShaderBackend)`, `prog_ps: (ProgramId,
  VariantKey, ShaderBackend)` (no parallel maps, so `clear`, `forget_failures` and `ff_count` cover AIR unchanged, and
  `install_library` records an AIR outcome under the AIR key only); `LibraryMemo` (address-keyed) holds the backend
  it answered for and misses on another; `lib_cache`'s `ShaderRecordRef` and `pending_libs` use the AIR kind.
- `LibraryInput::{ProgrammableVs, ProgrammablePs}` gain `backend: ShaderBackend`, `LibraryJob` the build key;
  `build_library`'s AIR branch: `translate_dxso(tokens, &StageSpec, entry)` → `compile_metallib_library` → persist
  `CacheEntry::new_air` only when the job's key is `Some` and the cache persists. A failed AIR library: `install_library`
  inserts its `ProgramId` into `air_failed` (C3b-3).
- `PipelineIdentity` gains `air_programs: Option<(ProgramId, ProgramId)>` (set by `pipeline_identity` when
  `shaders.backend` is `Air`, from `ProgrammableVsSource::vs_id`/`ProgrammablePsSource::ps_id`); `install_pipeline`
  inserts both into `air_failed` when the pipeline failed (C3b-3), logged once.
- Prewarm: `shader_prewarm::spawn(.., backend: ShaderBackend, key: Option<[u8; 32]>)`; under `Msl` AIR records and
  recipes naming AIR kinds skipped silently (C3b-7); under `Air` AIR records built by `compile_metallib_library`,
  those whose key differs from `key` (C3b-6) regenerated by `translate_dxso` from their source inside
  `startup_work::map` and re-appended, all AIR records skipped when `key` is `None`; the summary line gains
  `(<n> AIR)`.

- [ ] **Step 1 (RED):** unit tests (`compile/tests.rs`, `libraries/tests.rs`, prewarm's): `pair_backend_is_msl_under_msl`,
      `pair_backend_air_for_two_programmable_stages`, `pair_backend_msl_when_either_stage_is_ff`,
      `pair_backend_msl_when_a_program_failed`, `pair_backend_msl_when_a_lowering_refuses` (a program over the sampler
      limit), `air_lowering_is_memoized_per_stage_key`, `memo_keys_keep_backends_apart` (an MSL and an AIR library of
      one stage key are two entries; the address memo misses on the other backend),
      `an_air_failure_leaves_the_msl_index_unknown`, `failed_air_pipeline_demotes_the_pair`,
      `pipeline_refs_of_an_air_pair_name_air_kinds` (direct and deferred), `prewarm_skips_air_records_and_recipes_under_msl`,
      `prewarm_builds_air_records_under_air` (Mac, GPU), `prewarm_regenerates_an_air_record_with_another_key` (Mac, GPU;
      `spawn` given `Some(other)`), `an_air_record_is_not_written_without_a_build_key` (job key `None`).
- [ ] **Step 2:** the carrier, the rule, the AIR branch, the keyed indexes and memo, the two `air_failed` insertions,
      the info line, prewarm.
- [ ] **Step 3:** CK(`mtld3d-unix`, `mtld3d-core`), LX, KEY; `emit_corpus --digest` = base. Commit (below) before Step 4.
- [ ] **Step 4 (lock, early check, C3b-17), on Task 5's commit:** one job from `job6.sh`, main checkout at `main`
      (today's scripts suffice: they pass runner arguments through and the launcher appends the caller's
      `MTLD3D_CONFIG`), `$D` detached at the `c3b-rt` tip with `git -C $D rev-parse HEAD` = `git -C $S/wt-rt rev-parse
      HEAD` logged (Tasks 1-4 integrated beneath it), env `$B/c3/gates/env.sh`, CDHash logged, `J=$S/gates/early`:
      `make wine-arm64`; `sh dxmt/d3d9-check.sh` → `d3d9-check: ok`;
      `MTLD3D_CONFIG="log.dir=Z:$J/logs;shader.backend=air" sh dxmt/d3d9-check.sh` → `d3d9-check: ok` and
      `/usr/bin/grep -l 'shader backend: air, translator c298383e' $J/logs/*` non-empty (no AIR is built: the
      program is fixed-function); `DXMT_CONFIG=shader.backend=air sh dxmt/mtld3d-conformance.sh --keep-shader-cache
      $J/caches --only visual` → its verdict line as C3b-12's Expected, then from the lane `emit_corpus --kinds
      $J/caches/*.bin` → every programmable kind seen has `air_…` ≥ 1; prewarm on hardware: copy
      the kept `$J/caches/*-visual.bin` to `build/dxmt-tests/mtld3d_shaders.bin`, one launch of the x64 triangle
      (as d3d9-check's `launch` does it, `log.dir` and `shader.backend=air` as above), its log's prewarm summary shows
      `(<n> AIR)` with n ≥ 1 (only that line is the Expected: prewarming ~100 records may cost the timing rows), then
      `rm -f build/dxmt-tests/mtld3d_shaders.bin`;
      `$D` restored. A failure here is fixed in the lane (test first, a further commit) before review.

**Expected:** unit tests pass; the early job's lines as stated. Commit: `d3d9(unix): shader.backend=air builds each
programmable pair through the translator, MSL otherwise`.

### Task 6: AIR gate lanes (`$S/wt-mn` only)

**Files:** MacNeutron `dxmt/d3d9-check.sh`, `dxmt/mtld3d-conformance.sh`, `vm/dxmt/d3d9-gate.sh`, `vm/tests/d3d9.sh`.
No DXMTRS change: the runner already keeps caches, and `DXMT_CONFIG` carries the backend past it (C3b-12).

**Interfaces:** `D3D9_BACKEND=msl|air` (default `msl`) in `d3d9-check.sh`, `mtld3d-conformance.sh` and
`d3d9-gate.sh`, as C3b-12 spells out:
- `d3d9-check.sh` under `air`: `MTLD3D_CONFIG="${MTLD3D_CONFIG:+$MTLD3D_CONFIG;}log.dir=Z:$WORK/mtld3d-logs;shader.backend=air"`,
  a miss when no log under `$WORK/mtld3d-logs` holds `shader backend: air`; last line `d3d9-check: ok (air)`.
- `mtld3d-conformance.sh` under `air`: `DXMT_CONFIG=shader.backend=air`, `--keep-shader-cache
  $B/mtld3d-conformance/air-caches` (emptied first), then `emit_corpus --kinds` over it (cargo as the script runs the
  runner, `CARGO_TARGET_DIR=$B/d3d9-cargo/kinds`), printing `conformance: air kinds: …` and failing when a seen kind
  has no AIR record. In both lanes: the run fails when `$OUT` holds `^conformance: METAL VALIDATION` or
  `^conformance: GPU HANG`, whatever the triage (today's awk turns those into "passed", `:64-76`).
- `d3d9-gate.sh`: writes `$S/backend` (its `S=$W/share/d3d9`, beside `wow64`); under `air` adds rows `ok   air
  backend logged` (the info line in `$G/logs`) and `ok   air records x64`/`x86` (`emit_corpus --kinds` once per leg,
  `$G/caches/arm64x-*.bin` and `$G/caches/i686-arm64-*.bin`; the runner creates the folder, `run.rs:425-432`), and in both lanes a `conformance <x64|x86>: no
  validation error or hang` row from `conf-*.txt`; C3b-13's key row (host side, before the boot); last line `d3d9
  gate: ok (air)` under `air`. `D3D9_GATE_TRACES` re-check mode takes the lane from the caller's `D3D9_BACKEND`, reads
  `$G/caches` too, and skips the key row.
- `vm/tests/d3d9.sh` reads `/mnt/share/d3d9/backend`: under `air`, `MTLD3D_CONFIG="log.dir=Z:$O/logs;shader.backend=air"`
  for the `tri` rows, and `DXMT_CONFIG=shader.backend=air` plus `--keep-shader-cache $O/caches` for both conformance
  lanes.

- [ ] **Step 1:** the scripts; `sh -n` on each. Their runs are Task 5 Step 4's (the conformance arguments by hand) and
      Task 10's.

**Expected:** `sh -n` clean; `D3D9_BACKEND` unset leaves every script's command lines as today apart from the new
validation/hang check (diff read by the reviewer). Commit (`git add` the four paths only): MacNeutron `dxmt, vm: D3D9
gates run on either shader backend and prove the AIR lane ran AIR`.

### Task 7: depth parity (lane `pt`)

**Files:** `src/airconv/tests/dxso_parity.rs`, `src/d3d9/core/examples/emit_corpus.rs` (the depth families' recipes),
`tests/d3d9/dxso-corpus/{synthetic.bin,README.md,triaged.txt}`.

- [ ] **Step 1 (RED, minimal mutation):** with the harness reading depth and the corpus regenerated, two lowering
      mutations in the lane only, each reverted after: texdepth's `r5.r / r5.g` replaced by `r5.r` (the ps_1_4 path),
      and the SM2+ `oDepth` store writing `0.5` (the `ps_2_0`/`ps_3_0` path); each makes `synthetic.bin` report a
      `differ … depth`.
- [ ] **Step 2:** C3b-15's harness change; depth recipes and the three SM2+ `oDepth` programs; `synthetic.bin`
      regenerated (`--write-cache`), README counts;
      the two harness minors (stale-triage check skipped when `AIRCONV_DXSO_SELF=1`; a triaged `nodraw` counted in
      `triaged`).
- [ ] **Step 3:** run both files; CK(`airconv` tests only: fmt, clippy `--tests`), KEY (unchanged: `tests/` excluded);
      `emit_corpus --digest` = base.

**Expected:** `dxso-parity: synthetic.bin: <n>/<n> equal, 0 differ, 0 bind-differ, 0 air-refused, 0 msl-failed, 0
ff-skipped, 0 triaged` with `<n>` ≥ 159 (152 + four ps_1_4 depth recipes + three SM2+ `oDepth` ones) and depth
compared on those seven; a `depth-ulps synthetic.bin <max>` information line; `conformance-x64.bin` as C3a's (147/149
+ 2 triaged), now with depth compared where its recipes have it. A depth
difference is a lowering bug (Opus, test first, C3b-11's key rule) or triaged with its reason. Commit: `airconv: DXSO
parity reads depth back`.

### Task 8: the MSL inliner's bound (lane `msl`)

**Files:** `src/d3d9/core/src/dxso/emit.rs` (`expand_subroutine`, `:2375-2415`), `src/d3d9/core/src/dxso/emit/tests.rs`.

- [ ] **Step 1 (RED):** `emit::tests::call_depth_33_is_refused`, `…::a_doubling_call_chain_is_refused` (32 labels
      each calling the next twice, 2^31 expanded instructions, which today expands without bound: the test runs with a
      timeout guard, refusing in < 1 s), `…::call_depth_32_still_emits`,
      `…::five_calls_of_a_16k_instruction_subroutine_still_emit` (81920 expanded: past AIR's 65536, under the cap).
- [ ] **Step 2:** the bounds (C3b-16: depth 32 as `airconv/ir/src/dxso/lower/inline.rs:277-279`; `1 << 20` expanded
      DXSO instructions, `call`/`nop` counted as `lower/ops.rs:66-70` counts them), an emit error naming each.
- [ ] **Step 3:** CK(`mtld3d-core`), LX; `emit_corpus --digest` = base (`SHADER_EMITTER_VERSION` moves, the MSL does not).

**Expected:** tests pass, digest equal. Commit: `d3d9(core): the MSL emitter bounds subroutine expansion as the
translator does`.

### Task 9: e2e link attempt (lane `e2e`, C3b-14)

- [ ] **Step 1:** in the lane, host-only: `cargo build --offline --target x86_64-pc-windows-msvc -p mtld3d-tests --tests`
      with the recipe of `map-build.md` §5 (llvm-mingw's `lld-link`, `-lldmingw`, its `lib*.a`, `crt2.o`,
      `src/util/rust_pe_rt.c` for `_fltused`/`__chkstk`) through `--config` flags or one wrapper under `tools/`.
      Box: four hours of agent time or three distinct recipes, whichever first.
- [ ] **Step 2 (only if it linked; runs inside Task 10's Mac job, after its `make wine-arm64`, so wine.app knows
      `shader.backend`):** `cargo build -p mtld3d-e2e`; with `WINEPREFIX=$B/mtld3d-conformance/prefix` (configured and
      FEX-registered by that job's conformance rows), `mtld3d-e2e --wine <wine.app>/Contents/Resources/bin/wine --jobs 4
      --timeout 60 --no-fail-fast -- <binary>` over the functional suite (no `--ignored`), once with the default config
      and once with `MTLD3D_CONFIG=shader.backend=air`; box 30 minutes per backend, else `--filter shader` modules
      only; results into the ledger as information.

**Expected:** either a linked binary and (at Task 10) two result lines, committed wrapper (`tools: link mtld3d's e2e
programs with llvm-mingw`), or a ledgered "does not link: <last error>" and C3-17's parked state with the xwin ask.

### Task 10: gates, final review (owner)

- [ ] **Step 1:** every lane integrated (`git cherry` empty each), `Cargo.lock`/derive inventory regenerated if a merge
      touched them; RL; KEY = `c298383e…`; `sh $B/c3/xlate.sh $S/wt c3b` (scratch under `$B/c3`, its base unchanged) →
      `xlate: c3b ok (13 corpora), key c298383e…`; parity (Task 7's lines); `emit_corpus --digest` = base.
- [ ] **Step 2 (lock, Mac job, `$S/gates/mac.sh` from `job6.sh`; main checkout detached at `c3b-mn` for the whole job,
      trap restoring `main`; `$D` at the `c3b` tip):**
      `make wine-arm64` (CDHash); binary checks on wine.app's `mtld3d.so` (`nm -gU` = the six names + airconv's 14,
      C3b-9; `otool -L` = today + `libz.1` + `libxml2.2`; size; `/usr/bin/grep -c` key ≥ 1; `codesign --verify`);
      `sh dxmt/check.sh` x64 and ARM64EC (`dxmt-check: all passed`, its D3D9 row `ok   a D3D9 program draws through
      mtld3d`); `sh wine-arm64/check.sh` (exit 0; the atomic64typed row's flake rule: one rerun); `sh
      presenter/check.sh` (66 rows, exit 0); conformance MSL and `D3D9_BACKEND=air` (C3b-12's Expected, the air run's
      `conformance: air kinds:` line; per-subtest counts tabled against C3a's job3); `d3d9-check.sh` MSL and
      `D3D9_BACKEND=air` on wine.app (`d3d9-check: ok`, `d3d9-check: ok (air)`; fixed-function pairs stay MSL by
      design, so the air run is a smoke whose proof is only that the backend was logged), then the seeded prewarm run
      of Task 5 Step 4 (a kept `*-visual.bin` as `build/dxmt-tests/mtld3d_shaders.bin`, prewarm summary `(<n> AIR)`,
      n ≥ 1, file removed after); loopback `so=$(sh dxmt/mtld3d-build.sh --loopback)`, `MTLD3D_SO=$so sh
      dxmt/d3d9-check.sh` MSL and air; `sh dxmt/mtld3d-facade-test.sh` (`test result: ok`, the DXSO test and Task 3's
      `new_library_reports_a_bad_metallib` `ok`); `sh dxmt/wm-probe-check.sh` (11 `ok wm-probe g<n>`); Task 9 Step 2
      if it linked.
- [ ] **Step 3 (lock, VM job, `$S/gates/vm.sh` from `job4.sh` + `env-vm.sh`; main checkout detached at `c3b-mn`; `$D`
      at the same sha; signing env, `BUILDER_MAC`):** rebuild in order `sh vm/dxmt-native/build.sh`; `sh
      vm/image/guest-stages.sh dxmt d3d`; image sync + `build.sh wine` (wine.erofs newer than the job start); `sh
      vm/host/build.sh`. Then `sh dxmt/wm-probe-check.sh --native …` (11 ok); `sh vm/dxmt-native/build-tests.sh && sh
      vm/dxmt-native/gate.sh` (33); `sh vm/d3d/loopback-gate.sh` (33); `sh vm/dxmt/gate.sh` (47); `sh vm/d3d/gate.sh`
      (43); `sh vm/dxmt/d3d9-gate.sh`, `MN_WOW64=new sh vm/dxmt/d3d9-gate.sh`, and both again with `D3D9_BACKEND=air`
      (four `d3d9 gate: ok` lines, two with `(air)` and their `ok   air backend logged`/`ok   air records` rows, each
      with `ok   key guest=host`; after each, job4's greps of `translates_a_dxso_vs_and_ps_on_the_host` and Task 3's
      `new_library_reports_a_bad_metallib` in both unit traces); `sh vm/dxmt/present-gate.sh` and `… forward`
      (`present gate: all passed` ×2); `sh vm/tests/d3d-smoke.sh` and `MN_WOW64=new …` (`d3d-smoke: ok` ×2).
- [ ] **Step 4 (final review, with the results, C3b-17):** Opus review of `aa02c4b1..c3b` and `8d7db77..c3b-mn` given
      the code, this plan and the gate figures; one fix wave (Critical/Important, RED→GREEN each, each fix's task CK/LX,
      then RL and KEY once); scoped Opus re-review. Minors ledgered.
- [ ] **Step 5:** a gate failure or a fix-wave fix: root cause (systematic debugging), fix test-first in a `fix` lane,
      scoped Opus re-review, rerun the rows its files feed (a shipped-code fix in `src/d3d9/{unix,core}` or `src/airconv`
      reruns Step 2 and Step 3 in full, the guest mtld3d.so being built from them; a script-only fix its rows).
- [ ] **Step 6:** restore `$D` to `$(cat $B/wine-arm64-src/dxmt.applied)`; `git -C $M status -sb` = `## main`;
      ledgers: program ledger "C3b GATED, UNPUSHED …", step ledger final tips, figures, rulings, minors. STOP.

**Expected:** every line above; DXMTRS `c3b` and MacNeutron `c3b-mn` gated and unpushed.

---

## Push order (the parent's; nothing here runs them)

1. `$D` at `$(cat $B/wine-arm64-src/dxmt.applied)` (lock).
2. Privacy scan of `git -C $S/wt log -p aa02c4b1..c3b` and of `synthetic.bin`'s payloads; `git -C $D push --dry-run
   origin c3b:main`; push DXMTRS `c3b` to `main` (fast-forward from aa02c4b1).
3. On `c3b-mn`: `dxmt/pins` `DXMT_COMMIT=<pushed sha>` and spec §5's C3 entry "C3b done …" (as 8d7db77), commit.
4. The pushed pin's development build (lock): `$D` detached at the pushed sha, `make wine-arm64`, `sh dxmt/check.sh` x64
   → `dxmt-check: all passed`, `git -C $D ls-remote origin main` names it; `$D` restored.
5. Main checkout `## main`, `git -C $M merge --ff-only c3b-mn` (or a merge commit if `main` moved), privacy scan of
   `8d7db77..main`, push `main`. No tag, no release (C3b-18).

## Open risks

- **Pair rule coverage (C3b-2):** no-colour sibling pipelines, depth-only passes, a PS variant that depends on the VS:
  Task 5's tests and the AIR lanes are the check; the Opus review reads every `ShaderRef` construction.
- **Link of LLVM into mtld3d.so (C3b-8):** meson's LLVM is ThinLTO bitcode (`llvm23-arm64`); winemetal links it in the
  same meson run, so the same link line works for a second cdylib; first proven at Task 5's early job.
- **AIR conformance:** Wine's visual tests are mostly fixed-function pairs (MSL either way); the AIR lane covers the
  programmable ones only (~200 shaders, all six kinds), so its figures may equal MSL's without proving much beyond
  them; the kinds row proves AIR ran, the parity corpus stays the main AIR oracle. The AIR lane runs with
  `shaderCache.enable=true` (keeping caches implies it), the MSL lane with it off: a difference that traces to the
  cache alone is ledgered, not an AIR finding. A kind drawn in Wine's tests only beside a fixed-function stage would
  read `air_… = 0` and fail the kinds row; C3a's corpus has every kind in programmable recipes.
- **Encoder-thread lowering under `air` (C3b-2):** each new stage key lowers on the encoder thread and again in the
  worker's `translate`; unmeasured (deferred minors). Not on the `msl` path.
- **Unknown kinds in an older build (C3b-5):** a player who runs an older MacNeutron after a C3b one loses the AIR records
  of that game; no player runs `air` before C3d.

## Review record (2026-11-09)

Reviews 1 (translator and correctness: 0 Critical, 6 Important, 7 Minor), 2 (runtime, ABI and cache: 2 Critical, 7
Important, 9 Minor) and 3 (execution and gates: 2 Critical, 10 Important, 9 Minor), each checked against DXMTRS
aa02c4b1 and MacNeutron 8d7db77 before folding. Every Critical and Important held and is applied; none was ruled
against.

**Folded.**
- AIR proof (R2 C1, R3 C2, R2 I6, R3 I1, R3 I3): `d3d9_triangle.cpp:66` is fixed-function, so d3d9-check stays a
  smoke in the AIR lane. The proof is conformance with `--keep-shader-cache` and Task 4's `emit_corpus --kinds` (every
  programmable kind seen has ≥ 1 AIR record), on the Mac and in the VM (guest caches on the share, counted on the
  host), plus the Unix info line `shader backend: …, translator …` (d3d9-check and VM `tri` logs; the runner's
  gating runs log nothing, `RUST_LOG=off`), plus a seeded prewarm run for AIR prewarm on hardware. Of R2 C1's and R3
  C2's two fixes, the kept-cache one is taken: it needs no new test program. The runner `--config` flag is dropped
  for `DXMT_CONFIG` (a documented weaker source the runner leaves alone), which removes Task 6's DXMTRS half.
- Cache dedupe (R2 C2): AIR records last-wins in `push_record`; `needs_regeneration()` keeps its signature, prewarm
  compares keys; test `a_later_air_record_beats_its_earlier_twin_across_compaction`.
- C3b-3 (R1 I2, R2 I1, R1 I3): the synchronous fallback is deleted (it sat on a path only `rt0_drop` draws take, and
  had three gaps); library and pipeline AIR failures both feed `air_failed`. R1 I2 and R2 I1 offered "delete" or
  "repair"; delete is the smaller and covers both async settings.
- Carrier and keys (R1 I6, R1 M2, R2 I2, R2 I3, R2 M3): the backend rides in core's `ShaderRef` (Task 1's
  `ShaderBackend`, no unix enum) and in the existing index keys (no parallel maps).
- Key (R1 I5, R2 I7, R3 I10, R3 M7): parsed per call from `black_box(env!(…))` (no static; the literal survives);
  the key is an argument of `LibraryJob` and `shader_prewarm::spawn`.
- C3b-9 (R2 I5): ld64's "cannot be used together" message confirmed in Xcode's `ld`; the listed difference (six +
  14) is the ruling, `unexported.txt` gone.
- C3b-16 (R1 I1): depth 32 and a `1 << 20` expansion cap; citations corrected to `lower.rs:665, 667`,
  `ops.rs:66-70`, `inline.rs:277-279`; the 16k×5 test keeps C3b-19 true.
- Ownership (R2 I4, R3 I4): `from_programmable`, `CacheEntry::new`, `needs_regeneration()` and `refresh_msl` keep
  their signatures, so `compile.rs` needs no Task 4 edit; Task 4 owns the two forced lines
  (`shader_prewarm.rs:305`, `e2e/shaders.rs:521`) and lints `mtld3d-tests`.
- Execution (R3 C1, R3 I2, R3 I5, R3 I6, R3 I7, R3 I8, R3 I9): Mac job detached at `c3b-mn`; Task 5 Step 4 on Task
  5's commit; the facade test on the build-tree winemetal with stand-ins; explicit-path commits in `$S/wt-mn`;
  `METAL VALIDATION`/`GPU HANG` fail the conformance script and get VM rows; Task 9's run moved into Task 10's Mac
  job, boxed; gates before the final review (R18's reason, a key move, is excluded by C3b-11), every gate-time fix
  re-reviewed.
- Depth (R1 I4, R1 M5): three SM2+ `oDepth` programs and a second mutation RED; blit option, `HAS_DEPTH`,
  `depth-ulps`, root-workspace feature rule.
- Minors applied: R1 M1, M2, M5, M7 (the Rules check, required by `W CLAUDE.md`); R2 M1-M7; R3 M1-M7, M8 (moot: no
  count is read from the shared triangle cache; the seeded file is removed after), M9 (no GPU lane work beside a lock
  job).

**Deferred minors.**
- R1 M3 / R2 M9: the encoder thread lowers each new stage key and the worker lowers it again; prewarmed AIR records
  do not seed `air_ok_*`. Measure under `air` at C3d (when `air` becomes a default candidate), seed then if it costs.
- R1 M4 / R2 M8: `pair_backend` probes two maps per draw under `air`; memoizing it beside `LibraryMemo` must also
  forget on `air_failed` insertions. `msl` pays one compare. C3d, with the measurement above.
- R1 M6: an AIR VS's invariance across two pipelines (depth `EQUAL` second pass): sound by construction (no
  contract/reassoc flags, `W src/airconv/src/dxbc/converter.rs:404-413`); a Task 3 test if C3d's lane shows z-fighting.
- R3 M9, second half: `$S/lane.sh`'s comment names `build/c3/wt-<lane>` while it cuts `build/c3b/wt-<lane>`
  (outside this plan's files; owner's fix).
