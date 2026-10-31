#!/bin/sh
# lib.sh's translator_key (arm64 release Ruling 46), which keys DXMT's shader-translation cache and the launcher's
# replay stamp, on scratch DXMT-shaped trees fetched and patched as build.sh does: the C++ translator's (sections 1-3)
# and, with src/airconv/Cargo.toml, the Rust one's (section 4: cargo resolves a scratch workspace from the local crate
# cache, offline). No network, no build.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
. "$ROOT/wine-arm64/lib.sh"
. "$ROOT/dxmt/pins"  # LLVM_TAG
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
fail=0
expect() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got [$2], want [$3]"; fail=1; fi; }
g() { _g_dir=$1; shift; git -C "$_g_dir" -c user.name=t -c user.email=t@t "$@"; }

# The pin: the translator (src/airconv, the DXBC parser, the headers, the top-level build files, d3d11's compile
# arguments, the airconv thunks) and the rest of DXMT.
O="$T/origin"
mkdir -p "$O/src/airconv/dxil" "$O/src/d3d12" "$O/src/d3d11" "$O/src/winemetal/unix" "$O/libs/DXBCParser" "$O/include"
echo 'int convert();' > "$O/src/airconv/dxbc_converter.cpp"
echo 'int lower();' > "$O/src/airconv/dxil/dxil_lower.cpp"
echo 'int parse();' > "$O/libs/DXBCParser/DXBCUtils.cpp"
echo '#pragma once' > "$O/include/adt.hpp"
echo "project('dxmt')" > "$O/meson.build"
echo "option('x', type : 'string')" > "$O/meson.options"
echo 'int variant();' > "$O/src/d3d11/d3d11_shader.cpp"
echo 'int thunk();' > "$O/src/winemetal/airconv_thunks.c"
echo '#pragma once' > "$O/src/winemetal/airconv_thunks.h"
echo 'int queue();' > "$O/src/d3d12/d3d12_command_queue.cpp"
echo 'int present();' > "$O/src/winemetal/unix/winemetal_unix.c"
echo 'int context();' > "$O/src/d3d11/d3d11_context.cpp"
git init -q "$O"; g "$O" add -A; g "$O" commit -qm pin
# The series: one patch in the translator, one outside it.
git clone -q "$O" "$T/work"
echo 'int convert(int);' > "$T/work/src/airconv/dxbc_converter.cpp"; g "$T/work" commit -qam 'airconv: one'
echo 'int queue(int);' > "$T/work/src/d3d12/d3d12_command_queue.cpp"; g "$T/work" commit -qam 'd3d12: two'
g "$T/work" format-patch -q --zero-commit -N -o "$T/patches" HEAD~2..HEAD

