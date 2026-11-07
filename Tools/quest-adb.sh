#!/usr/bin/env bash
# Quest 2 ADB helper. Uses Tools/platform-tools/adb (no root install).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
ADB="$ROOT/platform-tools/adb"
if [ ! -x "$ADB" ]; then
	echo "adb missing. unzip platform-tools into $ROOT/platform-tools/"
	exit 1
fi
export PATH="$ROOT/platform-tools:$PATH"
"$ADB" start-server
echo "=== adb devices ==="
"$ADB" devices -l
echo "=== usb (Quest is 2833:) ==="
lsusb | grep -iE '2833|oculus|meta|quest' || echo "(no Quest on USB — plug a USB3 cable, enable USB debugging, accept the prompt)"
if "$ADB" devices | grep -q $'\tdevice$'; then
	echo "=== reverse WiVRn 9757 ==="
	"$ADB" reverse tcp:9757 tcp:9757 || true
	"$ADB" reverse --list
	echo "Quest is authorized. WiVRn USB: keep this cable in, start wivrn-server, open WiVRn on the headset."
fi
