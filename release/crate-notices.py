"""The licence files of the crates MacNeutron's own Rust binaries link: macneutron (Contents/Helpers), MacNeutron
(Contents/MacOS) and Install MacNeutron, from the repository's one Cargo workspace (macneutron-vm links none). The
pattern of dxmt/mtld3d-notices.py: each crate's LICENSE*, LICENCE*, COPYING*, NOTICE* and UNLICENSE* files go to
<out>/crates/<name>-<version>/, and <out>/crates/INDEX lists every crate with its licence expression. The workspace's
own crates are MacNeutron's (../LICENSE) and are not listed. A crate that ships no licence file (the objc2 family, MIT
or a choice including MIT) gets MIT's text with its manifest's authors as the copyright line; one with no file and no
MIT among its terms stops the run. zstd-sys compiles zstd's C decoder in: zstd's own LICENSE (BSD-3-Clause), from the
crate's zstd/ folder, goes beside the crate's as zstd-LICENSE. Rust's standard library, which every binary links, has
its notices in the toolchain: its COPYRIGHT-library.html and licenses/ go to <out>/std/.
    python3 -I crate-notices.py <repository> <out> <cargo>
"""
import json
import os
import shutil
import subprocess
import sys

PACKAGES = ("macneutron-cli", "macneutron-app", "macneutron-installer")
TARGET = "aarch64-apple-darwin"
PREFIXES = ("license", "licence", "copying", "notice", "unlicense")
# crate -> (file in the crate, name it ships as): another project's code the crate compiles in, with its own notice.
EMBEDDED = {"zstd-sys": (os.path.join("zstd", "LICENSE"), "zstd-LICENSE")}


def main(tree, out, cargo):
    tree = os.path.abspath(tree)
    # rustup's cargo proxy runs the rustc beside it from PATH (vm/libkrun/licences.py does the same).
    os.environ["PATH"] = os.path.dirname(os.path.abspath(cargo)) + os.pathsep + os.environ.get("PATH", "")
    meta = json.loads(subprocess.run([cargo, "metadata", "--format-version", "1", "--locked"], cwd=tree,
                                     check=True, capture_output=True, text=True).stdout)
    by_name = {(p["name"], p["version"]): p for p in meta["packages"]}
    own = {p["id"] for p in meta["packages"] if p["id"] in meta["workspace_members"]}
    args = [cargo, "tree", "--locked", "-e", "normal,no-proc-macro", "--prefix", "none", "--format", "{p}",
            "--target", TARGET]
    for package in PACKAGES:
        args += ["-p", package]
    listing = subprocess.run(args, cwd=tree, check=True, capture_output=True, text=True).stdout
    crates = {}
    for line in listing.splitlines():
        words = line.split()
        if len(words) < 2 or not words[1].startswith("v"):
            continue
        p = by_name[(words[0], words[1][1:])]
        if p["id"] not in own:
            crates[(p["name"], p["version"])] = p
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(os.path.join(out, "crates"))
    index = []
    for (name, version), p in sorted(crates.items()):
        folder = os.path.dirname(p["manifest_path"])
        expression = p.get("license") or "(none given)"
        files = sorted(f for f in os.listdir(folder)
                       if f.lower().startswith(PREFIXES) and os.path.isfile(os.path.join(folder, f)))
        dest = os.path.join(out, "crates", f"{name}-{version}")
        os.makedirs(dest)
        for f in files:
            shutil.copyfile(os.path.join(folder, f), os.path.join(dest, f))
        note = ""
        if not files:
            if "MIT" not in expression.replace("(", " ").replace(")", " ").split():
                sys.exit(f"crate-notices: {name} {version} ({expression}) ships no licence file and isn't MIT")
            with open(os.path.join(dest, "LICENSE-MIT"), "w") as f:
                f.write(MIT.format(authors=", ".join(p.get("authors") or []) or f"the {name} authors"))
            note = " (MIT's text written here: the crate ships none)"
        if name in EMBEDDED:
            source, shipped = EMBEDDED[name]
            shutil.copyfile(os.path.join(folder, source), os.path.join(dest, shipped))
            note += f"; the code it compiles in: {shipped}"
        index.append(f"{name} {version}: {expression}{note}")
    with open(os.path.join(out, "crates", "INDEX"), "w") as f:
        f.write("\n".join(index) + "\n")
    rustc = os.path.join(os.path.dirname(cargo), "rustc")
    sysroot = subprocess.run([rustc, "--print", "sysroot"], cwd=tree, check=True, capture_output=True,
                             text=True).stdout.strip()
    docs = os.path.join(sysroot, "share", "doc", "rust")
    os.makedirs(os.path.join(out, "std"))
    shutil.copyfile(os.path.join(docs, "COPYRIGHT-library.html"), os.path.join(out, "std", "COPYRIGHT-library.html"))
    shutil.copytree(os.path.join(docs, "licenses"), os.path.join(out, "std", "licenses"))
    print(f"crate-notices: {len(crates)} crates", file=sys.stderr)


MIT = """MIT License

Copyright (c) {authors}

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
"""

if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit("usage: crate-notices.py <repository> <out> <cargo>")
    main(*sys.argv[1:])
