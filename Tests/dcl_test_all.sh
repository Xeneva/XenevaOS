#!/usr/bin/env bash
# Tests/dcl_test_all.sh -- the gate for the DCL tty/serial port.
#
# "Green here" is what a finished stage means: every stage ends by running
# this. Four parts, in order, each reporting `ok` or stopping with a reason:
#
#   1. build        -- full `make llvm`, grepped for `error:`. Worth being
#                      explicit: `make llvm | tail` reports *tail's* exit
#                      status, so pipefail alone would pass a failed build
#   2. boot         -- a complete QEMU run. Exit 124 (timeout) is the
#                      expected status; the verdict is the log
#   3. tree counts  -- a file appearing or disappearing should be a decision
#                      with a number attached, not something noticed three
#                      stages later
#   4. assertions   -- on that log
#
# Usage:
#   ./Tests/dcl_test_all.sh              full run
#   CLEAN=1 ./Tests/dcl_test_all.sh      `make clean` first -- required after any edit
#                                        under BaseHdr/, because KernelAA64/Makefile has
#                                        no header dependency tracking and stale objects
#                                        will silently keep old header contents
#
set -uo pipefail

cd "$(dirname "$0")/.." || exit 1

# Upstream's Fs/Fat/FatAttr.c includes <FS/Fat/Fat.h>. That spelling only
# resolves on a case-insensitive filesystem, while Linux is case-sensitive
# and the header lives under BaseHdr/Fs. The gate builds upstream as-is, so
# provide the missing spelling locally and remove it on exit.
CASE_SHIM=KernelAA64/FS
CASE_SHIM_CREATED=0
cleanup_case_shim() {
	if [ "$CASE_SHIM_CREATED" -eq 1 ]; then
		rm -f "$CASE_SHIM"
	fi
}
trap cleanup_case_shim EXIT INT TERM
if [ ! -e "$CASE_SHIM" ]; then
	ln -s ../BaseHdr/Fs "$CASE_SHIM"
	CASE_SHIM_CREATED=1
fi

# The gate tracks the vendored path now.  An explicit 0 still selects the
# native virtiokbd/virtiotablet pair for a manual comparison, but the input
# assertions below only run when this is 1.
BYPASS_VIRTIO_INPUT="${BYPASS_VIRTIO_INPUT:-1}"
export BYPASS_VIRTIO_INPUT
# The build script no longer carries the --bypass-virtgpu/--bypass-virtio-input
# options, but the kernel Makefile still selects both paths from these
# variables. Keep them exported for the make invocation the script runs.
BYPASS_VIRTGPU="${BYPASS_VIRTGPU:-0}"
export BYPASS_VIRTGPU

# ─── expected tree shape ────────────────────────────────────────────────────
# Bump deliberately, in the same change that alters the count.
EXP_LINUX_H=244    # BaseHdr/linux/*.h
EXP_ASM_H=16       # BaseHdr/asm/*.h
EXP_DCL_C=31       # DCL/*.c   (dcl_va_trampoline.s adds a 32nd object)
EXP_DCL_O=32       # KernelAA64/obj/DCL/*.o
EXP_VEND_O=112     # KernelAA64/obj/Vendored/**/*.o  (rng, tty/serial,
                   #   virtio_console, input core + virtio_input, and the
                   #   drm set: core, vkms, tiny/bochs, virtio-gpu,
                   #   dma-buf, lib)
EXP_ALL_O=282      # everything under KernelAA64/obj

# ─── expected boot-log results ──────────────────────────────────────────────
EXP_MEM=9
EXP_PRIM=10
EXP_TTYBUF=25
EXP_SERIAL=21
# 44 stage-1..4 + 38 KMS objects + 22 dumb/atomic + 3 client caps + 1 wb
# encoder lookup + 20 writeback pixel proof + 13 page flip event. Stage 5
# (45 checks) and stage 6 (34) are added only when their driver is present;
# the guest finds both by the name DRM VERSION reports.
EXP_DCLTEST=141
# Upstream's QEMU command no longer adds bochs-display. A caller can still
# add one through XENEVA_QEMU_EXTRA_ARGS; when it does, stage 5 runs and the
# gate holds its assertions to that. Otherwise the guest must report the skip.
EXPECT_BOCHS=${EXPECT_BOCHS:-0}
case " ${XENEVA_QEMU_EXTRA_ARGS:-} " in
	*bochs-display*) EXPECT_BOCHS=1 ;;
esac
if [ "$EXPECT_BOCHS" -eq 1 ]; then
	EXP_DCLTEST=$((EXP_DCLTEST + 45))
fi
if [ "${BYPASS_VIRTGPU:-0}" -eq 1 ]; then
	EXP_DCLTEST=$((EXP_DCLTEST + 34))
fi

LOG=${LOG:-/tmp/xeneva/test_all.log}
mkdir -p "$(dirname "$LOG")"

fails=0
ok()  { printf '  %-42s ok\n' "$1"; }
bad() { printf '  %-42s FAILED (%s)\n' "$1" "${2:-}"; fails=$((fails + 1)); }

check_count() {  # label actual expected
	if [ "$2" -eq "$3" ]; then ok "$1 = $2"; else bad "$1" "got $2, want $3"; fi
}

# ─── 1 + 2. build and boot ──────────────────────────────────────────────────
echo
echo "build + boot (log: $LOG)"
if [ -n "${CLEAN:-}" ]; then
	echo "  CLEAN=1 -> make clean"
	make -C KernelAA64 clean >/dev/null 2>&1
	for app in Init Clipd dcltest XEShell Terminal bttest; do
		( cd "Process/$app" && make clean >/dev/null 2>&1 )
	done
fi

# Everything this run prints goes in one file, from a fresh start: QEMU
# appends below rather than truncating, so the test programs' own build
# logs stay visible to the assertions.
: >"$LOG"

# The assertions read these programs' output, so this run has to build them.
# The general build only ships user apps under --force-user-apps, so without
# this the summary would be read off whatever stale dcltest.exe happens to
# be in Resources/ -- which is how a changed test kept reporting its old
# count. Same for every program this feature touches, and for the reason
# that is easier to miss: init spawns clipd, XEShell carries Ctrl+C/Ctrl+V,
# and the GUI Terminal carries Ctrl+Shift+C/V on the same clipboard node.
# A source change that never reaches the image reads as a bug somewhere
# else entirely.
echo "  building Process/{Init,Clipd,dcltest,XEShell,Terminal,bttest}"
for app in Init Clipd dcltest XEShell Terminal bttest; do
	( cd "Process/$app" && make llvm ) >>"$LOG" 2>&1
done
cp -f Process/Init/init.exe Resources/resources/ >>"$LOG" 2>&1
cp -f Process/Clipd/clipd.exe Resources/resources/ >>"$LOG" 2>&1
cp -f Process/dcltest/dcltest.exe Resources/resources/ >>"$LOG" 2>&1
cp -f Process/XEShell/xesh.exe Resources/resources/ >>"$LOG" 2>&1
cp -f Process/Terminal/term.exe Resources/resources/ >>"$LOG" 2>&1
cp -f Process/bttest/bttest.exe Resources/resources/ >>"$LOG" 2>&1

# Host-side BT unit tests (crypto, H4, event parsing, LC3). They are
# independent of the guest, so a break here is reported before QEMU starts
# and the guest bttest cannot mask it. Build outside the tree: Tests/bt/
# bt_test is tracked, and running a stale copy after a failed build would
# report the old result.
echo "  host BT unit tests"
BT_BIN="$(dirname "$LOG")/bt_test"
rm -f "$BT_BIN"
make -C Tests/bt OUT="$BT_BIN" >>"$LOG" 2>&1
if [ -x "$BT_BIN" ]; then
	"$BT_BIN" >>"$LOG" 2>&1
