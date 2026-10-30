#!/bin/sh
# cover.sh: wm_probe's coverage checks (DXMTRS step 2: winemetal's slots, each pinned by a probe case on the C).
#   slots                                  slots.txt names each of 0-157 once, in groups of the plan's sizes
#   table <host-dir> <image>               the image's unix-call tables, named by `nm -a <image>`, equal slots.txt
#                                          (the WoW64 table: entries 0-152, thunk32_ where the Wine build has one)
#   cases <wm_probe> <host-dir> [<group>]  every slot has a case and is called; a case per command type (g3:
#                                          blit-<Name>, e.g. blit-CopyFromBufferToBuffer, blit-Nop); no cross call
#                                          but g0's aniso-env
#   bites <wm_probe> <host-dir> <group>    each slot stubbed alone (WM_STUB) changes a line naming it
#   ported <dxmtrs-tree> <libwinemetal.a> <group...>
#                                          those groups are Rust only, every other slot defined once (C or Rust),
#                                          and the archive defines none of what the link provides
# Goldens: WM_GOLDENS (default: this folder). The C objects of `ported`: WM_COVER_OBJ (default: the archive's
# folder's obj/, where $S/lane-so.sh compiles the tree's C). winemetal.h: DXMT_SRC/winemetal (default the main
# checkout's DXMT). wm_probe for `table`: WM_PROBE (default the main checkout's build/wm-probe/wm_probe).
# Exit 0 with "ok cover <check>", else 1 with each "FAIL cover <check>: <why>".
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
SLOTS=$HERE/slots.txt
ROOT=$(cd "$HERE/../../.." && pwd)
# build/ lives in the main checkout (a worktree has none).
MAIN=$(git -C "$HERE" rev-parse --path-format=absolute --git-common-dir 2> /dev/null) && MAIN=${MAIN%/.git} || MAIN=$ROOT
GOLDENS=${WM_GOLDENS:-$HERE}
DXMT_SRC=${DXMT_SRC:-$MAIN/build/wine-arm64-src/dxmt/src}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/wm-cover.XXXXXX") || exit 1
trap 'rm -rf "$TMP"' EXIT
fails=0
fail() { echo "FAIL cover $*"; fails=$((fails + 1)); }
done_() { if [ "$fails" = 0 ]; then echo "ok   cover $1"; exit 0; fi; exit 1; }
usage() { echo "usage: cover.sh slots | table <host-dir> <image> | cases <wm_probe> <host-dir> [<group>] |" \
  "bites <wm_probe> <host-dir> <group> | ported <dxmtrs-tree> <libwinemetal.a> <group...>" >&2; exit 2; }
need_slots() { [ -f "$SLOTS" ] || { echo "FAIL cover $1: $SLOTS is missing"; exit 1; }; }
# The slots of a group: "<slot> <symbol>" lines.
group_slots() { awk -v g="$1" '$2 == g { print $1, $3 }' "$SLOTS"; }

