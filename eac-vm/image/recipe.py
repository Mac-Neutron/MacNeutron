#!/usr/bin/env python3
# The recipes a Mac rebuilds the base image and the initramfs from (docs/superpowers/specs/2026-10-17-macneutron-vm-
# setup-design.md §4.4): every entry of the builder's trees, each file's bytes found in a pinned package's payload
# (`pkg:<rpm>`, by SHA-256: payloads list /lib, the tree has /usr/lib) or else kept in the overlay
# (out/overlay/<sha256>: what package scripts and our build steps wrote). Runs in the builder as root, after build.sh.
#   recipe.py <root> <initramfs.img> <kver> <out> <rpm dir>...
# Writes out/recipe/base.tsv, out/recipe/initramfs.tsv, out/overlay/ and out/packages.json; prints counts and the
# overlay's size. Lines: type path mode uid gid size sha256 source link caps (see Sources/MacNeutronCore/VMSetup/
# Recipe.swift).
import hashlib, json, os, shutil, stat, subprocess, sys, tarfile, tempfile

def payload_index(rpm_dirs):
    index, packages = {}, []
    for d in rpm_dirs:
        for name in sorted(os.listdir(d)):
            if not name.endswith(".rpm"):
                continue
            path = os.path.join(d, name)
            q = subprocess.run(["rpm", "-qp", "--qf", "%{NAME}\\n%{VERSION}\\n%{RELEASE}\\n%{ARCH}\\n", path],
                               capture_output=True, text=True, check=True).stdout.split("\n")
            with open(path, "rb") as f:
                digest = hashlib.file_digest(f, "sha256").hexdigest()
            packages.append({"file": name, "name": q[0], "version": q[1], "release": q[2], "arch": q[3],
                             "size": os.path.getsize(path), "sha256": digest})
            with open(path, "rb") as src:
                archive = subprocess.Popen(["rpm2archive", "-n", "-"], stdin=src, stdout=subprocess.PIPE)
                with tarfile.open(fileobj=archive.stdout, mode="r|") as tar:
                    for member in tar:
                        if member.isfile():
                            h = hashlib.sha256()
                            data = tar.extractfile(member)
                            for chunk in iter(lambda: data.read(1 << 20), b""):
                                h.update(chunk)
                            index.setdefault(h.hexdigest(), name)
                archive.wait()
                if archive.returncode:
                    sys.exit(f"recipe: rpm2archive failed on {name}")
    return index, packages


def escaped(text):
    return text.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n")


def walk(root, index, overlay):
    lines, counts = [], {"pkg": 0, "overlay": 0, "d": 0, "l": 0, "dev": 0, "skipped": 0}

    def entry(full, path):
        st = os.lstat(full)
        mode, cols = stat.S_IMODE(st.st_mode), None
        if stat.S_ISDIR(st.st_mode):
            cols = ["d", path, "%o" % mode, st.st_uid, st.st_gid, "-", "-", "-", "-", "-"]
            counts["d"] += 1
        elif stat.S_ISLNK(st.st_mode):
            cols = ["l", path, "%o" % mode, st.st_uid, st.st_gid, "-", "-", "-", escaped(os.readlink(full)), "-"]
            counts["l"] += 1
        elif stat.S_ISREG(st.st_mode):
            with open(full, "rb") as f:
                digest = hashlib.file_digest(f, "sha256").hexdigest()
            if digest in index:
                source = "pkg:" + index[digest]
                counts["pkg"] += 1
            else:
                source = "overlay"
                counts["overlay"] += 1
                kept = os.path.join(overlay, digest)
                if not os.path.exists(kept):
                    shutil.copyfile(full, kept)
            try:
                caps = os.getxattr(full, "security.capability", follow_symlinks=False).hex()
            except OSError:
                caps = "-"
            cols = ["f", path, "%o" % mode, st.st_uid, st.st_gid, st.st_size, digest, source, "-", caps]
        elif stat.S_ISCHR(st.st_mode) or stat.S_ISBLK(st.st_mode):
            kind = "c" if stat.S_ISCHR(st.st_mode) else "b"
            cols = [kind, path, "%o" % mode, st.st_uid, st.st_gid, "-", "-", "-",
                    f"{os.major(st.st_rdev)}:{os.minor(st.st_rdev)}", "-"]
            counts["dev"] += 1
        else:
            print(f"recipe: skipped {path} (a fifo or socket)", file=sys.stderr)
            counts["skipped"] += 1
            return
        lines.append("\t".join([escaped(str(c)) if i == 1 else str(c) for i, c in enumerate(cols)]))

    entry(root, "/")
    for top, dirs, files in os.walk(root):
        rel = "/" + os.path.relpath(top, root) if top != root else ""
        dirs.sort()
        for name in sorted(files) + dirs:
            entry(os.path.join(top, name), rel + "/" + name)
    return lines, counts


def main():
    root, initramfs, kver, out, rpm_dirs = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5:]
    overlay = os.path.join(out, "overlay")
    for mine in ("recipe", "overlay"):  # out also holds the image build's wine.tar
        shutil.rmtree(os.path.join(out, mine), ignore_errors=True)
        os.makedirs(os.path.join(out, mine))
    index, packages = payload_index(rpm_dirs)
    for name, tree in (("base", root), ("initramfs", None)):
        if tree is None:
            tree = tempfile.mkdtemp(prefix="mn-initramfs-")
            subprocess.run(["lsinitrd", "--unpack", initramfs], cwd=tree, check=True, stdout=subprocess.DEVNULL)
        lines, counts = walk(tree, index, overlay)
        with open(os.path.join(out, "recipe", name + ".tsv"), "w") as f:
            f.write("\n".join(lines) + "\n")
        print(f"recipe: {name}: {len(lines)} entries, {counts}")
        if name == "initramfs":
            shutil.rmtree(tree)
    with open(os.path.join(out, "packages.json"), "w") as f:
        json.dump(packages, f, indent=1)
    size = sum(os.path.getsize(os.path.join(overlay, n)) for n in os.listdir(overlay))
    print(f"recipe: overlay: {len(os.listdir(overlay))} files, {size / 1e6:.1f} MB; {len(packages)} packages")


main()
