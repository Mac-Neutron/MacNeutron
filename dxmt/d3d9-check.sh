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
#   d3d9-check.sh [<wine.app>]
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP=$(cd "${1:-$ROOT/build/wine-arm64/wine.app}" && pwd)
EXE="$ROOT/build/dxmt-tests/d3d9_triangle.exe" DYN="$ROOT/build/dxmt-tests/d3d9_dynamic.exe"
ARM="$ROOT/build/dxmt-tests-arm64/d3d9_triangle.exe"
CLI="$ROOT/target/release/macneutron"
for e in "$EXE" "$DYN" "$ARM"; do [ -f "$e" ] || { echo "d3d9-check: no $e: run make dxmt-tests" >&2; exit 2; }; done
[ -x "$CLI" ] || { echo "d3d9-check: no $CLI: run make build" >&2; exit 2; }
WORK=${DXMT_CHECK_WORK:-$(mktemp -d "${TMPDIR:-/tmp}/macneutron-d3d9.XXXXXX")}
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
# launch <tool> <compat> <name> [arg]: d3d9_triangle.exe (or $PROG) [arg] through <tool>'s launcher in the prefix of <compat>,
# killed after 120 s; its lines in $WORK/<name>.txt. MACNEUTRON_VM=0: the macOS path, whatever VM folder this Mac has.
launch() {
  cfg=$MTLD3D_CONFIG
  if [ "$RT" = core ]; then
    mkdir -p "$WORK/mtld3d-logs/$3"
    cfg="$cfg;log.dir=Z:$WORK/mtld3d-logs/$3;runtime.mode=core"
  fi
  env MTLD3D_CONFIG="$cfg" STEAM_COMPAT_DATA_PATH="$WORK/compat/$2" SteamAppId=0 MACNEUTRON_VM=0 MACNEUTRON_GRAPHICS=dxmt \
    MACNEUTRON_NO_STEAM_BRIDGE=1 MACNEUTRON_NO_METALFX=1 \
    "$WORK/$1/bin/macneutron" launch waitforexitandrun "${PROG:-$EXE}" ${4:+"$4"} > "$WORK/$3.out" 2>&1 &
  pid=$!
  ( sleep 120; kill "$pid" 2> /dev/null ) > /dev/null 2>&1 & dog=$!
  wait "$pid" || true
  { kill "$dog" && wait "$dog"; } 2> /dev/null || true
  tr -d '\r' < "$WORK/$3.out" > "$WORK/$3.txt"
  [ "$RT" = core ] || return 0
  # The core lane's proof, from the code that ran: the core device's line in this launch's logs and none of mtld3d.so's
  # encoder line; the native arm64 half has no core runtime (C4-15), so there the fallback warning and the encoder line.
  d="$WORK/mtld3d-logs/$3"
  enc='texture versions: rename pool cap'
  # A core device that cannot present or draw logs one of these and still answers D3D_OK: none may appear.
  unpresented='nothing presents|d3d9 core: no Metal view|does not build'
  if [ "$3" = arm64 ]; then
    if /usr/bin/grep -rqF 'carries no core runtime, so mtld3d runs' "$d" && /usr/bin/grep -rqF "$enc" "$d" \
      && ! /usr/bin/grep -rqF 'd3d9 core: device on dxmt-core' "$d"; then
      echo "d3d9-check: $3: runtime mtld3d (the arm64 fallback)"
    else
      echo "d3d9-check: $3: no arm64 fallback in $d"; fail="$fail $3-runtime"
    fi
  elif [ "$3" = dynamic ]; then
    # d3d9_dynamic.exe makes no device (Direct3DCreate9 through GetProcAddress only): the config's mode, no fallback.
    if /usr/bin/grep -rqF 'config: runtime.mode = core' "$d" && ! /usr/bin/grep -rqF "$enc" "$d" \
      && ! /usr/bin/grep -rqF 'carries no core runtime' "$d"; then
      echo "d3d9-check: $3: runtime core (no device)"
    else
      echo "d3d9-check: $3: no core runtime in $d"; fail="$fail $3-runtime"
    fi
  elif /usr/bin/grep -rqF 'd3d9 core: device on dxmt-core' "$d" && ! /usr/bin/grep -rqF "$enc" "$d" \
    && ! /usr/bin/grep -rqE "$unpresented" "$d"; then
    echo "d3d9-check: $3: runtime core"
  else
    echo "d3d9-check: $3: no core device in $d"; fail="$fail $3-runtime"
  fi
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
launch ours fresh vsync vsync
check vsync vsync
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
