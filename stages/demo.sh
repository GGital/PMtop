#!/usr/bin/env bash
# demo.sh - Builds, loads, uses and unloads each stage, printing every
#           command before it runs, and checking the result.
#
#   ./demo.sh            run stages 1, 2, 3
#   ./demo.sh 2          run only stage 2
#   ./demo.sh --pause 3  stop after each step (take your screenshot,
#                        then press Enter)
set -uo pipefail
cd "$(dirname "$0")"

PAUSE=0
STAGES=()
for a in "$@"; do
	case $a in
		--pause) PAUSE=1 ;;
		1|2|3)   STAGES+=("$a") ;;
		*) echo "usage: $0 [--pause] [1] [2] [3]"; exit 1 ;;
	esac
done
[ ${#STAGES[@]} -eq 0 ] && STAGES=(1 2 3)

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
stage1() {
	title "STAGE 1: hello - the smallest kernel module"
	cd 1_hello
	step "Compile it against the running kernel's headers"
	run make -s 2>&1 | grep -v "differs from\|You are using\|built by\|built with\|^  " || true
	run ls -l hello.ko
	check "hello.ko was built" test -f hello.ko
	pause

	step "Look inside the module file"
	run modinfo hello.ko
	pause

	step "Load it with a parameter"
	unload_if_loaded hello
	run sudo insmod hello.ko who=student
	run "lsmod | grep '^hello'"
	run cat /sys/module/hello/parameters/who
	run "sudo dmesg | grep 'hello:' | tail -1"
	check "module is loaded" "lsmod | grep -q '^hello '"
	check "init() ran: greeting in kernel log" "klog 'hello: Hello, student' | grep -q student"
	pause

	step "Unload it"
	run sudo rmmod hello
	run "sudo dmesg | grep 'hello:' | tail -1"
	check "exit() ran: goodbye in kernel log" "klog 'hello: Goodbye' | grep -q Goodbye"
	check "module is gone" "! lsmod | grep -q '^hello '"
	cd ..
	pause
}

# ------------------------------------------------------------------
stage2() {
	title "STAGE 2: pmon v1.0 - list processes in /proc/pmon"
	cd 2_proclist
	step "Compile"
	run make -s 2>&1 | grep -v "differs from\|You are using\|built by\|built with\|^  " || true
	run "modinfo pmon.ko | grep -E '^(version|description|parm)'"
	check "version is 1.0" "modinfo -F version pmon.ko | grep -qx 1.0"
	pause

	step "Load it: a new file appears in /proc"
	unload_if_loaded pmon
	run sudo insmod pmon.ko
	run "sudo dmesg | grep 'pmon v' | tail -1"
	run ls -l /proc/pmon
	check "/proc/pmon exists" test -r /proc/pmon
	pause

	step "Use it: read the kernel's process list"
	run "head -12 /proc/pmon"
	run "tail -1 /proc/pmon"
	pause

	step "Evidence: compare one process with ps"
	sleep 300 & local SP=$!
	sleep 0.2
	run "grep -E '^$SP ' /proc/pmon"
	run "ps -o pid,ppid,stat,nlwp,rss,comm -p $SP"
	check "our sleep ($SP) is listed" "grep -qE '^$SP +[0-9]+ +S +1 +[0-9]+ +sleep' /proc/pmon"
	check "PPID, threads and RSS match ps exactly" \
		"[ \"\$(awk '\$1==$SP {print \$2, \$4, \$5}' /proc/pmon)\" = \"\$(ps -o ppid=,nlwp=,rss= -p $SP | xargs)\" ]"
	kill $SP; wait $SP 2>/dev/null
	pause

	step "Change a parameter while loaded: hide kernel threads"
	local before after
	before=$(tail -1 /proc/pmon)
	run "echo N | sudo tee /sys/module/pmon/parameters/show_kthreads"
	after=$(tail -1 /proc/pmon)
	echo "  before: $before"
	echo "  after:  $after"
	check "fewer processes without kernel threads" "[ '${after//[^0-9]/}' -lt '${before//[^0-9]/}' ]"
	run "echo Y | sudo tee /sys/module/pmon/parameters/show_kthreads"
	pause

	step "Unload: the file disappears"
	run sudo rmmod pmon
	run "ls /proc/pmon 2>&1 || true"
	check "/proc/pmon removed" "! test -e /proc/pmon"
	cd ..
	pause
}

# ------------------------------------------------------------------
stage3() {
	title "STAGE 3: pmon v2.0 - improved: watch fork/exit events"
	step "What changed from v1.0 (summary of the diff)"
	run "diff -u 2_proclist/pmon.c 3_events/pmon.c | grep -c '^+[^+]' | xargs echo 'lines added:'"
	run "diff -u 2_proclist/pmon.c 3_events/pmon.c | grep '^+.*NEW' | head -12"
	pause

	cd 3_events
	step "Compile"
	run make -s 2>&1 | grep -v "differs from\|You are using\|built by\|built with\|^  " || true
	run "modinfo -F version pmon.ko"
	check "version is now 2.0" "modinfo -F version pmon.ko | grep -qx 2.0"
	pause

	step "Load the new version"
	unload_if_loaded pmon
	run sudo insmod pmon.ko
	run "sudo dmesg | grep 'pmon v' | tail -1"
	run ls -l /proc/pmon /proc/pmon_events
	check "/proc/pmon still works" "tail -1 /proc/pmon | grep -q 'total:'"
	check "/proc/pmon_events exists (new)" test -r /proc/pmon_events
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

for s in "${STAGES[@]}"; do "stage$s"; done

echo
if [ $FAILS -eq 0 ]; then echo "${B}${G}ALL CHECKS PASSED${N}"
else echo "${B}${R}$FAILS CHECK(S) FAILED${N}"; fi
exit $((FAILS > 0))
