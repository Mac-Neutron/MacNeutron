#!/bin/sh
# Direct3D 9 in the VM (D3D9 M2b gate), from the Mac: mtld3d's Linux unit tests in the guest, the facade's on the guest
# winemetal.so; d3d9_triangle for x86-64 and i386 in a new prefix and in one without mtld3d.dll's markers that
# run-game.sh's update then fixes; and Wine's d3d9 tests through mtld3d's runner, the x64 lane held to the macOS
# product's figures at most (D3D9 spec §5.1's loopback run: stateblock 0, visual 31, device 739, d3d9ex 74, no crash,
# no untriaged new site) and the i386 lane to its baseline's failures and crashes at most, no untriaged new site.
# Builds the Linux test binaries and the runner (DXMTRS's tools/linux-cargo.sh); needs the image with mtld3d
# (guest-stages.sh dxmt, image/build.sh wine), the VM host (host/build.sh), make build/dxmt-tests/d3d9_triangle.exe
# build/dxmt-tests-i386/d3d9_triangle.exe, and make wine-arm64-winetests (the x64 and x86 d3d9_test.exe). One VM run.
# Two lanes (C3d-6): D3D9_BACKEND=air (the default lane) sets no shader.backend in the guest, so the build's default
# runs; D3D9_BACKEND=msl sets shader.backend=msl there (the share's backend file). Each proves its backend: the Unix
# side's info line in the triangle's logs, and the kinds of each leg's kept conformance caches (default: an AIR record
# of every programmable kind the tests made and AIR fixed-function records; msl: no AIR record, MSL programmable and
# fixed-function records). Each leg's runner output, stamped with the lane, is kept as
# vm-work/d3d9-conf/conf-<x64|x86>-<old|new>-<default|msl>.txt (dxmt/d3d9-conf-compare.sh's inputs).
# Prints a line a check; exits 1 on a FAIL. The 32-bit rows run under old WoW64, the image's default, or with
# MN_WOW64=new under new WoW64. D3D9_RUNTIME=core (DXMTRS C4a, with the default backend) is a third lane, "core"
# (the share's runtime file): runtime.mode=core in the guest, D3D9's encoder in d3d9.dll on dxmt-core (an image built
# with DXMT_D3D9_CORE=1), no mtld3d.so unit tests, and every triangle row's logs must hold the core device's line
# ("d3d9 core: device on dxmt-core"), not mtld3d.so's encoder line ("texture versions: rename pool cap", printed by
# every mtld3d device) and none of the core's can't-present lines (no command library, no Metal view, a pipeline that
# does not build: each still answers D3D_OK) nor its line of a dynamic buffer staged for want of a usable name ("has
# no mapped memory", DXMTRS C4c-2b). Under core the guest runs no conformance (it
# runs from C4b on) and the gate checks no shader-backend or kinds row: the triangle rows, the proof, the sm50 rows.
# D3D9_BENCH=1 (no other value) adds the guest's d3d9_triangle --bench rows, each proven by its lane's line and kept as
# vm-work/d3d9-bench/<row>-<old|new>-<lane>.txt by a real run only (vm/tests/d3d9-gate-bench-test.sh): bench-* the
# fixed-function draw (line "mode ff"), benchp-* the vs_2_0/ps_2_0 one (DXMTRS C4b2-12, "mode prog").
# Every lane checks d3d9_triangle's 22 shader rows (DXMTRS C4b-2, C4b2-11) for x86-64 and i386 (shd64-fresh,
# shd32-fresh; "shaders strict" under core: the first frame must match). Under core a launch's logs also hold no core
# refusal line ("... is not implemented under runtime.mode = core"), the shd* rows' none since DXMTRS C4c-2a, which
# draws the crossing row. Every lane also checks d3d9_triangle state's 16 rows (DXMTRS C4b-3a's 12 and C4c-6a's
# state-getters, state-stubs, state-present and state-ex; st64-fresh, st32-fresh); under core their logs hold no refusal
# line (since DXMTRS C4c-1: buffer locks are the core's; since DXMTRS C4c-6a the getters, the stubs and the presents).
# Every lane also checks
# d3d9_triangle textures' 39 rows (DXMTRS C4b-3b and C4b-3c's vertex textures, tx64-fresh, tx32-fresh), and tx32-fresh
# the 32-bit-only tex-big256 (the staged-bytes budget); under core their logs hold no refusal line (since DXMTRS C4c-3a,
# which locks a render-target texture's levels). Every lane also checks d3d9_triangle fixed's 31 rows (DXMTRS C4b-3c:
# the fixed-function vertex stage; fx64-fresh, fx32-fresh); under core their logs hold exactly its refused row's line.
# Every lane also checks d3d9_triangle targets' 35 rows (DXMTRS C4b-3d1: render targets other than the back buffer,
# C4b-3d5's rt-plain, C4c-3a's locks and front buffer: rt-lockable, rt-nolock, rt-bblock, rt-front, and C4c-3b's device
# contexts, an additional swap chain's back buffer and a lockable back buffer: dc-mem, dc-tex, dc-rtex, dc-rt, dc-bb,
# rt-chain, lb-lock, lb-dc; tg64-fresh, tg32-fresh); under core their logs hold no refusal line (since DXMTRS C4c-4c,
# which copies rt-msaa's multisampled surface into a 32x32 target). Every lane also checks d3d9_triangle depth's 31
# rows (DXMTRS C4b-3d2: the
# depth-stencil, on a device with an auto depth-stencil, ds-dynlock C4c-3b's; dp64-fresh, dp32-fresh); under core their
# logs hold exactly its one refused call's line (a depth-tested draw sampling the bound depth-stencil; a dynamic depth
# texture's lock is the core's since DXMTRS C4c-3b). Every lane also checks d3d9_triangle blend's 36
# rows (DXMTRS C4b-3d3: partial clears, the scissor test, a depth-stencil of another size, sRGB write and blending;
# bl64-fresh, bl32-fresh); under core their logs hold no refusal line. Every lane also checks d3d9_triangle mrt's 20
# rows (DXMTRS C4b-3d4: render targets at index 1-3; mr64-fresh, mr32-fresh); under core their logs hold no refusal
# line. Every lane also checks d3d9_triangle msaa's 23 rows (DXMTRS C4b-3d5: multisampling, on a device whose back
# buffer and auto depth-stencil have 4 samples, C4c-3a's ms-bblock and ms-front: the back buffer read by a lock and by
# GetFrontBufferData, C4c-4b's ms-dcopy: a depth-stencil copy of 4 samples, C4c-4d's ms-dres: a depth-stencil
# resolve, and C4d-2a's ms-lblock and ms-lbdc: writable LockRect and GetDC on a 4x MSAA lockable back buffer;
# ms64-fresh, ms32-fresh); under core their logs hold no refusal line (since DXMTRS C4c-4b, which copies
# ms-desc's and ms-dcopy's depth-stencil surfaces, and C4c-4d, which resolves ms-dres's).
# Every lane also checks d3d9_triangle buffers' 39 rows (DXMTRS C4c-1, C4c-2a and
# C4c-2b: vertex and index buffers and the draws that read them; bf64-fresh, bf32-fresh); under core their logs hold
# exactly its one refusal line (a layout no binding can fetch: buf-wide's attribute wider than its stride), and the line
# logged at the first draw that reads a dynamic buffer in place ("d3d9 core: dynamic buffers are read in place", DXMTRS
# C4c-2b; the row "bf<64|32>-fresh buffers direct"). Every lane also checks d3d9_triangle blit's 41 rows (DXMTRS
# C4c-4a: ColorFill, UpdateSurface and StretchRect's answers, C4c-4b's 13 sc-* rows: StretchRect's unscaled copies,
# C4c-4c's 10 sq-* rows: its sampled ones, and C4c-4d's sd-resolve: its depth-stencil resolve; on a device with no
# depth-stencil; bt64-fresh, bt32-fresh); under core their logs hold no refusal line (since DXMTRS C4c-4d, which
# resolves sd-resolve's depth-stencil surfaces). Every lane also checks d3d9_triangle queries' 12 rows (DXMTRS C4c-5:
# EVENT and OCCLUSION queries, on a device with no depth-stencil; qu64-fresh, qu32-fresh); under core their logs hold
# no refusal line (since DXMTRS C4c-5, which makes CreateQuery the core's). Every lane also checks d3d9_triangle reset's
# 14 rows (DXMTRS C4c-6c: Reset and ResetEx of a windowed device, TestCooperativeLevel and CheckDeviceState, each row on
# a device and a window of its own; "reset strict" under core, where rs-refuse alone asks for a Reset to an R5G6B5 back
# buffer; rs64-fresh, rs32-fresh); under core their logs hold exactly its one refusal line (that Reset to a back-buffer
# format the core does not make, refused by name). Every lane also checks d3d9_triangle extras' 15 rows (DXMTRS C4c-7a:
# the gamma ramp, frame latency, the Ex creators, RESZ and the cursor calls, each row on a device and a window of its
# own; "extras strict" under core, where rz-self and rz-refuse alone run; ex64-fresh, ex32-fresh); under core their logs
# hold exactly its one refusal line (rz-refuse's RESZ into a dynamic depth texture, refused by name). Every lane also
# checks d3d9_triangle fullscreen's 9 rows (DXMTRS C4c-6d: fullscreen devices, Reset into and out of fullscreen and the
# window subclass, each row on a plain device and a window of its own, in the guest's prefix, which emulates mode sets;
# "fullscreen strict" under core; fs64-fresh, fs32-fresh); under core their
# logs hold no refusal line. Every lane also checks d3d9_triangle parity's 11 rows (DXMTRS C4c-7d-1, C4c-7d-2, C4d-2a:
# D3DSAMP_SRGBTEXTURE's low bit, a volume texture of depth 1, a managed texture's uploaded levels, ProcessVertices, a
# one-sample RESZ between two depth formats, the gamma ramp on a fullscreen device, a draw that samples its own render
# target, and zeroed unwritten texture staging, rz-cross and gm-full each on a device and a window of its own, in the
# guest's prefix, which emulates mode sets; "parity strict" under core;
# pa64-fresh, pa32-fresh); under core their logs hold no refusal line. Every lane also checks d3d9_triangle lost's 2
# rows (DXMTRS C4d-2b: permanent D3DERR_DEVICELOST latching under debug.failNextSubmit=true: lost-plain and lost-ex,
# each on a device and a window of its own; "lost strict" under core; lo64-fresh, lo32-fresh); under core their logs
# hold no refusal line. Every lane also checks d3d9_triangle pace's 3 rows (DXMTRS C4d-4a: present pacing and occluded
# presents under present.maxFps=50;debug.presentOccluded=true: pace-imm, pace-two, pace-occ, each on a device and a
# window of its own; "pace strict" under core; pc64-fresh, pc32-fresh); under core their logs hold no refusal line.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B=${BUILD_DIR:-$ROOT/build}
W=$B/vm-work
T=$B/wine-arm64-src/dxmt
S=$W/share/d3d9 G=$W/share/traces/d3d9
fail=0
BK=${D3D9_BACKEND:-air} RT=${D3D9_RUNTIME:-mtld3d}
case $RT/$BK in
  mtld3d/air) lane=default ;;
  mtld3d/msl) lane=msl ;;
  core/air) lane=core ;;
  core/*) echo "FAIL D3D9_RUNTIME=core takes the default backend lane only"; exit 2 ;;
  mtld3d/*) echo "FAIL D3D9_BACKEND=$BK: air or msl"; exit 2 ;;
  *) echo "FAIL D3D9_RUNTIME=$RT: mtld3d or core"; exit 2 ;;
esac
case $lane in
  msl) echo "d3d9-gate: lane msl (shader.backend=msl)" ;;
  core) echo "d3d9-gate: lane core (runtime.mode=core)" ;;
  *) echo "d3d9-gate: lane $lane" ;;
esac
check() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi; }
# D3D9_GATE_TRACES=<folder>: no build and no run, the checks on a saved run's traces (a copy of $G).
if [ -n "${D3D9_GATE_TRACES:-}" ]; then
  G=$D3D9_GATE_TRACES
  units=$(cd "$G" && ls unit-*.status | sed 's/^unit-//; s/\.status$//')
else
  # The translator key is one string in the host's and both guest arches' mtld3d.so (the guest ones staged by
  # guest-stages.sh, built before this run), or a guest AIR record would carry a key the Mac never writes.
  # The core lane writes no mtld3d.so AIR records (d3d9.dll shares DXMT's SQLite shader cache, whose non-dev
  # cache_<version> table is verified in wm-cache below), so it carries no translator key here.
  key=$(cat "$B/wine-arm64/wine.app/Contents/Resources/DXMT/translator" 2> /dev/null) kr=
  for so in "$W/dxmt-guest/x86_64-unix/mtld3d.so" "$W/dxmt-guest/i386-unix/mtld3d.so" \
    "$B/wine-arm64/wine.app/Contents/Resources/lib/wine/aarch64-unix/mtld3d.so"; do
    n=$(/usr/bin/grep -c "$key" "$so" 2> /dev/null); { [ -n "$key" ] && [ "${n:-0}" -ge 1 ]; } || kr="$kr ${so#$B/}"
  done
  check "key guest=host" "${kr:-ok}" ok
  rm -rf "$S"; mkdir -p "$S/unit" "$S/unit32" "$S/winetests/x64" "$S/winetests/x86"
  echo "${MN_WOW64:-old}" > "$S/wow64"
  echo "$lane" > "$S/backend"
  echo "$RT" > "$S/runtime"
  [ "${D3D9_BENCH:-}" != 1 ] || echo 1 > "$S/bench"
  # The Linux unit tests (release profile, as their macOS runs), x86_64 and i686 (old WoW64's, on the i686
  # winemetal.so), and the runner; each test binary under its crate's name. DXMTRS's tools/linux-cargo.sh, on the
  # builder's sysroot of each arch (vm/sysroot/fetch.sh), into build/d3d9-cargo/gate-<arch>.
  . "$ROOT/dxmt/pins"
  LM=$(dirname "$(sh "$ROOT/dxmt/toolchain.sh")")
  PATH="$(sh "$ROOT/dxmt/mtld3d-tools.sh"):$PATH"  # cargo +$MTLD3D_RUST
  export PATH MTLD3D_RUST MTLD3D_LLVM_MINGW="$LM"
  for a in x86_64 i686; do
    case $a in x86_64) u=$S/unit ;; *) u=$S/unit32 ;; esac
    [ "$RT" = core ] && [ $a = i686 ] && continue  # the core lane runs no mtld3d.so unit tests (the runner below)
    MTLD3D_SYSROOT=$(sh "$ROOT/vm/sysroot/fetch.sh" $a 2>> "$W/d3d9-gate-build.log") \
      || { echo "FAIL the $a sysroot: see $W/d3d9-gate-build.log"; exit 1; }
    export MTLD3D_SYSROOT
    [ "$RT" = core ] || CARGO_TARGET_DIR=$B/d3d9-cargo/gate-$a MTLD3D_LINUX=$a sh "$T/tools/linux-cargo.sh" test --release --no-run \
      --message-format=json -p mtld3d-shared -p mtld3d-unix -p mtld3d-mtl 2>> "$W/d3d9-gate-build.log" | python3 -I -c '
import json, shutil, sys
for line in sys.stdin:
    m = json.loads(line)
    if m.get("reason") == "compiler-artifact" and m.get("executable") and m["profile"]["test"]:
        shutil.copy(m["executable"], sys.argv[1] + "/" + m["target"]["name"])' "$u" \
      || { echo "FAIL the $a Linux test binaries: see $W/d3d9-gate-build.log"; exit 1; }
    [ $a != x86_64 ] || CARGO_TARGET_DIR=$B/d3d9-cargo/gate-$a MTLD3D_LINUX=$a sh "$T/tools/linux-cargo.sh" build \
      --release -p mtld3d-conformance 2>> "$W/d3d9-gate-build.log" \
      || { echo "FAIL the Linux runner: see $W/d3d9-gate-build.log"; exit 1; }
  done
  cp "$B/d3d9-cargo/gate-x86_64/x86_64-unknown-linux-gnu/release/mtld3d-conformance" "$S/"
  mkdir -p "$S/conformance" \
    && cp "$T/tests/d3d9/conformance/baseline.txt" "$T/tests/d3d9/conformance/CONFORMANCE.md" "$S/conformance/"
  cp "$B/dxmt-tests/d3d9_triangle.exe" "$S/" && cp "$B/dxmt-tests-i386/d3d9_triangle.exe" "$S/d3d9_triangle32.exe" \
    || { echo "FAIL no d3d9_triangle.exe: make build/dxmt-tests/d3d9_triangle.exe build/dxmt-tests-i386/d3d9_triangle.exe"; exit 1; }
  for l in x64 x86; do
    cp "$B/wine-arm64-tests/winetests/$l/d3d9_test.exe" "$S/winetests/$l/" \
      || { echo "FAIL no $l d3d9_test.exe: make wine-arm64-winetests"; exit 1; }
  done
  # The tests' Wine version: our Wine's, whose tree they were built from (dxmt/mtld3d-conformance.sh's).
  "$B/wine-arm64/wine.app/Contents/Resources/bin/wine" --version > "$S/tests-wine-version"
  WM_CACHE="$W/d3d9-wm-cache"
  rm -rf "$G" "$WM_CACHE"
  mkdir -p "$WM_CACHE"
  MN_WM_CACHE_DIR="$WM_CACHE" MN_RUN_TIMEOUT=7200 "$ROOT/vm/run-guest.sh" "$ROOT/vm/tests/d3d9.sh" > "$W/d3d9-guest-run.txt" 2>&1 \
    || { echo "FAIL the guest run: see $W/d3d9-guest-run.txt"; fail=1; }
  rm -rf "$G/wm-cache"
  cp -R "$WM_CACHE" "$G/wm-cache" 2> /dev/null || true
  units=$(ls "$S/unit"; ls "$S/unit32" | sed 's/^/32-/')
  mkdir -p "$W/d3d9-conf"
  for l in x64 x86; do cp "$G/conf-$l.txt" "$W/d3d9-conf/conf-$l-${MN_WOW64:-old}-$lane.txt" 2> /dev/null || true; done
fi
for n in $units; do
  check "unit $n" "$(cat "$G/unit-$n.status" 2> /dev/null)/$(/usr/bin/grep -c '^test result: ok' "$G/unit-$n.txt" 2> /dev/null)" 0/1
done
for n in metal-sm50-args32-test metal-sm50-args32-test32; do
  check "$n" "$(cat "$G/$n.status" 2> /dev/null)/$(/usr/bin/grep -c '^sm50-args32-test: ok' "$G/$n.txt" 2> /dev/null)" 0/1
done
for r in tri64-fresh tri32-fresh tri64-updated tri32-updated; do
  for row in module clear triangle present; do
    check "$r $row" "$(/usr/bin/grep "^d3d9 $row " "$G/$r.txt" 2> /dev/null || echo "no line")" "d3d9 $row ok"
  done
done
for r in shd64-fresh shd32-fresh; do
  for row in caps vs1 vs2 vs3 consti constb rel decl indexed fan bypass vsff alpha fog mask wire cull viewport clip \
    srgb crossing getters; do
    check "$r $row" "$(/usr/bin/grep "^d3d9 $row " "$G/$r.txt" 2> /dev/null || echo "no line")" "d3d9 $row ok"
  done
done
for r in tx64-fresh tx32-fresh; do
  for row in tex-ps2d tex-ff tex-cascade tex-cube tex-volume tex-mips tex-address tex-filter tex-srgb tex-dxt1 tex-l8 \
    tex-a8 tex-a8l8 tex-r5g6b5 tex-a4r4g4b4 tex-r8g8b8 tex-lock tex-twodraws tex-dynamic tex-levelsurf tex-genmips \
    tex-update tex-autogen tex-black tex-bumpenv tex-texbem tex-big tex-release tex-depthtex tex-fetch4 tex-fetch4a tex-rttex \
    tex-vtx     tex-rtlock tex-vtxlod tex-vtxblack tex-vtxcube tex-vtxvolume tex-vtxsrgb \
    $([ $r = tx32-fresh ] && echo tex-big256); do
    check "$r $row" "$(/usr/bin/grep "^d3d9 $row " "$G/$r.txt" 2> /dev/null || echo "no line")" "d3d9 $row ok"
  done
done
for r in fx64-fresh fx32-fresh; do
  for row in ff-xform ff-persp ff-rhwmix ff-viewchange ff-ambient ff-lightdefault ff-lightdir ff-lightpoint ff-lightspot \
    ff-lightsparse ff-light8 ff-specular ff-matsrc ff-normalize ff-fogvertex ff-fogtable ff-texgen ff-tt ff-ttproj \
    ff-tcindex ff-tex ff-blend ff-blendidx ff-point ff-pointscale ff-clip ff-indexed ff-decl tex-vtxrt tex-vtxdepth \
    tex-vtxdepthfmt; do
    check "$r $row" "$(/usr/bin/grep "^d3d9 $row " "$G/$r.txt" 2> /dev/null || echo "no line")" "d3d9 $row ok"
  done
done
for r in tg64-fresh tg32-fresh; do
  for row in rt-tex rt-read rt-surface rt-viewport rt-big rt-mip rt-cube rt-switch rt-rewrite rt-clearonly rt-fresh \
    rt-autogen rt-release rt-srgb rt-ps rt-update rt-updatebound rt-formats rt-mrt rt-msaa rt-plain rt-cubelock \
    rt-huge rt-lockable rt-nolock rt-bblock rt-front dc-mem dc-tex dc-rtex dc-rt dc-bb rt-chain lb-lock lb-dc; do
    check "$r $row" "$(/usr/bin/grep "^d3d9 $row " "$G/$r.txt" 2> /dev/null || echo "no line")" "d3d9 $row ok"
  done
done
for r in dp64-fresh dp32-fresh; do
  for row in ds-default ds-test ds-func ds-write ds-zenable ds-clearz ds-clearonly ds-stencil ds-twosided ds-bias \
    ds-slope ds-clamp ds-odepth ds-surface ds-nostencil ds-unbound ds-persist ds-texture ds-texstencil ds-mip \
    ds-release ds-shadow ds-intz ds-df ds-fetch4 ds-size ds-rhwclip ds-feedback ds-msaa ds-huge ds-dynlock; do
    check "$r $row" "$(/usr/bin/grep "^d3d9 $row " "$G/$r.txt" 2> /dev/null || echo "no line")" "d3d9 $row ok"
  done
done
for r in bl64-fresh bl32-fresh; do
  for row in cl-rects cl-count cl-scissor cl-viewport cl-z cl-stencil cl-mask cl-order cl-level cl-bigz cl-smallz \
    sc-rect sc-off sc-empty sc-clamp sc-pass sc-break sz-large sz-clear sz-small sr-clear sr-blend sr-toggle \
    bl-alpha bl-add bl-ops bl-factors bl-both bl-destalpha bl-separate bl-factor bl-stencil bl-off bl-mask bl-prog \
    bl-pass; do
    check "$r $row" "$(/usr/bin/grep "^d3d9 $row " "$G/$r.txt" 2> /dev/null || echo "no line")" "d3d9 $row ok"
  done
done
for r in mr64-fresh mr32-fresh; do
  for row in mrt-two mrt-four mrt-hole mrt-unwritten mrt-nobind mrt-clear mrt-mask mrt-blend mrt-destalpha \
    mrt-formats mrt-srgb mrt-srgbclear mrt-size mrt-switch mrt-scissor mrt-depth mrt-back mrt-autogen mrt-order \
    mrt-read; do
    check "$r $row" "$(/usr/bin/grep "^d3d9 $row " "$G/$r.txt" 2> /dev/null || echo "no line")" "d3d9 $row ok"
  done
done
for r in ms64-fresh ms32-fresh; do
  for row in ms-initial ms-desc ms-dcopy ms-dres ms-resolve ms-edge ms-back ms-clear ms-depth ms-mismatch ms-mask \
    ms-nonmask ms-a2c ms-srgbmask ms-blend ms-mrt ms-scissor ms-redraw ms-samples ms-bblock ms-front ms-lblock \
    ms-lbdc; do
    check "$r $row" "$(/usr/bin/grep "^d3d9 $row " "$G/$r.txt" 2> /dev/null || echo "no line")" "d3d9 $row ok"
  done
done
for r in bf64-fresh bf32-fresh; do
  for row in buf-d3d buf-lock buf-draw buf-start buf-offset buf-strip buf-fan buf-indexed buf-index32 buf-base buf-ifan \
    buf-managed buf-dynamic buf-order buf-discard buf-held buf-relock buf-streams buf-null buf-null-up buf-zero-stride \
    buf-instanced buf-shader buf-tex buf-nobuffer buf-release buf-odd buf-cross buf-cross-up buf-cross-multi \
    buf-cross-odd buf-cross-indexed buf-wide buf-dyn-preserve buf-dyn-many buf-dyn-index buf-dyn-read buf-dyn-cross \
    buf-writeonly; do
    check "$r $row" "$(/usr/bin/grep "^d3d9 $row " "$G/$r.txt" 2> /dev/null || echo "no line")" "d3d9 $row ok"
  done
done
for r in bt64-fresh bt32-fresh; do
  for row in cf-rt cf-lockable cf-bb cf-unbound cf-rtex cf-autogen cf-msaa cf-plain cf-reject us-tex us-level us-rtex \
    us-rt us-bb us-conv us-reject sr-reject sd-resolve sc-rt sc-bb sc-lockable sc-rtex sc-tex sc-plain sc-plain2 \
    sc-conv sc-same sc-self sc-autogen sc-order sq-scale sq-level sq-conv sq-swizzle sq-yuv sq-ms sq-overlap sq-plain \
    sq-order sq-autogen sc-depth; do
    check "$r $row" "$(/usr/bin/grep "^d3d9 $row " "$G/$r.txt" 2> /dev/null || echo "no line")" "d3d9 $row ok"
  done
done
for r in qu64-fresh qu32-fresh; do
  for row in qy-support qy-event qy-count qy-empty qy-open qy-twice qy-present qy-two qy-pass qy-release qy-reuse \
    qy-noflush; do
    check "$r $row" "$(/usr/bin/grep "^d3d9 $row " "$G/$r.txt" 2> /dev/null || echo "no line")" "d3d9 $row ok"
  done
done
for r in rs64-fresh rs32-fresh; do
  for row in rs-size rs-depth rs-msaa rs-blockers rs-state rs-autogen rs-scene rs-invalid rs-refuse rs-window rs-ex \
    rs-held rs-query rs-twice; do
    check "$r $row" "$(/usr/bin/grep "^d3d9 $row " "$G/$r.txt" 2> /dev/null || echo "no line")" "d3d9 $row ok"
  done
done
for r in ex64-fresh ex32-fresh; do
  for row in gm-ramp fl-latency ex-rt ex-ds ex-plain rz-msaa rz-auto rz-single rz-plain rz-edges rz-self rz-refuse \
    cu-props cu-show cu-setcursor; do
    check "$r $row" "$(/usr/bin/grep "^d3d9 $row " "$G/$r.txt" 2> /dev/null || echo "no line")" "d3d9 $row ok"
  done
done
for r in fs64-fresh fs32-fresh; do
  for row in fs-create fs-cover fs-reject fs-reset-in fs-reset-window fs-wndproc fs-reset-mode fs-activate ws-size; do
    check "$r $row" "$(/usr/bin/grep "^d3d9 $row " "$G/$r.txt" 2> /dev/null || echo "no line")" "d3d9 $row ok"
  done
done
for r in pa64-fresh pa32-fresh; do
  for row in sr-lsb vo-one dr-keep pv-sysmem pv-draw pv-dynsrc pv-dyndst rz-cross gm-full tb-self tex-unwritten; do
    check "$r $row" "$(/usr/bin/grep "^d3d9 $row " "$G/$r.txt" 2> /dev/null || echo "no line")" "d3d9 $row ok"
  done
done
for r in lo64-fresh lo32-fresh; do
  for row in lost-plain lost-ex; do
    check "$r $row" "$(/usr/bin/grep "^d3d9 $row " "$G/$r.txt" 2> /dev/null || echo "no line")" "d3d9 $row ok"
  done
done
for r in pc64-fresh pc32-fresh; do
  for row in pace-imm pace-two pace-occ; do
    check "$r $row" "$(/usr/bin/grep "^d3d9 $row " "$G/$r.txt" 2> /dev/null || echo "no line")" "d3d9 $row ok"
  done
done
for r in st64-fresh st32-fresh; do
  for row in state-xform state-material state-tss state-swapchain state-rt state-tex state-late state-lock state-sb \
    state-upunbind state-fftex state-fftexoff state-getters state-stubs state-present state-ex; do
    check "$r $row" "$(/usr/bin/grep "^d3d9 $row " "$G/$r.txt" 2> /dev/null || echo "no line")" "d3d9 $row ok"
  done
done
# core: runtime <folder> [fixed|depth|buffers|reset|extras]: the core device's line in a launch's logs, none of
# mtld3d.so's encoder line nor of the core's can't-present, dropped-draw or staged-dynamic-buffer ("has no mapped
# memory", DXMTRS C4c-2b) lines (the code that ran prints them, not the switch), and no refusal line, or with
# "fixed", "depth", "buffers", "reset" or "extras" exactly that launch's refused rows'
# (fixed_refusals, depth_refusals: one line since DXMTRS C4c-3b, which locks a dynamic depth texture,
# buffers_refusals: DXMTRS C4c-2a's layout no binding can fetch, reset_refusals: DXMTRS C4c-6c's Reset to a
# back-buffer format the core does not make, rs-refuse's under strict alone (its one since DXMTRS C4c-6d, which makes a
# Reset into fullscreen the core's), extras_refusals: DXMTRS C4c-7a's RESZ into a dynamic depth texture, rz-refuse's
# under strict alone).
# The shaders, state, textures, targets, blend, mrt, msaa, blit, queries, fullscreen, parity, lost and pace launches (DXMTRS
# C4b-2, C4b-3a, C4b-3b, C4b-3d1, C4b-3d3, C4b-3d4, C4b-3d5, C4c-4a, C4c-5, C4c-6d, C4c-7d-1, C4d-2b, C4d-4a) take no kind: no refusal
# line (the parity launch's since DXMTRS C4c-7d-1, which makes ProcessVertices the core's; the
# fullscreen launch's since DXMTRS C4c-6d, which makes fullscreen devices and Resets into fullscreen the core's; the
# shaders launch's since DXMTRS C4c-2a, which draws the crossing row; the state launch's since DXMTRS C4c-1: buffer
# locks are the core's, and since
# DXMTRS C4c-6a the getters, the stubs and the presents; the
# textures launch's since DXMTRS C4c-3a: a render-target texture's lock is the core's; the targets launch's since
# DXMTRS C4c-4c: a scaled copy of a multisampled surface is the core's; the msaa launch's since DXMTRS C4c-4b: a
# depth-stencil copy of 4 samples is the core's, and since DXMTRS C4c-4d its resolve; the blit launch's since DXMTRS
# C4c-4d: a depth-stencil resolve is the core's; the queries launch's since DXMTRS C4c-5: CreateQuery is the core's).
refused='is not implemented under runtime.mode = core'
fixed_refusals='IDirect3DDevice9::Draw*PrimitiveUP (a depth texture at a vertex sampler)'
depth_refusals='IDirect3DDevice9::Draw*PrimitiveUP (the depth-stencil sampled while bound)'
buffers_refusals='IDirect3DDevice9::Draw*Primitive (a layout no binding can fetch)'
reset_refusals='IDirect3DDevice9::Reset (a back-buffer format the core does not make)'
extras_refusals='IDirect3DDevice9::SetRenderState(RESZ) (into a dynamic depth texture)'
runtime() {
  n=$(/usr/bin/grep -rhF "$refused" "$1" 2> /dev/null | wc -l | tr -d ' ')
  /usr/bin/grep -rqF 'd3d9 core: device on dxmt-core' "$1" 2> /dev/null && ! /usr/bin/grep -rqF 'texture versions: rename pool cap' "$1" \
    && ! /usr/bin/grep -rqE 'nothing presents|d3d9 core: no Metal view|does not build|colour targets are not its key|depth-stencil is not its key|buffer has no core form|has no mapped memory|was not recorded' "$1" \
    && case ${2:-} in
      fixed|depth|buffers|reset|extras)
        case $2 in
          fixed) set_=$fixed_refusals ;; depth) set_=$depth_refusals ;; buffers) set_=$buffers_refusals ;;
          reset) set_=$reset_refusals ;; extras) set_=$extras_refusals ;;
        esac
        [ "$n" = "$(printf '%s\n' "$set_" | wc -l | tr -d ' ')" ] && printf '%s\n' "$set_" | while IFS= read -r m; do
          /usr/bin/grep -rqF "d3d9 core: $m $refused" "$1" || exit 1; done ;;
      *) [ "$n" = 0 ] ;;
    esac && echo ok
}
if [ "$RT" = core ]; then
  for r in tri64-fresh tri32-fresh tri64-updated tri32-updated; do
    check "$r runtime core" "$(runtime "$G/logs/$r")" ok
  done
  for r in tx64-fresh tx32-fresh; do
    check "$r runtime core" "$(runtime "$G/logs/$r")" ok
  done
  for r in fx64-fresh fx32-fresh; do
    check "$r runtime core" "$(runtime "$G/logs/$r" fixed)" ok
  done
  for r in dp64-fresh dp32-fresh; do
    check "$r runtime core" "$(runtime "$G/logs/$r" depth)" ok
  done
  for r in rs64-fresh rs32-fresh; do
    check "$r runtime core" "$(runtime "$G/logs/$r" reset)" ok
  done
  for r in ex64-fresh ex32-fresh; do
    check "$r runtime core" "$(runtime "$G/logs/$r" extras)" ok
  done
  for r in bf64-fresh bf32-fresh; do
    check "$r runtime core" "$(runtime "$G/logs/$r" buffers)" ok
    # The line d3d9-rt logs at the first draw that reads a dynamic buffer in place (DXMTRS C4c-2b).
    check "$r buffers direct" \
      "$(/usr/bin/grep -rqF 'd3d9 core: dynamic buffers are read in place' "$G/logs/$r" 2> /dev/null && echo ok)" ok
  done
  for r in shd64-fresh shd32-fresh st64-fresh st32-fresh tg64-fresh tg32-fresh bl64-fresh bl32-fresh mr64-fresh \
    mr32-fresh ms64-fresh ms32-fresh bt64-fresh bt32-fresh qu64-fresh qu32-fresh fs64-fresh fs32-fresh pa64-fresh \
    pa32-fresh lo64-fresh lo32-fresh pc64-fresh pc32-fresh; do
    check "$r runtime core" "$(runtime "$G/logs/$r")" ok
  done
  for r in shd64-cold shd64-warm; do
    check "$r runtime core" "$(runtime "$G/logs/$r")" ok
    for row in caps vs1 vs2 vs3 consti constb rel decl indexed fan bypass vsff alpha fog mask wire cull viewport clip \
      srgb crossing getters; do
      check "$r $row" "$(/usr/bin/grep "^d3d9 $row " "$G/$r.txt" 2> /dev/null || echo "no line")" "d3d9 $row ok"
    done
  done
  cold_sum=$(/usr/bin/grep -rhF 'd3d9 core: shader cache summary:' "$G/logs/shd64-cold" 2> /dev/null | tail -1)
  echo "info shd64-cold cache summary: ${cold_sum:-none}"
  check "shd64-cold cache misses and recipes_saved" \
    "$(printf '%s\n' "$cold_sum" | /usr/bin/grep -qE '(^|[ ,])(cache_)?misses=[1-9]' \
      && printf '%s\n' "$cold_sum" | /usr/bin/grep -qE '(^|[ ,])recipes_saved=[1-9]' && echo ok)" ok
  warm_sum=$(/usr/bin/grep -rhF 'd3d9 core: shader cache summary:' "$G/logs/shd64-warm" 2> /dev/null | tail -1)
  echo "info shd64-warm cache summary: ${warm_sum:-none}"
  check "shd64-warm cache hits, zero misses, recipes_loaded" \
    "$(printf '%s\n' "$warm_sum" | /usr/bin/grep -qE '(^|[ ,])(cache_)?hits=[1-9]' \
      && printf '%s\n' "$warm_sum" | /usr/bin/grep -qE '(^|[ ,])(cache_)?misses=0([^0-9]|$)' \
      && printf '%s\n' "$warm_sum" | /usr/bin/grep -qE '(^|[ ,])recipes_loaded=[1-9]' && echo ok)" ok
  wm_dbs=0 wm_bad=
  for db in "$G/wm-cache/"*shaders_*.db; do
    [ -f "$db" ] || continue
    wm_dbs=$((wm_dbs + 1))
    t=$(sqlite3 "$db" "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'cache_%';" 2> /dev/null || true)
    tc=$(printf '%s\n' "$t" | sed '/^$/d' | wc -l | tr -d ' ')
    echo "info wm-cache db $(basename "$db"): tables=${t:-none} count=$tc"
    [ "$tc" = 1 ] && [ "$t" != "cache_5555813613417847110" ] && [ "$t" != "cache_10131673474557754031" ] \
      || wm_bad="$wm_bad $(basename "$db"):$tc:$t"
  done
  check "wm-cache sqlite db exists" "$([ "$wm_dbs" -ge 1 ] && echo ok)" ok
  check "wm-cache single non-dev table" "${wm_bad:-ok}" ok
fi
if [ "${D3D9_BENCH:-}" = 1 ]; then
  rows="bench-32-0 bench-32-600 benchp-32-0 benchp-32-600"
  [ "${MN_WOW64:-old}" = new ] || rows="bench-64-0 bench-64-600 benchp-64-0 benchp-64-600 $rows"
  mkdir -p "$W/d3d9-bench"
  for r in $rows; do
    m=ff; case $r in benchp-*) m=prog ;; esac
    if [ "$RT" = core ]; then p=$(runtime "$G/logs/$r")
    else p=$(/usr/bin/grep -rqF 'texture versions: rename pool cap' "$G/logs/$r" 2> /dev/null \
      && ! /usr/bin/grep -rqF 'd3d9 core: device on dxmt-core' "$G/logs/$r" && echo ok); fi
    check "$r bench" "$(/usr/bin/grep -c "^d3d9 bench: mode $m " "$G/$r.txt" 2> /dev/null)/$(/usr/bin/grep -cx 'status 0' "$G/$r.txt" 2> /dev/null)/$p" 1/1/ok
    # A real run's rows only: a trace re-check never rewrites what a run saved.
    [ -n "${D3D9_GATE_TRACES:-}" ] \
      || /usr/bin/grep '^d3d9 bench:' "$G/$r.txt" > "$W/d3d9-bench/$r-${MN_WOW64:-old}-$lane.txt" 2> /dev/null
  done
fi
# Without the markers d3d9.dll's import of mtld3d.dll isn't found: the reason run-game.sh updates such a prefix.
for r in tri64-nomarker tri32-nomarker; do
  check "$r fails without the marker" "$(/usr/bin/grep -c '^d3d9 module ok' "$G/$r.txt" 2> /dev/null)" 0
done
if [ "$RT" = core ]; then  # no conformance, shader-backend or kinds rows under core in C4a
  [ $fail -eq 0 ] && echo "d3d9 gate: ok (core)" || echo "d3d9 gate: FAIL"
  exit $fail
fi
# The runner's per-subtest lines: "<leg>/<subtest>  baseline(failed=N crash=N) current(failed=N crash=N) executed=N
# <status>".
figure() { sed -n "s|^$1/$2 .*current(failed=\([0-9]*\) crash=\([0-9]*\)).*|\1 \2|p" "$G/conf-$3.txt" 2> /dev/null; }
executed() { sed -n "s|^$1/$2 .* executed=\([0-9]*\) .*|\1|p" "$G/conf-$3.txt" 2> /dev/null; }
# Each subtest: the macOS product's failures, and the tests it executes at least, the lower of the macOS run's and the
# guest's (2026-10-21: the guest runs 19 fewer d3d9ex display-mode tests under Xvfb, macOS 7 fewer device ones), so a
# run that returned early without crashing doesn't pass as an improvement.
WANT="stateblock 0 14738,visual 31 211219,device 739 159542,d3d9ex 74 2407"
# The x64 lane: at most the macOS product's figures, no crash, those tests executed, and no failing site of its own
# beyond the macOS run's triaged ones (dxmt/mtld3d-triaged.txt, as dxmt/mtld3d-conformance.sh rules them). The guest
# fails a few display-mode sites fewer than macOS (Xvfb's mode emulation).
IFS=, read -r w1 w2 w3 w4 <<EOF2
$WANT
EOF2
for want in "$w1" "$w2" "$w3" "$w4"; do
  set -- $want
  got=$(figure arm64x "$1" x64) ran=$(executed arm64x "$1" x64)
  echo "info conformance x64 $1: failed ${got% *}, crash ${got#* }, executed ${ran:-?} (macOS $2; at least $3)"
  check "conformance x64 $1: at most $2, no crash" \
    "$([ -n "$got" ] && [ "${got% *}" -le "$2" ] && [ "${got#* }" = 0 ] && echo ok)" ok
  check "conformance x64 $1: at least $3 tests executed" "$([ -n "$ran" ] && [ "$ran" -ge "$3" ] && echo ok)" ok
done
# A lane's new failing sites outside dxmt/mtld3d-triaged.txt (dxmt/mtld3d-conformance.sh's rule), "none" if none.
untriaged() {
  core=; [ "$RT" = core ] && core="$ROOT/dxmt/mtld3d-triaged-core.txt"  # the core lane's known sites too (C4-7)
  left=$(awk -v triaged="$ROOT/dxmt/mtld3d-triaged.txt" -v core="$core" '
    BEGIN { while ((getline l < triaged) > 0) if (l !~ /^#/ && l != "") { split(l, f, " "); ok[f[1] " " f[2]] = 1 }
            if (core != "") while ((getline l < core) > 0) if (l !~ /^#/ && l != "") { split(l, f, " "); ok[f[1] " " f[2]] = 1 } }
    /^[a-z0-9_+@-]+\/[a-z0-9]+ / { sub_ = $1; next }
    /^  [a-z0-9_]+\.c:[0-9]+ .*REGRESSION/ { if (!((sub_ " " $1) in ok)) printf "%s %s ", sub_, $1 }
  ' "$G/conf-$1.txt" 2> /dev/null)
  echo "${left:-none}"
}
check "conformance x64: no untriaged new failing site" "$(untriaged x64)" none
# The i386 lane: at most the baseline's failures (the macOS i686-arm64 leg's) and its crashes (that leg crashes once in
# device, which the runner names by subtest, not by site), the x64 lane's tests executed but where the baseline crashes
# (the run ends before Wine's summary), and no untriaged new site.
base() { sed -n "s|^$1/$2 .*baseline(failed=\([0-9]*\) crash=\([0-9]*\)).*|\1 \2|p" "$G/conf-$3.txt" 2> /dev/null; }
for want in "$w1" "$w2" "$w3" "$w4"; do
  set -- $want
  got=$(figure i686-arm64 $1 x86) base=$(base i686-arm64 $1 x86) ran=$(executed i686-arm64 $1 x86)
  echo "info conformance x86 $1: failed ${got% *}, crash ${got#* }, executed ${ran:-?} (baseline ${base% *}, crash" \
    "${base#* })"
  check "conformance x86 $1: at most the baseline's failures and crashes" "$([ -n "$got" ] && [ -n "$base" ] &&
    [ "${got% *}" -le "${base% *}" ] && [ "${got#* }" -le "${base#* }" ] && echo ok)" ok
  [ "${base#* }" != 0 ] || check "conformance x86 $1: at least $3 tests executed" \
    "$([ -n "$ran" ] && [ "$ran" -ge "$3" ] && echo ok)" ok
done
check "conformance x86: no untriaged new failing site" "$(untriaged x86)" none
for l in x64 x86; do
  check "conformance $l: no validation error or hang" \
    "$(/usr/bin/grep -cE '^conformance: (METAL VALIDATION|GPU HANG)' "$G/conf-$l.txt" 2> /dev/null)" 0
done
if [ "$BK" = air ]; then
  check "air backend logged" "$(/usr/bin/grep -rqE 'shader backend: air, translator [0-9a-f]{8}' "$G/logs" 2> /dev/null && echo ok)" ok
else
  check "msl backend logged" "$(/usr/bin/grep -rqE 'shader backend: msl, translator ' "$G/logs" 2> /dev/null && echo ok)" ok
fi
# emit_corpus --kinds over each leg's kept caches (the runner makes $G/caches), as dxmt/mtld3d-conformance.sh's rows.
# Built as it builds the runner.
. "$ROOT/dxmt/pins"
TOOLS=$(sh "$ROOT/dxmt/mtld3d-tools.sh")
GA=$(cd "$G" && pwd)
for lp in x64:arm64x x86:i686-arm64; do
  leg=${lp%:*} arch=${lp#*:}
  set -- "$GA/caches/$arch"-*.bin
  line=$(cd "$T" && PATH="$TOOLS:$PATH" MACOSX_DEPLOYMENT_TARGET=27.0 CARGO_TARGET_DIR="$B/d3d9-cargo/kinds" \
    cargo "+$MTLD3D_RUST" run -q --release -p mtld3d-core --features disk-cache --example emit_corpus -- \
    --kinds "$@" 2> /dev/null | sed -n '/^kinds /p') || line=
  echo "info $BK records $leg: $line"
  if [ "$BK" = air ]; then
    miss=$(printf '%s\n' "$line" | tr ' ' '\n' | awk -F= '
      /^(air_)?(sm[0-9]|ff)(vs|ps)=/ { k = $1; sub(/^air_/, "", k); seen[k] = 1; if ($1 ~ /^air_/) { a[k] = $2; t += $2; if (k ~ /^ff/) af[k] = $2 } else m[k] = $2 }
      END { for (k in seen) if (m[k] + a[k] > 0 && a[k] < 1) printf "%s ", k; if (t < 1) printf "no-air-records "; if (af["ffvs"] < 1 || af["ffps"] < 1) printf "no-air-ff-records " }')
  else
    miss=$(printf '%s\n' "$line" | tr ' ' '\n' | awk -F= '
      /^air_(sm[0-9]|ff)(vs|ps)=/ { t += $2 } /^sm[0-9](vs|ps)=/ { p += $2 } /^ff(vs|ps)=/ { m[$1] = $2 }
      END { if (t > 0) printf "air-records-under-msl "; if (p < 1) printf "no-msl-records "; if (m["ffvs"] < 1 || m["ffps"] < 1) printf "no-msl-ff-records " }')
  fi
  check "$BK records $leg" "$([ -n "$line" ] && [ -z "$miss" ] && echo ok || echo "none or missing: ${miss:-no kinds line}")" ok
done
sfx=; [ "$BK" = air ] || sfx=" ($BK)"; [ "$RT" = core ] && sfx=" (core)"
[ $fail -eq 0 ] && echo "d3d9 gate: ok$sfx" || echo "d3d9 gate: FAIL"
exit $fail
