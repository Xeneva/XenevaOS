#!/usr/bin/env bash
# Hotplug the demo USB stick through QEMU's HMP monitor.
#
# The demo starts the stick as device demo-usb0.  Removing a usb-storage
# device also removes the -drive backend it was created from, so adding it
# back needs a fresh drive_add before device_add.
#
# usage: demo_usb_hotplug.sh remove [device-id]
#        demo_usb_hotplug.sh add [image] [port]
#
#   remove  defaults to demo-usb0.
#   add     defaults to Build/xeneva-demo-usb.img and port 4.3 (the guest's
#           root port 8.3).  It prints the generated device id; pass that id
#           to a later `remove`.
#
# XENEVA_MONITOR overrides the monitor socket (default /tmp/xeneva-mon.sock).
# XENEVA_DEMO_USB_IMG overrides the default image.

set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
MON=${XENEVA_MONITOR:-/tmp/xeneva-mon.sock}
DEFAULT_IMG=${XENEVA_DEMO_USB_IMG:-$ROOT/Build/xeneva-demo-usb.img}

usage() {
	cat <<'EOF'
usage: demo_usb_hotplug.sh remove [device-id]
       demo_usb_hotplug.sh add [image] [port]
EOF
}

hmp() {
	python3 - "$MON" "$1" <<'PY'
import socket
import sys
import time

mon, cmd = sys.argv[1], sys.argv[2]
s = socket.socket(socket.AF_UNIX)
s.settimeout(5)
try:
    s.connect(mon)
except OSError as exc:
    sys.exit("demo_usb_hotplug: cannot connect to %s: %s" % (mon, exc))
time.sleep(0.1)
try:
    s.recv(8192)
except OSError:
    pass
s.sendall((cmd + "\n").encode())
time.sleep(0.5)
out = b""
try:
    while True:
        chunk = s.recv(8192)
        if not chunk:
            break
        out += chunk
        if b"(qemu)" in out:
            break
except OSError:
    pass
text = out.decode(errors="replace")
if "Error:" in text:
    for line in text.splitlines():
        if "Error:" in line:
            sys.exit("demo_usb_hotplug: " + line.strip())
PY
}

case "${1:-}" in
remove)
	dev=${2:-demo-usb0}
	hmp "device_del $dev"
	echo "[+] removed $dev"
	;;
add)
	img=${2:-$DEFAULT_IMG}
	port=${3:-4.3}
	if [ ! -f "$img" ]; then
		echo "demo_usb_hotplug: no image at $img" >&2
		exit 1
	fi
	stamp=$(date +%s)
	drive="demo-drive-$stamp"
	dev="demo-usb-$stamp"
	# HMP splits on spaces, so the options go in double quotes (with \ and "
	# escaped); QEMU also needs commas in the path doubled.
	drive_opts="if=none,id=$drive,file=${img//,/,,},format=raw"
	drive_opts=${drive_opts//\\/\\\\}
	drive_opts=${drive_opts//\"/\\\"}
	hmp "drive_add 0 \"$drive_opts\""
	hmp "device_add usb-storage,drive=$drive,id=$dev,port=$port"
	echo "[+] added $dev on port $port (drive $drive)"
	echo "    remove it with: $0 remove $dev"
	;;
*)
	usage >&2
	exit 1
	;;
esac
