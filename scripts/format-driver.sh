#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"

if (($#)); then
	FILES=("$@")
else
	FILES=(
		"$ROOT_DIR/driver/char/miniaccel_drv.c"
		"$ROOT_DIR/driver/char/miniaccel_edu_drv.c"
	)
fi

clang-format -i -style=file "${FILES[@]}"
