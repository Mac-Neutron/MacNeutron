# Direct3D forwarding in a Wine prefix (M2), sourced with WINEPREFIX set and after wineboot (run-game.sh, tests/dxmt.sh):
# with MACNEUTRON_VM_D3D=forward, mnd3d.dll and its stand-ins d3d12.dll, dxgi.dll, d3d11.dll and d3d10core.dll (M4:
# a forwarded process's D3D11 is the host's too, as its DXGI is) linked into the prefix's system32 and taken as native,
# so the program's Direct3D runs on the host (Wine's d3d10.dll on that d3d10core); otherwise Wine's builtins, DXMT in
# the guest (the default). Native then builtin: a 32-bit process finds no forwarding DLLs in syswow64 and falls back to
# Wine's builtins (DXMT for i386 in the guest), unless MACNEUTRON_VM_D3D32=forward also links the i386 set there (M4b:
# opt-in until 32-bit D3D12 forwards; in either mode, the links into mn-d3d an opted-in run left are replaced by Wine's
# own builtins: wineboot ran before this, and a DLL missing from syswow64 isn't found). A
# missing DLL of a set says so on stderr (n,b would take Wine's builtin for it silently). Installed as
# /opt/mn-wine/lib/wine/mn-d3d/prefix.sh (eac-vm/image/build.sh).
mn_d3d_root=/opt/mn-wine/lib/wine/mn-d3d
mn_d3d_dir=$mn_d3d_root/x86_64-windows
mn_d3d_link() {
  [ -f "$1" ] || echo "mn-d3d: $1 is missing: Wine's builtin $(basename "$1") runs instead" >&2
  ln -sf "$1" "$2"
}
mn_d3d_o="d3d12,dxgi,d3d11,d3d10core=b" mn_d3d_32=
if [ "${MACNEUTRON_VM_D3D:-guest}" = forward ] && [ -f "$mn_d3d_dir/mnd3d.dll" ]; then
  for mn_d3d_n in mnd3d d3d12 dxgi d3d11 d3d10core; do
    mn_d3d_link "$mn_d3d_dir/$mn_d3d_n.dll" "$WINEPREFIX/drive_c/windows/system32/$mn_d3d_n.dll"
  done
  mn_d3d_o="d3d12,dxgi,d3d11,d3d10core,mnd3d=n,b"
  [ "${MACNEUTRON_VM_D3D32:-guest}" != forward ] || [ ! -f "$mn_d3d_root/i386-windows/mnd3d.dll" ] || mn_d3d_32=1
  # Old WoW64's i686 winemetal.so has no forwarding (its Unix side is 64-bit only): new WoW64's.
  if [ -n "$mn_d3d_32" ] && [ "${WINEARCH:-}" != wow64 ] && [ -d /opt/mn-wine/lib/wine/i386-unix ]; then
    echo "mn-d3d: MACNEUTRON_VM_D3D32=forward needs MACNEUTRON_VM_WOW64=1: DXMT for i386 runs in the guest" >&2
    mn_d3d_32=
  fi
fi
for mn_d3d_n in mnd3d d3d12 dxgi d3d11 d3d10core; do
  mn_d3d_l=$WINEPREFIX/drive_c/windows/syswow64/$mn_d3d_n.dll
  if [ -n "$mn_d3d_32" ]; then
    mn_d3d_link "$mn_d3d_root/i386-windows/$mn_d3d_n.dll" "$mn_d3d_l"
  else  # an opted-in run's link: Wine's own builtin in its place, as wineboot puts it (a missing one isn't found)
    case $(readlink "$mn_d3d_l" 2> /dev/null) in "$mn_d3d_root"/*)
      rm -f "$mn_d3d_l"
      [ ! -f "/opt/mn-wine/lib/wine/i386-windows/$mn_d3d_n.dll" ] || cp "/opt/mn-wine/lib/wine/i386-windows/$mn_d3d_n.dll" "$mn_d3d_l" ;;
    esac
  fi
done
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:+$WINEDLLOVERRIDES;}$mn_d3d_o"
