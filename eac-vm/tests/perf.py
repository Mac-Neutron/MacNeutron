"""The measurements of eac-vm/tests/perf-compare.sh (Direct3D forwarding spec §5.2), and the comparison against the
spec's gate (§1): the VM's GPU busy 95% or more, its frame rate within 10% of macOS, its CPU within 1.5 times.
  perf.py record <out.json> --mode macos|vm --appid N --seconds S --ps-before F --ps-after F --ioreg F... [--stats F
          --stats-offset N] [--profile F --profile-offset N] [--sample F]
  perf.py compare <macos.json> <vm.json>
"""
import argparse
import json
import os
import re
import sys
from collections import defaultdict

GATE_GPU, GATE_FPS, GATE_CPU = 95.0, 0.9, 1.5
# The VM's processes; on macOS, the game's (macos_game).
VM_PROCESSES = {"macneutron-vm", "mn-steam-host", "gvproxy"}
# sample(1)'s innermost frames where a thread waits rather than runs (an idle workqueue thread shows start_wqthread).
BLOCKING = ("__psynch_cvwait", "__psynch_mutexwait", "kevent", "__workq_kernreturn", "start_wqthread", "mach_msg2_trap",
            "mach_wait_until", "__semwait_signal", "semaphore_wait_trap", "semaphore_timedwait_trap",
            "semaphore_wait_signal_trap", "__sigsuspend", "__wait4", "poll", "read", "__accept", "__ulock_wait",
            "nanosleep", "__select", "usleep", "__recvfrom")
SPIN = {"consume", "mn_ring_next", "mach_absolute_time", "clock_gettime_nsec_np", "mach_timebase_info"}


def parse_time(text):
    """ps(1)'s TIME, [d-][h:]m:s (macOS prints minutes past 59), in seconds."""
    days, _, rest = text.strip().rpartition("-")
    seconds = 0.0
    for part in rest.split(":"):
        seconds = seconds * 60 + float(part)
    return seconds + (int(days) * 86400 if days else 0)


def _listing(text, match):
    procs = {}
    for line in text.splitlines():
        fields = line.split(None, 2)
        if len(fields) == 3 and fields[0].isdigit() and match(fields[2]):
            procs[fields[0]] = parse_time(fields[1])
    return procs


def macos_game(comm):
    """A process of the game on macOS: Wine names one by its Windows path; wineserver and the loader run from the
    tool's wine.app."""
    return "/wine.app/" in comm or re.match(r"^[A-Za-z]:\\", comm) is not None


def cpu_delta(before, after, match):
    """CPU seconds the matching processes used between two `ps -A -o pid=,time=,comm=` listings, and how many of
    them ended in between (their last stretch is lost). A process that started in between counts from zero."""
    old, new = _listing(before, match), _listing(after, match)
    return sum(t - old.get(pid, 0.0) for pid, t in new.items()), sum(1 for pid in old if pid not in new)


def gpu_busy(texts):
    """The mean of ioreg's IOAccelerator "Device Utilization %" over its outputs."""
    values = [int(m.group(1)) for t in texts for m in [re.search(r'"Device Utilization %"=(\d+)', t)] if m]
    return sum(values) / len(values) if values else None


def fps(stats, offset):
    """The mean frame rate of DXMT's full windows (stats.txt's ", every 5 s" lines) past byte offset."""
    rates = [float(m.group(1)) for m in re.finditer(r"^# [\d:]+ [\d.]+ s, \d+ frames \(([\d.]+) fps\), every 5 s$",
                                                     stats[offset:], re.M)]
    return sum(rates) / len(rates) if rates else None


def threads(profile, offset):
    """Per thread name, the mean over top's batches past byte offset of its threads' summed %CPU, highest first."""
    sums = defaultdict(float)
    batches = 0
    for batch in re.split(r"^(?=top - )", profile[offset:], flags=re.M):
        if not batch.startswith("top - "):
            continue
        batches += 1
        rows = False
        for line in batch.splitlines():
            if line.lstrip().startswith("PID "):
                rows = True
                continue
            fields = line.split(None, 11)
            if rows and len(fields) == 12 and fields[0].isdigit():
                sums[fields[11]] += float(fields[8])
    return sorted(((n, s / batches) for n, s in sums.items()), key=lambda x: -x[1]) if batches else []


def sample_split(sample):
    """Cores per kind of host work in a sample(1) call graph: running leaves over the window's samples (the most any
    thread has; sample lists a thread again for each dispatch queue it served, with that queue's few samples)."""
    lines = sample.splitlines()
    if "Call graph:" not in lines:
        return {}
    threads, current = [], None
    for line in lines[lines.index("Call graph:") + 1:]:
        if line.startswith("Total number in stack"):
            break
        head = re.match(r"^    (\d+) (Thread_\d+.*)$", line)
        node = re.match(r"^([ +!:|]*)(\d+) (\S+)", line)
        if head:
            current = (head.group(2), int(head.group(1)), [])
            threads.append(current)
        elif node and current:
            current[2].append((len(node.group(1)), int(node.group(2)), node.group(3)))
    split = dict.fromkeys(("guest", "wake", "spin", "sound", "other"), 0.0)
    window = max((total for _, total, _ in threads), default=0)
    for name, _, nodes in threads:
        path = []
        for i, (depth, count, fn) in enumerate(nodes):
            while path and path[-1][0] >= depth:
                path.pop()
            path.append((depth, fn))
            if i + 1 == len(nodes) or nodes[i + 1][0] <= depth:  # nothing deeper follows: a leaf
                kind = _kind(name, fn, {f for _, f in path})
                if kind:
                    split[kind] += count / window
    return {k: round(v, 3) for k, v in split.items()}


