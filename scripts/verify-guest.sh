#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
SSH=(ssh -F "$SCRIPT_DIR/ssh-config" -i "$ROOT_DIR/.ssh/id_ed25519" miniaccel)
DEADLINE=$((SECONDS + 600))

echo "Waiting for SSH and cloud-init..."
until "${SSH[@]}" 'cloud-init status --wait >/dev/null 2>&1' 2>/dev/null; do
  if ((SECONDS >= DEADLINE)); then
    echo "Guest did not become ready within 10 minutes." >&2
    exit 1
  fi
  sleep 5
done

EDU_LINE="$("${SSH[@]}" "lspci -nn | grep -i '1234:11e8'")"
echo "Guest is ready. EDU device: $EDU_LINE"