fi

# stdin from /dev/null rather than inherited. `-serial stdio` makes the
# guest's console this process's stdin, and a TTY there gives a guest that
# prints nothing for the whole timeout: QEMU exits 124 as expected and the
# log ends at the banner, so every assertion below fails on a boot that
# never happened. Verified -- the same run green on a non-TTY stdin, 17
# FAILED on a pty.
#
# Stage 5's pixel proof needs one thing done *during* the run: when the
# guest's dcltest says the bochs pattern commit has returned, take a
# screendump of bochs0 through QEMU's monitor.  The guest cannot see its
# own scanout -- the pixels belong to the device -- so the only place to
# look at them is here, from the host, while QEMU is alive.  The helper
# polls the log for that line and does the one monitor command; it is
# killed below if QEMU ends before the line ever appears.  There is no
# bochs display in upstream's QEMU defaults, so this is armed only when
# the caller supplied one.
SHOT=${SHOT:-/tmp/xeneva/bochs.ppm}
SHOOT_PID=
SHOOT_RC=2
if [ "$EXPECT_BOCHS" -eq 1 ]; then
	rm -f "$SHOT"
	python3 ./Tests/dcl_screendump.py wait \
		--log "$LOG" \
		--marker "[dcltest] info bochs scanout committed" \
		--out "$SHOT" --dev bochs0 --timeout 90 >/dev/null 2>&1 &
	SHOOT_PID=$!
fi

# Stage 6's pixel proof is the same host-side idea on the virtio-gpu
# display: dcltest flips the virtio-gpu card's primary plane to a known
# colour, prints the marker once the atomic commit has returned, and waits
# long enough for this watcher to attach to QEMU's monitor.  It is armed
# only in a bypass image because the vendored driver does not own the
# device (and does not publish a card) otherwise.
GPU_SHOT=${GPU_SHOT:-/tmp/xeneva/virtgpu.ppm}
GPU_SHOOT_PID=
GPU_SHOOT_RC=2
if [ "${BYPASS_VIRTGPU:-0}" -eq 1 ]; then
	rm -f "$GPU_SHOT"
	python3 ./Tests/dcl_screendump.py wait \
		--log "$LOG" \
		--marker "[dcltest] info virtio-gpu scanout committed" \
		--out "$GPU_SHOT" --dev gpu0 --timeout 90 >/dev/null 2>&1 &
	GPU_SHOOT_PID=$!
fi

# Input injection needs a QMP socket for absolute tablet coordinates; the
# HMP monitor can only send keys and relative motion.  The helper waits for
# the guest's attach marker before touching either socket, so it can start
# while the tree is still building.
INPUT_INJECT_PID=
INPUT_INJECT_RC=2
INPUT_INJECT_LOG="$(dirname "$LOG")/input_inject.log"
# Do not force bochs-display here: it is absent from upstream's defaults and
# adding one currently trips the DCL PCI path before the driver binds. A
# caller that supplies one through XENEVA_QEMU_EXTRA_ARGS opts into stage 5.
XENEVA_QEMU_EXTRA_ARGS="${XENEVA_QEMU_EXTRA_ARGS:-}"
# Exercise a SuperSpeed device on a USB3 root port.  A 3 TiB disk makes the
# SuperSpeed enumeration reach both the stream transport and the 64-bit CDB
# path, not just EP0.
UAS_IMG="$(dirname "$LOG")/xhci-uas.img"
rm -f "$UAS_IMG"
truncate -s 3T "$UAS_IMG"
printf 'XENEVA UAS TEST\n' | dd of="$UAS_IMG" conv=notrunc status=none
# A second LUN on the same target proves REPORT LUNS discovery and per-LUN
# command routing.  It stays small so LUN 1 does not repeat the 64-bit CDB
# path that the three terabyte LUN 0 already covers.
UAS_LUN1_IMG="$(dirname "$LOG")/xhci-uas-lun1.img"
rm -f "$UAS_LUN1_IMG"
truncate -s 8M "$UAS_LUN1_IMG"
printf 'UAS LUN ONE\n' | dd of="$UAS_LUN1_IMG" conv=notrunc status=none
XENEVA_QEMU_EXTRA_ARGS="$XENEVA_QEMU_EXTRA_ARGS -device usb-uas,id=uas0,port=3 -drive if=none,id=uasdisk0,file=$UAS_IMG,format=raw -device scsi-hd,drive=uasdisk0,bus=uas0.0,lun=0 -drive if=none,id=uasdisk1,file=$UAS_LUN1_IMG,format=raw -device scsi-hd,drive=uasdisk1,bus=uas0.0,lun=1"
# A 3 TiB sparse disk for the mass-storage class driver.  Its size is past
# the READ CAPACITY (10) limit, so the probe only reports the real geometry
# when READ CAPACITY (16) works; LBA 0 still carries a recognisable pattern
# for the READ (10) data phase.
MSC_IMG="$(dirname "$LOG")/xhci-msc.img"
rm -f "$MSC_IMG"
truncate -s 3T "$MSC_IMG"
printf 'XENEVA MSC TEST\n' | dd of="$MSC_IMG" conv=notrunc status=none
XENEVA_QEMU_EXTRA_ARGS="$XENEVA_QEMU_EXTRA_ARGS -drive if=none,id=msc0,file=$MSC_IMG,format=raw -device usb-storage,drive=msc0,id=mscdev0,port=4.3"
# A BOT disk on a root port runs at SuperSpeed, so its probe covers the
# non-stream bulk transport at 512-byte packets while the UAS disk on the
# other root port covers the stream transport.
MSC_SS_IMG="$(dirname "$LOG")/xhci-msc-ss.img"
rm -f "$MSC_SS_IMG"
truncate -s 8M "$MSC_SS_IMG"
printf 'SS MSC TEST\n' | dd of="$MSC_SS_IMG" conv=notrunc status=none
XENEVA_QEMU_EXTRA_ARGS="$XENEVA_QEMU_EXTRA_ARGS -drive if=none,id=mscss0,file=$MSC_SS_IMG,format=raw -device usb-storage,drive=mscss0,id=mscssdev0,port=2"
# A second, read-only disk makes the probe WRITE fail with CHECK CONDITION.
# That proves the driver fetches sense data instead of leaving the BOT pipe
# wedged, without needing to inject a transport fault.
MSC_RO_IMG="$(dirname "$LOG")/xhci-msc-ro.img"
rm -f "$MSC_RO_IMG"
truncate -s 8M "$MSC_RO_IMG"
printf 'XENEVA MSC RO\n' | dd of="$MSC_RO_IMG" conv=notrunc status=none
XENEVA_QEMU_EXTRA_ARGS="$XENEVA_QEMU_EXTRA_ARGS -drive if=none,id=mscro0,file=$MSC_RO_IMG,format=raw,readonly=on -device usb-storage,drive=mscro0,id=mscrodev0,port=4.4"
# Two GPT disks exercise the vdisk partition path.  The first has a raw
# partition in entry slot 1, so a walker that treats the entry index as a
# sector number reads the wrong sector and misses it.  The second carries a
# real FAT32 volume, which proves partition-relative probing and mounting.
MSC_GPT_IMG="$(dirname "$LOG")/xhci-msc-gpt.img"
MSC_FAT_IMG="$(dirname "$LOG")/xhci-msc-fat.img"
rm -f "$MSC_GPT_IMG" "$MSC_FAT_IMG"
python3 - "$MSC_GPT_IMG" "$MSC_FAT_IMG" <<'PY'
import struct, sys, uuid, zlib

