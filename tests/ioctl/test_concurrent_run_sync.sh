#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
DRIVER_DIR="${DRIVER_DIR:-$ROOT_DIR/driver/char}"
MODULE_NAME="${MODULE_NAME:-miniaccel_drv}"
KO_PATH="${KO_PATH:-$DRIVER_DIR/$MODULE_NAME.ko}"
TEST_BIN="${TEST_BIN:-/tmp/miniaccel-test-concurrent-run-sync}"
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
	rm -f "$TEST_BIN" "$RUN_BIN" /tmp/miniaccel-mp-run-sync.*
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

gcc -Wall -Wextra -Werror -pthread -I"$ROOT_DIR/include/uapi" \
	"$ROOT_DIR/tests/ioctl/test_concurrent_run_sync.c" -o "$TEST_BIN"
gcc -Wall -Wextra -Werror -I"$ROOT_DIR/include/uapi" \
	"$ROOT_DIR/tools/miniaccel-run-sync.c" -o "$RUN_BIN"

unload_module
"${SUDO[@]}" dmesg -C
"${SUDO[@]}" insmod "$KO_PATH"

"${SUDO[@]}" "$TEST_BIN" /dev/miniaccel0 8 50

pids=()
for i in $(seq 0 19); do
	"${SUDO[@]}" "$RUN_BIN" "$((i % 13))" 1000 \
		>"/tmp/miniaccel-mp-run-sync.$i" &
	pids+=("$!")
done

for pid in "${pids[@]}"; do
	wait "$pid"
done

echo "multi-process-run-sync-ok count=${#pids[@]}"

"${SUDO[@]}" bash -c '
exec 9<>/dev/miniaccel0
if rmmod miniaccel_drv 2>/tmp/miniaccel-rmmod-open.err; then
	echo "rmmod unexpectedly succeeded with an open fd" >&2
	exit 1
fi
'
echo "open-fd-rmmod-blocked-ok"

if "${SUDO[@]}" dmesg | tail -200 | grep -E "(Oops|BUG:|WARNING:|Call Trace|panic)"; then
	exit 1
fi

echo "concurrent-run-sync-sh-ok"
