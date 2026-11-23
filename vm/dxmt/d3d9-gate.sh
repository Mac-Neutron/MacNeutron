#!/bin/sh
# Direct3D 9 in the VM (D3D9 M2b gate), from the Mac: d3d9_triangle for x86-64 and i386 in a new prefix and in one
# without winemetal.dll's markers that run-game.sh's update then fixes; and Wine's d3d9 tests through mtld3d's
# runner, the x64 lane held to the macOS product's figures at most (D3D9 spec §5.1's loopback run: stateblock 0,
# visual 31, device 739, d3d9ex 74, no crash, no untriaged new site) and the i386 lane to its baseline's failures and
# crashes at most, no untriaged new site.
# Builds the runner (DXMTRS's tools/linux-cargo.sh); needs the image with DXMT/d3d9
# (guest-stages.sh dxmt, image/build.sh wine), the VM host (host/build.sh), make build/dxmt-tests/d3d9_triangle.exe
# build/dxmt-tests-i386/d3d9_triangle.exe, and make wine-arm64-winetests (the x64 and x86 d3d9_test.exe). One VM run.
# Runs D3D9's in-process encoder in d3d9.dll on dxmt-core; every triangle row's logs must hold the core device's line
# ("d3d9 core: device on dxmt-core"), none of the legacy encoder line ("texture versions: rename pool cap") and none
# of the core's can't-present lines (no command library, no Metal view, a pipeline that does not build: each still
# answers D3D_OK) nor its line of a dynamic buffer staged for want of a usable name ("has no mapped memory", DXMTRS
# C4c-2b). Also runs Wine's d3d9 conformance for x64 and x86 (logged to logs/conf-<x64|x86>, proven by d3d9.dll's
# identity, the core device line, and no legacy, refusal or panic line), and checks SQLite wm-cache.
# Each leg's runner output, stamped with the lane, is kept as vm-work/d3d9-conf/conf-<x64|x86>-<old|new>-default.txt
# (dxmt/d3d9-conf-compare.sh's inputs). Prints a line a check; exits 1 on a FAIL. The 32-bit rows run under old WoW64,
# the image's default, or with MN_WOW64=new under new WoW64.
# D3D9_BENCH=1 (no other value) adds the guest's d3d9_triangle --bench rows, each proven by its core log and kept as
# vm-work/d3d9-bench/<row>-<old|new>-default.txt by a real run only (vm/tests/d3d9-gate-bench-test.sh): bench-* the
# fixed-function draw (line "mode ff"), benchp-* the vs_2_0/ps_2_0 one (DXMTRS C4b2-12, "mode prog").
# Also checks d3d9_triangle's 22 shader rows (DXMTRS C4b-2, C4b2-11) for x86-64 and i386 (shd64-fresh,
# shd32-fresh; "shaders strict": the first frame must match); their logs hold no core refusal line (none since
# DXMTRS C4c-2a, which draws the crossing row). Also checks d3d9_triangle state's 16 rows (DXMTRS C4b-3a's 12 and
# C4c-6a's state-getters, state-stubs, state-present and state-ex; st64-fresh, st32-fresh); their logs hold no refusal
# line (since DXMTRS C4c-1 and C4c-6a). Also checks d3d9_triangle textures' 39 rows (DXMTRS C4b-3b and C4b-3c's vertex
# textures, tx64-fresh, tx32-fresh), and tx32-fresh the 32-bit-only tex-big256 (the staged-bytes budget); their logs
# hold no refusal line (since DXMTRS C4c-3a, which locks a render-target texture's levels). Also checks d3d9_triangle
# fixed's 31 rows (DXMTRS C4b-3c: the fixed-function vertex stage; fx64-fresh, fx32-fresh); their logs hold exactly its
# refused row's line. Also checks d3d9_triangle targets' 35 rows (DXMTRS C4b-3d1, C4b-3d5's rt-plain, C4c-3a's locks
# and front buffer, C4c-3b's device contexts, additional swap chain and lockable back buffer; tg64-fresh, tg32-fresh);
# their logs hold no refusal line (since DXMTRS C4c-4c). Also checks d3d9_triangle depth's 31 rows (DXMTRS C4b-3d2,
# ds-dynlock C4c-3b's; dp64-fresh, dp32-fresh); their logs hold no refusal line. Also checks d3d9_triangle blend's 36
# rows (DXMTRS C4b-3d3; bl64-fresh, bl32-fresh); their logs hold no refusal line. Also checks d3d9_triangle mrt's 20
# rows (DXMTRS C4b-3d4; mr64-fresh, mr32-fresh); their logs hold no refusal line. Also checks d3d9_triangle msaa's 23
# rows (DXMTRS C4b-3d5, C4c-3a, C4c-4b, C4c-4d, C4d-2a; ms64-fresh, ms32-fresh); their logs hold no refusal line.
# Also checks d3d9_triangle buffers' 39 rows (DXMTRS C4c-1, C4c-2a, C4c-2b; bf64-fresh, bf32-fresh); their logs hold
# exactly its one refusal line (a layout no binding can fetch: buf-wide's attribute wider than its stride), and the line
# logged at the first draw that reads a dynamic buffer in place ("d3d9 core: dynamic buffers are read in place", DXMTRS
# C4c-2b; the row "bf<64|32>-fresh buffers direct"). Also checks d3d9_triangle blit's 41 rows (DXMTRS C4c-4a, C4c-4b,
# C4c-4c, C4c-4d; bt64-fresh, bt32-fresh); their logs hold no refusal line. Also checks d3d9_triangle queries' 12 rows
# (DXMTRS C4c-5; qu64-fresh, qu32-fresh); their logs hold no refusal line. Also checks d3d9_triangle reset's 14 rows
# (DXMTRS C4c-6c; "reset strict", where rs-refuse alone asks for a Reset to an R5G6B5 back buffer; rs64-fresh,
# rs32-fresh); their logs hold exactly its one refusal line. Also checks d3d9_triangle extras' 15 rows (DXMTRS C4c-7a;
# "extras strict", where rz-self and rz-refuse alone run; ex64-fresh, ex32-fresh); their logs hold exactly its one
# refusal line. Also checks d3d9_triangle fullscreen's 9 rows (DXMTRS C4c-6d; fs64-fresh, fs32-fresh); their logs hold
# no refusal line. Also checks d3d9_triangle parity's 11 rows (DXMTRS C4c-7d-1, C4c-7d-2, C4d-2a; pa64-fresh,
# pa32-fresh); their logs hold no refusal line. Also checks d3d9_triangle lost's 2 rows (DXMTRS C4d-2b; lo64-fresh,
# lo32-fresh); their logs hold no refusal line. Also checks d3d9_triangle pace's 3 rows (DXMTRS C4d-4a; pc64-fresh,
# pc32-fresh); their logs hold no refusal line. Also checks d3d9_triangle scale's 7 rows (DXMTRS C4d-4b; sc64-fresh,
# sc32-fresh); their logs hold no refusal line.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B=${BUILD_DIR:-$ROOT/build}
W=$B/vm-work
T=$B/wine-arm64-src/dxmt
S=$W/share/d3d9 G=$W/share/traces/d3d9
fail=0
lane=default
echo "d3d9-gate: lane default (core)"
check() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi; }
# D3D9_GATE_TRACES=<folder>: no build and no run, the checks on a saved run's traces (a copy of $G).
if [ -n "${D3D9_GATE_TRACES:-}" ]; then
  G=$D3D9_GATE_TRACES
