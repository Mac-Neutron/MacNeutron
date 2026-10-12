#!/bin/sh
# The arm64 Wine runtime on the maintainer's Mac (native arm64 spec §7.3): `make wine-arm64-check`.
# Usage: check.sh [step...]   no step = all, in STEPS' order. Needs `make build wine-arm64 wine-arm64-tests`, the
# dxmt steps `make dxmt-tests presenter dxmt-tests-arm64ec`, and winetests `make wine-arm64-winetests`. g4-bench, for
# its Rosetta baseline, and the dxmt-* steps, for their D3DMetal reference, need the frozen Rosetta reference
# (tools/freeze-rosetta-reference.sh; MACNEUTRON_REFERENCE names another), run by its own launcher. dxmt-x64's FSR 3 check needs SMITE 2 installed
# (Steam): its amd_fidelityfx_dx12.dll, read from the game's install, never copied. steam-bridge needs `make bridge`,
# Steam running and logged in, and SMITE 2 installed (its steam_api64.dll, read in place). steamclient needs Valve's signed
# steamclient64.dll at build/valve/steamclient64.dll (never in the repo; VALVE_STEAMCLIENT64 names another) and prints
# a skip line without it.
# Every run starts fresh: a new clone of the staged bundle, a new prefix. The clone sits at a path with a space, as the
# app installs it. A step that needs a prefix gets one from `boot`, which runs first if it isn't named.
# Nothing of the runtime is left after the script exits, whatever the reason: the last line is PASS or FAIL orphans.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/wine-arm64/lib.sh"
B="${BUILD_DIR:-$ROOT/build}"
STAGED="$B/wine-arm64/wine.app"
TESTS="$B/wine-arm64-tests"
WORK="$B/wine-arm64 check"
TOOL="$WORK/Application Support/wine.app"
PFX="$WORK/prefix arm64"
# The `unentitled` step's loader: a clone of $TOOL re-signed without the entitlement, with a prefix of its own.
UNENT="$WORK/unentitled.app"
UPFX="$WORK/prefix unentitled"
# Gate G4's baseline: a clone of the frozen Rosetta reference (x86_64 Wine under Rosetta), never the reference itself,
# run by its own launcher. RPFX is its STEAM_COMPAT_DATA_PATH; Wine's prefix is RPFX/pfx.
REF="${MACNEUTRON_REFERENCE:-$HOME/Library/Application Support/MacNeutron Reference/rosetta-tool}"
RTOOL="$WORK/rosetta tool"
RPFX="$WORK/prefix rosetta"
RWINE="$RTOOL/Libraries/Wine/bin"
# steam-bridge's tool folder, assembled with `macneutron install`, and the compat folder its launcher runs use.
BTOOL="$WORK/steam-bridge tool"
BCOMPAT="$WORK/steam-bridge launcher ü/compat"
# msync (ship-base spec §6) is on, as on the Rosetta runtime: a client and its wineserver have to agree, so every run
# sees WINEMSYNC=1, and the wineserver a run starts gets it too. Only the msync step, which starts its own, differs.
export WINEMSYNC=1

# Steps, in order; each task appends its own. NEEDS_PREFIX: the steps that run in the prefix `boot` creates.
# NEEDS_FEX: the x64 steps, which run after `fex` registers FEX in that prefix (else Wine's stub xtajit64 runs them).
# NEEDS_DXMT: the steps that run DXMT, after `dxmt` puts its front ends in that prefix.
G1="g1-hello g1-seh g1-threads g1-kuser g1-smc g1-tsc g1-unaligned"
STEPS="macos signature boot pages unentitled arm64 isec g3-cpu fex $G1 g2-litmus viewec wxflip wxflip-x64 msync x18"
STEPS="$STEPS g5-jit"
STEPS="$STEPS fonts-tls steam-bridge dxmt dxmt-present dxmt-arm64ec dxmt-x64 g4-bench"
NEEDS_DXMT="dxmt-present dxmt-arm64ec dxmt-x64"
NEEDS_PREFIX="pages arm64 isec g3-cpu fex $G1 g2-litmus viewec wxflip wxflip-x64 msync x18 g5-jit fonts-tls steam-bridge"
NEEDS_PREFIX="$NEEDS_PREFIX dxmt $NEEDS_DXMT g4-bench"
NEEDS_FEX="$G1 g2-litmus wxflip-x64 msync x18 g5-jit steam-bridge $NEEDS_DXMT g4-bench"
# The media steps (video playback spec §8): run by name (make media-check), each its arm64 program and its x64 twin
# under FEX; not in the full run while they are red. Each later task adds its step here.
MEDIA="media-mf"
NEEDS_PREFIX="$NEEDS_PREFIX $MEDIA"
NEEDS_FEX="$NEEDS_FEX $MEDIA"
# The measurement lanes (batch Task 2): measured, not gated beyond each program's PASS and row count; run by name
# (make lanes-check), not in the full run.
LANES="lanes"
NEEDS_PREFIX="$NEEDS_PREFIX $LANES"
NEEDS_FEX="$NEEDS_FEX $LANES"
# The batch's gated steps (batch Tasks 4-8): each needs the prefix with FEX; they run after the rest.
BATCH="fex-vmd winetests ec-hook crt steamclient"
STEPS="$STEPS $BATCH" NEEDS_PREFIX="$NEEDS_PREFIX $BATCH" NEEDS_FEX="$NEEDS_FEX $BATCH"

# The processes running the runtime's executables. Wine rewrites argv, so `pkill -f <path>` finds nothing; the kernel
# knows the executable. The Rosetta tool folders' (the launcher, Wine and its server): g4-bench's, and the reference's
# clones dxmt/check.sh makes in the dxmt-* steps' work folders; and the tool folders it assembles there, and
# steam-bridge's.
runtime_pids() {
  for d in "$RTOOL" "$WORK"/dxmt-*/ref; do
    set -- "$@" "$d/bin/macneutron" "$d/Libraries/Wine/lib/wine/x86_64-unix/wine" "$d/Libraries/Wine/bin/wineserver"
  done
  for d in "$WORK"/dxmt-*/ours "$BTOOL"; do
    set -- "$@" "$d/bin/macneutron" "$d/wine.app/Contents/MacOS/wine" "$d/wine.app/Contents/Resources/bin/wineserver"
  done
  for f in "$TOOL/Contents/MacOS/wine" "$TOOL/Contents/Resources/bin/wineserver" \
    "$UNENT/Contents/MacOS/wine" "$UNENT/Contents/Resources/bin/wineserver" "$@"; do
    [ -e "$f" ] || continue
    lsof -t "$f" 2> /dev/null || true
  done | sort -u | tr '\n' ' '
}

# Stops the runtimes: their servers first (the clones' too), then whatever still runs one of the binaries.
cleanup() {
  for pair in "$TOOL/Contents/Resources/bin/wineserver|$PFX" "$UNENT/Contents/Resources/bin/wineserver|$UPFX" \
    "$RWINE/wineserver|$RPFX/pfx" "$BTOOL/wine.app/Contents/Resources/bin/wineserver|$BCOMPAT/pfx"; do
    if [ -d "${pair#*|}" ] && [ -x "${pair%%|*}" ]; then
      WINEPREFIX="${pair#*|}" "${pair%%|*}" -k > /dev/null 2>&1 || true
    fi
  done
  pids=$(runtime_pids)
  # shellcheck disable=SC2086  # pids is a list
  [ -z "$pids" ] || kill -9 $pids 2> /dev/null || true
}

# Prints PASS orphans, or FAIL orphans: <pids> (the processes get a few seconds to die).
orphans() {
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    pids=$(runtime_pids)
    [ -n "$pids" ] || { echo "PASS orphans"; return 0; }
    sleep 0.5
  done
  echo "FAIL orphans: $pids"
  return 1
}

# Stops the running step, if any: its subshell, then the children it had (snapshot first: once the subshell is gone
# they belong to launchd). As a background job of a non-interactive shell the subshell ignores SIGINT, so on an
# interrupt it would go on to its next command; TERM is default there. Runtime processes further down are cleanup's.
stop_step() {
  [ -n "${pid:-}" ] || return 0
  kids=$(pgrep -P "$pid" 2> /dev/null || true)
  kill "$pid" 2> /dev/null || true
  # shellcheck disable=SC2086  # kids is a list
  [ -z "$kids" ] || kill $kids 2> /dev/null || true
  pid=
}

