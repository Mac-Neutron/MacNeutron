# Shared by wine-arm64/build.sh, bundle.sh, export.sh, tests and the release scripts (sourced). Messages go to stderr.
die() { echo "wine-arm64: $*" >&2; exit 1; }

# Tools the build needs, collected so one run names every missing one (die_if_missing); never installed here.
missing=""
need_tool() {  # need_tool <command> <brew formula> [keg]
  # "keg": the formula is keg-only, so its bin goes first on PATH and only that one counts (macOS ships bison 2.3).
  if [ "${3:-}" = keg ]; then
    keg=$(brew --prefix "$2" 2> /dev/null) || keg=
    if [ -n "$keg" ] && [ -x "$keg/bin/$1" ]; then PATH="$keg/bin:$PATH"; return 0; fi
  elif command -v "$1" > /dev/null 2>&1; then
    return 0
  fi
  missing="$missing, $1 (brew install $2)"
}
die_if_missing() { [ -z "$missing" ] || die "missing tools: ${missing#, }"; }

# What a source tree is: pinned (no tree yet: fetch it), applied (HEAD is the commit recorded in the applied file,
# nothing else changed: the fork commit is the truth), development (anything else, including a stash, a second branch
# or a second worktree: build it as it is) or, when a series file and the current pin are given, reapply (an applied
# tree fetched at another pin: it holds no work of its own, so build.sh deletes it and starts over).
build_mode() {  # build_mode <src-dir> <applied-file> [<series-file> <series>]
  [ -d "$1" ] || { echo pinned; return 0; }
  if [ -n "$(git -C "$1" status --porcelain)" ] || [ "$(git -C "$1" rev-parse HEAD)" != "$(cat "$2" 2> /dev/null)" ] \
    || [ -n "$(git -C "$1" stash list)" ] || [ -n "$(git -C "$1" for-each-ref refs/heads | sed -n 2p)" ] \
    || [ -n "$(git -C "$1" worktree list | sed -n 2p)" ]; then
    echo development
  elif [ $# -ge 4 ] && [ "$(cat "$3" 2> /dev/null)" != "$4" ]; then
    echo reapply
  else
    echo applied
  fi
}

# SHA-256 over the files' contents in order, then the signing identity (a different identity is a different build).
stamp_of() {  # stamp_of <file>...
  for f in "$@"; do [ -f "$f" ] || die "no such build input: $f"; done
  { cat "$@"; printf '%s' "${MACNEUTRON_SIGN_IDENTITY:-}"; } | shasum -a 256 | cut -d ' ' -f 1
}

# Each tree's series: the commit of our fork it is pinned to (the fork's main carries our changes), and the tree's
# build_mode against it (build.sh, bundle.sh --release and release.sh agree through these). The caller sets ROOT;
# BUILD_DIR replaces build/.
tree_series() {  # tree_series <wine|fex|dxmt|lsteamclient>
  case $1 in
    wine) ( . "$ROOT/wine-arm64/pins"; echo "$WINE_COMMIT" ) ;;
    fex) ( . "$ROOT/wine-arm64/pins"; echo "$FEX_COMMIT" ) ;;
    dxmt) ( . "$ROOT/dxmt/pins"; echo "$DXMT_COMMIT" ) ;;
    lsteamclient) ( . "$ROOT/wine-arm64/deps.pins"; echo "$LSTEAMCLIENT_COMMIT" ) ;;
    *) die "no tree $1" ;;
  esac
}
tree_mode() {  # tree_mode <tree>
  _tm_s="${BUILD_DIR:-$ROOT/build}/wine-arm64-src"
  build_mode "$_tm_s/$1" "$_tm_s/$1.applied" "$_tm_s/$1.series" "$(tree_series "$1")"
}

