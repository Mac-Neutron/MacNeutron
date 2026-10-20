# Direct3D forwarding in a Wine prefix (M2), sourced with WINEPREFIX set and after wineboot (run-game.sh, tests/dxmt.sh):
# with MACNEUTRON_VM_D3D=forward, mnd3d.dll and its stand-ins d3d12.dll, dxgi.dll, d3d11.dll and d3d10core.dll (M4:
# a forwarded process's D3D11 is the host's too, as its DXGI is) linked into the prefix's system32 and taken as native,
# so the program's Direct3D runs on the host (Wine's d3d10.dll on that d3d10core); otherwise Wine's builtins, DXMT in
# the guest (the default). Native then builtin: a 32-bit process finds no forwarding DLLs in syswow64 and falls back to
# Wine's builtins (DXMT for i386 in the guest), unless MACNEUTRON_VM_D3D32=forward also links the i386 set there (M4b:
# opt-in until 32-bit D3D12 forwards; without it, the links an opted-in run left are removed). Installed as
# /opt/mn-wine/lib/wine/mn-d3d/prefix.sh (eac-vm/image/build.sh).
mn_d3d_dir=/opt/mn-wine/lib/wine/mn-d3d/x86_64-windows
if [ "${MACNEUTRON_VM_D3D:-guest}" = forward ] && [ -f "$mn_d3d_dir/mnd3d.dll" ]; then
  for mn_d3d_n in mnd3d d3d12 dxgi d3d11 d3d10core; do
    ln -sf "$mn_d3d_dir/$mn_d3d_n.dll" "$WINEPREFIX/drive_c/windows/system32/$mn_d3d_n.dll"
  done
  mn_d3d_o="d3d12,dxgi,d3d11,d3d10core,mnd3d=n,b"
  for mn_d3d_n in mnd3d d3d12 dxgi d3d11 d3d10core; do
    mn_d3d_l=$WINEPREFIX/drive_c/windows/syswow64/$mn_d3d_n.dll
    if [ "${MACNEUTRON_VM_D3D32:-guest}" = forward ] && [ -f "${mn_d3d_dir%/*}/i386-windows/mnd3d.dll" ]; then
      ln -sf "${mn_d3d_dir%/*}/i386-windows/$mn_d3d_n.dll" "$mn_d3d_l"
    elif [ -L "$mn_d3d_l" ]; then
      rm -f "$mn_d3d_l"  # an opted-in run's: Wine's builtin comes back on the next update (n,b finds none meanwhile)
    fi
  done
else
  mn_d3d_o="d3d12,dxgi,d3d11,d3d10core=b"
fi
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:+$WINEDLLOVERRIDES;}$mn_d3d_o"
