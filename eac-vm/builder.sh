#!/bin/sh
# The Fedora builder VM (Virtualization.framework, headless): builds FEX for Linux and the microVM's images, and runs
# the fidelity tests.
#   builder.sh start               boot it (VM_SHARES="tag=/host/dir,..." adds virtiofs shares) and wait for ssh
#   builder.sh stop                power it off
#   builder.sh ssh <command>       run a command in it as `bench`
#   builder.sh sync <host> <guest> copy a host directory into the guest (rsync --delete, without build/)
# Headless boot leaves the guest's sway session, and so its Steam, stopped.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILDER_DIR=${BUILDER_DIR:-$ROOT/build/eac-vm-work/eacvm/fedora/vm}
BUILDER_KEY=${BUILDER_KEY:-$ROOT/build/eac-vm-work/eacbench/key}
BUILDER_MAC=${BUILDER_MAC:-52:54:00:ea:c0:02}
VMHOST=$ROOT/build/eac-vm-work/vmhost
SRC=$ROOT/eac-vm/host/vmhost.swift

ip() {  # the lease macOS's DHCP server gave our MAC (written without leading zeros)
  mac=$(echo "$BUILDER_MAC" | sed -E 's/(^|:)0([0-9a-f])/\1\2/g')
  awk -v m="1,$mac" '/ip_address=/ {split($0,a,"="); ip=a[2]} /hw_address=/ {split($0,a,"="); if (a[2]==m) print ip}' \
    /var/db/dhcpd_leases | head -1
}
sshx() { ssh -q -i "$BUILDER_KEY" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5 "bench@$(ip)" "$@"; }

case ${1:-} in
start)
  if [ ! -x "$VMHOST" ] || [ "$SRC" -nt "$VMHOST" ]; then
    swiftc -O -o "$VMHOST" "$SRC" "$ROOT/eac-vm/host/host_ring.c" -import-objc-header "$ROOT/eac-vm/host/host_ring.h"
    codesign -s - -f --entitlements "$ROOT/eac-vm/host/ent.plist" "$VMHOST"
  fi
  if [ -f "$BUILDER_DIR/vmhost.pid" ] && kill -0 "$(cat "$BUILDER_DIR/vmhost.pid")" 2>/dev/null; then
    echo "builder already running"
  else
    mkdir -p "$BUILDER_DIR/share"
    VM_CPUS=12 VM_MEM_GB=16 VM_MAC=$BUILDER_MAC nohup "$VMHOST" "$BUILDER_DIR" > "$BUILDER_DIR/host.log" 2>&1 &
    echo $! > "$BUILDER_DIR/vmhost.pid"
  fi
  i=0
  until sshx true 2>/dev/null; do
    i=$((i + 1)); [ $i -lt 60 ] || { echo "builder: no ssh after 120 s" >&2; exit 1; }
    sleep 2
  done
  echo "builder up at $(ip)" ;;
stop)
  pid=$(cat "$BUILDER_DIR/vmhost.pid" 2>/dev/null || true)
  sshx sudo systemctl poweroff 2>/dev/null || true
  i=0
  while [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; do
    i=$((i + 1)); [ $i -lt 30 ] || { kill -TERM "$pid"; break; }
    sleep 2
  done
  rm -f "$BUILDER_DIR/vmhost.pid" ;;
ssh) shift; sshx "$@" ;;
sync)
  rsync -a --delete --exclude build/ -e "ssh -q -i $BUILDER_KEY -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null" \
    "${2%/}/" "bench@$(ip):$3/" ;;
*) sed -n '2,8p' "$0"; exit 2 ;;
esac
