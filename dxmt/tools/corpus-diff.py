#!/usr/bin/env python3
"""corpus-diff.py <before> <after> [--stages vs,ps,cs,hs,ds,gs]: compares two dxil-translate outputs shader by shader
(DXIL SM 6.8 plan, P-corpus; DXMTRS step 4: dis=, refl=, tags=, --initialize-only's init lines). Prints
  corpus-diff: <ok>/<total> ok, <n> new failures, <n> fixed, <n> changed, <n> extra, <n> unchanged
(<ok>/<total> from <after>'s summary line), then a "new-fail <file> <reason>", "fixed <file>", "changed <file>",
"extra <file>" or "missing <file>" line per shader concerned, and "air-only <file>" for one whose metallib bytes alone
differ (air=: information, never a failure). Exit 1 on a status change either way (ok or init to fail is a new
failure, fail to ok a fix: an identity port accepts no shader the original refuses), a dis=, refl= or tags= difference,
a reason or refl= difference between two fail lines, a shader missing from <after> or extra in it, zero shaders
compared, an ok line without air= (a run with --flags, or a tool without the hash; init lines carry none), or a file
without a summary line (a run that stopped early). --stages keeps a shader by the stage on its <before> line (an extra
one by its <after> line); a "-" stage (Initialize failed) or a plain fail line (no stage) passes every filter."""
import sys

errors = []


def read(path):
    results, summary = {}, None
    for line in open(path, encoding="utf-8", errors="replace"):
        parts = line.rstrip("\n").split(" ")
        if parts[0] in ("ok", "init") and len(parts) >= 3:
            # ok <file> <stage> <ms> air= [dis= refl= tags=]; init <file> <stage> refl=: the time varies, the rest doesn't
            fields = dict(p.split("=", 1) for p in parts[3:] if "=" in p)
            if parts[0] == "ok" and "air" not in fields:
                errors.append(f"corpus-diff: no air= in {path}: {line.rstrip()}")
            results[parts[1]] = ("ok", parts[2], fields)
        elif parts[0] == "fail" and len(parts) >= 2:
            if len(parts) >= 4 and parts[3].startswith("refl="):  # fail <file> <stage>|- refl=<h>|refl=- <reason>
                results[parts[1]] = ("fail", parts[2], {"refl": parts[3][5:], "reason": " ".join(parts[4:])})
            else:  # fail <file> <reason>: a run with Metal
                results[parts[1]] = ("fail", "-", {"reason": " ".join(parts[2:])})
        elif parts[0] == "summary" and len(parts) >= 2:
            summary = parts[1]
    if summary is None:
        errors.append(f"corpus-diff: no summary line in {path}")
    return results, summary


args, stages = sys.argv[1:], None
if "--stages" in args:
    i = args.index("--stages")
    stages = set(args[i + 1].split(",")) if i + 1 < len(args) else set()
    del args[i:i + 2]
if len(args) != 2:
    sys.exit("usage: corpus-diff.py <before> <after> [--stages vs,ps,cs,hs,ds,gs]")
(before, _), (after, after_summary) = read(args[0]), read(args[1])
def kept(r):
    return stages is None or r[1] == "-" or r[1] in stages


names = sorted(n for n in set(before) | set(after) if kept(before.get(n) or after[n]))
new_fail, fixed, changed, extra, missing, air_only, unchanged = [], [], [], [], [], [], 0
for name in names:
    old, new = before.get(name), after.get(name)
    if old is None:
        extra.append(name)
    elif new is None:
        missing.append(name)
    elif old[0] == "ok" and new[0] == "fail":
        new_fail.append(f"{name} {new[2]['reason']}")
    elif old[0] == "fail" and new[0] == "ok":
        fixed.append(name)
    elif any(old[2].get(k) != new[2].get(k) for k in ("dis", "refl", "tags", "reason")):
        changed.append(name)
    else:
        unchanged += 1
        if old[2].get("air") != new[2].get("air"):
            air_only.append(name)
if not any(n in before for n in names):
    errors.append("corpus-diff: no shader compared")
print(f"corpus-diff: {after_summary or '?'} ok, {len(new_fail)} new failures, {len(fixed)} fixed, {len(changed)} changed, "
      f"{len(extra)} extra, {unchanged} unchanged")
for kind, items in (("new-fail", new_fail), ("fixed", fixed), ("changed", changed), ("extra", extra),
                    ("missing", missing), ("air-only", air_only)):
    for item in items:
        print(f"{kind} {item}")
for e in errors:
    print(e)
sys.exit(1 if new_fail or fixed or changed or extra or missing or errors else 0)
