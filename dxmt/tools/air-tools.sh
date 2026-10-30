#!/bin/sh
# air-tools.sh <llvm15-prefix> <llvm23-prefix|-> <out-dir>
# DXMTRS step 4W's host tools, built into <out-dir>: air15 and dxil-probe-15 against LLVM 15, and dxil-probe-23 against
# LLVM 23 when a second prefix is given. The two LLVM 15 tools link the way dxmt/llvm.sh's build_probe does (its hand
# list: the LLVM 15 install has no llvm-config). dxil-probe-23 takes llvm-config's libraries, because LLVM 23's Core
# also needs LLVMTargetParser. Not sourced from llvm.sh: that file is a translator-key input and fixes its output name.
set -eu
[ $# -eq 3 ] || { echo "usage: sh air-tools.sh <llvm15-prefix> <llvm23-prefix|-> <out-dir>" >&2; exit 1; }
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$3"
cxx="/usr/bin/clang++ -arch arm64 -std=c++17 -O2 -fno-rtti -Wall"

l15="-L$1/lib -lLLVMBitReader -lLLVMCore -lLLVMRemarks -lLLVMBitstreamReader -lLLVMBinaryFormat -lLLVMSupport
  -lLLVMDemangle -lz -lcurses"
$cxx -I"$1/include" "$here/air15.cpp" -o "$3/air15" $l15
$cxx -I"$1/include" "$here/dxil-probe.cpp" -o "$3/dxil-probe-15" $l15

if [ "$2" != - ]; then
  cfg="$2/bin/llvm-config"
  /usr/bin/clang++ -arch arm64 -O2 -Wall $("$cfg" --cxxflags) "$here/dxil-probe.cpp" -o "$3/dxil-probe-23" \
    -L"$2/lib" $("$cfg" --libs bitreader core) $("$cfg" --system-libs --link-static)
fi