case ${1:-} in
slots)
  [ $# = 1 ] || usage
  need_slots slots
  awk '
    NF != 3 || $1 !~ /^[0-9]+$/ || $2 !~ /^g([0-9]|10)$/ { print "line " NR " is not <slot> <group> <symbol>: " $0; next }
    { if (seen[$1]++) print "slot " $1 " twice"; n[$2]++; if ($1 == 83 && $3 != "-") print "slot 83 has a symbol" }
    $1 != 83 && $3 == "-" { print "slot " $1 " has no symbol" }
    END {
      for (s = 0; s < 158; s++) if (!seen[s]) print "slot " s " missing"
      if (NR != 158) print NR " lines, not 158"
      split("4 26 28 24 12 9 19 16 5 3 12", want, " ")
      for (g = 0; g <= 10; g++) if (n["g" g] + 0 != want[g + 1]) print "g" g " has " n["g" g] + 0 " slots, not " want[g + 1]
    }' "$SLOTS" > "$TMP/why"
  while IFS= read -r l; do fail "slots: $l"; done < "$TMP/why"
  done_ slots
  ;;

table)
  [ $# = 3 ] || usage
  need_slots table
  P=${WM_PROBE:-$MAIN/build/wm-probe/wm_probe}
  "$P" "$2" table > "$TMP/table" || { echo "FAIL cover table: wm_probe $2 table exited $?"; exit 1; }
  # Text symbols by address, as lowercase hex without leading zeros (wm_probe's form); nm prints the Mach-O name, the
  # C name with one more leading underscore.
  nm -a "$3" | awk '$2 ~ /^[tT]$/ && NF == 3 { a = tolower($1); sub(/^0+/, "", a); print (a == "" ? "0" : a), substr($3, 2) }' \
    > "$TMP/nm" || exit 1
  awk -v slots="$SLOTS" -v summary="$TMP/summary" '
    BEGIN {
      while ((getline l < slots) > 0) { split(l, f, " "); want[f[1]] = f[3] }
      split("74 76 77 79 81 82 84 85 88", t32, " "); for (i in t32) thunk32[t32[i]] = 1
    }
    FNR == NR { names[$1] = names[$1] " " $2 " "; next }
    function check(kind, slot, off, w,   got) {
      got = off == "null" ? " null " : (off in names ? names[off] : " " off "(unnamed) ")
      if (w == "-") { if (off != "null") print kind " slot " slot " is" got "not null"; return }
      if (index(got, " " w " ") == 0) print kind " slot " slot " is" got "not " w
    }
    $1 == "table" { n++; check("table", $2, $3, want[$2]) }
    $1 == "wow64" && $2 > 152 { next }
    $1 == "wow64" {
      w64++
      w = want[$2]; if ($2 in thunk32) sub(/^thunk_/, "thunk32_", w)
      if ($2 > 152) print "wow64 slot " $2 " past 152"; else check("wow64", $2, $3, w)
    }
    END {
      if (n != 158) print "table has " n + 0 " entries, not 158"
      if (w64 && w64 != 153) print "wow64 table has " w64 " entries, not 153"
      print n + 0 " entries" (w64 ? ", wow64 " w64 : ", no wow64 table") > summary
    }' "$TMP/nm" "$TMP/table" > "$TMP/why"
  while IFS= read -r l; do fail "table: $l"; done < "$TMP/why"
  [ "$fails" = 0 ] && echo "     table: $(cat "$TMP/summary")"
  done_ table
  ;;

cases)
  [ $# = 3 ] || [ $# = 4 ] || usage
  need_slots cases
  P=$2 D=$3 G=${4:-}
  "$P" --list > "$TMP/list" || { echo "FAIL cover cases: $P --list exited $?"; exit 1; }
  "$P" "$D" fixed $G --counts > "$TMP/out" 2> "$TMP/err"
  st=$?; [ $st = 0 ] || fail "cases: wm_probe exited $st: $(tail -1 "$TMP/err")"
  for g in ${G:-g0 g1 g2 g3 g4 g5 g6 g7 g8 g9 g10}; do
    group_slots "$g" | while read -r s sym; do
      if [ "$sym" = - ]; then
        awk -v g="$g" -v s="$s" '$1 == g && $2 == s { f = 1 } END { exit !f }' "$TMP/list" ||
          echo "$g slot $s (null) has no case"
      else
        awk -v g="$g" -v s="$s" '$1 == g && $2 == s && $4 == "calls" && $5 > 0 { f = 1 } END { exit !f }' "$TMP/out" ||
          echo "$g slot $s is never called"
      fi
    done
  done > "$TMP/why"
  # One case per command type (g3): blit-, compute-, render-<the enum name without WMT<Kind>Command>, Nop included
  # (Unused0-2 have no such name: 14 + 14 + 47).
  if [ -z "$G" ] || [ "$G" = g3 ]; then
    [ -f "$DXMT_SRC/winemetal/winemetal.h" ] || { echo "FAIL cover cases: no $DXMT_SRC/winemetal/winemetal.h"; exit 1; }
    for k in Blit Compute Render; do
      awk -v k="$k" '
        $0 ~ "^enum WMT" k "CommandType" { on = 1; next }
        on && /^};/ { exit }
        on { gsub(/[ ,]/, ""); if ($0 ~ "^WMT" k "Command") { sub("^WMT" k "Command", ""); print tolower(k) "-" $0 } }
      ' "$DXMT_SRC/winemetal/winemetal.h"
    done | while read -r c; do
      awk -v c="$c" '$1 == "g3" && $3 == c { f = 1 } END { exit !f }' "$TMP/list" || echo "no case $c"
    done >> "$TMP/why"
  fi
  awk '$4 == "cross" && !($1 == "g0" && $3 == "aniso-env" && $5 == 19) { print "cross call: " $0 }' "$TMP/out" >> "$TMP/why"
  while IFS= read -r l; do fail "cases: $l"; done < "$TMP/why"
  done_ "cases${G:+ $G}"
  ;;