# fetch <tree> <time>: the pin plus the series by git am, as build.sh's patch_tree does, committed at <time>.
fetch() {
  git clone -q "$O" "$1"
  GIT_COMMITTER_DATE="$2" g "$1" am -q "$T/patches"/*.patch
}
fetch "$T/a" 2026-10-01T00:00:00Z
fetch "$T/b" 2026-10-02T00:00:00Z
expect "a re-fetch makes other commits" \
  "$([ "$(git -C "$T/a" rev-parse HEAD)" != "$(git -C "$T/b" rev-parse HEAD)" ] && echo yes)" yes
k=$(translator_key "$T/a" release)
expect "the key is a SHA-256" "$(echo "$k" | LC_ALL=C /usr/bin/grep -cE '^[0-9a-f]{64}$')" 1
# 1. Two fetches of one series: one key.
expect "two fetches of an unchanged series have one key" "$(translator_key "$T/b" release)" "$k"
cp -R "$T/a" "$T/moved"
expect "wherever the tree is" "$(translator_key "$T/moved" release)" "$k"

# 2. What translates changes it, committed or not.
changes() {  # changes <what> <tree> <buildtype>: the key differs from $k
  expect "$1 changes the key" "$([ "$(translator_key "$2" "$3")" != "$k" ] && echo yes)" yes
}
printf ' ' >> "$T/a/src/airconv/dxbc_converter.cpp"
changes "an uncommitted byte in src/airconv" "$T/a" release
g "$T/a" checkout -q -- src/airconv/dxbc_converter.cpp
expect "and putting it back restores it" "$(translator_key "$T/a" release)" "$k"
echo x > "$T/a/src/airconv/dxil/new.hpp"
changes "an untracked file in src/airconv" "$T/a" release
rm "$T/a/src/airconv/dxil/new.hpp"
rm "$T/a/src/airconv/dxil/dxil_lower.cpp"
changes "a deleted file in src/airconv" "$T/a" release
g "$T/a" checkout -q -- src/airconv/dxil/dxil_lower.cpp
for f in libs/DXBCParser/DXBCUtils.cpp include/adt.hpp meson.build meson.options src/d3d11/d3d11_shader.cpp \
  src/winemetal/airconv_thunks.c src/winemetal/airconv_thunks.h; do
  cp "$T/a/$f" "$T/saved"; printf ' ' >> "$T/a/$f"
  changes "a byte in $f" "$T/a" release
  cp "$T/saved" "$T/a/$f"
done
changes "another buildtype" "$T/a" debug
expect "another LLVM pin changes the key" "$([ "$(LLVM_TAG=llvmorg-0 translator_key "$T/a" release)" != "$k" ] && echo yes)" yes
expect "the tree is as fetched again" "$(translator_key "$T/a" release)" "$k"

# 3. The rest of DXMT doesn't, committed or not.
printf ' ' >> "$T/a/src/d3d12/d3d12_command_queue.cpp"
expect "an uncommitted byte in src/d3d12 keeps the key" "$(translator_key "$T/a" release)" "$k"
for f in src/d3d11/d3d11_context.cpp src/winemetal/unix/winemetal_unix.c; do
  printf ' ' >> "$T/a/$f"
  expect "an uncommitted byte in $f keeps the key" "$(translator_key "$T/a" release)" "$k"
done
g "$T/a" commit -qam 'd3d12: three'
echo 'int fence();' > "$T/a/src/d3d12/d3d12_fence.cpp"
expect "a commit and a new file outside the translator keep it" "$(translator_key "$T/a" release)" "$k"

# 4. The Rust translator (DXMTRS step 4a): a tree with src/airconv/Cargo.toml is keyed by src/airconv, d3d11's compile
# arguments and the airconv thunks, the cargo settings that reach it, the toolchain and the crates cargo resolves for
# airconv (the path crates by content through src/airconv, the others by name and version); not by libs/DXBCParser, the
# headers or the top-level meson files. It needs rustup's cargo and rustc and, offline, rustc-hash and libc in the crate
# cache (the lock rows need two versions of each, and say so when the cache holds one).
PATH="$(sh "$ROOT/dxmt/mtld3d-tools.sh"):$PATH"
W="$T/w"
git clone -q "$O" "$W"
mkdir -p "$W/.cargo" "$W/src/airconv/ir/src" "$W/src/airconv/src" "$W/src/other/src"
cat > "$W/Cargo.toml" <<'EOF'
[workspace]
members = ["src/airconv", "src/airconv/ir", "src/other"]
resolver = "2"

[workspace.package]
version = "0.1.0"
edition = "2024"

[profile.release]
panic = "abort"
# A comment is not a setting.
lto = "thin"

[profile.release.package.zstd-sys]
debug = 0

[profile.production]
inherits = "release"
lto = "fat"

[profile.production-unwind]
inherits = "production"
panic = "unwind"
EOF
cat > "$W/src/airconv/Cargo.toml" <<'EOF'
[package]
name = "airconv"
version.workspace = true
edition.workspace = true

[dependencies]
airconv-ir = { path = "ir" }
rustc-hash = "2"
EOF
cat > "$W/src/airconv/ir/Cargo.toml" <<'EOF'
[package]
name = "airconv-ir"
version.workspace = true
edition.workspace = true
EOF
cat > "$W/src/other/Cargo.toml" <<'EOF'
[package]
name = "other"
version.workspace = true
edition.workspace = true

[dependencies]
libc = "0.2"
EOF
: > "$W/src/airconv/src/lib.rs"; : > "$W/src/airconv/ir/src/lib.rs"; : > "$W/src/other/src/lib.rs"
cat > "$W/.cargo/config.toml" <<'EOF'
# Not a setting.
[target.x86_64-apple-darwin]
rustflags = ["-C", "target-cpu=x86-64-v2"]

[env]
MACOSX_DEPLOYMENT_TARGET = "15.0"

[target.aarch64-apple-darwin]
rustflags = []
EOF
# The toolchain the images build with: meson.options' default, which the key checks against MTLD3D_RUST.
toolchain() {  # toolchain <name>: meson.options' cargo_toolchain default in the scratch tree
  printf "option('cargo_toolchain', type : 'string', value : '%s', description : 'x')\n" "$1" > "$W/meson.options"
}
toolchain "$MTLD3D_RUST"
(cd "$W" && cargo "+$MTLD3D_RUST" generate-lockfile --offline -q)
kw=$(translator_key "$W" release)
expect "a Rust translator's key is a SHA-256" "$(echo "$kw" | LC_ALL=C /usr/bin/grep -cE '^[0-9a-f]{64}$')" 1
expect "it is not the C++ translator's" "$([ "$kw" != "$(translator_key "$T/b" release)" ] && echo yes)" yes
cp -R "$W" "$T/w-moved"
expect "wherever the tree is" "$(translator_key "$T/w-moved" release)" "$kw"
rm -rf "$T/w-moved"
wkeeps() {  # wkeeps <what>: the key is as it was
  expect "$1 keeps the key" "$(translator_key "$W" release)" "$kw"
}
wchanges() {  # wchanges <what>: the key differs
  expect "$1 changes the key" "$([ "$(translator_key "$W" release)" != "$kw" ] && echo yes)" yes
}
# dies <what> [<env assignment>]: translator_key fails and prints no key (in a subshell: die exits).
dies() {
  _d_st=0
  _d_out=$(eval "${2:-}" translator_key "$W" release 2> /dev/null) || _d_st=$?
  expect "$1 dies" "$([ "$_d_st" != 0 ] && echo nonzero) [$_d_out]" "nonzero []"
}

# The crates: a lock edit outside airconv's dependency set keeps the key, one inside it changes it. The lock holds the
# highest cached version of each (offline); the edit moves it to the lowest.
lowest() {  # lowest <crate>: its lowest cached version, printed only when two or more are cached
  _lo=$(ls "${CARGO_HOME:-$HOME/.cargo}"/registry/cache/*/"$1"-[0-9]*.crate 2> /dev/null \
    | sed "s|.*/$1-\(.*\)\.crate\$|\1|" | sort -t. -k1,1n -k2,2n -k3,3n)
  [ "$(printf '%s\n' "$_lo" | LC_ALL=C /usr/bin/grep -c .)" -ge 2 ] && printf '%s\n' "$_lo" | head -n 1 || true
}
cp "$W/Cargo.lock" "$T/lock"
v=$(lowest libc)
if [ -n "$v" ]; then
  (cd "$W" && cargo "+$MTLD3D_RUST" update -p libc --precise "$v" --offline -q)
  expect "the scratch lock moved libc" "$([ "$(cat "$W/Cargo.lock")" != "$(cat "$T/lock")" ] && echo yes)" yes
  wkeeps "a lock edit of a crate outside airconv's dependency set"
  cp "$T/lock" "$W/Cargo.lock"
