#!/bin/sh
# Developer end-to-end check of the Steam bridge (bridge spec §4.3). Needs Steam running and logged in, `make bridge`,
# and a wine.app: the bundle's ARM64X lsteamclient.dll as steamclient64.dll, the aarch64 steam.exe, and the x64
# steamprobe.exe under FEX with its fault check. Prints your SteamID and persona name: don't paste them anywhere public.
#   bridge/probe.sh <path to a game's steam_api64.dll>
# APPID=<id> replaces 480; WINEDEBUG=+steamclient shows the bridge's log.
# PROBE_REDACT=1 prints only the probe's own rows, with "steamid ok" and "persona ok" for the values (WINEDEBUG=-all).
# STEAM_COMPAT_CLIENT_INSTALL_PATH replaces Mac Steam's folder (steamclient.dylib). Two modes:
# - arm64 (ship-base spec §7), when MACNEUTRON_ARM64_APP names a wine.app: directly in the booted prefix
#   MACNEUTRON_ARM64_PREFIX.
# - launcher (release spec §9 L3), when MACNEUTRON_TOOL_DIR names a tool folder (`macneutron install … --steam-exe`):
#   through its `launch waitforexitandrun`, as Steam starts a game, in the compat folder STEAM_COMPAT_DATA_PATH
#   (default $TMPDIR/macneutron probe/compat). The launcher prepares the prefix and copies the bridge in.
#   bridge/probe.sh --redact-self-test: checks PROBE_REDACT's filter on fake rows, no Wine needed.
set -eu
# PROBE_REDACT's filter, stdin to stdout: only the probe's rows, the SteamID and persona name as "ok", any other
# SteamID-shaped number as <steamid>. A missing persona name prints "(null)": that one is a FAIL, not a name.
probe_redact() {
  keep='^(load|init|SteamUser|SteamFriends|auth ticket|callback|fault|missing export|steamid|persona)'
  # Byte-wise (LC_ALL=C): in a UTF-8 locale a name that isn't UTF-8 stops tr and sed, or slips past the persona rule.
  LC_ALL=C tr -d '\r' | LC_ALL=C /usr/bin/grep -E "$keep" | LC_ALL=C sed -E 's/^persona: \(null\)$/persona FAIL/
    s/^steamid: [1-9][0-9]*$/steamid ok/; s/^steamid: 0$/steamid FAIL/; s/^persona: .+$/persona ok/
    s/7656119[0-9]{10}/<steamid>/g'
}
if [ "${1:-}" = --redact-self-test ]; then
  # Fake rows only (SteamIDs 7656119000...), in a UTF-8 locale, where a byte that isn't UTF-8 trips a locale-aware tool.
  bad=0
  row() {  # row <printf format of the input rows> <expected output>
    got=$(printf "$1\n" | LC_ALL=en_US.UTF-8 probe_redact 2>&1) || true
    [ "$got" = "$2" ] || { printf "FAIL probe redaction: '%s' gave '%s', not '%s'\n" "$1" "$got" "$2"; bad=1; }
  }
  row 'steamid: 76561190000000001' 'steamid ok'
  row 'steamid: 0' 'steamid FAIL'
  row 'persona: Fake Name' 'persona ok'
  row 'persona: (null)' 'persona FAIL'
  row 'SteamInternal_SetMinidumpSteamID:  Caching Steam ID:  76561190000000001 [API loaded no]' ''
  row 'callback 76561190000000001 (8 bytes)' 'callback <steamid> (8 bytes)'
  row 'persona: \377\376x\nfault: caught' "$(printf 'persona ok\nfault: caught')"
  [ "$bad" = 1 ] || echo "PASS probe redaction"
  exit "$bad"
fi
[ $# -eq 1 ] || { echo "usage: bridge/probe.sh <steam_api64.dll>" >&2; exit 2; }
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DLL="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
if [ -n "${MACNEUTRON_TOOL_DIR:-}" ]; then
  export STEAM_COMPAT_DATA_PATH="${STEAM_COMPAT_DATA_PATH:-${TMPDIR:-/tmp}/macneutron probe/compat}"
else
  WINE="${MACNEUTRON_ARM64_APP:?needs MACNEUTRON_ARM64_APP or MACNEUTRON_TOOL_DIR}/Contents/MacOS/wine"
  CLIENT64="$MACNEUTRON_ARM64_APP/Contents/Resources/lib/wine/aarch64-windows/lsteamclient.dll"
  export WINEPREFIX="${MACNEUTRON_ARM64_PREFIX:?arm64 mode needs MACNEUTRON_ARM64_PREFIX}"
  [ -d "$WINEPREFIX" ] || { echo "probe: no prefix at $WINEPREFIX" >&2; exit 1; }
fi
if [ "${PROBE_REDACT:-}" = 1 ]; then WINEDEBUG=-all; fi
export WINEDEBUG="${WINEDEBUG:--all}" WINEMSYNC=1
export SteamAppId="${APPID:-480}" SteamGameId="${APPID:-480}"
MAC_STEAM="$HOME/Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS"
export STEAM_COMPAT_CLIENT_INSTALL_PATH="${STEAM_COMPAT_CLIENT_INSTALL_PATH:-$MAC_STEAM}"

winpath() { printf 'Z:%s' "$1" | tr / '\\'; }
if [ -n "${MACNEUTRON_TOOL_DIR:-}" ]; then
  set -- "$MACNEUTRON_TOOL_DIR/bin/macneutron" launch waitforexitandrun "$ROOT/build/bridge/steamprobe.exe" \
    "$(winpath "$DLL")" fault
else
  STEAM="$WINEPREFIX/drive_c/Program Files (x86)/Steam"
  mkdir -p "$STEAM"
  cp "$ROOT/build/bridge/arm64/steam.exe" "$STEAM/steam.exe"
  cp "$CLIENT64" "$STEAM/steamclient64.dll"
  set -- "$WINE" 'C:\Program Files (x86)\Steam\steam.exe' "$(winpath "$ROOT/build/bridge/steamprobe.exe")" \
    "$(winpath "$DLL")" fault
fi
[ "${PROBE_REDACT:-}" = 1 ] || exec "$@"
# Redacted: everything the run prints (Steam's client writes to it too) stays in a variable, never in a file.
out=$("$@" 2>&1) && rc=0 || rc=$?
printf '%s\n' "$out" | probe_redact
exit "$rc"
