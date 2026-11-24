#!/bin/sh
# Game-independent guest verification of EAC Linux v1 (Legacy EAC 1 & EOS EAC 2, 64-bit & 32-bit Old WoW64):
#   1. Read-only virtiofs mount at /Volumes/Proton EasyAntiCheat Runtime + all 6 Valve depot 1826331 files
#   2. Both x86_64-unix and i386-unix ntdll.so contain PROTON_EAC_RUNTIME
#   3. Prefix hygiene: run-game.sh-style wineboot removes/avoids easyanticheat*.dll in system32/ & syswow64/
#   4. Wave 5C wine_get_version() for 64-bit (winever.exe) & 32-bit Old WoW64 (winever32.exe): 11.20 / 9.0 / 10.5
#   5. loadorder.c bridge routing (easyanticheat_x64.dll, easyanticheat.dll, easyanticheat_x86.dll):
#      no sibling .so -> native; sibling .so -> builtin from v2/lib64 or v2/lib32; PROTON_EAC_LAUNCHER_PROCESS=1 -> native
# Prints per-check rows and exits 0 iff all checks pass.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${MN_VM_DIR:-${BUILD_DIR:-$ROOT/build}/vm-work}

has_all_eac_files() {
  [ -f "$1/v2/lib64/easyanticheat_x64.dll" ] && [ -f "$1/v2/lib64/easyanticheat_x64.so" ] &&
  [ -f "$1/v2/lib64/easyanticheat.dll" ] && [ -f "$1/v2/lib64/easyanticheat.so" ] &&
  [ -f "$1/v2/lib32/easyanticheat_x86.dll" ] && [ -f "$1/v2/lib32/easyanticheat_x86.so" ]
}

if [ -z "${EAC_DIR:-}" ]; then
  STEAM_DIR="$HOME/Library/Application Support/Steam"
  candidates=""
  add_candidate() {
    candidates="${candidates}${1}
"
  }
  add_candidate "$STEAM_DIR/steamapps/common/Proton EasyAntiCheat Runtime"
  add_candidate "$STEAM_DIR/steamapps/content/app_1826330/depot_1826331"
  add_candidate "$STEAM_DIR/Steam.AppBundle/Steam/Contents/MacOS/steamapps/content/app_1826330/depot_1826331"
  VDF="$STEAM_DIR/steamapps/libraryfolders.vdf"
  if [ -f "$VDF" ]; then
    while IFS= read -r lib; do
      [ -n "$lib" ] || continue
      add_candidate "$lib/steamapps/common/Proton EasyAntiCheat Runtime"
      add_candidate "$lib/steamapps/content/app_1826330/depot_1826331"
    done <<EOF
$(sed -n 's/^[[:space:]]*"path"[[:space:]]*"\([^"]*\)".*/\1/p' "$VDF")
EOF
  fi
  fallback=
  while IFS= read -r c; do
    [ -n "$c" ] && [ -d "$c/v2" ] || continue
    if has_all_eac_files "$c"; then
      EAC_DIR=$c
      break
    fi
    [ -n "$fallback" ] || fallback=$c
  done <<EOF
$candidates
EOF
  [ -n "${EAC_DIR:-}" ] || EAC_DIR=$fallback
fi

if [ -z "${EAC_DIR:-}" ] || [ ! -d "$EAC_DIR/v2" ]; then
  echo "eac-v1-check: Proton EasyAntiCheat Runtime (app 1826330 / depot 1826331) not found." >&2
  echo "Install it via Steam (steam://install/1826330 or Steam Console: download_depot 1826330 1826331) or pass EAC_DIR=..." >&2
  exit 1
fi

for f in winever.exe winever32.exe eacdummy64.dll eacdummy32.dll; do
  if [ ! -f "$W/share/bin/$f" ]; then
    echo "eac-v1-check: missing $W/share/bin/$f — run 'sh vm/guest/build-win-tests.sh' first" >&2
    exit 1
  fi
done

mkdir -p "$W/runs"
T=$(mktemp -d "$W/runs/eac-v1-check.XXXXXX")
STATE="$W/state/eac-v1-check.img"
cleanup() {
  rc=$?
  if [ "$rc" -ne 0 ] && [ -f "$T/share/console.log" ]; then
    echo "--- tail of $T/share/console.log ---" >&2
    tail -n 40 "$T/share/console.log" >&2 || true
  fi
  rm -rf "$T" "$STATE"
}
trap cleanup EXIT
mkdir -p "$T/share"
printf 'eac-runtime /Volumes/Proton EasyAntiCheat Runtime\n' > "$T/share/mounts"

cat > "$T/guest.sh" <<'EOF'
fail=0
check() {
  if [ "$2" = "$3" ]; then
    echo "ok   $1: $2"
  else
    echo "FAIL $1: got '$2', want '$3'"
    fail=1
  fi
}

