#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
DRIVER_DIR="${DRIVER_DIR:-$ROOT_DIR/driver/char}"
MODULE_NAME="${MODULE_NAME:-miniaccel_drv}"
KO_PATH="${KO_PATH:-$DRIVER_DIR/$MODULE_NAME.ko}"
TEST_BIN="${TEST_BIN:-/tmp/miniaccel-test-invalid-args}"

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
	rm -f "$TEST_BIN"
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
	"$ROOT_DIR/tests/ioctl/test_invalid_args.c" -o "$TEST_BIN"

unload_module
"${SUDO[@]}" insmod "$KO_PATH"
"${SUDO[@]}" "$TEST_BIN"