# A bundle's licenses/SOURCE (ship-base spec §4, arm64 release spec §14): the inputs it is built from, each tree's
# series (its pin; dev unless the tree is applied), the submodules and tarballs. build.sh writes the development one, naming
# <mac>; bundle.sh --release writes the release bundle's own, naming HEAD.
write_source() {  # write_source <out> <mac>
  (
    . "$ROOT/wine-arm64/pins"; . "$ROOT/dxmt/pins"; . "$ROOT/wine-arm64/deps.pins"
    S="${BUILD_DIR:-$ROOT/build}/wine-arm64-src"
    series() { if [ "$(tree_mode "$1")" = applied ]; then tree_series "$1"; else echo dev; fi; }
    echo "MACNEUTRON_COMMIT=$2"
    echo "WINE_COMMIT=$WINE_COMMIT"
    echo "WINE_SERIES=$(series wine)"
    echo "FEX_COMMIT=$FEX_COMMIT"
    echo "FEX_SERIES=$(series fex)"
    # " <sha> <path> (<describe>)", the first character "+" or "-" when the checkout differs from FEX's record.
    git -C "$S/fex" submodule status | awk '{ c = $1; sub(/^[-+U]/, "", c); n = $2; sub(/.*\//, "", n) }
      n ~ /^(fmt|range-v3|rpmalloc|unordered_dense|xxhash|cpp-optparse)$/ { print "FEX_SUBMODULE_" n "=" c }'
    echo "DXMT_COMMIT=$DXMT_COMMIT"
    echo "DXMT_SERIES=$(series dxmt)"
    git -C "$S/dxmt" submodule status | awk '{ c = $1; sub(/^[-+U]/, "", c); n = $2; sub(/.*\//, "", n)
      print "DXMT_SUBMODULE_" n "=" c }'  # external/nvapi, include/native/directx
    echo "LLVM_TAG=$LLVM_TAG"
    echo "LLVM_MINGW_SHA256=$LLVM_MINGW_SHA256"
    echo "LSTEAMCLIENT_COMMIT=$LSTEAMCLIENT_COMMIT"
    echo "LSTEAMCLIENT_SERIES=$(series lsteamclient)"
    deps_pins  # <NAME>_REPO and <NAME>_COMMIT, GMP's <NAME>_URL and <NAME>_SHA256
  ) > "$1"
}

# The FreeType, gnutls, libtasn1, nettle, GMP and FFmpeg pins (<NAME>_REPO and <NAME>_COMMIT; GMP's <NAME>_URL and
# <NAME>_SHA256): build.sh's deps input and SOURCE's.
# The p profile (spec 2026-10-19-macneutron-performance-build §5): the components built with its optimisation flags,
# all by default; MACNEUTRON_P_FLAGS names fewer for the benches (empty: none). Compile-outs apply to every p build.
P_FLAGS_DEFAULT="wine fex host krun dxmt"
p_flag() {  # p_flag <component>: true when the p profile builds <component> with its optimisation flags
  [ "${MACNEUTRON_PROFILE:-std}" = p ] || return 1
  case " ${MACNEUTRON_P_FLAGS-$P_FLAGS_DEFAULT} " in *" $1 "*) return 0 ;; esac
  return 1
}

# PGO under the p profile (spec 2026-10-19-macneutron-performance-build §7): MACNEUTRON_PGO=generate builds
# instrumented binaries (they write LLVM_PROFILE_FILE's profiles); unset, a component uses pgo/<profile>.profdata when it
# exists. Profiles by toolchain: host (Apple clang), guest (the builder's clang); libkrun has none (its instrumented
# build crashed in zstd's C code under rustc's profile runtime).
pgo_mode() {  # pgo_mode <profile>: generate, use or none
  [ "${MACNEUTRON_PROFILE:-std}" = p ] || { echo none; return; }
  case ${MACNEUTRON_PGO:-} in
    generate) echo generate ;;
    none) echo none ;;
    *) if [ -f "$ROOT/pgo/$1.profdata" ]; then echo use; else echo none; fi ;;
  esac
}
# The compiler flags for pgo_mode's answer: clang's, for Apple's or the builder's. Continuous mode (%c, which a killed
# process's profile survives) has no value profiling: its records are allocated at run time, outside the mapped file,
# and the instrumented code crashed in __llvm_profile_instrument_target; the profile-using build matches it (a profile
# with no value sites against code that expects them is an error under -Werror). On Linux it needs the counters relocated.
pgo_cflags() {  # pgo_cflags <profile> [linux]
  case $(pgo_mode "$1") in
    generate) printf '%s' "-fprofile-generate -mllvm -disable-vp=true${2:+ -mllvm -runtime-counter-relocation}" ;;
    use) printf '%s' "-fprofile-use=$ROOT/pgo/$1.profdata -mllvm -disable-vp=true -Wno-profile-instr-out-of-date -Wno-profile-instr-unprofiled" ;;
  esac
}

