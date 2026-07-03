#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
DRIVER_DIR="${DRIVER_DIR:-$ROOT_DIR/driver/char}"
MODULE_NAME="${MODULE_NAME:-miniaccel_drv}"
KO_PATH="${KO_PATH:-$DRIVER_DIR/$MODULE_NAME.ko}"
RUN_BIN="${RUN_BIN:-/tmp/miniaccel-run-sync-irq-wait}"
WAIT_BIN="${WAIT_BIN:-/tmp/miniaccel-wait-irq-wait}"
INVALID_BIN="${INVALID_BIN:-/tmp/miniaccel-test-invalid-irq-wait}"
CONCURRENT_BIN="${CONCURRENT_BIN:-/tmp/miniaccel-test-concurrent-irq-wait}"
WAIT_OUT="${WAIT_OUT:-/tmp/miniaccel-irq-wait.out}"
TIMEOUT_OUT="${TIMEOUT_OUT:-/tmp/miniaccel-irq-timeout.out}"
FORCE_OUT="${FORCE_OUT:-/tmp/miniaccel-force-timeout.out}"
FORCE_ERR="${FORCE_ERR:-/tmp/miniaccel-force-timeout.err}"

if ((EUID == 0)); then
	SUDO=()
else
	SUDO=(sudo -n)
fi

module_loaded()
{
	lsmod | awk '{print $1}' | grep -qx "$MODULE_NAME"
}

unload_module()
{
	if module_loaded; then
		"${SUDO[@]}" rmmod "$MODULE_NAME"
	fi
}

cleanup()
{
	unload_module
	rm -f "$RUN_BIN" "$WAIT_BIN" "$INVALID_BIN" "$CONCURRENT_BIN"
	rm -f "$WAIT_OUT" "$TIMEOUT_OUT" "$FORCE_OUT" "$FORCE_ERR"
	rm -f /tmp/miniaccel-irq-mp-run-sync.*
}

trap cleanup EXIT

[[ -f "$KO_PATH" ]] || {
	echo "Missing $KO_PATH; build the driver first." >&2
	exit 1
}

gcc -Wall -Wextra -Werror -I"$ROOT_DIR/include/uapi" \
	"$ROOT_DIR/tools/miniaccel-run-sync.c" -o "$RUN_BIN"
gcc -Wall -Wextra -Werror -I"$ROOT_DIR/include/uapi" \
	"$ROOT_DIR/tools/miniaccel-wait.c" -o "$WAIT_BIN"
gcc -Wall -Wextra -Werror -I"$ROOT_DIR/include/uapi" \
	"$ROOT_DIR/tests/ioctl/test_invalid_args.c" -o "$INVALID_BIN"
gcc -Wall -Wextra -Werror -pthread -I"$ROOT_DIR/include/uapi" \
	"$ROOT_DIR/tests/ioctl/test_concurrent_run_sync.c" -o "$CONCURRENT_BIN"

load_module()
{
	unload_module
	"${SUDO[@]}" dmesg -C
	"${SUDO[@]}" insmod "$KO_PATH" "$@"
}

irq_count()
{
	awk '
		/miniaccel_drv/ {
			for (i = 2; i <= NF; i++) {
				if ($i ~ /^[0-9]+$/)
					total += $i
			}
		}
		END { print total + 0 }
	' /proc/interrupts
}

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

load_module

irq_before="$(irq_count)"
run_and_check 5 120
irq_after="$(irq_count)"
if ((irq_after <= irq_before)); then
	echo "IRQ count did not increase: before=$irq_before after=$irq_after" >&2
	exit 1
fi
echo "normal-irq-ok before=$irq_before after=$irq_after"

"${SUDO[@]}" "$WAIT_BIN" --timeout-ms 3000 >"$WAIT_OUT" &
wait_pid="$!"
sleep 0.2
run_and_check 6 720
wait "$wait_pid"
cat "$WAIT_OUT"
grep -q "readable" "$WAIT_OUT"
echo "poll-wakeup-ok"

"${SUDO[@]}" "$WAIT_BIN" --timeout-ms 100 >"$TIMEOUT_OUT"
cat "$TIMEOUT_OUT"
grep -q "timeout timeout_ms=100" "$TIMEOUT_OUT"
echo "poll-no-stale-event-ok"

unload_module
load_module force_timeout=1
if "${SUDO[@]}" "$RUN_BIN" 5 1000 >"$FORCE_OUT" 2>"$FORCE_ERR"; then
	cat "$FORCE_OUT"
	echo "RUN_SYNC unexpectedly succeeded with force_timeout=1" >&2
	exit 1
fi
cat "$FORCE_ERR"
grep -qi "timed out" "$FORCE_ERR"
echo "forced-timeout-ok"

"${SUDO[@]}" bash -c \
	'printf 0 >/sys/module/miniaccel_drv/parameters/force_timeout'
run_and_check 5 120
echo "timeout-cleanup-ok"

"${SUDO[@]}" "$INVALID_BIN"
echo "invalid-ioctl-ok"

"${SUDO[@]}" "$CONCURRENT_BIN" /dev/miniaccel0 8 50

pids=()
for i in $(seq 0 19); do
	"${SUDO[@]}" "$RUN_BIN" "$((i % 13))" 1000 \
		>"/tmp/miniaccel-irq-mp-run-sync.$i" &
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

if "${SUDO[@]}" dmesg | tail -200 |
	grep -E "(Oops|BUG:|WARNING:|Call Trace|panic)"; then
	exit 1
fi
echo "dmesg-clean-ok"

unload_module
echo "unload-ok"
echo "irq-wait-sh-ok"
