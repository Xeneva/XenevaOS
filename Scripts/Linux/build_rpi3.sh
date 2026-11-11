#!/bin/bash
set -e

# Build + image script for XenevaOS on Raspberry Pi 3B / 3B+ (AArch64).
#
# Boot chain on the Pi:
#   bootcode.bin -> start.elf (VideoCore firmware) reads config.txt
#     -> loads kernel8.img (LittleBoot) at 0x80000, xeneva.img as initramfs,
#        and the board DTB, then enters LittleBoot at EL2 with x0 = DTB
#     -> LittleBoot finds EFI/XENEVA/xnkrnl.exe inside the initramfs (FAT32),
#        maps it and jumps to KernelAA64 (built with BOARD=rpi3)
#     -> KernelAA64 mounts the same initramfs as its root file system.
#
# So xeneva.img is the QEMU initrd2.img contents plus EFI/XENEVA/xnkrnl.exe.
#
# Usage: Scripts/Linux/build_rpi3.sh [OPTIONS]
#
#   --llvm                  Build with LLVM/Clang (default).
#   --gcc                   Build with GCC (aarch64-linux-gnu-).
#   --skip-build            Don't build anything; reassemble the images from the
#                           last rpi3 build (Build/rpi3/xnkrnl.exe, kernel8.img).
#   --force-user-apps       Also rebuild userspace libs/apps and redeploy them
#                           into Resources/resources/ before packing.
#   --apps=LIST             Rebuild only the listed apps (comma separated).
#   --bleed                 Streamlined profile (same meaning as the QEMU script).
#   --soak / --debug-alloc  Kernel test/debug options (same as the QEMU script).
#   --no-network            Leave the network userspace out of the image.
#   --no-audio              Leave the audio userspace out of the image.
#   --no-doom               Leave doom.exe/doom2.wad out of the image.
#   --no-netsurf            Leave NetSurf out of the image.
#   --isolate-cpus=LIST     Reserve cores for bound threads (writes
#                           /cores.cnf, e.g. --isolate-cpus=3). Without it
#                           every core is general purpose, as in the QEMU
#                           script; Resources/resources/cores.cnf is ignored.
#   --initrd-size-mb=N      Size of xeneva.img (default: auto).
#   --sd-size-mb=N          Size of the SD card image (default: auto).
#   --hdmi-cvt=WxH          Drive HDMI with a custom mode (hdmi_group=2,
#                           hdmi_mode=87, hdmi_cvt W H 60 6), e.g. 800x480 for
#                           the Waveshare 5"/7" HDMI touch LCDs, which
#                           otherwise come up in a 640x480 fallback mode.
#   --initrd-addr=ADDR      Physical address the firmware loads xeneva.img at
#                           (default 0x08000000, same place QEMU puts -initrd).
#   --firmware-dir=DIR      Use Pi firmware files from DIR instead of fetching
#                           them (also $RPI_FIRMWARE_DIR). DIR must contain
#                           bootcode.bin, start.elf, fixup.dat and the DTBs.
#   --firmware-ref=REF      raspberrypi/firmware git tag/branch to fetch
#                           (default 1.20250430, also $RPI_FIRMWARE_REF).
#   --out=DIR               Output directory (default Build/rpi3).
#   --no-sd-image           Only produce kernel8.img, xeneva.img and boot/.
#   --no-usb                Leave the DWC2 USB host driver (dwc2.dll) out of
#                           the image; it is loaded through /board.cnf.
#                           --no-audio likewise leaves out the headphone jack
#                           driver (rpisnd.dll) along with the audio userspace.
#   --no-bt                 Leave the on-board Bluetooth driver (rpibt.dll) and
#                           its controller patch out of the image.
#   --bt-firmware=FILE      Use FILE as the CYW43455 Bluetooth patch
#                           (BCM4345C0.hcd) instead of fetching it (also
#                           $RPI_BT_FIRMWARE). Packed as /bt43455.hcd.
#   --bt-firmware-ref=REF   RPi-Distro/bluez-firmware commit to fetch the patch
#                           and its licence from (also $RPI_BT_FIRMWARE_REF).
#   --qemu                  After building, boot it on QEMU's raspi3b machine.
#   --headless              With --qemu: no display, stop after a timeout
#                           ($XENEVA_QEMU_TIMEOUT seconds, default 60).
#   -h, --help              Show this help and exit.
#
# Outputs (in Build/rpi3 by default):
#   kernel8.img         LittleBoot flat binary
#   xnkrnl.exe          KernelAA64 built with BOARD=rpi3
#   xeneva.img          FAT32 initramfs: EFI/XENEVA/xnkrnl.exe + system files
#   boot/               Contents of the SD card boot partition (copy these
#                       onto a FAT32-formatted card if you prefer)
#   xeneva-rpi3.img     Whole SD card image (MBR + FAT32 boot partition)
#
# Write the SD image with, e.g.:
#   sudo dd if=Build/rpi3/xeneva-rpi3.img of=/dev/sdX bs=4M conv=fsync status=progress

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPT_DIR="$REPO_ROOT/Scripts/Linux"
ORIG_PWD="$PWD"

