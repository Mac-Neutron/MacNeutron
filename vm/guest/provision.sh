#!/bin/sh
# Provisions the EAC guest: Fedora 44 Cloud (aarch64), booted with Secure Boot through the distribution's signed shim
# and kernel (no custom kernel or module), as the user `bench` with sudo. Run once in the guest:
#   sh provision.sh <path to the eac-2610.2 FEX source>
# Steps, each idempotent:
#   1. DNS through public resolvers and no IPv6 (Virtualization.framework's NAT has neither working DNS for the guest
#      nor IPv6 egress, and Steam's updater gives up on an unreachable IPv6 address).
#   2. Fedora's FEX packaging (binfmt, the x86-64 Fedora RootFS) with the EAC-fidelity FEX build swapped in as the
#      x86/x86-64 binfmt interpreter (StonkGuy/FEX eac-2610.2: ptrace emulation, signal masks, seccomp, SMC hot pages).
#      qemu-user's x86 handlers are removed so they never claim an x86 binary.
#   3. The CA bundle at the paths Steam's updater reads (Fedora 44 ships neither /etc/pki/tls/cert.pem nor
#      /etc/ssl/certs/ca-certificates.crt).
#   4. A sway session on tty1 that starts x86-64 Steam (Linux) under FEX, as Fedora Asahi runs it.
set -eu
FEX_SRC=${1:?usage: provision.sh <FEX eac-2610.2 source>}
sudo mkdir -p /etc/systemd/resolved.conf.d
printf '[Resolve]\nDNS=1.1.1.1 8.8.8.8\nFallbackDNS=9.9.9.9\n' | sudo tee /etc/systemd/resolved.conf.d/dns.conf >/dev/null
sudo systemctl restart systemd-resolved
printf 'net.ipv6.conf.all.disable_ipv6=1\nnet.ipv6.conf.default.disable_ipv6=1\n' | sudo tee /etc/sysctl.d/90-no-ipv6.conf >/dev/null
sudo sysctl -q --system
grep -q ip_resolve /etc/dnf/dnf.conf || echo ip_resolve=4 | sudo tee -a /etc/dnf/dnf.conf >/dev/null

sudo dnf -y -q install clang lld llvm cmake ninja-build git python3 python3-setuptools gcc-c++ \
  fex-emu fex-emu-rootfs-fedora fex-emu-filesystem erofs-fuse wget fontforge patch \
  sway xorg-x11-server-Xwayland foot dbus-x11 mesa-dri-drivers mesa-vulkan-drivers
sudo dnf -y -q remove 'qemu-user-static*' || true

if [ ! -x "$FEX_SRC/build/Bin/FEX" ]; then
  cmake -S "$FEX_SRC" -B "$FEX_SRC/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ -DBUILD_TESTING=OFF -DBUILD_THUNKS=OFF -DBUILD_FEXCONFIG=OFF -DENABLE_LTO=ON \
    -DENABLE_ASSERTIONS=OFF -DTUNE_CPU=none -DCMAKE_CXX_SCAN_FOR_MODULES=OFF
  cmake --build "$FEX_SRC/build" -j"$(nproc)"
fi
for b in FEX FEXServer FEXBash FEXGetConfig FEXpidof; do sudo install -m755 "$FEX_SRC/build/Bin/$b" /usr/local/bin/; done
for a in x86_64 x86; do sed 's|/usr/bin/FEX|/usr/local/bin/FEX|' /usr/lib/binfmt.d/FEX-$a.conf | sudo tee /etc/binfmt.d/FEX-$a.conf >/dev/null; done
sudo systemctl reset-failed systemd-binfmt || true
sudo systemctl restart systemd-binfmt
mkdir -p "$HOME/.fex-emu"
printf '{"Config":{"RootFS":"/usr/share/fex-emu/RootFS/default.erofs"}}\n' > "$HOME/.fex-emu/Config.json"

sudo ln -sf /etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem /etc/pki/tls/cert.pem
sudo ln -sf /etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem /etc/ssl/certs/ca-certificates.crt

mkdir -p "$HOME/steam-bootstrap"
if [ ! -x "$HOME/steam-bootstrap/steam-launcher/bin_steam.sh" ]; then
  wget -q -O "$HOME/steam-bootstrap/steam_latest.tar.gz" https://repo.steampowered.com/steam/archive/stable/steam_latest.tar.gz
  tar -xzf "$HOME/steam-bootstrap/steam_latest.tar.gz" -C "$HOME/steam-bootstrap"
fi
cat > "$HOME/start-steam.sh" <<'EOS'
#!/bin/sh
exec /usr/local/bin/FEXBash -c "$HOME/steam-bootstrap/steam-launcher/bin_steam.sh" > "$HOME/steam.log" 2>&1
EOS
chmod +x "$HOME/start-steam.sh"
sudo mkdir -p /etc/systemd/system/getty@tty1.service.d
printf '[Service]\nExecStart=\nExecStart=-/sbin/agetty --autologin %s --noclear %%I $TERM\n' "$(id -un)" \
  | sudo tee /etc/systemd/system/getty@tty1.service.d/autologin.conf >/dev/null
grep -q 'exec dbus-run-session sway' "$HOME/.bash_profile" 2>/dev/null \
  || echo 'if [ "$(tty)" = /dev/tty1 ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then exec dbus-run-session sway; fi' >> "$HOME/.bash_profile"
mkdir -p "$HOME/.config/sway"
{ cat /etc/sway/config; printf '\nexec foot\nexec %s/start-steam.sh\noutput * scale 2\n' "$HOME"; } > "$HOME/.config/sway/config"
echo "provisioned: reboot, then log in to Steam in the VM window"
