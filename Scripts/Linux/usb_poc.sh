#!/usr/bin/env bash
# One entry point for the USB mass-storage and HID proof of concept.
# Run with no arguments (or "demo") for the live desktop, "verify" for the
# automated gate, "commands" for the XEShell card, and "evidence" for the
# latest gate log.

set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
MODE=${1:-demo}
shift || true

usage() {
	cat <<'EOF'
usage:
  usb_poc.sh demo [--fresh] [--skip-build] [build options...]
  usb_poc.sh verify
  usb_poc.sh commands
  usb_poc.sh evidence [log]

  demo      print the XEShell command card, then boot the live desktop
  verify    run the automated DCL gate (Tests/dcl_test_all.sh)
  commands  print only the XEShell command card
  evidence  summarize the latest gate log for a pull request
EOF
}

command_card() {
	cat <<'EOF'
XEShell -- USB mass storage
===========================
  systeminfo
  cd /
  ls
  cd b
  ls
  cd Demo
  ls
  cd /
  umount b
  cd b          # expected: the directory is gone
  mount b
  cd b
  ls

HID -- no shell command
=======================
  move the USB mouse      -> pointer moves
  click the launcher      -> Terminal opens
  type the commands above -> USB keyboard
EOF
}

evidence() {
	local log=${1:-${LOG:-/tmp/xeneva/test_all.log}}
	if [ ! -f "$log" ]; then
		echo "usb_poc: no gate log at $log" >&2
		echo "usb_poc: run '$0 verify' first" >&2
		exit 1
	fi
	echo "gate log: $log"
	grep -E \
		'\[dcltest\] SUMMARY|\[bttest\] SUMMARY|\[dcltest\] PASS vfs|\[xhci-hid\]: report iface|HID (keyboard|pointer) ready|\[xhci-hotplug\]: disconnect|\[xhci-msc\]: vdisk reuse|\[fat\]: USB|\[vdisk\]: USB|test_all: (ALL GREEN|[0-9]+ FAILED)' \
		"$log" || true
}

case "$MODE" in
demo)
	command_card
	echo
	echo "expected serial evidence:"
	echo "  [xhci-hid]: report iface ... kind keyboard"
	echo "  [xhci]: HID pointer ready on port 8.8.1"
	echo "  [fat]: USB QEMU HARDDISK mounted at b"
	echo "  [vdisk]: USB QEMU HARDDISK unmounted from b"
	echo
	exec "$ROOT/Scripts/Linux/demo_xeneva.sh" "$@"
	;;
verify)
	exec "$ROOT/Tests/dcl_test_all.sh" "$@"
	;;
commands)
	command_card
	;;
evidence)
	evidence "$@"
	;;
*)
	usage >&2
	exit 1
	;;
esac