source "$SCRIPT_DIR/lib/enviorment_variables.sh"
source "$SCRIPT_DIR/lib/functions.sh"

TOOLCHAIN=llvm
SKIP_BUILD=0
BUILD_USER_APPS=0
USER_APPS=""
BLEED=0
SOAK=0
DEBUG_ALLOC=0
NO_NETWORK=0
NO_AUDIO=0
NO_DOOM=0
NO_NETSURF=0
ISOLATE_CPUS=""
HDMI_CVT=""
INITRD_SIZE_MB=""
SD_SIZE_MB=""
INITRD_ADDR="${RPI_INITRD_ADDR:-0x08000000}"
FW_DIR="${RPI_FIRMWARE_DIR:-}"
FW_REF="${RPI_FIRMWARE_REF:-1.20250430}"
OUT_DIR="$REPO_ROOT/Build/rpi3"
NO_SD_IMAGE=0
NO_USB=0
NO_BT=0
BT_FW_FILE="${RPI_BT_FIRMWARE:-}"
# RPi-Distro/bluez-firmware (pios/trixie, 2025-10-02): BCM4345C0.hcd and
# LICENSE.cypress, the Cypress licence it is redistributed under.
BT_FW_REF="${RPI_BT_FIRMWARE_REF:-cdf61dc691a49ff01a124752bd04194907f0f9cd}"
RUN_QEMU=0
HEADLESS=0

# Files the Pi 3 boot partition needs from raspberrypi/firmware (boot/).
FW_FILES=(
    bootcode.bin
    start.elf
    fixup.dat
    bcm2710-rpi-3-b-plus.dtb
    bcm2710-rpi-3-b.dtb
    LICENCE.broadcom
)

abs_path() {
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s\n' "$ORIG_PWD/$1" ;;
    esac
}

die() {
    printf "${STY_RED}[%s]: %s${STY_RST}\n" "$(basename "$0")" "$*" >&2
    exit 1
}

info() {
    printf "${STY_CYAN}[rpi3]${STY_RST} %s\n" "$*"
}

