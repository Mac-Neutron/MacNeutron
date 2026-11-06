#!/bin/sh
# Builds build/vm-work/macneutron-vm against vm/libkrun's build, with the Metal transport's executor
# (vm/metal/host), Direct3D forwarding's dispatcher and stubs (vm/d3d, generated from native DXMT's headers)
# and the TPM's engine (vm/libtpms), signed with the hypervisor entitlement; build/vm-work/metal-host,
# MacNeutron's winemetal.so beside stand-ins for its Wine libraries (vm/metal/wm), which the executor loads; and
# build/vm-work/dxmt, native DXMT (vm/dxmt-native/build.sh), which the first Direct3D record loads.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/vm-work
# Sources go to the compilers relative to the repository: a PGO profile names a file's static functions by the path
# it was compiled from, and a profile must name no home folder (and match between its two builds).
cd "$ROOT"
rel() { printf '%s' "${1#"$ROOT"/}"; }
# The p profile with "host" in its flags (spec 2026-10-19-macneutron-performance-build §5): -O3 for the M5, ThinLTO.
. "$ROOT/wine-arm64/lib.sh"
HOPT="-O3 -mcpu=${MACNEUTRON_CPU:-apple-m5} -flto=thin"
# And its PGO (host.profdata, with native DXMT's): instrumented, or built with the profile.
PGO=$(pgo_cflags host)
HOPT="$HOPT${PGO:+ $PGO}"
# The Rust host's PGO is rustc's spelling of the same profile (mynewt: -Cprofile-generate / -Cprofile-use). It rides the
# same $ROOT/pgo/host.profdata; clang and rustc share one LLVM, so the profile format matches. No value profiling, as
# pgo_cflags disables it (-disable-vp), and rustc's default matches, so a profile with no value sites fits.
RUST_PGO=
case $(pgo_mode host) in
  generate) RUST_PGO="-Cprofile-generate=$ROOT/pgo" ;;
  use) RUST_PGO="-Cprofile-use=$ROOT/pgo/host.profdata -Cllvm-args=-pgo-warn-missing-function" ;;
  *) RUST_PGO= ;;
esac
# rustup's cargo and rustc proxies sit beside the rustup binary (Homebrew keeps them off PATH): the Rust host builds
# with them, as DXMTRS's own builds do (dxmt/mtld3d-tools.sh).
RUSTUP_PROXIES=$(dirname "$(readlink -f "$(command -v rustup)")")
[ -x "$RUSTUP_PROXIES/cargo" ] || { echo "no cargo beside rustup in $RUSTUP_PROXIES" >&2; exit 1; }
PATH="$RUSTUP_PROXIES:$PATH"
# The winemetal handler's C and Objective-C, compiled with clang: swiftc takes the objects.
DXMT=${BUILD_DIR:-$ROOT/build}/wine-arm64-src/dxmt/src
M=$ROOT/vm/metal
mkdir -p "$W/obj"
objs=
for f in wm/calls.c wm/cmdlist.c wm/marshal.c wm/slots.c wm/host/htable.c wm/host/loader.c wm/host/invoke.m wm/host/memory.m \
  wm/host/events.m wm/host/present.m wm/host/dispatch.m; do
  o="$W/obj/$(basename "$f").o"
  clang $HOPT -std=gnu2x -Wall -DMN_WM_HOST -DDXMT_GUEST_VM -c -I"$M" -I"$DXMT/winemetal" -I"$DXMT/airconv" -o "$o" "$(rel "$M/$f")"
  objs="$objs $o"
done
# Direct3D forwarding (M2): the tables and stubs from native DXMT's headers, its dispatcher, and native DXMT itself.
G=$W/d3d-gen
D3D=$ROOT/vm/d3d
I=${BUILD_DIR:-$ROOT/build}/dxmt-native/install/include-native
NT=$ROOT/vm/dxmt-native/tests
[ -f "${BUILD_DIR:-$ROOT/build}/dxmt-native/install/lib/d3d12.dylib" ] || { echo "no native DXMT: run vm/dxmt-native/build.sh" >&2; exit 1; }
python3 -I "$D3D/gen.py" --headers "$I" --rules "$D3D/rules.txt" --out "$G" calls
for f in "$D3D/host/dispatch.c" "$D3D/host/memory.c" "$D3D/host/events.c" "$D3D/host/window.c" "$G/calls.c" \
  "$G/ifaces.c"; do
  o="$W/obj/d3d-$(basename "$f").o"
  clang $HOPT -std=gnu2x -Wall -c -I"$M" -I"$M/wm" -I"$G" -I"$I" -I"$DXMT/winemetal" -o "$o" "$(rel "$f")"
  objs="$objs $o"
done
for f in "$G/stubs.cpp" "$D3D/host/native.cpp" "$D3D/guest/formats.cpp"; do  # formats: the host checks data sizes too
  o="$W/obj/d3d-$(basename "$f").o"
  clang++ $HOPT -std=c++17 -Wall -Werror -Wno-extern-c-compat -DWIDL_EXPLICIT_AGGREGATE_RETURNS -include "$NT/native-prelude.h" -I"$NT/include" \
    -I"$I/d3d12" -I"$I/windows" -I"$I/directx" -I"$I" -I"$DXMT/util" -I"$DXMT/d3d12" -I"$DXMT/d3d11" -I"$G" -I"$D3D/host" -I"$D3D/guest" -c -o "$o" "$(rel "$f")"
  objs="$objs $o"
