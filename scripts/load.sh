#!/usr/bin/env bash
# Load procmon.ko (reloads if already loaded) and show the interface.
set -euo pipefail
cd "$(dirname "$0")/.."
KO=kmod/procmon.ko
[[ -f $KO ]] || { echo "build first: make kmod" >&2; exit 1; }

if lsmod | grep -q '^procmon '; then
	echo "procmon already loaded, reloading..."
	sudo rmmod procmon
fi
sudo insmod "$KO" "$@"          # e.g. ./scripts/load.sh max_events=4096
sudo dmesg | grep procmon | tail -n 3
ls -l /proc/procmon/
