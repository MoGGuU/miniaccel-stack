#!/usr/bin/env bash
set -euo pipefail

BDF="${BDF:-00:02.0}"

if lspci -nn -s "$BDF" | grep -qi "1afe:acc1"; then
	echo "miniaccel_drv"
elif lspci -nn -s "$BDF" | grep -qi "1234:11e8"; then
	echo "miniaccel_edu_drv"
else
	echo "No supported MiniAccel/EDU PCI device found at $BDF." >&2
	exit 1
fi
