#!/bin/sh
# lib.sh's translator_key (arm64 release Ruling 46, composition 3: DXMTRS step 4b), which keys DXMT's
# shader-translation cache and the launcher's replay stamp, on a scratch DXMT-shaped workspace fetched and patched as
# build.sh does. Cargo resolves the scratch workspace from the local crate cache, offline. No network, no build.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
. "$ROOT/wine-arm64/lib.sh"
. "$ROOT/dxmt/pins"  # MTLD3D_RUST
PATH="$(sh "$ROOT/dxmt/mtld3d-tools.sh"):$PATH"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
fail=0
expect() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got [$2], want [$3]"; fail=1; fi; }
g() { _g_dir=$1; shift; git -C "$_g_dir" -c user.name=t -c user.email=t@t "$@"; }

# The pin: a DXMT workspace with src/airconv/Cargo.toml and tools/llvm.sh, D3D11's compile arguments, the airconv
# thunks, and the rest of DXMT.
O="$T/origin"
mkdir -p "$O/.cargo" "$O/src/airconv/ir/src" "$O/src/airconv/src" "$O/src/airconv/dxil" "$O/src/other/src" \
  "$O/src/d3d12" "$O/src/d3d11" "$O/src/winemetal/unix" "$O/libs/DXBCParser" "$O/include" "$O/tools"
cat > "$O/Cargo.toml" <<'EOF'
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
cat > "$O/src/airconv/Cargo.toml" <<'EOF'
[package]
name = "airconv"
version.workspace = true
edition.workspace = true

[dependencies]
airconv-ir = { path = "ir" }
rustc-hash = "2"
EOF
cat > "$O/src/airconv/ir/Cargo.toml" <<'EOF'
[package]
name = "airconv-ir"
version.workspace = true
edition.workspace = true
EOF
cat > "$O/src/other/Cargo.toml" <<'EOF'
[package]
name = "other"
version.workspace = true
edition.workspace = true

[dependencies]
libc = "0.2"
EOF
: > "$O/src/airconv/src/lib.rs"; : > "$O/src/airconv/ir/src/lib.rs"; : > "$O/src/other/src/lib.rs"
cat > "$O/.cargo/config.toml" <<'EOF'
# Not a setting.
[target.x86_64-apple-darwin]
rustflags = ["-C", "target-cpu=x86-64-v2"]

[env]
MACOSX_DEPLOYMENT_TARGET = "15.0"

[target.aarch64-apple-darwin]
rustflags = []
EOF
echo 'int convert();' > "$O/src/airconv/dxbc_converter.cpp"
echo 'int lower();' > "$O/src/airconv/dxil/dxil_lower.cpp"
echo 'int parse();' > "$O/libs/DXBCParser/DXBCUtils.cpp"
echo '#pragma once' > "$O/include/adt.hpp"
echo "project('dxmt')" > "$O/meson.build"
printf "option('cargo_toolchain', type : 'string', value : '%s', description : 'x')\n" "$MTLD3D_RUST" > "$O/meson.options"
echo 'int variant();' > "$O/src/d3d11/d3d11_shader.cpp"
echo 'int thunk();' > "$O/src/winemetal/airconv_thunks.c"
echo '#pragma once' > "$O/src/winemetal/airconv_thunks.h"
echo 'LLVM_AIR_COMMIT=0' > "$O/tools/llvm.sh"
echo 'int queue();' > "$O/src/d3d12/d3d12_command_queue.cpp"
echo 'int present();' > "$O/src/winemetal/unix/winemetal_unix.c"
echo 'int context();' > "$O/src/d3d11/d3d11_context.cpp"
(cd "$O" && cargo "+$MTLD3D_RUST" generate-lockfile --offline -q)
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
W="$T/w"
fetch "$W" 2026-10-01T00:00:00Z
fetch "$T/b" 2026-10-02T00:00:00Z
expect "a re-fetch makes other commits" \
  "$([ "$(git -C "$W" rev-parse HEAD)" != "$(git -C "$T/b" rev-parse HEAD)" ] && echo yes)" yes

