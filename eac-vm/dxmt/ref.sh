#!/bin/sh
# ref.sh: the 4b-2 gate's macOS reference. Each program in the share's invocations (share.sh) runs twice through
# MacNeutron's launcher, as dxmt/check.sh's `run ours` does: a tool folder `macneutron install` assembles from
# build/wine-arm64/wine.app in ${TMPDIR}/macneutron dxmt-guest-ref (never a user's; Steam is never started), check.sh's
# environment, a fresh shader cache per run, the share's timeout as a watchdog. Writes
# build/eac-vm-work/share/dxmt-ref/<name>.<1|2>.txt, .status (the launcher's exit status, or "timeout") and .time.
# The 4c gate (present-gate.sh) sets MN_REF_INVOCATIONS (its own programs), MN_REF_OUT and MN_REF_RUNS=1.
# MN_REF_WINE_APP=<wine.app> runs that runtime instead of build/wine-arm64/wine.app (a saved one, to re-record a
# reference after the C is replaced); the gates skip this script altogether with MN_REF_FROZEN=<folder>.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK="${TMPDIR:-/tmp}/macneutron dxmt-guest-ref"
OUT=${MN_REF_OUT:-${BUILD_DIR:-$ROOT/build}/eac-vm-work/share/dxmt-ref}
T=$ROOT/dxmt/tests
I=${BUILD_DIR:-$ROOT/build}/eac-vm-work/share/dxmt-tests
TO=$(cat "$I/timeout")
now() { perl -MTime::HiRes=time -e 'printf "%.2f\n", time'; }
rm -rf "$WORK" "$OUT"; mkdir -p "$WORK/compat" "$OUT"
WINE_APP=${MN_REF_WINE_APP:-${BUILD_DIR:-$ROOT/build}/wine-arm64/wine.app}
"$ROOT/.build/release/macneutron" install --tool-dir "$WORK/ours" --wine-app "$WINE_APP" > /dev/null
stop() {
  [ ! -d "$WORK/compat/pfx" ] || WINEPREFIX="$WORK/compat/pfx" WINEMSYNC=1 \
    "$WORK/ours/wine.app/Contents/Resources/bin/wineserver" -k > /dev/null 2>&1 || true
}
trap stop EXIT
env STEAM_COMPAT_DATA_PATH="$WORK/compat" SteamAppId=0 "$WORK/ours/bin/macneutron" launch getcompatpath "$WORK" \
  > /dev/null 2>&1
while IFS='|' read -r name exe args prefixes; do
  args=$(printf '%s' "$args" | sed "s|@S|Z:$T/shaders|g; s|@T|Z:$T|g; s|@X|Z:$I|g")
  for i in $(seq 1 "${MN_REF_RUNS:-2}"); do
    start=$(now)
    # shellcheck disable=SC2086  # the arguments, one word each
    case $name in d3d1[12]_dlss*|d3d11_nvapi) dlss=1 ;; *) dlss= ;; esac  # invocations: DLSS rows run with MACNEUTRON_DLSS=1
    env STEAM_COMPAT_DATA_PATH="$WORK/compat" SteamAppId=0 MACNEUTRON_GRAPHICS=dxmt MACNEUTRON_NO_STEAM_BRIDGE=1 \
      MACNEUTRON_DLSS=$dlss \
      MACNEUTRON_NO_METALFX=1 DXMT_SHADER_CACHE_PATH="$WORK/cache/$name.$i" DXMT_MAX_ANISOTROPY=0 \
      "$WORK/ours/bin/macneutron" launch waitforexitandrun "$I/$exe" $args \
      < /dev/null > "$WORK/out" 2>&1 &
    pid=$!
    ( sleep "$TO"; touch "$WORK/timed-out"; kill "$pid" 2> /dev/null ) > /dev/null 2>&1 & dog=$!
    rc=0; wait "$pid" || rc=$?
    kill "$dog" 2> /dev/null || true
    if [ -f "$WORK/timed-out" ]; then echo timeout; else echo "$rc"; fi > "$OUT/$name.$i.status"
    rm -f "$WORK/timed-out"
    tr -d '\r' < "$WORK/out" > "$OUT/$name.$i.txt"
    echo "$start $(now)" | awk '{printf "%.2f\n", $2 - $1}' > "$OUT/$name.$i.time"
  done
  echo "ref: $name $(cat "$OUT/$name.1.time") s, status $(cat "$OUT/$name.1.status")"
done < "${MN_REF_INVOCATIONS:-$I/invocations}"
