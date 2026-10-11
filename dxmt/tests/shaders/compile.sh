#!/bin/sh
# Compiles the test shaders to DXIL with Microsoft's dxc.exe under the frozen Rosetta reference's Wine (DXMT fork spec
# §6; tools/freeze-rosetta-reference.sh). Fetches DXC once into build/dxmt-src. The .dxil files are committed; rerun
# this after editing a shader.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
. "$ROOT/dxmt/pins"
. "$ROOT/dxmt/lib.sh"
REF="${MACNEUTRON_REFERENCE:-$HOME/Library/Application Support/MacNeutron Reference/rosetta-tool}"
[ -f "$REF/../FROZEN" ] || die "no frozen Rosetta reference at $REF (run tools/freeze-rosetta-reference.sh)"
SRC="$ROOT/build/dxmt-src"
DXC="$SRC/dxc/bin/x64/dxc.exe"
if [ ! -f "$DXC" ]; then
  mkdir -p "$SRC"
  fetch "$DXC_URL" "$SRC/dxc.zip" "$DXC_SHA256"
  rm -rf "$SRC/dxc" "$SRC/dxc.tmp"
  # Exit 1 is a warning: DXC's zip uses backslash separators, which unzip converts.
  unzip -q "$SRC/dxc.zip" -d "$SRC/dxc.tmp" 2> /dev/null || [ $? -eq 1 ] || die "can't unpack dxc.zip"
  mv "$SRC/dxc.tmp" "$SRC/dxc"