print_help() {
    sed -n '4,/^REPO_ROOT=/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
    case "$1" in
        --llvm) TOOLCHAIN=llvm ;;
        --gcc) TOOLCHAIN=gcc ;;
        --skip-build) SKIP_BUILD=1 ;;
        --force-user-apps) BUILD_USER_APPS=1 ;;
        --apps=*|--user-apps=*) BUILD_USER_APPS=1; USER_APPS="${1#*=}" ;;
        --apps|--user-apps) BUILD_USER_APPS=1; shift; USER_APPS="$1" ;;
        --bleed) BLEED=1 ;;
        --soak) SOAK=1 ;;
        --debug-alloc) DEBUG_ALLOC=1 ;;
        --no-network) NO_NETWORK=1 ;;
        --no-audio) NO_AUDIO=1 ;;
        --no-doom) NO_DOOM=1 ;;
        --no-netsurf) NO_NETSURF=1 ;;
        --isolate-cpus=*) ISOLATE_CPUS="${1#*=}" ;;
        --hdmi-cvt=*) HDMI_CVT="${1#*=}" ;;
        --initrd-size-mb=*) INITRD_SIZE_MB="${1#*=}" ;;
        --sd-size-mb=*) SD_SIZE_MB="${1#*=}" ;;
        --initrd-addr=*) INITRD_ADDR="${1#*=}" ;;
        --firmware-dir=*) FW_DIR="$(abs_path "${1#*=}")" ;;
        --firmware-ref=*) FW_REF="${1#*=}" ;;
        --out=*) OUT_DIR="$(abs_path "${1#*=}")" ;;
        --no-sd-image) NO_SD_IMAGE=1 ;;
        --no-usb) NO_USB=1 ;;
        --no-bt) NO_BT=1 ;;
        --bt-firmware=*) BT_FW_FILE="$(abs_path "${1#*=}")" ;;
        --bt-firmware-ref=*) BT_FW_REF="${1#*=}" ;;
        --qemu) RUN_QEMU=1 ;;
        --headless) HEADLESS=1 ;;
        -h|--help) print_help; exit 0 ;;
        *) die "Unknown option: $1 (see --help)" ;;
    esac
    shift
done

prevent_privelage_run

for n in "$INITRD_SIZE_MB" "$SD_SIZE_MB"; do
    case "$n" in
        ''|*[!0-9]*) [ -z "$n" ] || die "--initrd-size-mb / --sd-size-mb must be whole numbers." ;;
    esac
done
if [ -n "$HDMI_CVT" ]; then
    case "$HDMI_CVT" in
        [0-9]*x[0-9]*) ;;
        *) die "--hdmi-cvt must look like 800x480." ;;
    esac
fi
case "$INITRD_ADDR" in
    0x[0-9a-fA-F]*) ;;
    *) die "--initrd-addr must be a hex address like 0x08000000." ;;
esac

# --- Host tools ---

need_tools=(mkfs.vfat mcopy mmd minfo truncate)
[ "$NO_SD_IMAGE" -eq 1 ] || need_tools+=(sfdisk dd)
[ "$RUN_QEMU" -eq 0 ] || need_tools+=(qemu-system-aarch64)
{ [ -n "$FW_DIR" ] && { [ "$NO_BT" -eq 1 ] || [ -n "$BT_FW_FILE" ]; }; } || need_tools+=(curl)
for tool in "${need_tools[@]}"; do
    command -v "$tool" >/dev/null 2>&1 || die "Required host tool '$tool' not found."
done

mkdir -p "$OUT_DIR"
cd "$REPO_ROOT"

# --- Build ---

