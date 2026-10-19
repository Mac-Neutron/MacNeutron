#!/bin/sh
# R0 (spec §6.2): notarize today's staged wine.app, then launch its quarantined copy through launchd (not as a child
# of a terminal app: Developer Tools are exempt from Gatekeeper), once online and once with the network off.
# Usage: r0.sh <notarize|online|offline|results>
#   notarize  build/release/r0/wine.app (stapled, unquarantined; Task 9 reuses it) and quarantined/wine.app
#   online    clone the quarantined copy to <mode>/Application Support/wine.app, boot a fresh prefix with the staged
#   offline   bundle, run arm64-hello.exe with the clone's loader as a launchd job: result-<mode>.txt.
#             offline refuses while Apple's ticket or OCSP servers are reachable.
#   results   both result files, the CDHash lines and every submission
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/release/lib.sh"
B="${BUILD_DIR:-$ROOT/build}"
STAGED="$B/wine-arm64/wine.app"
HELLO="$B/wine-arm64-tests/arm64-hello.exe"
R0="$B/release/r0"
LABEL=net.authspot.macneutron.r0
DOMAIN="gui/$(id -u)"

cdhash() { codesign -dvvv "$1" 2>&1 | LC_ALL=C /usr/bin/grep '^CDHash=' || die "no CDHash for $1"; }
q() { printf "'%s'" "$(printf %s "$1" | sed "s/'/'\\\\''/g")"; }  # quoted for sh -c

