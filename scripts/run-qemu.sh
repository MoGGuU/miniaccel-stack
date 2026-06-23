#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
GUEST_DIR="$ROOT_DIR/build/guest"
ROOTFS="$GUEST_DIR/rootfs.qcow2"
SEED_IMAGE="$GUEST_DIR/seed.img"
PID_FILE="$GUEST_DIR/qemu.pid"
SERIAL_LOG="$GUEST_DIR/serial.log"
MONITOR_SOCKET="$GUEST_DIR/monitor.sock"
SSH_PORT="${SSH_PORT:-2222}"
QEMU_SYSTEM_X86_64="${QEMU_SYSTEM_X86_64:-qemu-system-x86_64}"

for file in "$ROOTFS" "$SEED_IMAGE"; do
  [[ -f "$file" ]] || {
    echo "Missing $file; run scripts/prepare-rootfs.sh first." >&2
    exit 1
  }
done

if [[ -f "$PID_FILE" ]] && kill -0 "$(<"$PID_FILE")" 2>/dev/null; then
  echo "QEMU is already running with PID $(<"$PID_FILE")." >&2
  exit 1
fi

rm -f "$PID_FILE" "$SERIAL_LOG" "$MONITOR_SOCKET"

"$QEMU_SYSTEM_X86_64" \
  -name miniaccel-guest \
  -machine q35,accel=tcg \
  -cpu max \
  -smp 2 \
  -m 1024 \
  -device edu \
  -fsdev "local,id=miniaccel_fs,path=$ROOT_DIR,security_model=mapped-xattr" \
  -device "virtio-9p-pci,fsdev=miniaccel_fs,mount_tag=miniaccel" \
  -drive "file=$ROOTFS,if=virtio,format=qcow2" \
  -drive "file=$SEED_IMAGE,if=virtio,format=raw,readonly=on" \
  -netdev "user,id=net0,hostfwd=tcp:127.0.0.1:$SSH_PORT-:22" \
  -device virtio-net-pci,netdev=net0 \
  -display none \
  -serial "file:$SERIAL_LOG" \
  -monitor "unix:$MONITOR_SOCKET,server=on,wait=off" \
  -pidfile "$PID_FILE" \
  -daemonize

echo "QEMU started with PID $(<"$PID_FILE"); SSH port is $SSH_PORT."
