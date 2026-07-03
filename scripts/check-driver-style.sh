#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
CHECKPATCH="$ROOT_DIR/tools/kernel/scripts/checkpatch.pl"

if (($#)); then
	FILES=("$@")
else
	FILES=(
		"$ROOT_DIR/driver/char/miniaccel_drv.c"
		"$ROOT_DIR/driver/char/miniaccel_edu_drv.c"
	)
fi

git -C "$ROOT_DIR" diff --check

for file in "${FILES[@]}"; do
	if [[ "$file" != /* ]]; then
		file="$ROOT_DIR/$file"
	fi

	"$CHECKPATCH" --no-tree --strict --file "$file"
done
