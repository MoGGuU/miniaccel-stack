#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
DRIVER_DIR="${DRIVER_DIR:-$ROOT_DIR/driver/char}"
if [[ -z "${MODULE_NAME:-}" ]]; then
	MODULE_NAME="$("$ROOT_DIR/scripts/detect-driver-module.sh")"
fi
BDF="${BDF:-00:02.0}"
SYSFS_BDF="${SYSFS_BDF:-0000:$BDF}"
KO_PATH="${KO_PATH:-$DRIVER_DIR/$MODULE_NAME.ko}"
RESOURCE_FILE="/sys/bus/pci/devices/$SYSFS_BDF/resource"

if ((EUID == 0)); then
	SUDO=()
else
	SUDO=(sudo -n)
fi

module_loaded()
{
	lsmod | awk '{print $1}' | grep -qx "$MODULE_NAME"
}

region_claimed()
{
	"${SUDO[@]}" grep -qE "^[[:space:]]*[0-9a-f]+-[0-9a-f]+ : miniaccel$" \
		/proc/iomem
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

[[ -r "$RESOURCE_FILE" ]] || {
	echo "Missing PCI resource file: $RESOURCE_FILE" >&2
	exit 1
}

read -r start end flags _ < "$RESOURCE_FILE"
length=$((end - start + 1))

if ((length <= 0)); then
	echo "BAR0 has invalid length: $length" >&2
	exit 1
fi

cleanup
"${SUDO[@]}" dmesg -C
"${SUDO[@]}" insmod "$KO_PATH"

lspci -nnk -s "$BDF" | grep -q "Kernel driver in use: $MODULE_NAME"
region_claimed

"${SUDO[@]}" dmesg | grep -q "BAR0: start="
if lspci -nn -s "$BDF" | grep -qi "1afe:acc1"; then
	"${SUDO[@]}" dmesg | grep -q "MiniAccel identity: magic=0x4d414343"
	"${SUDO[@]}" dmesg | grep -q "probe ok: MiniAccel"
else
	"${SUDO[@]}" dmesg | grep -q "EDU identifier: 0x010000ed"
	"${SUDO[@]}" dmesg | grep -q "liveness=0xedcba987"
fi

"${SUDO[@]}" rmmod "$MODULE_NAME"

if region_claimed; then
	echo "BAR region remains claimed after rmmod." >&2
	exit 1
fi

if module_loaded; then
	echo "$MODULE_NAME remains loaded after rmmod." >&2
	exit 1
fi

printf 'pci-resources-ok start=%#x end=%#x len=%#x flags=%#x\n' \
	"$start" "$end" "$length" "$flags"