# On any exit: stop the step and the runtime, then say whether anything is left. A leftover turns a pass into a failure.
finish() {
  rc=$?
  trap - EXIT INT TERM
  stop_step
  cleanup
  orphans || rc=1
  exit "$rc"
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# step <name> <cap-seconds> <command...>: runs the command (output in $WORK/<name>.log) for at most the cap.
# Prints PASS <name>, or FAIL <name>: <last output line> and exits 1.
step() {
  name=$1 cap=$2; shift 2
  log="$WORK/$name.log"
  ( trap - EXIT INT TERM; "$@" ) > "$log" 2>&1 &
  pid=$!
  waited=0
  while kill -0 "$pid" 2> /dev/null && [ "$waited" -lt $((cap * 4)) ]; do sleep 0.25; waited=$((waited + 1)); done
  why=
  if kill -0 "$pid" 2> /dev/null; then
    stop_step
    why="timed out after ${cap} s"
    rc=1
  else
    wait "$pid" && rc=0 || rc=$?
    pid=
  fi
  if [ "$rc" = 0 ]; then echo "PASS $name"; return 0; fi
  last=$(tr -d '\r' < "$log" | grep . | tail -n 1 || true)
  last=${last#"FAIL $name: "}  # a command that already names the step
  echo "FAIL $name: ${why:+$why; }${last:-exit $rc}"
  exit 1
}

wine_run() { WINEPREFIX="$PFX" "$TOOL/Contents/MacOS/wine" "$@"; }
# A run on DXMT: the launcher's DXMT overrides (GraphicsBackend.swift), its front ends from system32 (the `dxmt` step).
dxmt_run() { WINEDLLOVERRIDES="dxgi,d3d10core,d3d11,d3d12=n,b;d3d9,d3d10=b" wine_run "$@"; }

# `env -u` options for every FEX_* variable the caller set: runs that measure FEX run it with its defaults.
unfex() { env | sed -n 's/^\(FEX_[A-Za-z0-9_]*\)=.*/-u \1/p'; }

macos_cmd() {
  v=$(sw_vers -productVersion)
  [ "${v%%.*}" -ge 27 ] || { echo "macOS $v is below 27"; return 1; }
}

signature_cmd() {
  [ -d "$TOOL" ] || { echo "no bundle at ${STAGED#"$ROOT"/}"; return 1; }
  codesign --verify --strict --deep "$TOOL" || return 1
  codesign -d --entitlements - "$TOOL/Contents/MacOS/wine" 2>&1 | grep -q cross-architecture-support \
    || { echo "the loader lacks com.apple.developer.cross-architecture-support"; return 1; }
  got=$(realpath "$TOOL/Contents/Resources/lib/wine/aarch64-unix/wine")
  want="$(realpath "$TOOL")/Contents/MacOS/wine"
  [ "$got" = "$want" ] || { echo "aarch64-unix/wine is $got, not $want"; return 1; }
}

boot_cmd() { WINEDLLOVERRIDES="mscoree,mshtml=" wine_run wineboot -i; }

# Runs wine with +virtual (a new Windows process traces its host page size once); the run has to trace at least one
# `host page size:` line, and every one says 4k. A 16K process only gets to say so if nothing re-execs it.
pages_run() {  # pages_run <name> <wine args...>
  name=$1; shift
  trace="$WORK/pages-$name.trace"
  WINEDEBUG=+virtual WINEDLLOVERRIDES="mscoree,mshtml=" wine_run "$@" > "$trace" 2>&1 || { echo "$name: exit $?"; return 1; }
  lines=$(grep 'host page size:' "$trace" | tr -d '\r' || true)
  [ -n "$lines" ] || { echo "$name: no host page size line in ${trace#"$ROOT"/}"; return 1; }
  bad=$(echo "$lines" | grep -v 'host page size: 4k$' || true)
  [ -z "$bad" ] || { echo "$name: $(echo "$bad" | head -n 1)"; return 1; }
  echo "$name: $(echo "$lines" | wc -l | tr -d ' ') processes, all 4k"
}

pages_cmd() {
  pages_run wineboot wineboot -u && pages_run arm64-hello "$TESTS/arm64-hello.exe"
}

# The entitlement is checked before the exec: without it the kernel kills the 4K exec with no message at all.
unentitled_cmd() {
  cp -cR "$TOOL" "$UNENT"
  codesign -f -s - "$UNENT/Contents/MacOS/wine"  # ad hoc, no entitlements
  rc=0
  err=$(WINEPREFIX="$UPFX" WINEDLLOVERRIDES="mscoree,mshtml=" "$UNENT/Contents/MacOS/wine" wineboot 2>&1 > /dev/null) || rc=$?
  echo "$err"
  [ "$rc" != 0 ] || { echo "wineboot ran from a loader without the entitlement"; return 1; }
  echo "$err" | grep -q "lacks the com.apple.developer.cross-architecture-support entitlement" \
    || { echo "exit $rc, without saying the entitlement is missing"; return 1; }
}

# exe_cmd <test> [args...]: runs $TESTS/<test>.exe with the arguments, which passes when it prints PASS <test>. Its
# stderr (Wine's messages and traces) goes to $WORK/<test>.err; on a failure, its last 5 lines come first in the log, so
# the step's FAIL line is still the program's own last line (or, if it printed nothing, Wine's).
exe_cmd() {
  t=$1; shift
  out=$(wine_run "$TESTS/$t.exe" "$@" 2> "$WORK/$t.err" | tr -d '\r') || true  # CRLF line ends: text mode on a pipe
  echo "$out" | LC_ALL=C /usr/bin/grep -qx "PASS $t" && { echo "$out"; return 0; }
  echo "the last lines of ${WORK#"$ROOT"/}/$t.err:"
  tr -d '\r' < "$WORK/$t.err" | tail -n 5
  echo "$out"
  return 1
}

# Gate G3: the CPU ID registers FEX reads (patch 10). `reg query` prints nothing for REG_QWORD; `reg export` does.
g3_cpu_cmd() {
  wine_run reg export 'HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0' "Z:$WORK/cpu.reg" /y || return 1
  python3 "$ROOT/wine-arm64/tools/cpuregs.py" "$WORK/cpu.reg"
}

# FEX as the prefix's x64 emulator (native arm64 spec §6.3): the default value of HKLM\Software\Microsoft\Wow64\amd64.
fex_cmd() {
  wine_run reg add 'HKLM\Software\Microsoft\Wow64\amd64' /ve /d libarm64ecfex.dll /f || return 1
  out=$(wine_run reg query 'HKLM\Software\Microsoft\Wow64\amd64' /ve | tr -d '\r') || return 1
  printf '%s\n' "$out"  # not echo: it would read the key's \a as a bell
  printf '%s\n' "$out" | grep -q 'REG_SZ *libarm64ecfex\.dll$' \
    || { echo "the amd64 emulator is not libarm64ecfex.dll"; return 1; }
}

# Gate G1's hello: x64 code under FEX, with the exception and DLL-load traces in $WORK/x64-hello.err.
g1_hello_cmd() {
  export WINEDEBUG=+seh,+loaddll
  exe_cmd x64-hello
}

# The rest of gate G1 (spec §8), each test under FEX. Structured exceptions and a C++ throw are one step.
g1_seh_cmd() { exe_cmd x64-seh && exe_cmd x64-seh-cpp; }

# Gate G2 (spec §8): x64 memory ordering under FEX's software TSO. The default run, with FEX's defaults (every FEX_*
# variable the caller set is dropped), forbids every pattern. The control run, TSO off, has to show MP reordering, or
# the test can't see reordering at all; its other patterns are only reported.
g2_litmus_cmd() {
  n=10000000
  t0=$(date +%s)
  # shellcheck disable=SC2046  # unfex prints a list of options
  out=$(env $(unfex) WINEPREFIX="$PFX" "$TOOL/Contents/MacOS/wine" "$TESTS/x64-litmus.exe" $n | tr -d '\r') || true
  echo "$out"
  echo "info TSO on: $(($(date +%s) - t0)) s"
  for p in MP LB 2+2W IRIW; do
    f=$(echo "$out" | sed -n "s/^litmus $p forbidden=\([0-9]*\) runs=$n\$/\1/p")
    [ -n "$f" ] || { echo "FAIL g2-litmus: no $p result for $n runs"; return 1; }
    [ "$f" = 0 ] || { echo "FAIL g2-litmus: $p forbidden=$f"; return 1; }
  done
  t0=$(date +%s)
  out=$(FEX_TSOENABLED=0 wine_run "$TESTS/x64-litmus.exe" $n | tr -d '\r') || true
  echo "$out" | sed 's/^litmus /info TSO off: litmus /'
  echo "info TSO off: $(($(date +%s) - t0)) s"
  f=$(echo "$out" | sed -n "s/^litmus MP forbidden=\([0-9]*\) runs=$n\$/\1/p")
  [ -n "$f" ] || { echo "FAIL g2-litmus: control: no MP result for $n runs"; return 1; }
  [ "$f" -ge 1 ] || { echo "FAIL g2-litmus: control saw no MP violation"; return 1; }
}

# Patch 12's W^X flip trace (spec §5.2): an RWX page, rewritten and run 10 times, flips at least 10 times, in both
# directions, and every trace line has the one format.
wxflip_cmd() {
  out=$(WINEDEBUG=+wxflip wine_run "$TESTS/arm64-wxflip.exe" 2>&1 | tr -d '\r') || true
  echo "$out" | LC_ALL=C /usr/bin/grep -v 'trace:wxflip' || true
  n=$(echo "$out" | LC_ALL=C /usr/bin/grep -c 'trace:wxflip' || true)
  [ "$n" -gt 0 ] || { echo "FAIL wxflip: 0 trace lines"; return 1; }
  bad=$(echo "$out" | LC_ALL=C /usr/bin/grep 'trace:wxflip' | LC_ALL=C /usr/bin/grep -Ev 'trace:wxflip:virtual_handle_fault 0x[0-9a-f]+ -> r[wx]$' || true)
  [ -z "$bad" ] || { echo "FAIL wxflip: odd trace line: $(echo "$bad" | head -n 1)"; return 1; }
  for to in rx rw; do
    echo "$out" | LC_ALL=C /usr/bin/grep -q "trace:wxflip:virtual_handle_fault 0x[0-9a-f]* -> $to\$" || { echo "FAIL wxflip: no flip to $to"; return 1; }
  done
  echo "$out" | LC_ALL=C /usr/bin/grep -qx 'PASS arm64-wxflip' || { echo "FAIL wxflip: the program did not pass"; return 1; }
  echo "info $n trace lines"
  [ "$n" -ge 10 ] || { echo "FAIL wxflip: $n trace lines, wanted at least 10"; return 1; }
}

# Gate S4 (ship-base spec §8): x64 code that rewrites its own RWX memory under FEX never flips W^X. x64-smc allocates
# PAGE_EXECUTE_READWRITE memory and makes its own .text RWX, then rewrites and runs code in both; every trace line
# would be a flip. wxflip, run first, shows the trace counts flips.
wxflip_x64_cmd() {
  out=$(WINEDEBUG=+wxflip wine_run "$TESTS/x64-smc.exe" 2>&1 | tr -d '\r') || true
  echo "$out"
  n=$(echo "$out" | LC_ALL=C /usr/bin/grep -c 'trace:wxflip' || true)
  echo "info wxflip-x64: $n flips"
  echo "$out" | LC_ALL=C /usr/bin/grep -qx 'PASS x64-smc' || { echo "FAIL wxflip-x64: no PASS line"; return 1; }
  [ "$n" = 0 ] || { echo "FAIL wxflip-x64: $n flips"; return 1; }
}

# Gate S3 (ship-base spec §6): msync, in both modes. The step owns the prefix's wineserver: it kills the running one,
# then per mode starts one by hand (its stderr, where msync says it is up and what failed, in msync-server-<mode>.log),
# runs x64-sync, runs a client of the other mode, which has to exit non-zero with its own message (Wine's err class on,
# whatever the caller's WINEDEBUG), and kills the server. The time rows are reported, not gated.
msync_cmd() {
  S="$TOOL/Contents/Resources/bin/wineserver"
  # The crash dialog off: a crash ends the run.
  wine_run reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f || return 1
  WINEPREFIX="$PFX" "$S" -k || true  # exits 1 when no server was running
  for m in ${MSYNC_MODES:-1 0}; do  # MSYNC_MODES=1 (or 0) runs one mode: the red runs
    o=$((1 - m)) slog="$WORK/msync-server-$m.log"
    WINEMSYNC=$m WINEPREFIX="$PFX" "$S" -p 2> "$slog" || { echo "FAIL msync: wineserver -p, WINEMSYNC=$m: exit $?"; return 1; }
    out=$(WINEMSYNC=$m exe_cmd x64-sync) && rc=0 || rc=$?
    echo "$out" | sed -E "s/^(time|info) /info msync $m /"
    cp "$WORK/x64-sync.err" "$WORK/x64-sync-$m.err"
    [ "$rc" = 0 ] || { echo "FAIL msync: WINEMSYNC=$m: $(echo "$out" | LC_ALL=C /usr/bin/grep -m 1 '^FAIL' || echo "$out" | tail -n 1)"; return 1; }
    dup='info wait-all-duplicate auto-event invalid-parameter semaphore invalid-parameter mutex invalid-parameter'
    [ "$m" = 0 ] || echo "$out" | LC_ALL=C /usr/bin/grep -qx "$dup" \
      || { echo "FAIL msync: WINEMSYNC=1: a duplicate in a wait-all isn't ERROR_INVALID_PARAMETER"; return 1; }
    # The server has answered x64-sync, so it is past msync's start: the line is there in mode 1, absent in mode 0.
    n=$(LC_ALL=C /usr/bin/grep -c '^msync: up and running\.$' "$slog" || true)
    [ "$n" = "$m" ] || { echo "FAIL msync: WINEMSYNC=$m: 'msync: up and running.' $n times in ${slog#"$ROOT"/}"; return 1; }
    if [ $m = 1 ]; then want="Server is running with WINEMSYNC but this process is not"; else want="Failed bootstrap_look_up"; fi
    err=$(WINEMSYNC=$o WINEDEBUG=err+all wine_run "$TESTS/x64-sync.exe" 2>&1 > /dev/null) && rc=0 || rc=$?
    hit=$(printf '%s\n' "$err" | LC_ALL=C /usr/bin/grep -m 1 -F "$want" || true)
    echo "a WINEMSYNC=$o client: exit $rc: ${hit:-$(printf '%s\n' "$err" | tail -n 1)}"
    [ "$rc" != 0 ] || { echo "FAIL msync: a WINEMSYNC=$o client ran on the WINEMSYNC=$m server"; return 1; }
    [ -n "$hit" ] || { echo "FAIL msync: a WINEMSYNC=$o client on the WINEMSYNC=$m server didn't say '$want'"; return 1; }
    WINEPREFIX="$PFX" "$S" -k || { echo "FAIL msync: the WINEMSYNC=$m server was gone before -k"; return 1; }
    bad=$(LC_ALL=C /usr/bin/grep -E "msync: (error|failed|couldn't)" "$slog" || true)
    [ -z "$bad" ] || { echo "$bad"; echo "FAIL msync: WINEMSYNC=$m: msync errors in ${slog#"$ROOT"/}"; return 1; }
  done
}

# Gate S5 (ship-base spec §9): strict x18. T1 arm64-x18v: 16 threads keep x18 == their TEB through preemption. T2
# arm64-x18path and x64-x18path (under FEX): x18 is the TEB again after every way between PE and unix code. T4
# arm64-x18path stress: the same while a fifth thread suspends the four that run syscalls and unix calls. T3: a
# toggle imbalance (ntdll's WINE_X18_SELFTEST=double_on enables the mode twice at process start) reaches the toggle's
# trap, which ntdll recognises and, in self-test mode only, ends with a line and _exit(133) (patch 0018) instead of
# the real SIG_DFL re-raise, so no crash report or dialog appears: never a Windows exception, and no new
# DiagnosticReports/wine-*.ips. Statically, ntdll.so names x18 only in the routines that move between PE and unix code.
# Nothing may print ntdll's "x18: PE stack running OFF". Each run's stderr is kept as x18-<test>[-<arg>].err.
X18_ROUTINES="___wine_syscall_dispatcher ___wine_unix_call_dispatcher _call_user_mode_callback"
X18_ROUTINES="$X18_ROUTINES ___wine_syscall_dispatcher_return"
x18_run() {  # x18_run <test> [arg]
  out=$(exe_cmd "$@") && rc=0 || rc=$?
  echo "$out"
  cp "$WORK/$1.err" "$WORK/x18-$1${2:+-$2}.err"
  [ "$rc" = 0 ] || { echo "FAIL x18: $*: $(echo "$out" | LC_ALL=C /usr/bin/grep -m 1 '^FAIL' || echo "$out" | tail -n 1)"; }
  return "$rc"
}
x18_cmd() {
  # The crash dialog off: a crash ends the run.
  wine_run reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f || return 1
  x18_run arm64-x18v && x18_run arm64-x18path && x18_run x64-x18path && x18_run arm64-x18path stress || return 1
  ips_before=$(ls "$HOME"/Library/Logs/DiagnosticReports/wine-*.ips 2> /dev/null || true)
  WINE_X18_SELFTEST=double_on wine_run "$TESTS/arm64-hello.exe" > "$WORK/x18-t3.log" 2>&1 && rc=0 || rc=$?
  echo "T3: WINE_X18_SELFTEST=double_on: exit $rc"
  [ "$rc" = 133 ] || { echo "FAIL x18: T3 exited $rc, not 133"; return 1; }
  ! LC_ALL=C /usr/bin/grep -q 'err:seh' "$WORK/x18-t3.log" || { echo "FAIL x18: T3 logged err:seh"; return 1; }
  so="$TOOL/Contents/Resources/lib/wine/aarch64-unix/ntdll.so"
  h=$(sh "$ROOT/wine-arm64/tools/x18scan.sh" -arch arm64 "$so") || { echo "FAIL x18: x18scan.sh failed on ntdll.so"; return 1; }
  [ -n "$h" ] || { echo "FAIL x18: ntdll.so names x18 nowhere"; return 1; }
  bad=$(echo "$h" | awk -v ok=" $X18_ROUTINES " 'index(ok, " " $1 " ") == 0')
  [ -z "$bad" ] || { echo "$bad"; echo "FAIL x18: ntdll.so names x18 outside $X18_ROUTINES"; return 1; }
  echo "info x18: ntdll.so names x18 $(echo "$h" | LC_ALL=C /usr/bin/grep -c .) times, in $(echo "$h" | awk '{ print $1 }' | sort -u | tr '\n' ' ')"
  off=$(LC_ALL=C /usr/bin/grep -l 'x18: PE stack running OFF' "$WORK"/x18-*.err "$WORK/x18-t3.log" || true)
  [ -z "$off" ] || { echo "FAIL x18: 'x18: PE stack running OFF' in $(echo "$off" | tr '\n' ' ')"; return 1; }
  # ReportCrash writes from the dead process's corpse, after T3 has returned: the listing comes last.
  sleep 2
  ips_new=$( (ls "$HOME"/Library/Logs/DiagnosticReports/wine-*.ips 2> /dev/null || true) | LC_ALL=C /usr/bin/grep -v -x -F "$ips_before" || true)
  echo "info x18 crash reports: $(echo "$ips_new" | LC_ALL=C /usr/bin/grep -c . || true)"
  LC_ALL=C /usr/bin/grep -q -F 'x18: toggle trap passed through (self-test)' "$WORK/x18-t3.log" ||
    { echo "FAIL x18: T3's log lacks 'x18: toggle trap passed through (self-test)'"; return 1; }
  [ -z "$ips_new" ] || { echo "FAIL x18: T3 left a crash report: $ips_new"; return 1; }
}

# x64-bench's rows (gates G5 and G4). bench_rows <file>: fails, saying so, unless the run printed every one.
BENCH_ROWS=36
bench_rows() {
  n=$(grep -c '^row ' "$1" || true)
  [ "$n" = "$BENCH_ROWS" ] || { echo "${1#"$WORK"/} has $n of $BENCH_ROWS rows; it ends: $(tail -n 1 "$1")"; return 1; }
}

# Text and TLS (ship-base spec §5): the bundled FreeType and gnutls load where Wine dlopens them. Wine says so on
# stderr when one doesn't; those lines come before the FAIL line.
fonts_tls_cmd() {
  exe_cmd arm64-fonts-tls "Z:$ROOT/wine-arm64/tests/fixtures/fonts-tls.pfx" && rc=0 || rc=1
  err="$WORK/arm64-fonts-tls.err"
  bad=$(tr -d '\r' < "$err" | LC_ALL=C /usr/bin/grep -iE 'cannot find the FreeType|failed to load libgnutls' || true)
  [ -z "$bad" ] || { echo "$bad"; echo "FAIL fonts-tls: a library didn't load (${err#"$ROOT"/})"; return 1; }
  return $rc
}

# Gate S7 (ship-base spec §7): the Steam bridge on this runtime. bridge/check.sh in arm64 mode (the aarch64 steam.exe,
# no Steam needed), then bridge/probe.sh in arm64 mode: the bundle's lsteamclient.dll as steamclient64.dll, the x64
# steamprobe.exe under FEX with SMITE 2's steam_api64.dll, against Mac Steam's steamclient.dylib (or the one in
# STEAM_COMPAT_CLIENT_INSTALL_PATH, passed through). PROBE_REDACT=1: the log never holds the SteamID or persona name.
# The crash dialog is off, so a crash ends the run. The x18 hits in Valve's arm64 code (data after ret today) are
# reported, not gated. The probe's redaction self-test runs first.
# Gate L3 (release spec §9): then both again through the launcher of a tool folder assembled from this runtime with
# `macneutron install`, in one compat folder (bridge/check.sh's launcher pass, then the probe as Steam starts a game).
SMITE2_API="$HOME/Library/Application Support/Steam/steamapps/common/SMITE 2/Windows/Engine/Binaries/ThirdParty"
SMITE2_API="$SMITE2_API/Steamworks/Steamv157/Win64/steam_api64.dll"
MAC_STEAM="$HOME/Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS"
# probe_rows <run>: the redacted probe output in $out (exit $rc) has every row, and an auth ticket of more than 0 bytes.
probe_rows() {
  echo "$out"
  echo "info steam-bridge: $1 probe exit $rc"
  has() { echo "$out" | LC_ALL=C /usr/bin/grep -qx "$1"; }
  m=$(echo "$out" | LC_ALL=C /usr/bin/grep -m 1 '^init message: ' || true)
  ! has 'init: FAIL' || { echo "FAIL steam-bridge: $1: SteamAPI_Init failed: is Steam running and logged in?${m:+ ($m)}"
    return 1; }
  for want in 'init: ok' 'steamid ok' 'persona ok' 'auth ticket: callback, result 1' 'fault: caught'; do
    has "$want" || { echo "FAIL steam-bridge: $1: no '$want' line"; return 1; }
  done
  n=$(echo "$out" | sed -n 's/^auth ticket: handle [0-9]*, \([0-9]*\) bytes$/\1/p' | head -n 1)
  [ "${n:-0}" -gt 0 ] || { echo "FAIL steam-bridge: $1: auth ticket of ${n:-no} bytes"; return 1; }
  echo "info steam-bridge: $1: steamid ok, ticket $n bytes"
}
steam_bridge_cmd() {
  sh "$ROOT/bridge/probe.sh" --redact-self-test \
    || { echo "FAIL steam-bridge: the probe's redaction self-test failed"; return 1; }
  client="${STEAM_COMPAT_CLIENT_INSTALL_PATH:-$MAC_STEAM}"
  [ -f "$client/steamclient.dylib" ] || { echo "Steam's steamclient.dylib not found at $client/steamclient.dylib"; return 1; }
  [ -f "$SMITE2_API" ] || { echo "SMITE 2 isn't installed"; return 1; }
  wine_run reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f || return 1
  out=$(BRIDGE_CHECK_WORK="$WORK/steam-bridge ü" MACNEUTRON_ARM64_APP="$TOOL" MACNEUTRON_ARM64_PREFIX="$PFX" \
    sh "$ROOT/bridge/check.sh" 2>&1) && rc=0 || rc=$?
  echo "$out"
  [ "$rc" = 0 ] || { echo "FAIL steam-bridge: bridge/check.sh: $(echo "$out" | LC_ALL=C /usr/bin/grep -m 1 '^FAIL' \
    || echo "$out" | tail -n 1)"; return 1; }
  out=$(PROBE_REDACT=1 STEAM_COMPAT_CLIENT_INSTALL_PATH="$client" MACNEUTRON_ARM64_APP="$TOOL" \
    MACNEUTRON_ARM64_PREFIX="$PFX" sh "$ROOT/bridge/probe.sh" "$SMITE2_API" 2>&1) && rc=0 || rc=$?
  probe_rows direct || return 1
  "$ROOT/.build/release/macneutron" install --tool-dir "$BTOOL" --wine-app "$TOOL" \
    --steam-exe "$ROOT/build/bridge/arm64/steam.exe" || return 1
  out=$(BRIDGE_CHECK_WORK="${BCOMPAT%/compat}" MACNEUTRON_TOOL_DIR="$BTOOL" sh "$ROOT/bridge/check.sh" 2>&1) \
    && rc=0 || rc=$?
  echo "$out"
  [ "$rc" = 0 ] || { echo "FAIL steam-bridge: bridge/check.sh through the launcher: $(echo "$out" \
    | LC_ALL=C /usr/bin/grep -m 1 '^FAIL' || echo "$out" | tail -n 1)"; return 1; }
  out=$(PROBE_REDACT=1 STEAM_COMPAT_CLIENT_INSTALL_PATH="$client" STEAM_COMPAT_DATA_PATH="$BCOMPAT" \
    MACNEUTRON_TOOL_DIR="$BTOOL" sh "$ROOT/bridge/probe.sh" "$SMITE2_API" 2>&1) && rc=0 || rc=$?
  probe_rows launcher || return 1
  if h=$(sh "$ROOT/wine-arm64/tools/x18scan.sh" -arch arm64 "$client/steamclient.dylib"); then
    echo "info steam x18: $(echo "$h" | LC_ALL=C /usr/bin/grep -c . || true) hits"
  else
    echo "info steam x18: the scan failed"
  fi
}

# Gate G5 (spec §8): FEX's code memory never flips W^X (patch 12's trace) once a program runs, over one full x64-bench
# run. It calls OutputDebugStringA("jit: start") (kernel32 WARNs it on debugstr) before its rows; the flips counted
# are those after it, and the run has to print every row. A log with no marker fails: its count would mean nothing.
# FEX runs with its defaults, as in G2 and G4.
g5_jit_cmd() {
  log="$WORK/g5-x64-bench.log" out="$WORK/g5-x64-bench.txt"
  # shellcheck disable=SC2046  # unfex prints a list of options
  env $(unfex) WINEDEBUG=+wxflip,warn+debugstr,warn+seh WINEPREFIX="$PFX" "$TOOL/Contents/MacOS/wine" \
    "$TESTS/x64-bench.exe" 2> "$log" | tr -d '\r' > "$out" || true
  cat "$out"
  grep -q 'jit: start' "$log" || { echo "FAIL g5-jit: no marker in ${log#"$ROOT"/}"; return 1; }
  bench_rows "$out" || return 1
  n=$(sed -n '/jit: start/,$p' "$log" | grep -c 'trace:wxflip' || true)
  echo "info x64-bench: $n flips after the marker"
  [ "$n" = 0 ] || { echo "FAIL g5-jit: $n flips"; return 1; }
}

# FEX patch 0006 (batch Task 4): EVMD ranges turn TSO off in every block inside them, as FEX documents
# (Config.json.in:582-597), and listed instructions keep it. Every check runs; the last line names each failure.
# VMD_CALIBRATE=1 adds three x64-bench runs with TSO off everywhere (reported, not gated).
VMD_RATIO=0.75
fex_vmd_cmd() {
  T="$(sh "$ROOT/dxmt/toolchain.sh")"
  fails=
  # The crash dialog off: a crash ends the run.
  wine_run reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f > /dev/null || return 1
  # 1. x64-litmus with the whole module as its range: MP reordering shows, and FEX logs the module's coverage.
  size=$("$T/llvm-readobj" --file-headers "$TESTS/x64-litmus.exe" | awk '/SizeOfImage:/ { print $2; exit }')
  # shellcheck disable=SC2046  # unfex prints a list of options
  out=$(env $(unfex) FEX_SILENTLOG=0 FEX_EXTENDEDVOLATILEMETADATA=x64-litmus.exe WINEPREFIX="$PFX" \
    "$TOOL/Contents/MacOS/wine" "$TESTS/x64-litmus.exe" 10000000 2> "$WORK/fex-vmd-module.err" | tr -d '\r') || true
  echo "$out"
  f=$(echo "$out" | sed -n 's/^litmus MP forbidden=\([0-9]*\) runs=10000000$/\1/p')
  echo "info fex-vmd x64-litmus MP forbidden=$f"
  [ "${f:-0}" -ge 1 ] || fails="$fails${fails:+; }x64-litmus with EVMD over the whole module: MP forbidden=${f:-none}: the range didn't turn TSO off"
  tr -d '\r' < "$WORK/fex-vmd-module.err" | LC_ALL=C /usr/bin/grep -qE "volatile metadata: x64-litmus\.exe at [0-9A-F]+: 0 instructions, 1 ranges, $size bytes\$" \
    || fails="$fails${fails:+; }FEX logged no coverage line for x64-litmus.exe (0 instructions, 1 range, $size bytes)"
  # 2. The same range with every instruction of the litmus kernels run and worker listed: they keep TSO.
  base=$("$T/llvm-readobj" --file-headers "$TESTS/x64-litmus.exe" | awk '/ImageBase:/ { print $2; exit }')
  listed=$("$T/llvm-objdump" -d --no-show-raw-insn --disassemble-symbols=run,worker "$TESTS/x64-litmus.exe" | awk -v b=$((base)) '
    function hex(s,  i, v) { for (i = 1; i <= length(s); i++) v = v * 16 + index("0123456789abcdef", substr(s, i, 1)) - 1; return v }
    /^[[:space:]]*[0-9a-f]+:/ { sub(/:.*/, ""); sub(/^[[:space:]]*/, ""); printf "%s0x%x", k++ ? "," : "", hex($0) - b }')
  k=$(echo "$listed" | tr ',' '\n' | LC_ALL=C /usr/bin/grep -c . || true)
  if [ "$k" = 0 ]; then
    fails="$fails${fails:+; }x64-litmus.exe has no run and worker of its own"
  else
    # shellcheck disable=SC2046  # unfex prints a list of options
    out=$(env $(unfex) FEX_SILENTLOG=0 FEX_EXTENDEDVOLATILEMETADATA="x64-litmus.exe;0x0-$(printf 0x%x "$size");$listed" \
      WINEPREFIX="$PFX" "$TOOL/Contents/MacOS/wine" "$TESTS/x64-litmus.exe" 10000000 2> "$WORK/fex-vmd-listed.err" \
      | tr -d '\r') || true
    echo "$out"
    f=$(echo "$out" | sed -n 's/^litmus MP forbidden=\([0-9]*\) runs=10000000$/\1/p')
    echo "info fex-vmd x64-litmus listed MP forbidden=$f"
    [ "${f:-none}" = 0 ] || fails="$fails${fails:+; }x64-litmus with run and worker listed: MP forbidden=${f:-none}: a listed access lost TSO"
    tr -d '\r' < "$WORK/fex-vmd-listed.err" | LC_ALL=C /usr/bin/grep -qE "volatile metadata: x64-litmus\.exe at [0-9A-F]+: $k instructions, 1 ranges, $size bytes\$" \
      || fails="$fails${fails:+; }FEX logged no coverage line for x64-litmus.exe ($k instructions, 1 range, $size bytes)"
  fi
  # 3. The same range with only MP's flag accesses listed, worker's store and run's load: MSVC's shape, where listed
  # accesses order their unlisted neighbours. MP stays at 0.
  # shellcheck disable=SC2046  # one address per line
  set -- $("$T/llvm-objdump" -d --no-show-raw-insn --disassemble-symbols=run,worker "$TESTS/x64-litmus.exe" | awk '
    / <(run|worker)>:$/ { fn = $2 }
    /<flag>$/ && (fn == "<worker>:" && /\(%rip\)[[:space:]]+#/ || fn == "<run>:" && /\(%rip\), %/) {
      sub(/:.*/, ""); sub(/^[[:space:]]*/, ""); print }')
  if [ $# != 2 ]; then
    fails="$fails${fails:+; }x64-litmus.exe's worker and run don't hold one flag store and one flag load ($# accesses)"
  else
    flags=$(printf '0x%x,0x%x' $((0x$1 - base)) $((0x$2 - base)))
    # shellcheck disable=SC2046  # unfex prints a list of options
    out=$(env $(unfex) FEX_SILENTLOG=0 FEX_EXTENDEDVOLATILEMETADATA="x64-litmus.exe;0x0-$(printf 0x%x "$size");$flags" \
      WINEPREFIX="$PFX" "$TOOL/Contents/MacOS/wine" "$TESTS/x64-litmus.exe" 10000000 2> "$WORK/fex-vmd-flags.err" \
      | tr -d '\r') || true
    echo "$out"
    f=$(echo "$out" | sed -n 's/^litmus MP forbidden=\([0-9]*\) runs=10000000$/\1/p')
    echo "info fex-vmd x64-litmus flags $flags MP forbidden=$f"
    [ "${f:-none}" = 0 ] || fails="$fails${fails:+; }x64-litmus with only its flag store and load listed: MP forbidden=${f:-none}: a listed access didn't order its neighbours"
    tr -d '\r' < "$WORK/fex-vmd-flags.err" | LC_ALL=C /usr/bin/grep -qE "volatile metadata: x64-litmus\.exe at [0-9A-F]+: 2 instructions, 1 ranges, $size bytes\$" \
      || fails="$fails${fails:+; }FEX logged no coverage line for x64-litmus.exe (2 instructions, 1 range, $size bytes)"
  fi
  # 4. x64-bench's two scalar-memory kernels, each range running to the next symbol: with their ranges, at most
  # VMD_RATIO of their default time.
  base=$("$T/llvm-readobj" --file-headers "$TESTS/x64-bench.exe" | awk '/ImageBase:/ { print $2; exit }')
  # shellcheck disable=SC2046  # each kernel's start and the next symbol's
  set -- $("$T/llvm-nm" -n "$TESTS/x64-bench.exe" | awk 'p { print a, $1; p = 0 } $3 ~ /mem_seq_(read|write)v$/ { a = $1; p = 1 }')
  if [ $# = 4 ]; then
    evmd=$(printf 'x64-bench.exe;0x%x-0x%x,0x%x-0x%x' $((0x$1 - base)) $((0x$2 - base)) $((0x$3 - base)) $((0x$4 - base)))
    echo "info fex-vmd ranges $evmd"
    kinds="default ranges" w="$WORK/vmd"
    [ "${VMD_CALIBRATE:-0}" != 1 ] || kinds="$kinds tso-off"
    for i in 1 2 3; do
      for kind in $kinds; do
        case $kind in default) e= ;; ranges) e="FEX_EXTENDEDVOLATILEMETADATA=$evmd" ;; tso-off) e=FEX_TSOENABLED=0 ;; esac
        mkdir -p "$w/$kind"
        # shellcheck disable=SC2046,SC2086  # unfex prints a list of options; e is one assignment or none
        env $(unfex) $e WINEDEBUG=-all WINEPREFIX="$PFX" "$TOOL/Contents/MacOS/wine" "$TESTS/x64-bench.exe" \
          2> "$w/$kind/run$i.err" | tr -d '\r' > "$w/$kind/run$i.txt" || true
        msg=$(bench_rows "$w/$kind/run$i.txt") && continue
        # TSO off everywhere breaks x64-bench's own SPSC ring (mt_spsc_ring, after the mem_seq rows): reported only.
        if [ "$kind" = tso-off ]; then echo "info fex-vmd tso-off: $msg"; else fails="$fails${fails:+; }$msg"; fi
      done
    done
    med() { cat "$w/$1"/run*.txt | sed -n "s/^row $r \([0-9.]*\)\$/\1/p" | sort -n | sed -n 2p; }  # med <kind>: row $r's
    for r in mem_seq_read mem_seq_write; do
      d=$(med default) v=$(med ranges) o=
      [ "${VMD_CALIBRATE:-0}" != 1 ] || o=$(med tso-off)
      echo "info fex-vmd $r default $d ranges $v${o:+ tso-off $o}"
      { [ -n "$d" ] && [ -n "$v" ] && awk -v v="$v" -v d="$d" -v m="$VMD_RATIO" 'BEGIN { exit !(v <= m * d) }'; } \
        || fails="$fails${fails:+; }$r took ${v:-no} s with its range, ${d:-no} s without: the range didn't turn TSO off"
    done
  else
    fails="$fails${fails:+; }x64-bench.exe has no mem_seq_read and mem_seq_write of their own"
  fi
  [ -z "$fails" ] || { echo "FAIL fex-vmd: $fails"; return 1; }
}

# Gate G4 (spec §8), measured, not gated: x64-bench, five processes per side, the sides alternating so drift falls on
# both alike. FEX: this stack, with FEX's defaults. Rosetta: the frozen reference's own launcher, in a clone of it (the
# pinned runtime-v4.7.3), with dxmt/check.sh's environment; that launcher adds ROSETTA_ADVERTISE_AVX=1 and
# WINEMSYNC=1. Both sides run with WINEDEBUG=-all, the launcher's default. Passes when every run printed every row;
# bench_report.py's table (FEX time / Rosetta time per row) says how fast. Output in $WORK/bench.
rosetta() {  # rosetta <launch verb> <args...>
  env STEAM_COMPAT_DATA_PATH="$RPFX" SteamAppId=0 MACNEUTRON_NO_STEAM_BRIDGE=1 MACNEUTRON_NO_METALFX=1 \
    "$RTOOL/bin/macneutron" launch "$@"
}
g4_bench_cmd() {
  v=$(cat "$REF/runtime-version" 2> /dev/null || true)
  [ "$v" = runtime-v4.7.3 ] || { echo "the reference at $REF holds ${v:-no runtime}, not runtime-v4.7.3"; return 1; }
  # The folder itself, not a symlink to it: cp -R would copy the link, and the launches would run in the reference.
  cp -cR "$(cd "$REF" && pwd -P)" "$RTOOL" || return 1
  rosetta getcompatpath "$WORK" > /dev/null || { echo "creating the Rosetta prefix failed"; return 1; }
  b="$WORK/bench"
  mkdir -p "$b/fex" "$b/rosetta"
  for i in 1 2 3 4 5; do
    t0=$(date +%s)
    # shellcheck disable=SC2046  # unfex prints a list of options
    env $(unfex) WINEDEBUG=-all WINEPREFIX="$PFX" "$TOOL/Contents/MacOS/wine" "$TESTS/x64-bench.exe" \
      2> "$b/fex/run$i.err" | tr -d '\r' > "$b/fex/run$i.txt" || true
    t1=$(date +%s)
    WINEDEBUG=-all rosetta waitforexitandrun "$TESTS/x64-bench.exe" 2> "$b/rosetta/run$i.err" | tr -d '\r' \
      > "$b/rosetta/run$i.txt" || true
    echo "info run $i: fex $((t1 - t0)) s, rosetta $(($(date +%s) - t1)) s"
  done
  for f in "$b"/fex/run*.txt "$b"/rosetta/run*.txt; do bench_rows "$f" || return 1; done
  echo "info fex: $(grep '^cpuid ' "$b/fex/run1.txt")"
  echo "info rosetta: $(grep '^cpuid ' "$b/rosetta/run1.txt")"
  python3 "$ROOT/wine-arm64/tools/bench_report.py" "$b/fex" "$b/rosetta" > "$b/report.txt" || return 1
  cat "$b/report.txt"
}

# DXMT in the prefix (arm64 DXMT spec §6, §7): the bundle's front ends copied into system32, as the launcher will, and
# the crash dialog off, so a crash ends the run instead of waiting for a watchdog. The markers and the version are
# checked as bundle.sh checks them (Wine's builtin marker is bytes 64-79), and every front end in system32 has to be the
# bundle's. Ends once the prefix's server has exited, so a later clone of the prefix gets a saved registry.
builtin() { [ "$(dd if="$1" bs=1 skip=64 count=16 2> /dev/null)" = "Wine builtin DLL" ]; }
dxmt_cmd() {
  . "$ROOT/dxmt/pins"  # DXMT_COMMIT
  d="$TOOL/Contents/Resources/DXMT" sys="$PFX/drive_c/windows/system32"
  cp "$d/aarch64-windows/"* "$sys/" || return 1
  wine_run reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f || return 1
  builtin "$TOOL/Contents/Resources/lib/wine/aarch64-windows/winemetal.dll" \
    || { echo "winemetal.dll lacks Wine's builtin marker"; return 1; }
  for f in "$d/aarch64-windows/"*; do
    cmp -s "$f" "$sys/${f##*/}" || { echo "system32/${f##*/} is not the bundle's"; return 1; }
    ! builtin "$sys/${f##*/}" || { echo "system32/${f##*/} carries Wine's builtin marker"; return 1; }
  done
  ver=$(cat "$d/version") || return 1
  case $ver in "$DXMT_COMMIT"+?*) ;; *) echo "DXMT/version is '$ver', not $DXMT_COMMIT+<series or dev>"; return 1 ;; esac
  echo "waiting for the prefix's wineserver"
  WINEPREFIX="$PFX" "$TOOL/Contents/Resources/bin/wineserver" -w
}

# Gate D2's minimum winshot shares (percent of the window's pixels), from Task 4's measurement of the same programs on
# our DXMT under the Rosetta runtime (dark mode, 1x display): present_loop green 71 white 24, d3d12_clear green 95
# white 0. Green keeps 15 points of margin, white half the measured share (a light-mode title bar adds about 4 white).
LOOP_GREEN=56 LOOP_WHITE=12 CLEAR_GREEN=80

# onscreen <lane> <exe> <completion line> <min green> <min white> <args...>: runs the program on DXMT in the
# background, reads its window (titled after the program) with winshot, then waits for the program, whatever winshot
# said. Passes when the program printed a line starting with the completion line and the shares reach the minimums.
# Files: $WORK/<lane>-<program>.png, .txt (stdout), .err (stderr, kept out of the log so a FAIL line stays last).
onscreen() {
  lane=$1 exe=$2 want=$3 green=$4 white=$5; shift 5
  p=${exe##*/}; p=${p%.exe}; o="$WORK/$lane-$p"
  echo "running $lane $p"
  dxmt_run "$exe" "$@" > "$o.raw" 2> "$o.err" &
  bg=$!
  shot=$("$TESTS/winshot" "$p" "$o.png" 2>&1) && shot_ok=1 || shot_ok=0
  wait "$bg" && rc=0 || rc=$?
  tr -d '\r' < "$o.raw" > "$o.txt"  # CRLF line ends: text mode
  cat "$o.txt"
  if [ "$shot_ok" = 1 ]; then echo "info $lane $p: $shot"; else echo "$shot"; fi
  # shellcheck disable=SC2046  # pixels <n> green <pct> white <pct>
  set -- $(printf '%s\n' "$shot" | grep '^pixels')
  if ! grep -q "^$want" "$o.txt"; then why="no '$want' line, exit $rc"
  elif [ "$shot_ok" = 0 ]; then why=$(echo "$shot" | tail -n 1)
  elif [ "${4:-0}" -lt "$green" ]; then why="green ${4:-none} < $green"
  elif [ "${6:-0}" -lt "$white" ]; then why="white ${6:-none} < $white"
  else return 0; fi
  tr -d '\r' < "$o.err" | tail -n 5
  echo "FAIL dxmt-present: $lane $p: $why"
  return 1
}

# Gate D2 (arm64 DXMT spec §7, §8): D3D11 and D3D12 windows on screen in both lanes (ARM64EC programs natively, x64
# programs under FEX), then, ARM64EC only, 20 rounds of window, device and swap chain torn down in both orders.
dxmt_present_cmd() {
  ec="$B/dxmt-tests-arm64ec"
  for lane in arm64ec x64; do
    if [ $lane = arm64ec ]; then loop="$ec/present_loop.exe" clear="$ec/d3d12_clear.exe"
    else loop="$B/presenter/present_loop.exe" clear="$B/dxmt-tests/d3d12_clear.exe"; fi
    onscreen $lane "$loop" "frames 3000," $LOOP_GREEN $LOOP_WHITE 1280 720 1280 720 3000 0 || return 1
    onscreen $lane "$clear" "presented 3000/3000 frames" $CLEAR_GREEN 0 3000 || return 1
  done
  o="$WORK/arm64ec-cycles" what="arm64ec present_loop cycles=20"
  echo "running $what"
  dxmt_run "$ec/present_loop.exe" 640 360 640 360 60 0 cycles=20 > "$o.raw" 2> "$o.err" && rc=0 || rc=$?
  tr -d '\r' < "$o.raw" > "$o.txt"
  cat "$o.txt"
  why=
  grep -qx 'cycles 20 ok' "$o.txt" || why="no 'cycles 20 ok' line, exit $rc"
  [ -n "$why" ] || [ "$rc" = 0 ] || why="exit $rc"
  [ -z "$why" ] || { tr -d '\r' < "$o.err" | tail -n 5; echo "FAIL dxmt-present: $what: $why"; return 1; }
  echo "info $what: cycles 20 ok"
}

# dxmt/check.sh (arm64 DXMT spec §7, arm64 release spec §8.2): our DXMT on this runtime, through the launcher in a tool
# folder it assembles, against D3DMetal on the frozen reference. dxmt_lane_cmd <lane> <machine> <tests folder>
# <present_loop.exe> [line it must print]: passes when every program is built for <machine> (ARM64EC or AMD64, as
# llvm-readobj reads the hybrid metadata: both lanes' headers say 0x8664), the check ran in arm64 mode and all passed;
# else its last line gives the number of FAIL lines and the first (none: the check's own), or, for a missing line,
# dxmt/check.sh's skip line for it (FSR 3: SMITE 2 isn't installed).
dxmt_lane_cmd() {
  l="$WORK/dxmt-$1.log" t0=$(date +%s) ro="$(sh "$ROOT/dxmt/toolchain.sh")/llvm-readobj"
  for e in "$3"/*.exe "$4"; do
    "$ro" --file-headers "$e" | grep -q "Machine: IMAGE_FILE_MACHINE_$2 " || { echo "${e##*/} is not built for $2"; return 1; }
  done
  DXMT_CHECK_WORK="$WORK/dxmt-$1" MACNEUTRON_ARM64_APP="$TOOL" MACNEUTRON_ARM64_TESTS="$3" MACNEUTRON_ARM64_LOOP="$4" \
    MACNEUTRON_ARM64_TOOLS="$B/wine-arm64" sh "$ROOT/dxmt/check.sh" || true
  echo "info dxmt-$1: $(($(date +%s) - t0)) s"
  grep -q '^info arm64 mode: ' "$l" || { echo "dxmt/check.sh did not run in arm64 mode"; return 1; }
  n=$(grep -c '^FAIL' "$l" || true)
  [ "$n" = 0 ] || { echo "$n FAIL lines; first: $(grep -m 1 '^FAIL' "$l")"; return 1; }
  grep -qx 'dxmt-check: all passed' "$l" || { grep -v '^info dxmt-' "$l" | tail -n 1; return 1; }
  [ -z "${5:-}" ] || grep -qxF "$5" "$l" || { echo "$(grep -m 1 '^skip the FSR 3' "$l" || echo "no '$5' line")"; return 1; }
}

# Video playback (video playback spec §8): Wine's own test clips, read in place by their Z: path. clip <path under
# dlls/> <sha256>: the clip's Z: path, or a failure naming it when Wine's tree holds another file there.
WD="$B/wine-arm64-src/wine/dlls"
clip() {
  [ "$(shasum -a 256 "$WD/$1" 2> /dev/null | cut -d ' ' -f 1)" = "$2" ] \
    || { echo "dlls/$1 is not the pinned clip"; return 1; }
  echo "Z:$WD/$1"
}
# media_run <step> <row> <args...>: the row in the step's arm64 program, then in its x64 twin under FEX, both always;
# the last line names every lane that failed, with the program's own FAIL line.
media_run() {
  s=$1; shift
  fails=
  for t in "arm64-$s" "x64-$s"; do
    out=$(exe_cmd "$t" "$@") || fails="$fails${fails:+; }$(echo "$out" | LC_ALL=C /usr/bin/grep -m 1 "^FAIL $t" \
      || echo "$t: $(echo "$out" | tail -n 1)")"
    echo "$out"
  done
  [ -z "$fails" ] || { echo "FAIL $s: $fails"; return 1; }
}
# media-mf: the source reader on test.mp4 (H.264 + AAC). DisableGstByteStreamHandler, set in this prefix until Wine's
# wine.inf sets it: MP4 files go to winedmo, not to the GStreamer handler, which this Wine doesn't build. The crash
# dialog off: a crash ends the run, with its stage lines, instead of waiting out the cap.
media_mf_cmd() {
  mp4=$(clip mfreadwrite/tests/test.mp4 485a145b3d3a82fe3356c574899b13467a8672c0bbfcf8a5d51b6bedb256408f) \
    || { echo "$mp4"; return 1; }
  wine_run reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f || return 1
  wine_run reg add 'HKCU\Software\Wine\MediaFoundation' /v DisableGstByteStreamHandler /t REG_DWORD /d 1 /f || return 1
  media_run media-mf open "$mp4"
}

# The measurement lanes (batch Task 2): x64-sync and arm64-xcall as ARM64, ARM64EC and x64 programs, the x64 ones under
# FEX with its defaults, in mode 1 against the prefix's server. Each has to pass and print all its time rows; each row
# becomes `info <program> <row> <ns>` (lanes_report.py's input), and the programs' own info lines `info <program>: ...`.
SYNC_ROWS=17 XCALL_ROWS=16
lanes_cmd() {
  export WINEDEBUG=-all
  for v in $(env | sed -n 's/^\(FEX_[A-Za-z0-9_]*\)=.*/\1/p'); do unset "$v"; done
  # The crash dialog off: a crash ends the run.
  wine_run reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f > /dev/null || return 1
  for t in arm64-sync arm64ec-sync x64-sync arm64-xcall arm64ec-xcall x64-xcall; do
    out=$(exe_cmd "$t") || { echo "$out"; echo "FAIL lanes: $t"; return 1; }
    case $t in *-sync) want=$SYNC_ROWS ;; *) want=$XCALL_ROWS ;; esac
    n=$(echo "$out" | LC_ALL=C /usr/bin/grep -c '^time ' || true)
    [ "$n" = "$want" ] || { echo "$out"; echo "FAIL lanes: $t printed $n of $want time rows"; return 1; }
    echo "$out" | sed -e "s/^info /info $t: /" -e "s/^time /info $t /"
  done
}

