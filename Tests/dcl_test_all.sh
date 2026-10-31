#!/usr/bin/env bash
# Tests/dcl_test_all.sh -- the gate for the DCL tty/serial port.
#
# "Green here" is what a finished stage means: every stage ends by running
# this. Four parts, in order, each reporting `ok` or stopping with a reason:
#
#   1. tree counts  -- a file appearing or disappearing should be a decision
#                      with a number attached, not something noticed three
#                      stages later
#   2. build        -- full `make llvm`, grepped for `error:`. Worth being
#                      explicit: `make llvm | tail` reports *tail's* exit
#                      status, so pipefail alone would pass a failed build
#   3. boot         -- a complete QEMU run. Exit 124 (timeout) is the
#                      expected status; the verdict is the log
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

# The gate tracks the vendored path now.  An explicit 0 still selects the
# native virtiokbd/virtiotablet pair for a manual comparison, but the input
# assertions below only run when this is 1.
BYPASS_VIRTIO_INPUT="${BYPASS_VIRTIO_INPUT:-1}"
export BYPASS_VIRTIO_INPUT

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
EXP_ALL_O=277      # everything under KernelAA64/obj

# ─── expected boot-log results ──────────────────────────────────────────────
EXP_MEM=9
EXP_PRIM=10
EXP_TTYBUF=25
EXP_SERIAL=21
EXP_DCLTEST=186   # 44 stage-1..4 + 38 KMS objects + 22 dumb/atomic + 3 client caps + 1 wb encoder lookup + 20 writeback pixel proof + 13 page flip event + 45 stage-5 card1 (bochs)
if [ "${BYPASS_VIRTGPU:-0}" -eq 1 ]; then
	# + 33 stage-6 card2 (virtio-gpu) checks
	EXP_DCLTEST=220
fi

LOG=${LOG:-/tmp/xeneva/test_all.log}
mkdir -p "$(dirname "$LOG")"

fails=0
ok()  { printf '  %-42s ok\n' "$1"; }
bad() { printf '  %-42s FAILED (%s)\n' "$1" "${2:-}"; fails=$((fails + 1)); }

check_count() {  # label actual expected
	if [ "$2" -eq "$3" ]; then ok "$1 = $2"; else bad "$1" "got $2, want $3"; fi
}

# ─── 1. tree counts ─────────────────────────────────────────────────────────
echo "tree counts"
check_count "BaseHdr/linux headers" "$(ls BaseHdr/linux/*.h | wc -l)"            "$EXP_LINUX_H"
check_count "BaseHdr/asm headers"   "$(ls BaseHdr/asm/*.h | wc -l)"              "$EXP_ASM_H"
check_count "DCL sources"           "$(ls DCL/*.c | wc -l)"                      "$EXP_DCL_C"
check_count "DCL objects"           "$(ls KernelAA64/obj/DCL/*.o 2>/dev/null | wc -l)" "$EXP_DCL_O"
check_count "Vendored objects"      "$(find KernelAA64/obj/Vendored -name '*.o' 2>/dev/null | wc -l)" "$EXP_VEND_O"
check_count "total objects"         "$(find KernelAA64/obj -name '*.o' | wc -l)" "$EXP_ALL_O"

[ "$fails" -eq 0 ] || { echo "count assertions failed; not booting"; exit 1; }

# ─── 2 + 3. build and boot ──────────────────────────────────────────────────
echo
echo "build + boot (log: $LOG)"
if [ -n "${CLEAN:-}" ]; then
	echo "  CLEAN=1 -> make clean"
	make -C KernelAA64 clean >/dev/null 2>&1
	for app in Init Clipd dcltest XEShell Terminal; do
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
echo "  building Process/{Init,Clipd,dcltest,XEShell,Terminal}"
for app in Init Clipd dcltest XEShell Terminal; do
	( cd "Process/$app" && make llvm ) >>"$LOG" 2>&1
done
cp -f Process/Init/init.exe Resources/resources/ >>"$LOG" 2>&1
cp -f Process/Clipd/clipd.exe Resources/resources/ >>"$LOG" 2>&1
cp -f Process/dcltest/dcltest.exe Resources/resources/ >>"$LOG" 2>&1
cp -f Process/XEShell/xesh.exe Resources/resources/ >>"$LOG" 2>&1
cp -f Process/Terminal/term.exe Resources/resources/ >>"$LOG" 2>&1

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
# killed below if QEMU ends before the line ever appears.
SHOT=${SHOT:-/tmp/xeneva/bochs.ppm}
rm -f "$SHOT"
python3 ./Tests/dcl_screendump.py wait \
	--log "$LOG" \
	--marker "[dcltest] info bochs scanout committed" \
	--out "$SHOT" --dev bochs0 --timeout 90 >/dev/null 2>&1 &
SHOOT_PID=$!

# Stage 6's pixel proof is the same host-side idea on the virtio-gpu
# display: dcltest flips card2's primary plane to a known colour, prints
# the marker once the atomic commit has returned, and waits long enough
# for this watcher to attach to QEMU's monitor.  It is armed only in a
# bypass image because card2 does not exist otherwise.
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
if [ "$BYPASS_VIRTIO_INPUT" -eq 1 ]; then
	rm -f /tmp/xeneva-qmp.sock "$INPUT_INJECT_LOG"
	XENEVA_QEMU_EXTRA_ARGS="${XENEVA_QEMU_EXTRA_ARGS:-} -qmp unix:/tmp/xeneva-qmp.sock,server=on,wait=off"
	export XENEVA_QEMU_EXTRA_ARGS
	python3 ./Tests/dcl_input_inject.py --log "$LOG" >"$INPUT_INJECT_LOG" 2>&1 &
	INPUT_INJECT_PID=$!
