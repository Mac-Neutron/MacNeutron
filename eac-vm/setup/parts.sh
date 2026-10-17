#!/bin/sh
# Stages the VM parts (docs/superpowers/specs/2026-10-17-macneutron-vm-setup-design.md §4.2) in <out>: the image build's
# recipes, overlay and wine.tar (build/eac-vm-work/images/setup, from eac-vm/image/build.sh), its cmdline.txt,
# packages.lock and empty state template, and the guest tools run-game.sh and asound.conf use (share/bin). Release
# archives the folder as MacNeutron-<version>-vm-parts.tar.xz; macneutron vm-setup unpacks it into setup/parts.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$ROOT/build/eac-vm-work
I=$W/images
D=${1:?usage: parts.sh <out dir> [wine.app]}
APP=${2:-$ROOT/build/wine-arm64/wine.app}  # whose licence texts the parts carry (the release passes its own)
rm -rf "$D"
mkdir -p "$D/share/bin"
cp -c -R "$I/setup/recipe" "$I/setup/overlay" "$D/"
cp -c "$I/setup/wine.tar" "$I/cmdline.txt" "$I/packages.lock" "$I/state-template.img" "$D/"
cp "$W/share/bin/libasound_module_pcm_mn.so" "$W/share/bin/mn-input" "$W/share/bin/steam.exe" "$D/share/bin/"
# The licences of what the parts hold, from wine.app (the same Wine, DXMT, FEX and lsteamclient trees' texts).
L=$D/licenses
mkdir -p "$L/dxmt"
for d in wine fex lsteamclient macneutron; do cp -R "$APP/Contents/Resources/licenses/$d" "$L/"; done
cp "$APP/Contents/Resources/DXMT/LICENSE" "$APP/Contents/Resources/DXMT/COPYING.LIB" "$APP/Contents/Resources/DXMT/LICENSE.OLD" \
  "$L/dxmt/"
cat > "$L/README" << 'EOF'
MacNeutron's VM parts: the game VM's own files. macneutron vm-setup builds the VM's images from them and from
Fedora's packages, which it downloads from Fedora to the Mac (MacNeutron never ships those).
- wine.tar: MacNeutron's Wine for x86-64 Linux (LGPL-2.1-or-later, wine/), with DXMT's Direct3D DLLs and its guest
  winemetal.so (LGPL-2.1-or-later, dxmt/), and Proton's lsteamclient (BSD-3-Clause, lsteamclient/) with MacNeutron's
  Steam forwarder (MIT, macneutron/).
- overlay/: FEX for Linux (MIT, fex/); MacNeutron's tso-litmus and image configuration (MIT, macneutron/); and data
  the image build generated from Fedora's packages: the user database, the library and module caches, systemd's
  hardware database, and the CA trust bundles built from ca-certificates (Mozilla's root store, MPL-2.0).
- share/bin: MacNeutron's guest tools (MIT, macneutron/).
The source is in the release's source archive (MacNeutron-<version>-source.tar.gz): wine-linux.tar is this Wine
(eac-vm/wine-patches on its BASE), dxmt.tar is DXMT, lsteamclient.tar is lsteamclient, and MacNeutron.tar holds the rest.
EOF
echo "$D"
