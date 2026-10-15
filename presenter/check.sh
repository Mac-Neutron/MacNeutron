#!/bin/sh
# Runs the MetalFX presenter inside wine.app through the launcher on DXMT: real Wine, no Steam (upscaler spec §6,
# release spec §9 L4). Needs `make build wine-arm64 presenter`. The tool folder is assembled from
# build/wine-arm64/wine.app with `macneutron install`; the launcher sets MACNEUTRON_PRESENT=1 for the game, and DXMT's
# winemetal.so loads the presenter from beside itself. MACNEUTRON_NO_METALFX=1 turns that off (the runs without it)
# unless MACNEUTRON_POST_AA=cmaa2 asks for its CMAA2 anti-aliasing, which runs natively first (tests/cmaa2_check.m).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
B="$ROOT/build/presenter"
WORK="${TMPDIR:-/tmp}/macneutron presenter"
TOOL="$WORK/tool"
mkdir -p "$WORK/compat"
"$ROOT/.build/release/macneutron" install --tool-dir "$TOOL" --wine-app "$ROOT/build/wine-arm64/wine.app" \
  || { echo "check: assembling the tool folder failed" >&2; exit 1; }
fail=0
expect() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got [$2], want [$3]"; fail=1; fi; }

# run_loop <name> <inject 0|1> <scale> <present_loop args...>  →  output in $WORK/<name>.txt; $aa, when set, is
# MACNEUTRON_POST_AA, $refuse MACNEUTRON_PRESENT_REFUSE (an output size MetalFX is made to refuse), $sharpen
# MACNEUTRON_SHARPEN
aa= refuse= sharpen=
run_loop() {
  name=$1 inject=$2 scale=$3; shift 3
  off=; [ "$inject" = 1 ] || off=1
  env STEAM_COMPAT_DATA_PATH="$WORK/compat/0" SteamAppId=0 MACNEUTRON_GRAPHICS=dxmt MACNEUTRON_NO_STEAM_BRIDGE=1 \
      ${off:+MACNEUTRON_NO_METALFX=1} ${aa:+MACNEUTRON_POST_AA=$aa} ${refuse:+MACNEUTRON_PRESENT_REFUSE=$refuse} \
      ${sharpen:+MACNEUTRON_SHARPEN=$sharpen} \
      MACNEUTRON_PRESENT_SCALE="$scale" \
      MACNEUTRON_PRESENT_DUMP="$WORK/frame.ppm" \
      "$TOOL/bin/macneutron" launch waitforexitandrun "$B/present_loop.exe" "$@" > "$WORK/$name.out" 2>&1 &
  pid=$!
  ( sleep 120; kill "$pid" 2>/dev/null ) & dog=$!
  wait "$pid" || true
  kill "$dog" 2>/dev/null || true
  tr -d '\r' < "$WORK/$name.out" > "$WORK/$name.txt"
}
count() { LC_ALL=C /usr/bin/grep -c "$1" "$WORK/$2.txt" || true; }   # count <pattern> <run>
frame_ms() { LC_ALL=C /usr/bin/grep -o 'avg frame [0-9.]*' "$WORK/$1.txt" | awk '{print $3}'; }
edge() { python3 "$ROOT/presenter/tests/pixels.py" "$1" edges; }   # edge <ppm>: how hard its edges are

