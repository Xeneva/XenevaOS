#!/usr/bin/env bash
# Tests/dcl_test_dclapp.sh -- the userspace half of the gate.
#
# Tests/dcl_test_all.sh asserts the kernel's own boot-time tests; this asserts the
# *dcltest* application, which reaches the same devices the way a real
# program does -- through devfs, with file descriptors, from Process/dcltest.
# The distinction matters: the kernel self-test reads /dev/hwrng through its
# own shim, while dcltest opens the node, reads it, and checks the bytes, so
# a cdev/registration bug that the kernel test cannot see shows up here.
#
# Pass a boot log to re-check one that already exists (that is how Tests/dcl_test_all.sh
# reuses its own run); with no argument it boots QEMU itself, in a window.
# Windowed rather than headless on purpose: headless wraps QEMU in a
# `timeout 120` that always burns the full 120s, and it hides the boot you
# would otherwise be watching. Press Enter at the resolution menu, then close
# the window to run the checks against the captured log.
#
# Usage:
#   ./Tests/dcl_test_dclapp.sh                 boot in a window and check
#   ./Tests/dcl_test_dclapp.sh <bootlog>       check an existing log
#
set -uo pipefail

cd "$(dirname "$0")/.." || exit 1

EXP_DCLTEST=33
fails=0
ok()  { printf '  %-46s ok\n' "$1"; }
bad() { printf '  %-46s FAILED (%s)\n' "$1" "${2:-}"; fails=$((fails + 1)); }

if [ $# -ge 1 ]; then
	LOG=$1
else
	LOG=/tmp/xeneva/test_dclapp.log
	mkdir -p "$(dirname "$LOG")"
	: >"$LOG"
	echo "booting (log: $LOG) -- close the QEMU window when the tests are done"
	# Same reason Tests/dcl_test_all.sh builds first: the general build only
	# ships user apps under --force-user-apps, so a window booted straight
	# from the tree would run whatever stale copy is sitting in Resources/.
	for app in Init Clipd dcltest XEShell Terminal; do
		( make -C "Process/$app" llvm ) >>"$LOG" 2>&1
	done
	cp -f Process/Init/init.exe Process/Clipd/clipd.exe \
		Process/dcltest/dcltest.exe Process/XEShell/xesh.exe \
		Process/Terminal/term.exe \
		Resources/resources/ >>"$LOG" 2>&1
	# The guest side of the clipboard bridge is now in the image; this end
	# is a separate process because which way it copies is the operator's
	# choice, not the guest's.
	echo "  copy host <-> VM from another terminal:"
	echo "    ./Tests/clip_bridge.py --dir=bi --watch"
	./Scripts/Linux/build_and_run_qemu.sh \
		--llvm --no-bt --no-audio >>"$LOG" 2>&1
fi

[ -f "$LOG" ] || { echo "no log at $LOG"; exit 1; }

expect() {   # label pattern
	if grep -qF -- "$2" "$LOG"; then ok "$1"; else bad "$1" "missing: $2"; fi
}

echo "dcltest ($LOG)"

expect "summary" "[dcltest] SUMMARY $EXP_DCLTEST ok, 0 failed"

# clipd is spawned by init and reports in before it starts its loop; no
# host on the other end is needed to get that far, and none is expected
# here -- the bridge that would supply one is run by hand, not part of
# the gate.
expect "clipd running" "[clipd] ready"

# Every PASS line must be there and no line may say FAIL -- a summary of
# "27 ok, 0 failed" is only meaningful if it is the whole story.
passes=$(grep -c '\[dcltest\] PASS' "$LOG" || true)
if [ "$passes" -eq "$EXP_DCLTEST" ]; then
	ok "PASS lines = $passes"
else
	bad "PASS lines" "got $passes, want $EXP_DCLTEST"
fi

if grep -q '\[dcltest\] FAIL' "$LOG"; then
	bad "no dcltest FAIL lines" "$(grep -m1 '\[dcltest\] FAIL' "$LOG" | cut -c1-90)"
else
	ok "no dcltest FAIL lines"
fi

# The checks that only userspace can make: opening the nodes through devfs
# and getting bytes back out of them.
expect "opened /dev/hwrng"   "[dcltest] PASS open /dev/hwrng"
expect "read /dev/hwrng"     "[dcltest] PASS hwrng read (16)"
expect "urandom distinct"    "[dcltest] PASS urandom samples differ"
expect "opened /dev/kmsg"    "[dcltest] PASS open /dev/kmsg"
expect "opened /dev/dcl"     "[dcltest] PASS open /dev/dcl"

echo
if [ "$fails" -eq 0 ]; then
	echo "test_dclapp: ALL GREEN"
	exit 0
fi
echo "test_dclapp: $fails FAILED"
exit 1
