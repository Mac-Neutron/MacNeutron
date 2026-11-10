# DXMTRS C3d: D3D9's shader backend defaults to AIR — Implementation Plan

> **For agentic workers:** execution is **native** (`superpowers:executing-plans`): the owner implements Tasks 1-3 inline,
> one fresh Opus reviewer checks the branch before the lock jobs (C3c R4), then the owner runs Task 4's lock jobs. Steps
> use checkbox (`- [ ]`) syntax for tracking.

**Go-ahead.** The maintainer's "Start C3" (chat, 2026-11-07) covers all of C3; the standing DXMTRS authorization
self-approves this plan after three adversarial Opus reviews (record at the end). No ruling here amends a C3 ruling.

**Goal:** `shader.backend` defaults to `air` (C3-12); `msl` stays the escape (`mtld3d.conf` or `MTLD3D_CONFIG`); every
D3D9 gate runs a default lane (no `shader.backend` set, so AIR) and an explicit MSL lane, each proving which backend it
ran; conformance parity is checked per site on every leg; the cost the flip moves onto every launch is measured or
parked by name. The MSL deletion, the benches and the release stay parked (the C3 plan's C3d outline, items 3-4).

**Architecture.** One default changes (`ShaderBackend`'s `#[default]`, `Config::default()`), resolved in one place (the
PE side's `parse_sources`; the Unix side gets the value over the wire, review 1). Nothing else in the runtime changes:
under `air` a pair is AIR when both stages lower (C3c-6), else MSL. A build without a translator key still draws AIR,
uncached (`W src/d3d9/unix/src/encoder/compile.rs:618-639`, test `a_keyless_air_ff_build_persists_no_record`); shipped
builds are always keyed. MacNeutron's gate scripts stop relying on the default for their MSL lane; a comparator turns
the two conformance lanes into a per-site check.

**Tech Stack:** Rust 1.99.0 (workspace lints, `docs/CONVENTIONS.md`), POSIX sh/awk, Metal.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5 (C3). **C3 plan:**
`docs/superpowers/plans/2026-11-07-dxmtrs-c3-dxso-translator.md` (C3-12, "C3d outline"). **C3b plan** (deferred items
`:750-755`), **C3c plan** `docs/superpowers/plans/2026-11-09-dxmtrs-c3c-fixed-function.md` (Global Constraints, Task 9
rows, Push order, open risks `:618-625`). **Ledgers:** `.superpowers/sdd/dxmtrs-program.md`, C3c's
`.superpowers/sdd/2026-11-09-dxmtrs-c3c-fixed-function/progress.md` (figures, present-held record), this step's
`.superpowers/sdd/2026-11-10-dxmtrs-c3d-flip/progress.md`. `W` = DXMTRS `build/c3d/wt` (branch `c3d` from a93f9033),
`M` = MacNeutron `build/c3d/wt-mn` (branch `c3d-mn` from 23c4141); file:line refs are at those commits.

**Parity today (C3c's final gates).** Failures per suite, MSL / AIR; AIR never worse in the totals:

| Leg | device | visual | d3d9ex | stateblock |
|---|---|---|---|---|
| Mac arm64x (`build/c3c/gates/mac/conf-{msl,air}.out`) | 740 / 739 | 31 / 30 | 74 / 74 | 0 / 0 |
| VM x64 (`build/c3c/gates/vm/d3d9{,air}.out`, summary lines only) | 736 / 736 | 31 / 30 | 72 / 72 | 0 / 0 |
| VM x86 (summary lines only) | 739 crash 1 / 739 crash 1 | 31 / 30 | 72 / 72 | 0 / 0 |

Per site, only the arm64x leg has both lanes' outputs (`ok` by hand: device 4471 1/0, 14544/14946/15104 1/1; visual
15686 3/3, 16481 1/0). The VM legs' per-site comparison happens first in Task 4. Corpora (the two in the tree):
synthetic 521/521 (362 FF), conformance-x64 847/896 with 49 triaged, 0 untriaged differ.

---

## Rulings

- **C3d-1 (scope):** the flip (Task 1), honest gate lanes (Task 2), the comparator (Task 3), the review and one full
  gate run with the upgrade row (Task 4). Parked: the benches (with the named costs below), then the MSL deletion and
  the release. Cost if wrong: none to players until a release.
- **C3d-2 (legacy caches, no migration):** an existing `mtld3d_shaders.bin` keeps its MSL records and recipes. Under the
  new default, every launch until the MSL deletion prewarms the legacy MSL set and, from the second flipped launch,
  the AIR set too, ahead of the first frame (`W encoder.rs:9542-9556`); the file keeps both (dedup and compaction keep
  both kinds). The first flipped session builds AIR pairs on first use; a draw whose build is in flight waits unless
  its targets are rebuilt every frame (`W config.rs:140-145`), so it can stall rather than hide. Task 4 measures this
  path (the upgrade row). Follow-ups named, decided by the benches: skip MSL pipeline recipes at prewarm under `air`
  with a key (MSL pairs are fallback-only there), or derive AIR twins from retained DXSO. Cost if wrong: longer loads
  and a first-session hitch for players with warm MSL caches until the deletion.
- **C3d-3 (discriminants):** `Msl = 0`, `Air = 1` stay (index keys, `ShaderRef`, library-key tags); only `#[default]`
  moves. Cost if wrong: none.
- **C3d-4 (unknown values):** `shader.backend = <other>` warns and keeps the value already in effect (`W
  config.rs:777-785`): `air` unless an earlier line or source set `msl`. Cost if wrong: a typo runs AIR.
- **C3d-5 (legs and parity):** legs are Mac arm64x, and in the VM x64 and x86 under old and new WoW64 (four VM pairs:
  new WoW64's x86 leg is a different 32-bit path). Parity per suite: no site fails more often under AIR than under
  MSL; no suite crashes more or executes fewer tests (beyond 0.1%) under AIR; each file's suite totals equal Wine's
  baseline plus its site deltas (else it is truncated); sites the runner marks `flaky` are information; a site in `M
  dxmt/mtld3d-triaged.txt` (key: leg/subtest + site) that is worse under AIR is information and ledgered (every entry
  there is environmental: displays, cursor clip). A suite that crashes in both lanes is reported `partial` and ledgered
  (i686 device today). Cost if wrong: a game-visible AIR difference the suites never hit.
- **C3d-6 (lanes and their proof):** `D3D9_BACKEND` is `air` (the default lane: sets no `shader.backend`, keeps
  `log.dir`, the scratch EXE copies and `--keep-shader-cache`) or `msl` (the same, plus `shader.backend=msl` wherever the
  AIR lane set `air` before, in `MTLD3D_CONFIG` and `DXMT_CONFIG`). Every script prints a lane stamp first (`<script>:
  lane default` / `lane msl (shader.backend=msl)`) and writes it into its kept outputs. Proof per lane: the default
  lane's kept caches show AIR kinds > 0 (FF included) and its triangle log shows `shader backend: air, translator
  <hex>`; the MSL lane's kept caches show every `air_*` kind = 0 and MSL kinds > 0 (FF included), and its triangle log
  `shader backend: msl`. Conformance logs are silenced by the runner (`RUST_LOG=off`, no `log.dir`): its proof is the
  kinds rows only. Both conformance lanes keep their caches, so both run `shaderCache.enable=true` (`W
  tests/d3d9/conformance/src/run.rs:848-852`). Verdict lines: the default lane prints exactly `d3d9-check: ok`, the
  MSL lane `d3d9-check: ok (msl)`. Cost if wrong: an MSL lane secretly AIR; the stamps, kinds rows and the comparator's
  stamp check catch it.
- **C3d-7 (the kinds step outside gate jobs):** the kinds rows need `emit_corpus` built from `$D`, which sits at
  `dxmt.applied` (before C3) outside gate jobs. `d3d9-check.sh` runs the kinds rows only when `D3D9_KINDS=1`; gate jobs
  set it on both lanes; `dxmt/check.sh` calls `D3D9_BACKEND=air sh dxmt/d3d9-check.sh` without it. Cost if wrong: one
  lane proof missing from `dxmt/check.sh`'s single D3D9 row (the gate jobs carry it).
- **C3d-8 (the flip's cost on every launch):** under `msl` none of these ran; under the default they run on every D3D9
  launch. Named, measured where cheap, the rest parked as release-blocking bench items: (a) the encoder thread lowers
  each new stage key and the worker lowers it again; prewarmed AIR records do not seed `PairRule` (`W encoder.rs:1711`,
  `libraries.rs:604-622,650-658`), on the VM legs under FEX; (b) each new FF key's `ff_build` + parse on the encoder
  thread (`libraries.rs:661-708`); (c) the per-draw `pair_backend` probe (`libraries.rs:556-583`); (d) mixed-backend
  z-fighting (C3c R9: no conformance site shows it; game benches are the check); (e) the upgrade path (C3d-2). Task 4
  records the cheap numbers. Cost if wrong: a slower first launch the benches catch before any release.
- **C3d-9 (present-held, evidence only):** the C3c-open row (forwarded D3D12, `vm/tests/d3d-smoke.sh`, which loads no
  D3D9 at all) is measured once more with Task 4's VM job as release evidence; the release job re-measures six runs on
  the release build (C3c's ruling). Procedure in Task 4 Step 4. Cost if wrong: none to the flip.
- **C3d-10 (no release):** C3-9 holds; nothing is tagged or released; the parent pushes after reading the gate logs.

## Global Constraints

C3c's Global Constraints apply, with `$S` = `$M/build/c3d`, branches `c3d`/`c3d-mn`, worktrees `$S/wt`/`$S/wt-mn`: who
pushes, hold, usage stop rule, names, the lock (one job per build/gate/`$D` checkout; `$D` restored to `dxmt.applied`;
main `## main` after every job; gate scripts run from the main checkout detached at `c3d-mn`, which has `build/`), the
builder, signing env, Rust (UT/CK/LX/RL, PAR, DIG as defined there), conventions, `run <n> env VAR=x …` never `VAR=x
run …`. No lanes (one owner). KEY stays `051df2a0ccf282556ac496a88725beb1f83df7f7b11024b155fde1760d730f1c` (no input of
`translator_key` is edited); DIG stays `msl-digest 730b0a5c1332e44f 131 shaders`, fingerprint `32d02bac6250476c`
(`config.rs` and `mtld3d.conf` are not fingerprint inputs). Long jobs under `caffeinate -d -i -u`. VM boots use
`MN_STATE=<fresh file>`, never `state/default.img`, deleted (with `.tpm`) after.

## Review Focus

1. **Tests that relied on the implicit default** (`Config::default()` expecting MSL libraries) now get AIR: each is made
   explicit, never re-asserted (Task 1 Step 4).
2. **An MSL lane that is secretly AIR:** a missing or overridden `shader.backend=msl` in `DXMT_CONFIG` (conformance) or
   `MTLD3D_CONFIG` (triangle); the MSL kinds row, the lane stamp and Task 2's mutation RED pin it.
3. **A comparator that passes on less evidence:** empty, truncated or early-returned files, a suite missing in both, an
   AIR suite crashing earlier; Task 3's fixtures cover each.
4. **The upgrade path:** a warm MSL-only cache launched twice under the default; Task 4's upgrade row.
5. **A player's `shader.backend = msl`** (file or env, either order) still gives MSL; Task 1's tests.

---

## Execution

### Task 1: the default flips to `air` (DXMTRS, `c3d`)

**Files:** `W src/d3d9/core/src/config.rs:149,347-351,415`; `W src/d3d9/core/src/config/tests.rs:667-706`; `W
src/d3d9/core/src/encoder_config/tests.rs:35`; `W mtld3d.conf:232-241`; docs: `W docs/d3d9/STATUS.md:10`, `W
docs/d3d9/ARCHITECTURE.md:92` and `:166` (the shader path), `W docs/d3d9/TESTING.md:254-261,432-436`.

**Interfaces:** Produces `ShaderBackend::default() == Air`, `Config::default().shader_backend == Air`; log lines unchanged
in form (`config: shader.backend = air`, `config.rs:586`; `shader backend: air, translator <8 hex|none>`, `W
src/d3d9/unix/src/encoder.rs:1713-1719`).

- [ ] **Step 1: tests first.** `shader_backend_defaults_to_msl` → `shader_backend_defaults_to_air` (`parse(None, "",
  None)` and `ShaderBackend::default()` are `Air`); `shader_backend_bad_value_warns_and_keeps_msl` →
  `shader_backend_bad_value_warns_and_keeps_the_value_in_effect` (`"shader.backend = metal\n"` → `Air`;
  `"shader.backend = msl\nshader.backend = metal\n"` → `Msl`); new `shader_backend_msl_in_the_file_is_kept`
  (`"shader.backend = msl\n"` → `Msl`), new `mtld3d_config_env_msl_overrides_a_file_saying_air` (`parse(None,
  "shader.backend = air\n", Some("shader.backend=msl"))` → `Msl`), new `dxmt_config_msl_reaches_d3d9` (the
  `DXMT_CONFIG` source the conformance lane uses, via the `sources(..)` helper at `config/tests.rs:708`, gives `Msl`).
  `encoder_config/tests.rs:35`'s non-default becomes `ShaderBackend::Msl`.
- [ ] **Step 2:** `cargo test --offline -p mtld3d-core config` → FAIL in the default and bad-value tests and in
  `resolved_configuration_preserves_nondefaults`.
- [ ] **Step 3:** `#[default]` to `Air` (discriminants unchanged), `config.rs:415` to `Air`, doc `:149` "Default:
  [`ShaderBackend::Air`]". `mtld3d.conf`: "an unknown value warns and is ignored", "Default: air", `# shader.backend =
  air`, and "msl compiles generated Metal Shading Language: the escape if a game draws wrongly under air (also
  `MTLD3D_CONFIG=shader.backend=msl`)". Docs: one short paragraph each: D3D9 shaders go through the translator to AIR
  by default, `shader.backend=msl` is the escape, MSL output unchanged until its deletion.
- [ ] **Step 4:** UT for `mtld3d-core`, `mtld3d-mtl`, `mtld3d-unix`. A test failing because it relied on the default:
  set `shader_backend: ShaderBackend::Msl` in that test (it tests MSL behaviour) and ledger it. Any other failure stops
  the task (systematic-debugging).
- [ ] **Step 5:** CK (`mtld3d-core`, `mtld3d-unix`), LX, RL; KEY = `051df2a0…`; DIG and fingerprint as Global Constraints.
- [ ] **Step 6: commit** `d3d9(core): shader.backend defaults to air; msl stays the escape`.

### Task 2: honest gate lanes (MacNeutron, `c3d-mn`)

**Files:** `M dxmt/d3d9-check.sh:18-42,100-112`, `M dxmt/mtld3d-conformance.sh:10,19,61-105`, `M vm/dxmt/d3d9-gate.sh:10,
19-41,76,102-164`, `M vm/tests/d3d9.sh:13,20-27,73-75`, `M dxmt/check.sh:1476-1477`.

**Interfaces:** Consumes Task 1's log lines. Produces (C3d-6, C3d-7): `D3D9_BACKEND=${D3D9_BACKEND:-air}`; lane stamps;
verdict lines; `D3D9_KINDS=1` enabling the kinds rows; kept outputs: Mac conformance `$B/mtld3d-conformance/run-<lane>.txt`
(stamp on line 1), VM conformance `$B/vm-work/d3d9-conf/conf-<x64|x86>-<old|new>-<lane>.txt` (outside the traces
folder each run wipes); the MSL kinds rows (`no-msl-records`, `air-records-under-msl` as failure names).

- [ ] **Step 1: plumbing** per C3d-6/C3d-7 in the four scripts; both lanes keep `log.dir`, scratch copies,
  `--keep-shader-cache`; in the guest those are unconditional and only the `shader.backend=msl` entries depend on the
  lane; the MSL kinds row mirrors `mtld3d-conformance.sh:94-105` and `d3d9-gate.sh:148-164` (all `air_*` = 0; `vs`,
  `ps`, `ffvs`, `ffps` > 0); `dxmt/check.sh:1477` calls `D3D9_BACKEND=air` explicitly and still expects `d3d9-check: ok`.
  `sh -n` each; the `VAR=x run` guard prints nothing. Commit `dxmt, vm: the D3D9 gates' default lane is the build's
  default (air); msl is explicit and proven`.
- [ ] **Step 2: RED (lock job, main checkout detached at `c3d-mn`, installed wine.app = C3c's pushed pin, CDHash and
  `DXMT/translator` = `051df2a0…` logged):** `D3D9_KINDS=1 sh dxmt/d3d9-check.sh` → fails exactly `air-log` and
  `air-kinds` (C3c defaults to msl); `D3D9_KINDS=1 D3D9_BACKEND=msl sh dxmt/d3d9-check.sh` → `d3d9-check: ok (msl)`.
  Mutation: the msl lane with its `shader.backend=msl` entry deleted, run against a wine.app whose default is air (Task
  4's build, so this mutation runs in Task 4 Step 2) → fails `msl backend logged` and `air-records-under-msl`; reverted.

### Task 3: per-site parity comparator (MacNeutron, `c3d-mn`)

**Files:** create `M dxmt/d3d9-conf-compare.sh`, `M dxmt/tests/d3d9-conf-compare-test.sh`, fixtures in `M
dxmt/tests/d3d9-conf-compare/` (cut from `build/c3c/gates/mac/conf-{msl,air}.out` and `build/vm-work/share/traces/d3d9/
conf-x86.txt`; no path or user name in them).

**Interfaces:** `sh dxmt/d3d9-conf-compare.sh <msl> <default> [<triaged>]` (triaged default `dxmt/mtld3d-triaged.txt`)
→ exit 0 with `conf-compare: ok (<legs> legs, <suites> suites, <sites> sites, <info> info)`, else exit 1 with one line
per finding: `stamp <file>` (line 1 is not `lane msl` / `lane default`, or both paths or contents are identical),
`missing <leg>/<suite>`, `truncated <leg>/<suite>` (totals check fails, or no closing `conformance:` line), `air-worse
<leg>/<suite> <site> <msl> -> <air>`, `air-crash <leg>/<suite>`, `air-executed <leg>/<suite> <msl> -> <air>` (> 0.1%
fewer), `air-skipped <leg>/<suite> <site>` (SKIPPED under AIR, measured under MSL); info lines (not failures): `info
flaky <site>`, `info triaged <site>`, `partial <leg>/<suite>` (crashed in both). Each leg must have the four suites
(stateblock, visual, device, d3d9ex).

**Input model** (`W tests/d3d9/conformance/src/diff.rs:66-125`): suite lines `<leg>/<suite>  baseline(failed=B crash=C)
current(failed=F crash=K) executed=E <status>`; site lines match `^  [a-z0-9_]+\.c:[0-9]+  ` with `<base> -> <cur>
<status…>` or arrow-less `SKIPPED …` / `N  untriaged …`; `  crash  0 -> 1 …` is not a site; `  note: …` and `  [leg/suite]
output cut short: …` (printed before their suite line) are skipped; script lines `conformance: …` are skipped except as
the closing marker. A site absent from a file has `cur = base` there (the runner prints every changed site).

- [ ] **Step 1: test first.** `d3d9-conf-compare-test.sh` asserts, each on fixtures: equal → `ok`; AIR better at a site
  → `ok`; one site `0 -> 1` under AIR only → `air-worse`; same site flaky in either → `ok` + `info flaky`; triaged for
  this leg → `ok` + `info triaged`; triaged only for another leg → `air-worse`; crash 0 → 1 → `air-crash`; executed 1%
  lower → `air-executed`; a file cut after a suite line → `truncated`; empty file → `missing`; both lack d3d9ex →
  `missing`; SKIPPED under AIR → `air-skipped`; crashed in both → `ok` + `partial`; swapped stamps or the same file twice
  → `stamp`; the real arm64x pair (stamped copies of C3c's outputs) → `ok`. Prints `d3d9-conf-compare-test: ok`.
- [ ] **Step 2:** run → FAIL (no comparator). **Step 3:** the comparator (awk; `/usr/bin/grep` for scans). **Step 4:**
  test → `d3d9-conf-compare-test: ok`. **Step 5: commit** `dxmt: a per-site comparison of the D3D9 conformance lanes`.

### Task 4: review, gates on the flip, the upgrade row (owner)

- [ ] **Step 1:** RL on `c3d`; KEY; DIG; PAR (synthetic 521/521, conformance-x64 847/896, 49 triaged, unchanged). Final
  Opus review of `a93f9033..c3d` and `23c4141..c3d-mn` with this plan; one fix wave (Critical/Important, RED→GREEN).
- [ ] **Step 2 (lock, Mac job `$S/gates/mac.sh`, written fresh from C3c's with every lane's command spelled out):**
  `make wine-arm64` (CDHash, key literal = KEY); `dxmt/check.sh` x64 and ARM64EC (the nvapi display row needs two
  displays: with one, its FAIL is ledgered, as C3c's); `wine-arm64/check.sh`; `presenter/check.sh`;
  `sh dxmt/tests/d3d9-conf-compare-test.sh`; `run conf-msl env D3D9_BACKEND=msl sh dxmt/mtld3d-conformance.sh`, `run
  conf-default sh dxmt/mtld3d-conformance.sh` (verdict lines, kinds rows); `sh dxmt/d3d9-conf-compare.sh
  $B/mtld3d-conformance/run-msl.txt $B/mtld3d-conformance/run-default.txt` → `ok`; `run check-msl env D3D9_KINDS=1
  D3D9_BACKEND=msl sh dxmt/d3d9-check.sh` (`ok (msl)`), `run check-default env D3D9_KINDS=1 sh dxmt/d3d9-check.sh`
  (`ok`); Task 2's mutation RED; seeded prewarm (C3c's AIR seed, `(<n> AIR)`); **the upgrade row** (C3d-2, C3d-8 e):
  the msl lane's kept triangle cache (MSL-only) beside a copy of the x64 triangle, launched twice under the default
  and once under msl on the same seed; record each launch's `shader_cache: pre-warmed … startup` line, `(<n> AIR)` (0
  then > 0), the cache size and draws ok; restore; loopback default and msl; facade; wm-probe. Grep each `*-msl.out`
  for its lane stamp.
- [ ] **Step 3 (lock, VM job `$S/gates/vm.sh`):** C3c's rebuild order and rows; `d3d9-gate.sh` old/new WoW64 × default/msl
  (four `d3d9 gate: ok`, msl ones `(msl)`; key guest=host; each lane's kinds and log rows); the comparator on the four
  VM pairs (x64/x86 × old/new) → `ok` (partial lines ledgered); the job's summary grep includes `msl backend logged` and
  the MSL kinds row.
- [ ] **Step 4 (same VM job, C3d-9):** after one discarded run, six `vm/tests/d3d-smoke.sh` runs, three old WoW64 then
  three new, each with a new `MN_STATE=$B/vm-work/state/c3d-<n>.img` (its `.tpm` deleted before and after), the
  `fresh.sh` probe line recorded, `cp $B/vm-work/d3d-smoke-forward.log $S/gates/smoke/forward-<n>.log`; count = `awk
  '/slow present: ctx 1 frame/ { if ($7 >= 5 && $7 <= 72) n++ } END { print n+0 }'`; a void run (no `forward:` lines) is
  rerun at most twice, then ledgered; 0-6 normal, 7-15 "between" (not hold), ≥ 16 hold. The smoke's own
  `present-held-a-vcpu` verdict is information; every other smoke row must be ok. These six replace C3c's smoke rows.
- [ ] **Step 5:** `$D` restored; main `## main`; ledgers: program "C3d flip GATED, UNPUSHED …", step ledger (figures,
  comparator lines, upgrade-row numbers, the six counts, rulings, minors), memory's DXMTRS line. STOP for the Push order.

**Expected:** every C3c figure held or bettered on the default lane; the msl lane equal to C3c's MSL figures; comparator
`ok` on five legs; upgrade row numbers recorded; six present-held counts recorded.

---

## Push order (the parent's)

1. `$D` at `dxmt.applied` (lock). Privacy scan of `a93f9033..c3d`; dry run; push DXMTRS `c3d` to `main`.
2. On `c3d-mn`: `dxmt/pins` `DXMT_COMMIT=<pushed sha>`; spec §5's C3 entry gains "C3d's flip done …" (default `air`,
   comparator legs, upgrade-row figures, present-held counts; benches, MSL deletion, release parked); commit.
3. Pushed-pin build (lock, `$D` at the pushed sha): `make wine-arm64`, `sh dxmt/check.sh` x64.
4. `git -C $M merge --ff-only c3d-mn`, privacy scan, push `main`. No tag, no release (C3d-10).

## Parked for the maintainer (release-blocking)

- The D3D9 milestone benches (spec §8; titles the maintainer picks; a 32-bit title in the VM) and C3's D3D10-12 benches
  (SMITE 2, Palworld, a D3D11 title) for the key move, each run once. D3D9 benches report C3d-8's (a)-(e): launch to
  first present with a warm AIR cache and with a warm MSL cache (upgrade), encoder-thread lowering time, z-fighting.
- After the benches: the MSL deletion (C3 outline item 4), with C3d-2's follow-up if the upgrade numbers ask for it.
- The release: also waits on six clean present-held runs of the release build (C3c's ruling).

## Open risks

- **Upgrade cost (C3d-2):** the recurring double prewarm until the deletion; Task 4 sizes it on the triangle only.
- **Encoder-thread lowering under FEX (C3d-8 a):** unmeasured until the benches.
- **Present-held:** unexplained, not D3D9 (the smoke loads none); six runs may miss a mode that clusters by session.

## Review record (2026-11-10)

Three adversarial Opus reviews against a93f9033/23c4141: `build/c3d/review-1-runtime.md` (0 Critical, 3 Important, 5
Minor), `review-2-gates.md` (2 Critical, 7 Important, 8 Minor), `review-3-execution.md` (0 Critical, 8 Important, 11
Minor). Every Critical and Important is folded; none ruled against:
- R1 I1 / R3 I1 (keyless builds are not MSL): Architecture corrected, the false test dropped. R1 I2 / R3 I8 / R3 M10
  (recurring upgrade cost, no gate): C3d-2 reworded, Task 4's upgrade row. R1 I3 (deferred measurements dropped): C3d-8.
- R2 C1 / R3 I3 (no MSL proof in conformance): both lanes keep caches; MSL kinds rows; stamps (C3d-6). R2 C2 / R3 M2
  (blind copy runs "msl" as AIR): lane stamps, comparator stamp check, Task 4's spelled-out commands.
- R2 I1 (default lane loses its plumbing): C3d-6. R2 I2 / R3 I4 (inputs not kept): Task 2's kept paths. R2 I3
  (comparator passes less evidence): totals, executed, partial, closing line. R2 I4 / R3 I5 (flaky sites): info. R2 I5
  (line types): the input model. R2 I6 / R3 I7 (RED unrunnable): Task 2 Step 2 reordered, run from the main checkout,
  both failing rows named, mutation RED. R2 I7 / R3 I2 (`dxmt/check.sh` row, `$D` dependency): C3d-7.
- R3 I6 (present-held procedure): Task 4 Step 4, C3d-9 (evidence only; the release re-measures).
Minors folded where they changed a line: leg-keyed triage (R2 M1), suite floor (R2 M2), the self-test in the Mac job
(R2 M3), `vm.sh` summary grep (R2 M5), the smoke loads no D3D9 (R2 M6), VM legs' first comparison (R2 M7), msl lane
from scratch copies (R2 M8), `encoder_config/tests.rs:35` (R3 M3), doc refs (R3 M4), four VM pairs (R3 M6), "every
corpus" = the two in the tree (R3 M7), review before the lock jobs (R3 M9), template wording (R3 M11), the `DXMT_CONFIG`
source test (R1 M1). **Deferred minors** (step ledger): R1 M2 (the escape's reach on the VM path, template note), R1 M3
(an AIR failure after lowering drops a frame each launch; no row counts it), R1 M5 (old-WoW64 per-key memo), R2 M4
(`D3D9_GATE_TRACES` stamp check), R3 M1 (no Task 0 baseline: C3c's final figures stand in), R3 M8 (Makefile placement:
the self-test runs in the Mac job instead).