fi
WINE="$REF/Libraries/Wine/bin/wine"
export WINEPREFIX="${TMPDIR:-/tmp}/macneutron dxc" WINEDEBUG=-all
cd "$HERE"  # relative paths: dxc.exe would read a leading / as an option
# Every dxc.exe runs at once; finish waits for them (set -e: a failed one stops the script). The first call, alone,
# creates the Wine prefix.
"$WINE" "$DXC" --version > /dev/null
pids=""
dxc() { "$WINE" "$DXC" "$@" & pids="$pids $!"; }
finish() { for p in $pids; do wait "$p"; done; pids=""; }
dxc -T vs_6_0 -E vsmain -Fo triangle.vs.dxil triangle.hlsl
dxc -T ps_6_0 -E psmain -Fo triangle.ps.dxil triangle.hlsl
dxc -T cs_6_0 -E csmain -Fo compute.cs.dxil compute.hlsl
dxc -T vs_6_6 -E vsmain -Fo triangle2.vs.dxil triangle2.hlsl
dxc -T ps_6_6 -E psmain -Fo triangle2.ps.dxil triangle2.hlsl
dxc -T gs_6_6 -E gsmain -Fo triangle2.gs.dxil triangle2.hlsl
dxc -T vs_6_6 -E vsmain -Fo depth.vs.dxil depth.hlsl
dxc -T ps_6_6 -E psmain -Fo depth.ps.dxil depth.hlsl
dxc -T ps_6_6 -E psdepth -Fo depth.psdepth.dxil depth.hlsl
dxc -T cs_6_0 -E main -Fo null.cs.dxil null.hlsl
dxc -T vs_6_6 -E vsmain -Fo cache.vs.dxil cache.hlsl
dxc -T ps_6_6 -E psmain -Fo cache.ps.dxil cache.hlsl
dxc -T cs_6_6 -E csmain -Fo cache.cs.dxil cache.hlsl
dxc -T vs_6_6 -E vsmain -Fo layered.vs.dxil layered.hlsl
dxc -T ps_6_6 -E psmain -Fo layered.ps.dxil layered.hlsl
dxc -T cs_6_6 -E fill -Fo volume.fill.dxil volume.hlsl
dxc -T cs_6_6 -E sample -Fo volume.sample.dxil volume.hlsl
dxc -T vs_6_6 -E vsmain -Fo vsread.vs.dxil vsread.hlsl
dxc -T ps_6_6 -E psmain -Fo vsread.ps.dxil vsread.hlsl
dxc -T vs_6_6 -E vsia -Fo vsread.vsia.dxil vsread.hlsl
dxc -T vs_6_6 -E vsmain -Fo indirect.vs.dxil indirect.hlsl
dxc -T ps_6_6 -E psmain -Fo indirect.ps.dxil indirect.hlsl
dxc -T cs_6_6 -E csmain -Fo indirect.cs.dxil indirect.hlsl
dxc -T vs_6_6 -E vsid -Fo indirect.vsid.dxil indirect.hlsl
dxc -T ps_6_6 -E psid -Fo indirect.psid.dxil indirect.hlsl
dxc -T vs_6_6 -E vsfull -Fo hazards.vsfull.dxil hazards.hlsl
dxc -T ps_6_6 -E psvalue -Fo hazards.psvalue.dxil hazards.hlsl
dxc -T ps_6_6 -E pssample -Fo hazards.pssample.dxil hazards.hlsl
dxc -T cs_6_6 -E csfill -Fo hazards.csfill.dxil hazards.hlsl
dxc -T cs_6_6 -E cscount -Fo hazards.cscount.dxil hazards.hlsl
dxc -T cs_6_6 -E csargs -Fo hazards.csargs.dxil hazards.hlsl
dxc -T cs_6_0 -E csmain -Fo bounds.cs.dxil bounds.hlsl
dxc -T vs_6_6 -E vsmain -Fo lod.vs.dxil lod.hlsl
dxc -T ps_6_6 -E psmain -Fo lod.ps.dxil lod.hlsl
dxc -T ps_6_6 -E psmain -Fo sm6x.ps.dxil sm6x.hlsl
dxc -T cs_6_6 -E csmain -Fo sm6x.cs.dxil sm6x.hlsl
dxc -T ps_6_6 -E psmain -Fo helper.ps.dxil helper.hlsl
dxc -T ps_6_7 -E psinclude -Fo helper.psinclude.dxil helper.hlsl
dxc -T ps_6_6 -E psgreen -Fo helper.psgreen.dxil helper.hlsl
dxc -T vs_6_6 -E vsmain -Fo varying16.vs32.dxil varying16.hlsl
dxc -T ps_6_6 -E psmain -Fo varying16.ps32.dxil varying16.hlsl
dxc -T vs_6_6 -E vsmain -D MIN16 -Fo varying16.vs16.dxil varying16.hlsl
dxc -T ps_6_6 -E psmain -D MIN16 -Fo varying16.ps16.dxil varying16.hlsl
dxc -T vs_6_6 -E vsmain -D NATIVE16 -enable-16bit-types -Fo varying16.vs16n.dxil varying16.hlsl
dxc -T ps_6_6 -E psmain -D NATIVE16 -enable-16bit-types -Fo varying16.ps16n.dxil varying16.hlsl
finish
ls -l ./*.dxil
# DXIL translator behaviour groups (dxmt/tests/dxil; see common.hlsli). 16-bit types where the group needs them.
cd "$HERE/../dxil"
for g in buffers math transcendental textures groupshared wave atomics quad heap specials mad barriers msad wavematch wavesize32 deriv; do dxc -T cs_6_6 -E main -Fo "$g.dxil" "$g.hlsl"; done
dxc -T cs_6_7 -E main -Fo helperquad.dxil helperquad.hlsl
dxc -T cs_6_7 -E main -Fo offsets.dxil offsets.hlsl
dxc -T cs_6_8 -E main -Fo wavesize32r.dxil wavesize32.hlsl
dxc -T cs_6_8 -E main -Fo wavesize.dxil wavesize.hlsl
for g in half packed; do dxc -T cs_6_6 -E main -enable-16bit-types -Fo "$g.dxil" "$g.hlsl"; done
finish
ls -l ./*.dxil
# Shaders dxil-translate and D3D12 must refuse.
cd "$HERE/../dxil/reject"
dxc -T cs_6_6 -E main -Fo wavesize64.dxil wavesize64.hlsl
dxc -T cs_6_8 -E main -Fo wavesizerange.dxil wavesizerange.hlsl
finish
ls -l ./*.dxil
