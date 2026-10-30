#!/bin/sh
# wm-probe-check.sh: winemetal's unix-call slots on one image against wm_probe's goldens (DXMTRS step 2; the probe is
# dxmt/tests/wm_probe/probe.h's).
#   sh dxmt/wm-probe-check.sh [--dir <host-dir> | --so <winemetal.so> | --native <dylib>] [--goldens <dir>]
#                             [--full] [<group>...]
# The image: --dir, a folder holding winemetal.so beside the VM host's stand-ins; --so, that .so beside fresh
# stand-ins; --native, the native winemetal.dylib (wm_probe native); by default build/wine-arm64/wine.app's
# winemetal.so beside fresh stand-ins. Goldens: --goldens (default dxmt/tests/wm_probe, the committed ones).
# One wm_probe process per group (g0-g10, or those named): `fixed` against <group>.fixed (<group>.native with
# --native); with --full, `full` against <group>.full, where each "<case> <key>" line of <group>.mask makes that key
# of that case's lines read <var> on both sides, and must be present. The lines of a WM_NOT_ON_C case the golden holds
# none of are skipped. A missing golden, or one whose case set isn't wm_probe --list's for that group and mode, fails.
# WM_PROBE=<path>: that probe, never built; else `make wm-probe` into this checkout's build/wm-probe first (DXMT_SRC,
# default the main checkout's build/wine-arm64-src/dxmt/src). wine.app and DXMT are the main checkout's build, also
# from a worktree.
# Prints "ok   wm-probe <group>", "FAIL wm-probe <group>: <first differing line>" (+ the probe's, - the golden's) or
# "FAIL wm-probe <group>: crashed (signal <n>)"; exits 1 on a FAIL.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
# build/ lives in the main checkout (a worktree has none).
MAIN=$(git -C "$ROOT" rev-parse --path-format=absolute --git-common-dir 2> /dev/null) && MAIN=${MAIN%/.git} || MAIN=$ROOT
GOLDENS=$ROOT/dxmt/tests/wm_probe
dir= so= native= full= groups=
while [ $# -gt 0 ]; do
  case $1 in
  --dir) dir=$2; shift 2 ;;
  --so) so=$2; shift 2 ;;
  --native) native=$2; shift 2 ;;
  --goldens) GOLDENS=$2; shift 2 ;;
  --full) full=1; shift ;;
  g[0-9] | g10) groups="$groups $1"; shift ;;
  *) echo "usage: wm-probe-check.sh [--dir <host-dir> | --so <winemetal.so> | --native <dylib>] [--goldens <dir>]" \
       "[--full] [<group>...]" >&2; exit 2 ;;
  esac
done
groups=${groups:-g0 g1 g2 g3 g4 g5 g6 g7 g8 g9 g10}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/wm-probe-check.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT

if [ -n "${WM_PROBE:-}" ]; then
  P=$WM_PROBE
else
  make -s -C "$ROOT" wm-probe DXMT_SRC="${DXMT_SRC:-$MAIN/build/wine-arm64-src/dxmt/src}" \
    WM_PROBE_OUT="$ROOT/build/wm-probe" || exit 2
  P=$ROOT/build/wm-probe/wm_probe
fi
if [ -n "$native" ]; then
  set -- native "$native"
elif [ -n "$dir" ]; then
  set -- "$dir"
else
  so=${so:-$MAIN/build/wine-arm64/wine.app/Contents/Resources/lib/wine/aarch64-unix/winemetal.so}
  make -s -C "$ROOT/eac-vm/metal/wm" metal-host HOST="$TMP/host" WINEMETAL="$so" || exit 2
  set -- "$TMP/host"
fi
mode=fixed ext=fixed
[ -n "$native" ] && ext=native
[ -n "$full" ] && mode=full ext=full
"$P" --list > "$TMP/list" || { echo "FAIL wm-probe: $P --list exited $?"; exit 1; }

# mask <mask-file> <lines-file> <report-missing>: the masked keys read <var>; with 1, each missing key on stderr.
mask() {
  perl -e '
    my ($maskf, $file, $check) = @ARGV;
    my @m;
    if (open my $mf, "<", $maskf) { while (<$mf>) { chomp; next if /^\s*(#|$)/; push @m, [split " ", $_, 2] } }
    my %seen;
    open my $f, "<", $file or die;
    while (my $l = <$f>) {
      my $case = (split / /, $l)[2] // "";
      for my $m (@m) {
        next unless $case eq $m->[0];
        my $k = quotemeta $m->[1];
        $seen{"$m->[0] $m->[1]"} = 1 if $l =~ s/(^| )$k=("(?:[^"\\]|\\.)*"|[^ \n]*)/$1$m->[1]=<var>/g;
      }
      print $l;
    }
    if ($check) { for my $m (@m) { print STDERR "$m->[0] $m->[1]\n" unless $seen{"$m->[0] $m->[1]"} } }
  ' "$1" "$2" "$3"
}

fails=0
fail() { echo "FAIL wm-probe $1: $2"; fails=$((fails + 1)); }
for g in $groups; do
  golden=$GOLDENS/$g.$ext
  [ -f "$golden" ] || { fail "$g" "no golden $golden"; continue; }
  # The cases this mode runs; a WM_NOT_ON_C one counts only once the golden holds a line of it.
  awk -v g="$g" -v native="$native" -v full="$full" -v golden="$golden" '
    BEGIN { while ((getline l < golden) > 0) { split(l, f, " "); held[f[3]] = 1 } }
    $1 != g { next }
    native != "" && $4 ~ /wine-only/ { next }
    native == "" && $4 ~ /(^|,)native(,|$)/ { next }
    full == "" && $4 ~ /full-only/ { next }
    $4 ~ /not-on-c/ && !held[$3] { print $3 > "/dev/stderr"; next }
    { print $3 }' "$TMP/list" 2> "$TMP/skip" | sort -u > "$TMP/want"
  awk '{ print $3 }' "$golden" | sort -u > "$TMP/held"
  d=$(diff "$TMP/want" "$TMP/held" | awk '/^</ { print "golden lacks case " $2; exit }
    /^>/ { print "golden has case " $2 " not in --list"; exit }')
  [ -z "$d" ] || { fail "$g" "$d"; continue; }
  ("$P" "$@" $mode "$g" > "$TMP/raw" 2> "$TMP/err") 2> /dev/null
  st=$?
  if [ $st -gt 128 ]; then fail "$g" "crashed (signal $((st - 128)))"; continue; fi
  if [ $st != 0 ]; then fail "$g" "exit $st: $(tail -1 "$TMP/err")"; continue; fi
  awk -v skip="$TMP/skip" 'BEGIN { while ((getline c < skip) > 0) drop[c] = 1 } !($3 in drop)' "$TMP/raw" > "$TMP/out"
  if [ -n "$full" ]; then
    mask "$GOLDENS/$g.mask" "$TMP/out" 1 > "$TMP/out.m" 2> "$TMP/missing"
    mask "$GOLDENS/$g.mask" "$golden" 0 > "$TMP/golden.m"
    if [ -s "$TMP/missing" ]; then fail "$g" "masked key missing: $(head -1 "$TMP/missing")"; continue; fi
  else
    cp "$TMP/out" "$TMP/out.m"
    cp "$golden" "$TMP/golden.m"
  fi
  d=$(diff "$TMP/golden.m" "$TMP/out.m" | awk '/^</ { print "-" substr($0, 3); exit } /^>/ { print "+" substr($0, 3); exit }')
  if [ -n "$d" ]; then fail "$g" "$d"; else echo "ok   wm-probe $g"; fi
done
[ "$fails" = 0 ]
