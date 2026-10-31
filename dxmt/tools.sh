# The DXIL host tools of wine-arm64/build.sh (arm64), against an LLVM 15 from dxmt/llvm.sh (sourced). The caller defines
# die, ROOT, MTLD3D_RUST (dxmt/pins), UNIX_CPU and UNIX_LTO, and has rustup's cargo and rustc on PATH
# (dxmt/mtld3d-tools.sh); this file sets no variables. Messages go to stderr. Kept apart from dxmt/llvm.sh so that an
# edit to a tool never changes the translator's key (lib.sh's translator_key hashes llvm.sh).

# The DXIL probe (spec §6), against the same LLVM. -fno-rtti matches LLVM's own build.
build_probe() {  # build_probe <arch> <llvm> <out-dir> <log-dir>
  /usr/bin/clang++ -arch "$1" -std=c++17 -O3 $UNIX_CPU $UNIX_LTO -fno-rtti -I"$2/include" "$ROOT/dxmt/tools/dxil-probe.cpp" -o "$3/dxil-probe" \
    -L"$2/lib" -lLLVMBitReader -lLLVMCore -lLLVMRemarks -lLLVMBitstreamReader -lLLVMBinaryFormat -lLLVMSupport \
    -lLLVMDemangle -lz -lcurses > "$4/dxil-probe.log" 2>&1 || die "dxil-probe failed to build; see $4/dxil-probe.log"
}

# The offline corpus tool (DXIL translator plan, Task 5), against the fork's Rust translator: airconv's static library
# (the production profile, as the images' winemetal links it, without the type oracle), built in <cargo-target-dir> from
# <dxmt-src>'s workspace (its crates fetched already: cargo runs offline), then linked with LLVM 15 and the shim's C++.
# The -lLLVM list is src/airconv/meson.build's llvm_deps, in order. No rustc target-cpu: as the images' (step 4a P10).
build_translate() {  # build_translate <arch> <llvm> <dxmt-src> <cargo-target-dir> <out-dir> <log-dir>
  [ "$1" = arm64 ] || die "build_translate: arm64 only, not $1"  # ponytail: the one caller; a Rust target per arch otherwise
  {
    (cd "$3" && AIRCONV_LLVM="$2" AIRCONV_XCRUN="$ROOT/wine-arm64/tools/xcrun-metal.sh" CARGO_TARGET_DIR="$4" \
      cargo "+$MTLD3D_RUST" rustc --locked --offline -p airconv --profile production --crate-type staticlib \
      --target aarch64-apple-darwin) \
    && /usr/bin/clang++ -arch "$1" -std=c++20 -O3 $UNIX_CPU $UNIX_LTO -fno-rtti -fno-exceptions -fobjc-arc -I"$2/include" \
      -I"$3/src/airconv" "$ROOT/dxmt/tools/dxil-translate.mm" -o "$5/dxil-translate" \
      "$4/aarch64-apple-darwin/production/libairconv.a" \
      -L"$2/lib" -lLLVMPasses -lLLVMTarget -lLLVMObjCARCOpts -lLLVMCoroutines -lLLVMipo -lLLVMInstrumentation \
      -lLLVMVectorize -lLLVMLinker -lLLVMIRReader -lLLVMAsmParser -lLLVMFrontendOpenMP -lLLVMScalarOpts \
      -lLLVMInstCombine -lLLVMAggressiveInstCombine -lLLVMTransformUtils -lLLVMBitWriter -lLLVMAnalysis \
      -lLLVMProfileData -lLLVMSymbolize -lLLVMDebugInfoPDB -lLLVMDebugInfoMSF -lLLVMDebugInfoDWARF -lLLVMObject \
      -lLLVMTextAPI -lLLVMMCParser -lLLVMMC -lLLVMDebugInfoCodeView -lLLVMBitReader -lLLVMCore -lLLVMRemarks \
      -lLLVMBitstreamReader -lLLVMBinaryFormat -lLLVMSupport -lLLVMDemangle -lm -lz -lcurses -lxml2 \
      -framework Metal -framework Foundation
  } > "$6/dxil-translate.log" 2>&1 || die "dxil-translate failed to build; see $6/dxil-translate.log"
}