# Wine's own conformance tests (batch Task 8): each module's test program from `make wine-arm64-winetests`, in all
# three lanes (x64 under FEX), crash dialog off, no WINETEST_* variable of the caller's (WINETEST_PLATFORM turns
# todo_wine off, WINETEST_DEBUG=0 drops the summary line, WINETEST_REPORT_FLAKY changes the exit code). A test passes
# when it prints its summary line, exits with that line's failure count, and every location it fails at is listed in
# WINETESTS_FAILS as `<lane>:<file>:<line>`: failures it had before Wine 0033, never a critical-section line, after
# the baseline added only by a ruling. Output is kept as winetests-<lane>-<module>.out and .err (Wine's ERRs).
# msvcrt:string (batch Task 6, Wine patch 0035's string routines) had no failure on 0034 in any lane.
WINETESTS="ntdll:rtl kernel32:sync atl:module atl100:atl msvcirt:msvcirt msvcrt:string"
# kernel32 test_mutex (batch Task 8 baseline, Wine 0032): a mutex whose owner TerminateThread ends is never abandoned
# under msync (WINEMSYNC=0 passes), so its waiter times out.
WINETESTS_FAILS="arm64:sync.c:416 arm64:sync.c:420 arm64ec:sync.c:416 arm64ec:sync.c:420 x64:sync.c:416 x64:sync.c:420"
winetests_cmd() {
  for v in $(env | sed -n 's/^\(WINETEST_[A-Za-z0-9_]*\)=.*/\1/p'); do unset "$v"; done
  wine_run reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f > /dev/null || return 1
  fails=
  for l in arm64 arm64ec x64; do
    for t in $WINETESTS; do
      m=${t%%:*} n=${t#*:} o="$WORK/winetests-$l-$m"
      wine_run "$TESTS/winetests/$l/${m}_test.exe" "$n" > "$o.out" 2> "$o.err" && rc=0 || rc=$?
      s=$(tr -d '\r' < "$o.out" | sed -n "s/^[0-9a-f]*:$n: \([0-9]* tests executed .*\)/\1/p" | tail -n 1)
      f=$(echo "$s" | sed -n 's/.* \([0-9]*\) failures*), .*/\1/p')
      new=
      for k in $(tr -d '\r' < "$o.out" \
        | sed -nE 's/^([A-Za-z0-9_]+\.c:[0-9]+): Test (failed|succeeded inside todo block):.*/\1/p' | sort -u); do
        echo "info winetests-failed $l $k"
        case " $WINETESTS_FAILS " in *" $l:$k "*) ;; *) new="$new $k" ;; esac
      done
      echo "info winetests $l $t ${s:-no summary line} (exit $rc)"
      [ -n "$f" ] && [ "$rc" = "$f" ] && [ -z "$new" ] \
        || fails="$fails${fails:+; }$l $t: exit $rc, ${f:-no} failures${new:+, unlisted$new}"
    done
  done
  [ -z "$fails" ] || { echo "FAIL winetests: $fails"; return 1; }
}

