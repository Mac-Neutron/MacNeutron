#!/usr/bin/env python3
"""corpus-diff.py <before> <after>: compares two dxil-translate outputs (plain, with air= hashes) shader by shader
(DXIL SM 6.8 plan, P-corpus). Prints
  corpus-diff: <ok>/<total> ok, <n> new failures, <n> fixed, <n> changed, <n> unchanged
(<ok>/<total> from <after>'s summary line), then a "new-fail <file> <reason>" line per shader that translated before and
fails after, a "changed <file>" line per shader whose translated metallibs differ, and a "missing <file>" line per
shader in <before> but not in <after>. Exit 1 on a new failure, a missing shader, an ok line without air= (a run of
dxil-translate --flags, or of a tool without the hash), or when either file has no summary line (a run that stopped
early)."""
import sys

errors = []


def read(path):
    results, summary = {}, None
    for line in open(path, encoding="utf-8", errors="replace"):
        parts = line.split()
        if parts[:1] == ["ok"] and len(parts) >= 3:  # ok <file> <stage> <ms> air=<hash>: the time varies, the hash doesn't
            air = next((p for p in parts if p.startswith("air=")), None)
            if air is None:
                errors.append(f"corpus-diff: no air= in {path}: {line.rstrip()}")
            results[parts[1]] = ("ok", air)
        elif parts[:1] == ["fail"] and len(parts) >= 2:
            results[parts[1]] = ("fail", line.rstrip("\n").split(" ", 2)[2] if len(parts) > 2 else "")
        elif parts[:1] == ["summary"] and len(parts) >= 2:
            summary = parts[1]
    if summary is None:
        errors.append(f"corpus-diff: no summary line in {path}")
    return results, summary


(before, _), (after, after_summary) = read(sys.argv[1]), read(sys.argv[2])
new_fail, fixed, changed, unchanged = [], 0, [], 0
for name, (status, value) in sorted(after.items()):
    old = before.get(name)
    if status == "fail" and (old is None or old[0] == "ok"):
        new_fail.append((name, value))
    elif status == "ok" and old is not None and old[0] == "fail":
        fixed += 1
    elif status == "ok" and old is not None and old[1] != value:
        changed.append(name)
    else:
        unchanged += 1
missing = sorted(set(before) - set(after))
print(f"corpus-diff: {after_summary or '?'} ok, {len(new_fail)} new failures, {fixed} fixed, {len(changed)} changed, "
      f"{unchanged} unchanged")
for name, reason in new_fail:
    print(f"new-fail {name} {reason}")
for name in changed:
    print(f"changed {name}")
for name in missing:
    print(f"missing {name}")
for e in errors:
    print(e)
sys.exit(1 if new_fail or missing or errors else 0)
