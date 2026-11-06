#!/usr/bin/env bash
# Boot XenevaOS as a live demo.
#
# This is the interactive sibling of Tests/dcl_test_all.sh: it uses the same
# QEMU machine and the same USB controller, but opens the desktop instead of
# running the automated gate. The default topology already includes a USB
# keyboard, a USB tablet, a two-tier hub chain, a Wacom tablet with report
# IDs, and a USB serial port. This wrapper adds a FAT32 USB stick with a few
# files so the storage path is visible in the file manager.
#
# usage: Scripts/Linux/demo_xeneva.sh [--fresh] [--skip-build] [build options...]
#
#   --fresh       Recreate Build/xeneva-demo-usb.img before boot.
#   --skip-build  Reuse the existing kernel and userspace binaries.
#   -h, --help    Show this help.
#
# Any other option is passed to build_and_run_qemu.sh, for example
# --headless or --no-audio. The loader's resolution menu stays enabled:
# choose 1024x768 there so the Terminal window (680x450 at 300,100) fits.
# --no-boot-menu and --headless keep the loader's 640x480 default.
#
# The demo builds the kernel and bootloader but reuses the userspace
# binaries already in Resources/resources. A full --force-user-apps build is
# currently blocked by an unrelated link error in Process/Files
# (ListViewSetActionHandler), and the shipped file.exe is already usable.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEMO_IMG="${XENEVA_DEMO_USB_IMG:-$REPO_ROOT/Build/xeneva-demo-usb.img}"
FRESH=0
SKIP_BUILD=0
PASSTHROUGH=()

usage() {
    cat <<'EOF'
usage: Scripts/Linux/demo_xeneva.sh [--fresh] [--skip-build] [build options...]

  --fresh       Recreate Build/xeneva-demo-usb.img before boot.
  --skip-build  Reuse the existing kernel and userspace binaries.
  -h, --help    Show this help.

Any other option is passed to build_and_run_qemu.sh, for example
--headless or --no-audio. The loader menu stays enabled: choose 1024x768
there so the Terminal window fits. --no-boot-menu and --headless keep the
loader's 640x480 default.
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --fresh) FRESH=1 ;;
        --skip-build) SKIP_BUILD=1 ;;
        -h|--help) usage; exit 0 ;;
        *) PASSTHROUGH+=("$1") ;;
    esac
    shift
done

if ! command -v mkfs.vfat >/dev/null 2>&1 ||
   ! command -v mcopy >/dev/null 2>&1 ||
   ! command -v mmd >/dev/null 2>&1; then
    echo "demo_xeneva: mkfs.vfat, mcopy and mmd are required (mtools/dosfs tools)." >&2
    exit 1
fi

mkdir -p "$(dirname "$DEMO_IMG")"

if [ "$FRESH" -eq 1 ] || [ ! -f "$DEMO_IMG" ]; then
    echo "[+] Creating demo USB stick: $DEMO_IMG (64 MiB FAT32)"
    dd if=/dev/zero of="$DEMO_IMG" bs=1M count=64 status=none
    mkfs.vfat -F 32 -n XENEVA "$DEMO_IMG" >/dev/null
    mmd -i "$DEMO_IMG" ::/Demo

    welcome="$(mktemp)"
    cat >"$welcome" <<'EOF'
XenevaOS live demo
==================

This file lives on the USB stick attached to the xHCI controller.

What the stick proves:
  - USB mass storage enumeration (Bulk-Only Transport)
  - SCSI INQUIRY / READ CAPACITY / block reads
  - FAT32 mount in the VFS
  - the Files application can browse a removable volume

The desktop also has a two-tier USB hub chain:
  root port 8 -> hub 0 -> port 8 -> hub 1 -> port 1 -> Wacom tablet
The guest log names that path 8.8.1. The Wacom report descriptor uses
report IDs, so the HID parser records an ID stream rather than assuming
every report starts with a usage field.

