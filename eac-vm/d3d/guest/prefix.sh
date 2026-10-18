# Direct3D forwarding in a Wine prefix (M2), sourced with WINEPREFIX set and after wineboot (run-game.sh, tests/dxmt.sh):
# with MACNEUTRON_VM_D3D=forward, mnd3d.dll and its stand-ins d3d12.dll, dxgi.dll, d3d11.dll and d3d10core.dll (M4:
# a forwarded process's D3D11 is the host's too, as its DXGI is) linked into the prefix's system32 and taken as native,
# so the program's Direct3D runs on the host (Wine's d3d10.dll on that d3d10core); otherwise Wine's builtins, DXMT in
# the guest (the default). Installed as /opt/mn-wine/lib/wine/mn-d3d/prefix.sh (eac-vm/image/build.sh).
mn_d3d_dir=/opt/mn-wine/lib/wine/mn-d3d/x86_64-windows
if [ "${MACNEUTRON_VM_D3D:-guest}" = forward ] && [ -f "$mn_d3d_dir/mnd3d.dll" ]; then
  for mn_d3d_n in mnd3d d3d12 dxgi d3d11 d3d10core; do
    ln -sf "$mn_d3d_dir/$mn_d3d_n.dll" "$WINEPREFIX/drive_c/windows/system32/$mn_d3d_n.dll"
  done
  mn_d3d_o="d3d12,dxgi,d3d11,d3d10core,mnd3d=n"
else
  mn_d3d_o="d3d12,dxgi,d3d11,d3d10core=b"
fi
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:+$WINEDLLOVERRIDES;}$mn_d3d_o"
