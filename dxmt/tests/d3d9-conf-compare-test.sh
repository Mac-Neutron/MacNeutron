#!/bin/sh
# dxmt/d3d9-conf-compare.sh's cases (C3d plan Task 3), on two stamped copies of a real arm64x run (msl.txt, default.txt
# in d3d9-conf-compare/) and variants made from them here. Prints "d3d9-conf-compare-test: ok" or each case that failed.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
C="$ROOT/dxmt/d3d9-conf-compare.sh" F="$ROOT/dxmt/tests/d3d9-conf-compare"
X=$(mktemp -d "${TMPDIR:-/tmp}/d3d9-conf-compare-test.XXXXXX")
trap 'rm -rf "$X"' EXIT
M=$F/msl.txt D=$F/default.txt
fail=
# t <case> <exit> <pattern> <msl> <default> [triaged]: the comparator's exit status and a line matching <pattern>.
t() {
  out=$(sh "$C" "$4" "$5" ${6:+"$6"} 2>&1); st=$?
  if [ "$st" = "$2" ] && printf '%s\n' "$out" | /usr/bin/grep -qE "$3"; then return; fi
  echo "d3d9-conf-compare-test: $1: exit $st, want $2 and /$3/:"; printf '%s\n' "$out" | sed 's/^/  /'
  fail=1
}
v() { sed "$2" "$1"; }  # v <file> <sed script>: a variant on stdout
t real 0 '^conf-compare: ok \(1 legs, 4 suites' "$M" "$D"
t better-and-flaky 0 '^info flaky arm64x/device device\.c:4471' "$M" "$D"
v "$D" 's|visual.c:15686  15 -> 3|visual.c:15686  15 -> 4|; s|current(failed=30 crash=0) executed=211219|current(failed=31 crash=0) executed=211219|' > "$X/worse"
t air-worse 1 '^air-worse arm64x/visual visual\.c:15686 3 -> 4$' "$M" "$X/worse"
v "$M" 's|current(failed=740 crash=0)|current(failed=739 crash=0)|; /device.c:4476/a\
  device.c:4471  1 -> 0  flaky (count down, tolerated)' > "$X/msl-flaky"
v "$D" '/device.c:4471/d; s|current(failed=739 crash=0)|current(failed=740 crash=0)|' > "$X/air-noflaky"
t flaky-in-msl 0 '^info flaky arm64x/device device\.c:4471' "$X/msl-flaky" "$X/air-noflaky"
v "$M" '/device.c:14544/d; s|current(failed=740 crash=0)|current(failed=739 crash=0)|' > "$X/msl-no14544"
t triaged 0 '^info triaged arm64x/device device\.c:14544' "$X/msl-no14544" "$D"
echo "i686-arm64/device device.c:14544 another leg's entry" > "$X/triaged-other"
t triaged-other-leg 1 '^air-worse arm64x/device device\.c:14544 0 -> 1$' "$X/msl-no14544" "$D" "$X/triaged-other"
v "$D" 's|current(failed=739 crash=0)|current(failed=739 crash=1)|' > "$X/crash"
t air-crash 1 '^air-crash arm64x/device$' "$M" "$X/crash"
v "$D" 's|current(failed=30 crash=0) executed=211219|current(failed=30 crash=0) executed=209000|' > "$X/executed"
t air-executed 1 '^air-executed arm64x/visual 211219 -> 209000$' "$M" "$X/executed"
v "$D" '/^arm64x\/visual/q' > "$X/cut"
t truncated 1 '^truncated arm64x/visual' "$M" "$X/cut"
v "$D" '/visual.c:16481/d' > "$X/lost-site"
t lost-site 1 '^truncated arm64x/visual$' "$M" "$X/lost-site"
v "$D" '/^conformance: /d' > "$X/unclosed"
t unclosed 1 '^truncated arm64x' "$M" "$X/unclosed"
v "$D" '1q' > "$X/empty"
t empty 1 '^missing arm64x/device' "$M" "$X/empty"
v "$M" '/^arm64x\/d3d9ex/d' > "$X/msl-no9ex"
v "$D" '/^arm64x\/d3d9ex/d' > "$X/air-no9ex"
t no-d3d9ex-in-both 1 '^missing arm64x/d3d9ex' "$X/msl-no9ex" "$X/air-no9ex"
v "$D" 's|^  visual.c:15686  15 -> 3  ceiling.*|  visual.c:15686  SKIPPED (baseline count 15 retained): no adapter|; s|current(failed=30 crash=0) executed=211219|current(failed=42 crash=0) executed=211219|' > "$X/skipped"
t air-skipped 1 '^air-skipped arm64x/visual visual\.c:15686$' "$M" "$X/skipped"
v "$M" 's|current(failed=740 crash=0)|current(failed=740 crash=1)|' > "$X/msl-crash"
t partial 0 '^partial arm64x/device$' "$X/msl-crash" "$X/crash"
t swapped 1 '^stamp ' "$D" "$M"
t same-file 1 '^stamp ' "$M" "$M"
cp "$M" "$X/msl-copy"
t same-content 1 '^stamp ' "$M" "$X/msl-copy"
[ -z "$fail" ] && echo "d3d9-conf-compare-test: ok" || { echo "d3d9-conf-compare-test: FAIL"; exit 1; }