if [ "$SKIP_BUILD" -eq 0 ]; then
    if [ "$BUILD_USER_APPS" -eq 0 ] && [ ! -f "$REPO_ROOT/Process/Init/init.exe" ]; then
        info "Process/Init/init.exe not found; building user space too (same as --force-user-apps)."
        BUILD_USER_APPS=1
    fi

    info "Building LittleBoot (kernel8.img) with $TOOLCHAIN..."
    ( cd "$REPO_ROOT/LittleBoot" && make clean && make "$TOOLCHAIN" )

    info "Building KernelAA64 (BOARD=rpi3)$([ "$BUILD_USER_APPS" -eq 1 ] && echo ' and user space') with $TOOLCHAIN..."
    # lib/{llvm,gcc}.sh are the same build steps build_and_run_qemu.sh uses.
    # BOARD=rpi3 selects __TARGET_BOARD_RPI3__ in the kernel; the EFI loader
    # is skipped because the Pi boots through LittleBoot. The virtio drivers
    # (and static components) are QEMU-only, so they are not built here.
    BOARD=rpi3
    SKIP_BOOTAA64=1
    STATIC_COMPONENTS=0
    export BOARD SKIP_BOOTAA64 STATIC_COMPONENTS BUILD_USER_APPS USER_APPS BLEED SOAK DEBUG_ALLOC \
           NO_NETWORK NO_AUDIO NO_DOOM NO_NETSURF
    pushd "$SCRIPT_DIR" >/dev/null
    if [ "$TOOLCHAIN" = llvm ]; then
        source ./lib/llvm.sh
    else
        source ./lib/gcc.sh
    fi
    popd >/dev/null

    # Stash the rpi3 kernel: KernelAA64/KernelAA64.exe is shared with the QEMU
    # flow, which rebuilds it for qemu_virt. --skip-build packs this copy.
    cp -f "$REPO_ROOT/LittleBoot/kernel8.img" "$OUT_DIR/kernel8.img"
    cp -f "$REPO_ROOT/KernelAA64/KernelAA64.exe" "$OUT_DIR/xnkrnl.exe"

    # board drivers loaded by the driver manager from /board.cnf; they link
    # against the import library of the kernel just built
    if [ "$NO_USB" -eq 0 ]; then
        info "Building the DWC2 USB host driver (dwc2.dll)..."
        ( cd "$REPO_ROOT/Drivers/USB/dwc2-otg" && make clean && make )
        cp -f "$REPO_ROOT/Drivers/USB/dwc2-otg/dwc2.dll" "$OUT_DIR/dwc2.dll"
    fi
    if [ "$NO_AUDIO" -eq 0 ]; then
        info "Building the headphone jack audio driver (rpisnd.dll)..."
        ( cd "$REPO_ROOT/Drivers/Sound/RPI" && make clean && make )
        cp -f "$REPO_ROOT/Drivers/Sound/RPI/rpisnd.dll" "$OUT_DIR/rpisnd.dll"
    fi
    if [ "$NO_BT" -eq 0 ]; then
        info "Building the on-board Bluetooth driver (rpibt.dll)..."
        ( cd "$REPO_ROOT/Drivers/Bt/RPI" && make clean && make )
        cp -f "$REPO_ROOT/Drivers/Bt/RPI/rpibt.dll" "$OUT_DIR/rpibt.dll"
    fi
    printf "${STY_YELLOW}[rpi3] Note: KernelAA64/KernelAA64.exe is now the rpi3 build; run build_and_run_qemu.sh without --skip-build before booting QEMU virt.${STY_RST}\n"
else
    info "--skip-build: reusing the last rpi3 build in $OUT_DIR."
    [ -f "$OUT_DIR/xnkrnl.exe" ] || die "$OUT_DIR/xnkrnl.exe missing; run once without --skip-build."
    if [ "$NO_USB" -eq 0 ] && [ ! -f "$OUT_DIR/dwc2.dll" ]; then
        die "$OUT_DIR/dwc2.dll missing; run once without --skip-build (or pass --no-usb)."
    fi
    if [ "$NO_AUDIO" -eq 0 ] && [ ! -f "$OUT_DIR/rpisnd.dll" ]; then
        die "$OUT_DIR/rpisnd.dll missing; run once without --skip-build (or pass --no-audio)."
    fi
    if [ "$NO_BT" -eq 0 ] && [ ! -f "$OUT_DIR/rpibt.dll" ]; then
        die "$OUT_DIR/rpibt.dll missing; run once without --skip-build (or pass --no-bt)."
    fi
    # LittleBoot only ever builds for the Pi, so its tree copy is current
    if [ -f "$REPO_ROOT/LittleBoot/kernel8.img" ]; then
        cp -f "$REPO_ROOT/LittleBoot/kernel8.img" "$OUT_DIR/kernel8.img"
    fi
    [ -f "$OUT_DIR/kernel8.img" ] || die "kernel8.img missing; run once without --skip-build."
    [ -f "$REPO_ROOT/Process/Init/init.exe" ] || die "Process/Init/init.exe missing; run without --skip-build."
fi

KERNEL8="$OUT_DIR/kernel8.img"
XNKRNL="$OUT_DIR/xnkrnl.exe"
INITRD="$OUT_DIR/xeneva.img"

# --- Bluetooth controller patch ---
# The CYW43455 runs a RAM patch that the driver uploads over the UART on
# every boot (controller RAM does not survive power off). It is Cypress
# firmware, redistributable under LICENSE.cypress, which goes onto the boot
# partition next to the Broadcom licence of the Pi firmware.

