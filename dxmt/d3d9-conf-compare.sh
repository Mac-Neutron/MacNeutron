#!/bin/sh
# D3D9 conformance parity per site (C3d-5): two runner outputs of the same leg(s), the msl lane's and the default
# lane's, each stamped on its first line by dxmt/mtld3d-conformance.sh or vm/tests/d3d9.sh. Fails when, under the default
# lane (AIR), a site fails more often, a suite crashes more, executes over 0.1% fewer tests or skips a site the msl
# lane measured; when a suite is missing from either file (each leg needs stateblock, visual, device, d3d9ex); when a
# file is cut short (a suite's current failures differ from its baseline plus its listed sites' changes, or no closing
# "conformance:" line); or when the stamps are not msl then default. Information, not failures: a site the runner calls
# flaky in either file, a worse site listed in <triaged> for that leg and subtest (default dxmt/mtld3d-triaged.txt), a
# suite that crashed in both ("partial": its counts cover only the failures before the crash). A site a file doesn't
# list failed there as often as in the baseline (the runner lists every site whose count changed).
#   d3d9-conf-compare.sh <msl.txt> <default.txt> [<triaged.txt>]
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
[ $# -ge 2 ] || { echo "usage: d3d9-conf-compare.sh <msl.txt> <default.txt> [<triaged.txt>]" >&2; exit 2; }
MSL=$1 AIR=$2 TRI=${3:-$ROOT/dxmt/mtld3d-triaged.txt}
bad=
case $(head -1 "$MSL" 2> /dev/null) in *": lane msl"*) ;; *) bad=1; echo "stamp $MSL: not the msl lane" ;; esac
case $(head -1 "$AIR" 2> /dev/null) in *": lane default"*) ;; *) bad=1; echo "stamp $AIR: not the default lane" ;; esac
if [ "$MSL" = "$AIR" ] || cmp -s "$MSL" "$AIR"; then bad=1; echo "stamp $AIR: the same run as $MSL"; fi
[ -z "$bad" ] || exit 1
awk -v tri="$TRI" '
  BEGIN { while ((getline l < tri) > 0) if (l !~ /^#/ && l != "") { split(l, f, " "); triaged[f[1] " " f[2]] = 1 }
          nsuite = split("stateblock visual device d3d9ex", want, " ") }
  FNR == 1 { lane = (FILENAME == ARGV[1]) ? "m" : "a"; su = ""; next }
  /^conformance: / { closed[lane] = 1; su = ""; next }
  /^[a-z0-9_+@-]+\/[a-z0-9]+  +baseline\(/ {
    su = $1; split(su, p, "/"); legs[p[1]] = 1; suites[su] = 1; has[lane, su] = 1
    line = $0
    sub(/.*baseline\(failed=/, "", line); B = line + 0; sub(/^[0-9]+ crash=/, "", line); BC = line + 0
    line = $0; sub(/.*current\(failed=/, "", line); F = line + 0; sub(/^[0-9]+ crash=/, "", line); K = line + 0
    line = $0; E = -1; if (sub(/.* executed=/, "", line)) E = line + 0
    base[lane, su] = B; cur[lane, su] = F; crash[lane, su] = K; exe[lane, su] = E; delta[lane, su] = 0
    next
  }
  /^  [a-z0-9_]+\.c:[0-9]+  / {
    if (su == "") next
    s = $1; sites[su " " s] = 1
    if ($2 == "SKIPPED") {  # the runner leaves a skipped site out of current(failed=): its retained count too
      skipped[lane, su, s] = 1; n = $0; if (sub(/.*baseline count /, "", n)) delta[lane, su] -= n + 0
      next
    }
    if ($3 != "->") next  # an untriaged count without a change: as the baseline
    sbase[su, s] = $2; scur[lane, su, s] = $4; listed[lane, su, s] = 1; delta[lane, su] += $4 - $2
    if ($0 ~ /flaky/) flaky[su, s] = 1
    next
  }
  END {
    fails = 0; info = 0; nsites = 0; nsu = 0; nlegs = 0
    for (lg in legs) {
      nlegs++
      for (i = 1; i <= nsuite; i++) suites[lg "/" want[i]] = 1
    }
    for (su in suites) {
      nsu++
      if (!has["m", su] || !has["a", su]) { print "missing " su; fails++; continue }
      for (ln = 0; ln < 2; ln++) {
        L = ln ? "a" : "m"
        if (!closed[L] || cur[L, su] != base[L, su] + delta[L, su]) { tr[su] = 1 }
      }
      if (su in tr) { print "truncated " su; fails++; continue }
      both = crash["m", su] > 0 && crash["a", su] > 0
      if (crash["a", su] > crash["m", su]) { print "air-crash " su; fails++ }
      else if (both) { print "partial " su; info++ }
      if (!both && exe["m", su] > 0 && exe["a", su] * 1000 < exe["m", su] * 999) {
        print "air-executed " su " " exe["m", su] " -> " exe["a", su]; fails++
      }
    }
    for (k in sites) {
      split(k, q, " "); su = q[1]; s = q[2]
      if (su in tr || !has["m", su] || !has["a", su]) continue
      nsites++
      if (skipped["a", su, s] && !skipped["m", su, s]) { print "air-skipped " su " " s; fails++; continue }
      if (skipped["m", su, s] || skipped["a", su, s]) continue
      b = sbase[su, s]
      m = listed["m", su, s] ? scur["m", su, s] : b
      a = listed["a", su, s] ? scur["a", su, s] : b
      if ((su, s) in flaky) { print "info flaky " su " " s; info++; continue }
      if (a > m) {
        if ((su " " s) in triaged) { print "info triaged " su " " s " " m " -> " a; info++ }
        else { print "air-worse " su " " s " " m " -> " a; fails++ }
      }
    }
    if (!nlegs) { print "missing every suite: no leg in either file"; fails++ }
    if (fails) exit 1
    printf "conf-compare: ok (%d legs, %d suites, %d sites, %d info)\n", nlegs, nsu, nsites, info
  }
' "$MSL" "$AIR"