SECTOR = 512


def write_gpt(path, sectors, part_start, part_end, name, slot, payload=None):
    image = bytearray(sectors * SECTOR)
    if payload:
        off = part_start * SECTOR
        image[off:off + len(payload)] = payload

    # Protective MBR: one type-0xEE entry covering the disk from LBA 1.
    image[446 + 4] = 0xEE
    struct.pack_into('<I', image, 446 + 8, 1)
    struct.pack_into('<I', image, 446 + 12, min(sectors - 1, 0xFFFFFFFF))
    image[510:512] = b'\x55\xaa'

    entry = bytearray(128)
    entry[0:16] = struct.pack('<IHH', 0x0FC63DAF, 0x8483, 0x4772) + \
        bytes([0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4])
    entry[16:32] = uuid.uuid4().bytes_le
    struct.pack_into('<QQQ', entry, 32, part_start, part_end, 0)
    label = name.encode('utf-16-le')
    entry[56:56 + len(label)] = label
    entries = bytearray(128 * 128)
    entries[slot * 128:(slot + 1) * 128] = entry

    header = bytearray(92)
    header[0:8] = b'EFI PART'
    struct.pack_into('<IIII', header, 8, 0x00010000, 92, 0, 0)
    struct.pack_into('<QQQQ', header, 24, 1, sectors - 1, 34, sectors - 34)
    header[56:72] = uuid.uuid4().bytes_le
    struct.pack_into('<Q', header, 72, 2)
    struct.pack_into('<II', header, 80, 128, 128)
    struct.pack_into('<I', header, 88, zlib.crc32(entries) & 0xFFFFFFFF)
    struct.pack_into('<I', header, 16, zlib.crc32(header) & 0xFFFFFFFF)

    image[SECTOR:SECTOR + len(header)] = header
    image[2 * SECTOR:2 * SECTOR + len(entries)] = entries
    with open(path, 'wb') as f:
        f.write(image)


def make_fat32(part_start, part_sectors):
    """Build a minimal FAT32 volume: 512-byte sectors, one sector per
    cluster, volume label only.  sectors-per-FAT grows until it describes
    every cluster the data area can hold."""
    vol = bytearray(part_sectors * SECTOR)
    reserved = 32
    nfats = 2
    spc = 1
    spf = 1
    while True:
        data_sectors = part_sectors - reserved - nfats * spf
        clusters = data_sectors // spc
        need = ((clusters + 2) * 4 + SECTOR - 1) // SECTOR
        if need <= spf:
            break
        spf = need

    bs = bytearray(SECTOR)
    bs[0:3] = b'\xeb\x58\x90'
    bs[3:11] = b'XENEVA  '
    struct.pack_into('<H', bs, 0x0B, SECTOR)
    bs[0x0D] = spc
    struct.pack_into('<H', bs, 0x0E, reserved)
    bs[0x10] = nfats
    struct.pack_into('<H', bs, 0x11, 0)
    struct.pack_into('<H', bs, 0x13, 0)
    bs[0x15] = 0xF8
    struct.pack_into('<H', bs, 0x16, 0)
    struct.pack_into('<H', bs, 0x18, 63)
    struct.pack_into('<H', bs, 0x1A, 255)
    struct.pack_into('<I', bs, 0x1C, part_start)
    struct.pack_into('<I', bs, 0x20, part_sectors)
    struct.pack_into('<I', bs, 0x24, spf)
    struct.pack_into('<H', bs, 0x28, 0)
    struct.pack_into('<H', bs, 0x2A, 0)
    struct.pack_into('<I', bs, 0x2C, 2)
    struct.pack_into('<H', bs, 0x30, 1)
    struct.pack_into('<H', bs, 0x32, 6)
    bs[0x40] = 0x80
    bs[0x42] = 0x29
    struct.pack_into('<I', bs, 0x43, uuid.uuid4().int & 0xFFFFFFFF)
    bs[0x47:0x52] = b'XENEVAUSB  '
    bs[0x52:0x5A] = b'FAT32   '
    bs[510:512] = b'\x55\xaa'
    vol[0:SECTOR] = bs
    vol[6 * SECTOR:7 * SECTOR] = bs   # backup boot sector

    fsi = bytearray(SECTOR)
    struct.pack_into('<I', fsi, 0x000, 0x41615252)
    struct.pack_into('<I', fsi, 0x1E4, 0x61417272)
    struct.pack_into('<I', fsi, 0x1E8, clusters - 1)
    struct.pack_into('<I', fsi, 0x1EC, 3)
    struct.pack_into('<I', fsi, 0x1FC, 0xAA550000)
    vol[SECTOR:2 * SECTOR] = fsi

    fat = bytearray(spf * SECTOR)
    struct.pack_into('<I', fat, 0, 0x0FFFFFF8)
    struct.pack_into('<I', fat, 4, 0x0FFFFFFF)
    struct.pack_into('<I', fat, 8, 0x0FFFFFFF)
    for n in range(nfats):
        off = (reserved + n * spf) * SECTOR
        vol[off:off + len(fat)] = fat

    # Root directory: one volume-label entry, the rest is end-of-directory.
    root_off = (reserved + nfats * spf) * SECTOR
    label = bytearray(32)
    label[0:11] = b'XENEVAUSB  '
    label[11] = 0x08
    vol[root_off:root_off + 32] = label
    return vol


gpt_path, fat_path = sys.argv[1], sys.argv[2]
# Raw GPT disk: the used entry sits in slot 1 on purpose.
write_gpt(gpt_path, 16384, 2048, 14335, 'xeneva', 1)

# FAT32 disk: 64 MiB, partition in slot 0.
fat_sectors = 131072
fat_start = 2048
fat_end = fat_sectors - 34
write_gpt(fat_path, fat_sectors, fat_start, fat_end, 'fatdisk', 0,
          make_fat32(fat_start, fat_end - fat_start + 1))
