#!/usr/bin/env bash
set -euo pipefail
if lsmod | grep -q '^procmon '; then
	sudo rmmod procmon
	sudo dmesg | grep procmon | tail -n 1
else
	echo "procmon is not loaded"
fi