# CMAA2 natively: wine.app's presenter on layers outside any window, under Metal's shader validation, then again under
# its API validation (both at once crash inside MetalTools on a view of a drawable's texture). BGRA8 layers, then
# RGB10A2 and BGR10A2 (10-bit SDR, no sRGB view: CMAA2 decodes and encodes by hand and keeps 10 bits).
LIB="$ROOT/build/wine-arm64/wine.app/Contents/Resources/lib/wine/aarch64-unix/libmacneutron-present.dylib"
native() {  # native <name> <variable=value...> <cmaa2_check args...>  →  $WORK/<name>.txt, the exit statuses in <name>.status
  name=$1; shift
  s1=0; env MTL_SHADER_VALIDATION=1 MACNEUTRON_PRESENT_SCALE=1 "$@" > "$WORK/$name.txt" 2>&1 || s1=$?
  s2=0; env MTL_DEBUG_LAYER=1 MACNEUTRON_PRESENT_SCALE=1 "$@" > "$WORK/$name.api.txt" 2>&1 || s2=$?
  echo "$s1$s2" > "$WORK/$name.status"
}
value() { sed -n "s/^$1: //p" "$WORK/$2.txt"; }  # value <what> <run>
statuses=
for fmt in "" rgb10a2 bgr10a2; do for size in "2560 1440" "1728 1117"; do
  at="${size% *}${fmt:+ $fmt}" n=aa_on_${size% *}$fmt o=aa_off_${size% *}$fmt
  native "$n" MACNEUTRON_POST_AA=cmaa2 "$B/cmaa2_check" "$LIB" frames $size $fmt
  native "$o" "$B/cmaa2_check" "$LIB" frames $size $fmt
  statuses="$statuses$(cat "$WORK/$n.status")$(cat "$WORK/$o.status")"
  expect "post-AA off leaves frames byte for byte at $at" "$(value 'dense changed' "$o"):$(value 'sparse changed' "$o")" "0:0"
  expect "CMAA2 leaves a flat frame alone at $at" "$(value 'flat changed' "$n")" 0
  expect "CMAA2 changes only pixels next to edges at $at" "$(value 'sparse changed far from edges' "$n")" 0
  expect "CMAA2 keeps every pixel's alpha at $at" "$(value 'alpha changed' "$n")" 0
  expect "CMAA2 changes 2-12 per mille of a frame of silhouettes at $at" \
    "$(awk -v v="$(value 'sparse changed per mille' "$n")" 'BEGIN { print (v != "" && v >= 2 && v <= 12) ? "yes" : "no (" v ")" }')" yes
  expect "CMAA2 keeps 80 % of 1-px glyph strokes' contrast at $at" \
    "$(awk -v v="$(value 'glyph contrast kept percent' "$n")" 'BEGIN { print (v != "" && v >= 80 && v < 100) ? "yes" : "no (" v ")" }')" yes
  # 10 bits all the way: about 3 in 4 of the blended channel values are codes no 8-bit value gives (27-29 % when the
  # blends went through 8-bit sRGB, the 8-bit path's packing)
  [ -z "$fmt" ] || expect "CMAA2 blends at 10 bits, no 8-bit step, at $at" \
    "$(awk -v v="$(value 'sparse blended values off the 8-bit grid percent' "$n")" 'BEGIN { print (v != "" && v >= 60) ? "yes" : "no (" v ")" }')" yes
done; done
native aa_hdr MACNEUTRON_POST_AA=cmaa2 "$B/cmaa2_check" "$LIB" frames 1280 720 fp16
expect "CMAA2 leaves HDR layers alone" \
  "$(value 'dense changed' aa_hdr):$(value 'sparse changed' aa_hdr):$(count 'left alone (HDR' aa_hdr)" "0:0:1"
rm -f "$WORK/aa_up.ppm" "$WORK/aa_up_off.ppm"
native aa_up MACNEUTRON_POST_AA=cmaa2 MACNEUTRON_PRESENT_SCALE=2 MACNEUTRON_PRESENT_DUMP="$WORK/aa_up.ppm" \
  "$B/cmaa2_check" "$LIB" upscale
native aa_up_off MACNEUTRON_PRESENT_SCALE=2 MACNEUTRON_PRESENT_DUMP="$WORK/aa_up_off.ppm" "$B/cmaa2_check" "$LIB" upscale
expect "CMAA2 runs on the game-size frame before MetalFX" \
  "$(count 'MetalFX 640x360 -> 1280x720' aa_up):$(count 'MetalFX 640x360 -> 1280x720' aa_up_off):$(value 'drawable changed' aa_up_off):$(
    awk -v v="$(value 'drawable changed' aa_up)" 'BEGIN { print (v > 0 ? "changed" : "unchanged") }'):$(
    cmp -s "$WORK/aa_up.ppm" "$WORK/aa_up_off.ppm" && echo same || { [ -s "$WORK/aa_up.ppm" ] && echo differs; } || echo none)" \
  "1:1:0:changed:differs"
expect "Metal validation clean" "$statuses$(cat "$WORK/aa_hdr.status" "$WORK/aa_up.status" "$WORK/aa_up_off.status" | tr -d '\n')" \
  000000000000000000000000000000
for fmt in "" rgb10a2; do for size in "1728 1117" "2560 1440"; do  # the GPU time CMAA2 adds per frame, printed (no validation)
  MACNEUTRON_POST_AA=cmaa2 MACNEUTRON_PRESENT_SCALE=1 "$B/cmaa2_check" "$LIB" cost $size $fmt 2>/dev/null | sed 's/^/info /'