PY
XENEVA_QEMU_EXTRA_ARGS="$XENEVA_QEMU_EXTRA_ARGS -drive if=none,id=mscgpt0,file=$MSC_GPT_IMG,format=raw -device usb-storage,drive=mscgpt0,id=mscgptdev0,port=4.5"
XENEVA_QEMU_EXTRA_ARGS="$XENEVA_QEMU_EXTRA_ARGS -drive if=none,id=mscfat0,file=$MSC_FAT_IMG,format=raw -device usb-storage,drive=mscfat0,id=mscfatdev0,port=4.6"
if [ "$BYPASS_VIRTIO_INPUT" -eq 1 ]; then
	rm -f /tmp/xeneva-qmp.sock "$INPUT_INJECT_LOG"
	XENEVA_QEMU_EXTRA_ARGS="$XENEVA_QEMU_EXTRA_ARGS -qmp unix:/tmp/xeneva-qmp.sock,server=on,wait=off"
	# The replug disk is not attached at boot: the helper adds it through
	# QMP, removes it, and adds it again with the same backend.
	MSC_REPLUG_IMG="$(dirname "$LOG")/xhci-msc-replug.img"
	rm -f "$MSC_REPLUG_IMG"
	truncate -s 8M "$MSC_REPLUG_IMG"
	printf 'XENEVA MSC REPLUG\n' | dd of="$MSC_REPLUG_IMG" conv=notrunc status=none
	# A second UAS disk is attached the same way so the helper can prove
	# that a SuperSpeed replug releases its stream context arrays and reuses
	# the vdisk left behind by the first attach.  Its second LUN covers the
	# shared transport entry and the per-LUN vdisk registry.
	UAS_REPLUG_IMG="$(dirname "$LOG")/xhci-uas-replug.img"
	rm -f "$UAS_REPLUG_IMG"
	truncate -s 8M "$UAS_REPLUG_IMG"
	printf 'XENEVA UAS REPLUG\n' | dd of="$UAS_REPLUG_IMG" conv=notrunc status=none
	UAS_REPLUG_LUN1_IMG="$(dirname "$LOG")/xhci-uas-replug-lun1.img"
	rm -f "$UAS_REPLUG_LUN1_IMG"
	truncate -s 8M "$UAS_REPLUG_LUN1_IMG"
	printf 'XENEVA UAS REPLUG LUN1\n' | dd of="$UAS_REPLUG_LUN1_IMG" conv=notrunc status=none
	# usb-bot gets the same two-LUN replug check over Bulk-Only Transport.
	# The helper adds it dynamically and warms it up once: QEMU reports a
	# unit attention for a freshly realized scsi-hd, and its BOT target
	# answers the follow-up REQUEST SENSE with LOGICAL UNIT NOT SUPPORTED
	# for a non-zero LUN, so LUN 1 cannot probe on the first attach.  The
	# two re-plugs after that prove REPORT LUNS discovery and per-LUN vdisk
	# reuse through a downstream port.
	BOT_REPLUG_IMG="$(dirname "$LOG")/xhci-bot-replug.img"
	rm -f "$BOT_REPLUG_IMG"
	truncate -s 8M "$BOT_REPLUG_IMG"
	printf 'XENEVA BOT REPLUG\n' | dd of="$BOT_REPLUG_IMG" conv=notrunc status=none
	BOT_REPLUG_LUN1_IMG="$(dirname "$LOG")/xhci-bot-replug-lun1.img"
	rm -f "$BOT_REPLUG_LUN1_IMG"
	truncate -s 8M "$BOT_REPLUG_LUN1_IMG"
	printf 'XENEVA BOT REPLUG LUN1\n' | dd of="$BOT_REPLUG_LUN1_IMG" conv=notrunc status=none
	# Keep this in step with the hub path in
	# Scripts/Linux/build_and_run_qemu.sh: the boot tablet hangs off
	# external hub port 1, and the replacement is re-added there so the
	# gate covers hub hotplug as well as root-port hotplug.
	python3 ./Tests/dcl_input_inject.py --log "$LOG" --usb-device gpu0 \
		--tablet-port 4.1 --msc-image "$MSC_REPLUG_IMG" \
		--uas-image "$UAS_REPLUG_IMG" \
		--uas-lun1-image "$UAS_REPLUG_LUN1_IMG" \
		--bot-image "$BOT_REPLUG_IMG" \
		--bot-lun1-image "$BOT_REPLUG_LUN1_IMG" \
		>"$INPUT_INJECT_LOG" 2>&1 &
	INPUT_INJECT_PID=$!
fi
export XENEVA_QEMU_EXTRA_ARGS

# The bypasses are kernel Makefile options now: the test exports the
# variables above and the build script's `make` invocation inherits them.
# Passing --bypass-* here would fail at option parsing before boot.
QEMU_ARGS=(--llvm --headless --no-bt --no-audio)
QEMU_TIMEOUT="${QEMU_TIMEOUT:-120}" ./Scripts/Linux/build_and_run_qemu.sh \
	"${QEMU_ARGS[@]}" >>"$LOG" 2>&1 </dev/null
rc=$?

# The helper has either already exited with the screendump, or is still
# polling a marker that will never come (a failed boot, a failed commit).
if [ -n "$SHOOT_PID" ]; then
	if kill -0 "$SHOOT_PID" 2>/dev/null; then
		kill "$SHOOT_PID" 2>/dev/null
		wait "$SHOOT_PID" 2>/dev/null
		SHOOT_RC=2
	else
		wait "$SHOOT_PID"
		SHOOT_RC=$?
	fi
fi

if [ -n "$GPU_SHOOT_PID" ]; then
	if kill -0 "$GPU_SHOOT_PID" 2>/dev/null; then
		kill "$GPU_SHOOT_PID" 2>/dev/null
		wait "$GPU_SHOOT_PID" 2>/dev/null
		GPU_SHOOT_RC=2
	else
		wait "$GPU_SHOOT_PID"
		GPU_SHOOT_RC=$?
	fi
fi

if [ -n "$INPUT_INJECT_PID" ]; then
	if kill -0 "$INPUT_INJECT_PID" 2>/dev/null; then
		kill "$INPUT_INJECT_PID" 2>/dev/null
		wait "$INPUT_INJECT_PID" 2>/dev/null
		INPUT_INJECT_RC=2
	else
		wait "$INPUT_INJECT_PID"
		INPUT_INJECT_RC=$?
	fi
fi

# ─── 2b. USB2 transport boot ────────────────────────────────────────────────
# usb-uas uses its shared-ring transport only when the controller has no
# streams.  QEMU's dual-speed root ports always pick SuperSpeed for the UAS
# device, so force every xHCI root port to USB2 for this boot and attach two
# LUNs.  The same boot attaches a two-LUN usb-bot on the free USB2 root
# port: usb-storage classic reports max_lun 0, while usb-bot exposes 15, so
# this is where BOT REPORT LUNS discovery can actually run.  Reusing the
# kernel the gate just built keeps this to one extra packing pass plus a
# short QEMU run, and the separate log makes every assertion below prove
# the second boot rather than the first.
UAS_NS_LOG="$(dirname "$LOG")/test_all_uas_ns.log"
: >"$UAS_NS_LOG"
UAS_NS_IMG="$(dirname "$LOG")/xhci-uas-ns.img"
UAS_NS_LUN1_IMG="$(dirname "$LOG")/xhci-uas-ns-lun1.img"
BOT_NS_IMG="$(dirname "$LOG")/xhci-bot-ns.img"
BOT_NS_LUN1_IMG="$(dirname "$LOG")/xhci-bot-ns-lun1.img"
rm -f "$UAS_NS_IMG" "$UAS_NS_LUN1_IMG" "$BOT_NS_IMG" "$BOT_NS_LUN1_IMG"
truncate -s 8M "$UAS_NS_IMG"
truncate -s 8M "$UAS_NS_LUN1_IMG"
truncate -s 8M "$BOT_NS_IMG"
truncate -s 8M "$BOT_NS_LUN1_IMG"
printf 'NO STREAM UAS\n' | dd of="$UAS_NS_IMG" conv=notrunc status=none
printf 'UAS NS LUN1\n' | dd of="$UAS_NS_LUN1_IMG" conv=notrunc status=none
printf 'BOTZERO\n' | dd of="$BOT_NS_IMG" conv=notrunc status=none
printf 'BOTLUN1\n' | dd of="$BOT_NS_LUN1_IMG" conv=notrunc status=none
UAS_NS_ARGS="-global qemu-xhci.p3=0"
UAS_NS_ARGS="$UAS_NS_ARGS -drive if=none,id=uasns0,file=$UAS_NS_IMG,format=raw"
UAS_NS_ARGS="$UAS_NS_ARGS -device usb-uas,id=uasnsdev0,port=3"
UAS_NS_ARGS="$UAS_NS_ARGS -device scsi-hd,drive=uasns0,bus=uasnsdev0.0,lun=0"
UAS_NS_ARGS="$UAS_NS_ARGS -drive if=none,id=uasns1,file=$UAS_NS_LUN1_IMG,format=raw"
UAS_NS_ARGS="$UAS_NS_ARGS -device scsi-hd,drive=uasns1,bus=uasnsdev0.0,lun=1"
UAS_NS_ARGS="$UAS_NS_ARGS -drive if=none,id=botns0,file=$BOT_NS_IMG,format=raw"
UAS_NS_ARGS="$UAS_NS_ARGS -device usb-bot,id=botnsdev0,port=2"
UAS_NS_ARGS="$UAS_NS_ARGS -device scsi-hd,drive=botns0,bus=botnsdev0.0,lun=0,product=BOTLUNZERO"
UAS_NS_ARGS="$UAS_NS_ARGS -drive if=none,id=botns1,file=$BOT_NS_LUN1_IMG,format=raw"
UAS_NS_ARGS="$UAS_NS_ARGS -device scsi-hd,drive=botns1,bus=botnsdev0.0,lun=1,product=BOTLUNONE"
echo
echo "USB2 transport boot (log: $UAS_NS_LOG)"
XENEVA_QEMU_EXTRA_ARGS="$UAS_NS_ARGS" QEMU_TIMEOUT="${UAS_NS_TIMEOUT:-60}" \
	./Scripts/Linux/build_and_run_qemu.sh --llvm --headless --no-bt --no-audio \
	--skip-build >>"$UAS_NS_LOG" 2>&1 </dev/null
