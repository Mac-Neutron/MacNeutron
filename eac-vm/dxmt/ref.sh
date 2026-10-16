#!/bin/sh
# ref.sh: the 4b-2 gate's macOS reference. Each program in invocations runs twice through MacNeutron's launcher, as
# dxmt/check.sh's `run ours` does: a tool folder `macneutron install` assembles from build/wine-arm64/wine.app in
# ${TMPDIR}/macneutron dxmt-guest-ref (never a user's; Steam is never started), check.sh's environment, a fresh
# shader cache per run, a 300 s watchdog. Writes build/eac-vm-work/share/dxmt-ref/<name>.<1|2>.txt and .time.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK="${TMPDIR:-/tmp}/macneutron dxmt-guest-ref"
OUT=$ROOT/build/eac-vm-work/share/dxmt-ref
T=$ROOT/dxmt/tests
now() { perl -MTime::HiRes=time -e 'printf "%.2f\n", time'; }
rm -rf "$WORK" "$OUT"; mkdir -p "$WORK/compat" "$OUT"
"$ROOT/.build/release/macneutron" install --tool-dir "$WORK/ours" --wine-app "$ROOT/build/wine-arm64/wine.app" > /dev/null
stop() {
  [ ! -d "$WORK/compat/pfx" ] || WINEPREFIX="$WORK/compat/pfx" WINEMSYNC=1 \
    "$WORK/ours/wine.app/Contents/Resources/bin/wineserver" -k > /dev/null 2>&1 || true
}
trap stop EXIT
env STEAM_COMPAT_DATA_PATH="$WORK/compat" SteamAppId=0 "$WORK/ours/bin/macneutron" launch getcompatpath "$WORK" \
  > /dev/null 2>&1
/usr/bin/grep -v '^#' "$ROOT/eac-vm/dxmt/invocations" | while IFS='|' read -r name exe args prefixes; do
  args=$(printf '%s' "$args" | sed "s|@S|Z:$T/shaders|g; s|@T|Z:$T|g")
  for i in 1 2; do
    start=$(now)
    # shellcheck disable=SC2086  # the arguments, one word each
    env STEAM_COMPAT_DATA_PATH="$WORK/compat" SteamAppId=0 MACNEUTRON_GRAPHICS=dxmt MACNEUTRON_NO_STEAM_BRIDGE=1 \
      MACNEUTRON_NO_METALFX=1 DXMT_SHADER_CACHE_PATH="$WORK/cache/$name.$i" DXMT_MAX_ANISOTROPY=0 \
      "$WORK/ours/bin/macneutron" launch waitforexitandrun "$ROOT/build/dxmt-tests/$exe" $args \
      < /dev/null > "$WORK/out" 2>&1 &
    pid=$!
    ( sleep 300; kill "$pid" 2> /dev/null ) > /dev/null 2>&1 & dog=$!
    wait "$pid" || true
    kill "$dog" 2> /dev/null || true
    tr -d '\r' < "$WORK/out" > "$OUT/$name.$i.txt"
    echo "$start $(now)" | awk '{printf "%.2f\n", $2 - $1}' > "$OUT/$name.$i.time"
  done
  echo "ref: $name $(cat "$OUT/$name.1.time") s"
done
