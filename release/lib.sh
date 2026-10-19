# Shared by the release scripts (sourced; they run under set -eu). Messages go to stderr.
set -eu
die() { echo "release: $*" >&2; exit 1; }

# The p build's compile-out probes (spec 2026-10-19-macneutron-performance-build §8): a format string only a TRACE in
# Wine's dlls/ntdll/unix/virtual.c uses (in both ntdll.so builds today, gone from a p build's).
WINE_TRACE_PROBE='Found free area is already mapped, start %p.'
# A string only a DEBUG in DXMT's d3d12_device.cpp logs (in native d3d12.dylib today, gone from a p build's).
DXMT_DEBUG_PROBE='GetResourceAllocationInfo: invalid texture descriptor'

# syspolicy_check <mode> <bundle>, its report in <work-dir>/syspolicy-<mode>.txt. Fails on any finding that isn't a
# warning, or on a failure with no findings (a tool error). Warnings are counted, not fatal: wine.app keeps its
# Mach-Os under Resources/ (Wine's layout), which the check flags as warnings; Apple's verdict decides.
# One exception, before submission only: an app that nests code Apple has already notarized (MacNeutron.app around the
# notarized wine.app) gets a lone "Gatekeeper rejected this file" on its main executable while spctl gives the plain
# pre-notarization verdict (with an un-notarized wine.app swapped in, the finding goes away). That one is expected;
# the notary service decides, and `syspolicy distribution` after stapling is the gate.
syspolicy() {  # syspolicy <mode> <bundle> <work-dir>
  report="$3/syspolicy-$1.txt"
  "${SYSPOLICY_CHECK:-/usr/bin/syspolicy_check}" "$1" "$2" > "$report" 2>&1 && return 0
  errors=$(LC_ALL=C /usr/bin/grep 'Severity:' "$report" | LC_ALL=C /usr/bin/grep -cv 'Severity: Warning' || true)
  warnings=$(LC_ALL=C /usr/bin/grep -c 'Severity: Warning' "$report" || true)
  if [ "$1" = notary-submission ] && [ "$errors" -eq 1 ] \
    && LC_ALL=C /usr/bin/grep -q 'Full Error: Gatekeeper rejected this file' "$report" \
    && spctl -a -vvv -t exec "$2" 2>&1 | LC_ALL=C /usr/bin/grep -qx 'source=Unnotarized Developer ID'; then
    echo "syspolicy_check $1: $warnings warnings, and Gatekeeper's pre-notarization rejection (nested code already" \
      "notarized): $report"
    return 0
  fi
  if [ "$errors" -gt 0 ] || [ "$warnings" -eq 0 ]; then cat "$report" >&2; die "syspolicy_check $1 failed for $2"; fi
  echo "syspolicy_check $1: $warnings warnings, no errors: $report"
}

# Gatekeeper accepts <path> to run (spctl writes its verdict to stderr).
accepted() { spctl -a -vvv -t exec "$1" 2>&1 | LC_ALL=C /usr/bin/grep -q ': accepted$'; }

# Staples <bundle>'s ticket: up to six tries, 10 s apart. Right after Accepted the ticket can take a while to reach
# stapler's lookup ("Could not find ticket"), and a rerun of the release would submit to Apple again.
staple() {  # staple <bundle>
  i=1
  until xcrun stapler staple "$1"; do
    [ $i -lt 6 ] || die "stapler staple failed for $1"
    echo "release: stapler staple failed for $1 (try $i of 6), again in 10 s" >&2
    i=$((i + 1))
    sleep 10
  done
}

# Notarizes <bundle> and staples its ticket (spec §6.2/§6.3): the zip and the notary output go in <work-dir>.
# Prints `submission <id>`. On a rejection, prints the notary log and fails.
notarize_and_staple() {  # notarize_and_staple <bundle> <work-dir>
  [ -d "$1" ] || die "no bundle at $1"
  mkdir -p "$2"
  profile="${MACNEUTRON_NOTARY_PROFILE:-macneutron}"
  zip="$2/$(basename "$1" .app).zip"
  syspolicy notary-submission "$1" "$2"
  rm -f "$zip"
  ditto -c -k --keepParent "$1" "$zip"
  # An upload error exits non-zero with no JSON: keep whatever it printed and judge by the JSON.
  xcrun notarytool submit "$zip" -p "$profile" --wait --output-format json > "$2/submit.json" || true
  id=$(jq -r -s 'last | .id // empty' "$2/submit.json" 2> /dev/null) || id=
  [ -n "$id" ] || die "notarytool submit gave no submission id: $(cat "$2/submit.json")"
  echo "submission $id"
  status=$(jq -r -s 'last | .status' "$2/submit.json")
  if [ "$status" != Accepted ]; then
    xcrun notarytool log "$id" -p "$profile" >&2 || true
    die "submission $id: $status"
  fi
  staple "$1"
  xcrun stapler validate "$1" || die "stapler validate failed for $1"
  syspolicy distribution "$1" "$2"
}
