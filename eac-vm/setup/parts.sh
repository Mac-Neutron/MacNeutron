#!/bin/sh
# Stages the VM parts (docs/superpowers/specs/2026-10-17-macneutron-vm-setup-design.md §4.2) in <out>: the image build's
# recipes, overlay and wine.tar (build/eac-vm-work/images/setup, from eac-vm/image/build.sh), its cmdline.txt,
# packages.lock and empty state template, and the guest tools run-game.sh and asound.conf use (share/bin). Release
# archives the folder as MacNeutron-<version>-vm-parts.tar.xz; macneutron vm-setup unpacks it into setup/parts.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$ROOT/build/eac-vm-work
I=$W/images
D=${1:?usage: parts.sh <out dir>}
rm -rf "$D"
mkdir -p "$D/share/bin"
cp -c -R "$I/setup/recipe" "$I/setup/overlay" "$D/"
cp -c "$I/setup/wine.tar" "$I/cmdline.txt" "$I/packages.lock" "$I/state-template.img" "$D/"
cp "$W/share/bin/libasound_module_pcm_mn.so" "$W/share/bin/mn-input" "$W/share/bin/steam.exe" "$D/share/bin/"
echo "$D"
