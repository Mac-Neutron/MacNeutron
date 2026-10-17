#!/usr/bin/env python3
# The licence files of libkrun and of every crate linked into our build of it (eac-vm/libkrun/build.sh's features), from
# Cargo's local registry, offline: what MacNeutron.app ships beside Contents/Helpers/vm/libkrun.
#   licences.py <libkrun source> <out dir> [cargo]
# Writes <out>/<crate>-<version>/<its LICENSE, COPYING and NOTICE files> and <out>/CRATES.txt (crate, version, licence
# expression); a crate with no licence file of its own is named there and fails the run.
import os, re, shutil, subprocess, sys

src, out = sys.argv[1], sys.argv[2]
cargo = sys.argv[3] if len(sys.argv) > 3 else "cargo"
features = "blk,net,ffi,gpu,input"
env = dict(os.environ, PATH=os.path.dirname(os.path.abspath(cargo)) + os.pathsep + os.environ.get("PATH", ""))
tree = subprocess.run([cargo, "tree", "--offline", "-p", "libkrun", "--no-default-features", "--features", features,
                       "-e", "normal", "--prefix", "none", "--format", "{p}|{l}"],
                      cwd=src, env=env, capture_output=True, text=True, check=True).stdout
registry = os.path.expanduser("~/.cargo/registry/src")
shutil.rmtree(out, ignore_errors=True)
os.makedirs(out)
rows, missing, seen = [], [], set()
for line in sorted(set(tree.splitlines())):
    if not line.strip():
        continue
    package, licence = line.split("|", 1)
    words = package.replace(" (*)", "").split()
    name, version = words[0], words[1].lstrip("v")
    if (name, version) in seen:
        continue
    seen.add((name, version))
    path = re.search(r"\((/[^)]*)\)", package)  # a path crate: libkrun's own workspace
    git = re.search(r"\(https?://[^)#]*#([0-9a-f]+)\)", package)  # a git crate: in Cargo's git checkouts
    root = None
    if path:
        folder = path.group(1)
    elif git:
        checkouts = os.path.expanduser("~/.cargo/git/checkouts")
        roots = [os.path.join(checkouts, d, git.group(1)[:7]) for d in os.listdir(checkouts)
                 if os.path.isdir(os.path.join(checkouts, d, git.group(1)[:7]))]
        root = roots[0] if roots else None
        folder = os.path.join(root, name) if root and os.path.isdir(os.path.join(root, name)) else root
    else:
        found = [os.path.join(registry, i, f"{name}-{version}") for i in os.listdir(registry)
                 if os.path.isdir(os.path.join(registry, i, f"{name}-{version}"))]
        folder = found[0] if found else None
    files = [f for f in sorted(os.listdir(folder)) if re.match(r"(LICEN[CS]E|COPYING|NOTICE)", f, re.I)] if folder else []
    if not files and path:  # libkrun's own crates take the repository's licence
        folder, files = os.path.abspath(src), ["LICENSE"]
    if not files and root:  # a git crate in a workspace: the repository's
        folder = root
        files = [f for f in sorted(os.listdir(root)) if re.match(r"(LICEN[CS]E|COPYING|NOTICE)", f, re.I)]
    if not files:
        missing.append(f"{name} {version}")
    dest = os.path.join(out, f"{name}-{version}")
    os.makedirs(dest, exist_ok=True)
    for f in files:
        shutil.copyfile(os.path.join(folder, f), os.path.join(dest, f))
    rows.append(f"{name} {version} {licence.strip() or '-'}")
with open(os.path.join(out, "CRATES.txt"), "w") as f:
    f.write("# crate version licence (each crate's files are in <crate>-<version>/)\n" + "\n".join(rows) + "\n")
print(f"licences: {len(rows)} crates")
if missing:
    sys.exit("licences: no licence file for " + ", ".join(missing))
