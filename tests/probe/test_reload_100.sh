#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
DRIVER_DIR="${DRIVER_DIR:-$ROOT_DIR/driver/char}"
if [[ -z "${MODULE_NAME:-}" ]]; then
	MODULE_NAME="$("$ROOT_DIR/scripts/detect-driver-module.sh")"
fi
KO_PATH="${KO_PATH:-$DRIVER_DIR/$MODULE_NAME.ko}"
COUNT="${COUNT:-100}"

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

for i in $(seq 1 "$COUNT"); do
	"${SUDO[@]}" insmod "$KO_PATH"
	"${SUDO[@]}" rmmod "$MODULE_NAME"
done

if "${SUDO[@]}" dmesg | tail -300 | grep -E "(Oops|BUG:|WARNING:|Call Trace|panic)"; then
	exit 1
fi

echo "reload-${COUNT}-ok"
echo "no-recent-oops-warning-panic"