notarize() {
  [ -d "$STAGED" ] || die "no staged bundle at $STAGED (make wine-arm64)"
  rm -rf "$R0/wine.app" "$R0/quarantined" "$R0/cdhash.txt" "$R0"/notary-*/*.zip
  mkdir -p "$R0/quarantined"
  cp -c -R "$STAGED" "$R0/wine.app"
  echo "before staple: $(cdhash "$R0/wine.app")" > "$R0/cdhash.txt"
  # One work folder per attempt, kept: `results` lists every submission id.
  notarize_and_staple "$R0/wine.app" "$R0/notary-$(date +%Y%m%dT%H%M%S)"
  echo "after staple: $(cdhash "$R0/wine.app")" >> "$R0/cdhash.txt"
  cat "$R0/cdhash.txt"
  [ "$(cut -d ' ' -f 3 "$R0/cdhash.txt" | sort -u | wc -l)" -eq 1 ] || die "stapling changed the CDHash"
  cp -c -R "$R0/wine.app" "$R0/quarantined/wine.app"
  xattr -r -w com.apple.quarantine "0081;$(printf %x "$(date +%s)");Safari;$(uuidgen)" "$R0/quarantined/wine.app"
  echo "quarantined: $(xattr -p com.apple.quarantine "$R0/quarantined/wine.app/Contents/MacOS/wine")"
}

# Stops the job and the prefix's Wine: the server first, then whatever still runs the clone's binaries.
stop() {
  launchctl bootout "$DOMAIN/$LABEL" 2> /dev/null || true
  [ ! -d "$PFX" ] || WINEPREFIX="$PFX" WINEMSYNC=1 "$STAGED/Contents/Resources/bin/wineserver" -k 2> /dev/null || true
  pids=$(for f in "$CLONE/Contents/MacOS/wine" "$CLONE/Contents/Resources/bin/wineserver"; do
    [ ! -e "$f" ] || lsof -t "$f" 2> /dev/null || true; done | sort -u)
  # shellcheck disable=SC2086  # pids is a list
  [ -z "$pids" ] || kill -9 $pids 2> /dev/null || true
}

run() {  # run <online|offline>
  mode=$1
  # Offline means Gatekeeper can't reach Apple: the ticket lookup (CloudKit) and certificate revocation (OCSP).
  if [ "$mode" = offline ] && { scutil -r api.apple-cloudkit.com | LC_ALL=C /usr/bin/grep -qx Reachable \
      || scutil -r ocsp.apple.com | LC_ALL=C /usr/bin/grep -qx Reachable; }; then
    die "offline needs the network off: Apple's ticket or OCSP servers are still reachable"
  fi
  Q="$R0/quarantined/wine.app"
  [ -d "$Q" ] || die "no quarantined copy at $Q: run r0.sh notarize first"
  [ -f "$HELLO" ] || die "no $HELLO (make wine-arm64-tests)"
  M="$R0/$mode"
  CLONE="$M/Application Support/wine.app"
  PFX="$M/pfx"
  OUT="$M/hello.out"
  PLIST="$M/job.plist"
  stop
  trap stop EXIT
  trap 'stop; exit 1' INT TERM
  rm -rf "$M" "$R0/result-$mode.txt"
  mkdir -p "$M/Application Support"
  cp -c -R "$Q" "$CLONE"
  xattr -p com.apple.quarantine "$CLONE/Contents/MacOS/wine" > /dev/null 2>&1 || die "the clone lost its quarantine"

  WINEPREFIX="$PFX" WINEMSYNC=1 WINEDLLOVERRIDES="mscoree,mshtml=" "$STAGED/Contents/MacOS/wine" wineboot -i \
    > "$M/boot.log" 2>&1 || die "wineboot failed: see $M/boot.log"
  WINEPREFIX="$PFX" WINEMSYNC=1 "$STAGED/Contents/Resources/bin/wineserver" -w

  job="[ -e $(q "$OUT.done") ] && exit 0; : > $(q "$OUT.done"); WINEPREFIX=$(q "$PFX") WINEMSYNC=1"
  job="$job $(q "$CLONE/Contents/MacOS/wine") $(q "$HELLO") > $(q "$OUT") 2>&1; echo \"status=\$?\" >> $(q "$OUT")"
  plutil -create xml1 "$PLIST"
  plutil -insert Label -string "$LABEL" "$PLIST"
  plutil -insert ProgramArguments -array "$PLIST"
  for a in /bin/sh -c "$job"; do plutil -insert ProgramArguments -string "$a" -append "$PLIST"; done
  plutil -insert RunAtLoad -bool true "$PLIST"
  plutil -insert KeepAlive -bool false "$PLIST"
  plutil -lint "$PLIST" > /dev/null || die "bad job plist: $PLIST"

  launchctl bootstrap "$DOMAIN" "$PLIST" || die "launchctl bootstrap failed"
  i=0
  while ! LC_ALL=C /usr/bin/grep -q '^status=' "$OUT" 2> /dev/null && [ $i -lt 120 ]; do sleep 1; i=$((i + 1)); done
  # A timeout with the loader still running is slow, not blocked: say which.
  running=$(lsof -t "$CLONE/Contents/MacOS/wine" 2> /dev/null | tr '\n' ' ' || true)
  launchctl bootout "$DOMAIN/$LABEL" 2> /dev/null || true
  {
    echo "R0 $mode $(date -u +%Y-%m-%dT%H:%M:%SZ) loader $CLONE/Contents/MacOS/wine"
    if [ -f "$OUT" ]; then tr -d '\r' < "$OUT"; else echo "(no output)"; fi
    [ $i -lt 120 ] || echo "timeout after 120 s; loader processes still running: ${running:-none}"
  } > "$R0/result-$mode.txt"
  cat "$R0/result-$mode.txt"
  LC_ALL=C /usr/bin/grep -q '^PASS arm64-hello' "$R0/result-$mode.txt" \
    && LC_ALL=C /usr/bin/grep -qx 'status=0' "$R0/result-$mode.txt" || die "R0 $mode failed: $R0/result-$mode.txt"
}

results() {
  for f in "$R0/result-online.txt" "$R0/result-offline.txt" "$R0/cdhash.txt"; do
    echo "== ${f#"$R0"/}"
    cat "$f" 2> /dev/null || echo "(none)"
  done
  echo "== submissions"
  for f in "$R0"/notary-*/submit.json; do
    [ ! -f "$f" ] || jq -r -s 'last | "submission \(.id): \(.status)"' "$f"
  done
}

case "${1:-}" in
  notarize) notarize ;;
  online | offline) run "$1" ;;
  results) results ;;
  *) echo "usage: r0.sh <notarize|online|offline|results>" >&2; exit 2 ;;
esac
