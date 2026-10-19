#!/usr/bin/env python3
# The forwarded calls a frame that leave the guest (the frame rate, 2026-10-18): a run's guest traces
# (MACNEUTRON_VM_D3D_TRACE=1: traces/d3d-guest.*.txt) joined with the call table's modes (d3d-gen/calls.c), per frame.
# Descriptor writes into shader-visible heaps are queued by guest code, whatever the table says (guest/proxy.cpp).
#   exits.py <calls.c> <frames> <guest trace>...   prints "exits: ..." then the top methods by mode
import re, sys
from collections import defaultdict

calls, frames, traces = sys.argv[1], float(sys.argv[2]), sys.argv[3:]
mode = {}
for m in re.finditer(r'\[(\d+)\] = \{"([^"]+)", [^,]+, (MN_WM_\w+)', open(calls).read()):
    mode[m.group(2)] = m.group(3)
count, totals = defaultdict(int), defaultdict(int)
for t in traces:
    for line in open(t):
        w = line.split()
        if len(w) >= 3 and w[0] == "#" and w[1] in ("exits", "submits"):
            totals[w[1]] += int(w[2])
        elif len(w) >= 3 and w[0].isdigit():
            count[w[1]] += int(w[2])
print("exits: %.0f a frame, submits %.0f a frame (the guest's counts over %d frames)"
      % (totals["exits"] / frames, totals["submits"] / frames, frames))
by = defaultdict(list)
for name, n in count.items():
    by[mode.get(name, "?")].append((n / frames, name))
for m in ("MN_WM_SUBMIT", "MN_WM_SYNC", "MN_WM_QUEUED"):
    print(m)
    for per, name in sorted(by[m], reverse=True)[:15]:
        print("  %8.1f  %s" % (per, name))
