#!/usr/bin/env python3
# The app's vm-manifest.json (docs/superpowers/specs/2026-10-17-macneutron-vm-setup-design.md §4.3; read by
# Sources/MacNeutronCore/VMSetup/VMSetupManifest.swift), printed:
#   manifest.py <parts archive> <packages.json> <kver> <x86-64-rootfs.erofs> <parts url> [mirror url...]
# packages.json is recipe.py's (every pinned RPM with its size and SHA-256); the FEX root's package goes in fexRootfs
# with the SHA-256 of the image inside it. Mirror URLs, for tests, replace Fedora's sources: <mirror>/<rpm file>.
import hashlib, json, os, sys

FEX = "fex-emu-rootfs-fedora"


def sha256(path):
    with open(path, "rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def main():
    parts, packages_json, kver, x86, url, mirrors = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6:]
    with open(packages_json) as f:
        packages = json.load(f)
    fex = [p for p in packages if p["name"] == FEX]
    if len(fex) != 1:
        sys.exit(f"manifest: {len(fex)} {FEX} packages in {packages_json}")
    manifest = {
        "version": 1,
        "parts": {"url": url, "size": os.path.getsize(parts), "sha256": sha256(parts)},
        "kernel": kver,
        "packages": [p for p in packages if p["name"] != FEX],
        "fexRootfs": {"package": fex[0], "image": "/usr/share/fex-emu/RootFS/default.erofs", "imageSha256": sha256(x86)},
        "koji": {"base": "https://kojipkgs.fedoraproject.org/packages/", "signedKey": "6d9f90a6"},
    }
    if mirrors:
        manifest["mirrors"] = mirrors
    json.dump(manifest, sys.stdout, indent=1)
    print()


main()
