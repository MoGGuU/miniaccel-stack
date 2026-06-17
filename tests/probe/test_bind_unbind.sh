#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
DRIVER_DIR="${DRIVER_DIR:-$ROOT_DIR/driver/char}"
MODULE_NAME="${MODULE_NAME:-miniaccel_drv}"
BDF="${BDF:-00:02.0}"
KO_PATH="${KO_PATH:-$DRIVER_DIR/$MODULE_NAME.ko}"

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
	if module_loaded; then
		"${SUDO[@]}" rmmod "$MODULE_NAME"
	fi
}

trap cleanup EXIT

[[ -f "$KO_PATH" ]] || {
	echo "Missing $KO_PATH; build the driver first." >&2
	exit 1
}

cleanup

"${SUDO[@]}" insmod "$KO_PATH"

if ! lspci -nnk -s "$BDF" | grep -q "Kernel driver in use: $MODULE_NAME"; then
	lspci -nnk -s "$BDF" >&2
	exit 1
fi

"${SUDO[@]}" rmmod "$MODULE_NAME"

if module_loaded; then
	echo "$MODULE_NAME is still loaded after rmmod." >&2
	exit 1
fi

"${SUDO[@]}" dmesg | tail -80 | grep -E "$MODULE_NAME.*(probe called|remove called)" >/dev/null

echo "bind-unbind-ok"
