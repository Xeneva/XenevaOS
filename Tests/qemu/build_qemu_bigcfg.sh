#!/usr/bin/env bash
# Build the test-only QEMU used by the xHCI big-configuration-descriptor
# boot in Tests/dcl_test_all.sh.  The patch adds usb-storage-bigcfg and
# raises QEMU's control data buffer from 4 KiB to 8 KiB.
#
# usage: ./build_qemu_bigcfg.sh /path/to/qemu-11.1.2 [build-dir]
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
src=${1:-}
build=${2:-${QEMU_BUILD:-/tmp/opencode/qemu-bigcfg-build}}

if [ -z "$src" ] || [ ! -x "$src/configure" ]; then
	echo "usage: $0 /path/to/qemu-11.1.2 [build-dir]" >&2
	exit 2
fi

for tool in patch meson ninja; do
	if ! command -v "$tool" >/dev/null; then
		echo "[!] missing build tool: $tool" >&2
		exit 1
	fi
done

if ! grep -q usb-storage-bigcfg "$src/hw/usb/dev-storage-classic.c"; then
	patch -d "$src" -p1 --forward <"$here/usb-storage-bigcfg.patch"
fi

mkdir -p "$build"
cd "$build"
"$src/configure" \
	--target-list=aarch64-softmmu \
	--prefix="$build/install" \
	--disable-docs \
	--disable-tools \
	--disable-guest-agent \
	--disable-werror \
	--disable-capstone \
	--disable-vnc \
	--disable-gtk \
	--disable-sdl \
	--disable-spice \
	--disable-opengl \
	--disable-libiscsi \
	--disable-libnfs \
	--disable-rbd
ninja qemu-system-aarch64

echo "[+] QEMU with usb-storage-bigcfg: $build/qemu-system-aarch64"
echo "[+] Gate: PATH=$build:\$PATH BYPASS_VIRTGPU=1 CLEAN=1 ./Tests/dcl_test_all.sh"