else
  rm -rf "$S"; mkdir -p "$S/winetests/x64" "$S/winetests/x86"
  echo "${MN_WOW64:-old}" > "$S/wow64"
  [ "${D3D9_BENCH:-}" != 1 ] || echo 1 > "$S/bench"
  # The x86_64 Linux conformance runner (DXMTRS's tools/linux-cargo.sh, on the builder's x86_64 sysroot via
  # vm/sysroot/fetch.sh, into build/d3d9-cargo/gate-x86_64).
  . "$ROOT/dxmt/pins"
  LM=$(dirname "$(sh "$ROOT/dxmt/toolchain.sh")")
  PATH="$(sh "$ROOT/dxmt/mtld3d-tools.sh"):$PATH"  # cargo +$MTLD3D_RUST
  export PATH MTLD3D_RUST MTLD3D_LLVM_MINGW="$LM"
  MTLD3D_SYSROOT=$(sh "$ROOT/vm/sysroot/fetch.sh" x86_64 2>> "$W/d3d9-gate-build.log") \
    || { echo "FAIL the x86_64 sysroot: see $W/d3d9-gate-build.log"; exit 1; }
  export MTLD3D_SYSROOT
  CARGO_TARGET_DIR=$B/d3d9-cargo/gate-x86_64 MTLD3D_LINUX=x86_64 sh "$T/tools/linux-cargo.sh" build \
    --release -p mtld3d-conformance 2>> "$W/d3d9-gate-build.log" \
    || { echo "FAIL the Linux runner: see $W/d3d9-gate-build.log"; exit 1; }
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
  mkdir -p "$W/d3d9-conf"
  for l in x64 x86; do cp "$G/conf-$l.txt" "$W/d3d9-conf/conf-$l-${MN_WOW64:-old}-$lane.txt" 2> /dev/null || true; done
