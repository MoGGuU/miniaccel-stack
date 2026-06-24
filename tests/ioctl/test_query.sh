#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
DRIVER_DIR="${DRIVER_DIR:-$ROOT_DIR/driver/char}"
MODULE_NAME="${MODULE_NAME:-miniaccel_drv}"
KO_PATH="${KO_PATH:-$DRIVER_DIR/$MODULE_NAME.ko}"
INFO_BIN="${INFO_BIN:-/tmp/miniaccel-info-test}"

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
	rm -f "$INFO_BIN"
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
	"$ROOT_DIR/tools/miniaccel-info.c" -o "$INFO_BIN"

unload_module
"${SUDO[@]}" insmod "$KO_PATH"

output="$("${SUDO[@]}" "$INFO_BIN")"
echo "$output"

grep -q "device_id=0x11e8" <<<"$output"
grep -q "version=0x010000ed" <<<"$output"
grep -q "capabilities=0x0000000000000001" <<<"$output"

echo "query-ioctl-ok"
