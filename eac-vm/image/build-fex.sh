#!/bin/sh
# Builds our FEX for the guest (Linux arm64) in the builder: ~/fex-linux holds build/eac-vm-work/fex-linux (our FEX's
# macneutron branch + eac-vm/fex-patches), synced without .git or the test binaries. The prefix is /usr, as the image
# installs it: FEX reads its global config from <prefix>/share/fex-emu.
set -eu
SRC=${1:-$HOME/fex-linux}
cmake -S "$SRC" -B "$SRC/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DBUILD_TESTING=OFF -DBUILD_THUNKS=OFF -DBUILD_FEXCONFIG=OFF \
  -DENABLE_LTO=ON -DENABLE_ASSERTIONS=OFF -DTUNE_CPU=none -DCMAKE_CXX_SCAN_FOR_MODULES=OFF > "$SRC/build-cmake.log"
nice cmake --build "$SRC/build" -j"$(nproc)" > "$SRC/build.log"
"$SRC/build/Bin/FEXGetConfig" --help > /dev/null 2>&1 || true
echo "$SRC/build/Bin"