uas_ns_rc=$?

case $rc in
	124) ok "QEMU ran to timeout (124, expected)" ;;
	0)   ok "QEMU exited 0" ;;
	*)   bad "QEMU exit status" "got $rc, want 124 or 0" ;;
esac
case $uas_ns_rc in
	124) ok "USB2 QEMU ran to timeout (124)" ;;
	0)   ok "USB2 QEMU exited 0" ;;
	*)   bad "USB2 QEMU exit status" "got $uas_ns_rc, want 124 or 0" ;;
esac

# ─── 2c. big configuration descriptor boot ─────────────────────────────────
# A configuration descriptor may legally exceed one page.  Stock QEMU has
# no model that emits one, so this boot runs only when the QEMU on PATH
# provides the test-only usb-storage-bigcfg model (Tests/qemu/README.md).
# That model puts 5 KB of vendor descriptors ahead of the mass-storage
# interface: a driver that reads only one page cannot find the disk.
BIGCFG_LOG=
if qemu-system-aarch64 -device help 2>/dev/null | grep -q 'usb-storage-bigcfg'; then
	BIGCFG_LOG="$(dirname "$LOG")/test_all_bigcfg.log"
	: >"$BIGCFG_LOG"
	BIGCFG_IMG="$(dirname "$LOG")/xhci-bigcfg.img"
	rm -f "$BIGCFG_IMG"
	truncate -s 8M "$BIGCFG_IMG"
	printf 'BIGC' | dd of="$BIGCFG_IMG" conv=notrunc status=none
	BIGCFG_ARGS="-drive if=none,id=bigcfg0,file=$BIGCFG_IMG,format=raw"
	BIGCFG_ARGS="$BIGCFG_ARGS -device usb-storage-bigcfg,drive=bigcfg0,id=bigcfgdev0,port=2"
	echo
	echo "big configuration descriptor boot (log: $BIGCFG_LOG)"
	XENEVA_QEMU_EXTRA_ARGS="$BIGCFG_ARGS" QEMU_TIMEOUT="${BIGCFG_TIMEOUT:-60}" \
		./Scripts/Linux/build_and_run_qemu.sh --llvm --headless --no-bt --no-audio \
		--skip-build >>"$BIGCFG_LOG" 2>&1 </dev/null
	bigcfg_rc=$?
	case $bigcfg_rc in
		124) ok "bigcfg QEMU ran to timeout (124)" ;;
		0)   ok "bigcfg QEMU exited 0" ;;
		*)   bad "bigcfg QEMU exit status" "got $bigcfg_rc, want 124 or 0" ;;
	esac
fi

# `make | tail` masks failures -- check the log for the compiler's own verdict.
if grep -q 'error:' "$LOG"; then
	bad "build has no error:" "$(grep -m1 'error:' "$LOG" | cut -c1-100)"
else
	ok "build has no error:"
fi

# ─── 3. tree counts ─────────────────────────────────────────────────────────
# After the build, not before: CLEAN=1 removes obj/ on every run, so checking
# first would make the gate depend on leftover objects from an earlier build.
echo
echo "tree counts"
check_count "BaseHdr/linux headers" "$(ls BaseHdr/linux/*.h | wc -l)"            "$EXP_LINUX_H"
check_count "BaseHdr/asm headers"   "$(ls BaseHdr/asm/*.h | wc -l)"              "$EXP_ASM_H"
check_count "DCL sources"           "$(ls DCL/*.c | wc -l)"                      "$EXP_DCL_C"
check_count "DCL objects"           "$(ls KernelAA64/obj/DCL/*.o 2>/dev/null | wc -l)" "$EXP_DCL_O"
check_count "Vendored objects"      "$(find KernelAA64/obj/Vendored -name '*.o' 2>/dev/null | wc -l)" "$EXP_VEND_O"
check_count "total objects"         "$(find KernelAA64/obj -name '*.o' | wc -l)" "$EXP_ALL_O"

# ─── 4. assertions ──────────────────────────────────────────────────────────
echo
echo "boot log"

# A guest that printed nothing is a different failure from a guest whose
# assertions failed, and the 17 `missing:` lines below would bury that
# distinction. Guest lines only exist past QEMU's banner; grepping the whole
# log would let a quoted UARTDebugOut inside a build warning count as boot
# output, so split at the banner first.
if ! awk '/Image ready! Booting QEMU/{s=1} s && /\[aurora\]|\[dcl\]|\[modtest\]|\[pmm\]/{f=1} END{exit f?0:1}' "$LOG"; then
	bad "guest produced output" "no boot lines after the QEMU banner"
	echo
	echo "test_all: $fails FAILED"
	exit 1
fi

expect() {   # label pattern
	if grep -qF -- "$2" "$LOG"; then ok "$1"; else bad "$1" "missing: $2"; fi
}

reject() {   # label pattern -- the line must NOT be somewhere in the log
	if grep -qF -- "$2" "$LOG"; then bad "$1" "present: $2"; else ok "$1"; fi
}

ns_expect() {   # label pattern -- the non-stream boot's own log
	if grep -qF -- "$2" "$UAS_NS_LOG"; then ok "$1"; else bad "$1" "missing: $2"; fi
}

ns_reject() {
	if grep -qF -- "$2" "$UAS_NS_LOG"; then bad "$1" "present: $2"; else ok "$1"; fi
}

bigcfg_expect() {   # label pattern -- the big-config boot's own log
	if grep -qF -- "$2" "$BIGCFG_LOG"; then ok "$1"; else bad "$1" "missing: $2"; fi
}

bigcfg_reject() {
	if grep -qF -- "$2" "$BIGCFG_LOG"; then bad "$1" "present: $2"; else ok "$1"; fi
}

expect "chr_dev_init"                       "chr_dev_init ok (mem devices registered)"
expect "mem devices"                        "mem devices $EXP_MEM ok, 0 failed"
expect "stage 1 primitives"                 "dcl primitives: $EXP_PRIM ok, 0 failed"
expect "stage 2 tty_buffer"                 "dcl tty_buffer: $EXP_TTYBUF ok, 0 failed"
expect "stage 3 serial core"                "dcl serial: $EXP_SERIAL ok, 0 failed"
expect "userspace dcltest"                  "[dcltest] SUMMARY $EXP_DCLTEST ok, 0 failed"

