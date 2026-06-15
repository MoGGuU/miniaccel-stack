#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
PID_FILE="$ROOT_DIR/build/guest/qemu.pid"

if [[ ! -f "$PID_FILE" ]] || ! kill -0 "$(<"$PID_FILE")" 2>/dev/null; then
  rm -f "$PID_FILE"
  echo "QEMU is not running."
  exit 0
fi

QEMU_PID="$(<"$PID_FILE")"
ssh -F "$SCRIPT_DIR/ssh-config" -i "$ROOT_DIR/.ssh/id_ed25519" \
  miniaccel 'sudo poweroff' 2>/dev/null || true
for _ in {1..30}; do
  kill -0 "$QEMU_PID" 2>/dev/null || {
    rm -f "$PID_FILE"
    echo "QEMU stopped."
    exit 0
  }
  sleep 1
done

echo "Guest did not power off within 30 seconds." >&2
exit 1
