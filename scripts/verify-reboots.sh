#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

for run in 1 2 3; do
  echo "Cold boot verification $run/3"
  "$SCRIPT_DIR/run-qemu.sh"
  "$SCRIPT_DIR/verify-guest.sh"
  "$SCRIPT_DIR/stop-qemu.sh"
done

echo "Three cold boots completed successfully."

