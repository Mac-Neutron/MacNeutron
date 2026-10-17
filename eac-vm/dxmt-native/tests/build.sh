#!/bin/sh
# The native build (Direct3D forwarding M1): eac-vm/dxmt-native/build.sh, then each dylib is there, arm64, and
# exports its entry points. Prints "build: ok" or "build: FAIL <what>".
set -u
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
L=$ROOT/build/dxmt-native/install/lib
sh "$ROOT/eac-vm/dxmt-native/build.sh" || true
fail=
for e in "d3d12 _D3D12CreateDevice _D3D12GetDebugInterface _D3D12SerializeVersionedRootSignature" \
         "dxgi _CreateDXGIFactory2" "d3d11 _D3D11CreateDevice" "d3d10core _D3D10CoreCreateDevice" "winemetal"; do
  set -- $e
  lib=$L/$1.dylib
  [ -f "$lib" ] || { fail="$fail $1-missing"; continue; }
  [ "$(lipo -archs "$lib" 2> /dev/null)" = arm64 ] || fail="$fail $1-not-arm64"
  name=$1; shift
  for sym in "$@"; do nm -gU "$lib" | /usr/bin/grep -q " T $sym\$" || fail="$fail $name-$sym"; done
done
if [ -z "$fail" ]; then echo "build: ok"; else echo "build: FAIL$fail"; exit 1; fi
