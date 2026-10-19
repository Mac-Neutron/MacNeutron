#!/bin/sh
# Builds our FEX for the guest (Linux arm64) in the builder: ~/fex-linux holds build/eac-vm-work/fex-linux (our FEX's
# macneutron branch + eac-vm/fex-patches), synced without .git or the test binaries. The prefix is /usr, as the image
# installs it: FEX reads its global config from <prefix>/share/fex-emu.
set -eu
SRC=${1:-$HOME/fex-linux}
# The p profile with "fex" in its flags (spec 2026-10-19-macneutron-performance-build §5): the features the guest's
# vCPU reports (/proc/cpuinfo, 2026-10-19), without SME.
F= L=
case " ${MACNEUTRON_P_FLAGS-} " in
  *" fex "*) [ "${MACNEUTRON_PROFILE:-std}" != p ] || F="-march=armv8.6-a+fp16+fp16fml+sha3+crypto" ;;
esac
# And its PGO (guest.profdata, with the guest Wine's; builder.sh's MACNEUTRON_PGO_GUEST): continuous mode needs the
# counters relocated at run time on Linux. The flags are always passed: CMake caches them, and a generate build's
# would outlive it (release.sh check_no_profile_runtime).
case ${MACNEUTRON_PGO_GUEST:-none} in
  generate) F="$F -fprofile-generate -mllvm -disable-vp=true -mllvm -runtime-counter-relocation"
            L=-fprofile-generate ;;
  use) F="$F -fprofile-use=$HOME/pgo/guest.profdata -mllvm -disable-vp=true -Wno-profile-instr-out-of-date -Wno-profile-instr-unprofiled" ;;
esac
set -- -DCMAKE_C_FLAGS="$F" -DCMAKE_CXX_FLAGS="$F" -DCMAKE_EXE_LINKER_FLAGS="$L" -DCMAKE_SHARED_LINKER_FLAGS="$L"
cmake -S "$SRC" -B "$SRC/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DBUILD_TESTING=OFF -DBUILD_THUNKS=OFF -DBUILD_FEXCONFIG=OFF \
  -DENABLE_LTO=ON -DENABLE_ASSERTIONS=OFF -DTUNE_CPU=none -DCMAKE_CXX_SCAN_FOR_MODULES=OFF "$@" > "$SRC/build-cmake.log"
nice cmake --build "$SRC/build" -j"$(nproc)" > "$SRC/build.log"
"$SRC/build/Bin/FEXGetConfig" --help > /dev/null 2>&1 || true
echo "$SRC/build/Bin"