deps_pins() { LC_ALL=C /usr/bin/grep -E '^(FREETYPE|GNUTLS|LIBTASN1|NETTLE|GMP|FFMPEG)_' "$ROOT/wine-arm64/deps.pins"; }

# A release ships no build path (arm64 release Ruling 20): the files under <dir> that contain, as bytes, the repository's
# path, the build folder's (BUILD_DIR can move it outside the repository) or the home folder's. The first ten, relative
# to <dir>. The caller sets ROOT.
build_paths() {  # build_paths <dir>; fails closed (no HOME, or grep can't read <dir>)
  [ -n "${HOME:-}" ] || die "HOME is not set"
  _bp_st=0
  _bp_out=$(LC_ALL=C /usr/bin/grep -rlaF -e "$ROOT" -e "${BUILD_DIR:-$ROOT/build}" -e "$HOME/" "$1") || _bp_st=$?
  [ "$_bp_st" -le 1 ] || die "grep failed ($_bp_st) on $1"
  [ -z "$_bp_out" ] || printf '%s\n' "$_bp_out" | head -n 10 | while IFS= read -r _bp; do echo "${_bp#"$1"/}"; done
}

# The App ID the entitled loader is signed for; a provisioning profile has to be for it (spec §7.2).
APP_ID=49QMZXLR8S.net.authspot.macneutron.wine

# A decoded provisioning profile (security cms -D) grants this App ID, the cross-architecture entitlement, and has not
# expired. Messages name what is wrong; the first problem found stops it.
check_profile_plist() {  # check_profile_plist <decoded-plist>
  pb() { /usr/libexec/PlistBuddy -c "Print :$1" "$2" 2> /dev/null; }
  id=$(pb Entitlements:com.apple.application-identifier "$1") || id="(no application identifier)"
  [ "$id" = "$APP_ID" ] || die "profile is for $id, not $APP_ID"
  [ "$(pb Entitlements:com.apple.developer.cross-architecture-support "$1" || true)" = true ] \
    || die "profile lacks com.apple.developer.cross-architecture-support"
  # PlistBuddy prints the date in local time ("Tue Sep 27 22:29:58 EST 2044"): off by hours at worst, fine for expiry.
  # Always in English, so date reads it in English too: LC_ALL=C (under fr_CA the month and day names don't parse).
  exp=$(pb ExpirationDate "$1") || die "profile has no ExpirationDate"
  at=$(LC_ALL=C date -j -f '%a %b %d %T %Z %Y' "$exp" +%s 2> /dev/null) || die "can't read the profile's ExpirationDate: $exp"
  [ "$at" -gt "$(date +%s)" ] || die "profile expired on $exp"
}

# The signing variables are set and name something usable: build.sh checks this before fetching or building (a missing
# variable costs seconds, not a build), bundle.sh before assembling. There is no ad-hoc mode: an unentitled loader can't boot.
check_signing() {
  [ -n "${MACNEUTRON_SIGN_IDENTITY:-}" ] || die "set MACNEUTRON_SIGN_IDENTITY"
  [ -n "${MACNEUTRON_PROVISIONING_PROFILE:-}" ] || die "set MACNEUTRON_PROVISIONING_PROFILE"
  plist=$(mktemp)
  security cms -D -i "$MACNEUTRON_PROVISIONING_PROFILE" > "$plist" 2> /dev/null \
    || { rm -f "$plist"; die "$MACNEUTRON_PROVISIONING_PROFILE is not a provisioning profile"; }
  ( check_profile_plist "$plist" ) || { rm -f "$plist"; exit 1; }  # check_profile_plist already said what is wrong
  rm -f "$plist"
}

