#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
DRIVER_DIR="${DRIVER_DIR:-$ROOT_DIR/driver/char}"
MODULE_NAME="${MODULE_NAME:-miniaccel_drv}"
KO_PATH="${KO_PATH:-$DRIVER_DIR/$MODULE_NAME.ko}"
RUN_BIN="${RUN_BIN:-/tmp/miniaccel-run-sync-test}"

if ((EUID == 0)); then
	SUDO=()
else
	SUDO=(sudo -n)
fi

module_loaded()
{
	lsmod | awk '{print $1}' | grep -qx "$MODULE_NAME"
}

cleanup()
{
	unload_module
	rm -f "$RUN_BIN"
}

unload_module()
{
	if module_loaded; then
		"${SUDO[@]}" rmmod "$MODULE_NAME"
	fi
}

trap cleanup EXIT

[[ -f "$KO_PATH" ]] || {
	echo "Missing $KO_PATH; build the driver first." >&2
	exit 1
}

gcc -Wall -Wextra -Werror -I"$ROOT_DIR/include/uapi" \
	"$ROOT_DIR/tools/miniaccel-run-sync.c" -o "$RUN_BIN"

unload_module
"${SUDO[@]}" insmod "$KO_PATH"

run_and_check()
{
	local input="$1"
	local expected="$2"
	local output

	output="$("${SUDO[@]}" "$RUN_BIN" "$input" 1000)"
	echo "$output"

	grep -q "input=$input" <<<"$output"
	grep -q "output=$expected" <<<"$output"
}

run_and_check 0 1
run_and_check 5 120
run_and_check 12 479001600

echo "run-sync-ioctl-ok"