done; done

# What the sharpening costs, printed: it replaces the presenter's copy of the frame, so the difference against the run
# without it is the pass's own cost. At a 2x upscale (1280x720 -> 2560x1440): at scale 1 nothing is upscaled, so
# nothing is sharpened and every number would read 0.
for s in "" 0.5 1; do
  printf 'info sharpening %s: ' "${s:-off}"
  env MACNEUTRON_PRESENT_SCALE=2 ${s:+MACNEUTRON_SHARPEN=$s} "$B/cmaa2_check" "$LIB" cost 1280 720 2>/dev/null |
    sed -n '1s/^cost //p'
done

# The prefix, prepared before the timed runs (a no-op when it is current).
env STEAM_COMPAT_DATA_PATH="$WORK/compat/0" SteamAppId=0 MACNEUTRON_GRAPHICS=dxmt \
    "$TOOL/bin/macneutron" launch getcompatpath "$WORK" > /dev/null 2>&1 \
  || { echo "check: the launcher didn't prepare $WORK/compat/0/pfx" >&2; exit 1; }

run_loop pass 1 1 1280 720 0 0 200 0
expect "full-size game passes through" "$(count 'macneutron-present: MetalFX' pass)" 0

rm -f "$WORK/frame.ppm"
run_loop up 1 1 1280 720 640 360 300 0
expect "small swap chain is upscaled" "$(count 'macneutron-present: MetalFX 640x360 -> 1280x720' up)" 1
expect "upscaled frame shows the whole checkerboard" \
  "$( [ -f "$WORK/frame.ppm" ] && python3 "$ROOT/presenter/tests/pixels.py" "$WORK/frame.ppm" || echo none)" "WNWNWNWN"

# MACNEUTRON_SHARPEN sharpens what the presenter upscales, in place of its copy of MetalFX's output (CAS): the note
# says so, a frame that isn't upscaled is left alone, and the same frames come out with harder edges. The edge metric
# uses CMAA2's DENSE study image (1-px lines and glyphs on a low-contrast gradient), which is what CAS works on: on a
# frame whose values are mostly clipped its contrast-adaptive weight nearly vanishes, by design.
run_loop sharp_note 1 1 1280 720 640 360 300 0
expect "no sharpening without MACNEUTRON_SHARPEN" "$(count 'sharpen' sharp_note)" 0
sharpen=0.8
run_loop sharp_on 1 1 1280 720 640 360 300 0
expect "the note says what it sharpened" \
  "$(count 'macneutron-present: MetalFX 640x360 -> 1280x720, sharpen 0.80' sharp_on)" 1
run_loop sharp_pass 0 1 1280 720 0 0 200 0
sharpen=
expect "sharpening doesn't touch a frame that isn't upscaled" \
  "$(count 'macneutron-present: MetalFX' sharp_pass):$(count 'sharpen' sharp_pass)" "0:0"
rm -f "$WORK/sharp_off.ppm" "$WORK/sharp_on.ppm"
native sharp_off MACNEUTRON_PRESENT_SCALE=2 MACNEUTRON_PRESENT_DUMP="$WORK/sharp_off.ppm" "$B/cmaa2_check" "$LIB" upscale
native sharp_shot MACNEUTRON_PRESENT_SCALE=2 MACNEUTRON_SHARPEN=0.8 MACNEUTRON_PRESENT_DUMP="$WORK/sharp_on.ppm" \
  "$B/cmaa2_check" "$LIB" upscale
sharp_edge_off=$(edge "$WORK/sharp_off.ppm"); sharp_edge_on=$(edge "$WORK/sharp_on.ppm")
expect "sharpening hardens the upscaled frames' edges" \
  "$( [ "$sharp_edge_on" -gt "$sharp_edge_off" ] && echo up || echo down)" "up"
echo "info sharpening edges: $sharp_edge_off -> $sharp_edge_on"
statuses="$statuses$(cat "$WORK/sharp_off.status" "$WORK/sharp_shot.status" | tr -d '\n')"

run_loop retina 1 2 1280 720 0 0 200 0
expect "Retina density is upscaled" "$(count 'macneutron-present: MetalFX 1280x720 -> 2560x1440' retina)" 1

run_loop resize 1 1 1280 720 640 360 300 0 resize=150:960x540
expect "overlay follows a window resize" "$(count 'macneutron-present: MetalFX 640x360 -> 960x540' resize)" 1