fi
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
for r in sc64-fresh sc32-fresh; do
  for row in scale-desc scale-draw scale-stretch scale-vpos scale-lod scale-write scale-mfx; do
    check "$r $row" "$(/usr/bin/grep "^d3d9 $row " "$G/$r.txt" 2> /dev/null || echo "no line")" "d3d9 $row ok"
  done
done
for r in st64-fresh st32-fresh; do
  for row in state-xform state-material state-tss state-swapchain state-rt state-tex state-late state-lock state-sb \
    state-upunbind state-fftex state-fftexoff state-getters state-stubs state-present state-ex; do
    check "$r $row" "$(/usr/bin/grep "^d3d9 $row " "$G/$r.txt" 2> /dev/null || echo "no line")" "d3d9 $row ok"
  done
done
# core: runtime <folder> [fixed|buffers|reset|extras]: the core device's line in a launch's logs, none of
# mtld3d.so's encoder line nor of the core's can't-present, dropped-draw or staged-dynamic-buffer ("has no mapped
# memory", DXMTRS C4c-2b) lines (the code that ran prints them, not the switch), and no refusal line, or with
# "fixed", "buffers", "reset" or "extras" exactly that launch's refused rows'
# (fixed_refusals,
# buffers_refusals: DXMTRS C4c-2a's layout no binding can fetch, reset_refusals: DXMTRS C4c-6c's Reset to a
# back-buffer format the core does not make, rs-refuse's under strict alone (its one since DXMTRS C4c-6d, which makes a
# Reset into fullscreen the core's), extras_refusals: DXMTRS C4c-7a's RESZ into a dynamic depth texture, rz-refuse's
# under strict alone).
# The shaders, state, textures, targets, depth, blend, mrt, msaa, blit, queries, fullscreen, parity, lost, pace and scale launches (DXMTRS
# C4b-2, C4b-3a, C4b-3b, C4b-3d1, C4b-3d2, C4b-3d3, C4b-3d4, C4b-3d5, C4c-4a, C4c-5, C4c-6d, C4c-7d-1, C4d-2b, C4d-4a, C4d-4b) take no kind: no refusal
# line (the depth launch's since depth snapshot covers bound-depth sampling, and a dynamic depth texture's lock is the
# core's since DXMTRS C4c-3b; the parity launch's since DXMTRS C4c-7d-1, which makes ProcessVertices the core's; the
# fullscreen launch's since DXMTRS C4c-6d, which makes fullscreen devices and Resets into fullscreen the core's; the
# shaders launch's since DXMTRS C4c-2a, which draws the crossing row; the state launch's since DXMTRS C4c-1: buffer
# locks are the core's, and since
# DXMTRS C4c-6a the getters, the stubs and the presents; the
# textures launch's since DXMTRS C4c-3a: a render-target texture's lock is the core's; the targets launch's since
# DXMTRS C4c-4c: a scaled copy of a multisampled surface is the core's; the msaa launch's since DXMTRS C4c-4b: a
# depth-stencil copy of 4 samples is the core's, and since DXMTRS C4c-4d its resolve; the blit launch's since DXMTRS
# C4c-4d: a depth-stencil resolve is the core's; the queries launch's since DXMTRS C4c-5: CreateQuery is the core's).
refused='d3d9 core: .* is not implemented'
fixed_refusals='IDirect3DDevice9::Draw*PrimitiveUP (a depth texture at a vertex sampler)'
buffers_refusals='IDirect3DDevice9::Draw*Primitive (a layout no binding can fetch)'
reset_refusals='IDirect3DDevice9::Reset (a back-buffer format the core does not make)'
extras_refusals='IDirect3DDevice9::SetRenderState(RESZ) (into a dynamic depth texture)'
runtime() {
  n=$(/usr/bin/grep -rhE "$refused" "$1" 2> /dev/null | wc -l | tr -d ' ')
  [ -n "$(find "$1" -maxdepth 1 -name '*.log' -type f 2> /dev/null | head -1)" ] \
    && /usr/bin/grep -rqF '] d3d9.dll ' "$1" 2> /dev/null \
    && /usr/bin/grep -rqF 'd3d9 core: device on dxmt-core' "$1" 2> /dev/null \
    && ! /usr/bin/grep -rqF 'texture versions: rename pool cap' "$1" 2> /dev/null \
    && ! /usr/bin/grep -rqE 'mtld3d::(unix|shim)|\] mtld3d\.so |\] mtld3d\.dll ' "$1" 2> /dev/null \
    && ! /usr/bin/grep -rqE 'nothing presents|d3d9 core: no Metal view|does not build|colour targets are not its key|depth-stencil is not its key|buffer has no core form|has no mapped memory|was not recorded' "$1" 2> /dev/null \
    && case ${2:-} in
      fixed|buffers|reset|extras)
        case $2 in
          fixed) set_=$fixed_refusals ;; buffers) set_=$buffers_refusals ;;
          reset) set_=$reset_refusals ;; extras) set_=$extras_refusals ;;
        esac
        [ "$n" = "$(printf '%s\n' "$set_" | wc -l | tr -d ' ')" ] && printf '%s\n' "$set_" | while IFS= read -r m; do
          /usr/bin/grep -rqF "d3d9 core: $m is not implemented" "$1" || exit 1; done ;;
      *) [ "$n" = 0 ] ;;
    esac && echo ok
}
for r in tri64-fresh tri32-fresh tri64-updated tri32-updated; do
  check "$r runtime core" "$(runtime "$G/logs/$r")" ok