# BT: QEMU virt always exposes UART1, but --no-bt leaves it unconnected, so
# Bt/host.c falls back to the deterministic in-guest mock controller and
# publishes /dev/bt0. bttest then drives the user-space ioctl ABI: identity,
# scan, LE connect, ATT name, LE audio, disconnect.
expect "BT mock controller"                 "[virtio-bt]: mock controller ready (2 fake advertisers)"
expect "BT device node"                     "[bt]: /dev/bt0 ready"
expect "userspace bttest"                   "[bttest] SUMMARY 22 ok, 0 failed"
expect "host BT unit tests"                 "PASSED (0)"

# The vendored driver, compiled from source into the image.
expect "vendored driver registered"         "[dcl]: virtio_rng driver registered"
expect "vendored driver is not a .ko"       "[modtest]: virtio_rng built-in"
expect "probe ran"                          "[dcl-virtio]: probe done"
# The number after the dot is DCL's shared ida counter (linux_kmod_shim.c
# hands out one sequence for every IDA), so it is not fixed at 0 once other
# IDAs have allocated; the registration itself is what this checks.
expect "/dev/hwrng registered"              "[dcl]: hwrng_register (virtio_rng."
expect "clipboard device"                   "[aurora]: device clipboard registered"
expect "/dev/hwrng round-trip"              "[dcl]: /dev/hwrng round-trip OK (samples differ)"

# Stage 4: drivers/char/virtio_console.c, vendored from source and compiled
# into the image -- registered by DclRunInitcalls(), bound by a scan for
# 1AF4:1043, then driven through one write/read round against QEMU's
# virtserialport. "device 'vport" rather than the full name: the node is
# named vport%up%u from DCL's device index and the *host's* port id
# (virtio_console.c:1371), so pinning the digits would make this assertion
# fail the day a second port appears instead of the day the driver breaks.
expect "virtio_console registered"          "[dcl]: virtio_console driver registered"
expect "virtio_console is not a .ko"        "[modtest]: virtio_console built-in"
expect "virtio-console device found"        "[dcl-virtio]: virtio-console device registered"
expect "vport node created"                 "[dcl]: device 'vport"
expect "vport round-trip"                   "[modtest]: vport selftest rc=0"

# Stage 5: drivers/gpu/drm/tiny/bochs.c, vendored from source and compiled
# into the image -- the tree's first PCI driver: matched by the DCL PCI
# fabric (DCL/pci_shim.c) against a bochs-display and bound with no virtio
# ring anywhere. Upstream's QEMU set no longer includes one, so the stage
# only runs when the caller supplied a bochs-display; otherwise the guest
# reports that it skipped, and that skip is asserted rather than inferred.
expect "bochs pci driver registered"        "[dcl]: bochs pci driver registered"
# The slot ceiling must not be what limits the bus: the scan walks only the
# buses the ECAM window covers, and "0 dropped" is the scan saying the
# ceiling was never reached. A regression to walking past the window shows
# up here as a count -- the first boot with the ceiling raised read RAM as
# devices and its drop count ran into the hundreds.
expect "pci bus fits its ceiling"           "0 dropped"

if [ "$EXPECT_BOCHS" -eq 1 ]; then
	expect "bochs bound by the pci fabric"  "bound to bochs-drm"
	expect "bochs card discovered by name"  "[dcltest] info bochs at /dev/dri/card"
	# And the pattern itself: dcltest committed a 0x123456-rgb fill
	# through the bochs card -- bochs.c's shadow plane memcpy'd it into
	# BAR0, which is what the device scans out -- and the helper above
	# took the screendump at that moment. A screendump that never got
	# taken and one that shows something else are different failures, so
	# they report separately.
	if [ "$SHOOT_RC" -eq 0 ] && [ -f "$SHOT" ]; then
		ok "bochs screendump captured"
	else
		bad "bochs screendump captured" "helper exit $SHOOT_RC"
	fi
	if [ -f "$SHOT" ]; then
		if pix_msg=$(python3 ./Tests/dcl_screendump.py check \
				--ppm "$SHOT" --rgb 123456 2>&1); then
			ok "bochs scanout pixel proof"
		else
			bad "bochs scanout pixel proof" \
				"$(printf '%s' "$pix_msg" | tail -1 | cut -c1-100)"
		fi
	else
		bad "bochs scanout pixel proof" "no screendump at $SHOT"
	fi
else
	expect "bochs stage skipped (no device)" "[dcltest] info bochs-drm absent, stage 5 skipped"
fi

# Stage 6: drivers/gpu/drm/virtio/, vendored from source -- the driver rides
# the same PCI fabric and virtio rings as the console, and it is armed only
# when the kernel is built with BYPASS_VIRTGPU=1. The test exports that
# Makefile variable; upstream no longer exposes a build-script flag for it.
if [ "${BYPASS_VIRTGPU:-0}" -eq 1 ]; then
	expect "virtio_gpu is not a .ko"         "[modtest]: virtio_gpu built-in"
	expect "virtio-gpu bound by the fabric"  "[drm] pci: virtio-gpu-pci detected at"
	expect "virtio-gpu card discovered"      "[dcltest] info virtio-gpu at /dev/dri/card"
	expect "virtio-gpu VERSION matches"      "[dcltest] PASS drm virtio-gpu VERSION (0)"
	expect "virtio-gpu initialized"          "[drm] Initialized virtio_gpu"
	# Stage 7: the console rides the vendored driver's own scanout -- the
	# bridge allocates a resource through the vendored object API, repoints
	# the console at its backing and installs the present mirror. The attach
	# marker is the whole claim; the failure marker would mean it declined.
	expect "console on the vendored scanout" "[virtgpu-console]: console on vendored scanout"
	expect "virtio-gpu console KMS fb"        "[virtgpu-console]: KMS fb"
	reject "no vendored console attach failures" "[virtgpu-console]: resource create failed"
	reject "no virtio-gpu KMS fallback"      "[virtgpu-console]: atomic modeset failed"
	reject "no virtio-gpu fb create failure" "[virtgpu-console]: framebuffer create failed"
	expect "virtio-gpu atomic plane commit"  "[dcltest] PASS drm virtio-gpu atomic commit (0)"
	expect "virtio-gpu console fb restored" "[dcltest] PASS drm virtio-gpu console fb active (1)"
	# And the same host-side pixel proof as bochs: the guest's test fb is
	# what QEMU scanned out when the marker stopped the guest for 4s.
	if [ "$GPU_SHOOT_RC" -eq 0 ] && [ -f "$GPU_SHOT" ]; then
		ok "virtio-gpu screendump captured"
	else
		bad "virtio-gpu screendump captured" "helper exit $GPU_SHOOT_RC"
	fi
	if [ -f "$GPU_SHOT" ]; then
		if pix_msg=$(python3 ./Tests/dcl_screendump.py check \
				--ppm "$GPU_SHOT" --rgb 00beef 2>&1); then
			ok "virtio-gpu scanout pixel proof"
		else
			bad "virtio-gpu scanout pixel proof" \
				"$(printf '%s' "$pix_msg" | tail -1 | cut -c1-100)"
		fi
	else
		bad "virtio-gpu scanout pixel proof" "no screendump at $GPU_SHOT"
	fi
	# QEMU rejects a descriptor chain it cannot map and the guest prints its
	# own verdict when a probe-time wait expires with the reply still unread;
	# either one is a broken queue, not a detail.
	reject "no rejected descriptor chains"   "zero sized buffers are not allowed"
	reject "no expired virtio-gpu waits"     "wait_event in virtio_gpu_init gave up"