R="/Volumes/Proton EasyAntiCheat Runtime"
# 1. Read-only mount & all 6 Valve depot 1826331 files
ro_ok=yes
for f in \
  "$R/v2/lib64/easyanticheat_x64.dll" "$R/v2/lib64/easyanticheat_x64.so" \
  "$R/v2/lib64/easyanticheat.dll"     "$R/v2/lib64/easyanticheat.so" \
  "$R/v2/lib32/easyanticheat_x86.dll" "$R/v2/lib32/easyanticheat_x86.so"; do
  if [ ! -r "$f" ]; then
    echo "FAIL missing/unreadable: $f"
    ro_ok=no
  fi
done
if touch "$R/.ro_check" 2>/dev/null || touch "$R/v2/.ro_check" 2>/dev/null; then
  rm -f "$R/.ro_check" "$R/v2/.ro_check" 2>/dev/null || true
  ro_ok=writable
fi
check "1 ro-mount-and-6-files" "$ro_ok" yes

# 2. Both Wine Unix sides patched with PROTON_EAC_RUNTIME
if grep -aq PROTON_EAC_RUNTIME /opt/mn-wine/lib/wine/x86_64-unix/ntdll.so 2>/dev/null; then c64=patched; else c64=missing; fi
if grep -aq PROTON_EAC_RUNTIME /opt/mn-wine/lib/wine/i386-unix/ntdll.so 2>/dev/null; then c32=patched; else c32=missing; fi
check "2 ntdll64-eac-patch" "$c64" patched
check "2 ntdll32-eac-patch" "$c32" patched

export PATH=/opt/mn-wine/bin:$PATH DISPLAY=:0 WINEDEBUG=-all
export FEX_X87REDUCEDPRECISION="${FEX_X87REDUCEDPRECISION:-1}"
export LD_LIBRARY_PATH="/opt/mn-wine/lib/mn-i386${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export WINEPREFIX=/home/mn/eac-v1-check
Xvfb :0 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
rm -rf "$WINEPREFIX"
mkdir -p /home/mn

# 3. Prefix hygiene: run-game.sh-style wineboot with PROTON_EAC_RUNTIME unset leaves zero easyanticheat*.dll
export PROTON_EAC_RUNTIME="$R"
env -u PROTON_EAC_RUNTIME WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -i >/dev/null 2>&1
WINDIR="$WINEPREFIX/drive_c/windows"
: > "$WINDIR/system32/easyanticheat_x64.dll"
if [ -d "$WINDIR/syswow64" ]; then
  : > "$WINDIR/syswow64/easyanticheat_x86.dll"
fi
find "$WINDIR/system32" "$WINDIR/syswow64" -maxdepth 1 -iname 'easyanticheat*.dll' -delete 2>/dev/null || true
env -u PROTON_EAC_RUNTIME WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -u >/dev/null 2>&1
leftover=$(find "$WINDIR/system32" "$WINDIR/syswow64" -maxdepth 1 -iname 'easyanticheat*.dll' 2>/dev/null | wc -l | tr -d ' ')
check "3 prefix-hygiene" "$leftover" 0

# Verify 32-bit probe uses Old WoW64 (no wow64.dll)
out=$(WINEDEBUG=+loaddll timeout 20 wine /mnt/share/bin/winever32.exe 2>&1 || true)
ntdll_ok=no
if printf '%s\n' "$out" | grep -qi 'Loaded .*ntdll\.dll'; then ntdll_ok=yes; fi
wow_cnt=$(printf '%s\n' "$out" | grep -c 'wow64\.dll' || true)
check "3 old-wow64-active" "${ntdll_ok}/${wow_cnt}" "yes/0"

# 4. Wave 5C wine_get_version() for 64-bit & 32-bit Old WoW64
v64_unset=$(env -u PROTON_EAC_RUNTIME -u MACNEUTRON_WINE_VERSION WINEDEBUG=-all timeout 20 wine /mnt/share/bin/winever.exe 2>/dev/null | tr -d '\r')
v32_unset=$(env -u PROTON_EAC_RUNTIME -u MACNEUTRON_WINE_VERSION WINEDEBUG=-all timeout 20 wine /mnt/share/bin/winever32.exe 2>/dev/null | tr -d '\r')
v64_eac=$(env -u MACNEUTRON_WINE_VERSION PROTON_EAC_RUNTIME="$R" WINEDEBUG=-all timeout 20 wine /mnt/share/bin/winever.exe 2>/dev/null | tr -d '\r')
v32_eac=$(env -u MACNEUTRON_WINE_VERSION PROTON_EAC_RUNTIME="$R" WINEDEBUG=-all timeout 20 wine /mnt/share/bin/winever32.exe 2>/dev/null | tr -d '\r')
v64_ovr=$(MACNEUTRON_WINE_VERSION=10.5 PROTON_EAC_RUNTIME="$R" WINEDEBUG=-all timeout 20 wine /mnt/share/bin/winever.exe 2>/dev/null | tr -d '\r')
v32_ovr=$(MACNEUTRON_WINE_VERSION=10.5 PROTON_EAC_RUNTIME="$R" WINEDEBUG=-all timeout 20 wine /mnt/share/bin/winever32.exe 2>/dev/null | tr -d '\r')
check "4 winever64-unset"    "$v64_unset" 11.20
check "4 winever32-unset"    "$v32_unset" 11.20
check "4 winever64-eac"      "$v64_eac"   9.0
check "4 winever32-eac"      "$v32_eac"   9.0
check "4 winever64-override" "$v64_ovr"   10.5
check "4 winever32-override" "$v32_ovr"   10.5

