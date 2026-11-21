#!/bin/sh
# Direct3D 9 on the macOS path (D3D9 spec §6, M1): build/dxmt-tests/d3d9_triangle.exe (`make dxmt-tests`) through the
# launcher just built (`make build`), on the tool folder `macneutron install` assembles from <wine.app> (default
# build/wine-arm64/wine.app): its lines "d3d9 module/clear/triangle/present ok" say mtld3d drew it, and "d3d9 imports
# ok" that it redirected the program's EnumDisplaySettingsW import (d3d9 is a static import there). d3d9_dynamic.exe's
# "d3d9 module/imports ok" say a run-time load of mtld3d's d3d9.dll left the program's imports alone. The arm64 build of
# d3d9_triangle (build/dxmt-tests-arm64, rows "arm64: ...") is a native arm64 process: it draws through the ARM64X
# d3d9.dll's native half, which no x64 program reaches (the launcher starts it as it is, Preflight admits arm64).
# Then with vsync ("d3d9 vsync ok": 60 presents at D3DPRESENT_INTERVAL_ONE paced, not waiting out mtld3d's 1 s cap),
# and in the same prefix as another wine.app left it (its stamp another runtime's, mtld3d.dll's marker gone), which the
# launcher's prefix update must give the marker back. With OLD=<another wine.app> the same run again in a prefix that
# wine.app made first (it must lack mtld3d.dll: a player's prefix from before mtld3d). Prints "d3d9-check: ok" or what
# failed; exits 1 on a miss. DXMT_CHECK_WORK names the work folder (default a temporary one). MTLD3D_SO=<mtld3d.so>
# runs that build of D3D9's Unix side instead (the loopback build of D3D9 spec §5.1, dxmt/mtld3d-build.sh --loopback),
# swapped into the tool folder's own wine.app and signed with MACNEUTRON_SIGN_IDENTITY (ad hoc without one).
# MTLD3D_PE=<folder> does the same with its ARM64X d3d9.dll and mtld3d.dll (DXMT's build marks both builtin:
# build/wine-arm64-src/dxmt-install/aarch64-windows): a build of D3D9's PE side checked without rebuilding wine.app.
# Two lanes (C3d-6): D3D9_BACKEND=air (the default lane) sets no shader.backend, so the build's default runs, and
# requires the Unix side's "shader backend: air" line in its logs; D3D9_BACKEND=msl sets shader.backend=msl
# (MTLD3D_CONFIG) and requires "shader backend: msl". Both run the programs from a scratch copy of the folder, so their
# mtld3d_shaders.bin lands in the work folder, and print a lane stamp first. D3D9_KINDS=1 adds the kinds row
# (emit_corpus --kinds, built from build/wine-arm64-src/dxmt, which must match the build): d3d9_triangle is
# fixed-function, so the default lane must show air_ffvs and air_ffps records, and the msl lane ffvs and ffps records
# and no air_* record at all (programmable kinds are dxmt/mtld3d-conformance.sh's). The verdict line is
# "d3d9-check: ok" on the default lane and "d3d9-check: ok (msl)" on the msl lane.
# D3D9_RUNTIME=core (DXMTRS C4a, the default backend lane only) runs every row under runtime.mode=core, D3D9's encoder
# in d3d9.dll on dxmt-core (a wine.app built with DXMT_D3D9_CORE=1): each launch logs to a folder of its own, which
# must hold the core device's line ("d3d9 core: device on dxmt-core") and not mtld3d.so's encoder line ("texture
# versions: rename pool cap", printed by every mtld3d device) nor a line saying the core cannot present (no command
# library, no Metal view, a pipeline that does not build) nor that a dynamic buffer fell back to staging ("has no
# mapped memory", DXMTRS C4c-2b), lines the code that ran prints, not the switch. The
# native arm64 half has no core runtime (C4-15): its launch must hold the fallback warning ("carries no core runtime,
# so mtld3d runs") and the encoder line. No shader-backend, kinds or conformance rows under core (C4b on). Its stamp is
# "d3d9-check: lane core (runtime.mode=core)", its verdict "d3d9-check: ok (core)". D3D9_RUNTIME=mtld3d (the default)
# is the lanes above.
# Every lane also runs d3d9_triangle shaders (DXMTRS C4b-2's 22 shader rows, C4b2-11; "shaders strict" under core: the
# first frame must match), its rows "shaders-<row>". Its launch's logs hold no core refusal line ("... is not
# implemented under runtime.mode = core") in any lane (under core since DXMTRS C4c-2a, which draws the crossing row),
# else "shaders-runtime". Every lane also runs d3d9_triangle state (DXMTRS C4b-3a's 12 rows of API state and inert
# objects and C4c-6a's four of the getters, the stubs, Present's other forms and an extended device: state-getters,
# state-stubs, state-present, state-ex: 16; "state strict" under core), its rows "state-<row>"; its launch's logs hold no
# refusal line in any lane (under core since DXMTRS C4c-1, which made buffer locks the core's, and C4c-6a, which makes
# the four rows' calls the core's). Every lane also runs d3d9_triangle textures (DXMTRS
# C4b-3b's 34 rows of sampled textures and C4b-3c's five vertex-texture rows; "textures strict" under core), its rows
# "textures-<row>"; under core its launch's logs hold no refusal line (since DXMTRS C4c-3a, which locks a render-target
# texture's levels). Every lane also runs d3d9_triangle fixed (DXMTRS C4b-3c's 31 rows of the fixed-function vertex
# stage; "fixed strict" under core), its rows "fixed-<row>"; under core its launch's logs hold exactly its refused rows'
# line (a depth texture at a vertex sampler). Every lane also runs d3d9_triangle targets (DXMTRS C4b-3d1's 22 rows of
# render targets other than the back buffer, C4b-3d5's rt-plain, C4c-3a's four of locks and the front buffer:
# rt-lockable, rt-nolock, rt-bblock, rt-front, and C4c-3b's eight of device contexts, an additional swap chain's back
# buffer and a lockable back buffer: dc-mem, dc-tex, dc-rtex, dc-rt, dc-bb, rt-chain, lb-lock, lb-dc: 35; "targets
# strict" under core), its rows "targets-<row>"; under core its launch's logs hold no refusal line (since DXMTRS
# C4c-4c, which copies rt-msaa's multisampled surface into a 32x32 target). Every lane also runs d3d9_triangle depth
# (DXMTRS C4b-3d2's 31 rows of
# the depth-stencil, on a device with an auto depth-stencil, ds-dynlock C4c-3b's; "depth strict" under core), its rows
# "depth-<row>"; under core its launch's logs hold exactly its one refused call's line (a depth-tested draw sampling
# the bound depth-stencil; a dynamic depth texture's lock is the core's since DXMTRS C4c-3b). Every lane also runs
# d3d9_triangle blend (DXMTRS C4b-3d3's 36 rows of partial clears, the scissor test, a depth-stencil of another size, sRGB write and
# blending, on such a device; "blend strict" under core), its rows "blend-<row>"; under core its launch's logs hold no
# refusal line. Every lane also runs d3d9_triangle mrt (DXMTRS C4b-3d4's 20 rows of render targets at index 1-3, on such
# a device; "mrt strict" under core), its rows "mrt-<row>"; under core its launch's logs hold no refusal line. Every
# lane also runs d3d9_triangle msaa (DXMTRS C4b-3d5's 17 rows of multisampling, C4c-3a's two reads of the back buffer,
# ms-bblock and ms-front, C4c-4b's depth-stencil copy of 4 samples, ms-dcopy, C4c-4d's depth-stencil resolve,
# ms-dres, and C4d-2a's 4x MSAA back-buffer writable LockRect and GetDC, ms-lblock and ms-lbdc: 23, on a device whose
# back buffer and auto depth-stencil have 4 samples; "msaa strict" under core), its rows
# "msaa-<row>"; under core its launch's logs hold no refusal line (since DXMTRS C4c-4b, which copies ms-desc's and
# ms-dcopy's depth-stencil surfaces, and C4c-4d, which resolves ms-dres's). Every
# lane also runs d3d9_triangle buffers (DXMTRS C4c-1's, C4c-2a's and C4c-2b's 39 rows of
# vertex and index buffers and the draws that read them, on a device with no depth-stencil; "buffers strict" under
# core), its rows "buffers-<row>"; under core its launch's logs hold exactly its one refusal line (a layout no binding
# can fetch: buf-wide's attribute wider than its stride) and no line of a draw dropped for a buffer with no core form,
# and the line logged at the first draw that reads a dynamic buffer in place ("d3d9 core: dynamic buffers are read in
# place", DXMTRS C4c-2b), else "buffers-direct". Every lane also runs d3d9_triangle blit (DXMTRS C4c-4a's 17 rows of
# ColorFill, UpdateSurface and StretchRect's answers, C4c-4b's 13 sc-* rows of StretchRect's unscaled copies, C4c-4c's
# 10 sq-* rows of its sampled ones and C4c-4d's depth-stencil resolve, sd-resolve: 41, on a device with no
# depth-stencil; "blit strict" under core), its rows "blit-<row>"; under core its launch's logs hold no refusal line
# (since DXMTRS C4c-4d, which resolves sd-resolve's depth-stencil surfaces). Every lane also runs d3d9_triangle queries
# (DXMTRS C4c-5's 12 rows of EVENT and OCCLUSION queries, on a device with no depth-stencil; "queries strict" under
# core), its rows "queries-<row>"; under core its launch's logs hold no refusal line (since DXMTRS C4c-5, which makes
# CreateQuery the core's). Every lane also runs d3d9_triangle reset (DXMTRS C4c-6c's 14 rows of Reset and ResetEx of a
# windowed device, TestCooperativeLevel and CheckDeviceState, each row on a device and a window of its own; "reset
# strict" under core, where rs-refuse alone asks for a Reset to an R5G6B5 back buffer), its rows "reset-<row>"; under
# core its launch's logs hold exactly its one refusal line (that Reset to a back-buffer format the core does not make,
# refused by name). Every lane also runs d3d9_triangle extras (DXMTRS C4c-7a's 15 rows of the gamma ramp, frame
# latency, the Ex creators, RESZ and the cursor calls, each row on a device and a window of its own; "extras strict"
# under core, where rz-self and rz-refuse alone run), its rows "extras-<row>"; under core its launch's logs hold exactly
# its one refusal line (rz-refuse's RESZ into a dynamic depth texture, refused by name). Every lane also runs
# d3d9_triangle fullscreen (DXMTRS C4c-6d's 9 rows of fullscreen devices, Reset into and out of fullscreen and the
# window subclass, each row on a plain device and a window of its own, every fs-* row FAILing before any device unless
# Wine emulates mode sets for it; "fullscreen strict" under core), its rows
# "fullscreen-<row>"; under core its launch's logs hold no refusal line. Every lane also runs d3d9_triangle parity
# (DXMTRS C4c-7d-1's, C4c-7d-2's and C4d-2a's 11 rows of D3DSAMP_SRGBTEXTURE's low bit, a volume texture of depth 1, a
# managed texture's uploaded levels, ProcessVertices, a one-sample RESZ between two depth formats, the gamma ramp on a
# fullscreen device, a draw that samples its own render target, and zeroed unwritten texture staging, rz-cross and
# gm-full each on a device and a window of its own, gm-full FAILing before any device
# unless Wine emulates mode sets for it; "parity strict" under core), its rows "parity-<row>"; under core its launch's
# logs hold no refusal line. Every lane also runs d3d9_triangle lost (DXMTRS C4d-2b's 2 rows of the device-lost failure
# latch under debug.failNextSubmit=true, lost-plain and lost-ex; "lost strict" under core), its rows "lost-<row>";
# under core its launch's logs hold no refusal line. Every lane also runs d3d9_triangle pace (DXMTRS C4d-4a's 3 rows of
# present pacing and occluded presents under present.maxFps=50;debug.presentOccluded=true: pace-imm, pace-two,
# pace-occ; "pace strict" under core), its rows "pace-<row>"; under core its launch's logs hold no refusal line.
# Every lane also runs d3d9_triangle scale (DXMTRS C4d-4b's 7 rows of render.scale=0.5, render.lodBias=true and
# MetalFX spatial upscaling under render.scale=0.5;render.lodBias=true;debug.presentOccluded=true: scale-desc,
# scale-draw, scale-stretch, scale-vpos, scale-lod, scale-write, scale-mfx; "scale strict" under core), its rows
# "scale-<row>"; under core its launch's logs hold no refusal line and hold "d3d9 core: MetalFX spatial scaler engaged".
#   d3d9-check.sh [<wine.app>]
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP=$(cd "${1:-$ROOT/build/wine-arm64/wine.app}" && pwd)
EXE="$ROOT/build/dxmt-tests/d3d9_triangle.exe" DYN="$ROOT/build/dxmt-tests/d3d9_dynamic.exe"
ARM="$ROOT/build/dxmt-tests-arm64/d3d9_triangle.exe"
CLI="$ROOT/target/release/macneutron"
for e in "$EXE" "$DYN" "$ARM"; do [ -f "$e" ] || { echo "d3d9-check: no $e: run make dxmt-tests" >&2; exit 2; }; done
[ -x "$CLI" ] || { echo "d3d9-check: no $CLI: run make build" >&2; exit 2; }
# The work folder holds a tool install and its prefixes (3.7 GiB a run). One this run made is removed when the run
# ends: all of it after a pass, the install, prefixes and programs after a failure (its logs and row files stay to be
# read). A folder the caller names (DXMT_CHECK_WORK) is the caller's.
made=; [ -n "${DXMT_CHECK_WORK:-}" ] || made=1
WORK=${DXMT_CHECK_WORK:-$(mktemp -d "${TMPDIR:-/tmp}/macneutron-d3d9.XXXXXX")}
trap 's=$?; [ -z "$made" ] || { rm -rf "$WORK/ours" "$WORK/old" "$WORK/compat" "$WORK/progs"; [ "$s" != 0 ] || rm -rf "$WORK"; }' EXIT
fail=
BK=${D3D9_BACKEND:-air} sfx= msl= RT=${D3D9_RUNTIME:-mtld3d}
case $RT/$BK in
  mtld3d/*) ;;
  core/air) sfx=" (core)"; echo "d3d9-check: lane core (runtime.mode=core)" ;;
  core/*) echo "d3d9-check: D3D9_RUNTIME=core takes the default backend lane only" >&2; exit 2 ;;
  *) echo "d3d9-check: D3D9_RUNTIME=$RT: mtld3d or core" >&2; exit 2 ;;
esac
case $BK in
  air) [ "$RT" = core ] || echo "d3d9-check: lane default" ;;
  msl) sfx=" (msl)" msl=1; echo "d3d9-check: lane msl (shader.backend=msl)" ;;
  *) echo "d3d9-check: D3D9_BACKEND=$BK: air or msl" >&2; exit 2 ;;
esac
rm -rf "$WORK/mtld3d-logs" "$WORK/progs"; mkdir -p "$WORK/mtld3d-logs" "$WORK/progs/x64" "$WORK/progs/arm64"
export DXMT_SHADER_CACHE_PATH="${DXMT_SHADER_CACHE_PATH:-$WORK/shader-cache}"
mkdir -p "$DXMT_SHADER_CACHE_PATH"
cp "$EXE" "$DYN" "$WORK/progs/x64/" && cp "$ARM" "$WORK/progs/arm64/" \
  || { echo "d3d9-check: could not copy the programs to $WORK/progs" >&2; exit 1; }
EXE=$WORK/progs/x64/$(basename "$EXE") DYN=$WORK/progs/x64/$(basename "$DYN") ARM=$WORK/progs/arm64/$(basename "$ARM")
export MTLD3D_CONFIG="${MTLD3D_CONFIG:+$MTLD3D_CONFIG;}log.dir=Z:$WORK/mtld3d-logs${msl:+;shader.backend=msl}"
# launch <tool> <compat> <name> [<secs> [arg...]]: d3d9_triangle.exe (or $PROG) [arg...] through <tool>'s launcher in
# the prefix of <compat>, killed after <secs> (default 120); its lines in $WORK/<name>.txt. MACNEUTRON_VM=0: the macOS
# path, whatever VM folder this Mac has.
launch() {
  tool=$1 compat=$2 name=$3 secs=${4:-120}
  shift 3; [ $# -eq 0 ] || shift
  cfg=$MTLD3D_CONFIG
  if [ "$RT" = core ]; then
    mkdir -p "$WORK/mtld3d-logs/$name"
    cfg="$cfg;log.dir=Z:$WORK/mtld3d-logs/$name;runtime.mode=core"
    case " $* " in *" strict "*) cfg="$cfg;shader.asyncCompile=false" ;; esac
  fi
  [ "$name" != lost ] || cfg="$cfg;debug.failNextSubmit=true"
  [ "$name" != pace ] || cfg="$cfg;present.maxFps=50;debug.presentOccluded=true"
  [ "$name" != scale ] || cfg="$cfg;render.scale=0.5;render.lodBias=true;debug.presentOccluded=true"
  env MTLD3D_CONFIG="$cfg" STEAM_COMPAT_DATA_PATH="$WORK/compat/$compat" SteamAppId=0 MACNEUTRON_VM=0 MACNEUTRON_GRAPHICS=dxmt \
    MACNEUTRON_NO_STEAM_BRIDGE=1 MACNEUTRON_NO_METALFX=1 \
    "$WORK/$tool/bin/macneutron" launch waitforexitandrun "${PROG:-$EXE}" "$@" > "$WORK/$name.out" 2>&1 &
  pid=$!
  ( sleep "$secs"; kill "$pid" 2> /dev/null ) > /dev/null 2>&1 & dog=$!
  wait "$pid" || true
  { kill "$dog" && wait "$dog"; } 2> /dev/null || true
  tr -d '\r' < "$WORK/$name.out" > "$WORK/$name.txt"
  [ "$RT" = core ] || return 0
  # The core lane's proof, from the code that ran: the core device's line in this launch's logs and none of mtld3d.so's
  # encoder line; the native arm64 half has no core runtime (C4-15), so there the fallback warning and the encoder line.
  d="$WORK/mtld3d-logs/$name"
  enc='texture versions: rename pool cap'
  # A core device that cannot present or draw logs one of these and still answers D3D_OK: none may appear. "has no
  # mapped memory": a dynamic buffer staged for want of a usable name (DXMTRS C4c-2b). "was not recorded": a copy or a
  # resolve the core did not record, or an occlusion query left open at the device's end (DXMTRS C4c-4b, C4c-5).
  unpresented='nothing presents|d3d9 core: no Metal view|does not build|colour targets are not its key|depth-stencil is not its key|buffer has no core form|has no mapped memory|was not recorded'
  if [ "$name" = arm64 ]; then
    if /usr/bin/grep -rqF 'carries no core runtime, so mtld3d runs' "$d" && /usr/bin/grep -rqF "$enc" "$d" \
      && ! /usr/bin/grep -rqF 'd3d9 core: device on dxmt-core' "$d"; then
      echo "d3d9-check: $name: runtime mtld3d (the arm64 fallback)"
    else
      echo "d3d9-check: $name: no arm64 fallback in $d"; fail="$fail $name-runtime"
    fi
  elif [ "$name" = dynamic ]; then
    # d3d9_dynamic.exe makes no device (Direct3DCreate9 through GetProcAddress only): the config's mode, no fallback.
    if /usr/bin/grep -rqF 'config: runtime.mode = core' "$d" && ! /usr/bin/grep -rqF "$enc" "$d" \
      && ! /usr/bin/grep -rqF 'carries no core runtime' "$d"; then
      echo "d3d9-check: $name: runtime core (no device)"
    else
      echo "d3d9-check: $name: no core runtime in $d"; fail="$fail $name-runtime"
    fi
  elif /usr/bin/grep -rqF 'd3d9 core: device on dxmt-core' "$d" && ! /usr/bin/grep -rqF "$enc" "$d" \
    && ! /usr/bin/grep -rqE "$unpresented" "$d" && refusals_ok "$name" "$d"; then
    echo "d3d9-check: $name: runtime core"
  else
    echo "d3d9-check: $name: no core device, or a refusal or can't-present line, in $d"; fail="$fail $name-runtime"
    /usr/bin/grep -rhE "$refused|$unpresented" "$d" | head -5 | sed "s|^|d3d9-check: $name: |" || true
  fi
}
# refusals_ok <name> <folder>: the fixed launch's logs hold exactly its one core refusal line (DXMTRS C4b-3c: a depth
# texture at a vertex sampler), the depth launch's exactly its one (DXMTRS C4b-3d2: a depth-tested draw sampling the
# bound depth-stencil; a dynamic depth texture's LockRect is the core's since DXMTRS C4c-3b); the buffers launch's
# exactly its one (DXMTRS C4c-2a: a layout no binding can fetch); the reset launch's exactly its one (DXMTRS C4c-6c: a
# Reset to a back-buffer format the core does not make, which rs-refuse asks for under strict alone; its one since
# DXMTRS C4c-6d, which makes a Reset into fullscreen the core's); the extras launch's exactly its one (DXMTRS C4c-7a:
# a RESZ into a dynamic depth texture, which rz-refuse asks for under strict alone); the shaders, state, textures,
# targets, blend, mrt, msaa, blit, queries, fullscreen, parity, lost, pace and scale launches' none (the scale launch's
# since DXMTRS C4d-4b, which wires core render.scale, render.lodBias and MetalFX spatial upscaling; the pace launch's
# since DXMTRS C4d-4a, which wires core pacing and occluded presents; the lost launch's since DXMTRS
# C4d-2b, which wires the core device-lost latch; the parity launch's since DXMTRS
# C4c-7d-1, which makes ProcessVertices the core's; the fullscreen launch's since DXMTRS C4c-6d,
# which makes fullscreen devices and Resets into fullscreen the core's; the shaders launch's since DXMTRS C4c-2a,
# which draws the crossing row; the state launch's since DXMTRS C4c-1: buffer locks are the core's, and since DXMTRS
# C4c-6a the getters, the stubs and the presents; the textures launch's since DXMTRS C4c-3a: a
# render-target texture's lock is the core's; the targets launch's since DXMTRS C4c-4c: a scaled copy of a
# multisampled surface is the core's; the msaa launch's since DXMTRS C4c-4b: a depth-stencil copy of 4 samples is the
# core's, and since DXMTRS C4c-4d its resolve; the blit launch's since DXMTRS C4c-4d: a depth-stencil resolve is the
# core's; the queries launch's since DXMTRS C4c-5: CreateQuery is the core's); the other launches are not held to it.
refused='is not implemented under runtime.mode = core'
fixed_refusals='IDirect3DDevice9::Draw*PrimitiveUP (a depth texture at a vertex sampler)'
depth_refusals='IDirect3DDevice9::Draw*PrimitiveUP (the depth-stencil sampled while bound)'
buffers_refusals='IDirect3DDevice9::Draw*Primitive (a layout no binding can fetch)'
reset_refusals='IDirect3DDevice9::Reset (a back-buffer format the core does not make)'
extras_refusals='IDirect3DDevice9::SetRenderState(RESZ) (into a dynamic depth texture)'
refusals_ok() {
  case $1 in
    fixed|depth|buffers|reset|extras)
      case $1 in
        fixed) set_=$fixed_refusals ;; depth) set_=$depth_refusals ;; buffers) set_=$buffers_refusals ;;
        reset) set_=$reset_refusals ;; extras) set_=$extras_refusals ;;
      esac
      [ "$(/usr/bin/grep -rhF "$refused" "$2" | wc -l | tr -d ' ')" = "$(printf '%s\n' "$set_" | wc -l | tr -d ' ')" ] \
        || return 1
      printf '%s\n' "$set_" | while IFS= read -r n; do
        /usr/bin/grep -rqF "d3d9 core: $n $refused" "$2" || exit 1
      done ;;
    shaders|state|textures|targets|blend|mrt|msaa|blit|queries|fullscreen|parity|lost|pace|scale|cache-cold|cache-warm)
      [ "$(/usr/bin/grep -rhF "$refused" "$2" | wc -l | tr -d ' ')" = 0 ] ;;
    *) return 0 ;;
  esac
}
# check <name> [rows]: the rows (default the four) ok, else each missing one named.
check() {
  for r in ${2:-module clear triangle present}; do
    got=$(/usr/bin/grep "^d3d9 $r " "$WORK/$1.txt" || echo "d3d9 $r: no line")
    echo "d3d9-check: $1: $got"
    [ "$got" = "d3d9 $r ok" ] || fail="$fail $1-$r"
  done
}
rm -rf "$WORK/ours" "$WORK/compat"; mkdir -p "$WORK/compat"
"$CLI" install --tool-dir "$WORK/ours" --wine-app "$APP" > /dev/null || { echo "d3d9-check: install failed" >&2; exit 1; }
if [ -n "${MTLD3D_SO:-}" ]; then
  so="$WORK/ours/wine.app/Contents/Resources/lib/wine/aarch64-unix/mtld3d.so"
  rm -f "$so" && cp "$MTLD3D_SO" "$so" && codesign -f -s "${MACNEUTRON_SIGN_IDENTITY:--}" "$so" 2> /dev/null \
    || { echo "d3d9-check: could not put $MTLD3D_SO in the tool folder" >&2; exit 1; }
fi
for dll in d3d9.dll mtld3d.dll; do
  [ -n "${MTLD3D_PE:-}" ] || break
  pe="$WORK/ours/wine.app/Contents/Resources/lib/wine/aarch64-windows/$dll"
  rm -f "$pe" && cp "$MTLD3D_PE/$dll" "$pe" || { echo "d3d9-check: could not put $MTLD3D_PE/$dll in the tool folder" >&2; exit 1; }
done
launch ours fresh fresh
check fresh "module clear triangle present imports"
PROG=$DYN; launch ours fresh dynamic; PROG=  # (an assignment before a function call outlives it in sh)
check dynamic "module imports"
launch ours fresh vsync 120 vsync
check vsync vsync
strict=; [ "$RT" != core ] || strict=strict  # the core compiles asynchronously by default since C4d-3; strict launches pass shader.asyncCompile=false
launch ours fresh shaders 300 shaders $strict
check shaders "caps vs1 vs2 vs3 consti constb rel decl indexed fan bypass vsff alpha fog mask wire cull viewport clip
  srgb crossing getters"
launch ours fresh state 120 state $strict
check state "state-xform state-material state-tss state-swapchain state-rt state-tex state-late state-lock state-sb
  state-upunbind state-fftex state-fftexoff state-getters state-stubs state-present state-ex"
launch ours fresh textures 240 textures $strict
check textures "tex-ps2d tex-ff tex-cascade tex-cube tex-volume tex-mips tex-address tex-filter tex-srgb tex-dxt1
  tex-l8 tex-a8 tex-a8l8 tex-r5g6b5 tex-a4r4g4b4 tex-r8g8b8 tex-lock tex-twodraws tex-dynamic tex-levelsurf tex-genmips
  tex-update tex-autogen tex-black tex-bumpenv tex-texbem tex-big tex-release tex-depthtex tex-fetch4 tex-fetch4a tex-rttex
  tex-vtx   tex-rtlock tex-vtxlod tex-vtxblack tex-vtxcube tex-vtxvolume tex-vtxsrgb"
launch ours fresh fixed 240 fixed $strict
check fixed "ff-xform ff-persp ff-rhwmix ff-viewchange ff-ambient ff-lightdefault ff-lightdir ff-lightpoint ff-lightspot
  ff-lightsparse ff-light8 ff-specular ff-matsrc ff-normalize ff-fogvertex ff-fogtable ff-texgen ff-tt ff-ttproj
  ff-tcindex ff-tex ff-blend ff-blendidx ff-point ff-pointscale ff-clip ff-indexed ff-decl tex-vtxrt tex-vtxdepth
  tex-vtxdepthfmt"
launch ours fresh targets 240 targets $strict
check targets "rt-tex rt-read rt-surface rt-viewport rt-big rt-mip rt-cube rt-switch rt-rewrite rt-clearonly rt-fresh
  rt-autogen rt-release rt-srgb rt-ps rt-update rt-updatebound rt-formats rt-mrt rt-msaa rt-plain rt-cubelock rt-huge
  rt-lockable rt-nolock rt-bblock rt-front dc-mem dc-tex dc-rtex dc-rt dc-bb rt-chain lb-lock lb-dc"
launch ours fresh depth 240 depth $strict
check depth "ds-default ds-test ds-func ds-write ds-zenable ds-clearz ds-clearonly ds-stencil ds-twosided ds-bias
  ds-slope ds-clamp ds-odepth ds-surface ds-nostencil ds-unbound ds-persist ds-texture ds-texstencil ds-mip ds-release
  ds-shadow ds-intz ds-df ds-fetch4 ds-size ds-rhwclip ds-feedback ds-msaa ds-huge ds-dynlock"
launch ours fresh blend 240 blend $strict
check blend "cl-rects cl-count cl-scissor cl-viewport cl-z cl-stencil cl-mask cl-order cl-level cl-bigz cl-smallz
  sc-rect sc-off sc-empty sc-clamp sc-pass sc-break sz-large sz-clear sz-small sr-clear sr-blend sr-toggle bl-alpha
  bl-add bl-ops bl-factors bl-both bl-destalpha bl-separate bl-factor bl-stencil bl-off bl-mask bl-prog bl-pass"
launch ours fresh mrt 240 mrt $strict
check mrt "mrt-two mrt-four mrt-hole mrt-unwritten mrt-nobind mrt-clear mrt-mask mrt-blend mrt-destalpha mrt-formats
  mrt-srgb mrt-srgbclear mrt-size mrt-switch mrt-scissor mrt-depth mrt-back mrt-autogen mrt-order mrt-read"
launch ours fresh msaa 240 msaa $strict
check msaa "ms-initial ms-desc ms-dcopy ms-dres ms-resolve ms-edge ms-back ms-clear ms-depth ms-mismatch ms-mask
  ms-nonmask ms-a2c ms-srgbmask ms-blend ms-mrt ms-scissor ms-redraw ms-samples ms-bblock ms-front ms-lblock ms-lbdc"
launch ours fresh buffers 240 buffers $strict
check buffers "buf-d3d buf-lock buf-draw buf-start buf-offset buf-strip buf-fan buf-indexed buf-index32 buf-base buf-ifan
  buf-managed buf-dynamic buf-order buf-discard buf-held buf-relock buf-streams buf-null buf-null-up buf-zero-stride
  buf-instanced buf-shader buf-tex buf-nobuffer buf-release buf-odd buf-cross buf-cross-up buf-cross-multi
  buf-cross-odd buf-cross-indexed buf-wide buf-dyn-preserve buf-dyn-many buf-dyn-index buf-dyn-read buf-dyn-cross
  buf-writeonly"
# Under core: the line d3d9-rt logs at the first draw that reads a dynamic buffer in place (DXMTRS C4c-2b).
if [ "$RT" = core ]; then
  if /usr/bin/grep -rqF 'd3d9 core: dynamic buffers are read in place' "$WORK/mtld3d-logs/buffers"; then
    echo "d3d9-check: buffers: dynamic buffers are read in place"
  else
    echo "d3d9-check: buffers: no 'dynamic buffers are read in place' line in $WORK/mtld3d-logs/buffers"
    fail="$fail buffers-direct"
  fi
fi
launch ours fresh blit 240 blit $strict
check blit "cf-rt cf-lockable cf-bb cf-unbound cf-rtex cf-autogen cf-msaa cf-plain cf-reject us-tex us-level us-rtex
  us-rt us-bb us-conv us-reject sr-reject sd-resolve sc-rt sc-bb sc-lockable sc-rtex sc-tex sc-plain sc-plain2 sc-conv
  sc-same sc-self sc-autogen sc-order sq-scale sq-level sq-conv sq-swizzle sq-yuv sq-ms sq-overlap sq-plain sq-order
  sq-autogen sc-depth"
launch ours fresh queries 240 queries $strict
check queries "qy-support qy-event qy-count qy-empty qy-open qy-twice qy-present qy-two qy-pass qy-release qy-reuse
  qy-noflush"
launch ours fresh reset 300 reset $strict
check reset "rs-size rs-depth rs-msaa rs-blockers rs-state rs-autogen rs-scene rs-invalid rs-refuse rs-window rs-ex
  rs-held rs-query rs-twice"
launch ours fresh extras 300 extras $strict
check extras "gm-ramp fl-latency ex-rt ex-ds ex-plain rz-msaa rz-auto rz-single rz-plain rz-edges rz-self rz-refuse
  cu-props cu-show cu-setcursor"
launch ours fresh fullscreen 300 fullscreen $strict
check fullscreen "fs-create fs-cover fs-reject fs-reset-in fs-reset-window fs-wndproc fs-reset-mode fs-activate ws-size"
launch ours fresh parity 300 parity $strict
check parity "sr-lsb vo-one dr-keep pv-sysmem pv-draw pv-dynsrc pv-dyndst rz-cross gm-full tb-self tex-unwritten"
launch ours fresh lost 180 lost $strict
check lost "lost-plain lost-ex"
launch ours fresh pace 120 pace $strict maxfps50
check pace "pace-imm pace-two pace-occ"
launch ours fresh scale 180 scale $strict
check scale "scale-desc scale-draw scale-stretch scale-vpos scale-lod scale-write scale-mfx"
if [ "$RT" = core ]; then
  if /usr/bin/grep -rqF 'd3d9 core: MetalFX spatial scaler engaged' "$WORK/mtld3d-logs/scale"; then
    echo "d3d9-check: scale: MetalFX spatial scaler engaged"
  else
    echo "d3d9-check: scale: no 'd3d9 core: MetalFX spatial scaler engaged' line in $WORK/mtld3d-logs/scale"
    fail="$fail scale-mfx-log"
  fi
fi
if [ "$RT" != core ] && /usr/bin/grep -rqF "$refused" "$WORK/mtld3d-logs"; then
  echo "d3d9-check: shaders, state: a core refusal line in an mtld3d lane's logs"; fail="$fail shaders-runtime"
fi
PROG=$ARM; launch ours fresh arm64; PROG=
check arm64
P="$WORK/compat/fresh/pfx/drive_c/windows/system32/mtld3d.dll"
rm -f "$P" && echo "wine.app another msync=0" > "$WORK/compat/fresh/version"
launch ours fresh stale
check stale
[ -f "$P" ] || { echo "d3d9-check: stale: the prefix update wrote no mtld3d.dll marker"; fail="$fail stale-marker"; }
if [ -n "${OLD:-}" ]; then
  rm -rf "$WORK/old"
  "$CLI" install --tool-dir "$WORK/old" --wine-app "$(cd "$OLD" && pwd)" > /dev/null \
    || { echo "d3d9-check: install of OLD failed" >&2; exit 1; }
  env STEAM_COMPAT_DATA_PATH="$WORK/compat/old" SteamAppId=0 "$WORK/old/bin/macneutron" launch getcompatpath "$WORK" \
    > /dev/null 2>&1 || { echo "d3d9-check: OLD made no prefix" >&2; exit 1; }
  [ ! -f "$WORK/compat/old/pfx/drive_c/windows/system32/mtld3d.dll" ] \
    || { echo "d3d9-check: OLD's prefix has mtld3d.dll already: OLD is no wine.app from before mtld3d" >&2; exit 1; }
  launch ours old updated
  check updated
fi
if [ "$RT" = core ]; then
  # Core SQLite shader cache + async compile + real D3D11 single-table coexistence check (DXMTRS C4d-3).
  rm -rf "$WORK/cache-coexist"; mkdir -p "$WORK/cache-coexist"
  _saved_cache_path=$DXMT_SHADER_CACHE_PATH
  _saved_mtld3d_cfg=$MTLD3D_CONFIG
  export DXMT_SHADER_CACHE_PATH="$WORK/cache-coexist"
  export DXMT_CACHE_EXE="d3d9_coexist"
  MTLD3D_CONFIG="$_saved_mtld3d_cfg;shader.asyncCompile=true;shaderCache.enable=true"

  shader_rows="caps vs1 vs2 vs3 consti constb rel decl indexed fan bypass vsff alpha fog mask wire cull viewport clip
    srgb crossing getters"
  launch ours fresh cache-cold 300 shaders
  check cache-cold "$shader_rows"
  db=$(find "$WORK/cache-coexist" -maxdepth 2 -name 'shaders_*.db' 2> /dev/null | head -1)
  cold_tables=
  if [ -z "$db" ]; then
    echo "d3d9-check: cache-coexist: no shaders_*.db in $WORK/cache-coexist"
    fail="$fail cache-cold-db"
  else
    cold_tables=$(sqlite3 "$db" "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'cache_%';" 2> /dev/null || true)
    cold_count=$(printf '%s\n' "$cold_tables" | sed '/^$/d' | wc -l | tr -d ' ')
    echo "d3d9-check: cache-coexist cold: db=$(basename "$db") tables=${cold_tables:-none} count=$cold_count"
    if [ "$cold_count" != 1 ] || [ "$cold_tables" = "cache_5555813613417847110" ] || [ "$cold_tables" = "cache_10131673474557754031" ]; then
      echo "d3d9-check: cache-coexist: expected 1 non-dev cache_* table, got '$cold_tables'"
      fail="$fail cache-cold-table"
    fi
  fi
  cold_sum=$(/usr/bin/grep -rhF 'd3d9 core: shader cache summary:' "$WORK/mtld3d-logs/cache-cold" 2> /dev/null | tail -1 || true)
  echo "d3d9-check: cache-cold summary: ${cold_sum:-none}"
  if ! printf '%s\n' "$cold_sum" | /usr/bin/grep -qE '(^|[ ,])(cache_)?misses=[1-9]' \
    || ! printf '%s\n' "$cold_sum" | /usr/bin/grep -qE '(^|[ ,])recipes_saved=[1-9]'; then
    echo "d3d9-check: cache-cold: missing misses>=1 or recipes_saved>=1 in summary"
    fail="$fail cache-cold-summary"
  fi

  if [ ! -f "$ROOT/build/dxmt-tests/d3d11_draw.exe" ] || [ ! -d "$ROOT/dxmt/tests/shaders/d3d11" ]; then
    echo "d3d9-check: cache-coexist: missing d3d11_draw.exe or shaders/d3d11"
    fail="$fail cache-coexist-d3d11"
  elif [ -n "$db" ]; then
    cold_rows=$(sqlite3 "$db" "SELECT COUNT(*) FROM \"$cold_tables\";" 2> /dev/null || echo 0)
    env DXMT_SHADER_CACHE_PATH="$WORK/cache-coexist" DXMT_CACHE_EXE="d3d9_coexist" \
      STEAM_COMPAT_DATA_PATH="$WORK/compat/fresh" SteamAppId=0 MACNEUTRON_VM=0 MACNEUTRON_GRAPHICS=dxmt \
      MACNEUTRON_NO_STEAM_BRIDGE=1 MACNEUTRON_NO_METALFX=1 \
      "$WORK/ours/bin/macneutron" launch waitforexitandrun "$ROOT/build/dxmt-tests/d3d11_draw.exe" \
      "Z:$ROOT/dxmt/tests/shaders/d3d11" < /dev/null > "$WORK/coexist-d3d11.out" 2>&1 &
    dpid=$!
    ( sleep 120; kill "$dpid" 2> /dev/null ) > /dev/null 2>&1 & ddog=$!
    drc=0; wait "$dpid" || drc=$?
    { kill "$ddog" && wait "$ddog"; } 2> /dev/null || true
    tr -d '\r' < "$WORK/coexist-d3d11.out" > "$WORK/coexist-d3d11.txt"
    db_count=$(find "$WORK/cache-coexist" -maxdepth 2 -name 'shaders_*.db' 2> /dev/null | wc -l | tr -d ' ')
    d11_tables=$(sqlite3 "$db" "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'cache_%';" 2> /dev/null || true)
    d11_count=$(printf '%s\n' "$d11_tables" | sed '/^$/d' | wc -l | tr -d ' ')
    d11_rows=$(sqlite3 "$db" "SELECT COUNT(*) FROM \"$cold_tables\";" 2> /dev/null || echo 0)
    echo "d3d9-check: cache-coexist d3d11: rc=$drc db_count=$db_count tables=${d11_tables:-none} count=$d11_count rows=${cold_rows}->${d11_rows}"
    if [ "$drc" != 0 ] || [ "$db_count" != 1 ] || [ "$d11_count" != 1 ] || [ "$d11_tables" != "$cold_tables" ] || [ "$d11_rows" -le "$cold_rows" ]; then
      echo "d3d9-check: cache-coexist: D3D11 failed, created extra DB, changed table, or added no rows"
      fail="$fail cache-coexist-d3d11"
    fi
  fi

  launch ours fresh cache-warm 300 shaders
  check cache-warm "$shader_rows"
  warm_sum=$(/usr/bin/grep -rhF 'd3d9 core: shader cache summary:' "$WORK/mtld3d-logs/cache-warm" 2> /dev/null | tail -1 || true)
  echo "d3d9-check: cache-warm summary: ${warm_sum:-none}"
  if ! printf '%s\n' "$warm_sum" | /usr/bin/grep -qE '(^|[ ,])(cache_)?hits=[1-9]' \
    || ! printf '%s\n' "$warm_sum" | /usr/bin/grep -qE '(^|[ ,])(cache_)?misses=0([^0-9]|$)' \
    || ! printf '%s\n' "$warm_sum" | /usr/bin/grep -qE '(^|[ ,])recipes_loaded=[1-9]'; then
    echo "d3d9-check: cache-warm: missing hits>=1, misses=0, or recipes_loaded>=1 in summary"
    fail="$fail cache-warm-summary"
  fi

  export DXMT_SHADER_CACHE_PATH="$_saved_cache_path"
  unset DXMT_CACHE_EXE
  MTLD3D_CONFIG="$_saved_mtld3d_cfg"
elif [ "$BK" = air ]; then
  /usr/bin/grep -rqE 'shader backend: air, translator [0-9a-f]{8}' "$WORK/mtld3d-logs" \
    || { echo "d3d9-check: no 'shader backend: air' line in $WORK/mtld3d-logs"; fail="$fail air-log"; }
else
  /usr/bin/grep -rqE 'shader backend: msl, translator ' "$WORK/mtld3d-logs" \
    || { echo "d3d9-check: no 'shader backend: msl' line in $WORK/mtld3d-logs"; fail="$fail msl-log"; }
fi
if [ "${D3D9_KINDS:-0}" = 1 ] && [ "$RT" != core ]; then
  . "$ROOT/dxmt/pins"
  line=$(cd "$ROOT/build/wine-arm64-src/dxmt" && PATH="$(sh "$ROOT/dxmt/mtld3d-tools.sh"):$PATH" MACOSX_DEPLOYMENT_TARGET=27.0 \
    CARGO_TARGET_DIR="$ROOT/build/d3d9-cargo/kinds" cargo "+$MTLD3D_RUST" run -q --release -p mtld3d-core \
    --features disk-cache --example emit_corpus -- --kinds "$WORK"/progs/*/mtld3d_shaders.bin 2> /dev/null \
    | sed -n '/^kinds /p') || line=
  echo "d3d9-check: $BK $line"
  if [ "$BK" = air ]; then
    miss=$(printf '%s\n' "$line" | tr ' ' '\n' | awk -F= '
      /^(air_)?(sm[0-9]|ff)(vs|ps)=/ { k = $1; sub(/^air_/, "", k); seen[k] = 1; if ($1 ~ /^air_/) { a[k] = $2; t += $2; if (k ~ /^ff/) af[k] = $2 } else m[k] = $2 }
      END { for (k in seen) if (m[k] + a[k] > 0 && a[k] < 1) printf "%s ", k; if (t < 1) printf "no-air-records "; if (af["ffvs"] < 1 || af["ffps"] < 1) printf "no-air-ff-records " }')
    kf=air-kinds
  else
    miss=$(printf '%s\n' "$line" | tr ' ' '\n' | awk -F= '
      /^air_(sm[0-9]|ff)(vs|ps)=/ { t += $2 } /^ff(vs|ps)=/ { m[$1] = $2 }
      END { if (t > 0) printf "air-records-under-msl "; if (m["ffvs"] < 1 || m["ffps"] < 1) printf "no-msl-records " }')
    kf=msl-kinds
  fi
  if [ -z "$line" ]; then fail="$fail $kf"; elif [ -n "$miss" ]; then echo "d3d9-check: kinds: $miss"; fail="$fail $kf $miss"; fi
fi
[ -z "$fail" ] && echo "d3d9-check: ok$sfx" || { echo "d3d9-check: FAIL$fail"; exit 1; }