fi

QEMU_ARGS=(--llvm --headless --no-bt --no-audio)
if [ "${BYPASS_VIRTGPU:-0}" -eq 1 ]; then
	QEMU_ARGS+=(--bypass-virtgpu)
fi
if [ "$BYPASS_VIRTIO_INPUT" -eq 1 ]; then
	QEMU_ARGS+=(--bypass-virtio-input)
fi
QEMU_TIMEOUT="${QEMU_TIMEOUT:-120}" ./Scripts/Linux/build_and_run_qemu.sh \
	"${QEMU_ARGS[@]}" >>"$LOG" 2>&1 </dev/null
rc=$?

# The helper has either already exited with the screendump, or is still
# polling a marker that will never come (a failed boot, a failed commit).
if kill -0 "$SHOOT_PID" 2>/dev/null; then
	kill "$SHOOT_PID" 2>/dev/null
	wait "$SHOOT_PID" 2>/dev/null
	shoot_rc=2
else
	wait "$SHOOT_PID"
	shoot_rc=$?
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

case $rc in
	124) ok "QEMU ran to timeout (124, expected)" ;;
	0)   ok "QEMU exited 0" ;;
	*)   bad "QEMU exit status" "got $rc, want 124 or 0" ;;
esac

# `make | tail` masks failures -- check the log for the compiler's own verdict.
if grep -q 'error:' "$LOG"; then
	bad "build has no error:" "$(grep -m1 'error:' "$LOG" | cut -c1-100)"
else
	ok "build has no error:"
fi

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

expect "chr_dev_init"                       "chr_dev_init ok (mem devices registered)"
expect "mem devices"                        "mem devices $EXP_MEM ok, 0 failed"
expect "stage 1 primitives"                 "dcl primitives: $EXP_PRIM ok, 0 failed"
expect "stage 2 tty_buffer"                 "dcl tty_buffer: $EXP_TTYBUF ok, 0 failed"
expect "stage 3 serial core"                "dcl serial: $EXP_SERIAL ok, 0 failed"
expect "userspace dcltest"                  "[dcltest] SUMMARY $EXP_DCLTEST ok, 0 failed"

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
# fabric (DCL/pci_shim.c) against QEMU's bochs-display and bound with no
# virtio ring anywhere.  These are the guest's lines; the pixels are the
# host's, asserted right below.
expect "bochs pci driver registered"        "[dcl]: bochs pci driver registered"
expect "bochs bound by the pci fabric"      "bound to bochs-drm"
expect "bochs published card1"              "[dcl]: device_add 'dri/card1'"
# The slot ceiling must not be what limits the bus: the scan walks only the
# buses the ECAM window covers, and "0 dropped" is the scan saying the
# ceiling was never reached. A regression to walking past the window shows
# up here as a count -- the first boot with the ceiling raised read RAM as
# devices and its drop count ran into the hundreds.
expect "pci bus fits its ceiling"           "0 dropped"

# And the pattern itself: dcltest committed a 0x123456-rgb fill through
# card1 -- bochs.c's shadow plane memcpy'd it into BAR0, which is what the
# device scans out -- and the helper above took the screendump at that
# moment.  A screendump that never got taken and one that shows something
# else are different failures, so they report separately.
if [ "$shoot_rc" -eq 0 ] && [ -f "$SHOT" ]; then
	ok "bochs screendump captured"
else
	bad "bochs screendump captured" "helper exit $shoot_rc"
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

# Stage 6: drivers/gpu/drm/virtio/, vendored from source -- the driver rides
# the same PCI fabric and virtio rings as the console, and it is armed only
# in a --bypass-virtgpu image: the internal virtgpu.dll owns 1AF4:1050 by
# default, so BYPASS_VIRTGPU=1 is what puts these lines in the log at all.
if [ "${BYPASS_VIRTGPU:-0}" -eq 1 ]; then
	expect "virtio_gpu is not a .ko"         "[modtest]: virtio_gpu built-in"
	expect "virtio-gpu bound by the fabric"  "[drm] pci: virtio-gpu-pci detected at"
	expect "virtio-gpu published card2"      "[dcl]: device_add 'dri/card2'"
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
	expect "virtio-gpu atomic plane commit"  "[dcltest] PASS drm card2 atomic commit (0)"
	expect "virtio-gpu console fb restored" "[dcltest] PASS drm card2 console fb active (1)"
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
if grep -qE "$_failre" "$LOG"; then
	bad "no failures anywhere" "$(grep -m1 -E "$_failre" "$LOG" | cut -c1-100)"
else
	ok "no failures anywhere"
fi
if grep -qiE '^[a-z].*\b(panic|unhandled)\b' "$LOG"; then
	bad "no panic/unhandled" "$(grep -m1 -iE '^[a-z].*\b(panic|unhandled)\b' "$LOG" | cut -c1-100)"
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
