#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
DRIVER_DIR="${DRIVER_DIR:-$ROOT_DIR/driver/char}"
if [[ -z "${MODULE_NAME:-}" ]]; then
	MODULE_NAME="$("$ROOT_DIR/scripts/detect-driver-module.sh")"
fi
KO_PATH="${KO_PATH:-$DRIVER_DIR/$MODULE_NAME.ko}"
RUN_BIN="${RUN_BIN:-/tmp/miniaccel-run-sync-test}"
INFO_BIN="${INFO_BIN:-/tmp/miniaccel-info-run-sync-test}"

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
	rm -f "$RUN_BIN" "$INFO_BIN"
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
gcc -Wall -Wextra -Werror -I"$ROOT_DIR/include/uapi" \
	"$ROOT_DIR/tools/miniaccel-info.c" -o "$INFO_BIN"

unload_module
"${SUDO[@]}" insmod "$KO_PATH"

info_output="$("${SUDO[@]}" "$INFO_BIN")"
echo "$info_output"

if grep -q "device_id=0xacc1" <<<"$info_output"; then
	output="$("${SUDO[@]}" "$RUN_BIN" --opcode nop --repeat 3 --timeout-ms 1000)"
	echo "$output"
	grep -q "opcode=nop seqno=1" <<<"$output"
	grep -q "opcode=nop seqno=2" <<<"$output"
	grep -q "opcode=nop seqno=3" <<<"$output"
	grep -q "run-sync-nop-ok repeat=3" <<<"$output"
	echo "run-sync-ioctl-ok"
	exit 0
fi

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
