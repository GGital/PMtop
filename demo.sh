#!/usr/bin/env bash
# demo.sh - Builds, loads, uses and unloads the pmon module, printing every
#           command before it runs, and checking the result.
#
#   ./demo.sh            run all steps
#   ./demo.sh --pause    stop after each step (take your screenshot,
#                        then press Enter)
set -uo pipefail
cd "$(dirname "$0")"

PAUSE=0
for a in "$@"; do
	case $a in
		--pause) PAUSE=1 ;;
		*) echo "usage: $0 [--pause]"; exit 1 ;;
	esac
done

B=$'\e[1m'; C=$'\e[36m'; G=$'\e[32m'; R=$'\e[31m'; Y=$'\e[33m'; N=$'\e[0m'
FAILS=0

title() { echo; echo "${B}${Y}==== $* ====${N}"; }
step()  { echo; echo "${B}# $*${N}"; }
# show the command, then run it
run()   { echo "${C}\$ $*${N}"; eval "$@"; }
check() {  # check "description" command...
	local what=$1; shift
	if eval "$@" >/dev/null 2>&1; then echo "${G}  ✔ $what${N}"
	else echo "${R}  ✘ $what${N}"; FAILS=$((FAILS + 1)); fi
}
pause() { [ $PAUSE -eq 1 ] && read -rp "  [Enter] to continue " _ || true; }
unload_if_loaded() { lsmod | grep -q "^$1 " && sudo rmmod "$1"; true; }
klog() { sudo dmesg | grep -E "$1" | tail -n "${2:-1}"; }

sudo -v || exit 1     # ask for the password once, up front

# ------------------------------------------------------------------
demo() {
	title "pmon v2.0 - process list + fork/exit events"
	cd module
	step "Compile"
	run make -s 2>&1 | grep -v "differs from\|You are using\|built by\|built with\|^  " || true
	run "modinfo -F version pmon.ko"
	check "version is 2.0" "modinfo -F version pmon.ko | grep -qx 2.0"
	pause

	step "Load it"
	unload_if_loaded pmon
	run sudo insmod pmon.ko
	run "sudo dmesg | grep 'pmon v' | tail -1"
	run ls -l /proc/pmon /proc/pmon_events
	check "/proc/pmon still works" "tail -1 /proc/pmon | grep -q 'total:'"
	check "/proc/pmon_events exists" test -r /proc/pmon_events
	pause

	step "Make some known events happen"
	run "sh -c 'exit 3'"
	run "sleep 300 & SP=\$!; sleep 0.2; kill -9 \$SP; wait \$SP 2>/dev/null; echo killed \$SP"
	run "ls / > /dev/null"
	sleep 0.2
	pause

	step "Use it: read the events the kernel recorded"
	run "tail -8 /proc/pmon_events"
	check "saw 'sh' exit with code 3" "grep -E 'EXIT .* sh +exit code 3' /proc/pmon_events"
	check "saw 'sleep' killed by signal 9" "grep -E 'EXIT .* sleep +killed by signal 9' /proc/pmon_events"
	check "saw 'ls' exit normally" "grep -E 'EXIT .* ls +exit code 0' /proc/pmon_events"
	pause

	step "Unload"
	run sudo rmmod pmon
	run "sudo dmesg | grep 'pmon v' | tail -1"
	check "both files removed" "! test -e /proc/pmon && ! test -e /proc/pmon_events"
	cd ..
}

demo

echo
if [ $FAILS -eq 0 ]; then echo "${B}${G}ALL CHECKS PASSED${N}"
else echo "${B}${R}$FAILS CHECK(S) FAILED${N}"; fi
exit $((FAILS > 0))