def scale(split, cores):
    """A sample split as shares of the CPU ps measured for macneutron-vm over the window. sample(1) records every
    thread at every tick, on a core or not, so a spinning thread waiting for one still looks busy: on 2026-10-17 a
    10 s sample summed 1.03 cores while ps measured 0.47 over the same seconds."""
    total = sum(split.values())
    return {k: round(v * cores / total, 3) if total else 0.0 for k, v in split.items()}


def _kind(thread, leaf, names):
    if leaf.startswith(BLOCKING):
        return None
    if leaf == "hv_trap":
        return "guest"
    if "fc_vcpu" in thread:
        return "wake"  # Hypervisor.framework's lock and interrupt bookkeeping around a vCPU's sleep
    if "serve" in names:
        return "sound"
    if names & SPIN or leaf.startswith("DYLD-STUB$$clock_gettime"):
        return "spin"
    return "other"


def _read(path, offset=0):
    if not path or not os.path.exists(path):
        return "", 0
    with open(path, errors="replace") as f:
        text = f.read()
    return text, min(offset, len(text))


def record(a):
    match = (lambda c: os.path.basename(c) in VM_PROCESSES) if a.mode == "vm" else macos_game
    before, after = _read(a.ps_before)[0], _read(a.ps_after)[0]
    used, ended = cpu_delta(before, after, match)
    vm_used = cpu_delta(before, after, lambda c: os.path.basename(c) == "macneutron-vm")[0]
    raw = sample_split(_read(a.sample)[0]) if a.sample else {}
    stats, so = _read(a.stats, a.stats_offset)
    profile, po = _read(a.profile, a.profile_offset)
    tops = threads(profile, po)
    out = {
        "appid": a.appid, "mode": a.mode, "seconds": a.seconds,
        "cpu_cores": round(used / a.seconds, 3), "processes_ended": ended,
        "gpu_busy": gpu_busy([_read(p)[0] for p in a.ioreg]),
        "fps": fps(stats, so),
        "threads": [[n, round(c, 1)] for n, c in tops[:20]],
        # DXMT's own threads: a lower bound on its share, D3D12 recording runs on the game's threads.
        "dxmt_threads": round(sum(c for n, c in tops if n.startswith("dxmt-")), 1),
        # Where macneutron-vm's CPU went: the sample's shares of what ps measured for it (scale), and the raw split.
        "host": scale(raw, vm_used / a.seconds) if raw else {},
        "host_sample": raw,
    }
    with open(a.out, "w") as f:
        json.dump(out, f, indent=1)
    print(json.dumps(out, indent=1))


def compare(a):
    with open(a.macos) as f:
        mac = json.load(f)
    with open(a.vm) as f:
        vm = json.load(f)
    ok = True

    def row(what, value, good):
        nonlocal ok
        ok &= good
        print(f"{what}: {value} {'PASS' if good else 'FAIL'}")

    gpu = vm.get("gpu_busy")
    row("VM GPU busy (95% or more)", f"{gpu:.1f}%" if gpu is not None else "none", gpu is not None and gpu >= GATE_GPU)
    if mac.get("fps") and vm.get("fps"):
        r = vm["fps"] / mac["fps"]
        row("fps, VM / macOS (0.90 or more)", f"{vm['fps']:.1f} / {mac['fps']:.1f} = {r:.2f}", r >= GATE_FPS)
    else:
        row("fps, VM / macOS (0.90 or more)", "no frame statistics", False)
    if mac.get("cpu_cores"):
        r = vm["cpu_cores"] / mac["cpu_cores"]
        row("CPU, VM / macOS (1.5 or less)", f"{vm['cpu_cores']:.2f} / {mac['cpu_cores']:.2f} cores = {r:.2f}",
            r <= GATE_CPU)
    else:
        row("CPU, VM / macOS (1.5 or less)", "no macOS CPU", False)
    print("perf-compare: gate " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main(argv):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("record")
    r.add_argument("out")
    r.add_argument("--mode", choices=["macos", "vm"], required=True)
    r.add_argument("--appid", required=True)
    r.add_argument("--seconds", type=float, required=True)
    r.add_argument("--ps-before", required=True)
    r.add_argument("--ps-after", required=True)
    r.add_argument("--ioreg", nargs="*", default=[])
    r.add_argument("--stats")
    r.add_argument("--stats-offset", type=int, default=0)
    r.add_argument("--profile")
    r.add_argument("--profile-offset", type=int, default=0)
    r.add_argument("--sample")
    c = sub.add_parser("compare")
    c.add_argument("macos")
    c.add_argument("vm")
    a = p.parse_args(argv)
    return record(a) or 0 if a.cmd == "record" else compare(a)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