fi

# QEMU xHCI: usb-kbd and usb-tablet are on the same machine as the virtio
# pair.  The native driver enumerates both and hands their HID reports to
# the /dev/kybrd and /dev/mice queues, independently of the vendored path.
expect "xhci keyboard enumerated"            "[xhci]: HID keyboard ready on port"
expect "xhci pointer enumerated"             "[xhci]: HID pointer ready on port"
expect "xhci superspeed root device"         "[xhci]: superspeed device on port"
expect "xhci superspeed companion"           "[xhci]: ss companion ep"
expect "xhci extended capabilities"          "[xhci]: ext cap 2 protocol"
expect "xhci supported protocols"            "[xhci]: protocol USB ports"
expect "xhci scratchpad count"               "[xhci]: 0 scratchpad buffers"
expect "xhci message irq allocated"          "[xhci]: irq spi"
expect "xhci message irq delivered"          "[xhci]: irq delivered"
expect "xhci product strings"                "[xhci]: product QEMU USB Keyboard"
expect "xhci HID descriptor parsed"          "[xhci-hid]: report iface"
expect "xhci keyboard LED report"            "[xhci-hid]: leds 0"
expect "xhci hub enumerated"                 "[xhci]: hub ready on port"
expect "xhci hub context updated"            "[xhci]: hub context slot"
expect "xhci hub child enumerated"           "[xhci-hub]: child enumerated on port"
expect "xhci nested hub enumerated"          "[xhci]: hub ready on port 8.8 slot"
expect "xhci nested hub child"               "[xhci-hub]: child enumerated on port 8.8.1"
expect "xhci nested hub product"             "[xhci]: product Wacom PenPartner"
expect "xhci report id descriptor"           "[xhci-hid]: report iface 0 len 110 fields 4 kind pointer ids 1"
expect "xhci nested HID path"                "[xhci]: HID pointer ready on port 8.8.1 slot"
expect "xhci high speed keyboard"            "speed=3 mps=64"
expect "xhci mass storage inquiry"           "[xhci-msc]: inquiry"
expect "xhci superspeed mass storage"        "[xhci-msc]: read lba0 53 53 20 4D"
expect "xhci mass storage capacity"          "[xhci-msc]: capacity"
expect "xhci mass storage large capacity"    "[xhci-msc]: capacity 3145728 MB x 512 bytes"
expect "xhci mass storage read"              "[xhci-msc]: read lba0 58 45 4E 45"
expect "xhci mass storage high lba"          "[xhci-msc]: read last lba ok"
expect "xhci mass storage high lba write"    "[xhci-msc]: write last lba ok"
expect "xhci mass storage write"             "[xhci-msc]: write lba0 ok"
expect "xhci mass storage sense"             "[xhci-msc]: sense status 1 key 7 asc 27"
expect "xhci mass storage write rejected"    "[xhci-msc]: write lba0 failed"
expect "xhci mass storage vdisk"             "[xhci-msc]: vdisk USB"
expect "xhci uas stream capability"          "[xhci]: primary stream arrays"
expect "xhci uas streams enabled"            "[xhci-uas]: streams enabled"
expect "xhci uas inquiry"                    "[xhci-uas]: inquiry QEMU HARDDISK"
expect "xhci uas capacity"                   "[xhci-uas]: capacity"
expect "xhci uas read"                       "[xhci-uas]: read lba0 58 45 4E 45"
expect "xhci uas high lba"                    "[xhci-uas]: read last lba ok"
expect "xhci uas high lba write"              "[xhci-uas]: write last lba ok"
expect "xhci uas write"                      "[xhci-uas]: write lba0 ok"
expect "xhci uas vdisk"                      "[xhci-uas]: vdisk USB"
expect "xhci uas report luns"                "[xhci-uas]: report luns 1"
expect "xhci uas second lun"                 "[xhci-uas]: lun 1"
expect "xhci uas second lun read"            "[xhci-uas]: read lba0 55 41 53 20"
expect "xhci uas second lun write"           "[xhci-uas]: write lba0 ok"
expect "xhci uas second lun vdisk"           "[xhci-uas]: vdisk USB QEMU HARDDISK L1 ready"
# The second boot forced every root port to USB2, so usb-uas attaches at
# high speed without streams and both LUNs share LUN 0's rings.  A short
# REPORT LUNS reply is the case that used to desync this path: its status
# was left queued and the retry read it as a ready notification.  The BOT
# disk on port 2 proves the same discovery path over Bulk-Only Transport,
# which usb-storage classic cannot expose because its max_lun is zero.
if awk '/Image ready! Booting QEMU/{s=1} s && /\[aurora\]|\[dcl\]|\[modtest\]|\[pmm\]/{f=1} END{exit f?0:1}' "$UAS_NS_LOG"; then
	ok "USB2 guest produced output"
else
	bad "USB2 guest produced output" "no boot lines after the QEMU banner"
fi
ns_expect "xhci uas non-stream attach"       "[xhci-uas]: streams off, shared rings"
ns_expect "xhci uas non-stream lun 0"        "[xhci-uas]: lun 0"
ns_expect "xhci uas non-stream capacity"     "[xhci-uas]: capacity 8 MB x 512 bytes"
ns_expect "xhci uas non-stream read"         "[xhci-uas]: read lba0 4E 4F 20 53"
ns_expect "xhci uas non-stream write"        "[xhci-uas]: write lba0 ok"
ns_expect "xhci uas non-stream vdisk"        "[xhci-uas]: vdisk USB QEMU HARDDISK ready on port 3"
ns_expect "xhci uas non-stream report luns"  "[xhci-uas]: report luns 1"
ns_expect "xhci uas non-stream lun 1 read"   "[xhci-uas]: read lba0 55 41 53 20"
ns_expect "xhci uas non-stream lun 1 vdisk"  "[xhci-uas]: vdisk USB QEMU HARDDISK L1 ready on port 3"
ns_reject "xhci uas non-stream no tag errors" "[xhci-uas]: tag"
ns_reject "xhci uas non-stream no probe fail" "[xhci]: uas probe failed"
ns_expect "xhci bot multi-lun report luns"   "[xhci-msc]: report luns 1"
ns_expect "xhci bot multi-lun lun 1"         "[xhci-msc]: lun 1"
ns_expect "xhci bot multi-lun inquiry"       "[xhci-msc]: inquiry BOTLUNONE"
ns_expect "xhci bot multi-lun capacity"      "[xhci-msc]: capacity 8 MB x 512 bytes"
ns_expect "xhci bot multi-lun lun 0 read"    "[xhci-msc]: read lba0 42 4F 54 5A"
ns_expect "xhci bot multi-lun read"          "[xhci-msc]: read lba0 42 4F 54 4C"
ns_expect "xhci bot multi-lun lun 0 vdisk"   "[xhci-msc]: vdisk USB BOTLUNZERO ready on port 2"
ns_expect "xhci bot multi-lun vdisk"         "[xhci-msc]: vdisk USB BOTLUNONE L1 ready on port 2"
ns_reject "xhci bot multi-lun no probe fail" "[xhci-msc]: lun 1 probe failed"
ns_reject "xhci bot multi-lun no slot error" "[xhci-msc]: no slot for lun"