# The ARM64EC auxiliary IAT (batch Task 5, Wine 0034): arm64ec-hook's rows, crash dialog off (the rows write x64 code
# into kernel32 and an IAT entry; a crash ends the run).
ec_hook_cmd() {
  wine_run reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f > /dev/null || return 1
  exe_cmd arm64ec-hook
}

# msvcrt's string routines (batch Task 6, Wine patch 0035): arm64-crt in all three lanes (x64 under FEX), each always,
# crash dialog off (a crash ends a program with its FAIL line); the last line names every program that failed, with its
# own FAIL line.
crt_cmd() {
  wine_run reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f || return 1
  fails=
  for t in arm64-crt arm64ec-crt x64-crt; do
    out=$(exe_cmd "$t") || fails="$fails${fails:+; }$(echo "$out" | LC_ALL=C /usr/bin/grep -m 1 "^FAIL $t:" \
      || echo "$t: $(echo "$out" | tail -n 1)")"
    echo "$out"
  done
  [ -z "$fails" ] || { echo "FAIL crt: $fails"; return 1; }
}

# A genuine steamclient64.dll (Wine patch 0036): x64-steamclient under FEX loads Valve's signed DLL from a copy here,
# then the bundle's lsteamclient.dll copied under the same name, which the loader must leave alone. Wine's trace+module
# lines show whose DllMain ran: lsteamclient.dll's (the control) and the builtin copy's, never the genuine one's.
STEAMCLIENT64="${VALVE_STEAMCLIENT64:-$B/valve/steamclient64.dll}"
dllmain_calls() {
  tr -d '\r' < "$WORK/x64-steamclient.err" | LC_ALL=C /usr/bin/grep -c "L\"$1\",PROCESS_ATTACH,.*) - CALL" || true
}
steamclient_cmd() {
  [ -f "$STEAMCLIENT64" ] || { echo "skip steamclient: no genuine steamclient64.dll at $STEAMCLIENT64"; return 0; }
  d="$WORK/steamclient"
  mkdir -p "$d/genuine" "$d/builtin"
  cp "$STEAMCLIENT64" "$d/genuine/steamclient64.dll"
  cp "$TOOL/Contents/Resources/lib/wine/aarch64-windows/lsteamclient.dll" "$d/builtin/steamclient64.dll"
  wine_run reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f > /dev/null || return 1
  WINEDEBUG=trace+module exe_cmd x64-steamclient genuine "Z:$d/genuine/steamclient64.dll" || return 1
  [ "$(dllmain_calls lsteamclient.dll)" -ge 1 ] || { echo "FAIL steamclient: no trace of lsteamclient.dll's DllMain"; return 1; }
  [ "$(dllmain_calls steamclient64.dll)" = 0 ] || { echo "FAIL steamclient: the genuine steamclient64.dll's DllMain ran"; return 1; }
  WINEDEBUG=trace+module exe_cmd x64-steamclient builtin "Z:$d/builtin/steamclient64.dll" || return 1
  [ "$(dllmain_calls steamclient64.dll)" -ge 1 ] || { echo "FAIL steamclient: the builtin copy's DllMain didn't run"; return 1; }
}

