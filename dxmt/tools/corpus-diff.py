#!/usr/bin/env python3
"""corpus-diff.py <before> <after>: compares two dxil-translate outputs (plain, with air= hashes) shader by shader
(DXIL SM 6.8 plan, P-corpus). Prints
  corpus-diff: <ok>/<total> ok, <n> new failures, <n> fixed, <n> changed, <n> unchanged
(<ok>/<total> from <after>'s summary line), then a "new-fail <file> <reason>" line per shader that translated before and
fails after, and a "changed <file>" line per shader whose translated metallibs differ. Exit 1 on a new failure, or when
either file has no summary line (a run that stopped early)."""
import sys


def read(path):
    results, summary = {}, None
    for line in open(path, encoding="utf-8", errors="replace"):
        parts = line.split()
        if parts[:1] == ["ok"] and len(parts) >= 3:  # ok <file> <stage> <ms> air=<hash>: the time varies, the hash doesn't
            results[parts[1]] = ("ok", next((p for p in parts if p.startswith("air=")), ""))
        elif parts[:1] == ["fail"] and len(parts) >= 2:
            results[parts[1]] = ("fail", line.rstrip("\n").split(" ", 2)[2] if len(parts) > 2 else "")
        elif parts[:1] == ["summary"] and len(parts) >= 2:
            summary = parts[1]
    return results, summary


(before, before_summary), (after, after_summary) = read(sys.argv[1]), read(sys.argv[2])
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
print(f"corpus-diff: {after_summary or '?'} ok, {len(new_fail)} new failures, {fixed} fixed, {len(changed)} changed, "
      f"{unchanged} unchanged")
for name, reason in new_fail:
    print(f"new-fail {name} {reason}")
for name in changed:
    print(f"changed {name}")
if not before_summary or not after_summary:
    print(f"corpus-diff: no summary line in {sys.argv[1] if not before_summary else sys.argv[2]}")
sys.exit(1 if new_fail or not before_summary or not after_summary else 0)