done
rm -rf "$W/dxmt"
mkdir -p "$W/dxmt"
cp "${BUILD_DIR:-$ROOT/build}"/dxmt-native/install/lib/*.dylib "${BUILD_DIR:-$ROOT/build}/dxmt-native/install/lib/dxmt-replay" "$W/dxmt/"
# Native DXMT's translator key, which the launcher stamps the VM's recordings with (MicroVM.swift): not code, so beside
# dxmt/ (MacNeutron.app keeps it in Contents/Resources/vm with the scripts).
cp "${BUILD_DIR:-$ROOT/build}/dxmt-native/install/lib/translator" "$W/dxmt-translator"
clang $HOPT -std=gnu2x -Wall -c -I"$W/libkrun/include" -o "$W/obj/input.c.o" vm/host/input.c
clang $HOPT -std=gnu2x -Wall -c -o "$W/obj/audio.c.o" vm/host/audio.c
# The TPM's engine: libtpms (vm/libtpms) on Homebrew's openssl@3, both linked in statically.
SSL=${OPENSSL:-$(brew --prefix openssl@3)}
cmp -s "$ROOT/vm/libtpms/PIN" "$W/libtpms/PIN" || "$ROOT/vm/libtpms/build.sh"
clang $HOPT -std=gnu2x -Wall -c -I"$W/libkrun/include" -I"$W/libtpms/include" -I"$SSL/include" -o "$W/obj/tpm.c.o" \
  vm/host/tpm.c
objs="$objs $W/obj/input.c.o $W/obj/audio.c.o $W/obj/tpm.c.o"
# The executor's C goes through clang so it always gets $HOPT (swiftc takes no clang optimisation flags).
csrc="vm/metal/host/executor.c vm/metal/host/testops.c vm/metal/ring.c"
for f in $csrc; do
  clang $HOPT -std=gnu2x -Wall -c -I"$W/libkrun/include" -o "$W/obj/$(basename "$f").o" "$f"
  objs="$objs $W/obj/$(basename "$f").o"
done
# The Rust host (src/main.rs, a 1:1 translation of macneutron-vm.swift): the AppKit shim is compiled apart so the crate
# stays Objective-C-free, and the C helpers and libkrun are linked through rustc's link-args. The C objects keep clang's
# $HOPT (so their PGO and ThinLTO match the rest of the host); rustc's link step drives Apple's ld, which reads them.
clang $HOPT -c -o "$W/obj/appkit_shim.o" vm/host/appkit_shim.m
objs="$objs $W/obj/appkit_shim.o"
# Every object and library rustc must hand to ld, each as its own -Clink-arg (the objects are space-separated in $objs).
link_args=
for o in $objs; do link_args="$link_args -Clink-arg=$o"; done
link_args="$link_args -Clink-arg=$W/libkrun/libkrun.1.dylib"
link_args="$link_args -Clink-arg=-Wl,-rpath,@executable_path/libkrun"
link_args="$link_args -Clink-arg=$W/libtpms/libtpms.a -Clink-arg=$SSL/lib/libcrypto.a"
link_args="$link_args -Clink-arg=-framework -Clink-arg=AudioToolbox"
link_args="$link_args -Clink-arg=-framework -Clink-arg=Security"
link_args="$link_args -Clink-arg=-framework -Clink-arg=AppKit"
link_args="$link_args -Clink-arg=-lc++"
# The host's PGO (RUST_PGO) rides the same $ROOT/pgo/host.profdata as the clang objects. The C objects are LLVM
# bitcode (-flto=thin) and are passed through -Clink-arg, which cargo does not track: touch the source so the link
# always reruns (the Swift host relinked on every build too -- swiftc was the link step).
touch "$ROOT/vm/host/src/main.rs"
# A member of the root workspace, built in its own target folder: these RUSTFLAGS would otherwise rebuild the shared
# target/ that make build uses (and BUILD_DIR's p profile keeps its own).
RUSTFLAGS="-Ctarget-cpu=${MACNEUTRON_CPU:-apple-m5} ${RUST_PGO:+$RUST_PGO }$link_args" \
  cargo build --release --manifest-path "$ROOT/vm/host/Cargo.toml" --target-dir "$W/cargo-target"
cp "$W/cargo-target/release/macneutron-vm" "$W/macneutron-vm"
make -s -C "$ROOT/vm/metal/wm" metal-host
# The VM's folder is what MacNeutron's launcher runs a game from (crates/macneutron-core/src/micro_vm.rs).
cp "$ROOT/vm/run-guest.sh" "$W/run-guest.sh"
cp "$ROOT/vm/guest/run-game.sh" "$W/run-game.sh"
# The TPMs' keys are keychain items that trust the binary that made them: signed with MACNEUTRON_SIGN_IDENTITY, a
# rebuild is still that binary; signed ad hoc, the first VM after each rebuild asks once to use the keychain.
codesign -s "${MACNEUTRON_SIGN_IDENTITY:--}" -f --entitlements "$ROOT/vm/host/hv.plist" "$W/macneutron-vm"
echo "$W/macneutron-vm"
