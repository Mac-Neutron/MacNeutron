#!/bin/sh
# Builds MacNeutron's own llvm-mingw into build/dxmt-src/llvm-mingw-own, once per recipe (this script, dxmt/pins'
# LLVM_MINGW_* and wine-arm64/patches/llvm; llvm-mingw-own/.recipe), and prints its bin folder. dxmt/toolchain.sh
# calls it unless MACNEUTRON_TOOLCHAIN=upstream. Messages go to stderr; a full build takes about 40 minutes.
#
# Why our own: the release's LLVM can't tune Windows-side code for Apple CPUs. Apple tuning aligns fusible CMP/CSEL
# and FCMP/FCSEL pairs to 64 bytes inside functions (AArch64CodeLayoutOpt), which leaves a function's length unknown
# when MC writes its ARM64 SEH unwind info ("Failed to evaluate function length in SEH unwind info");
# patches/llvm/0001 keeps every tuning but that one on Windows. And lld can't link ARM64EC bitcode against static
# ARM64EC libraries ("undefined symbol: ... (EC symbol)", libc++'s): patches/llvm/0002 loads them, so ARM64EC code
# (FEX's DLL, DXMT's) gets ThinLTO too; patches/llvm/0003 lets LTO read back the hybrid_patchable attribute Wine's
# headers put on ARM64EC functions ("Unknown attribute kind (95)"). Built for this Mac's CPU (-mcpu=apple-m5, MACNEUTRON_CPU), -O3 (LLVM's Release) with ThinLTO, without
# LLDB or clang-tools-extra; runtimes for aarch64, arm64ec (ARM64X with aarch64), x86_64 and i686 (Wine's ARM64EC
# modules' x64 sources, the x64 test programs), without Control Flow Guard (nothing here asks for -mguard=cf), the
# arm64 C++ runtime and compiler-rt tuned for the CPU too.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/dxmt/pins"
. "$ROOT/dxmt/lib.sh"
B="${BUILD_DIR:-$ROOT/build}"
S="$B/llvm-mingw-src"
T="$B/dxmt-src/llvm-mingw-own"
CPU="${MACNEUTRON_CPU:-apple-m5}"
[ "$CPU" != generic ] || CPU=apple-m1
recipe=$( { cat "$0" "$ROOT"/wine-arm64/patches/llvm/*.patch; echo "$LLVM_MINGW_COMMIT $LLVM_MINGW_LLVM $CPU"; } \
  | shasum -a 256 | cut -d ' ' -f 1)
if [ -x "$T/bin/aarch64-w64-mingw32-clang" ] && [ "$(cat "$T/.recipe" 2> /dev/null)" = "$recipe" ]; then
  echo "$T/bin"
  exit 0
fi
echo "dxmt: building MacNeutron's llvm-mingw ($LLVM_MINGW_COMMIT, $LLVM_MINGW_LLVM, $CPU; log: $S/build.log)" >&2
mkdir -p "$S"
: > "$S/build.log"
log() { "$@" >> "$S/build.log" 2>&1 || die "llvm-mingw: '$*' failed; see $S/build.log"; }
# The scripts at the pinned commit of llvm-mingw's master, fetched alone (a branch head moves); llvm-project, which
# build-llvm.sh clones into it, stays.
if [ "$(git -C "$S/scripts" rev-parse HEAD 2> /dev/null)" != "$LLVM_MINGW_COMMIT" ]; then
  [ -d "$S/scripts/.git" ] || { log git init -q "$S/scripts"; log git -C "$S/scripts" remote add origin "$LLVM_MINGW_REPO"; }
  log git -C "$S/scripts" fetch -q --depth 1 origin "$LLVM_MINGW_COMMIT"
  log git -C "$S/scripts" checkout -q -f FETCH_HEAD
fi
cd "$S/scripts"
git checkout -q -- .  # the runtime scripts' edits below are redone each time
# LLVM at the tag llvm-mingw pins, our patches on top (a fresh checkout drops older ones).
log env LLVM_VERSION="$LLVM_MINGW_LLVM" CHECKOUT_ONLY=1 SYNC=1 ./build-llvm.sh "$T.tmp"
git -C llvm-project checkout -q -f "$LLVM_MINGW_LLVM"
for p in "$ROOT"/wine-arm64/patches/llvm/*.patch; do
  log git -C llvm-project -c user.name=MacNeutron -c user.email=macneutron@localhost am -q "$p"
done
# The arm64 runtimes (ARM64X: aarch64 with arm64ec) for the CPU: libc++ and compiler-rt take it beside their own
# flags. The patched LLVM builds them Apple-tuned.
sed -i '' 's/        EXTRA_CFLAGS="\$ARM64X_FLAGS"/        EXTRA_CFLAGS="\$ARM64X_FLAGS -mcpu='"$CPU"'"/' build-libcxx.sh
sed -i '' 's/        FLAGS="\$ARM64X_FLAGS"/        FLAGS="\$ARM64X_FLAGS -mcpu='"$CPU"'"/' build-compiler-rt.sh
grep -q "mcpu=$CPU" build-libcxx.sh && grep -q "mcpu=$CPU" build-compiler-rt.sh \
  || die "llvm-mingw $LLVM_MINGW_COMMIT's runtime scripts changed: no place for -mcpu=$CPU"
# No build path in the C++ runtime every Windows-side binary links: libc++abi's checks name their source files
# (__FILE__), which named this tree's llvm-project folder in every DXMT DLL (a release refuses a build path).
map="-ffile-prefix-map=$S/scripts/llvm-project=/llvm-project"
sed -i '' 's|-DCMAKE_C_FLAGS_INIT="$CFGUARD_CFLAGS $EXTRA_CFLAGS"|-DCMAKE_C_FLAGS_INIT="$CFGUARD_CFLAGS $EXTRA_CFLAGS '"$map"'"|;
  s|-DCMAKE_CXX_FLAGS_INIT="$CFGUARD_CFLAGS $EXTRA_CFLAGS"|-DCMAKE_CXX_FLAGS_INIT="$CFGUARD_CFLAGS $EXTRA_CFLAGS '"$map"'"|' \
  build-libcxx.sh
[ "$(grep -cF -- "$map" build-libcxx.sh)" = 2 ] \
  || die "llvm-mingw $LLVM_MINGW_COMMIT's build-libcxx.sh changed: no place for $map"
export TOOLCHAIN_ARCHS="i686 x86_64 aarch64 arm64ec"
rm -rf "$T.tmp"
# The compilers themselves: -O3 (Release), ThinLTO, for this CPU. Only LLVM's own build sees these flags; the
# runtimes' cross builds below don't.
log env CFLAGS="-mcpu=$CPU" CXXFLAGS="-mcpu=$CPU" ./build-llvm.sh "$T.tmp" --thinlto --disable-lldb \
  --disable-clang-tools-extra
log ./strip-llvm.sh "$T.tmp"
log ./install-wrappers.sh "$T.tmp"
log ./build-mingw-w64-tools.sh "$T.tmp"
log ./build-mingw-w64.sh "$T.tmp" --disable-cfguard
log ./build-compiler-rt.sh "$T.tmp" --disable-cfguard
log ./build-libcxx.sh "$T.tmp" --disable-cfguard
log ./build-mingw-w64-libraries.sh "$T.tmp" --disable-cfguard
echo "$recipe" > "$T.tmp/.recipe"
rm -rf "$T"
mv "$T.tmp" "$T"
echo "$T/bin"