BT_FW_HCD=""
BT_FW_LICENSE=""
if [ "$NO_BT" -eq 0 ]; then
    if [ -n "$BT_FW_FILE" ]; then
        [ -f "$BT_FW_FILE" ] || die "Bluetooth patch $BT_FW_FILE does not exist."
        BT_FW_HCD="$BT_FW_FILE"
        lic="$(dirname "$BT_FW_FILE")/LICENSE.cypress"
        [ -f "$lic" ] && BT_FW_LICENSE="$lic"
    else
        bt_fw_dir="$REPO_ROOT/Build/bt-firmware/$BT_FW_REF"
        mkdir -p "$bt_fw_dir"
        for f in BCM4345C0.hcd LICENSE.cypress; do
            [ -f "$bt_fw_dir/$f" ] && continue
            info "Fetching Bluetooth firmware $f ($BT_FW_REF)..."
            curl -fsSL --retry 3 -o "$bt_fw_dir/$f.part" \
                "https://github.com/RPi-Distro/bluez-firmware/raw/$BT_FW_REF/debian/firmware/broadcom/$f" \
                || die "Download of $f failed. Pass --bt-firmware=FILE (or --no-bt) instead."
            mv -f "$bt_fw_dir/$f.part" "$bt_fw_dir/$f"
        done
        BT_FW_HCD="$bt_fw_dir/BCM4345C0.hcd"
        BT_FW_LICENSE="$bt_fw_dir/LICENSE.cypress"
    fi
    # an .hcd is a list of HCI commands: the first one is a write RAM (4C FC)
    if [ "$(head -c 2 "$BT_FW_HCD" | od -An -tx1 | tr -d ' \n')" != "4cfc" ]; then
        die "$BT_FW_HCD does not look like a Broadcom .hcd patch."
    fi
fi

# --- Assemble xeneva.img (initramfs: kernel + system files) ---

if [ -n "$INITRD_SIZE_MB" ]; then
    initrd_size_mb="$INITRD_SIZE_MB"
elif [ "$BLEED" -eq 1 ]; then
    initrd_size_mb=48
else
    resources_mb=$(du -sm Resources/resources | cut -f1)
    computed_mb=$(( (resources_mb * 3 + 1) / 2 ))   # ceil(resources * 1.5)
    initrd_size_mb=$(( computed_mb > 96 ? computed_mb : 96 ))
fi
kernel_mb=$(( ($(stat -c %s "$XNKRNL") + 1048575) / 1048576 ))
initrd_size_mb=$(( initrd_size_mb + kernel_mb ))

info "Creating ${initrd_size_mb}MB FAT32 xeneva.img..."
rm -f "$INITRD"
truncate -s "${initrd_size_mb}M" "$INITRD"
# 4 KiB clusters (-s 8 x 512 B) are required: LittleBoot (ramdisk.c) loads
# xnkrnl.exe one cluster per 4 KiB page and parses directories assuming that
# geometry. The image is smaller than FAT32's "suggested" 65525 clusters;
# mkfs.vfat warns but the volume is valid FAT32 (BPB fat32 fields set), which
# is what both LittleBoot and the kernel's FAT driver read.
mkfs_out="$(mkfs.vfat -F 32 -S 512 -s 8 -n XENEVA "$INITRD" 2>&1)" \
    || die "mkfs.vfat failed on xeneva.img: $mkfs_out"

# EFI/XENEVA first, so its directory entry sits in the first root cluster
# (LittleBoot only scans the first cluster of each directory).
mmd -i "$INITRD" ::/EFI ::/EFI/XENEVA
mcopy -o -i "$INITRD" "$XNKRNL" ::/EFI/XENEVA/xnkrnl.exe

