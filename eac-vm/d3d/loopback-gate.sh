#!/bin/sh
# Direct3D forwarding's loopback gate (M2, spec §8): M1's 19 headless D3D12 programs and M4's 11 D3D11 and D3D10 ones
# (all but d3d10_wine, which needs Wine's d3d10.dll) linked to the loopback library
# (build-loopback.sh: every Direct3D call through the guest's proxies, the marshaller, drained rings and the host's
# dispatcher, in one process) print what they print on the macOS Wine runtime, by M1's gate (compare.py's rule).
# MN_DXMT_ONLY: those rows only. Runs in build/d3d-loopback/runs. Prints "d3d-loopback gate: all <n> programs passed"
# or the failures; exits 1 on one.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${BUILD_DIR:-$ROOT/build}/d3d-loopback
LIB=$(sh "$ROOT/eac-vm/d3d/build-loopback.sh")
ROWS=${MN_DXMT_ONLY:-"d3d12_api d3d12_bounds d3d12_cache d3d12_compress d3d12_copy d3d12_depth d3d12_dxil d3d12_dxil_exec
  d3d12_hazards d3d12_indirect d3d12_layered d3d12_lod d3d12_null d3d12_query d3d12_timestamp d3d12_triangle
  d3d12_upscale d3d12_volume d3d12_vsread d3d11_api d3d11_compute d3d11_deferred d3d11_dynamic d3d11_getters
  d3d11_pipeline d3d11_query d3d11_render d3d11_transfer d3d11_vsia d3d10_core"}
ROWS=$(echo $ROWS)
MN_D3D_PROGRAMS=$ROWS MN_D3D_PROGRAMS_LIB=$LIB MN_D3D_PROGRAMS_OUT=$OUT/tests sh "$ROOT/eac-vm/dxmt-native/build-tests.sh"
MN_DXMT_ONLY=$ROWS MN_GATE_PROGRAMS=$OUT/tests MN_GATE_RUNS=$OUT/runs MN_GATE_NAME="d3d-loopback gate" \
  sh "$ROOT/eac-vm/dxmt-native/gate.sh"
