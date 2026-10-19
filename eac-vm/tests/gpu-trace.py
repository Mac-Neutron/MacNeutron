#!/usr/bin/env python3
# A Metal System Trace's GPU time (the frame rate, 2026-10-18): from `xctrace record --template 'Metal System Trace'
# --all-processes`, per process the GPU's busy time (its intervals' union across channels) and each channel's, per
# presented frame (the process's Core Animation present requests), and the time in each GPU performance state.
#   gpu-trace.py <trace> [process name, default macneutron-vm]   prints "gpu-trace: ..." lines
import subprocess, sys
import xml.etree.ElementTree as ET
from collections import defaultdict

trace = sys.argv[1]
want = sys.argv[2] if len(sys.argv) > 2 else "macneutron-vm"


def table(schema):
    x = subprocess.run(["xcrun", "xctrace", "export", "--input", trace, "--xpath",
                        f'/trace-toc/run[@number="1"]/data/table[@schema="{schema}"]'],
                       capture_output=True, text=True).stdout
    if not x.strip():
        return [], []
    root = ET.fromstring(x)
    cols = [c.findtext("mnemonic") for c in root.iter("col")]
    seen = {el.attrib["id"]: el for el in root.iter() if "id" in el.attrib}  # an id may be on a nested element
    rows = [[seen[el.attrib["ref"]] if "ref" in el.attrib else el for el in row] for row in root.iter("row")]
    return cols, rows


def text(el):  # a process's name is in its fmt ("name (pid)")
    return el.attrib.get("fmt", el.text or "") if el is not None else ""


def union(spans):
    total, end = 0, -1
    for s, e in sorted(spans):
        if e <= end:
            continue
        total += e - max(s, end)
        end = e
    return total


cols, rows = table("metal-gpu-intervals")
ix = {c: i for i, c in enumerate(cols)}
spans, channels, t0, t1 = defaultdict(list), defaultdict(lambda: defaultdict(list)), None, None
passes = defaultdict(list)
for r in rows:
    s, d = int(r[ix["start"]].text), int(r[ix["duration"]].text)
    t0, t1 = s if t0 is None else min(t0, s), s + d if t1 is None else max(t1, s + d)
    proc = text(r[ix["process"]]).split(" (")[0]
    if r[ix["event-depth"]].text not in (None, "0"):
        if proc == want:  # an encoder inside a command buffer: DXMT_STATS=1 labels them "pass-<n> <kind>"
            label = text(r[ix["event-label"]]).split(" ", 1)[-1] or "(none)"
            passes[label].append((s, s + d))
        continue
    spans[proc].append((s, s + d))
    channels[proc][text(r[ix["channel-name"]])].append((s, s + d))
window = (t1 - t0) / 1e9 if t0 is not None else 0
cols, rows = table("ca-client-present-request")
ix = {c: i for i, c in enumerate(cols)}
presents = defaultdict(int)
for r in rows:
    if "process" in ix:
        presents[text(r[ix["process"]]).split(" (")[0]] += 1
print(f"gpu-trace: {window:.2f} s of GPU intervals")
for proc in sorted(spans, key=lambda p: -union(spans[p])):
    busy = union(spans[proc]) / 1e9
    n = presents.get(proc, 0)
    per = f", {busy * 1e3 / n:.2f} ms a frame over {n} presents" if n else ""
    mark = " <-" if proc == want else ""
    print(f"gpu-trace: {proc}: busy {busy:.3f} s ({100 * busy / window:.1f}%){per}{mark}")
    if proc == want:
        for ch in sorted(channels[proc], key=lambda c: -union(channels[proc][c])):
            b = union(channels[proc][ch]) / 1e9
            print(f"gpu-trace:   {ch}: {b:.3f} s" + (f", {b * 1e3 / n:.2f} ms a frame" if n else ""))
        # The GPU's idle gaps between this process's work: how many a frame, and how long.
        gaps, end = [], None
        for s, e in sorted(spans[proc]):
            if end is not None and s > end:
                gaps.append(s - end)
            end = e if end is None else max(end, e)
        for lo, hi in ((0, 0.1e6), (0.1e6, 0.5e6), (0.5e6, 1e6), (1e6, 2e6), (2e6, 1e12)):
            g = [x for x in gaps if lo <= x < hi]
            if g:
                print(f"gpu-trace:   idle {lo / 1e6:g}-{hi / 1e6:g} ms: {len(g) / max(n, 1):.1f} a frame, "
                      f"{sum(g) / 1e6 / max(n, 1):.2f} ms a frame")
        for label in sorted(passes, key=lambda k: -union(passes[k]))[:15]:
            b = union(passes[label]) / 1e6
            print(f"gpu-trace:   pass {label}: {b / max(n, 1):.2f} ms a frame ({len(passes[label]) / max(n, 1):.1f} a frame)")
cols, rows = table("gpu-performance-state-intervals")
ix = {c: i for i, c in enumerate(cols)}
states = defaultdict(int)
for r in rows:
    states[text(r[ix["gpu-performance-state"]])] += int(r[ix["duration"]].text)
total = sum(states.values()) or 1
print("gpu-trace: performance states " + ", ".join(f"{k} {100 * v / total:.0f}%" for k, v in
                                                 sorted(states.items(), key=lambda kv: -kv[1])))