# The named sections of a TOML file, without comment lines and blank ones: the cargo settings translator_key hashes.
toml_sections() {  # toml_sections <file> <[section]>...
  _ts_f=$1; shift
  awk -v want="$*" '
    /^[[:space:]]*\[/ { s = $0; sub(/[[:space:]]*(#.*)?$/, "", s); sub(/^[[:space:]]*/, "", s); on = index(" " want " ", " " s " ") > 0 }
    on && !/^[[:space:]]*(#|$)/ { print }
  ' "$_ts_f"
}

# The key of DXMT's shader translator (arm64 release Ruling 46): DXMT's translation cache (ShaderCacheVersion, DXMT
# patch 0003) and the launcher's replay stamp (DXMT/translator) follow it, not DXMT's git version, which every re-fetch
# changes (git am makes new commits) and an uncommitted edit doesn't. It hashes what changes translated output, as the
# working tree has it (patched, committed or not, untracked files too), the buildtype, LLVM (the pin and dxmt/llvm.sh's
# recipe) and the compilers (Apple clang, metal through tools/xcrun-metal.sh). Relative paths only: the same tree
# anywhere has the same key. The caller sets ROOT and LLVM_TAG (dxmt/pins) and, for a tree with the Rust translator,
# MTLD3D_RUST (dxmt/pins) with rustup's cargo and rustc on PATH (dxmt/mtld3d-tools.sh).
#  - A tree with src/airconv/Cargo.toml (translator 2, DXMTRS step 4a): airconv (src/airconv, its shim, passes and .metal
#    helpers), d3d11's compile arguments (src/d3d11/d3d11_shader.cpp: its cache entries key on the variant's fields, not on
#    the argument chain built from them, unlike d3d12's) and the PE side of the airconv calls
#    (src/winemetal/airconv_thunks.[ch]; the unix side's 64-bit thunks pass the pointers through), the cargo settings that
#    reach the build (the root Cargo.toml's release and production profiles, .cargo/config.toml's [build], [env] and
#    host target: no comment, no other section), the Rust toolchain (rustc -vV), and the crates cargo resolves for
#    airconv (cargo tree: a path crate by name, since src/airconv's files are hashed; the others by name and version).
#    Not the DXBC parser, the headers or the top-level meson files: cargo builds the translator, and its build script
#    sees only the LLVM headers. The images build with meson.options' cargo_toolchain default, which has to be MTLD3D_RUST.
#  - Any other tree (an older pin): translator 1, as before: src/airconv, libs/DXBCParser, include, meson.build,
#    meson.options, d3d11's compile arguments and the thunks.
# No command is computed inside the hashed pipeline: sh has no pipefail, so a failing one there would leave a well-formed
# key without its lines. Each is taken first and only echoed in the group; the key dies, printing nothing, when one fails.
translator_key() {  # translator_key <dxmt-tree> <buildtype>
  if [ -f "$1/src/airconv/Cargo.toml" ]; then
    _tk_v=2 _tk_l="src/airconv src/d3d11/d3d11_shader.cpp src/winemetal/airconv_thunks.c src/winemetal/airconv_thunks.h"
  else
    _tk_v=1 _tk_l="src/airconv libs/DXBCParser include meson.build meson.options src/d3d11/d3d11_shader.cpp
      src/winemetal/airconv_thunks.c src/winemetal/airconv_thunks.h"
  fi
  for _tk_p in $_tk_l; do
    [ -e "$1/$_tk_p" ] || die "no $_tk_p in $1"
  done
  [ -r "$ROOT/dxmt/llvm.sh" ] && [ -r "$ROOT/wine-arm64/tools/xcrun-metal.sh" ] \
    || die "no dxmt/llvm.sh or wine-arm64/tools/xcrun-metal.sh in $ROOT"
  _tk_f=$(cd "$1" && find $_tk_l -name .git -prune -o -type f ! -name .DS_Store -print0 | LC_ALL=C sort -z \
    | xargs -0 shasum -a 256) || die "can't hash the translator's files in $1"
  _tk_c=$(c++ --version) && [ -n "$_tk_c" ] || die "c++ --version failed"
  _tk_m=$(xcrun -sdk macosx metal --version) && [ -n "$_tk_m" ] || die "metal --version failed"  # the rest names the toolchain's mount point
  if [ "$_tk_v" = 2 ]; then
    [ -n "${MTLD3D_RUST:-}" ] || die "MTLD3D_RUST is not set (dxmt/pins)"
    _tk_t=$(sed -n "s/^option('cargo_toolchain'.*value *: *'\([^']*\)'.*/\1/p" "$1/meson.options") \
      || die "can't read $1/meson.options"
    [ "$_tk_t" = "$MTLD3D_RUST" ] \
      || die "meson.options' cargo_toolchain default is '$_tk_t', not MTLD3D_RUST '$MTLD3D_RUST': the images build with the default"
    _tk_r=$(rustc "+$MTLD3D_RUST" -vV) || die "rustc +$MTLD3D_RUST -vV failed (is the toolchain installed?)"
    _tk_cp=$(toml_sections "$1/Cargo.toml" "[profile.release]" "[profile.production]" "[profile.production-unwind]") \
      || die "can't read $1/Cargo.toml"
    _tk_cc=
    if [ -f "$1/.cargo/config.toml" ]; then
      _tk_cc=$(toml_sections "$1/.cargo/config.toml" "[build]" "[env]" "[target.aarch64-apple-darwin]") \
        || die "can't read $1/.cargo/config.toml"
    fi
    _tk_dep=$(cd "$1" && cargo "+$MTLD3D_RUST" tree --locked --offline --no-dedupe -p airconv -e normal,build \
      --target aarch64-apple-darwin --prefix none --format '{p}') \
      || die "cargo tree -p airconv failed in $1 (a Cargo.lock out of step with a manifest, or crates not fetched: cargo fetch --locked)"
    [ -n "$_tk_dep" ] || die "cargo tree -p airconv printed nothing in $1"
    # Path crates are hashed by content through src/airconv, so one anywhere else would be in neither list.
    _tk_air=$(cd "$1/src/airconv" && pwd -P) || die "no src/airconv in $1"
    _tk_bad=$(printf '%s\n' "$_tk_dep" | sed -n 's/^.* (\(\/[^)]*\))$/\1/p' \
      | awk -v r="$_tk_air" '$0 != r && index($0, r "/") != 1')
    [ -z "$_tk_bad" ] || die "a path crate outside src/airconv in airconv's dependencies: $_tk_bad"
    # A path crate's name only (no version, no location: a workspace version bump or a move must not change the key).
    _tk_dep=$(printf '%s\n' "$_tk_dep" | sed -e 's/ v[^ ]* (\/[^)]*)$//' -e 's/ (\/[^)]*)$//' | LC_ALL=C sort -u) \
      || die "can't list airconv's dependencies"
  fi
  {
    echo "macneutron translator $_tk_v"
    printf '%s\n' "$_tk_f"
    if [ "$_tk_v" = 2 ]; then
      printf '%s\n' "cargo Cargo.toml" "$_tk_cp" "cargo .cargo/config.toml" "$_tk_cc" "$_tk_r" "$_tk_dep"
    fi
    echo "buildtype $2"
    echo "llvm $LLVM_TAG"; cat "$ROOT/dxmt/llvm.sh" "$ROOT/wine-arm64/tools/xcrun-metal.sh"
    printf '%s\n' "$_tk_c" | head -n 1
    printf '%s\n' "$_tk_m" | head -n 1
  } | shasum -a 256 | cut -d ' ' -f 1
}

# The PE side's instruction set (Apple M1, build.sh's PE_MARCH), checked on what Wine's make built: ARM64EC's
# ReadAcquire/WriteRelease are acquire loads and release stores (include/winnt.h's guard; ntdll's sync.o, since
# ntdll.dll's EC range already holds an acquire load elsewhere), and ntdll.dll's ARM64 and ARM64EC code (its CHPE
# CodeMap's ranges) use LSE atomics, not LL/SC loops alone. The caller has llvm-mingw's tools on PATH.
pe_baseline_check() {  # pe_baseline_check <wine-build dir>
  _pb_o="$1/dlls/ntdll/arm64ec-windows/sync.o" _pb_f="$1/dlls/ntdll/aarch64-windows/ntdll.dll"
  _pb_lse='\s(cas|casp|ldadd|ldclr|ldeor|ldset|ldsmax|ldsmin|ldumax|ldumin|swp)(a|l|al)?[bh]?\s'
  for _pb in "$_pb_o" "$_pb_f"; do [ -f "$_pb" ] || die "no $_pb: run pe_baseline_check after make"; done
  _pb_a=$(llvm-objdump -d "$_pb_o" | LC_ALL=C /usr/bin/grep -cE '\s(ldar|ldapr|ldapur)[bh]?\s' || true)
  _pb_r=$(llvm-objdump -d "$_pb_o" | LC_ALL=C /usr/bin/grep -cE '\s(stlr|stlur)[bh]?\s' || true)
  [ "$_pb_a" -gt 0 ] && [ "$_pb_r" -gt 0 ] \
    || die "ntdll's arm64ec sync.o: $_pb_a acquire loads, $_pb_r release stores (include/winnt.h's ARM64EC guard)"
  _pb_base=$(llvm-readobj --file-headers "$_pb_f" | awk '/ImageBase:/ { print $2; exit }')
  for _pb_k in ARM64 ARM64EC; do
    # shellcheck disable=SC2046  # the range's two ends
    set -- $(llvm-readobj --coff-load-config "$_pb_f" \
      | awk -v k="$_pb_k" '/CodeMap \[/ { m = 1; next } m && /\]/ { exit } m && $4 == k { print $1, $3 }')
    [ $# = 2 ] || die "ntdll.dll's CodeMap has no $_pb_k range"
    _pb_n=$(llvm-objdump -d --start-address=$((_pb_base + $1)) --stop-address=$((_pb_base + $2)) "$_pb_f" \
      | LC_ALL=C /usr/bin/grep -cE "$_pb_lse" || true)
    [ "$_pb_n" -gt 0 ] || die "ntdll.dll's $_pb_k code: 0 LSE atomics (the PE side isn't on the M1 baseline)"
  done
}

# ARM64EC code must not use what an x64 context can't hold: x13, x14, x23, x24, x28, v16-v31; a context round trip
# zeroes them (Wine's dlls/ntdll/unwind.h:147-168). Clang never picks them; hand-written assembly can, and llvm-mingw
# only warns. The msvcrt family's string.o carries Arm Optimized Routines (batch Task 6). Fails closed: each string.o
# disassembles to 6,700-9,700 instruction lines today, so a failed llvm-objdump or a format the filter no longer
# matches dies instead of passing on nothing.
ec_regs_check() {  # ec_regs_check <wine-build dir>
  _er_od="$(sh "$ROOT/dxmt/toolchain.sh")/llvm-objdump"
  for _er_m in msvcrt ucrtbase msvcr80 msvcr90 msvcr100 msvcr110 msvcr120; do
    _er_o="$1/dlls/$_er_m/arm64ec-windows/string.o"
    [ -f "$_er_o" ] || die "no $_er_o: run ec_regs_check after make"
    _er_d=$("$_er_od" -d --no-show-raw-insn --no-leading-addr "$_er_o") \
      || die "$_er_m's ARM64EC string.o: llvm-objdump failed"
    _er_d=$(printf '%s\n' "$_er_d" | sed -n 's|//.*||; s/<[^>]*>//g; /^[[:space:]]/p')
    _er_n=$(printf '%s\n' "$_er_d" | wc -l)
    [ "$_er_n" -ge 1000 ] || die "$_er_m's ARM64EC string.o: no disassembly ($((_er_n)) of 1000+ lines)"
    _er_h=$(printf '%s\n' "$_er_d" \
      | LC_ALL=C /usr/bin/grep -m 1 -E '[[:space:],[{]([xw](13|14|23|24|28)|[vqdsbh](1[6-9]|2[0-9]|3[01]))([^0-9]|$)' || true)
    [ -z "$_er_h" ] || die "$_er_m's ARM64EC string.o uses a register x64 code can't hold:$_er_h"
  done
}

# The CRT and DirectX redistributables' builtins replace a game's own x64 copies: version_heuristics sends a Microsoft
# DLL to LO_DEFAULT (Wine's dlls/ntdll/unix/loadorder.c:431), and none of those listed below prefers native, Wine's
# 0x10 in the DllCharacteristics (set by tools/winebuild/build.h:210, read by dlls/ntdll/unix/unix_private.h:430). A
# rebase that changes either moves them under FEX (native REPORT §3 R7; batch Task 7).
CRT_BUILTINS="ucrtbase vcruntime140 vcruntime140_1 msvcp140 msvcp140_1 msvcp140_2 msvcp140_atomic_wait msvcp140_codecvt_ids"
CRT_BUILTINS="$CRT_BUILTINS concrt140 vcomp140 msvcr120 msvcp120 msvcr100 msvcp100 d3dcompiler_43 d3dcompiler_47 d3dx9_43"
CRT_BUILTINS="$CRT_BUILTINS d3dx10_43 d3dx11_43 xaudio2_7 xaudio2_9 x3daudio1_7 xapofx1_5 xinput1_3 xinput1_4 xinput9_1_0"
prefer_native_check() {  # prefer_native_check <aarch64-windows dir> <Wine source tree>
  _pn_ro="$(sh "$ROOT/dxmt/toolchain.sh")/llvm-readobj"
  for _pn in $CRT_BUILTINS; do
    _pn_c=$("$_pn_ro" --file-headers "$1/$_pn.dll" \
      | awk '/ImageOptionalHeader/ { o = 1 } o && /Characteristics \[/ { gsub(/[()]/, "", $3); print $3; exit }')
    [ -n "$_pn_c" ] || die "can't read $_pn.dll's DllCharacteristics"
    [ $((_pn_c & 0x10)) = 0 ] || die "$_pn.dll prefers native ($_pn_c): a game's x64 copy would run under FEX"
  done
  LC_ALL=C /usr/bin/grep -qE '^#define IMAGE_DLLCHARACTERISTICS_PREFER_NATIVE[[:space:]]+0x0010' "$2/dlls/ntdll/unix/unix_private.h" \
    || die "ntdll no longer reads prefer-native as 0x0010 (dlls/ntdll/unix/unix_private.h)"
  LC_ALL=C /usr/bin/grep -qF "{'M','i','c','r','o','s','o','f','t',0}, LO_DEFAULT }" "$2/dlls/ntdll/unix/loadorder.c" \
    || die "loadorder.c's version_heuristics no longer sends Microsoft DLLs to LO_DEFAULT"
}

# D3D9's binaries carry no assertion code (DXMTRS docs/CONVENTIONS.md, "Production carries no debug assertions"). A C or
# C++ object built without NDEBUG leaves an import of the C library's handler: `_assert` or `_wassert` in a PE,
# `__assert_rtn` in a Mach-O, glibc's `__assert_fail` in an ELF. snmalloc's own checks do not go through the handler and
# leave their message format instead, which is looked for in every file. A tool that fails fails the check.
production_asserts() {  # production_asserts <file>...
  _pa_tools=$(sh "$ROOT/dxmt/toolchain.sh") || die "production_asserts: no llvm-mingw toolchain"
  for _pa_f in "$@"; do
    [ -f "$_pa_f" ] || die "production_asserts: no such file: $_pa_f"
    case $_pa_f in
      *.dll)
        _pa_imports=$("$_pa_tools/llvm-readobj" --coff-imports "$_pa_f") \
          || die "production_asserts: cannot read the imports of $_pa_f"
        _pa_hits=$(printf '%s\n' "$_pa_imports" | sed -n 's/^ *Symbol: \(_w\{0,1\}assert\) (.*/\1/p') ;;
      *)
        _pa_imports=$("$_pa_tools/llvm-nm" -u -j "$_pa_f") \
          || die "production_asserts: cannot read the imports of $_pa_f"
        _pa_hits=$(printf '%s\n' "$_pa_imports" | sed -n -E 's/^(_+assert_(rtn|fail))(@.*)?$/\1/p') ;;
    esac
    [ -z "$_pa_hits" ] \
      || die "production_asserts: $_pa_f imports the C assertion handler $_pa_hits: a C or C++ object was built without NDEBUG"
    ! LC_ALL=C /usr/bin/grep -a -q -F 'assert fail: {} in {} on {} ' "$_pa_f" \
      || die "production_asserts: $_pa_f carries snmalloc's assertion message: snmalloc was built without NDEBUG"
  done
}