else echo "skip a lock edit outside airconv's dependency set: one version of libc in the crate cache"; fi
v=$(lowest rustc-hash)
if [ -n "$v" ]; then
  (cd "$W" && cargo "+$MTLD3D_RUST" update -p rustc-hash --precise "$v" --offline -q)
  wchanges "a lock edit inside it (rustc-hash)"
  cp "$T/lock" "$W/Cargo.lock"
  wkeeps "putting the lock back"
else echo "skip a lock edit inside airconv's dependency set: one version of rustc-hash in the crate cache"; fi

# What translates: src/airconv (committed or not, new files too), d3d11's compile arguments, the thunks.
for f in src/airconv/dxbc_converter.cpp src/d3d11/d3d11_shader.cpp src/winemetal/airconv_thunks.c \
  src/winemetal/airconv_thunks.h; do
  cp "$W/$f" "$T/saved"; printf ' ' >> "$W/$f"
  wchanges "a byte in $f"
  cp "$T/saved" "$W/$f"
done
echo x > "$W/src/airconv/new.rs"
wchanges "an untracked file in src/airconv"
rm "$W/src/airconv/new.rs"
wkeeps "putting the tree back"
# What no longer does: the DXBC parser (a duplicate of airconv-ir's since 4a), the headers, the top-level meson files.
for f in libs/DXBCParser/DXBCUtils.cpp include/adt.hpp meson.build src/d3d12/d3d12_command_queue.cpp; do
  cp "$W/$f" "$T/saved"; printf ' ' >> "$W/$f"
  wkeeps "a byte in $f"
  cp "$T/saved" "$W/$f"