mkdir "$T/spy"
cat > "$T/spy/shasum" <<EOF
#!/bin/sh
if [ \$# = 2 ]; then tee "$T/stream" | /usr/bin/shasum "\$@"; else exec /usr/bin/shasum "\$@"; fi
EOF
chmod +x "$T/spy/shasum"
kw=$(PATH="$T/spy:$PATH" translator_key "$W" release)
expect "the key is a SHA-256" "$(echo "$kw" | LC_ALL=C /usr/bin/grep -cE '^[0-9a-f]{64}$')" 1
expect "the key's first line reads macneutron translator 3" "$(head -n 1 "$T/stream")" "macneutron translator 3"
expect "the key hashes the stream" "$(/usr/bin/shasum -a 256 < "$T/stream" | cut -d ' ' -f 1)" "$kw"

# 1. Two fetches of one series: one key.
expect "two fetches of an unchanged series have one key" "$(translator_key "$T/b" release)" "$kw"
cp -R "$W" "$T/w-moved"
expect "wherever the tree is" "$(translator_key "$T/w-moved" release)" "$kw"
rm -rf "$T/w-moved"

# The toolchain the images build with: meson.options' default, which the key checks against MTLD3D_RUST.
toolchain() {  # toolchain <name>: meson.options' cargo_toolchain default in the scratch tree
  printf "option('cargo_toolchain', type : 'string', value : '%s', description : 'x')\n" "$1" > "$W/meson.options"
}
wkeeps() {  # wkeeps <what>: the key is as it was
  expect "$1 keeps the key" "$(translator_key "$W" release)" "$kw"
}
wchanges() {  # wchanges <what>: the key differs
  expect "$1 changes the key" "$([ "$(translator_key "$W" release)" != "$kw" ] && echo yes)" yes
}
# dies <what> [<env assignment>]: translator_key fails and prints no key (in a subshell: die exits).
dies() {
  _d_st=0
  _d_out=$(eval "${2:-}" translator_key "$W" release 2> "$T/err") || _d_st=$?
  expect "$1 dies" "$([ "$_d_st" != 0 ] && echo nonzero) [$_d_out]" "nonzero []"
}

# 2. The crates: a lock edit outside airconv's dependency set keeps the key, one inside it changes it. The lock holds
# the highest cached version of each (offline); the edit moves it to the lowest.
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

# 3. What translates: src/airconv (committed or not, new or deleted files, without tests.rs and tests/), D3D11's
# compile arguments (d3d11_shader.cpp, else src/d3d11/src/shader/args.rs), the thunks, tools/llvm.sh, the buildtype.
for f in src/airconv/dxbc_converter.cpp src/d3d11/d3d11_shader.cpp src/winemetal/airconv_thunks.c \
  src/winemetal/airconv_thunks.h tools/llvm.sh; do
  cp "$W/$f" "$T/saved"; printf ' ' >> "$W/$f"
  wchanges "a byte in $f"
  cp "$T/saved" "$W/$f"
done
wkeeps "putting the edited files back"
echo x > "$W/src/airconv/new.rs"
wchanges "an untracked file in src/airconv"
rm "$W/src/airconv/new.rs"
rm "$W/src/airconv/dxil/dxil_lower.cpp"
wchanges "a deleted file in src/airconv"
g "$W" checkout -q -- src/airconv/dxil/dxil_lower.cpp
wkeeps "putting the tree back"
mkdir -p "$W/src/airconv/src/x" "$W/src/airconv/ir/tests/data"
echo '#[test] fn t() {}' > "$W/src/airconv/src/x/tests.rs"
wkeeps "an edit to src/airconv/src/x/tests.rs"
echo x > "$W/src/airconv/ir/tests/data/f"
wkeeps "an edit to src/airconv/ir/tests/data/f"
rm -rf "$W/src/airconv/src/x" "$W/src/airconv/ir/tests"
expect "another buildtype changes the key" "$([ "$(translator_key "$W" debug)" != "$kw" ] && echo yes)" yes

# D3D11's arguments file (step 5b's P3 rule, P9): d3d11_shader.cpp when present, else src/d3d11/src/shader/args.rs,
# and neither dies.
mkdir -p "$W/src/d3d11/src/shader"
echo 'pub fn args() {}' > "$W/src/d3d11/src/shader/args.rs"
wkeeps "adding src/d3d11/src/shader/args.rs while d3d11_shader.cpp exists"
mv "$W/src/d3d11/d3d11_shader.cpp" "$T/saved-d11"
ka=$(translator_key "$W" release 2> /dev/null) || ka=
expect "a tree with args.rs and no d3d11_shader.cpp has a SHA-256 key" \
  "$(echo "$ka" | LC_ALL=C /usr/bin/grep -cE '^[0-9a-f]{64}$' || true)" 1
printf ' ' >> "$W/src/d3d11/src/shader/args.rs"
ka2=$(translator_key "$W" release 2> /dev/null) || ka2=
expect "an edit to src/d3d11/src/shader/args.rs without d3d11_shader.cpp changes the key" \
  "$([ -n "$ka" ] && [ "$ka2" != "$ka" ] && echo yes)" yes
rm -rf "$W/src/d3d11/src"
dies "neither d3d11_shader.cpp nor src/d3d11/src/shader/args.rs"
mv "$T/saved-d11" "$W/src/d3d11/d3d11_shader.cpp"
wkeeps "putting d3d11_shader.cpp back"

# 4. What does not translate: the DXBC parser, the headers, the top-level meson files, the rest of DXMT.
for f in libs/DXBCParser/DXBCUtils.cpp include/adt.hpp meson.build src/d3d12/d3d12_command_queue.cpp \
  src/d3d11/d3d11_context.cpp src/winemetal/unix/winemetal_unix.c; do
  cp "$W/$f" "$T/saved"; printf ' ' >> "$W/$f"
  wkeeps "a byte in $f"
  cp "$T/saved" "$W/$f"
done
cp "$W/meson.options" "$T/saved"; echo "option('x', type : 'string')" >> "$W/meson.options"
wkeeps "another meson option"
cp "$T/saved" "$W/meson.options"
echo 'int fence();' > "$W/src/d3d12/d3d12_fence.cpp"
wkeeps "a new file outside the translator"
rm "$W/src/d3d12/d3d12_fence.cpp"

# 5. The cargo settings that reach it: the three profile sections and config.toml's [build], [env] and the host target.
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

# 6. What it refuses, with no key: no tools/llvm.sh, unhashed profile.package or target.cfg sections, an unreadable
# folder under src/airconv, an uninstalled toolchain, a lock out of step with a manifest, a path crate outside
# src/airconv, an images toolchain other than the key's, no toolchain named, nothing from cargo tree.
mv "$W/tools/llvm.sh" "$T/saved-llvm.sh"
dies "a tree without tools/llvm.sh"
expect "missing tools/llvm.sh names the reason" \
  "$(LC_ALL=C /usr/bin/grep -c 'built with the MacNeutron commit that pinned it' "$T/err" || true)" 1
mv "$T/saved-llvm.sh" "$W/tools/llvm.sh"
wkeeps "putting tools/llvm.sh back"

cp "$W/Cargo.toml" "$T/saved"
for pkg in airconv airconv-ir '"*"'; do
  printf '\n[profile.production.package.%s]\nopt-level = 2\n' "$pkg" >> "$W/Cargo.toml"
  dies "a [profile.production.package.$pkg] section"
  cp "$T/saved" "$W/Cargo.toml"
done
cp "$W/.cargo/config.toml" "$T/saved"
printf "\n[target.'cfg(unix)']\nrustflags = []\n" >> "$W/.cargo/config.toml"
dies "a [target.'cfg(unix)'] section in .cargo/config.toml"
cp "$T/saved" "$W/.cargo/config.toml"

mkdir "$W/src/airconv/src/unreadable"
chmod 000 "$W/src/airconv/src/unreadable"
dies "an unreadable folder under src/airconv"
chmod 700 "$W/src/airconv/src/unreadable"
rmdir "$W/src/airconv/src/unreadable"
wkeeps "removing the unreadable folder"

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

[ $fail = 0 ] && echo "translator_key_test: all passed"
exit $fail