done
for r in tx64-fresh tx32-fresh; do
  check "$r runtime core" "$(runtime "$G/logs/$r")" ok
done
for r in fx64-fresh fx32-fresh; do
  check "$r runtime core" "$(runtime "$G/logs/$r" fixed)" ok
done
for r in rs64-fresh rs32-fresh; do
  check "$r runtime core" "$(runtime "$G/logs/$r" reset)" ok
done
for r in ex64-fresh ex32-fresh; do
  check "$r runtime core" "$(runtime "$G/logs/$r" extras)" ok
done
for r in bf64-fresh bf32-fresh; do
  check "$r runtime core" "$(runtime "$G/logs/$r" buffers)" ok
  # The line d3d9.dll logs at the first draw that reads a dynamic buffer in place (DXMTRS C4c-2b).
  check "$r buffers direct" \
    "$(/usr/bin/grep -rqF 'd3d9 core: dynamic buffers are read in place' "$G/logs/$r" 2> /dev/null && echo ok)" ok
done
for r in shd64-fresh shd32-fresh st64-fresh st32-fresh tg64-fresh tg32-fresh dp64-fresh dp32-fresh bl64-fresh \
  bl32-fresh mr64-fresh mr32-fresh ms64-fresh ms32-fresh bt64-fresh bt32-fresh qu64-fresh qu32-fresh fs64-fresh \
  fs32-fresh pa64-fresh pa32-fresh lo64-fresh lo32-fresh pc64-fresh pc32-fresh sc64-fresh sc32-fresh; do
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
if [ "${D3D9_BENCH:-}" = 1 ]; then
  rows="bench-32-0 bench-32-600 benchp-32-0 benchp-32-600"
  [ "${MN_WOW64:-old}" = new ] || rows="bench-64-0 bench-64-600 benchp-64-0 benchp-64-600 $rows"
  mkdir -p "$W/d3d9-bench"
  for r in $rows; do
    m=ff; case $r in benchp-*) m=prog ;; esac
    p=$(runtime "$G/logs/$r")
    check "$r bench" "$(/usr/bin/grep -c "^d3d9 bench: mode $m " "$G/$r.txt" 2> /dev/null)/$(/usr/bin/grep -cx 'status 0' "$G/$r.txt" 2> /dev/null)/$p" 1/1/ok
    # A real run's rows only: a trace re-check never rewrites what a run saved.
    [ -n "${D3D9_GATE_TRACES:-}" ] \
      || /usr/bin/grep '^d3d9 bench:' "$G/$r.txt" > "$W/d3d9-bench/$r-${MN_WOW64:-old}-$lane.txt" 2> /dev/null
  done
