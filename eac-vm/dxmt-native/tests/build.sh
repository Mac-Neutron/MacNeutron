#!/bin/sh
# The native build (Direct3D forwarding M1): eac-vm/dxmt-native/build.sh, then each dylib is there, arm64, and
# exports its entry points. Prints "build: ok" or "build: FAIL <what>".
set -u
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
L=${BUILD_DIR:-$ROOT/build}/dxmt-native/install/lib
fail=
sh "$ROOT/eac-vm/dxmt-native/build.sh" || fail=" build.sh"
for e in "d3d12 _D3D12CreateDevice _D3D12GetDebugInterface _D3D12SerializeVersionedRootSignature" \
         "dxgi _CreateDXGIFactory _CreateDXGIFactory1 _CreateDXGIFactory2 _DXGIDeclareAdapterRemovalSupport _DXGIGetDebugInterface1" \
         "d3d11 _D3D11CreateDevice" "d3d10core _D3D10CoreCreateDevice _D3D10CoreRegisterLayers" "winemetal"; do
  set -- $e
  lib=$L/$1.dylib
  [ -f "$lib" ] || { fail="$fail $1-missing"; continue; }
  [ "$(lipo -archs "$lib" 2> /dev/null)" = arm64 ] || fail="$fail $1-not-arm64"
  name=$1; shift
  for sym in "$@"; do nm -gU "$lib" | /usr/bin/grep -q " T $sym\$" || fail="$fail $name-$sym"; done
done
if [ -z "$fail" ]; then echo "build: ok"; else echo "build: FAIL$fail"; exit 1; fi
