#!/bin/sh
# inert.sh <base> [<new>]: the product's DXMT build compiles the same source with DXMT's tree at <new> (default the
# macneutron branch) as at <base>. Every translation unit of the product's build (build/wine-arm64-src/dxmt-build's
# compile_commands.json) is preprocessed against APFS clones of the tree and of the build folder, one checked out at
# each commit; each output must be the same, byte for byte. Exits 1 naming each file that differs. It also prints both
# trees' translator keys (wine-arm64/lib.sh), which key the product's shader cache and are not in the preprocessed
# text: a patch that touches meson.build, meson.options or src/airconv changes the key, and with it every user's DXMT
# shader cache once, whatever its sources compile to. Reported, not failed: a new meson option can't avoid it.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
S=${BUILD_DIR:-$ROOT/build}/wine-arm64-src
T=${BUILD_DIR:-$ROOT/build}/eac-vm-work/inert
base=$1 new=${2:-macneutron}
PATH="$(sh "$ROOT/dxmt/toolchain.sh"):$PATH"
. "$ROOT/wine-arm64/lib.sh"
. "$ROOT/dxmt/pins"
rm -rf "$T"
for side in base new; do
  mkdir -p "$T/$side"
  cp -cR "$S/dxmt" "$T/$side/dxmt"
  cp -cR "$S/dxmt-build" "$T/$side/dxmt-build"
  git -C "$T/$side/dxmt" checkout -q --detach "$(eval echo \$$side)"
done
kb=$(translator_key "$T/base/dxmt" release) kn=$(translator_key "$T/new/dxmt" release)
if [ "$kb" = "$kn" ]; then echo "inert: translator key the same"
else echo "inert: translator key differs (the product's shader caches are rebuilt once): $kb -> $kn"; fi
python3 - "$T" "$S/dxmt-build/compile_commands.json" <<'EOF'
import json, shlex, subprocess, sys
T, commands = sys.argv[1], json.load(open(sys.argv[2]))
differ = []
for i, c in enumerate(commands):
    args, out = shlex.split(c['command']), []
    skip = 0
    for a in args:
        if skip: skip -= 1; continue
        if a in ('-MQ', '-MF', '-o'): skip = 1; continue
        if a in ('-MD', '-c'): continue
        out.append(a)
    texts = []
    for side in ('base', 'new'):
        r = subprocess.run(out + ['-E', '-P'], cwd=f'{T}/{side}/dxmt-build', capture_output=True)
        if r.returncode: sys.exit(f'inert: preprocessing {c["file"]} at {side} failed:\n{r.stderr.decode()[-2000:]}')
        texts.append(r.stdout)
    if texts[0] != texts[1]: differ.append(c['file'])
for f in differ: print(f'inert: {f} differs')
print(f'inert: {len(commands) - len(differ)} of {len(commands)} translation units the same')
sys.exit(1 if differ else 0)
EOF
