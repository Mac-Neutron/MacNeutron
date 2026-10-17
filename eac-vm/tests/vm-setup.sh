#!/bin/sh
# The VM setup's gate (docs/superpowers/plans/2026-10-17-vm-setup-2-assembly.md Task 10): a VM folder made the way a
# Mac makes one (macneutron vm-setup, from the builder's pinned packages and the parts, served as file: URLs) must hold
# the builder's own images, file for file (type, path, mode, owner, size, link, SHA-256, capabilities), and boot: the
# pinned kernel with lockdown and signed modules, the TPM, and sound from an x86-64 ALSA program and a Windows one
# (tests/audio.sh: FEX's x86-64 root and our Wine image both work). Needs the builder (eac-vm/builder.sh start) and
# build/eac-vm-work's images from eac-vm/image/build.sh. Prints "vm-setup: ok" or "vm-setup: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$ROOT/build/eac-vm-work
G=$ROOT/build/vm-setup-gate
B="sh $ROOT/eac-vm/builder.sh"
KVER=7.2.10-200.fc44.aarch64
fail=

swift build -c release --product macneutron --package-path "$ROOT" 2>&1 | tail -1
MKFS=$(sh "$ROOT/eac-vm/setup-tools/build.sh")
mkdir -p "$G"

# The builder's pinned packages, as a Mac's mirror would serve them.
if [ ! -f "$G/rpms/.complete" ] || ! cmp -s "$W/images/packages.lock" "$G/rpms/.complete"; then
  rm -rf "$G/rpms" "$W/images/gate-rpms"
  $B ssh 'mkdir -p /mnt/out/gate-rpms && cp /var/cache/mn-image-rpms/base/*.rpm /var/cache/mn-image-rpms/x86/*.rpm /mnt/out/gate-rpms/'
  mv "$W/images/gate-rpms" "$G/rpms"
  cp "$W/images/packages.lock" "$G/rpms/.complete"
fi

# No machine identity of the builder's goes to players: /etc/machine-id is empty in the base tree.
awk -F'\t' '$2 == "/etc/machine-id" && $6 != 0 { bad = 1 } END { exit bad }' "$W/images/setup/recipe/base.tsv" \
  || fail="$fail machine-id"

# The parts and a manifest pointing at local copies.
sh "$ROOT/eac-vm/setup/parts.sh" "$G/parts" > /dev/null
COPYFILE_DISABLE=1 tar --no-mac-metadata --no-xattrs --no-acls --no-fflags --uid 0 --gid 0 --uname root --gname root \
  -C "$G/parts" -cJf "$G/parts.tar.xz" .  # as release.sh makes it
python3 "$ROOT/eac-vm/setup/manifest.py" "$G/parts.tar.xz" "$W/images/setup/packages.json" "$KVER" \
  "$W/images/x86-64-rootfs.erofs" "file://$G/parts.tar.xz" "file://$G/rpms/" > "$G/vm-manifest.json"

# A fresh VM folder: the host tools as the app installs them, then the setup.
VM=$G/vm
rm -rf "$VM"
sh "$ROOT/eac-vm/host/stage.sh" "$VM"
start=$(date +%s)
"$ROOT/.build/release/macneutron" vm-setup --manifest "$G/vm-manifest.json" --vm "$VM" --mkfs "$MKFS" \
  | /usr/bin/grep -v '^progress download' || fail="$fail setup"
echo "vm-setup: the setup took $(( $(date +%s) - start )) s"
[ -f "$VM/images/manifest.sha256" ] || fail="$fail incomplete"

# The images against the builder's, in the builder.
rm -rf "$W/images/gate" && mkdir -p "$W/images/gate"
cp -c "$VM/images/base.erofs" "$VM/images/wine.erofs" "$VM/images/initramfs.img" "$W/images/gate/"
for f in vmlinuz x86-64-rootfs.erofs; do cmp -s "$VM/images/$f" "$W/images/$f" || fail="$fail $f"; done
$B ssh 'cat > /tmp/vm-setup-compare.sh' < "$ROOT/eac-vm/tests/vm-setup-compare.sh"
$B ssh 'sudo bash /tmp/vm-setup-compare.sh' > "$G/compare.txt" 2>&1 || true
cat "$G/compare.txt"
for img in base.erofs wine.erofs initramfs.img; do
  /usr/bin/grep -qx "vm-setup: $img: same" "$G/compare.txt" || fail="$fail $img"
done
rm -rf "$W/images/gate"

# The result boots: the pinned kernel, lockdown, signed modules, the TPM, then sound under FEX and Wine.
cp "$W/share/bin/alsa-play" "$W/share/bin/waveout.exe" "$VM/share/bin/"
{
  printf 'uname -r\ncat /sys/kernel/security/lockdown\ncat /sys/module/module/parameters/sig_enforce\nls /dev/tpm0\n'
  cat "$ROOT/eac-vm/tests/audio.sh"
} > "$G/guest.sh"
MN_VM_DIR=$VM MN_AUDIO_OUTPUT=none MN_RUN_TIMEOUT=600 sh "$VM/run-guest.sh" "$G/guest.sh" > "$G/guest.txt" 2>&1 || true
cat "$G/guest.txt" | /usr/bin/grep -v '^Fontconfig'
/usr/bin/grep -qx "$KVER" "$G/guest.txt" || fail="$fail kernel"
/usr/bin/grep -q '\[integrity\]' "$G/guest.txt" || fail="$fail lockdown"
/usr/bin/grep -qx 'Y' "$G/guest.txt" || fail="$fail sig_enforce"
/usr/bin/grep -qx '/dev/tpm0' "$G/guest.txt" || fail="$fail tpm"
/usr/bin/grep -qx 'audio: ok' "$G/guest.txt" || fail="$fail audio"
# The gate's TPM key, out of the keychain with its folder.
id=$(cat "$VM/images/state.tpm/id" 2>/dev/null || true)
[ -z "$id" ] || security delete-generic-password -s "MacNeutron vTPM" -a "$id" > /dev/null 2>&1 || true

if [ -z "$fail" ]; then echo "vm-setup: ok"; else echo "vm-setup: FAIL$fail"; exit 1; fi
