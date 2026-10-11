#!/bin/sh
# Our DXMT on the arm64 runtime, no Steam (DXMT fork spec §6, arm64 DXMT spec §7, arm64 release spec §8.2). Needs
# `make build wine-arm64 dxmt-tests presenter` (`dxmt-tests-arm64ec` too for the ARM64EC programs) and the frozen
# Rosetta reference (tools/freeze-rosetta-reference.sh; MACNEUTRON_REFERENCE names another). The checks run on two tool
# folders in the work folder (DXMT_CHECK_WORK overrides it): "ours", assembled by `macneutron install` from wine.app
# (MACNEUTRON_ARM64_APP, default build/wine-arm64/wine.app), runs our DXMT through the launcher just built; "ref", an
# APFS clone of the frozen tool, runs D3DMetal, the reference, under Rosetta through its own launcher. Neither the
# installed tool folders nor the reference is written. Our DXMT runs the programs in MACNEUTRON_ARM64_TESTS and
# MACNEUTRON_ARM64_LOOP (default: the x64 ones, which D3DMetal always runs), dxil-probe and dxil-translate from
# MACNEUTRON_ARM64_TOOLS (default build/wine-arm64).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TESTS="$ROOT/build/dxmt-tests"
S="$ROOT/dxmt/tests/shaders"
LOOP="$ROOT/build/presenter/present_loop.exe"
REF="${MACNEUTRON_REFERENCE:-$HOME/Library/Application Support/MacNeutron Reference/rosetta-tool}"
WINEAPP="${MACNEUTRON_ARM64_APP:-$ROOT/build/wine-arm64/wine.app}"
ATESTS="${MACNEUTRON_ARM64_TESTS:-$TESTS}" ALOOP="${MACNEUTRON_ARM64_LOOP:-$LOOP}"
TOOLS="${MACNEUTRON_ARM64_TOOLS:-$ROOT/build/wine-arm64}"
WORK="${DXMT_CHECK_WORK:-${TMPDIR:-/tmp}/macneutron dxmt}"
fail=0
# The D3D12 caps rows read DXMT's defaults: nothing from the caller's shell.
unset DXMT_D3D12_SM6 DXMT_DXIL_DUMP
expect() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got [$2], want [$3]"; fail=1; fi; }
die() { echo "dxmt-check: $*" >&2; exit 1; }
hazards() { grep '^hazard ' "$WORK/$1.txt" || echo "no hazard lines in $1"; }
# lone <run>: DXMT_STATS's count of timestamps given an encoder of their own, and why (Task F4); "no stats" when the run
# wrote none (killed before its exit), which no row wants.
lone() {
  [ -f "$WORK/$1/stats.txt" ] || { echo "no stats"; return; }
  grep -oE '(timestamps given their own encoder|lone timestamps \([a-z ]+\)) [0-9]+' "$WORK/$1/stats.txt" | tr '\n' ';'
}

echo "info arm64 mode: $WINEAPP"  # the line wine-arm64/check.sh's dxmt steps look for
[ -f "$REF/../FROZEN" ] || die "no frozen Rosetta reference at $REF (tools/freeze-rosetta-reference.sh makes it)"
for f in "$WINEAPP/Contents/MacOS/wine" "$TESTS/d3d12_clear.exe" "$LOOP" "$ATESTS/d3d12_clear.exe" "$ALOOP" \
  "$TOOLS/dxil-probe"; do
  [ -e "$f" ] || die "no $f"
done
# The launcher gets the programs' paths as given: absolute, whatever folder the check runs from.
ATESTS="$(cd "$ATESTS" && pwd)" ALOOP="$(cd "$(dirname "$ALOOP")" && pwd)/${ALOOP##*/}"

# "ours": wine.app and the launcher just built, assembled as the app installs them. "ref": the frozen tool with its own
# launcher (never this one). Each has its own compat folders: either launcher would rebuild the other's prefixes.
rm -rf "$WORK"; mkdir -p "$WORK/compat"
cp -cR "$(cd "$REF" && pwd -P)" "$WORK/ref"
"$ROOT/.build/release/macneutron" install --tool-dir "$WORK/ours" --wine-app "$WINEAPP" > /dev/null \
  || die "macneutron install could not assemble $WORK/ours"

# run <ours|ref> <name> <backend> <exe> [args...]  →  output in $WORK/<name>.txt. The backend picks the tool folder:
# d3dmetal the reference, with the x64 programs (Rosetta runs no ARM64EC code); dxmt ours, with the programs from
# MACNEUTRON_ARM64_TESTS and MACNEUTRON_ARM64_LOOP. DXMT_MAX_ANISOTROPY=0 (a launch option, so it wins): the samplers the
# programs ask for, not the launcher's 16x default; ANISO= leaves the default.
run() {
  name=$2 backend=$3; shift 3
  if [ "$backend" = d3dmetal ]; then
    tool=ref
  else
    tool=ours exe=$1; shift
    case $exe in "$LOOP") exe=$ALOOP ;; "$TESTS"/*) exe="$ATESTS/${exe#"$TESTS"/}" ;; esac
    set -- "$exe" "$@"
  fi
  # A fresh translation cache folder per run unless CACHE names a shared one: a dirty build shares its
  # `git describe`, so no run may read entries an earlier build left.
  aniso=${ANISO-0}
  env STEAM_COMPAT_DATA_PATH="$WORK/compat/$tool${LANE:+-$LANE}" SteamAppId=0 MACNEUTRON_GRAPHICS="$backend" \
      MACNEUTRON_NO_STEAM_BRIDGE=1 MACNEUTRON_NO_METALFX=1 DXMT_SHADER_CACHE_PATH="${CACHE:-$WORK/cache/$name}" \
      ${aniso:+DXMT_MAX_ANISOTROPY=$aniso} \
      "$WORK/$tool/bin/macneutron" launch waitforexitandrun "$@" > "$WORK/$name.out" 2>&1 &
  pid=$!
  # The watchdog's sleep outlives the kill below: kept off stdout, it can't hold a $(...) open for 120 s.
  ( sleep 120; kill "$pid" 2> /dev/null ) > /dev/null 2>&1 & dog=$!
  wait "$pid" || true
  kill "$dog" 2> /dev/null || true
  tr -d '\r' < "$WORK/$name.out" > "$WORK/$name.txt"
}
# At any exit, on TERM (wine-arm64/check.sh stops a step so) and on INT (Ctrl-C): the lanes and what they run (a
# snapshot, taken first: once a lane is gone its children belong to launchd), then each prefix's Wine, by its own
# tool's wineserver in the launchers' msync mode. The lanes are background subshells, which neither run these traps nor
# take SIGINT.
pA= pB= pC= pD= pE=
stop_lanes() {
  for p in $pA $pB $pC $pD $pE; do
    kids=$(pgrep -P "$p" 2> /dev/null || true)
    # shellcheck disable=SC2086  # kids is a list
    kill "$p" $kids 2> /dev/null || true
  done
  for p in "$WORK"/compat/ours*/pfx "$WORK"/compat/ref*/pfx; do
    case $p in "$WORK"/compat/ours*) ws="$WORK/ours/wine.app/Contents/Resources/bin/wineserver" ;;
      *) ws="$WORK/ref/Libraries/Wine/bin/wineserver" ;; esac
    [ -d "$p" ] && WINEPREFIX="$p" WINEMSYNC=1 "$ws" -k > /dev/null 2>&1 || true
  done
}
trap stop_lanes EXIT
trap 'exit 143' TERM
trap 'exit 130' INT
# The prefixes, created outside the 120 s watchdog, all at once (each its own): ours and ref for the checks run alone,
# and one of each per lane (LANE), as a launch waits for every Wine process in its prefix to exit.
pids=""
for p in ours ours-A ours-B ours-C ours-D ours-E ref ref-A ref-B ref-C ref-D ref-E; do
  env STEAM_COMPAT_DATA_PATH="$WORK/compat/$p" SteamAppId=0 \
      "$WORK/${p%%-*}/bin/macneutron" launch getcompatpath "$WORK" > /dev/null 2>&1 & pids="$pids $!"
done
for p in $pids; do wait "$p" || die "creating a prefix failed"; done
# Helpers the lanes share. invalid <run>: the Metal API validation errors (MTL_DEBUG_LAYER=1, logging instead of
# aborting) a run logged; each has "Validation" in its first line, as does the line saying it's on (once a process),
# which doesn't count. Frameworks' own debug-layer complaints (MetalFX: "outputTexture must have private storage
# mode") don't say "Validation": every NSLog line (date, time, wine[pid:tid]) counts too. "off" when no such line says
# so: a run validation never switched on for can't count as clean.
invalid() {
  grep -q 'Metal API Validation Enabled' "$WORK/$1.txt" || { echo off; return 0; }
  grep -E 'Validation|^[0-9-]+ [0-9:.]+ wine[^ ]*\[' "$WORK/$1.txt" | grep -vc 'Metal API Validation Enabled' || true
}
same_pixels() {  # same_pixels <ours> <ref> [prefix]: yes when both drew ("<prefix> ok") and 12 pixels are within 1/255
  python3 - "$1" "$2" "${3:-}" <<'PY'
import sys
def px(p, prefix):
    for l in open(p):
        s = l.split()
        if s[1:2] == ["ok"] and (not prefix or s[0] == prefix): return [int(x, 16) for x in s[3:15]]
a, b = px(sys.argv[1], sys.argv[3]), px(sys.argv[2], sys.argv[3])
print("yes" if a and b and len(a) == len(b) == 12 and all(abs(((x >> k) & 255) - ((y >> k) & 255)) <= 1 for x, y in zip(a, b) for k in (0, 8, 16, 24)) else f"no {a} {b}")
PY
}
cachetest() { run ours "$1" dxmt "$TESTS/d3d12_cache.exe" "Z:$S/cache.vs.dxil" "Z:$S/cache.ps.dxil" "Z:$S/cache.cs.dxil" "$2"; }
counters() { grep -o 'd3d12 shader cache: .*' "$WORK/$1.txt" | tail -1; }
drawn() { grep '^cache ' "$WORK/$1.txt" || echo "no cache line in $1"; }
RP="$WORK/ours/wine.app/Contents/Resources/DXMT/aarch64-windows/dxmt-replay.exe"
replay() { run ours "$1" dxmt "$RP" "Z:$2"; grep '^replay: ' "$WORK/$1.txt" | tail -1 | sed 's/, [0-9]* ms$//'; }

# Checks run alone first: the clear times against each other's, and the probe's 1 s answer, must not share the machine.
# 1. The D3D11 game runs our d3d11.dll, which the launcher put in the prefix (arm64 release spec §3.4). Its frame time
#    is recorded, not graded.
run ours d3d11 dxmt "$LOOP" 1280 720 0 0 600 0
echo "info D3D11 frame time: $(sed -n 's/.*avg frame \([0-9.]*\).*/\1/p' "$WORK/d3d11.txt") ms"
expect "the D3D11 game ran our d3d11.dll" \
  "$(cmp -s "$WORK/ours/wine.app/Contents/Resources/DXMT/aarch64-windows/d3d11.dll" \
    "$WORK/compat/ours/pfx/drive_c/windows/system32/d3d11.dll" && echo yes || echo no)" "yes"
# 9. GPU efficiency (spec 2026-10-02). E1: D3D12 textures that aren't UAVs get Apple lossless compression (which
#    PixelFormatView usage alone would turn off): a clear-only pass of a 3840x2160 RGBA16F target is at least 3x
#    cheaper than with DXMT_D3D12_COMPRESSION=0, and writes through other views of one layout, copies and placed
#    textures read back as on D3DMetal.
run ours compress dxmt "$TESTS/d3d12_compress.exe"
export DXMT_D3D12_COMPRESSION=0
run ours compress-off dxmt "$TESTS/d3d12_compress.exe"
unset DXMT_D3D12_COMPRESSION
run ref compress-ref d3dmetal "$TESTS/d3d12_compress.exe"
clear_on=$(awk '/^compress clear /{print $3}' "$WORK/compress.txt"); clear_off=$(awk '/^compress clear /{print $3}' "$WORK/compress-off.txt")
expect "compressed targets clear at least 3x cheaper ($clear_on against $clear_off us)" \
  "$(awk -v on="$clear_on" -v off="$clear_off" 'BEGIN { print (on > 0 && off >= 3 * on) ? "yes" : "no" }')" yes
expect "and read back as on D3DMetal through other views, copies and heap placement" \
  "$(grep -E '^compress (views|placed) ' "$WORK/compress.txt")" "$(grep -E '^compress (views|placed) ' "$WORK/compress-ref.txt")"

