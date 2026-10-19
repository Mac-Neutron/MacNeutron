#!/bin/sh
# Builds build/eac-vm-work/macneutron-vm against eac-vm/libkrun's build, with the Metal transport's executor
# (eac-vm/metal/host), Direct3D forwarding's dispatcher and stubs (eac-vm/d3d, generated from native DXMT's headers)
# and the TPM's engine (eac-vm/libtpms), signed with the hypervisor entitlement; build/eac-vm-work/metal-host,
# MacNeutron's winemetal.so beside stand-ins for its Wine libraries (eac-vm/metal/wm), which the executor loads; and
# build/eac-vm-work/dxmt, native DXMT (eac-vm/dxmt-native/build.sh), which the first Direct3D record loads.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$ROOT/build/eac-vm-work
# The winemetal handler's C and Objective-C, compiled with clang: swiftc takes the objects.
DXMT=$ROOT/build/wine-arm64-src/dxmt/src
M=$ROOT/eac-vm/metal
mkdir -p "$W/obj"
objs=
for f in wm/calls.c wm/cmdlist.c wm/marshal.c wm/slots.c wm/host/htable.c wm/host/loader.c wm/host/invoke.m wm/host/memory.m \
  wm/host/events.m wm/host/present.m wm/host/dispatch.m; do
  o="$W/obj/$(basename "$f").o"
  clang -O2 -std=gnu2x -Wall -DMN_WM_HOST -DDXMT_GUEST_VM -c -I"$M" -I"$DXMT/winemetal" -I"$DXMT/airconv" -o "$o" "$M/$f"
  objs="$objs $o"
done
# Direct3D forwarding (M2): the tables and stubs from native DXMT's headers, its dispatcher, and native DXMT itself.
G=$W/d3d-gen
D3D=$ROOT/eac-vm/d3d
I=$ROOT/build/dxmt-native/install/include-native
NT=$ROOT/eac-vm/dxmt-native/tests
[ -f "$ROOT/build/dxmt-native/install/lib/d3d12.dylib" ] || { echo "no native DXMT: run eac-vm/dxmt-native/build.sh" >&2; exit 1; }
python3 -I "$D3D/gen.py" --headers "$I" --rules "$D3D/rules.txt" --out "$G" calls
for f in "$D3D/host/dispatch.c" "$D3D/host/memory.c" "$D3D/host/events.c" "$D3D/host/window.c" "$G/calls.c" \
  "$G/ifaces.c"; do
  o="$W/obj/d3d-$(basename "$f").o"
  clang -O2 -std=gnu2x -Wall -c -I"$M" -I"$M/wm" -I"$G" -I"$I" -I"$DXMT/winemetal" -o "$o" "$f"
  objs="$objs $o"
done
for f in "$G/stubs.cpp" "$D3D/host/native.cpp" "$D3D/guest/formats.cpp"; do  # formats: the host checks data sizes too
  o="$W/obj/d3d-$(basename "$f").o"
  clang++ -O2 -std=c++17 -Wall -Werror -Wno-extern-c-compat -DWIDL_EXPLICIT_AGGREGATE_RETURNS -include "$NT/native-prelude.h" -I"$NT/include" \
    -I"$I/d3d12" -I"$I/windows" -I"$I/directx" -I"$I" -I"$DXMT/util" -I"$DXMT/d3d12" -I"$DXMT/d3d11" -I"$G" -I"$D3D/host" -I"$D3D/guest" -c -o "$o" "$f"
  objs="$objs $o"
done
rm -rf "$W/dxmt"
mkdir -p "$W/dxmt"
cp "$ROOT"/build/dxmt-native/install/lib/*.dylib "$ROOT/build/dxmt-native/install/lib/dxmt-replay" "$W/dxmt/"
# Native DXMT's translator key, which the launcher stamps the VM's recordings with (MicroVM.swift): not code, so beside
# dxmt/ (MacNeutron.app keeps it in Contents/Resources/vm with the scripts).
cp "$ROOT/build/dxmt-native/install/lib/translator" "$W/dxmt-translator"
clang -O2 -std=gnu2x -Wall -c -I"$W/libkrun/include" -o "$W/obj/input.c.o" "$ROOT/eac-vm/host/input.c"
clang -O2 -std=gnu2x -Wall -c -o "$W/obj/audio.c.o" "$ROOT/eac-vm/host/audio.c"
# The TPM's engine: libtpms (eac-vm/libtpms) on Homebrew's openssl@3, both linked in statically.
SSL=${OPENSSL:-$(brew --prefix openssl@3)}
cmp -s "$ROOT/eac-vm/libtpms/PIN" "$W/libtpms/PIN" || "$ROOT/eac-vm/libtpms/build.sh"
clang -O2 -std=gnu2x -Wall -c -I"$W/libkrun/include" -I"$W/libtpms/include" -I"$SSL/include" -o "$W/obj/tpm.c.o" \
  "$ROOT/eac-vm/host/tpm.c"
objs="$objs $W/obj/input.c.o $W/obj/audio.c.o $W/obj/tpm.c.o"
# shellcheck disable=SC2086
swiftc -O -o "$W/macneutron-vm" "$ROOT/eac-vm/host/macneutron-vm.swift" "$ROOT/eac-vm/metal/host/executor.c" \
  "$ROOT/eac-vm/metal/host/testops.c" "$ROOT/eac-vm/metal/ring.c" $objs \
  -import-objc-header "$ROOT/eac-vm/host/bridge.h" -Xcc -I"$W/libkrun/include" \
  "$W/libkrun/libkrun.1.dylib" -Xlinker -rpath -Xlinker @executable_path/libkrun -framework AudioToolbox \
  "$W/libtpms/libtpms.a" "$SSL/lib/libcrypto.a" -framework Security -lc++
make -s -C "$ROOT/eac-vm/metal/wm" metal-host
# The VM's folder is what MacNeutron's launcher runs a game from (Sources/MacNeutronCore/MicroVM.swift).
cp "$ROOT/eac-vm/run-guest.sh" "$W/run-guest.sh"
cp "$ROOT/eac-vm/guest/run-game.sh" "$W/run-game.sh"
# The TPMs' keys are keychain items that trust the binary that made them: signed with MACNEUTRON_SIGN_IDENTITY, a
# rebuild is still that binary; signed ad hoc, the first VM after each rebuild asks once to use the keychain.
codesign -s "${MACNEUTRON_SIGN_IDENTITY:--}" -f --entitlements "$ROOT/eac-vm/host/hv.plist" "$W/macneutron-vm"
echo "$W/macneutron-vm"
