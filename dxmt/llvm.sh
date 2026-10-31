# LLVM 15, by architecture, for wine-arm64/build.sh (arm64) and `make llvm-nolto` (sourced); dxmt/tools.sh links the host
# tools against it. The caller defines die, LLVM_TAG (dxmt/pins), UNIX_CPU and UNIX_LTO; this file sets no variables.
# Messages go to stderr.

# LLVM 15: static, with DXMT's CI flags but no assertions (they slowed every pipeline's translation, which Unreal does
# thousands of times a launch), and fix_llvm's fix; -O3 (Release), for the caller's UNIX_CPU, as ThinLTO bitcode when
# UNIX_LTO asks for it (winemetal.so's link optimizes the translator and LLVM together). Built once per install folder
# and recipe: <install>/.complete names the tag, fix, CPU and LTO it was built from (LLVM_TAG+fdivfmf+<cpu>+<lto>).
# Another tag's install, build folder and clone are started over; the same tag with another recipe keeps its clone,
# which is fixed and built again.
build_llvm() {  # build_llvm <arch> <install> <llvm-project>
  _bl_want="$LLVM_TAG+fdivfmf+${UNIX_CPU:-generic}+${UNIX_LTO:-nolto}" _bl_lto=Off
  [ -z "${UNIX_LTO:-}" ] || _bl_lto=Thin
  if [ -f "$2/.complete" ]; then
    # ponytail: an empty .complete predates the tag record; it was built from today's pin, so it's adopted, not rebuilt.
    if [ ! -s "$2/.complete" ]; then
      echo "$LLVM_TAG" > "$2/.complete"
      echo "dxmt: recorded LLVM $LLVM_TAG for $2" >&2
    fi
    [ "$(cat "$2/.complete")" != "$_bl_want" ] || return 0
    echo "dxmt: $2 was built from LLVM $(cat "$2/.complete"), not $_bl_want: building it again" >&2
    case $(cat "$2/.complete") in
      "$LLVM_TAG"|"$LLVM_TAG"+*) rm -rf "$2" "$2-build" ;;
      *) rm -rf "$2" "$2-build" "$3" ;;
    esac
  fi
  if [ ! -d "$3/llvm" ]; then  # cloned aside and moved into place, so an interrupted clone isn't taken for a source tree
    rm -rf "$3.tmp"
    git clone -q --depth 1 --branch "$LLVM_TAG" https://github.com/llvm/llvm-project.git "$3.tmp" \
      || die "can't clone llvm-project $LLVM_TAG"
    rm -rf "$3"; mv "$3.tmp" "$3"
  fi
  fix_llvm "$3"
  echo "dxmt: building LLVM $LLVM_TAG (a few minutes, once); log: $2.log" >&2
  { cmake -B "$2-build" -S "$3/llvm" -G Ninja \
      -DCMAKE_INSTALL_PREFIX="$2" -DCMAKE_OSX_ARCHITECTURES="$1" -DLLVM_HOST_TRIPLE="$1-apple-darwin" \
      -DLLVM_ENABLE_ASSERTIONS=Off -DLLVM_ENABLE_ZSTD=Off -DCMAKE_BUILD_TYPE=Release -DLLVM_TARGETS_TO_BUILD="" \
      -DLLVM_BUILD_TOOLS=Off -DLLVM_VERSION_PRINTER_SHOW_HOST_TARGET_INFO=Off -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DCMAKE_C_FLAGS="$UNIX_CPU" -DCMAKE_CXX_FLAGS="$UNIX_CPU" -DLLVM_ENABLE_LTO="$_bl_lto" &&
    cmake --build "$2-build" && cmake --install "$2-build"; } > "$2.log" 2>&1 \
    || die "LLVM build failed; see $2.log"
  echo "$_bl_want" > "$2/.complete"  # written last: an interrupted install is redone
}

# LLVM 15's IRBuilder::CreateFDivFMF folds and builds with the builder's own fast-math flags, not FMFSource's, as its
# siblings (CreateFMulFMF, ...) do: InstCombine's (-X/Y)+Z and Z-(-X/Y) folds turned DXIL's fast divides into precise
# ones (2-3% of SMITE 2's TAA kernel, GPU efficiency B6). Fixed as its siblings read it; a fixed tree is left alone.
fix_llvm() {  # fix_llvm <llvm-project>
  h="$1/llvm/include/llvm/IR/IRBuilder.h"
  grep -q 'MacNeutron: FMFSource' "$h" && return 0
  perl -0pi -e 's/(experimental_constrained_fdiv,\n\s+L, R, FMFSource, Name\);\n\n)(\s+)(if \(Value \*V = Folder\.FoldBinOpFMF\(Instruction::FDiv, L, R, FMF\)\))/$1$2FastMathFlags FMF = FMFSource->getFastMathFlags(); \/\/ MacNeutron: FMFSource\n$2$3/' "$h"
  grep -q 'MacNeutron: FMFSource' "$h" || die "can't fix CreateFDivFMF in $h"
}