# 5. The DXIL probe: results recorded, not graded; one line per shader, and a non-container is refused.
"$TOOLS/dxil-probe" "$S"/*.dxil > "$WORK/probe.txt" || true
cat "$WORK/probe.txt"
expect "the probe reports every shader" "$(grep -cE '^(ok|fail) ' "$WORK/probe.txt")" "$(ls "$S"/*.dxil | wc -l | tr -d ' ')"
expect "the probe refuses a non-container" "$("$TOOLS/dxil-probe" "$ROOT/dxmt/pins" | cut -d ' ' -f 1)" fail
# Malformed containers: a part count of 2^32-1, and bitcode that lies past the end of its DXIL part.
python3 - "$WORK" <<'PY'
import struct, sys
w = sys.argv[1]
open(w + "/many-parts.dxil", "wb").write(b"DXBC" + bytes(16) + struct.pack("<III", 1, 32, 0xFFFFFFFF))
part = b"DXIL" + struct.pack("<I", 24) + struct.pack("<II", 0x60060, 6) + b"DXIL" + struct.pack("<III", 0x106, 16, 64)
open(w + "/past-part.dxil", "wb").write(b"DXBC" + bytes(16) + struct.pack("<IIII", 1, 36 + len(part) + 64, 1, 36) + part + bytes(64))
PY
"$TOOLS/dxil-probe" "$WORK/many-parts.dxil" > "$WORK/many-parts.txt" & probe=$!
sleep 1
if kill -0 "$probe" 2> /dev/null; then kill "$probe"; quick=no; else quick=yes; fi
reason() { sed 's/^[a-z]* .*\.dxil //'; }  # the probe's message after "<ok|fail> <file>"; $WORK has a space
expect "the probe answers a huge part count at once" "$quick:$(reason < "$WORK/many-parts.txt")" "yes:no DXIL part (a DXBC shader)"
expect "the probe keeps bitcode inside its part" "$("$TOOLS/dxil-probe" "$WORK/past-part.dxil" | reason)" \
  "bitcode lies outside the DXIL part"

# The rest in five lanes at once, each a subshell with its own prefix (its exports stay its own) and its own log,
# printed in order once all are done. Each test program runs in one lane only: no two lanes share its Metal or
# D3DMetal caches.
# Lane A: the D3D12 translation cache, recording and replay (sections 7-9), one chain of files.
(
LANE=A
# 7. The D3D12 translation cache (shader pre-caching spec §5.1). Runs 1-5 share one folder; the counter line is ours
#    only. Each mode changes one thing a reused translated function would get wrong, so that function must miss.
for m in a rt layout root; do
  run ref "cache-ref-$m" d3dmetal "$TESTS/d3d12_cache.exe" "Z:$S/cache.vs.dxil" "Z:$S/cache.ps.dxil" "Z:$S/cache.cs.dxil" $m
done
CACHE="$WORK/cache/shared"
cachetest cache1 a
expect "cache run 1 (cold) draws as D3DMetal" "$(drawn cache1)" "$(drawn cache-ref-a)"
expect "cache run 1 misses every lookup" "$(counters cache1)" \
  "d3d12 shader cache: functions 0 hit 3 missed, reflections 0 hit 3 missed"
cachetest cache2 a
expect "cache run 2 (warm) draws as D3DMetal" "$(drawn cache2)" "$(drawn cache-ref-a)"
expect "cache run 2 only hits" "$(counters cache2)" "d3d12 shader cache: functions 3 hit 0 missed, reflections 3 hit 0 missed"
echo "info pipeline creation: $(grep '^timing' "$WORK/cache1.txt") ms cold, $(grep '^timing' "$WORK/cache2.txt") ms warm"
cachetest cache3rt rt
expect "another render target format draws as D3DMetal" "$(drawn cache3rt)" "$(drawn cache-ref-rt)"
expect "and misses the pixel shader only" "$(counters cache3rt)" \
  "d3d12 shader cache: functions 2 hit 1 missed, reflections 3 hit 0 missed"
cachetest cache3layout layout
expect "another input layout draws as D3DMetal" "$(drawn cache3layout)" "$(drawn cache-ref-layout)"
expect "and misses the vertex shader only" "$(counters cache3layout)" \
  "d3d12 shader cache: functions 2 hit 1 missed, reflections 3 hit 0 missed"
cachetest cache3root root
expect "another root signature draws as D3DMetal" "$(drawn cache3root)" "$(drawn cache-ref-root)"
expect "and misses both graphics shaders" "$(counters cache3root)" \
  "d3d12 shader cache: functions 1 hit 2 missed, reflections 3 hit 0 missed"
db="$CACHE/shaders_310.db"
sqlite3 "$db" "UPDATE \"$(sqlite3 "$db" "SELECT name FROM sqlite_master WHERE name GLOB 'cache_*'")\" SET value = x'00';"
cachetest cache4 a
expect "corrupt entries: still draws as D3DMetal" "$(drawn cache4)" "$(drawn cache-ref-a)"
expect "corrupt entries are misses" "$(counters cache4)" \
  "d3d12 shader cache: functions 0 hit 3 missed, reflections 0 hit 3 missed"
expect "a rejected cached function is logged once" \
  "$(grep -c 'd3d12 shader cache: rejected a cached function, recompiling' "$WORK/cache4.txt" || true)" 1
sqlite3 "$db" "CREATE TABLE cache_1 (key BLOB PRIMARY KEY, value BLOB NOT NULL);"
cachetest cache5 a
expect "D3D12 drops another build's table too" "$(sqlite3 "$db" "SELECT count(*) FROM sqlite_master WHERE name = 'cache_1'")" 0
expect "and still draws as D3DMetal" "$(drawn cache5)" "$(drawn cache-ref-a)"
# A probed run (DXMT_PROBE rewrites a pixel shader's output for frame debugging) neither reads nor stores translated
# functions: stored, they would draw a later normal run in the probe's colours.
CACHE="$WORK/cache/probe"
export DXMT_PROBE=0000000000000000:-,-,-
cachetest probe1 a
unset DXMT_PROBE
cachetest probe2 a
expect "a probed run stores no translated function" "$(counters probe2)" \
  "d3d12 shader cache: functions 0 hit 3 missed, reflections 3 hit 0 missed"
# 8. Recording (spec §3.5, §5.2 runs 6-7): each pipeline once, a torn tail cut and re-recorded, a foreign file
#    started over, and an unwritable folder that changes nothing drawn.
REC="$WORK/rec"; f="$REC/d3d12_cache.exe.pipelines"
export DXMT_PIPELINE_RECORD="$REC"
CACHE="$WORK/cache/rec"
cachetest rec6 a
expect "run 6: the pipelines are recorded" "$(head -c 8 "$f" 2> /dev/null)" DXMTPRC1
full=$(wc -c < "$f" | tr -d ' ')
cachetest rec7 a
expect "run 7: a second run records nothing new" "$(wc -c < "$f" | tr -d ' ')" "$full"
python3 -c "import os, sys; os.truncate(sys.argv[1], os.path.getsize(sys.argv[1]) - 10)" "$f"
cachetest rec-torn a
expect "a torn tail is cut and its pipeline recorded again" "$(wc -c < "$f" | tr -d ' ')" "$full"
expect "recording changes nothing drawn" "$(drawn rec-torn)" "$(drawn cache-ref-a)"
printf 'not a recording' > "$f"
cachetest rec-foreign a
expect "a foreign file is started over" "$(head -c 8 "$f"):$(wc -c < "$f" | tr -d ' ')" "DXMTPRC1:$full"
export DXMT_PIPELINE_RECORD="/nonexistent/macneutron rec"
cachetest rec-unwritable a
expect "an unwritable recording folder changes nothing drawn" "$(drawn rec-unwritable)" "$(drawn cache-ref-a)"
expect "and says once that recording is off" "$(grep -c 'd3d12 pipeline recording off' "$WORK/rec-unwritable.txt" || true)" 1
unset DXMT_PIPELINE_RECORD CACHE
# 9. Replay (spec §3.6, §5.2 runs 8-12): dxmt-replay.exe rebuilds a recording into that game's caches.
UC="$(getconf DARWIN_USER_CACHE_DIR)dxmt"; rm -rf "$UC/d3d12_cache.exe" "$UC/dxmt-replay.exe"
CACHE="$WORK/cache/replay"
expect "run 8: the replay rebuilds every recorded pipeline" "$(replay rep8 "$f")" \
  "replay: 2 pipelines (1 graphics, 1 compute), 2 created, 0 failed, 0 bad records"
expect "into the game's Metal cache, not the replayer's" \
  "$([ -d "$UC/d3d12_cache.exe/com.apple.metal" ] && echo game):$([ -d "$UC/dxmt-replay.exe" ] && echo replayer)" "game:"
cachetest rep9 a
expect "run 9: after a replay the game only hits" "$(counters rep9)" \
  "d3d12 shader cache: functions 3 hit 0 missed, reflections 3 hit 0 missed"
expect "and draws as D3DMetal" "$(drawn rep9)" "$(drawn cache-ref-a)"
cp "$f" "$WORK/torn.pipelines"
python3 -c "import os, sys; os.truncate(sys.argv[1], os.path.getsize(sys.argv[1]) - 10)" "$WORK/torn.pipelines"
CACHE="$WORK/cache/replay-torn"
expect "run 10: a torn record is skipped and counted" "$(replay rep10 "$WORK/torn.pipelines")" \
  "replay: 1 pipelines (1 graphics, 0 compute), 1 created, 0 failed, 1 bad records"
exit $fail
) > "$WORK/lane-A.log" 2>&1 & pA=$!

# Lane B: encoder ordering and its stats (section 8, GPU overlap M2-M4, GPU efficiency E4, E7, E10).
(
LANE=B
# 8. Encoder ordering (GPU overlap spec §5): each mode's first pass is heavy, so work after it that doesn't wait for it
#    reads or overwrites its results early. Our DXMT in strict order (the default), with overlap (DXMT_D3D12_OVERLAP=1),
#    and in strict order while dumping passes and pixel history (the queue's own encoders), and D3DMetal print the
#    same lines. (ts-queues: timestamps sampled on one queue and resolved on another, which has no counter ring.)
want=$(printf 'hazard %s\n' "rt-read 257" "same-target 7 5" "uav 1048576" "copy-read 6" "indirect 9" "aliasing 2" \
  "occlusion 268435456" "independent 256 256" "precise 257" "mid-pass 77" "twice 514" "many 1200 1200" "clear-rects 3 256" \
  "signal 0" "wrap 4194304" "onewait 256 5 6" "newest 3 2" "nodraw 257" "queues 257 257" "unsplit 257" "unsplit-barrier 257" \
  "unsplit-samebuffer 257" "unsplit-midbarrier 258" "unsplit-query 258 268435456 1048576" "unsplit-twice 2" "deferred 1" "zeroed 0 0" "fold 6 2 9 5" "fold-order 6 7" \
  "fence-reset 1" "fence-cpu-late 1" "fence-transitive 0 0" "fence-custom 1" "fence-lower 0 1" "fence-wait-first 257" "fence-order 1 1" "two-heaps 1 1" "ts-start 1" "after-own-blit 7" \
  "fold-lists 6 2" "fold-lists-barrier 6 2" "fold-m4 11 7" "fold-twice 10 14" "fold-copy 2 6 2" "indirect-war 265" \
  "merge-indirect 10" "placed-uav 1048576" "heap-released 257" "ts-many 1" "sampled 1" "ts-queues 1")
run ours hazards dxmt "$TESTS/d3d12_hazards.exe" "Z:$S"
export DXMT_D3D12_OVERLAP=1
run ours hazards-overlap dxmt "$TESTS/d3d12_hazards.exe" "Z:$S"
unset DXMT_D3D12_OVERLAP
run ref hazards-ref d3dmetal "$TESTS/d3d12_hazards.exe" "Z:$S"
expect "work after a heavy pass waits for it (strict order, the default)" "$(hazards hazards)" "$want"
expect "and with overlap (DXMT_D3D12_OVERLAP=1)" "$(hazards hazards-overlap)" "$want"
# D3DMetal counts the second list's draw before its own query into the first list's ended query 0 (269484032);
# D3D12 ends query 0 at its EndQuery, as our DXMT does (268435456).
# D3DMetal resolves timestamps as zero, so two-heaps reads 0 0 there, and ts-start, ts-many and ts-queues 0.
expect "and on D3DMetal (but for its occlusion count after a merged pass, and its zero timestamps)" "$(hazards hazards-ref)" \
  "$(echo "$want" | sed -e 's/^hazard unsplit-query 258 268435456 1048576$/hazard unsplit-query 258 269484032 1048576/' \
    -e 's/^hazard two-heaps 1 1$/hazard two-heaps 0 0/' -e 's/^hazard ts-start 1$/hazard ts-start 0/' \
    -e 's/^hazard ts-many 1$/hazard ts-many 0/' -e 's/^hazard ts-queues 1$/hazard ts-queues 0/')"
rm -rf "$WORK/hz-dump"; export DXMT_DXIL_DUMP="$WORK/hz-dump" DXMT_DUMP_FRAME=0 DXMT_DUMP_PIXEL=512,512,0,40
run ours hazards-dump dxmt "$TESTS/d3d12_hazards.exe" "Z:$S" rt-read indirect precise
unset DXMT_DXIL_DUMP DXMT_DUMP_FRAME DXMT_DUMP_PIXEL
expect "and while dumping passes and pixel history" "$(hazards hazards-dump)" \
  "$(printf 'hazard %s\n' "rt-read 257" "indirect 9" "precise 257")"
export MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=nslog
run ours hazards-validation dxmt "$TESTS/d3d12_hazards.exe" "Z:$S"
unset MTL_DEBUG_LAYER MTL_DEBUG_LAYER_ERROR_MODE
expect "and under Metal validation" "$(hazards hazards-validation)" "$want"
expect "which rejects nothing (nodraw's only draw has no instances)" "$(invalid hazards-validation)" 0
# A texture placed in a heap holds it (DXMT 0011): the app's release of the heap (Release still answers 0, as on
# D3DMetal) destroys nothing while the texture lives, so the heap stays resident; the texture's release, after the
# readback, destroys it (no leak). Two heaps, each with a texture, both released; one texture released: 1 heap
# destroyed (2 if textures didn't hold their heaps, 0 if the last release leaked it).
rm -rf "$WORK/heap-stats"; export DXMT_DXIL_DUMP="$WORK/heap-stats" DXMT_STATS=1
run ours heap-stats dxmt "$TESTS/d3d12_hazards.exe" "Z:$S" heap-released
unset DXMT_DXIL_DUMP DXMT_STATS
expect "a heap released while a texture placed in it lives stays, and goes with the texture" \
  "$(grep -oE 'heaps destroyed [0-9]+' "$WORK/heap-stats/stats.txt" 2> /dev/null):$(hazards heap-stats)" \
  "heaps destroyed 1:hazard heap-released 257"
# Overlap happens when asked for (GPU overlap spec §3.8): with DXMT_D3D12_OVERLAP=1, passes into different targets
# with no barrier between them leave their boundaries free to overlap; by default (strict order) none is, and every
# encoder joins.
rm -rf "$WORK/ov-stats" "$WORK/ov-default"; export DXMT_DXIL_DUMP="$WORK/ov-stats" DXMT_STATS=1 DXMT_D3D12_OVERLAP=1
run ours ov-stats dxmt "$TESTS/d3d12_hazards.exe" "Z:$S" independent
unset DXMT_D3D12_OVERLAP; export DXMT_DXIL_DUMP="$WORK/ov-default"
run ours ov-default dxmt "$TESTS/d3d12_hazards.exe" "Z:$S" independent
unset DXMT_DXIL_DUMP DXMT_STATS
expect "independent passes are free to overlap with DXMT_D3D12_OVERLAP=1" \
  "$(grep -cE '^  encoder boundaries free to overlap [1-9]' "$WORK/ov-stats/stats.txt" 2> /dev/null || true)" 1
expect "and never by default (strict order)" \
  "$(grep -c '^  encoder boundaries free to overlap' "$WORK/ov-default/stats.txt" 2> /dev/null || true):$(grep -c '^  encoder full joins' "$WORK/ov-default/stats.txt" 2> /dev/null || true)" "0:1"
# M2 (GPU overlap spec §3.1): a barrier ending one render target's writes makes later work wait on that target's
# writer alone. d3d12_hazards precise: the sampling pass waits on T0's pass (not T1's), the read on T0's and T2's.
rm -rf "$WORK/precise-stats"; export DXMT_DXIL_DUMP="$WORK/precise-stats" DXMT_STATS=1 DXMT_D3D12_OVERLAP=1
run ours precise-stats dxmt "$TESTS/d3d12_hazards.exe" "Z:$S" precise
unset DXMT_DXIL_DUMP DXMT_STATS DXMT_D3D12_OVERLAP
expect "a transition waits on the transitioned resource's writers alone" \
  "$(grep -oE '(encoders with a dependency list|encoder dependency waits) [0-9]+' "$WORK/precise-stats/stats.txt" 2> /dev/null | tr '\n' ';')" \
  "encoder dependency waits 3;encoders with a dependency list 2;"
# A (GPU overlap spec §3.9): encoders after a join wait on its early fence alone, and on the newest writer of what
# they write. d3d12_hazards onewait and newest, each alone.
for m in onewait newest; do
  rm -rf "$WORK/$m-stats"; export DXMT_DXIL_DUMP="$WORK/$m-stats" DXMT_STATS=1 DXMT_D3D12_OVERLAP=1
  run ours "$m-stats" dxmt "$TESTS/d3d12_hazards.exe" "Z:$S" $m
  unset DXMT_DXIL_DUMP DXMT_STATS DXMT_D3D12_OVERLAP
done
expect "encoders after a join wait on one fence" \
  "$(grep -oE 'encoder fence waits [0-9]+' "$WORK/onewait-stats/stats.txt" 2> /dev/null)" "encoder fence waits 10"
rm -rf "$WORK/onewait-default"; export DXMT_DXIL_DUMP="$WORK/onewait-default" DXMT_STATS=1
run ours onewait-default dxmt "$TESTS/d3d12_hazards.exe" "Z:$S" onewait
unset DXMT_DXIL_DUMP DXMT_STATS
expect "early fences only with overlap" \
  "$(grep -c 'encoder early fences' "$WORK/onewait-default/stats.txt" 2> /dev/null || true):$(grep -c 'encoder early fences' "$WORK/onewait-stats/stats.txt" 2> /dev/null || true)" "0:1"
expect "and on the newest writer alone" \
  "$(grep -oE 'encoder dependency waits [0-9]+' "$WORK/newest-stats/stats.txt" 2> /dev/null)" "encoder dependency waits 3"
# M3 (GPU overlap spec §3.5): two lists' passes into one target with only a timestamp between them are one Metal render
# pass; not across a barrier (between or inside the passes), nor when the timestamp's counter buffer is already
# sampled at that pass's end, nor out of a pass counting into an occlusion query, nor a list into itself.
rm -rf "$WORK/m3-stats"; export DXMT_DXIL_DUMP="$WORK/m3-stats" DXMT_STATS=1
run ours m3-stats dxmt "$TESTS/d3d12_hazards.exe" "Z:$S" unsplit unsplit-barrier unsplit-samebuffer unsplit-midbarrier \
  unsplit-query unsplit-twice
unset DXMT_DXIL_DUMP DXMT_STATS
expect "render passes into one target across lists are one Metal render pass" \
  "$(grep -oE '(render passes merged|timestamp blits folded) [0-9]+' "$WORK/m3-stats/stats.txt" 2> /dev/null | tr '\n' ';')" \
  "render passes merged 1;timestamp blits folded 1;"
# For the base-pass merge (B6, Task F4), DXMT_STATS says why the others didn't join: unsplit-barrier's and
# unsplit-midbarrier's barriers, unsplit-samebuffer's counter buffer sampled twice, unsplit-twice's list run twice.
expect "and why the others didn't" \
  "$(grep -oE 'render pass merges refused \([a-z]+\) [0-9]+' "$WORK/m3-stats/stats.txt" 2> /dev/null | tr '\n' ';')" \
  "render pass merges refused (barrier) 2;render pass merges refused (other) 1;render pass merges refused (timestamps) 1;"
rm -rf "$WORK/m3-off-stats"; export DXMT_DXIL_DUMP="$WORK/m3-off-stats" DXMT_STATS=1 DXMT_D3D12_MERGE=0
run ours m3-off-stats dxmt "$TESTS/d3d12_hazards.exe" "Z:$S" unsplit
unset DXMT_DXIL_DUMP DXMT_STATS DXMT_D3D12_MERGE
expect "and none with DXMT_D3D12_MERGE=0" \
  "$(grep -c 'render passes merged' "$WORK/m3-off-stats/stats.txt" 2> /dev/null || true):$(grep '^hazard ' "$WORK/m3-off-stats.txt")" "0:hazard unsplit 257"
# M4 (GPU overlap spec §3.6): a clear then a pass into the cleared target, no barrier between: one Metal render pass
# with the clear as its load action. Not across a barrier, nor with DXMT_D3D12_MERGE=0.
rm -rf "$WORK/m4-stats"; export DXMT_DXIL_DUMP="$WORK/m4-stats" DXMT_STATS=1
run ours m4-stats dxmt "$TESTS/d3d12_hazards.exe" "Z:$S" fold
unset DXMT_DXIL_DUMP DXMT_STATS
# T1's clear, behind a barrier on T2, folds at execution since GPU efficiency E7 (counted apart): no clear pass is left.
expect "a clear before a pass into its target is the pass's load action" \
  "$(grep -oE '(clears folded|clear passes) [0-9]+' "$WORK/m4-stats/stats.txt" 2> /dev/null | sort | tr '\n' ';')" \
  "clears folded 1;"
rm -rf "$WORK/m4-off-stats"; export DXMT_DXIL_DUMP="$WORK/m4-off-stats" DXMT_STATS=1 DXMT_D3D12_MERGE=0
run ours m4-off-stats dxmt "$TESTS/d3d12_hazards.exe" "Z:$S" fold
unset DXMT_DXIL_DUMP DXMT_STATS DXMT_D3D12_MERGE
expect "and none with DXMT_D3D12_MERGE=0" \
  "$(grep -oE '(clears folded|clear passes) [0-9]+' "$WORK/m4-off-stats/stats.txt" 2> /dev/null | sort | tr '\n' ';'):$(grep '^hazard ' "$WORK/m4-off-stats.txt")" \
  "clear passes 2;:hazard fold 6 2 9 5"
# E4: a lone timestamp (a list's first, or a list of timestamps alone) is taken at the start of the next encoder the
# queue encodes, not by a blit of its own. d3d12_hazards ts-start alone: t0 rides on the pass after it, and t2, t3
# and t4 together on the next one (Task F4: one sample for timestamps at one point, so none keeps a blit).
rm -rf "$WORK/e4-stats"; export DXMT_DXIL_DUMP="$WORK/e4-stats" DXMT_STATS=1
run ours e4-stats dxmt "$TESTS/d3d12_hazards.exe" "Z:$S" ts-start
unset DXMT_DXIL_DUMP DXMT_STATS
expect "a lone timestamp is taken at the next encoder's start" \
  "$(grep -oE "timestamps at the next encoder's start [0-9]+" "$WORK/e4-stats/stats.txt" 2> /dev/null):$(grep '^hazard ' "$WORK/e4-stats.txt")" \
  "timestamps at the next encoder's start 2:hazard ts-start 1"
# E7: a clear-only pass folds into the first pass binding its view later in the same call, across lists, timestamps
# and barriers on other textures (fold-lists); not across a barrier naming its texture (fold-lists-barrier) or a copy
# of it (fold-copy). d3d12_hazards, each mode alone.
# Task F4 (Ruling 23): every timestamp rides an encoder of its call, whatever its query heap. d3d12_hazards ts-many
# alone (203 timestamps from two heaps between render passes, dispatches and copies, in order and bracketing a heavy
# dispatch) takes no blit of its own; with DXMT_D3D12_TIMESTAMP_BLITS=1 (the heaps' own counter buffers, one an
# encoder) 53 do: 49 for a second counter buffer, 3 at the call's end, 1 of two waiting for one encoder's start.
for m in shared blits; do
  rm -rf "$WORK/f4-$m"; export DXMT_DXIL_DUMP="$WORK/f4-$m" DXMT_STATS=1
  [ $m = blits ] && export DXMT_D3D12_TIMESTAMP_BLITS=1
  run ours "f4-$m" dxmt "$TESTS/d3d12_hazards.exe" "Z:$S" ts-many
  unset DXMT_DXIL_DUMP DXMT_STATS DXMT_D3D12_TIMESTAMP_BLITS
done
expect "timestamps from any query heap ride the call's encoders" "$(lone f4-shared):$(hazards f4-shared)" ":hazard ts-many 1"
expect "and with DXMT_D3D12_TIMESTAMP_BLITS=1, 53 get blits of their own" "$(lone f4-blits):$(hazards f4-blits)" \
  "lone timestamps (another counter buffer) 49;lone timestamps (end of call) 3;lone timestamps (several waiting) 1;timestamps given their own encoder 53;:hazard ts-many 1"
for m in fold-lists fold-lists-barrier fold-copy; do
  rm -rf "$WORK/e7-$m"; export DXMT_DXIL_DUMP="$WORK/e7-$m" DXMT_STATS=1
  run ours "e7-$m" dxmt "$TESTS/d3d12_hazards.exe" "Z:$S" $m
  unset DXMT_DXIL_DUMP DXMT_STATS
done
e7() { grep -oE '(clears folded at execute|clear folds refused \([a-z ]+\)) [0-9]+' "$WORK/e7-$1/stats.txt" 2> /dev/null | tr '\n' ';'; }
expect "a clear folds into its pass across lists" "$(e7 fold-lists)" "clears folded at execute 1;"
expect "but not across a barrier naming its texture" "$(e7 fold-lists-barrier)" "clear folds refused (barrier) 1;"
expect "nor across a copy of it" "$(e7 fold-copy)" "clear folds refused (barrier) 1;"
# E10: two lists' passes into one target, each an indirect draw read from the argument buffer, are one Metal render
# pass (no resolver pass before the second); with DXMT_D3D12_INDIRECT=icb, two.
for m in native icb; do
  rm -rf "$WORK/e10-$m"; export DXMT_DXIL_DUMP="$WORK/e10-$m" DXMT_STATS=1 DXMT_D3D12_INDIRECT=$m
  run ours "e10-$m" dxmt "$TESTS/d3d12_hazards.exe" "Z:$S" merge-indirect
  unset DXMT_DXIL_DUMP DXMT_STATS DXMT_D3D12_INDIRECT
done
e10() { echo "$(grep -oE 'render passes merged [0-9]+' "$WORK/e10-$1/stats.txt" 2> /dev/null):$(grep '^hazard ' "$WORK/e10-$1.txt")"; }
expect "indirect draws from the argument buffer merge across lists" "$(e10 native)" "render passes merged 1:hazard merge-indirect 10"
expect "and with DXMT_D3D12_INDIRECT=icb, don't" "$(e10 icb)" ":hazard merge-indirect 10"
exit $fail
) > "$WORK/lane-B.log" 2>&1 & pB=$!

# Lane C: presenting, DXIL capture and behaviour, the triangles and the geometry shader's recording and replay.
(
LANE=C
# 2. A D3D12 program presents through our d3d12.dll (the capture dump below shows it is ours).
run ours clear dxmt "$TESTS/d3d12_clear.exe" 300
expect "d3d12_clear presents every frame" "$(grep -c 'presented 300/300 frames' "$WORK/clear.txt" || true)" 1
expect "ResizeBuffers keeps the swapchain flags it's given (0: those it had)" \
  "$(grep '^resize flags' "$WORK/clear.txt" || echo none)" "resize flags 0x2 0x2"
expect "and the waitable object it was created with" "$(grep '^resize waitable' "$WORK/clear.txt" || echo none)" \
  "resize waitable 0x42 1"
# DXMT_DUMP_FRAMES=<n>: a pass dump takes n consecutive frames (a one-frame glitch is hard to catch with one F9).
rm -rf "$WORK/clear-dump"; export DXMT_DXIL_DUMP="$WORK/clear-dump" DXMT_DUMP_FRAME=5 DXMT_DUMP_FRAMES=3
run ours clear-dump dxmt "$TESTS/d3d12_clear.exe" 20
unset DXMT_DXIL_DUMP DXMT_DUMP_FRAME DXMT_DUMP_FRAMES
expect "DXMT_DUMP_FRAMES=3 dumps three frames in a row" \
  "$(grep '^# frame ' "$WORK/clear-dump/passes.txt" 2> /dev/null | tr '\n' ';')" "# frame 0 presented;# frame 1 presented;# frame 2 presented;"
# By default DXMT reports what Unreal Engine's SM6 check needs (DXMT 0039), shader model 6.7 included.
expect "by default the D3D12 device reports shader model 6.7 and binding tier 3" \
  "$(grep -cE '^(shader model 0x67 \(hr 0x00000000\)$|resource binding tier 3$)' "$WORK/clear.txt" || true)" 2
expect "and feature level 12_1, wave ops and 64-bit atomics" \
  "$(grep -c '^feature level 0xc100, wave ops 1, atomic64 1$' "$WORK/clear.txt" || true)" 1
# 3. DXIL pipelines are created (an out-of-scope op fails only its own pipeline, named in the log), and DXMT_DXIL_DUMP
#    captures each shader once, byte for byte. $WORK has a space in it, like the Application Support paths users pass.
H="$ROOT/dxmt/tests/dxil/heap.dxil"
dxil() { run ours "$1" dxmt "$TESTS/d3d12_dxil.exe" "Z:$S/triangle.vs.dxil" "Z:$S/triangle.ps.dxil" "Z:$S/compute.cs.dxil" "Z:$H"; }
D="$WORK/dxil"
export DXMT_DXIL_DUMP="$D"
dxil dxil
expect "DXIL pipelines are created" "$(grep -cE '^(graphics|compute) hr=0x00000000$' "$WORK/dxil.txt" || true)" 2
expect "an out-of-scope DXIL op fails only its pipeline" "$(grep -c '^heap hr=0x80004001$' "$WORK/dxil.txt" || true)" 1
run ref dxil-ref d3dmetal "$TESTS/d3d12_dxil.exe" "Z:$S/triangle.vs.dxil" "Z:$S/triangle.ps.dxil" "Z:$S/compute.cs.dxil" "Z:$H"
expect "newer device interfaces (5-8) answer as on D3DMetal" "$(grep '^device' "$WORK/dxil.txt" | tr -d '\r' | tr '\n' ' ')" \
  "$(grep '^device' "$WORK/dxil-ref.txt" | tr -d '\r' | tr '\n' ' ')"
expect "a graphics pipeline with sample count 0 fares as on D3DMetal" "$(grep '^graphics-samples0' "$WORK/dxil.txt" | tr -d '\r')" \
  "$(grep '^graphics-samples0' "$WORK/dxil-ref.txt" | tr -d '\r')"
expect "the capture folder is created" "$([ -d "$D" ] && echo yes || echo no)" yes
expect "four shaders captured" "$(ls "$D" | grep -c '\.dxil$')" 4
# Capture mode also saves the root signature and a line per pipeline naming its shaders and root signature by hash.
rs=$(ls "$D" | sed -n 's/^rs-\([0-9a-f]\{16\}\)\.bin$/\1/p')
expect "the root signature is captured" "$(echo "$rs" | grep -c .)" 1
expect "pipelines.txt names each pipeline's shaders and root signature" \
  "$(grep -cE "^(gfx vs=[0-9a-f]{16} ps=[0-9a-f]{16}|cs cs=[0-9a-f]{16}) rs=$rs( |$)" "$D/pipelines.txt" 2> /dev/null || true)" 4
expect "each capture is its shader, byte for byte" "$(for src in "$S/triangle.vs.dxil" "$S/triangle.ps.dxil" "$S/compute.cs.dxil" "$H"; do
    found=n; for f in "$D"/*.dxil; do cmp -s "$src" "$f" && found=y; done; printf %s $found
  done)" yyyy
expect "capture names are <stage>-<16 hex>.dxil" "$(ls "$D" | grep -cE '^(vs|ps|cs)-[0-9a-f]{16}\.dxil$')" 4
vs=$(ls "$D"/vs-*.dxil 2> /dev/null | head -1)
[ -z "$vs" ] || echo keep > "$vs"
dxil dxil-again
expect "an existing capture is left alone" "$(cat "$vs" 2> /dev/null)" keep
# Capture mode reports what Unreal Engine's SM6 check needs, so SM6-only games get as far as creating pipelines,
# even with DXMT_D3D12_SM6=0.
export DXMT_D3D12_SM6=0
run ours clear-capture dxmt "$TESTS/d3d12_clear.exe" 10
unset DXMT_D3D12_SM6
expect "capture mode reports shader model 6.7 and binding tier 3" \
  "$(grep -cE '^(shader model 0x67 \(hr 0x00000000\)$|resource binding tier 3$)' "$WORK/clear-capture.txt" || true)" 2
expect "capture mode reports feature level 12_1, wave ops and 64-bit atomics" \
  "$(grep -c '^feature level 0xc100, wave ops 1, atomic64 1$' "$WORK/clear-capture.txt" || true)" 1
# Outside capture mode, DXMT_D3D12_SM6=0 brings back shader model 5.1 and DXMT's real limits (for a game whose
# Shader Model 6 path fails).
unset DXMT_DXIL_DUMP
export DXMT_D3D12_SM6=0
run ours clear-sm5 dxmt "$TESTS/d3d12_clear.exe" 10
unset DXMT_D3D12_SM6
expect "DXMT_D3D12_SM6=0 reports shader model 5.1 and binding tier 2" \
  "$(grep -cE '^(shader model 0x51 \(hr 0x00000000\)$|resource binding tier 2$)' "$WORK/clear-sm5.txt" || true)" 2
expect "and feature level 11_1, no wave ops, no 64-bit atomics" \
  "$(grep -c '^feature level 0xb100, wave ops 0, atomic64 0$' "$WORK/clear-sm5.txt" || true)" 1
export DXMT_DXIL_DUMP="$WORK/dxil é"
dxil dxil-unicode
expect "a capture folder named outside ASCII works" "$(ls "$WORK/dxil é" 2> /dev/null | grep -c '\.dxil$')" 4
expect "no capture is left half-written" "$(ls "$D" "$WORK/dxil é" 2> /dev/null | grep -c '\.tmp$')" 0
export DXMT_DXIL_DUMP="/nonexistent/macneutron dxil"
dxil dxil-unwritable
expect "an unwritable capture folder changes nothing for the game" "$(grep -c '^compute hr=0x00000000$' "$WORK/dxil-unwritable.txt" || true)" 1
unset DXMT_DXIL_DUMP
# The unsupported op is named in the game log (MACNEUTRON_LOG=1 sends the output there; gate L2).
LOG="$HOME/Library/Logs/MacNeutron/steam-0.log"; before=$(cat "$LOG" 2> /dev/null | wc -l)
export MACNEUTRON_LOG=1; dxil dxil-logged; unset MACNEUTRON_LOG
expect "the unsupported op is named in the log" "$(tail -n +$((before + 1)) "$LOG" | grep -c 'Failed to compile cs shader: DXIL: dx.op.createHandleFromHeap')" 1
# 3b. DXIL behaviour groups: our DXMT against D3DMetal on the same GPU.
X="$ROOT/dxmt/tests/dxil"
run ours exec-ours dxmt "$TESTS/d3d12_dxil_exec.exe" "Z:$X"
run ref exec-ref d3dmetal "$TESTS/d3d12_dxil_exec.exe" "Z:$X"
for g in buffers math transcendental textures groupshared wave half packed atomics quad specials mad barriers; do
  expect "DXIL $g matches D3DMetal" "$(python3 "$ROOT/dxmt/tests/compare.py" "$WORK/exec-ours.txt" "$WORK/exec-ref.txt" $g)" match
done
# Task F2: mad() fuses (one rounding) unless precise, against a CPU reference (dxil/mad.hlsl: the 32 odd threads tell
# the two apart).
expect "DXIL mad fuses, and precise mad doesn't" "$(python3 - "$WORK/exec-ours.txt" <<'PY'
import struct, sys
f32 = lambda x: struct.unpack('<f', struct.pack('<f', x))[0]  # exact inputs: one rounding
hexf = lambda x: '%08x' % struct.unpack('<I', struct.pack('<f', x))[0]
w = next((l.split()[3:] for l in open(sys.argv[1]) if l.split()[:3] == ['group', 'mad', 'ok']), [])
fused = [hexf(-n * n * 2.0**-26) for n in range(1, 65)]
split = [hexf(f32(f32(1 - n * n * 2.0**-26) - 1)) for n in range(1, 65)]
print('mad fused %d precise %d of 64 (%d differ)' % (sum(w[16 * i:16 * i + 1] == [fused[i]] for i in range(64)),
      sum(w[16 * i + 1:16 * i + 2] == [split[i]] for i in range(64)), sum(f != s for f, s in zip(fused, split))))
PY
)" "mad fused 64 precise 64 of 64 (32 differ)"
# Shader Model 6.1-6.8 groups (DXIL SM 6.8 plan): our DXMT only, each against its CPU reference (dxmt/tests/expect.py).
SM68_GROUPS="helperquad msad wavematch wavesize32 wavesize32r wavesize"
run ours exec-sm68 dxmt "$TESTS/d3d12_dxil_exec.exe" "Z:$X" $SM68_GROUPS
for g in $SM68_GROUPS; do
  expect "DXIL $g matches its CPU reference" "$(python3 "$ROOT/dxmt/tests/expect.py" "$WORK/exec-sm68.txt" $g)" match
done
# A [WaveSize] range without 32 fails its D3D12 pipeline (DXIL SM 6.8 plan, Task 4).
run ours exec-reject dxmt "$TESTS/d3d12_dxil_exec.exe" "Z:$X/reject" wavesize64 wavesizerange
expect "D3D12 refuses a pipeline whose [WaveSize] excludes 32" \
  "$(/usr/bin/grep -cE '^group wavesize(64|range) fail 0x80004005$' "$WORK/exec-reject.txt" || true)" 2
run ours exec-threads dxmt "$TESTS/d3d12_dxil_exec.exe" "Z:$X" threads
expect "DXIL pipelines compile on 8 threads at once" "$(grep -o 'threads ok 8/8' "$WORK/exec-threads.txt" || true)" "threads ok 8/8"
run ours tri-ours dxmt "$TESTS/d3d12_triangle.exe" "Z:$S/triangle2.vs.dxil" "Z:$S/triangle2.ps.dxil"
run ref tri-ref d3dmetal "$TESTS/d3d12_triangle.exe" "Z:$S/triangle2.vs.dxil" "Z:$S/triangle2.ps.dxil"
expect "DXIL triangle matches D3DMetal (12 pixels within 1/255)" "$(same_pixels "$WORK/tri-ours.txt" "$WORK/tri-ref.txt")" yes
run ours trigs-ours dxmt "$TESTS/d3d12_triangle.exe" "Z:$S/triangle2.vs.dxil" "Z:$S/triangle2.ps.dxil" "Z:$S/triangle2.gs.dxil"
run ref trigs-ref d3dmetal "$TESTS/d3d12_triangle.exe" "Z:$S/triangle2.vs.dxil" "Z:$S/triangle2.ps.dxil" "Z:$S/triangle2.gs.dxil"
expect "DXIL geometry shader triangle matches D3DMetal (12 pixels within 1/255)" \
  "$(same_pixels "$WORK/trigs-ours.txt" "$WORK/trigs-ref.txt")" yes
run ours tri-sm6x dxmt "$TESTS/d3d12_triangle.exe" "Z:$S/triangle2.vs.dxil" "Z:$S/sm6x.ps.dxil"
expect "ViewID, the render target's sample count and position read 0, 1 and (0,0)" \
  "$(same_pixels "$WORK/tri-sm6x.txt" "$WORK/tri-ours.txt")" yes
# Helper lanes (DXIL SM 6.8 plan, Task 2): left out of pixel-shader wave ops by default, in with the attribute.
img() { awk '$1 == "triangle" && $2 == "ok" { print $3 }' "$WORK/$1.txt"; }
pixels() { awk '$1 == "triangle" && $2 == "ok" && NF == 15 { for (i = 4; i <= 15; i++) print $i }' "$WORK/$1.txt" | sort | uniq -c | tr -s ' '; }
for p in ps psinclude psgreen; do
  run ours "tri-helper-$p" dxmt "$TESTS/d3d12_triangle.exe" "Z:$S/triangle2.vs.dxil" "Z:$S/helper.$p.dxil"
done
expect "pixel-shader wave ops leave helper lanes out by default" "$(img tri-helper-ps)" "$(img tri-helper-psgreen)"
expect "and count them with [WaveOpsIncludeHelperLanes]" "$(img tri-helper-psinclude)" "$(img tri-helper-psgreen)"
# One "triangle ok" line with 12 sampled pixels, all green: the hash the rows above compare with is a green image.
expect "the always-green image drew, its 12 sampled pixels green" "$(pixels tri-helper-psgreen)" " 12 ff00ff00"
# 16-bit varyings (DXIL SM 6.8 plan, Task 5): min precision and native 16-bit draw the float form's pixels.
for v in 32 16 16n; do run ours "tri-v$v" dxmt "$TESTS/d3d12_triangle.exe" "Z:$S/varying16.vs$v.dxil" "Z:$S/varying16.ps$v.dxil"; done
# The float form, which the rows below compare with, draws as D3DMetal: a common-mode break can't pass them all.
run ref tri-v32-ref d3dmetal "$TESTS/d3d12_triangle.exe" "Z:$S/varying16.vs32.dxil" "Z:$S/varying16.ps32.dxil"
expect "the float form of the 16-bit varyings draws as D3DMetal" "$(same_pixels "$WORK/tri-v32.txt" "$WORK/tri-v32-ref.txt")" yes
expect "min16float and min16uint varyings draw as float ones" "$(same_pixels "$WORK/tri-v16.txt" "$WORK/tri-v32.txt")" yes
expect "and float16_t and uint16_t ones" "$(same_pixels "$WORK/tri-v16n.txt" "$WORK/tri-v32.txt")" yes
# The float pixel shader reads the min16 vertex shader's 32-bit registers: its min16uint tag zero-extended, its min16int
# bias sign-extended (either the other way saturates alpha or blue). A register check only: D3DMetal creates the
# pipeline and draws only the clear colour for this pair, so the row has no D3DMetal reference.
run ours tri-v16x dxmt "$TESTS/d3d12_triangle.exe" "Z:$S/varying16.vs16.dxil" "Z:$S/varying16.ps32.dxil"
expect "and a float pixel shader reads them as float, uint and int" "$(same_pixels "$WORK/tri-v16x.txt" "$WORK/tri-v32.txt")" yes
CACHE="$WORK/cache/gs"
run ours trigs-cold dxmt "$TESTS/d3d12_triangle.exe" "Z:$S/triangle2.vs.dxil" "Z:$S/triangle2.ps.dxil" "Z:$S/triangle2.gs.dxil"
run ours trigs-warm dxmt "$TESTS/d3d12_triangle.exe" "Z:$S/triangle2.vs.dxil" "Z:$S/triangle2.ps.dxil" "Z:$S/triangle2.gs.dxil"
expect "a warm geometry-shader pipeline only hits" \
  "$(counters trigs-warm | grep -cE 'functions [1-9][0-9]* hit 0 missed, reflections [1-9][0-9]* hit 0 missed$' || true)" 1
expect "and draws as D3DMetal" "$(same_pixels "$WORK/trigs-warm.txt" "$WORK/trigs-ref.txt")" yes
unset CACHE
export DXMT_PIPELINE_RECORD="$WORK/rec-gs"
CACHE="$WORK/cache/gs-rec"
run ours gs-rec dxmt "$TESTS/d3d12_triangle.exe" "Z:$S/triangle2.vs.dxil" "Z:$S/triangle2.ps.dxil" "Z:$S/triangle2.gs.dxil"
export DXMT_PIPELINE_RECORD="$WORK/rec-threads"
run ours threads-rec dxmt "$TESTS/d3d12_dxil_exec.exe" "Z:$X" threads
unset DXMT_PIPELINE_RECORD
CACHE="$WORK/cache/gs-replay"
expect "run 12: a geometry-shader pipeline replays" \
  "$(replay gs-replay "$WORK/rec-gs/d3d12_triangle.exe.pipelines" | grep -cE '^replay: [1-9][0-9]* pipelines .*, 0 failed, 0 bad records$' || true)" 1
run ours gs-after dxmt "$TESTS/d3d12_triangle.exe" "Z:$S/triangle2.vs.dxil" "Z:$S/triangle2.ps.dxil" "Z:$S/triangle2.gs.dxil"
expect "and then only hits" \
  "$(counters gs-after | grep -cE 'functions [1-9][0-9]* hit 0 missed, reflections [1-9][0-9]* hit 0 missed$' || true)" 1
expect "and draws as D3DMetal" "$(same_pixels "$WORK/gs-after.txt" "$WORK/trigs-ref.txt")" yes
CACHE="$WORK/cache/threads"
expect "pipelines created on 8 threads at once replay whole" \
  "$(replay threads-replay "$WORK/rec-threads/d3d12_dxil_exec.exe.pipelines" | grep -cE '^replay: [1-9][0-9]* pipelines .*, 0 failed, 0 bad records$' || true)" 1
exit $fail
) > "$WORK/lane-C.log" 2>&1 & pC=$!

# Lane D: depth, pass dumps and pixel history, queries and the D3D12 stubs, against D3DMetal.
(
LANE=D
# Depth and stencil as Unreal uses them; occlusion queries, which Unreal culls meshes by (SMITE 2's lobby).
run ours depth-ours dxmt "$TESTS/d3d12_depth.exe" "Z:$S/depth.vs.dxil" "Z:$S/depth.ps.dxil" "Z:$S/depth.psdepth.dxil"
run ref depth-ref d3dmetal "$TESTS/d3d12_depth.exe" "Z:$S/depth.vs.dxil" "Z:$S/depth.ps.dxil" "Z:$S/depth.psdepth.dxil"
expect "depth and stencil match D3DMetal (12 pixels within 1/255)" "$(same_pixels "$WORK/depth-ours.txt" "$WORK/depth-ref.txt" depth)" yes
expect "read-only depth and stencil views match D3DMetal (12 pixels within 1/255)" \
  "$(same_pixels "$WORK/depth-ours.txt" "$WORK/depth-ref.txt" depth2)" yes
# The pass dump (capture mode, DXMT_DUMP_FRAME): frame 0 of a test that never presents, saved as its queue goes.
rm -rf "$WORK/passes"; export DXMT_DXIL_DUMP="$WORK/passes" DXMT_DUMP_FRAME=0
run ours depth-dump dxmt "$TESTS/d3d12_depth.exe" "Z:$S/depth.vs.dxil" "Z:$S/depth.ps.dxil" "Z:$S/depth.psdepth.dxil"
unset DXMT_DXIL_DUMP DXMT_DUMP_FRAME
expect "the pass dump saves the depth test's 6 render passes (11 attachments)" \
  "$(grep -c ' render ' "$WORK/passes/passes.txt" 2> /dev/null || true) $(ls "$WORK/passes" 2> /dev/null | grep -c '\.raw$')" "6 11"
# Pixel history (DXMT_DUMP_PIXEL=x,y): each draw of the dumped frame redrawn alone from its pass's starting state, and
# the draws that change the pixel listed with their pipeline in pixels.txt. At (32,32): pass-3 (the depth test's
# first, its target's and its depth's clears folded into it, M4) draws the near red quad and the far green quad, each
# alone passing its depth test; pass-4's yellow quad (depth EQUAL 0.25) is rejected by the 0.75 the near quad left,
# so pass-4 lists nothing.
rm -rf "$WORK/pixel"; export DXMT_DXIL_DUMP="$WORK/pixel" DXMT_DUMP_FRAME=0 DXMT_DUMP_PIXEL=32,32
run ours depth-pixel dxmt "$TESTS/d3d12_depth.exe" "Z:$S/depth.vs.dxil" "Z:$S/depth.ps.dxil" "Z:$S/depth.psdepth.dxil"
unset DXMT_DXIL_DUMP DXMT_DUMP_FRAME DXMT_DUMP_PIXEL
expect "pixel history: each quad's draw alone, named by its shaders and blending" \
  "$(grep -cE '^pass-3 draw-(0|1) gfx vs=[0-9a-f]{16} ps=[0-9a-f]{16} .* blend0=off mask0=15 c0 000000ff->(ff0000ff|00ff00ff) at 32,32$' "$WORK/pixel/pixels.txt" 2> /dev/null || true)" 2
expect "a draw the pass's starting depth rejects isn't listed" "$(grep -c '^pass-4 ' "$WORK/pixel/pixels.txt" 2> /dev/null || true)" 0
expect "pixel history leaves the frame's own passes as they were" \
  "$(cmp -s "$WORK/pixel/pass-3-c0-64x64-70.raw" "$WORK/passes/pass-3-c0-64x64-70.raw" && echo same || echo differ)" same
# In sequence (",seq"), each draw goes on top of the pass's earlier draws: the far quad, behind the near one, no longer
# changes (32,32), but it does change (48,8), which only it covers. Pixels join with '+'; passes 3-4 only are redrawn.
# draws.txt lists every draw redrawn; pixels.txt also says what each pass redrew.
rm -rf "$WORK/pixelseq"; export DXMT_DXIL_DUMP="$WORK/pixelseq" DXMT_DUMP_FRAME=0 DXMT_DUMP_PIXEL=32,32+48,8,3,4,seq
run ours depth-pixelseq dxmt "$TESTS/d3d12_depth.exe" "Z:$S/depth.vs.dxil" "Z:$S/depth.ps.dxil" "Z:$S/depth.psdepth.dxil"
unset DXMT_DXIL_DUMP DXMT_DUMP_FRAME DXMT_DUMP_PIXEL
expect "in sequence, only the near quad changes (32,32) in pass-3" \
  "$(grep -c '^pass-3 draw-.* at 32,32$' "$WORK/pixelseq/pixels.txt" 2> /dev/null || true):$(grep -c '^pass-3 draw-0 .* c0 000000ff->ff0000ff at 32,32$' "$WORK/pixelseq/pixels.txt" 2> /dev/null || true)" "1:1"
expect "and the far quad changes (48,8), a second watched pixel" \
  "$(grep -c '^pass-3 draw-1 .* c0 000000ff->00ff00ff at 48,8$' "$WORK/pixelseq/pixels.txt" 2> /dev/null || true)" 1
expect "only passes 3-4 are redrawn" "$(grep -c '^# pass-' "$WORK/pixelseq/pixels.txt" 2> /dev/null || true)" 2
# A pixel list longer than 260 characters (Windows' MAX_PATH) still arrives whole.
long="32,32"; for y in 900 1000 1100 1200 1300; do for x in 100 300 500 700 900 1100 1300 1500 1700 1900; do long="$long+$x,$y"; done; done
rm -rf "$WORK/pixellong"; export DXMT_DXIL_DUMP="$WORK/pixellong" DXMT_DUMP_FRAME=0 DXMT_DUMP_PIXEL="$long,3,3"
run ours depth-pixellong dxmt "$TESTS/d3d12_depth.exe" "Z:$S/depth.vs.dxil" "Z:$S/depth.ps.dxil" "Z:$S/depth.psdepth.dxil"
unset DXMT_DXIL_DUMP DXMT_DUMP_FRAME DXMT_DUMP_PIXEL
expect "a pixel list over 260 characters works" \
  "${#long}:$(grep -c '^pass-3 draw-0 .* at 32,32$' "$WORK/pixellong/pixels.txt" 2> /dev/null || true)" "${#long}:1"
expect "draws.txt lists both of pass-3's draws" "$(grep -c '^pass-3 draw-[01] gfx ' "$WORK/pixelseq/draws.txt" 2> /dev/null || true)" 2
expect "pixels.txt says what pass-3 redrew" "$(grep -c '^# pass-3: 2 draws redrawn in sequence$' "$WORK/pixelseq/pixels.txt" 2> /dev/null || true)" 1
run ours query-ours dxmt "$TESTS/d3d12_query.exe" "Z:$S/depth.vs.dxil" "Z:$S/depth.ps.dxil"
run ref query-ref d3dmetal "$TESTS/d3d12_query.exe" "Z:$S/depth.vs.dxil" "Z:$S/depth.ps.dxil"
expect "occlusion queries match D3DMetal" "$(grep '^query' "$WORK/query-ours.txt" || true)" "$(grep '^query' "$WORK/query-ref.txt" || echo 'D3DMetal ran no query')"
# Batch 1 of the D3D12 stubs spec: calls that aborted, hung or failed where D3DMetal succeeds (d3d12_api).
run ours api-ours dxmt "$TESTS/d3d12_api.exe" "Z:$S/depth.vs.dxil" "Z:$S/depth.ps.dxil"
run ref api-ref d3dmetal "$TESTS/d3d12_api.exe" "Z:$S/depth.vs.dxil" "Z:$S/depth.ps.dxil"
same_lines() {  # same_lines <prefix>: d3d12_api's lines starting with <prefix> are the same on both, and present
  a=$(grep "^$1 " "$WORK/api-ours.txt" || true); b=$(grep "^$1 " "$WORK/api-ref.txt" || true)
  [ -n "$a" ] && [ "$a" = "$b" ] && echo yes || echo "no: ours [$a] D3DMetal [$b]"
}
for s in markers cachedblob nulldsv list1 heap1 residency multifence feature; do expect "d3d12_api $s answers as D3DMetal" "$(same_lines $s)" yes; done
# Ruling 25: D3DMetal's Serialize races a concurrent Store (it writes past the size it was given, or not, by what ran
# earlier in the process), so its serialize-race line isn't compared; ours is pinned.
a=$(grep '^library ' "$WORK/api-ours.txt" | grep -v '^library serialize-race ' || true)
b=$(grep '^library ' "$WORK/api-ref.txt" | grep -v '^library serialize-race ' || true)
expect "d3d12_api library answers as D3DMetal (but for D3DMetal's serialize race)" \
  "$([ -n "$a" ] && [ "$a" = "$b" ] && echo yes || echo "no: ours [$a] D3DMetal [$b]")" yes
expect "and ours serializes beside a store without overflow" \
  "$(grep '^library serialize-race ' "$WORK/api-ours.txt" || echo none)" "library serialize-race overflow 0"
# D3D12 refuses Map where the CPU can't see the heap (D3DMetal maps a CUSTOM heap with no CPU pages anyway).
expect "Map refuses a buffer in a heap the CPU can't see" "$(grep '^map ' "$WORK/api-ours.txt" || echo none)" \
  "map custom-na 80070057 0 default 80070057 0 upload 00000000 1"
expect "our DXMT claims no raytracing, mesh shaders, VRS or sampler feedback" \
  "$(grep '^caps ' "$WORK/api-ours.txt" || true)" "caps rt=0 mesh=0 vrs=0 sfb=0"
# Batch 2: copies between formats D3D12 lets reinterpret.
run ours copy-ours dxmt "$TESTS/d3d12_copy.exe"
run ref copy-ref d3dmetal "$TESTS/d3d12_copy.exe"
expect "reinterpreting copies match D3DMetal byte for byte" \
  "$(grep '^copy ' "$WORK/copy-ours.txt" | tr '\n' ' ')" "$(grep '^copy ' "$WORK/copy-ref.txt" | tr '\n' ' ')"
expect "d3d12_copy ran its eleven cases" "$(grep -c '^copy .* ok ' "$WORK/copy-ours.txt" || true)" 11
export MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=nslog
run ours copy-validation dxmt "$TESTS/d3d12_copy.exe"
unset MTL_DEBUG_LAYER MTL_DEBUG_LAYER_ERROR_MODE
expect "d3d12_copy's cases, a BC footprint into a smaller mip among them, pass Metal validation" \
  "$(grep -c '^copy .* ok ' "$WORK/copy-validation.txt" || true) cases, $(invalid copy-validation) errors" "11 cases, 0 errors"
# Batch 2: null descriptors of every type.
run ours null-ours dxmt "$TESTS/d3d12_null.exe" "Z:$S/null.cs.dxil"
run ref null-ref d3dmetal "$TESTS/d3d12_null.exe" "Z:$S/null.cs.dxil"
expect "null descriptors read and report as on D3DMetal" \
  "$(grep '^null ' "$WORK/null-ours.txt" | tr '\n' ' ')" "$(grep '^null ' "$WORK/null-ref.txt" | tr '\n' ' ')"
expect "d3d12_null ran its 18 slots" "$(grep -c '^null ' "$WORK/null-ours.txt" || true)" 18
# Paths Unreal's particles and translucency lighting use, each against D3DMetal: layered rendering into a 3D texture
# from the vertex shader, a 3D texture written by compute then sampled and loaded, and resources a vertex shader reads.
run ours layered-ours dxmt "$TESTS/d3d12_layered.exe" "Z:$S/layered.vs.dxil" "Z:$S/layered.ps.dxil"
run ref layered-ref d3dmetal "$TESTS/d3d12_layered.exe" "Z:$S/layered.vs.dxil" "Z:$S/layered.ps.dxil"
expect "layered rendering into a 3D texture matches D3DMetal (within 1/255)" "$(python3 - "$WORK/layered-ours.txt" "$WORK/layered-ref.txt" <<'PY'
import sys
def texels(p):
    for l in open(p):
        s = l.split()
        if s[:2] == ["layered", "ok"]: return [int(x, 16) for x in s[2:]]
a, b = texels(sys.argv[1]), texels(sys.argv[2])
print("yes" if a and b and len(a) == len(b) == 4 and all(abs(((x >> k) & 255) - ((y >> k) & 255)) <= 1 for x, y in zip(a, b) for k in (0, 8, 16, 24)) else f"no {a} {b}")
PY
)" yes
run ours volume-ours dxmt "$TESTS/d3d12_volume.exe" "Z:$S/volume.fill.dxil" "Z:$S/volume.sample.dxil"
run ref volume-ref d3dmetal "$TESTS/d3d12_volume.exe" "Z:$S/volume.fill.dxil" "Z:$S/volume.sample.dxil"
expect "a 3D texture written by compute, then sampled and loaded, matches D3DMetal" \
  "$( (grep '^volume ' "$WORK/volume-ours.txt" || echo none) | tr '\n' ' ')" \
  "$( (grep '^volume ' "$WORK/volume-ref.txt" || echo 'D3DMetal printed nothing') | tr '\n' ' ')"
# Task FU: which mip a sample uses (the blur study's sweep, d3d12_lod.cpp). A gradient sample keeps the sampler's
# MipLODBias, as D3D says: s1, s2 (bias -1, +1) and s6 (aniso 4, bias -1) at 1:1 and 8:1, and s6 with its gradients
# halved at 16:1 (bias -2 in all, past Metal's anisotropy cap). D3DMetal adds the bias under that cap (8:1 s6: 2, not
# D3D's 1), so these aren't its values; every other case is.
run ours lod-ours dxmt "$TESTS/d3d12_lod.exe" "Z:$S/lod.vs.dxil" "Z:$S/lod.ps.dxil"
run ref lod-ref d3dmetal "$TESTS/d3d12_lod.exe" "Z:$S/lod.vs.dxil" "Z:$S/lod.ps.dxil"
expect "SampleGrad keeps the sampler's LOD bias, as D3D does (not D3DMetal)" \
  "$(grep -E '^lod k=(1|8) s[126] SampleGrad |^lod k=16 s6 SampleGrad/2 ' "$WORK/lod-ours.txt" | cut -d ' ' -f 2- | tr '\n' ';')" \
  "k=1 s1 SampleGrad 0.00;k=1 s2 SampleGrad 2.00;k=1 s6 SampleGrad 0.00;k=8 s1 SampleGrad 3.00;k=8 s2 SampleGrad 5.00;k=8 s6 SampleGrad 1.00;k=16 s6 SampleGrad/2 1.00;"
expect "and every sample without a sampler bias, or not by gradients, picks D3DMetal's mip" \
  "$(grep -c '^lod k=' "$WORK/lod-ours.txt"):$(grep '^lod k=' "$WORK/lod-ours.txt" | grep -vE ' s[126] SampleGrad' | tr '\n' ';')" \
  "84:$(grep '^lod k=' "$WORK/lod-ref.txt" | grep -vE ' s[126] SampleGrad' | tr '\n' ';')"
# The launcher's default anisotropic filtering (16x, GameSettings) reaches DXMT: s0 (trilinear) at 8:1 takes the short
# axis's mip, 1, where its own sampler takes the long one's (4, as D3DMetal).
ANISO=
run ours lod-16x dxmt "$TESTS/d3d12_lod.exe" "Z:$S/lod.vs.dxil" "Z:$S/lod.ps.dxil"
unset ANISO
expect "the launcher's 16x anisotropic filtering reaches the game's trilinear samplers" \
  "$(grep '^lod k=8 s0 Sample ' "$WORK/lod-16x.txt"):$(grep '^lod k=8 s0 Sample ' "$WORK/lod-ref.txt")" \
  "lod k=8 s0 Sample 1.00:lod k=8 s0 Sample 4.00"
exit $fail
) > "$WORK/lane-D.log" 2>&1 & pD=$!

# Lane E: the D3D11 cache table, vertex reads, ExecuteIndirect, timestamps, replay errors, FSR 3, D3DMetal, dxil-translate, bounds,
# MetalFX upscaling.
(
LANE=E
# 1b. The translation cache's table is named by the build (shader pre-caching spec §3.1): no table of
#     AIRCONV_VERSION alone (26), and a table another build left is dropped when the cache opens.
CACHE="$WORK/cache/version"
run ours version1 dxmt "$LOOP" 320 240 0 0 10 0
db=$(ls "$CACHE"/shaders_*.db 2> /dev/null | head -1)
expect "D3D11 opens the translation cache" "$([ -n "$db" ] && echo yes || echo no)" yes
expect "its table is named by the build, not AIRCONV_VERSION alone" \
  "$(sqlite3 "$db" "SELECT count(*) FROM sqlite_master WHERE name = 'cache_26'" 2> /dev/null)" 0
sqlite3 "$db" "CREATE TABLE cache_1 (key BLOB PRIMARY KEY, value BLOB NOT NULL);"
run ours version2 dxmt "$LOOP" 320 240 0 0 10 0
expect "another build's table is dropped when the cache opens" \
  "$(sqlite3 "$db" "SELECT count(*) FROM sqlite_master WHERE name = 'cache_1'" 2> /dev/null)" 0
unset CACHE
run ours vsread-ours dxmt "$TESTS/d3d12_vsread.exe" "Z:$S/vsread.vs.dxil" "Z:$S/vsread.ps.dxil"
run ref vsread-ref d3dmetal "$TESTS/d3d12_vsread.exe" "Z:$S/vsread.vs.dxil" "Z:$S/vsread.ps.dxil"
expect "a vertex shader reads typed, structured, 3D and cube resources as on D3DMetal" \
  "$(grep '^vsread ' "$WORK/vsread-ours.txt" || echo none)" "$(grep '^vsread ' "$WORK/vsread-ref.txt" || echo 'D3DMetal printed nothing')"
# Task F2: vertex buffer slots a pipeline reads but nobody bound (never set, or a null view) read zeros, which the
# format widens as D3D says ((0,0,0,1) for fewer than four components), in D3D12 and D3D11 (shaders pull vertices
# without a null check: the runtime binds a zeroed buffer). Slot 0 bound to (1, 2, 3, 4).
run ours vsia-ours dxmt "$TESTS/d3d12_vsread.exe" "Z:$S/vsread.vsia.dxil" "Z:$S/vsread.ps.dxil" ia
run ref vsia-ref d3dmetal "$TESTS/d3d12_vsread.exe" "Z:$S/vsread.vsia.dxil" "Z:$S/vsread.ps.dxil" ia
run ours vsia11-ours dxmt "$TESTS/d3d11_vsia.exe"
run ref vsia11-ref d3dmetal "$TESTS/d3d11_vsia.exe"
ia="1,2,3,4 0,0,0,1 0,0,0,0 0,0,0,1"
expect "unbound vertex buffer slots read zeros, widened by their format" "$(grep '^vsread ia' "$WORK/vsia-ours.txt" || echo none)" "vsread ia $ia"
expect "and on D3DMetal" "$(grep '^vsread ia' "$WORK/vsia-ref.txt" || echo 'D3DMetal printed nothing')" "vsread ia $ia"
expect "and in D3D11" "$(grep '^d3d11 ia' "$WORK/vsia11-ours.txt" || echo none)" "d3d11 ia $ia"
expect "and in D3D11 on D3DMetal" "$(grep '^d3d11 ia' "$WORK/vsia11-ref.txt" || echo 'D3DMetal printed nothing')" "d3d11 ia $ia"
# Task F3: IASetVertexBuffers(1, 3, NULL) unbinds slots 1-3 bound to data before, which then read as above (D3DMetal
# ignores such a call: its slots keep their data, 1,2,3,4 1,2,0,1 3,4,1,1.5 1.07374e+09,0,0,1; not pinned).
run ours vsunbind dxmt "$TESTS/d3d12_vsread.exe" "Z:$S/vsread.vsia.dxil" "Z:$S/vsread.ps.dxil" unbind
expect "vertex buffer slots set with no views read zeros" "$(grep '^vsread unbind' "$WORK/vsunbind.txt" || echo none)" "vsread unbind $ia"
# Unreal draws grass and GPU particles with ExecuteIndirect: 1024 of them in one render pass, 8 frames, none lost.
indirect() { run ours "$1" "$2" "$TESTS/d3d12_indirect.exe" "Z:$S/indirect.vs.dxil" "Z:$S/indirect.ps.dxil" \
  "Z:$S/indirect.cs.dxil" "Z:$S/indirect.vsid.dxil" "Z:$S/indirect.psid.dxil"; }
indirect indirect-ours dxmt
indirect indirect-ref d3dmetal
expect "1024 indirect draws in one pass paint every cell" "$(grep '^indirect ok' "$WORK/indirect-ours.txt" || echo none)" "indirect ok 8 0"
expect "and on D3DMetal" "$(grep '^indirect ok' "$WORK/indirect-ref.txt" || echo 'D3DMetal printed nothing')" "indirect ok 8 0"
expect "1024 indirect dispatches in one pass run every thread" "$(grep '^indirect dispatch' "$WORK/indirect-ours.txt" || echo none)" "indirect dispatch 81920 81920"
expect "and on D3DMetal" "$(grep '^indirect dispatch' "$WORK/indirect-ref.txt" || echo 'D3DMetal printed nothing')" "indirect dispatch 81920 81920"
# E10: draw signatures that set nothing but the draw: vertices fetched from StartVertexLocation (or index plus
# BaseVertexLocation), instances from StartInstanceLocation, ByteStride and the argument and index buffer offsets kept,
# an instance count of 0 drawing nothing, a count buffer capping the call, and an index buffer placed where a released
# buffer was. Then vertex buffer views as arguments, for slot 2 of a pipeline using slots 0 and 2 (DXMT 0010), and a
# null one, which reads zeros, not the slot's earlier view (Task F2).
native=$(printf 'indirect %s\n' "native-draw 2,0:107 3,0:100 4,0:100 2,1:108" "counted 2,0:107 2,1:108" \
  "native-indexed 0,0:100 4,0:107 5,0:107 7,0:102 4,1:108 5,1:108" \
  "aliased-indexed 0,0:100 4,0:107 5,0:107 7,0:102 4,1:108 5,1:108" "vbv-gap 2,0:107 3,0:300 2,1:108" "vbv-null 3,0:300")
expect "indirect draws read from the argument buffer draw what D3D12 says" \
  "$(grep -E '^indirect (native|counted|aliased|vbv)' "$WORK/indirect-ours.txt" || echo none)" "$native"
expect "and on D3DMetal" "$(grep -E '^indirect (native|counted|aliased|vbv)' "$WORK/indirect-ref.txt" || echo 'D3DMetal printed nothing')" "$native"
export DXMT_D3D12_INDIRECT=icb
indirect indirect-icb dxmt
unset DXMT_D3D12_INDIRECT
expect "and with DXMT_D3D12_INDIRECT=icb" "$(grep -E '^indirect (ok|native|counted|aliased|vbv)' "$WORK/indirect-icb.txt" || echo none)" \
  "$(printf 'indirect ok 8 0\n%s' "$native")"
# DXMT_STATS: every D3D12 call counted and timed per thread, encoder boundaries with and without a barrier, written to
# <capture folder>/stats.txt (at exit when nothing presents).
rm -rf "$WORK/stats"; export DXMT_DXIL_DUMP="$WORK/stats" DXMT_STATS=1
indirect indirect-stats dxmt
unset DXMT_DXIL_DUMP DXMT_STATS
expect "DXMT_STATS counts every ExecuteIndirect" "$(grep -c '^  list.ExecuteIndirect calls 16390 ' "$WORK/stats/stats.txt" 2> /dev/null || true)" 1
expect "and every encoder boundary, barrier or not" \
  "$(grep -cE '^  encoder boundaries [1-9][0-9]*, [0-9]+ with no barrier$' "$WORK/stats/stats.txt" 2> /dev/null || true)" 1
# E10: the 8192 single draws and the three uncounted calls draw from the argument buffer, with no indirect command buffer
# nor resolver pass; the dispatches' (1024 a frame), the counted call's and the two with vertex buffer views (a
# resolver pass each; Task F2 added vbv-null's) are reused once their allocator is reset.
# With DXMT_D3D12_INDIRECT=icb, every draw call's render pass resolves its commands in one compute pass before it.
expect "uncounted indirect draws read the argument buffer, with no indirect command buffer" \
  "$(grep -oE '(ExecuteIndirect native|indirect command buffers created|indirect resolve passes) [0-9]+' "$WORK/stats/stats.txt" 2> /dev/null | sort | tr '\n' ';')" \
  "ExecuteIndirect native 8195;indirect command buffers created 1026;indirect resolve passes 3;"
rm -rf "$WORK/stats-icb"; export DXMT_DXIL_DUMP="$WORK/stats-icb" DXMT_STATS=1 DXMT_D3D12_INDIRECT=icb
indirect indirect-stats-icb dxmt
unset DXMT_DXIL_DUMP DXMT_STATS DXMT_D3D12_INDIRECT
expect "and with DXMT_D3D12_INDIRECT=icb, one resolver pass per render pass" \
  "$(grep -oE '(ExecuteIndirect native|indirect resolve passes) [0-9]+' "$WORK/stats-icb/stats.txt" 2> /dev/null | tr '\n' ';')" \
  "indirect resolve passes 14;"
# GPU timestamps by D3D12's rules (D3DMetal has none), and a timestamp between draws never splits their pass.
rm -rf "$WORK/ts"; export DXMT_DXIL_DUMP="$WORK/ts" DXMT_DUMP_FRAME=0
run ours ts-ours dxmt "$TESTS/d3d12_timestamp.exe" "Z:$S/depth.vs.dxil" "Z:$S/depth.ps.dxil"
unset DXMT_DXIL_DUMP DXMT_DUMP_FRAME
expect "timestamps: frequency, increasing, advancing, calibrated, across lists and counter buffers" \
  "$(grep '^timestamp rules' "$WORK/ts-ours.txt" || true)" "timestamp rules 1 1 1 1 1 1"
expect "a timestamp between draws keeps them one render pass" "$(grep -c ' render ' "$WORK/ts/passes.txt" 2> /dev/null || true)" 1
expect "a timestamp resolve into a default heap never shows a previous submission's value" \
  "$(grep '^timestamp default-heap' "$WORK/ts-ours.txt" || true)" "timestamp default-heap 1"
# A timestamp resolved on the CPU (into a readback heap) needs no blit encoder: the test's 5 resolves made 10 before.
# Since GPU efficiency E4, 3 lone timestamps also ride on the next encoder (and since Task F4 the 3 of the list of
# timestamps alone share one blit of their own, counted apart), so 2 blit passes are left of 8.
rm -rf "$WORK/ts-stats"; export DXMT_DXIL_DUMP="$WORK/ts-stats" DXMT_STATS=1
run ours ts-stats dxmt "$TESTS/d3d12_timestamp.exe" "Z:$S/depth.vs.dxil" "Z:$S/depth.ps.dxil"
unset DXMT_DXIL_DUMP DXMT_STATS
expect "timestamps resolved on the CPU open no blit encoder" "$(grep -c '^  blit passes 2$' "$WORK/ts-stats/stats.txt" 2> /dev/null || true)" 1
run ours ts-leak dxmt "$TESTS/d3d12_timestamp.exe" "Z:$S/depth.vs.dxil" "Z:$S/depth.ps.dxil" leak
expect "1500 timestamp resolves grow memory by under 16 MB" "$(grep -o 'ok [01]$' "$WORK/ts-leak.txt" || true)" "ok 1"
# A recorded pipeline this DXMT can't build (a junk shader), and a file that isn't a recording.
python3 - "$WORK/junk.pipelines" <<'PY'
import hashlib, struct, sys
def fnv(b):
    h = 0xcbf29ce484222325
    for x in b: h = ((h ^ x) * 0x100000001b3) & 0xffffffffffffffff
    return h
def record(kind, payload, id):
    return struct.pack("<IIQ", kind, len(payload), fnv(payload)) + id + payload
junk = b"DXBC" + bytes(60)
compute = bytes(20) + hashlib.sha1(junk).digest() + struct.pack("<II", 0, 0)  # no root signature, the junk CS
open(sys.argv[1], "wb").write(b"DXMTPRC1" + record(1, junk, hashlib.sha1(junk).digest())
                              + record(3, compute, hashlib.sha1(compute).digest()))
PY
CACHE="$WORK/cache/junk"
expect "a recorded pipeline that no longer builds is counted, not fatal" "$(replay junk "$WORK/junk.pipelines")" \
  "replay: 1 pipelines (0 graphics, 1 compute), 0 created, 1 failed, 0 bad records"
printf 'nope' > "$WORK/foreign.pipelines"
expect "a file that isn't a recording is refused" "$(replay foreign "$WORK/foreign.pipelines")" "replay: not a recording"
unset CACHE
# AMD's FSR 3 swapchain proxy, which SMITE 2 (and other Unreal games with the FSR 3 plugin) create their swapchain
# through: read from the game's install when it's there, never copied.
FFX="$HOME/Library/Application Support/Steam/steamapps/common/SMITE 2/Windows/Hemingway/Binaries/Win64/amd_fidelityfx_dx12.dll"
if [ -f "$FFX" ]; then
  run ours ffx-ours dxmt "$TESTS/d3d12_ffx_swapchain.exe" "Z:$FFX" 60
  expect "the FSR 3 swapchain proxy presents on our DXMT" "$(grep -o 'presented 60/60' "$WORK/ffx-ours.txt" || grep -o 'ffxCreateContext rc=[0-9]*' "$WORK/ffx-ours.txt" || true)" "presented 60/60"
else
  echo "skip the FSR 3 swapchain proxy (SMITE 2 isn't installed)"
fi
# 4. The frozen D3DMetal reference, which every "as on D3DMetal" check compares with, still runs.
run ref d3dmetal d3dmetal "$LOOP" 1280 720 0 0 200 0
expect "the frozen D3DMetal reference still runs" "$(grep -c 'avg frame' "$WORK/d3dmetal.txt" || true)" 1
# 6. dxil-translate: every test shader reaches a Metal pipeline offline (heap.dxil is out of scope on purpose).
"$TOOLS/dxil-translate" "$ROOT/dxmt/tests/dxil" --flags > "$WORK/translate.txt" 2>&1 || true
expect "dxil-translate accepts every behaviour shader but heap" "$(tail -1 "$WORK/translate.txt" | cut -d ' ' -f 2)" "19/20"
# [WaveSize] outside the device's 32 lanes is refused, naming the range (DXIL SM 6.8 plan, Task 4).
"$TOOLS/dxil-translate" "$ROOT/dxmt/tests/dxil/reject" > "$WORK/translate-reject.txt" 2>&1 || true
expect "dxil-translate refuses every out-of-scope shader" "$(tail -1 "$WORK/translate-reject.txt" | cut -d ' ' -f 2)" "0/2"
expect "a [WaveSize] range without 32 is refused, naming it" \
  "$(/usr/bin/grep -oE 'WaveSize\([0-9]+, [0-9]+\)\] excludes' "$WORK/translate-reject.txt" | tr '\n' ';')" \
  "WaveSize(64, 64)] excludes;WaveSize(8, 16)] excludes;"
# Task F2, Task FR: DXIL barriers fence memory as Metal Shader Converter does at Metal 3.1 (an air.atomic.fence, then
# for a synced one an air.wg.barrier), the device and texture ones at device scope whether or not they sync the group
# (a barrier's memory flags only order within the group). dxil/barriers.hlsl's modes 8, 9, 2, 3, 10, 11:
# air.atomic.fence flags:scope 2:1 (8), 5:3 (2 and 3), 7:3 (10 and 11), and 3 air.wg.barrier calls (9, 3, 11). The
# runtime group can't tell (each fence is followed by a synced barrier). DXBC's (D3D11) the same, from dxbc/sync.hlsl,
# at Metal 3.1 as the tool translates. No row runs the 3.2 path D3D11 takes on this hardware (dxbc_converter_base.cpp's
# metal_version >= 3.2 branch): the runtime groups are DXIL only. ponytail: a D3D11 compute row of sync.hlsl vs D3DMetal.
expect "DXIL barriers fence memory, at device scope for device memory" \
  "$(grep '^ok barriers\.dxil ' "$WORK/translate.txt" | grep -oE 'fence=[^ ]+ barrier=[0-9]+' || echo none)" \
  "fence=2:1,5:3,5:3,7:3,7:3 barrier=3"
"$TOOLS/dxil-translate" "$ROOT/dxmt/tests/dxbc" --flags > "$WORK/translate-dxbc.txt" 2>&1 || true
expect "and DXBC's, GroupMemoryBarrier's threadgroup fence included" \
  "$(grep '^ok sync\.dxbc ' "$WORK/translate-dxbc.txt" | grep -oE 'fence=[^ ]+ barrier=[0-9]+' || echo none)" \
  "fence=2:1,5:3,5:3,7:3,7:3 barrier=3"
"$TOOLS/dxil-translate" "$S" > "$WORK/translate-shaders.txt" 2>&1 || true
expect "dxil-translate accepts the test shaders" "$(tail -1 "$WORK/translate-shaders.txt" | cut -d ' ' -f 2)" "45/45"
# DXIL keeps NaN and infinity: no translated shader assumes them away or keeps a fast compare. Vertex and geometry
# shaders also carry no reassoc, contract or arcp flags, as airconv's DXBC path, so LLVM fuses or reorders nothing on
# its own: a depth prepass and a base pass then compute the same positions, and their depth EQUAL test holds (grass
# flickered in SMITE 2 without it). The row checks those flags only: since Task F2 a non-precise mad is an explicit
# air.fma (a call with no flags, the same in every shader lowered from the same DXIL; DXC's -Gis marks every mad
# precise, so no fma there).
"$TOOLS/dxil-translate" "$S" --flags > "$WORK/translate-flags.txt" 2>&1 || true
expect "no translated shader assumes NaN or infinity away" \
  "$(grep -c '^ok ' "$WORK/translate-flags.txt" || true):$(grep '^ok ' "$WORK/translate-flags.txt" | grep -c ' nnan=0 ninf=0 cmp=0 ' || true)" "45:45"
expect "vertex and geometry shaders carry no reassoc, contract or arcp flags" \
  "$(grep -E '^ok [^ ]+ (vs|gs) ' "$WORK/translate-flags.txt" | grep -c ' reassoc=0 contract=0 arcp=0$' || true)" 15
# Task FU: Metal's gradient sample takes no LOD bias, so the sampler's MipLODBias scales the derivatives (2^bias): every
# gradient sample of shaders/lod.hlsl (DXIL SampleGrad) and dxbc/grad.hlsl (DXBC sample_d) has an fmul operand.
expect "gradient samples carry the sampler's LOD bias, DXIL and DXBC" \
  "$(grep -hE '^ok (lod\.ps\.dxil|grad\.dxbc) ' "$WORK/translate-flags.txt" "$WORK/translate-dxbc.txt" | grep -oE 'grad=[0-9/]+' | tr '\n' ' ')" \
  "grad=7/7 grad=1/1 "
# E5: a raw or structured buffer load is bounds-checked once for all its components, those the shader reads
# (shaders/bounds.hlsl: a 16-dword view from dword 4 and a 4-element view, over buffers holding 1..32). In bounds (the
# view's last dwords too), out of bounds and at an offset whose end wraps 32 bits read as on D3DMetal; a load
# straddling the view's end reads zeros, where D3DMetal reads past the view (19 20 21 22, 20 21).
run ours bounds dxmt "$TESTS/d3d12_bounds.exe" "Z:$S/bounds.cs.dxil"
run ref bounds-ref d3dmetal "$TESTS/d3d12_bounds.exe" "Z:$S/bounds.cs.dxil"
expect "buffer loads read zeros outside their views, in one check per load" \
  "$(grep '^bounds ' "$WORK/bounds.txt")" "bounds 5 6 7 8 0 0 0 0 0 0 0 0 0 0 0 0 13 14 15 16 0 0 0 0 20 19 20 0 0 0 0 0"
expect "and as on D3DMetal but where a load straddles the view's end" \
  "$(grep '^bounds ' "$WORK/bounds-ref.txt")" "bounds 5 6 7 8 19 20 21 22 0 0 0 0 20 21 0 0 13 14 15 16 0 0 0 0 20 19 20 0 0 0 0 0"
# Task F2: DXMT_DXIL_BOUNDS=off (for measuring their cost) drops the checks, typed Buffer<> reads' too: loads inside
# their views read as before, loads past them read the buffer beyond (all inside its 32 dwords here; the offset that
# wraps 32 bits reads the 4 dwords before the view), and a vertex shader's typed reads at a FirstElement read right.
export DXMT_DXIL_BOUNDS=off
run ours bounds-off dxmt "$TESTS/d3d12_bounds.exe" "Z:$S/bounds.cs.dxil"
run ours vsread-off dxmt "$TESTS/d3d12_vsread.exe" "Z:$S/vsread.vs.dxil" "Z:$S/vsread.ps.dxil"
unset DXMT_DXIL_BOUNDS
expect "DXMT_DXIL_BOUNDS=off reads past views, and inside them as before" \
  "$(grep '^bounds ' "$WORK/bounds-off.txt")" "bounds 5 6 7 8 19 20 21 22 21 22 23 24 20 21 0 0 13 14 15 16 17 18 19 20 20 19 20 0 1 2 3 4"
expect "and a vertex shader's typed and structured reads as without it" \
  "$(grep '^vsread ' "$WORK/vsread-off.txt" || echo none)" "$(grep '^vsread ' "$WORK/vsread-ours.txt" || echo none)"
# XeSS answered by MetalFX (spec §4.1): DXMT's private D3D12 interface runs MetalFX's temporal upscaler in the command
# list's order. 64 jittered frames of the spike's scene upscaled to 2560x1440 beat a bilinear upscale of the last input
# against the unjittered scene (d3d12_upscale prints both PSNRs), at three ratios and with the inputs MetalFX can't
# take directly (a depth/stencil depth, a render-target-only output, typeless motion vectors, typeless colour and
# output), on a COMPUTE list (with a reactive mask); bad calls (and unlisted typeless colours) record nothing.
upscaled() { grep -q "^upscale $2 ok" "$WORK/$1.txt" && echo yes || { grep -m1 -E '^upscale|failed' "$WORK/$1.txt" || echo none; }; }
for r in 1.5 2.0 3.0; do
  run ours "upscale-ratio-$r" dxmt "$TESTS/d3d12_upscale.exe" ratio "$r"
  expect "DXMT upscales ratio $r" "$(upscaled "upscale-ratio-$r" "ratio $r")" yes
done
for m in depthstencil rtoutput compute typeless typeless32 typeless10 bad; do
  run ours "upscale-$m" dxmt "$TESTS/d3d12_upscale.exe" "$m"
  expect "DXMT upscales $m" "$(upscaled "upscale-$m" "$m")" yes
done
# GPU-only heaps and the textures on them are Private (DXMT 0009), and a 2D UAV Metal can render to gets RenderTarget
# usage: MetalFX writes a UAV-only output directly, committed, placed in a DEFAULT heap, or at an offset in a larger one
# (XeSS's outputColorBase), as well as through a scratch texture and a copy (DXMT_STATS counts the outputs copied), which
# it does with DXMT_D3D12_PRIVATE=0 (all Shared): the same PSNR within 0.05 dB. An input MetalFX can't read as it is
# (a depth/stencil depth) is copied first; readable ones aren't.
copied() { awk "/^  upscale ${2:-output} copied /{n += \$4} END {print n + 0}" "$WORK/$1/stats.txt" 2> /dev/null || echo none; }
psnr() { awk '/^upscale .* ok psnr /{print $5}' "$WORK/$1.txt" 2> /dev/null; }
near() {  # near <run> <run>: both upscaled, their PSNRs within 0.05 dB
  awk -v a="$(psnr "$1")" -v b="$(psnr "$2")" \
    'BEGIN { d = a - b; print (a != "" && b != "" && d <= 0.05 + 1e-9 && -d <= 0.05 + 1e-9) ? "yes" : "no: " a " vs " b }'
}
for r in direct:direct placed:placed shared:direct offset:direct-offset shared-offset:direct-offset ds:depthstencil; do
  n=${r%:*} m=${r#*:}
  rm -rf "$WORK/upscale-$n-stats"; export DXMT_DXIL_DUMP="$WORK/upscale-$n-stats" DXMT_STATS=1
  case $n in shared*) export DXMT_D3D12_PRIVATE=0 ;; esac
  run ours "upscale-$n" dxmt "$TESTS/d3d12_upscale.exe" "$m"
  unset DXMT_DXIL_DUMP DXMT_STATS DXMT_D3D12_PRIVATE
done
expect "MetalFX writes a private UAV output directly" "$(near upscale-direct upscale-shared):$(copied upscale-direct-stats)" yes:0
expect "and one placed in a DEFAULT heap" "$(near upscale-placed upscale-shared):$(copied upscale-placed-stats)" yes:0
expect "and copies it with DXMT_D3D12_PRIVATE=0" \
  "$(upscaled upscale-shared direct):$(copied upscale-shared-stats | sed 's/^[1-9][0-9]*$/some/')" yes:some
expect "and writes at an offset in a larger output, directly or through the copy" \
  "$(near upscale-offset upscale-shared-offset):$(copied upscale-offset-stats):$(copied upscale-shared-offset-stats | sed 's/^[1-9][0-9]*$/some/')" \
  yes:0:some
expect "a depth/stencil depth is copied for MetalFX, readable inputs aren't" \
  "$(upscaled upscale-ds depthstencil):$(copied upscale-ds-stats input | sed 's/^[1-9][0-9]*$/some/'):$(copied upscale-direct-stats input)" \
  yes:some:0
# Task F4: timestamps around the upscale ride the blits before and after MetalFX's own passes (which take none): two
# before each of the 64 upscales and one after, in order with the upscale between, none with a blit of its own (128
# with DXMT_D3D12_TIMESTAMP_BLITS=1: the end of the call, and the upscale taking no start sample).
for m in shared blits; do
  rm -rf "$WORK/upscale-ts-$m"; export DXMT_DXIL_DUMP="$WORK/upscale-ts-$m" DXMT_STATS=1
  [ $m = blits ] && export DXMT_D3D12_TIMESTAMP_BLITS=1
  run ours "upscale-ts-$m" dxmt "$TESTS/d3d12_upscale.exe" timestamps
  unset DXMT_DXIL_DUMP DXMT_STATS DXMT_D3D12_TIMESTAMP_BLITS
done
uts() { echo "$(lone "upscale-ts-$1"):$(upscaled "upscale-ts-$1" timestamps):$(grep -oE '^upscale timestamps order [01]' \
  "$WORK/upscale-ts-$1.txt")"; }
expect "timestamps around the upscale ride the blits beside MetalFX's passes" "$(uts shared)" \
  ":yes:upscale timestamps order 1"
expect "and with DXMT_D3D12_TIMESTAMP_BLITS=1, get blits of their own" "$(uts blits)" \
  "lone timestamps (end of call) 64;lone timestamps (next encoder takes none) 64;timestamps given their own encoder 128;:yes:upscale timestamps order 1"
run ours upscale-range dxmt "$TESTS/d3d12_upscale.exe" range
expect "the device reports MetalFX's scale range" "$(grep '^range ' "$WORK/upscale-range.txt" || echo none)" "range 1.000 3.000"
export MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=nslog
run ours upscale-val dxmt "$TESTS/d3d12_upscale.exe" depthstencil rtoutput compute typeless32 typeless10 bad direct placed
run ours upscale-valo dxmt "$TESTS/d3d12_upscale.exe" direct-offset timestamps # apart: 10 modes take ~70 s (watchdog)
unset MTL_DEBUG_LAYER MTL_DEBUG_LAYER_ERROR_MODE
expect "Metal's validation rejects nothing in the upscale" \
  "$(invalid upscale-val):$(grep -c '^upscale [a-z0-9]* ok' "$WORK/upscale-val.txt" || true)" "0:8"
expect "nor at an output offset, nor with timestamps around it" \
  "$(invalid upscale-valo):$(grep -cE '^upscale (direct-offset|timestamps) ok' "$WORK/upscale-valo.txt" || true)" "0:2"
# XeSS answered by MetalFX (spec §4.2, §7): Wine's builtin libxess.dll driven through XeSS's API. d3d12_xess loads it
# by full path, the game's way, from a copy of itself named libxess.dll: only the launcher's libxess=b makes that the
# builtin. Every quality mode at its input size beats bilinear; a re-initialised context, one destroyed before its list
# runs, 20 more made and destroyed (DXMT reuses their upscaler), A -> B -> A, a history reset, and 5 settings in turn
# beside a live context (DXMT keeps 4 released); dynamic resolution keeping the upscaler and a new motion vector
# format replacing it; Init's flags, with real frames for the ones that change what the inputs mean and for Unreal's
# call (no depth texture), and the version calls; Unreal's call on a COMPUTE list and with inverted depth, the bridge's
# own depth read back at the far plane (1.0; not inverted's 0.0: fresh memory reads 0.0 uncleared too); "unsupported"
# for an object without DXMT's interface (under wined3d, Wine makes no D3D12 device here). Task FR: native AA at
# 1728x1117 (the built-in display's odd height) keeps input = output; an allocator released without a Reset still
# returns its upscaler to DXMT's pool; XeSS's API contract (build status, textures Init's flags ask for, the logging
# callback's level, the exports the bridge doesn't implement). A failing mode's FAIL line shows.
mkdir -p "$WORK/xess" && cp "$TESTS/d3d12_xess.exe" "$WORK/xess/libxess.dll"
xessed() {
  grep -q "^xess $2 ok" "$WORK/$1.txt" && echo yes ||
    { grep -m1 -E "^xess( $2)? FAIL|failed" "$WORK/$1.txt" || grep -m1 '^xess' "$WORK/$1.txt" || echo none; }
}
for m in aa:2560x1440 quality:1504x846 balanced:1280x720 performance:1112x626 ultraperf:854x480 aaodd:1728x1117; do
  run ours "xess-${m%:*}" dxmt "$TESTS/d3d12_xess.exe" "Z:$WORK/xess/libxess.dll" "${m%:*}"
  expect "XeSS ${m%:*} asks for ${m#*:}" "$(grep -m1 "^xess ${m%:*} input" "$WORK/xess-${m%:*}.txt" || echo none)" \
    "xess ${m%:*} input ${m#*:}"
  expect "XeSS ${m%:*} upscales on MetalFX" "$(xessed "xess-${m%:*}" "${m%:*}")" yes
done
for m in cycles reuse flags nodepth allocator; do
  run ours "xess-$m" dxmt "$TESTS/d3d12_xess.exe" "Z:$WORK/xess/libxess.dll" "$m"
  expect "XeSS $m" "$(xessed "xess-$m" "$m")" yes
done
# Build status only between BuildPipelines and Init (-12 WRONG_CALL_ORDER outside); a texture Init's flags ask for
# missing at Execute: -4; an Init refusal reaches a callback registered at ERROR at ERROR (3); the exports the bridge
# doesn't implement answer a live context -7 (NOT_IMPLEMENTED), a destroyed one -8.
run ours xess-api dxmt "$TESTS/d3d12_xess.exe" "Z:$WORK/xess/libxess.dll" api
expect "XeSS's API contract" "$(grep -m1 '^xess api' "$WORK/xess-api.txt" || echo none)" \
  "xess api status -12/0/-12/0 exposure -4 mask -4 log 1:3 unimplemented -7,-7,-7,-7,-7,-7,-7,-7,-7,-7 stale -8"
run ours xess-wd wined3d "$TESTS/d3d12_xess.exe" "Z:$WORK/xess/libxess.dll" unsupported
expect "XeSS reports no device for an object without DXMT's interface" "$(xessed xess-wd unsupported)" yes
export MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=nslog
run ours xess-val dxmt "$TESTS/d3d12_xess.exe" "Z:$WORK/xess/libxess.dll" performance cycles
# flags apart (the watchdog): its nodepth case is Unreal's call, the bridge's own depth (MetalFX wants it colour-sized)
run ours xess-valf dxmt "$TESTS/d3d12_xess.exe" "Z:$WORK/xess/libxess.dll" flags
run ours xess-valn dxmt "$TESTS/d3d12_xess.exe" "Z:$WORK/xess/libxess.dll" nodepth
unset MTL_DEBUG_LAYER MTL_DEBUG_LAYER_ERROR_MODE
expect "Metal's validation rejects nothing in XeSS" \
  "$(invalid xess-val):$(grep -c '^xess [a-z]* ok' "$WORK/xess-val.txt" || true)" "0:2"
expect "nor in its flags, Unreal's call without depth included" \
  "$(invalid xess-valf):$(grep -c '^xess flags ok' "$WORK/xess-valf.txt" || true)" "0:1"
expect "nor without depth on a COMPUTE list or with inverted depth" \
  "$(invalid xess-valn):$(grep -c '^xess nodepth ok' "$WORK/xess-valn.txt" || true)" "0:1"
exit $fail
) > "$WORK/lane-E.log" 2>&1 & pE=$!

for l in A B C D E; do eval "wait \$p$l" || fail=1; eval "p$l="; cat "$WORK/lane-$l.log"; done

# Checks run alone last: the queue stats count what DXMT saw at call time against the GPU's progress, and section 10
# reads lane A's prefix and the launcher log, which other launches would write to.
unset LANE
# B (GPU overlap spec §3.10): Wait, ExecuteCommandLists and Signal go into one Metal command buffer. d3d12_hazards
# queues alone: three such sequences over two queues.
rm -rf "$WORK/queues-stats"; export DXMT_DXIL_DUMP="$WORK/queues-stats" DXMT_STATS=1
run ours queues-stats dxmt "$TESTS/d3d12_hazards.exe" "Z:$S" queues
unset DXMT_DXIL_DUMP DXMT_STATS
expect "a Wait rides in its ExecuteCommandLists' command buffer, which commits at once" \
  "$(grep -oE 'command buffers committed [0-9]+' "$WORK/queues-stats/stats.txt" 2> /dev/null)" "command buffers committed 6"
# M5 (GPU overlap spec §3.11): a queue waiting on another queue's fence waits on its MTLEvent (released in under 1 us,
# against 130-150 us for the shared event): both of d3d12_hazards queues' waits, neither met when encoded.
expect "a queue waits on another queue's fence through its GPU event" \
  "$(grep -oE '(queue waits on the GPU event|queue waits already met) [0-9]+' "$WORK/queues-stats/stats.txt" 2> /dev/null | tr '\n' ';')" \
  "queue waits on the GPU event 2;"
# A signal waits for timestamps resolved on the CPU only while some are pending (d3d12_hazards deferred alone).
rm -rf "$WORK/deferred-stats"; export DXMT_DXIL_DUMP="$WORK/deferred-stats" DXMT_STATS=1
run ours deferred-stats dxmt "$TESTS/d3d12_hazards.exe" "Z:$S" deferred
unset DXMT_DXIL_DUMP DXMT_STATS
expect "a signal behind no pending timestamps stays on the GPU" \
  "$(grep -oE 'fence signals deferred to the CPU [0-9]+' "$WORK/deferred-stats/stats.txt" 2> /dev/null)" "fence signals deferred to the CPU 1"
# Task F4: a call that samples timestamps without resolving them owes their values too (the queue writes them into
# the query heap once its command buffer completes), so its fence waits for that: the app may release the heap once
# the fence passed (d3d12_hazards sampled alone).
rm -rf "$WORK/sampled-stats"; export DXMT_DXIL_DUMP="$WORK/sampled-stats" DXMT_STATS=1
run ours sampled-stats dxmt "$TESTS/d3d12_hazards.exe" "Z:$S" sampled
unset DXMT_DXIL_DUMP DXMT_STATS
expect "a signal after timestamps sampled without a resolve waits for their values" \
  "$(grep -oE 'fence signals deferred to the CPU [0-9]+' "$WORK/sampled-stats/stats.txt" 2> /dev/null):$(hazards sampled-stats)" \
  "fence signals deferred to the CPU 1:hazard sampled 1"
# Task FS (Rulings 36-37): exclusive fullscreen on Wine's emulated modes, which the launcher's prefixes have
# (EmulateModeset; the program refuses to run without it, so the Mac's display never changes). Alone: it needs its
# windows in front and behind, which another lane's windows would change.
# The program's guard reads EmulateModeset as Wine does: off for it in AppDefaults is off, whatever X11 Driver says.
run ours fs-guard dxmt "$TESTS/d3d12_fullscreen.exe" guard
expect "the fullscreen test refuses to run where its own AppDefaults turn emulation off" \
  "$(grep '^guard' "$WORK/fs-guard.txt" || echo none)" "guard not emulated"
for api in d3d12 d3d11; do
  rm -rf "$WORK/fs-$api"; export DXMT_DXIL_DUMP="$WORK/fs-$api" DXMT_STATS=1
  run ours "fs-$api" dxmt "$TESTS/d3d12_fullscreen.exe" $api
  unset DXMT_DXIL_DUMP DXMT_STATS
  expect "$api: leaving fullscreen in front restores the window as it was before the mode change" \
    "$(grep -E '^(fg-leave|not emulated)' "$WORK/fs-$api.txt" || echo none)" "fg-leave 1 0 same"
  expect "$api: leaving it with the game's own popup in front keeps the window up" \
    "$(grep '^popup-leave' "$WORK/fs-$api.txt" || echo none)" "popup-leave 1 0"
  expect "$api: leaving it with another process's window in front minimises the window, once" \
    "$(grep '^bg-leave' "$WORK/fs-$api.txt" || echo none):$(grep -c 'Leaving fullscreen in the background: minimising' "$WORK/fs-$api.txt" || true)" \
    "bg-leave 1 1:1"
done
expect "presents to a minimised window return at once, as presents, and free the frame latency slot, with no drawable" \
  "$(grep '^minimised presents' "$WORK/fs-d3d12.txt" || echo none):$(grep -oE 'presents skipped \(window minimised\) [0-9]+' "$WORK/fs-d3d12/stats.txt" 2> /dev/null | awk '{ n += $NF } END { print n + 0 }')" \
  "minimised presents 0x0 0 31 fast:31"
# Each report (every 5 s of frames, and at exit) counts since the last one: the sums.
expect "and count as frames in DXMT_STATS (31 and the restored one)" \
  "$(grep -oE '^# .* [0-9]+ frames' "$WORK/fs-d3d12/stats.txt" 2> /dev/null | awk '{ n += $(NF - 1) } END { print n + 0 }')" 32
expect "and the restored window presents again" "$(grep '^restored present' "$WORK/fs-d3d12.txt" || echo none)" \
  "restored present 0x0"
# 10. The launcher (spec §3.7; gate L1): recordings land in the compat folder and the first session stamps the builds;
#     with another build in the stamp, the next launch replays every recording before the game, which then only hits.
#     (d3d12_cache's recording there holds every mode section 7 ran: a, rt, layout and root.)
V=$(cat "$WORK/ours/wine.app/Contents/Resources/DXMT/translator" 2> /dev/null || true)  # the stamp's key (Ruling 46)
LANE=A; P="$WORK/compat/ours-A/dxmt-pipelines"  # lane A's prefix: its d3d12_cache runs recorded and stamped there
expect "the launcher records into the game's compat folder" "$([ -s "$P/d3d12_cache.exe.pipelines" ] && echo yes || echo no)" yes
expect "and stamps the builds after the first session" "$(cut -d ' ' -f 1 "$P/replayed" 2> /dev/null)" "${V:-no DXMT/translator}"
echo "old build" > "$P/replayed"
# The launcher log's lines since the run's start, by their UTC second (after a 1 s wait no earlier line shares it),
# from launcher.log.1 too: the log rotates at 1 MB, which may come mid-row.
LLOG="$HOME/Library/Logs/MacNeutron/launcher.log"; sleep 1; since=$(date -u +%Y-%m-%dT%H:%M:%SZ)
CACHE="$WORK/cache/e2e"
cachetest e2e a
unset CACHE
expect "a changed build replays d3d12_cache's recording before the game" \
  "$(cat "$LLOG.1" "$LLOG" 2> /dev/null | awk -v s="$since" '$1 >= s' | grep -cE 'precache: d3d12_cache\.exe\.pipelines exit=0 replay: [1-9][0-9]* pipelines .*, 0 failed, 0 bad records' || true)" 1
expect "then the game only hits" "$(counters e2e)" "d3d12 shader cache: functions 3 hit 0 missed, reflections 3 hit 0 missed"
expect "and draws as D3DMetal" "$(drawn e2e)" "$(drawn cache-ref-a)"
expect "and the stamp holds the current builds" "$(cut -d ' ' -f 1 "$P/replayed")" "${V:-no DXMT/translator}"

[ $fail = 0 ] && echo "dxmt-check: all passed"
exit $fail