Try from the Terminal:
  systeminfo
  neofetch
  cd /
  ls
  cd b
  ls
  cd Demo
  ls
  cd /
  umount b
  cd b
  mount b
  cd b
  ls
The USB volume appears as the single-letter mount point b. The Files
app shows it as a removable drive and lists this Demo folder too. The
umount/mount pair detaches and reattaches the same volume without
allocating a new letter.
EOF
    mcopy -i "$DEMO_IMG" "$welcome" ::/Demo/Welcome.txt
    rm -f "$welcome"

    mcopy -i "$DEMO_IMG" "$REPO_ROOT/Resources/resources/XE1_2.jpg" ::/Demo/Xeneva.jpg
    mcopy -i "$DEMO_IMG" "$REPO_ROOT/Resources/resources/snow.jpg" ::/Demo/Snow.jpg
    echo "[+] Demo files: Demo/Welcome.txt, Demo/Xeneva.jpg, Demo/Snow.jpg"
fi

# The xHCI root port numbering is reversed between QEMU and the guest: QEMU's
# bus path 4.3 is the guest's root port 8, hub port 3. The boot topology
# leaves that hub port free; the nested hub uses 4.8, so the two do not
# collide.
export XENEVA_QEMU_EXTRA_ARGS="${XENEVA_QEMU_EXTRA_ARGS:-} -drive if=none,id=demo-drive0,file=$DEMO_IMG,format=raw -device usb-storage,drive=demo-drive0,id=demo-usb0,port=4.3"

cat <<'EOF'

XenevaOS live demo
==================
QEMU opens the real desktop, not a recording. Suggested recording order:

  0. Boot
     At the loader menu choose 1024x768 (Down, Down, Enter). Start the
     recording once the desktop is up; the menu does not need to be in it.
  1. Desktop and launcher
     Move the USB mouse, click the launcher, open the Terminal.
  2. Terminal
     Type: systeminfo
     Type: cd /
     Type: ls
     Type: cd b
     Type: ls
     Type: cd Demo
     Type: ls
     Type: cd /
     Type: umount b
     Type: cd b
     Type: mount b
     Type: cd b
     Type: ls
     `b` is the mounted USB volume. Its Demo folder lists WELCOME.TXT,
     XENEVA.JPG and SNOW.JPG. After `umount b`, `cd b` fails; `mount b`
     attaches the same stick again.
  3. Files
     Open Files from the launcher and choose USB QEMU HARDDISK
     (Location: b). Double-click DEMO to list the three files.
  4. Optional hotplug
     Remove and re-add the stick with the helper:
       Scripts/Linux/demo_usb_hotplug.sh remove demo-usb0
       Scripts/Linux/demo_usb_hotplug.sh add
     It recreates the drive backend before adding the device, because
     QEMU deletes the original -drive along with the device. The guest
     detaches /b on unplug and remounts it on replug.
  5. Applications
     Open Calculator, XENotes, or XEPaints from the launcher.
  6. Optional browser
     Open NetSurf if the image includes it and the QEMU network is up.

The serial console (the terminal that launched this script) carries the
xHCI evidence: "hub ready on port 8.8", "report iface ... ids 1", and
"HID pointer ready on port 8.8.1". Keep that window visible if you want
to show the device path, or record only the QEMU window for a cleaner
desktop demo.

EOF

args=(--llvm)
if [ "$SKIP_BUILD" -eq 1 ]; then
    args+=(--skip-build)
elif [ ! -f "$REPO_ROOT/Process/Init/init.exe" ]; then
    echo "demo_xeneva: no userspace binaries in Resources/resources." >&2
    echo "demo_xeneva: run the dcl gate once to build them, then retry." >&2
    exit 1
fi
args+=("${PASSTHROUGH[@]}")

echo "[+] At the loader menu choose 1024x768: Down, Down, Enter."
echo "    1024x768 is the smallest mode that fits the Terminal window."
exec "$REPO_ROOT/Scripts/Linux/build_and_run_qemu.sh" "${args[@]}"
