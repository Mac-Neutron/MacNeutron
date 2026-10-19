#!/bin/sh
# The Fedora builder VM (Virtualization.framework, headless): builds FEX for Linux and the microVM's images, and runs
# the fidelity tests.
#   builder.sh start               boot it (VM_SHARES="tag=/host/dir,..." adds virtiofs shares) and wait for ssh
#   builder.sh stop                power it off
#   builder.sh ssh <command>       run a command in it as `bench`, in BUILDER_HOME (HOME there too)
#   builder.sh sync <host> <guest> copy a host directory into the guest (rsync --delete, without build/); a relative
#                                  <guest> is under BUILDER_HOME
# BUILDER_HOME: /home/bench, or the p profile's /home/bench/p (spec 2026-10-19-macneutron-performance-build §3), whose
# builds then never meet the normal ones'. MACNEUTRON_PROFILE, when set, reaches the command.
# Headless boot leaves the guest's sway session, and so its Steam, stopped.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILDER_DIR=${BUILDER_DIR:-$ROOT/build/eac-vm-work/eacvm/fedora/vm}
BUILDER_KEY=${BUILDER_KEY:-$ROOT/build/eac-vm-work/eacbench/key}
BUILDER_MAC=${BUILDER_MAC:-52:54:00:ea:c0:02}
VMHOST=$ROOT/build/eac-vm-work/vmhost
BUILDER_HOME=${BUILDER_HOME:-/home/bench}
. "$ROOT/wine-arm64/lib.sh"  # P_FLAGS_DEFAULT: the p profile's flags reach builder-side builds resolved
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
ssh)
  shift
  pre="export HOME=$BUILDER_HOME; mkdir -p \"\$HOME\" && cd \"\$HOME\" || exit 1;"
  # Another home still needs the login home's tool settings: FEX finds its x86-64 RootFS through ~/.fex-emu (without
  # it no x86-64 program runs, and Wine's configure decides it is cross-compiling).
  [ "$BUILDER_HOME" = /home/bench ] \
    || pre="$pre for d in .fex-emu .config .local; do [ -e \"\$HOME/\$d\" ] || ln -s /home/bench/\$d \"\$HOME/\$d\"; done;"
  [ -z "${MACNEUTRON_PROFILE:-}" ] \
    || pre="$pre export MACNEUTRON_PROFILE=$MACNEUTRON_PROFILE MACNEUTRON_P_FLAGS='${MACNEUTRON_P_FLAGS-$P_FLAGS_DEFAULT}' MACNEUTRON_PGO_GUEST=$(pgo_mode guest);"
  sshx "$pre" "$@" ;;
sync)
  case $3 in /*) dest=$3 ;; *) dest=$BUILDER_HOME/$3 ;; esac
  sshx mkdir -p "$dest"
  rsync -a --delete --exclude build/ -e "ssh -q -i $BUILDER_KEY -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null" \
    "${2%/}/" "bench@$(ip):$dest/" ;;
*) sed -n '2,8p' "$0"; exit 2 ;;
esac