run_step() {
  case $1 in
    macos) step macos 10 macos_cmd ;;
    signature) step signature 60 signature_cmd ;;
    boot) step boot 180 boot_cmd ;;
    pages) step pages 120 pages_cmd ;;
    unentitled) step unentitled 30 unentitled_cmd ;;
    arm64) step arm64 60 exe_cmd arm64-hello ;;
    isec) step isec 60 exe_cmd arm64ec-isec ;;
    g3-cpu) step g3-cpu 60 g3_cpu_cmd; grep '^feature ' "$WORK/g3-cpu.log" ;;
    fex) step fex 60 fex_cmd ;;
    g1-hello) step g1-hello 60 g1_hello_cmd ;;
    g1-seh) step g1-seh 60 g1_seh_cmd ;;
    g1-threads) step g1-threads 60 exe_cmd x64-threads ;;
    g1-kuser) step g1-kuser 60 exe_cmd x64-kuser ;;
    g1-smc) step g1-smc 60 exe_cmd x64-smc ;;
    g1-tsc) step g1-tsc 60 exe_cmd x64-tsc; grep '^info ' "$WORK/g1-tsc.log" ;;
    g2-litmus) step g2-litmus 1800 g2_litmus_cmd; grep '^info ' "$WORK/g2-litmus.log" ;;
    viewec) step viewec 60 exe_cmd arm64ec-viewec ;;
    wxflip) step wxflip 60 wxflip_cmd; grep '^info ' "$WORK/wxflip.log" ;;
    wxflip-x64) step wxflip-x64 60 wxflip_x64_cmd; grep '^info ' "$WORK/wxflip-x64.log" ;;
    msync) step msync 300 msync_cmd; grep '^info ' "$WORK/msync.log" ;;
    x18) step x18 180 x18_cmd; grep '^info ' "$WORK/x18.log" ;;
    g1-unaligned) step g1-unaligned 60 exe_cmd x64-unaligned ;;
    g5-jit) step g5-jit 600 g5_jit_cmd; grep '^info ' "$WORK/g5-jit.log" ;;
    fonts-tls) step fonts-tls 60 fonts_tls_cmd ;;
    steam-bridge) step steam-bridge 600 steam_bridge_cmd; grep '^info ' "$WORK/steam-bridge.log" ;;
    dxmt) step dxmt 120 dxmt_cmd ;;
    dxmt-present) step dxmt-present 600 dxmt_present_cmd; grep '^info ' "$WORK/dxmt-present.log" ;;
    dxmt-arm64ec) step dxmt-arm64ec 3600 dxmt_lane_cmd arm64ec ARM64EC "$B/dxmt-tests-arm64ec" \
      "$B/dxmt-tests-arm64ec/present_loop.exe"; grep '^info ' "$WORK/dxmt-arm64ec.log" ;;
    dxmt-x64) step dxmt-x64 3600 dxmt_lane_cmd x64 AMD64 "$B/dxmt-tests" "$B/presenter/present_loop.exe" \
      'ok   the FSR 3 swapchain proxy presents on our DXMT'; grep '^info ' "$WORK/dxmt-x64.log" ;;
    g4-bench) step g4-bench 3600 g4_bench_cmd; grep '^info ' "$WORK/g4-bench.log"; cat "$WORK/bench/report.txt" ;;
    media-mf) step media-mf 120 media_mf_cmd ;;
    lanes) step lanes 900 lanes_cmd; grep '^info ' "$WORK/lanes.log" ;;
    fex-vmd) step fex-vmd 900 fex_vmd_cmd; grep '^info ' "$WORK/fex-vmd.log" ;;
    winetests) step winetests 1800 winetests_cmd; grep '^info ' "$WORK/winetests.log" ;;
    ec-hook) step ec-hook 60 ec_hook_cmd ;;
    crt) step crt 300 crt_cmd ;;
    steamclient) step steamclient 120 steamclient_cmd; LC_ALL=C /usr/bin/grep -E '^(skip|info) ' "$WORK/steamclient.log" ;;
    *) die "no runner for $1" ;;
  esac
}

want="${*:-$STEPS}"
for s in $want; do
  case " $STEPS $MEDIA $LANES " in *" $s "*) ;; *) die "no step named $s (steps: $STEPS $MEDIA $LANES)" ;; esac
done
for s in $want; do
  case " $NEEDS_FEX " in *" $s "*) want="fex $want" ;; esac
done
# g5-jit's and wxflip-x64's zero flips mean something only once wxflip has shown the trace counts flips: their
# positive control.
case " $want " in *" g5-jit "* | *" wxflip-x64 "*) want="wxflip $want" ;; esac
for s in $want; do
  case " $NEEDS_DXMT " in *" $s "*) want="dxmt $want" ;; esac
done
for s in $want; do
  case " $NEEDS_PREFIX " in *" $s "*) want="boot $want" ;; esac
done

cleanup
rm -rf "$WORK"
mkdir -p "$WORK/Application Support"
# No staged bundle: the signature step says so.
if [ -d "$STAGED" ]; then cp -cR "$STAGED" "$TOOL"; fi
for s in $STEPS $MEDIA $LANES; do
  case " $want " in *" $s "*) run_step "$s" ;; esac
done