# The big-config boot only runs under a QEMU that has usb-storage-bigcfg.
# Its MSC interface starts after 5 KB of vendor descriptors, so a successful
# read proves the full descriptor was fetched rather than truncated.
if [ -n "$BIGCFG_LOG" ]; then
	if awk '/Image ready! Booting QEMU/{s=1} s && /\[aurora\]|\[dcl\]|\[modtest\]|\[pmm\]/{f=1} END{exit f?0:1}' "$BIGCFG_LOG"; then
		ok "bigcfg guest produced output"
	else
		bad "bigcfg guest produced output" "no boot lines after the QEMU banner"
	fi
	bigcfg_expect "xhci bigcfg product string" "[xhci]: product QEMU USB BIGCFG DISK"
	bigcfg_expect "xhci bigcfg disk capacity"  "[xhci-msc]: capacity 8 MB x 512 bytes"
	bigcfg_expect "xhci bigcfg disk read"      "[xhci-msc]: read lba0 42 49 47 43"
	bigcfg_expect "xhci bigcfg vdisk"          "[xhci-msc]: vdisk USB QEMU HARDDISK ready on port 2"
	bigcfg_reject "xhci bigcfg not truncated"  "truncated to"
	bigcfg_reject "xhci bigcfg no probe fail"  "[xhci]: msc probe failed"
else
	printf '  %-42s skipped (%s)\n' "xhci big configuration descriptor" \
		"QEMU lacks usb-storage-bigcfg"
fi
expect "xhci mass storage gpt partition"     "[vdisk]: USB QEMU HARDDISK gpt partition 1 lba 2048"
expect "xhci mass storage gpt name"          "[vdisk]: USB QEMU HARDDISK gpt partition 1 lba 2048 name xeneva"
expect "xhci fat32 partition"                "[vdisk]: USB QEMU HARDDISK gpt partition 0 lba 2048 name fatdisk"
expect "xhci fat32 filesystem"               "[vdisk]: USB QEMU HARDDISK has FAT32 file system"
expect "xhci fat32 mounted"                  "[fat]: USB QEMU HARDDISK mounted at"
expect "xhci fat32 volume label"             "oem XENEVA"
expect "xhci serial product string"          "[xhci]: product QEMU USB SERIAL"
expect "xhci serial ftdi initialized"        "[xhci-serial]: FTDI ready"
expect "xhci serial tty node"                "[xhci-serial]: /dev/ttyUSB"
expect "xhci HID poll thread"                "[xhci-hid]: poll thread running"
expect "xhci port poll thread"               "[xhci-hotplug]: port thread running"

# Vendored input: the native board scanner is compiled out, so these lines
# can only come from DCL's multi-function scan binding virtio_input.  The
# key and pointer lines are the guest's own decode of host-injected events;
# the helper above is what supplies those events after attach.
if [ "$BYPASS_VIRTIO_INPUT" -eq 1 ]; then
	expect "virtio_input driver registered"  "[dcl]: virtio_input driver registered"
	expect "virtio_input is not a .ko"       "[modtest]: virtio_input built-in"
	expect "keyboard and tablet found"       "[modtest]: virtio_input 2 device(s) registered"
	expect "vendored keyboard attached"      "[virtio-input]: attached keyboard"
	expect "vendored tablet attached"        "[virtio-input]: attached tablet"
	expect "injected key decoded"            "[virtio-input]: key code 30 value 1"
	expect "injected pointer decoded"        "[virtio-input]: pointer"
	# device=gpu0 above targets the USB HID handlers QEMU binds to that
	# console, proving the xHCI path decodes the same event classes.
	expect "injected USB key down decoded"   "[xhci-hid]: key 1E down"
	expect "injected USB key up decoded"     "[xhci-hid]: key 1E up"
	expect "injected USB pointer decoded"    "[xhci-hid]: pointer"
	# The helper removes both boot HID devices and adds replacements on the
	# xHCI bus. These lines are the port thread's own verdicts; the second
	# key decode comes from the replacement keyboard.
	expect "hotplug keyboard released"       "[xhci-hotplug]: disconnect on port"
	expect "hotplug device enumerated"       "[xhci-hotplug]: enumerated port"
	expect "hotplugged USB key down decoded" "[xhci-hid]: key 30 down"
	expect "hotplugged USB key up decoded"   "[xhci-hid]: key 30 up"
	# usb-audio reports a 64-byte control pipe, unlike the 8-byte HID and
	# hub devices, so the helper's transient attach covers Evaluate Context.
	expect "xhci ep0 max packet update"      "[xhci]: ep0 max packet 64"
	reject "no native virtio keyboard binding" \
		"[virtio-keyboard]: kernel driver got key code"
	reject "no vendored input handler failure" \
		"[virtio-input]: handler registration failed"
	if [ "$INPUT_INJECT_RC" -eq 0 ]; then
		ok "host input injection"
	else
		bad "host input injection" \
			"helper exit $INPUT_INJECT_RC: $(tail -1 "$INPUT_INJECT_LOG" 2>/dev/null | cut -c1-80)"
	fi
	if grep -qF "mass-storage replug reused its vdisk" "$INPUT_INJECT_LOG" 2>/dev/null; then
		ok "host mass-storage replug"
	else
		bad "host mass-storage replug" \
			"helper did not reach the replug check"
	fi
	if grep -qF "uas replug reused its vdisk" "$INPUT_INJECT_LOG" 2>/dev/null; then
		ok "host uas replug"
	else
		bad "host uas replug" \
			"helper did not reach the uas replug check"
	fi
	if grep -qF "bot multi-lun replug reused its vdisks" "$INPUT_INJECT_LOG" 2>/dev/null; then
		ok "host bot multi-lun replug"
	else
		bad "host bot multi-lun replug" \
			"helper did not reach the bot replug check"
	fi
fi

# The ELF loader is a separate guarantee from "the driver links" -- it still
# loads and runs test_module.o through DCL/module_loader.c.
expect "ELF loader still exercised"         "[modtest] init returned  0"

# Nothing anywhere may be failing. The bare `FAILED` in the old pattern was
# a substring match, and a *macro name* containing it -- `VIRTIO_CONFIG_S_FAILED`,
# quoted verbatim inside a clang -Wmacro-redefined warning -- was enough to
# fail this line. Requiring FAILED to stand as its own word keeps the intent
# ("something printed the word") and drops identifiers, which are names for
# things rather than reports of them. The `[1-9][0-9]* failed` half was
# already a count and needs no change.
_failre='[1-9][0-9]* failed|(^|[^_[:alnum:]])FAILED([^_[:alnum:]]|$)'
BOOT_LOGS=("$LOG" "$UAS_NS_LOG")
if [ -n "$BIGCFG_LOG" ]; then
	BOOT_LOGS+=("$BIGCFG_LOG")
fi
if grep -qE "$_failre" "${BOOT_LOGS[@]}"; then
	bad "no failures anywhere" "$(grep -m1 -E "$_failre" "${BOOT_LOGS[@]}" | cut -c1-100)"
else
	ok "no failures anywhere"
fi
if grep -qiE '^[a-z].*\b(panic|unhandled)\b' "${BOOT_LOGS[@]}"; then
	bad "no panic/unhandled" "$(grep -m1 -iE '^[a-z].*\b(panic|unhandled)\b' "${BOOT_LOGS[@]}" | cut -c1-100)"
else
	ok "no panic/unhandled"
fi

# ─── digest ─────────────────────────────────────────────────────────────────
echo
echo "digest"
python3 ./ts_filter.py "$LOG" 2>/dev/null || echo "  (ts_filter.py unavailable)"

echo
if [ "$fails" -eq 0 ]; then
	echo "test_all: ALL GREEN"
	exit 0
fi
echo "test_all: $fails FAILED"
exit 1
