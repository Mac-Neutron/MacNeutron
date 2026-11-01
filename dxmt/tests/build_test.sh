#!/bin/sh
# The DXMT tooling that outlived dxmt/build.sh, without network or a real build: dxmt/published.sh (the release's
# LGPL check), the pinned Clang and the reuse of a finished LLVM and llvm-mingw only when they match the pins.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="${TMPDIR:-/tmp}/macneutron dxmt-build-test"
rm -rf "$T"; mkdir -p "$T"
fail=0
expect() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got [$2], want [$3]"; fail=1; fi; }

# A release ships only a fork commit that's published on the fork's main branch (LGPL).
git init -q --bare "$T/origin.git"
git clone -q "$T/origin.git" "$T/clone" 2> /dev/null
git -C "$T/clone" -c user.name=t -c user.email=t@t commit -q --allow-empty -m pushed
git -C "$T/clone" push -q origin HEAD:main
pushed=$(git -C "$T/clone" rev-parse HEAD)
git -C "$T/clone" -c user.name=t -c user.email=t@t commit -q --allow-empty -m local
sh "$ROOT/dxmt/published.sh" "$T/clone" "$pushed" > /dev/null 2>&1 && st=0 || st=$?
expect "a pushed fork commit may ship" "$st" 0
out=$(sh "$ROOT/dxmt/published.sh" "$T/clone" "$(git -C "$T/clone" rev-parse HEAD)" 2>&1) && st=0 || st=$?
expect "an unpushed fork commit may not" "$st:$(echo "$out" | LC_ALL=C /usr/bin/grep -c 'push it before shipping')" "1:1"

# The Windows compiler is Clang from our llvm-mingw, not Homebrew's GCC. In make bridge the VM's steam.exe and
# steamprobe.exe are x64 (they run under FEX); the Mac runtime's steam.exe and its helper are arm64.
bin=$(sh "$ROOT/dxmt/toolchain.sh")
expect "the Windows compiler is Clang" "$("$bin/x86_64-w64-mingw32-gcc" --version | head -1 | LC_ALL=C /usr/bin/grep -c clang)" 1
expect "make uses it" "$(make -s -C "$ROOT" -n bridge | LC_ALL=C /usr/bin/grep -c "$bin/x86_64-w64-mingw32-clang")" 2

# DXMT's tools/llvm.sh reuses a finished LLVM install built from the pinned LLVM_AIR_COMMIT for the caller's CPU and LTO
# (its .complete names them, LLVM_AIR_COMMIT+<cpu>+<lto>; generic and nolto here): nothing is fetched or built (stub
# cmake, git and curl fail, each logging its name; git rev-parse is passed through). One for another CPU or LTO keeps
# its source and rebuilds; a source at LLVM_AIR_COMMIT is used without fetching; one at another HEAD is fetched again.
. "$ROOT/dxmt/pins"
mkdir -p "$T/bin3"
for t in cmake git curl; do
  {
    echo '#!/bin/sh'
    [ "$t" != git ] || echo '[ "${3:-}" = rev-parse ] && exec /usr/bin/git "$@"'
    printf 'echo %s >> "%s/called"; exit 1\n' "$t" "$T"
  } > "$T/bin3/$t"
  chmod +x "$T/bin3/$t"
done
called() { [ ! -f "$T/called" ] || tr '\n' ' ' < "$T/called"; rm -f "$T/called"; }
LLVM_SH="${LLVM_SH:-${BUILD_DIR:-$ROOT/build}/wine-arm64-src/dxmt/tools/llvm.sh}"
if [ -f "$LLVM_SH" ]; then
  . "$LLVM_SH"
  mkdir -p "$T/llvm/install" "$T/llvm/project/llvm"
  /usr/bin/git init -q "$T/llvm/project"
  echo "$LLVM_AIR_COMMIT" > "$T/llvm/project/.git/HEAD"
  llvm() { PATH="$T/bin3:/usr/bin:/bin" sh -c 'die() { echo "$*"; exit 1; }; . "$1"
    build_llvm arm64 "$2/llvm/install" "$2/llvm/project" 2> /dev/null && echo reused' sh "$LLVM_SH" "$T" || true; }
  echo "$LLVM_AIR_COMMIT+generic+nolto" > "$T/llvm/install/.complete"
  expect "a finished LLVM install of the pinned commit is reused" "$(llvm):$(called)" "reused:"
  echo "$LLVM_AIR_COMMIT+-mcpu=apple-m5+Thin" > "$T/llvm/install/.complete"
  expect "one built for another CPU or LTO keeps its source and is built again" \
    "$(llvm):$(called):$([ -d "$T/llvm/project/llvm" ] && echo kept)" "LLVM build failed; see $T/llvm/install.log:cmake :kept"
  expect "a source at the pinned commit is built without fetching" \
    "$(llvm):$(called)" "LLVM build failed; see $T/llvm/install.log:cmake "
  echo 0000000000000000000000000000000000000000 > "$T/llvm/project/.git/HEAD"
  expect "a source at another commit is fetched again" \
    "$(llvm):$(called)" "can't fetch llvm-project $LLVM_AIR_COMMIT:git "
  rm -f "$T/called"
else
  echo "skip DXMT's tools/llvm.sh rows ($LLVM_SH absent)"
fi

# dxmt/toolchain.sh likewise, for the release (MACNEUTRON_TOOLCHAIN=upstream): llvm-mingw from before its pin was
# recorded (no .pin) is adopted; another pin's is fetched again (the stub curl fails, so the run stops there).
M="$T/b/dxmt-src/llvm-mingw"
mkdir -p "$M/bin"; printf '#!/bin/sh\n' > "$M/bin/x86_64-w64-mingw32-clang"; chmod +x "$M/bin/x86_64-w64-mingw32-clang"
mingw() { MACNEUTRON_TOOLCHAIN=upstream BUILD_DIR="$T/b" PATH="$T/bin3:/usr/bin:/bin" sh "$ROOT/dxmt/toolchain.sh" \
  2> /dev/null || true; }
expect "llvm-mingw from before its pin was recorded is adopted" "$(mingw):$(cat "$M/.pin")" "$M/bin:$LLVM_MINGW_SHA256"
expect "llvm-mingw of the pin is reused" "$(mingw):$([ -f "$T/called" ] && echo called)" "$M/bin:"
echo 0000 > "$M/.pin"
expect "llvm-mingw of another pin is fetched again" "$(mingw):$([ -f "$T/called" ] && echo called)" ":called"
rm -f "$T/called"
# MacNeutron's own llvm-mingw (the default) is built again when its .recipe isn't this one (the stub git fails).
O="$T/b/dxmt-src/llvm-mingw-own"
mkdir -p "$O/bin"; printf '#!/bin/sh\n' > "$O/bin/aarch64-w64-mingw32-clang"; chmod +x "$O/bin/aarch64-w64-mingw32-clang"
echo 0000 > "$O/.recipe"
own() { BUILD_DIR="$T/b" PATH="$T/bin3:/usr/bin:/bin" sh "$ROOT/dxmt/toolchain.sh" 2> /dev/null || true; }
expect "our llvm-mingw of another recipe is built again" "$(own):$(called)" ":git "

exit $fail