bites)
  [ $# = 4 ] || usage
  need_slots bites
  P=$2 D=$3 G=$4
  [ -f "$GOLDENS/$G.fixed" ] || { echo "FAIL cover bites: no golden $GOLDENS/$G.fixed"; exit 1; }
  "$P" --list > "$TMP/list" || exit 1
  # The lines of a WM_NOT_ON_C case the golden doesn't hold yet are not compared (as wm-probe-check.sh).
  awk -v g="$G" '$1 == g && $4 ~ /not-on-c/ { print $3 }' "$TMP/list" | while read -r c; do
    awk -v c="$c" '$3 == c { f = 1 } END { exit !f }' "$GOLDENS/$G.fixed" || echo "$c"
  done > "$TMP/skip"
  group_slots "$G" | while read -r s sym; do
    [ "$sym" = - ] && continue
    WM_STUB=$s "$P" "$D" fixed "$G" 2> /dev/null | awk -v skip="$TMP/skip" '
      BEGIN { while ((getline c < skip) > 0) drop[c] = 1 } !($3 in drop)' > "$TMP/stub"
    diff "$GOLDENS/$G.fixed" "$TMP/stub" | awk -v s="$s" '/^[<>] / && $3 == s { f = 1 } END { exit !f }' ||
      echo "slot $s ($sym) stubbed changes no line naming it"
  done > "$TMP/why"
  while IFS= read -r l; do fail "bites $G: $l"; done < "$TMP/why"
  done_ "bites $G"
  ;;

ported)
  [ $# -ge 4 ] || usage
  need_slots ported
  T=$2 A=$3; shift 3
  O=${WM_COVER_OBJ:-$(dirname "$A")/obj}
  [ -f "$A" ] || { echo "FAIL cover ported: no archive $A"; exit 1; }
  # The C objects must be the tree's current C: one per src/winemetal/unix/*.c, newer than it.
  for c in "$T"/src/winemetal/unix/*.c; do
    o=$O/$(basename "${c%.c}").o
    { [ -f "$o" ] && [ "$o" -nt "$c" ]; } || fail "ported: $o is missing or older than $c (run lane-so.sh first)"
  done
  for o in "$O"/*.o; do
    [ -f "$T/src/winemetal/unix/$(basename "${o%.o}").c" ] || fail "ported: $o has no source in $T"
  done
  [ "$fails" = 0 ] || exit 1
  nm "$O"/*.o 2> /dev/null | awk 'NF >= 2 && $(NF-1) ~ /^[tT]$/ { print substr($NF, 2) }' | sort > "$TMP/c-defs"
  nm "$A" 2> /dev/null | awk 'NF >= 2 && $(NF-1) ~ /^[tT]$/ { print substr($NF, 2) }' | sort > "$TMP/rs-defs"
  nm "$A" 2> /dev/null | awk 'NF >= 2 && $(NF-1) ~ /^[A-TV-Za-tv-z]$/ { print $NF }' |
    /usr/bin/grep -E '^(_NtSetEvent|_SM50.*|_thunk32_.*|_macdrv_functions|_sqlite3_.*|_wmt_native_.*)$' |
    sort -u > "$TMP/stubs"
  while IFS= read -r l; do fail "ported: the archive defines $l (the link provides it)"; done < "$TMP/stubs"
  for g in "$@"; do awk -v g="$g" '$2 == g' "$SLOTS" | /usr/bin/grep -q . || fail "ported: no group $g"; done
  awk -v named=" $* " -v cdefs="$TMP/c-defs" -v rdefs="$TMP/rs-defs" '
    BEGIN {
      while ((getline l < cdefs) > 0) c[l]++
      while ((getline l < rdefs) > 0) r[l]++
    }
    $3 == "-" || seen[$3]++ { next }
    {
      s = $3; ported = index(named, " " $2 " ") > 0
      if (ported && !r[s]) print $2 " slot " $1 ": " s " is not defined in the archive"
      if (ported && c[s]) print $2 " slot " $1 ": " s " is still defined in the C"
      if (c[s] + r[s] != 1) print $2 " slot " $1 ": " s " is defined " c[s] + r[s] " times (C " c[s] + 0 ", Rust " r[s] + 0 ")"
    }' "$SLOTS" > "$TMP/why"
  while IFS= read -r l; do fail "ported: $l"; done < "$TMP/why"
  done_ "ported $*"
  ;;

*) usage ;;
esac