done
cp "$W/meson.options" "$T/saved"; echo "option('x', type : 'string')" >> "$W/meson.options"
wkeeps "another meson option"
cp "$T/saved" "$W/meson.options"
# The cargo settings that reach it: the three profile sections and config.toml's [build], [env] and the host target.
cp "$W/Cargo.toml" "$T/saved"
sed 's/lto = "fat"/lto = "thin"/' "$T/saved" > "$W/Cargo.toml"
wchanges "an edit to [profile.production]"
sed 's/panic = "abort"/panic = "unwind"/' "$T/saved" > "$W/Cargo.toml"
wchanges "an edit to [profile.release]"
sed 's/panic = "unwind"/panic = "abort"/' "$T/saved" > "$W/Cargo.toml"
wchanges "an edit to [profile.production-unwind]"
sed 's/debug = 0/debug = 1/' "$T/saved" > "$W/Cargo.toml"
wkeeps "an edit to [profile.release.package.zstd-sys]"
sed 's/# A comment is not a setting./# Another comment./' "$T/saved" > "$W/Cargo.toml"
wkeeps "a comment in a profile section"
cp "$T/saved" "$W/Cargo.toml"
cp "$W/.cargo/config.toml" "$T/saved"
sed 's/15.0/16.0/' "$T/saved" > "$W/.cargo/config.toml"
wchanges "an edit to config.toml's [env]"
sed 's/rustflags = \[\]/rustflags = ["-C", "target-cpu=native"]/' "$T/saved" > "$W/.cargo/config.toml"
wchanges "an edit to config.toml's [target.aarch64-apple-darwin]"
sed 's/x86-64-v2/x86-64-v3/' "$T/saved" > "$W/.cargo/config.toml"
wkeeps "an edit to config.toml's x86_64 target"
cp "$T/saved" "$W/.cargo/config.toml"
rm "$W/.cargo/config.toml"
wchanges "removing config.toml"
cp "$T/saved" "$W/.cargo/config.toml"
wkeeps "putting config.toml back"
# The toolchain: another one (nightly is installed for rustfmt, with its own cargo and rustc) changes it.
toolchain nightly
expect "another toolchain changes the key" \
  "$([ "$(MTLD3D_RUST=nightly translator_key "$W" release)" != "$kw" ] && echo yes)" yes
toolchain "$MTLD3D_RUST"

# 5. What it refuses, with no key: an uninstalled toolchain, a lock out of step with a manifest, a path crate outside
# src/airconv, an images toolchain other than the key's, no toolchain named, nothing from cargo tree.
toolchain no-such-toolchain-xyz
dies "an uninstalled MTLD3D_RUST" "MTLD3D_RUST=no-such-toolchain-xyz"
toolchain "$MTLD3D_RUST"
dies "no MTLD3D_RUST" "MTLD3D_RUST="
dies "a meson.options toolchain other than MTLD3D_RUST" "MTLD3D_RUST=nightly"
mkdir "$T/stub"; printf '#!/bin/sh\nexit 0\n' > "$T/stub/cargo"; chmod +x "$T/stub/cargo"
dies "an empty cargo tree" "PATH=$T/stub:\$PATH"
cp "$W/src/airconv/Cargo.toml" "$T/saved"
printf 'libc = "0.2"\n' >> "$W/src/airconv/Cargo.toml"
dies "a Cargo.lock out of step with a manifest"
cp "$T/saved" "$W/src/airconv/Cargo.toml"
printf 'other = { path = "../other" }\n' >> "$W/src/airconv/Cargo.toml"
(cd "$W" && cargo "+$MTLD3D_RUST" metadata --offline --format-version 1 > /dev/null)
expect "the scratch lock follows the path crate" "$(cd "$W" && cargo "+$MTLD3D_RUST" tree --locked --offline -p airconv --prefix none \
  | LC_ALL=C /usr/bin/grep -c '^other ')" 1
dies "a path crate outside src/airconv"
cp "$T/saved" "$W/src/airconv/Cargo.toml"; cp "$T/lock" "$W/Cargo.lock"
wkeeps "putting the manifest back"

# 6. An older pin (a tree without src/airconv/Cargo.toml) keeps the key dxmtrs's lib.sh gave it, byte for byte; after
# the pin lands dxmtrs holds this lib.sh, and the row compares it with itself.
git -C "$ROOT" show dxmtrs:wine-arm64/lib.sh > "$T/lib-before.sh"
expect "a tree without src/airconv/Cargo.toml keeps its earlier key" "$(translator_key "$T/b" release)" \
  "$(. "$T/lib-before.sh"; translator_key "$T/b" release)"

[ $fail = 0 ] && echo "translator_key_test: all passed"
exit $fail