# 5. loadorder.c bridge routing (Legacy EAC 1 & EOS EAC 2, 64-bit & 32-bit Old WoW64)
P=/tmp/eac-probe
rm -rf "$P"
mkdir -p "$P"
cp /mnt/share/bin/eacdummy64.dll "$P/easyanticheat_x64.dll"
cp /mnt/share/bin/eacdummy64.dll "$P/easyanticheat.dll"
cp /mnt/share/bin/eacdummy32.dll "$P/easyanticheat_x86.dll"

L64=/opt/mn-wine/lib/wine/x86_64-unix
L32=/opt/mn-wine/lib/wine/i386-unix

probe_load() {
  # $1=label $2=arch(64|32) $3=dllname $4=mode(no-so|with-so|launcher) $5=want_loaddll_mode(native|builtin)
  esc_dll=$(printf '%s' "$3" | sed 's/\./\\./g')
  case "$4" in
    no-so)    want_lo="got hardcoded n for .*${esc_dll}.*is not present" ;;
    with-so)  want_lo="got hardcoded b for .*${esc_dll}.*is present" ;;
    launcher) want_lo="got hardcoded n for .*${esc_dll}.*EAC launcher process" ;;
    *)        want_lo=$4 ;;
  esac
  if [ "$2" = 32 ]; then
    if [ -x "$L32/wine-preloader" ]; then
      out=$(PROTON_EAC_RUNTIME="$R" WINEDEBUG=+module,+loaddll WINELOADERNOEXEC=1 timeout 20 "$L32/wine-preloader" "$L32/wine" 'C:\windows\syswow64\rundll32.exe' "Z:\\tmp\\eac-probe\\$3,MnNoSuchEntry" 2>&1 || true)
    else
      out=$(PROTON_EAC_RUNTIME="$R" WINEDEBUG=+module,+loaddll WINELOADERNOEXEC=1 timeout 20 "$L32/wine" 'C:\windows\syswow64\rundll32.exe' "Z:\\tmp\\eac-probe\\$3,MnNoSuchEntry" 2>&1 || true)
    fi
  else
    out=$(PROTON_EAC_RUNTIME="$R" WINEDEBUG=+module,+loaddll WINELOADERNOEXEC=1 timeout 20 "$L64/wine-preloader" "$L64/wine" 'C:\windows\system32\rundll32.exe' "Z:\\tmp\\eac-probe\\$3,MnNoSuchEntry" 2>&1 || true)
  fi
  lo_ok=no
  ld_ok=no
  if printf '%s\n' "$out" | grep -Eq "$want_lo"; then lo_ok=yes; fi
  if printf '%s\n' "$out" | grep -Eqi "Loaded .*${esc_dll}.*: ${5}"; then ld_ok=yes; fi
  check "5 $1" "${lo_ok}/${ld_ok}" "yes/yes"
  if [ "$lo_ok" != yes ] || [ "$ld_ok" != yes ]; then
    printf '%s\n' "$out" | grep -E 'get_load_order|loaddll|err:' | head -n 12
  fi
}

# 5a. Without sibling .so -> native
probe_load "x64-no-so-native"       64 easyanticheat_x64.dll no-so    native
probe_load "eos-no-so-native"       64 easyanticheat.dll     no-so    native
probe_load "x86-no-so-native"       32 easyanticheat_x86.dll no-so    native

# 5b. With sibling .so -> builtin from $PROTON_EAC_RUNTIME/v2/lib{64,32}/
: > "$P/easyanticheat_x64.so"
: > "$P/easyanticheat_x86.so"
probe_load "x64-with-so-builtin"    64 easyanticheat_x64.dll with-so  builtin
probe_load "eos-with-so-builtin"    64 easyanticheat.dll     with-so  builtin
probe_load "x86-with-so-builtin"    32 easyanticheat_x86.dll with-so  builtin

# 5c. With PROTON_EAC_LAUNCHER_PROCESS=1 -> native even with sibling .so present
export PROTON_EAC_LAUNCHER_PROCESS=1
probe_load "x64-launcher-native"    64 easyanticheat_x64.dll launcher native
probe_load "eos-launcher-native"    64 easyanticheat.dll     launcher native
probe_load "x86-launcher-native"    32 easyanticheat_x86.dll launcher native
unset PROTON_EAC_LAUNCHER_PROCESS

wineserver -k >/dev/null 2>&1 || true
rm -rf "$P"
[ $fail -eq 0 ] && echo "eac-v1-check: ok" || echo "eac-v1-check: FAIL"
exit $fail
EOF

rm -f "$STATE"
MN_MOUNTS=keep MN_SHARE="$T/share" MN_STATE="$STATE" MN_TPM= MN_AUDIO_OUTPUT=none MN_GAMEPAD=0 \
  sh "$ROOT/vm/run-guest.sh" "$T/guest.sh" --ro-share "eac-runtime=$EAC_DIR"
