"""The licence files of the crates mtld3d's shipped binaries link (D3D9 spec §4; wine.app carries every notice its
binaries need, ship-base spec §4): d3d9.dll and mtld3d.dll (aarch64- and arm64ec-pc-windows-msvc) and mtld3d.so
(aarch64-apple-darwin). Each crate's LICENSE*, LICENCE*, COPYING*, NOTICE* and UNLICENSE* files go to
<out>/<name>-<version>/, and <out>/INDEX lists every crate with its licence expression. mtld3d's own crates are its
zlib LICENSE (bundle.sh ships it); the snmalloc-sys it vendors is third-party code and is listed. A crate that ships no
licence file (the objc2 family, MIT or a choice including MIT) gets MIT's text with its manifest's authors as the
copyright line; one with no file and no MIT among its terms stops the run, to be handled by hand.
    python3 -I mtld3d-notices.py <mtld3d tree> <out> <cargo>   (cargo: the toolchain's, with `+<toolchain>` as needed)
"""
import json
import os
import shutil
import subprocess
import sys

LEGS = (("windows", ("d3d9", "mtld3d"), ("aarch64-pc-windows-msvc", "arm64ec-pc-windows-msvc")),
        ("unix", ("mtld3d-unix",), ("aarch64-apple-darwin",)))
PREFIXES = ("license", "licence", "copying", "notice", "unlicense")
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


def main(tree, out, cargo):
    cargo = cargo.split()
    tree = os.path.abspath(tree)
    crates = {}  # (name, version) -> (directory, licence expression, authors)
    for workspace, packages, targets in LEGS:
        cwd = os.path.join(tree, workspace)
        meta = json.loads(subprocess.run(cargo + ["metadata", "--format-version", "1", "--locked"], cwd=cwd,
                                         check=True, capture_output=True, text=True).stdout)
        by_name = {(p["name"], p["version"]): p for p in meta["packages"]}
        for target in targets:
            args = ["tree", "--locked", "-e", "normal", "--prefix", "none", "--format", "{p}", "--target", target]
            for package in packages:
                args += ["-p", package]
            listing = subprocess.run(cargo + args, cwd=cwd, check=True, capture_output=True, text=True).stdout
            for line in listing.splitlines():
                words = line.split()
                if len(words) < 2 or not words[1].startswith("v"):
                    continue
                key = (words[0], words[1][1:])
                p = by_name[key]
                folder = os.path.dirname(p["manifest_path"])
                own = folder.startswith(tree + os.sep) and os.sep + "vendor" + os.sep not in folder
                if not own:
                    crates[key] = (folder, p.get("license") or "(none given)", p.get("authors") or [])
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    index = []
    for (name, version), (folder, expression, authors) in sorted(crates.items()):
        files = sorted(f for f in os.listdir(folder)
                       if f.lower().startswith(PREFIXES) and os.path.isfile(os.path.join(folder, f)))
        dest = os.path.join(out, f"{name}-{version}")
        os.makedirs(dest)
        for f in files:
            shutil.copyfile(os.path.join(folder, f), os.path.join(dest, f))
        if not files:
            if "MIT" not in expression.replace("(", " ").replace(")", " ").split():
                sys.exit(f"mtld3d-notices: {name} {version} ({expression}) ships no licence file and isn't MIT")
            with open(os.path.join(dest, "LICENSE-MIT"), "w") as f:
                f.write(MIT.format(authors=", ".join(authors) or f"the {name} authors"))
        note = "" if files else " (MIT's text written here: the crate ships none)"
        index.append(f"{name} {version}: {expression}{note}")
    with open(os.path.join(out, "INDEX"), "w") as f:
        f.write("\n".join(index) + "\n")
    print(f"mtld3d-notices: {len(crates)} crates, {sum(1 for line in index if 'ships none' in line)} with MIT's text "
          f"written for them", file=sys.stderr)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit("usage: mtld3d-notices.py <mtld3d tree> <out> <cargo>")
    main(*sys.argv[1:])
