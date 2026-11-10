#!/usr/bin/env bash
# Sanity checks for the /proc interface. Run after: make load
set -euo pipefail
fail() { echo "FAIL: $*" >&2; exit 1; }

for f in processes summary events; do
	[[ -r /proc/procmon/$f ]] || fail "/proc/procmon/$f missing"
done

head -1 /proc/procmon/processes | grep -q '^# procmon-processes v1' || fail "bad processes header"
head -1 /proc/procmon/summary   | grep -q '^# procmon-summary v1'   || fail "bad summary header"
head -1 /proc/procmon/events    | grep -q '^# procmon-events v1'    || fail "bad events header"

# every data line must have exactly 13 TAB-separated fields
awk -F'\t' 'NR>1 && NF!=13 { print "line " NR ": " NF " fields"; bad=1 } END { exit bad }' \
	/proc/procmon/processes || fail "malformed processes line"

echo "processes: $(($(wc -l < /proc/procmon/processes) - 1)) rows"
echo "OK"
