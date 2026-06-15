#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"

exec ssh -F "$SCRIPT_DIR/ssh-config" -i "$ROOT_DIR/.ssh/id_ed25519" miniaccel "$@"
