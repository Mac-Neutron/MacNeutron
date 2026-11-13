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
# library, no Metal view, a pipeline that does not build), lines the code that ran prints, not the switch. The
# native arm64 half has no core runtime (C4-15): its launch must hold the fallback warning ("carries no core runtime,
# so mtld3d runs") and the encoder line. No shader-backend, kinds or conformance rows under core (C4b on). Its stamp is
# "d3d9-check: lane core (runtime.mode=core)", its verdict "d3d9-check: ok (core)". D3D9_RUNTIME=mtld3d (the default)
# is the lanes above.
# Every lane also runs d3d9_triangle shaders (DXMTRS C4b-2's 22 shader rows, C4b2-11; "shaders strict" under core:
# the first frame must match), its rows "shaders-<row>". Its launch's logs hold no core refusal line ("... is not
# implemented under runtime.mode = core") in the mtld3d lanes, and under core exactly one, the crossing row's (C4b2-8:
# its name says "crossing"), else "shaders-runtime". Every lane also runs d3d9_triangle state (DXMTRS C4b-3a's 12 rows
# of API state and inert objects; "state strict" under core), its rows "state-<row>"; under core its launch's logs hold
# exactly its refusal line (refusals_ok), in the mtld3d lanes none. Every lane also runs d3d9_triangle textures (DXMTRS
# C4b-3b's 34 rows of sampled textures and C4b-3c's five vertex-texture rows; "textures strict" under core), its rows
# "textures-<row>"; under core its launch's logs hold exactly its refused row's line. Every lane also runs
# d3d9_triangle fixed (DXMTRS C4b-3c's 31 rows of the fixed-function vertex stage; "fixed strict" under core), its rows
# "fixed-<row>"; under core its launch's logs hold exactly its refused rows' line (a depth texture at a vertex
# sampler). Every lane also runs d3d9_triangle targets (DXMTRS C4b-3d1's 22 rows of render targets other than the back
# buffer; "targets strict" under core), its rows "targets-<row>"; under core its launch's logs hold exactly its two
# refused rows' lines (a render target at index 1, a multisampled surface). Every lane also runs d3d9_triangle depth
# (DXMTRS C4b-3d2's 30 rows of the depth-stencil, on a device with an auto depth-stencil; "depth strict" under core),
# its rows "depth-<row>"; under core its launch's logs hold exactly its five refused calls' lines.
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
  fi
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
  # A core device that cannot present or draw logs one of these and still answers D3D_OK: none may appear.
  unpresented='nothing presents|d3d9 core: no Metal view|does not build'
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
# refusals_ok <name> <folder>: the shaders launch's logs hold exactly one core refusal line, the crossing row's
# (C4b2-8); the state launch's exactly its buffer lock's (DXMTRS C4b-3a); the textures launch's exactly its one
# (DXMTRS C4b-3b: a render-target texture's LockRect), the fixed launch's exactly its one (DXMTRS C4b-3c: a depth
# texture at a vertex sampler), the targets launch's exactly its two (DXMTRS C4b-3d1: a render target at index 1, a
# multisampled surface), the depth launch's exactly its five (DXMTRS C4b-3d2: a depth-tested draw and a Z Clear with
# a depth-stencil of another size than the target, a depth-tested draw sampling the bound depth-stencil, a
# multisampled depth-stencil surface, a dynamic depth texture's LockRect), one line each; the other launches are not
# held to it.
refused='is not implemented under runtime.mode = core'
state_refusals='IDirect3DVertexBuffer9::Lock'
texture_refusals='IDirect3DTexture9::LockRect (a render-target or depth texture)'
fixed_refusals='IDirect3DDevice9::Draw*PrimitiveUP (a depth texture at a vertex sampler)'
targets_refusals='IDirect3DDevice9::SetRenderTarget (index above 0)
IDirect3DDevice9::CreateRenderTarget (multisampled)'
depth_refusals='IDirect3DDevice9::Draw*PrimitiveUP (a depth-stencil of another size than the target)
IDirect3DDevice9::Clear (a depth-stencil of another size than the target)
IDirect3DDevice9::Draw*PrimitiveUP (the depth-stencil sampled while bound)
IDirect3DDevice9::CreateDepthStencilSurface (multisampled)
IDirect3DTexture9::LockRect (a render-target or depth texture)'
refusals_ok() {
  case $1 in
    shaders)
      [ "$(/usr/bin/grep -rhF "$refused" "$2" | wc -l | tr -d ' ')" = 1 ] \
        && /usr/bin/grep -rhF "$refused" "$2" | /usr/bin/grep -qi crossing ;;
    state|textures|fixed|targets|depth)
      case $1 in
        state) set_=$state_refusals ;; textures) set_=$texture_refusals ;; fixed) set_=$fixed_refusals ;;
        targets) set_=$targets_refusals ;; depth) set_=$depth_refusals ;;
      esac
      [ "$(/usr/bin/grep -rhF "$refused" "$2" | wc -l | tr -d ' ')" = "$(printf '%s\n' "$set_" | wc -l | tr -d ' ')" ] \
        || return 1
      printf '%s\n' "$set_" | while IFS= read -r n; do
        /usr/bin/grep -rqF "d3d9 core: $n $refused" "$2" || exit 1
      done ;;
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
strict=; [ "$RT" != core ] || strict=strict  # the core compiles synchronously: the first frame must match
launch ours fresh shaders 300 shaders $strict
check shaders "caps vs1 vs2 vs3 consti constb rel decl indexed fan bypass vsff alpha fog mask wire cull viewport clip
  srgb crossing getters"
launch ours fresh state 120 state $strict
check state "state-xform state-material state-tss state-swapchain state-rt state-tex state-late state-lock state-sb
  state-upunbind state-fftex state-fftexoff"
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
  rt-autogen rt-release rt-srgb rt-ps rt-update rt-updatebound rt-formats rt-mrt rt-msaa rt-cubelock rt-huge"
launch ours fresh depth 240 depth $strict
check depth "ds-default ds-test ds-func ds-write ds-zenable ds-clearz ds-clearonly ds-stencil ds-twosided ds-bias
  ds-slope ds-clamp ds-odepth ds-surface ds-nostencil ds-unbound ds-persist ds-texture ds-texstencil ds-mip ds-release
  ds-shadow ds-intz ds-df ds-fetch4 ds-size ds-feedback ds-msaa ds-huge ds-lock"
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
if [ "$RT" = core ]; then :  # no shader-backend row: the core has none in C4a
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