fi
# Without the markers d3d9.dll's import of winemetal.dll isn't found: the reason run-game.sh updates such a prefix.
for r in tri64-nomarker tri32-nomarker; do
  check "$r fails without the marker" "$(/usr/bin/grep -c '^d3d9 module ok' "$G/$r.txt" 2> /dev/null)" 0
done
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
# New failing sites outside dxmt/mtld3d-triaged.txt (dxmt/mtld3d-conformance.sh's rule), "none" if none.
untriaged() {
  left=$(awk -v triaged="$ROOT/dxmt/mtld3d-triaged.txt" '
    BEGIN { while ((getline l < triaged) > 0) if (l !~ /^#/ && l != "") { split(l, f, " "); ok[f[1] " " f[2]] = 1 } }
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
for l in x64 x86; do
  check "conformance $l runtime core" "$(
    [ -n "$(find "$G/logs/conf-$l" -maxdepth 1 -name '*.log' -type f 2> /dev/null | head -1)" ] \
      && /usr/bin/grep -rqF '] d3d9.dll ' "$G/logs/conf-$l" 2> /dev/null \
      && /usr/bin/grep -rqF 'd3d9 core: device on dxmt-core' "$G/logs/conf-$l" 2> /dev/null \
      && ! /usr/bin/grep -rqF 'texture versions: rename pool cap' "$G/logs/conf-$l" 2> /dev/null \
      && ! /usr/bin/grep -rqE 'mtld3d::(unix|shim)|\] mtld3d\.so |\] mtld3d\.dll ' "$G/logs/conf-$l" 2> /dev/null \
      && ! /usr/bin/grep -rqE "$refused" "$G/logs/conf-$l" 2> /dev/null \
      && ! /usr/bin/grep -rqF 'panicked' "$G/logs/conf-$l" "$G/conf-$l.txt" 2> /dev/null \
      && echo ok
  )" ok
done
[ $fail -eq 0 ] && echo "d3d9 gate: ok" || echo "d3d9 gate: FAIL"
exit $fail