# A size MetalFX refuses falls back to Core Animation's linear filter for that size only, asked once: the next size
# upscales again. A refusal whatever the size (no MetalFX on the GPU, a pixel format it can't scale) lasts.
refuse=1280x720
run_loop refuse 1 1 1280 720 640 360 300 0 resize=150:960x540
expect "a MetalFX refusal falls back for that size only, asked once" \
  "$(count 'macneutron-present: linear filter (refused for the test)' refuse):$(count 'macneutron-present: MetalFX 640x360 -> 960x540' refuse):$(count 'macneutron-present: test refusal at 1280x720' refuse)" "1:1:1"
refuse=all
run_loop refuseall 1 1 1280 720 640 360 300 0 resize=150:960x540
refuse=
expect "a refusal whatever the size lasts for the layer, asked once" \
  "$(count 'macneutron-present: linear filter (refused for the test)' refuseall):$(count 'macneutron-present: MetalFX' refuseall):$(count 'macneutron-present: test refusal' refuseall)" "1:0:1"

run_loop grow 1 1 1280 720 640 360 300 0 grow=150
expect "overlay goes away at full size" "$(count 'macneutron-present: pass-through (full size)' grow)" 1

run_loop hdr 1 1 1280 720 640 360 200 0 fp16
expect "HDR layers are left alone" \
  "$(count 'left alone (HDR/extended-range layer)' hdr):$(count 'macneutron-present: MetalFX' hdr)" "1:0"

run_loop vsync 1 1 1280 720 640 360 200 1
expect "vsync-on game is upscaled" "$(count 'macneutron-present: MetalFX 640x360 -> 1280x720' vsync)" 1

rm -f "$WORK/frame.ppm"
run_loop vswitch 1 1 1280 720 640 360 300 0 vsync_at=60:1
expect "switching vsync on keeps upscaling" \
  "$( [ -f "$WORK/frame.ppm" ] && python3 "$ROOT/presenter/tests/pixels.py" "$WORK/frame.ppm" || echo none)" "WNWNWNWN"

run_loop format 1 1 1280 720 640 360 300 0 format_at=100
expect "a pixel-format switch rebuilds the overlay" \
  "$(count 'macneutron-present: MetalFX 640x360 -> 1280x720' format):$(count 'avg frame' format)" "2:1"

# CMAA2 in Wine: at full size, before MetalFX, with MetalFX off (the launcher still loads the presenter), never on HDR.
aa=cmaa2
run_loop aa 1 1 1280 720 0 0 200 0
expect "CMAA2 runs at full size" "$(count 'macneutron-present: CMAA2 1280x720' aa):$(count 'macneutron-present: MetalFX' aa)" "1:0"
rm -f "$WORK/frame.ppm"
run_loop aaup 1 1 1280 720 640 360 300 0
expect "CMAA2 runs before MetalFX" "$(LC_ALL=C /usr/bin/grep -o 'macneutron-present: [CM][A-Za-z0-9]*' "$WORK/aaup.txt" \
  | head -n 2 | tr '\n' ' ')" "macneutron-present: CMAA2 macneutron-present: MetalFX "
expect "upscaled anti-aliased frame shows the whole checkerboard" \
  "$( [ -f "$WORK/frame.ppm" ] && python3 "$ROOT/presenter/tests/pixels.py" "$WORK/frame.ppm" || echo none)" "WNWNWNWN"
run_loop aanofx 0 1 1280 720 640 360 200 0
expect "MetalFX off keeps CMAA2 and skips only the upscale" \
  "$(count 'macneutron-present: CMAA2 640x360' aanofx):$(count 'macneutron-present: MetalFX' aanofx)" "1:0"
run_loop aahdr 1 1 1280 720 640 360 200 0 fp16
expect "CMAA2 leaves HDR layers alone in Wine" \
  "$(count 'left alone (HDR/extended-range layer)' aahdr):$(count 'macneutron-present: CMAA2' aahdr)" "1:0"
aa=

run_loop base 0 1 1280 720 640 360 600 0
expect "MACNEUTRON_NO_METALFX=1 loads no presenter" "$(count 'macneutron-present' base)" 0
run_loop pace 1 1 1280 720 640 360 600 0
expect "pacing within 1 ms of no library" \
  "$(awk -v a="$(frame_ms pace)" -v b="$(frame_ms base)" 'BEGIN { print (a != "" && b != "" && a <= b + 1.0) ? "yes" : "no (" a " vs " b " ms)" }')" "yes"

exit $fail