pack_excluded() {
    case "$1" in
        # QEMU virtio drivers: no virtio/PCI on the Pi
        virtnet.dll|virtgpu.dll|virtsnd.dll|virtblk.dll)
            return 0 ;;
        MUSIC|ARCH_X64|snd.wav|RoLight.ttf|RoLiIta.ttf|RoThin.ttf|corbel.ttf)
            [ "$BLEED" -eq 1 ] && return 0 || return 1 ;;
        netmngr.exe|route.exe|iptable.exe|ping.exe|udpecho.exe|dig.exe|nslook.exe|telnet.exe|finger.exe|gopher.exe|ntpd.exe|curl.exe|wifictl.exe)
            [ "$NO_NETWORK" -eq 1 ] && return 0 || return 1 ;;
        doom.exe|doom2.wad)
            [ "$NO_DOOM" -eq 1 ] && return 0 || return 1 ;;
        netsurf.exe)
            [ "$NO_NETSURF" -eq 1 ] && return 0 || return 1 ;;
        hello.elf|musl.elf)
            return 0 ;;
        # core reservation is decided by --isolate-cpus, not a stale file
        cores.cnf)
            return 0 ;;
        deoaud.exe|audplr.exe)
            [ "$NO_AUDIO" -eq 1 ] && return 0 || return 1 ;;
    esac
    return 1
}

info "Packing Resources/resources into xeneva.img..."
for resource in Resources/resources/*; do
    pack_excluded "$(basename "$resource")" && continue
    mcopy -o -s -i "$INITRD" "$resource" ::/
done

# Launcher menu: only list apps whose binaries made it into the image
# (same filter as build_and_run_qemu.sh).
if [ -f Resources/resources/lncher.cnf ]; then
    tmp_cnf="$(mktemp)"
    entry=""
    while IFS= read -r line || [ -n "$line" ]; do
        entry="${entry}${line}"$'\n'
        if [[ "$line" =~ (\}|>)[[:space:]]*$ ]]; then
            app="$(printf '%s' "$entry" | sed -n 's/.*\[App\]\/\([^|>\r\n}]*\).*/\1/p')"
            if [ -z "$app" ] || { [ -f "Resources/resources/$app" ] && ! pack_excluded "$app"; }; then
                printf '%s\n' "$entry" >> "$tmp_cnf"
            fi
            entry=""
        fi
    done < Resources/resources/lncher.cnf
    if [ -s "$tmp_cnf" ]; then
        sed -i -e '/^[[:space:]]*$/d' "$tmp_cnf"
        sed -i -e '$ { s/}[[:space:]]*$/>/ }' "$tmp_cnf"
        mcopy -o -i "$INITRD" "$tmp_cnf" ::/lncher.cnf
    fi
    rm -f "$tmp_cnf"
fi

mcopy -o -i "$INITRD" Process/Init/init.exe ::/init.exe

# board drivers: the driver manager reads /board.cnf on non-PCIe boards,
# "(count)" then one "[name]/file|" line per driver
board_entries=()
if [ "$NO_USB" -eq 0 ]; then
    mcopy -o -i "$INITRD" "$OUT_DIR/dwc2.dll" ::/dwc2.dll
    board_entries+=("[DWC2-USB]/dwc2.dll|")
fi
if [ "$NO_AUDIO" -eq 0 ]; then
    mcopy -o -i "$INITRD" "$OUT_DIR/rpisnd.dll" ::/rpisnd.dll
    board_entries+=("[RPI-AUDIO]/rpisnd.dll|")
fi
if [ "$NO_BT" -eq 0 ]; then
    mcopy -o -i "$INITRD" "$OUT_DIR/rpibt.dll" ::/rpibt.dll
    mcopy -o -i "$INITRD" "$BT_FW_HCD" ::/bt43455.hcd
    board_entries+=("[RPI-BT]/rpibt.dll|")
fi
if [ "${#board_entries[@]}" -gt 0 ]; then
    board_tmp="$(mktemp)"
    printf '(%d)\r\n' "${#board_entries[@]}" > "$board_tmp"
    printf '%s\r\n' "${board_entries[@]}" >> "$board_tmp"
    mcopy -o -i "$INITRD" "$board_tmp" ::/board.cnf
    rm -f "$board_tmp"
    info "Board drivers packed via /board.cnf: ${board_entries[*]}"
fi

if [ -n "$ISOLATE_CPUS" ]; then
    info "Reserving cores $ISOLATE_CPUS for bound threads (/cores.cnf)"
    cores_tmp="$(mktemp)"
    printf 'isolate=%s\n' "$ISOLATE_CPUS" > "$cores_tmp"
    mcopy -o -i "$INITRD" "$cores_tmp" ::/cores.cnf
    rm -f "$cores_tmp"
