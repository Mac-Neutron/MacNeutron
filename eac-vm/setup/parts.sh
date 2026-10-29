#!/bin/sh
# Stages the VM parts (docs/superpowers/specs/2026-10-17-macneutron-vm-setup-design.md §4.2) in <out>: the image build's
# recipes, overlay and wine.tar (build/eac-vm-work/images/setup, from eac-vm/image/build.sh), its cmdline.txt,
# packages.lock and empty state template, and the guest tools run-game.sh and asound.conf use (share/bin). Release
# archives the folder as MacNeutron-<version>-vm-parts.tar.xz; macneutron vm-setup unpacks it into setup/parts.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
I=$W/images
D=${1:?usage: parts.sh <out dir> [wine.app]}
APP=${2:-${BUILD_DIR:-$ROOT/build}/wine-arm64/wine.app}  # whose licence texts the parts carry (the release passes its own)
rm -rf "$D"
mkdir -p "$D/share/bin/lib64" "$D/share/bin/lib"
cp -c -R "$I/setup/recipe" "$I/setup/overlay" "$D/"
cp -c "$I/setup/wine.tar" "$I/cmdline.txt" "$I/packages.lock" "$I/state-template.img" "$D/"
cp "$W/share/bin/libasound_module_pcm_mn.so" "$W/share/bin/mn-input" "$W/share/bin/steam.exe" "$D/share/bin/"
for l in lib64 lib; do cp "$W/share/bin/$l/libasound_module_pcm_mn.so" "$D/share/bin/$l/"; done
# The licences of what the parts hold, from wine.app (the same Wine, DXMT, FEX and lsteamclient trees' texts).
L=$D/licenses
mkdir -p "$L/dxmt"
for d in wine fex lsteamclient macneutron; do cp -R "$APP/Contents/Resources/licenses/$d" "$L/"; done
cp "$APP/Contents/Resources/DXMT/LICENSE" "$APP/Contents/Resources/DXMT/COPYING.LIB" "$APP/Contents/Resources/DXMT/LICENSE.OLD" \
  "$L/dxmt/"
# DXMT's nvngx.dll is Rust (DXMTRS): Rust's standard library's texts, wine.app's copy of them.
cp -R "$APP/Contents/Resources/licenses/mtld3d/rust" "$L/dxmt/rust"
[ -s "$L/dxmt/rust/COPYRIGHT-library.html" ] || { echo "parts: no Rust standard library notice in $APP" >&2; exit 1; }
. "$ROOT/eac-vm/image/alsa-lib.pin"
cat > "$L/README" << EOF
MacNeutron's VM parts: the game VM's own files. macneutron vm-setup builds the VM's images from them and from
Fedora's packages, which it downloads from Fedora to the Mac (MacNeutron ships none of those but one library, below).
- wine.tar: MacNeutron's Wine for x86-64 Linux (LGPL-2.1-or-later, wine/), with DXMT's Direct3D DLLs and its guest
  winemetal.so (LGPL-2.1-or-later, dxmt/; its nvngx.dll links Rust's standard library, dxmt/rust), and Proton's lsteamclient (BSD-3-Clause, lsteamclient/) with MacNeutron's
  Steam forwarder (MIT, macneutron/). Its lib/mn-i386 holds Fedora's alsa-lib for i686 ($ALSA_LIB_NVR:
  libasound.so.2, LGPL-2.1-or-later, COPYING.alsa-lib beside it), for 32-bit games' sound. Its share/fonts holds
  Proton's Windows-named fonts, built from the Liberation fonts (SIL OFL 1.1, LICENSE beside them; not Microsoft's
  fonts) as Proton names them.
- overlay/: FEX for Linux (MIT, fex/); MacNeutron's tso-litmus and image configuration (MIT, macneutron/); and data
  the image build generated from Fedora's packages with their own tools: the user database (systemd-sysusers), the
  library cache (glibc's ldconfig), the kernel module lists (kmod's depmod, from the kernel's modules), systemd's
  hardware database (from systemd's hwdb files, LGPL-2.1-or-later), and the CA trust bundles (update-ca-trust, from
  ca-certificates: Mozilla's root store, MPL-2.0). Those packages' sources are Fedora's, at the versions in
  packages.lock.
- share/bin: MacNeutron's guest tools (MIT, macneutron/).
The source is in the release's source archive (MacNeutron-<version>-source.tar.gz): wine.tar is this Wine (the
Mac build's tree, our Wine fork), fex.tar is FEX, dxmt.tar is DXMT, lsteamclient.tar is lsteamclient, and MacNeutron.tar holds the
rest; ${ALSA_LIB_SRPM_URL##*/} is Fedora's source of the i686 libasound.
EOF
echo "$D"