fi

# --- Pi firmware ---

if [ -n "$FW_DIR" ]; then
    [ -d "$FW_DIR" ] || die "Firmware directory $FW_DIR does not exist."
else
    FW_DIR="$REPO_ROOT/Build/rpi-firmware/$FW_REF"
    for f in "${FW_FILES[@]}"; do
        [ -f "$FW_DIR/$f" ] && continue
        info "Fetching firmware $f ($FW_REF)..."
        mkdir -p "$(dirname "$FW_DIR/$f")"
        curl -fsSL --retry 3 -o "$FW_DIR/$f.part" \
            "https://github.com/raspberrypi/firmware/raw/$FW_REF/boot/$f" \
            || die "Download of $f failed. Pass --firmware-dir=DIR with the Pi firmware files instead."
        mv -f "$FW_DIR/$f.part" "$FW_DIR/$f"
    done
fi
for f in bootcode.bin start.elf fixup.dat bcm2710-rpi-3-b-plus.dtb; do
    [ -f "$FW_DIR/$f" ] || die "Firmware file $f missing in $FW_DIR."
done

# --- Boot partition tree ---

BOOT_DIR="$OUT_DIR/boot"
info "Assembling boot partition contents in $BOOT_DIR..."
rm -rf "$BOOT_DIR"
mkdir -p "$BOOT_DIR/overlays"
for f in "${FW_FILES[@]}"; do
    [ -f "$FW_DIR/$f" ] && cp -f "$FW_DIR/$f" "$BOOT_DIR/$f"
done
rmdir "$BOOT_DIR/overlays" 2>/dev/null || true
cp -f "$KERNEL8" "$BOOT_DIR/kernel8.img"
[ -z "$BT_FW_LICENSE" ] || cp -f "$BT_FW_LICENSE" "$BOOT_DIR/LICENCE.cypress"
cp -f "$INITRD" "$BOOT_DIR/xeneva.img"

cat > "$BOOT_DIR/config.txt" <<EOF
# XenevaOS -- Raspberry Pi 3B/3B+ boot configuration
# Generated by Scripts/Linux/build_rpi3.sh; edits are lost on rebuild.

[all]
# 64-bit boot: start.elf loads kernel8.img (LittleBoot) at 0x80000 and
# enters it at EL2 with x0 = device tree.
arm_64bit=1
kernel=kernel8.img

# FAT32 initramfs holding EFI/XENEVA/xnkrnl.exe and the system files.
# Fixed address instead of "followkernel": LittleBoot's .bss reaches
# ~8.5 MiB past the end of kernel8.img and would overwrite it.
initramfs xeneva.img ${INITRD_ADDR}

# Serial console: LittleBoot and the kernel drive the mini UART on
# GPIO14/15 (pins 8/10), 115200 8N1. The PL011 stays on GPIO30-33 for the
# on-board Bluetooth controller (rpibt.dll). The mini UART's baud rate
# follows the VPU core clock, so keep that clock fixed (enable_uart=1 also
# does this when the mini UART is the primary UART).
enable_uart=1
core_freq=250

# HDMI console: LittleBoot asks the VideoCore for a 1024x768x32
# framebuffer through the mailbox; drive HDMI even if hotplug is missed.
hdmi_force_hotplug=1
disable_overscan=1
gpu_mem=64
EOF

if [ -n "$HDMI_CVT" ]; then
    cat >> "$BOOT_DIR/config.txt" <<EOF

# custom HDMI mode for the attached panel (--hdmi-cvt=$HDMI_CVT)
hdmi_group=2
hdmi_mode=87
hdmi_cvt ${HDMI_CVT%x*} ${HDMI_CVT#*x} 60 6 0 0 0
hdmi_drive=1
EOF
fi

# --- SD card image ---

SD_IMG="$OUT_DIR/xeneva-rpi3.img"
if [ "$NO_SD_IMAGE" -eq 0 ]; then
    boot_used_mb=$(( $(du -sm "$BOOT_DIR" | cut -f1) ))
    if [ -n "$SD_SIZE_MB" ]; then
        sd_mb="$SD_SIZE_MB"
    else
        sd_mb=$(( boot_used_mb + 32 ))
        sd_mb=$(( sd_mb > 64 ? sd_mb : 64 ))
    fi
    # Same layout Raspberry Pi Imager writes: MBR, one bootable FAT32 (LBA,
    # type 0x0c) partition starting at 4 MiB (sector 8192), filesystem
    # formatted in place so its hidden-sectors field equals the partition
    # start, as the Pi boot ROM / firmware see on every stock card.
    part_start=8192
    part_sectors=$(( sd_mb * 2048 - part_start ))
    [ $(( part_sectors / 2048 )) -gt "$boot_used_mb" ] || die "--sd-size-mb=$sd_mb is too small (boot files need ${boot_used_mb}MB)."
    # largest cluster size that keeps the volume above FAT32's minimum
    # cluster count (65525); the ROM treats fewer clusters as FAT16
    spc=1
    for c in 8 4 2; do
        if [ $(( part_sectors / c )) -ge 70000 ]; then spc=$c; break; fi
    done

    info "Creating ${sd_mb}MB SD card image $SD_IMG ($(( spc * 512 )) B clusters)..."
    rm -f "$SD_IMG"
    truncate -s "${sd_mb}M" "$SD_IMG"
    printf 'label: dos\nstart=%s, type=c, bootable\n' "$part_start" | sfdisk --quiet "$SD_IMG"
    # mkfs.vfat takes the filesystem size in 1 KiB blocks
    mkfs.vfat -F 32 -S 512 -s "$spc" -n XENEVABOOT -h "$part_start" --offset "$part_start" "$SD_IMG" $(( part_sectors / 2 )) >/dev/null \
        || die "mkfs.vfat failed on the SD image."
    sd_part="$SD_IMG@@$(( part_start * 512 ))"
    for entry in "$BOOT_DIR"/*; do
        mcopy -o -s -i "$sd_part" "$entry" ::/
    done

    # the Pi boot ROM needs exactly these at the partition root
    for f in bootcode.bin start.elf fixup.dat config.txt kernel8.img xeneva.img bcm2710-rpi-3-b-plus.dtb; do
        mdir -i "$sd_part" "::/$f" >/dev/null 2>&1 || die "$f is missing from the SD image boot partition."
    done
    info "Verified firmware files on the SD image boot partition."
fi

# --- Summary ---

printf "${STY_GREEN}[rpi3] Done.${STY_RST}\n"
echo "    kernel8.img : $KERNEL8"
echo "    xeneva.img  : $INITRD (${initrd_size_mb}MB, loaded at $INITRD_ADDR)"
echo "    boot files  : $BOOT_DIR/"
if [ "$NO_SD_IMAGE" -eq 0 ]; then
    echo "    SD image    : $SD_IMG"
    echo "    Flash with  : sudo dd if=\"$SD_IMG\" of=/dev/sdX bs=4M conv=fsync status=progress"
fi
echo "    Serial      : mini UART on GPIO14 (TX, pin 8) / GPIO15 (RX, pin 10), 115200 8N1"
[ "$NO_BT" -eq 1 ] || echo "    Bluetooth   : rpibt.dll + /bt43455.hcd, advertised as \"XenevaOS\""

# --- Optional QEMU raspi3b run ---

if [ "$RUN_QEMU" -eq 1 ]; then
    QEMU_ARGS=(
        -M raspi3b
        -kernel "$KERNEL8"
        -dtb "$FW_DIR/bcm2710-rpi-3-b-plus.dtb"
        -initrd "$INITRD"
        # serial0 is the PL011 (Bluetooth), serial1 the mini UART console
        -serial null
        -serial stdio
    )
    info "Booting on QEMU raspi3b..."
    if [ "$HEADLESS" -eq 1 ]; then
        timeout --foreground "${XENEVA_QEMU_TIMEOUT:-60}" \
            qemu-system-aarch64 "${QEMU_ARGS[@]}" -display none || true
    else
        qemu-system-aarch64 "${QEMU_ARGS[@]}"
    fi
fi
