#!/bin/bash
set -e

# All-in-one build + image + QEMU test script for XenevaOS (AArch64).
#
# Usage: Scripts/Linux/build_and_run_qemu.sh [OPTIONS]
#
#   --llvm                  Build with the LLVM/Clang toolchain (default).
#   --gcc                   Build with the GCC toolchain.
#   --skip-build            Don't build BootAA64/KernelAA64/apps at all; just
#                           reassemble images from whatever binaries already
#                           exist on disk.
#   --force-user-apps       Also rebuild userspace libs/apps and redeploy them
#                           into Resources/resources/ before packing.
#   --bleed                 Streamlined AArch64 LLVM profile: glass is kept,
#                           network and audio stay alive, memory stays small.
#   --soak                  Build the kernel with the scheduler soak test
#                           (KernelAA64/Hal/sched_soak.c) started at boot.
#   --debug-alloc           Build the kernel with allocator debug detectors
#                           (redzones, poisoning, caller tracking, leak dump).
#   --direct-scanout        Rebuild userspace with the compositor drawing into
#                           the GOP framebuffer when its pitch permits it.
#   --unikernel             One-process XR shell: DeodhaiXR links XELnch and
#                           Namdapha as threads instead of LoadExec. Does not
#                           merge the kernel. Requires a userspace rebuild.
#   --static-components     Kernel-side unikernel: the virtio drivers (net, gpu,
#                           sound, blk) are linked into the kernel image instead
#                           of loaded from .dll files. LLVM only. They stay
#                           resident (no drvctl unload, no user-space servers).
#   --openxr                DeodhaiXR emits OpenXR (QEMU SBS runtime). Pair with
#                           Tools/xeneva-xr-view and WiVRn for Quest 2.
#   -xr-demo, --xr-demo     Open the modular XR demo TUI. Defaults enable EGL,
#                           hand pointer, controllers, both-hand mesh, and
#                           automatic 1024x768 boot. Needs WiVRn + Quest 2.
#   --xr-demo-defaults      Run that recommended XR profile without a TUI.
#   --xr-[no-]hands         Enable/disable right-hand pointer + pinch.
#   --xr-[no-]controllers   Enable/disable right-controller pointer + trigger.
#   --xr-[no-]hand-mesh     Enable/disable both-hand joint mesh.
#   --xr-resolution=MODE    1024x768 (default), 800x600, 640x480, or manual.
#   --xr-gain=N             Pointer gain in guest pixels per meter (20000).
#   --xr-scale=MODE         sharp (nearest, default) or smooth (linear).
#   --xr-[no-]vnc           Enable/disable optional localhost VNC service.
#   --xr-[no-]telnet        Enable/disable optional localhost HMP monitor.
#   --egl-headless          QEMU -display egl-headless + dbus (DMA-BUF scanout).
#                           Use Tools/xeneva-xr-view --desktop --egl to steal frames.
#   --tui                   Open an interactive text menu to pick toolchain,
#                           profile, run mode and guest options, then continue
#                           into the normal build/run flow. Needs a terminal;
#                           flags passed alongside preselect menu entries.
#   --no-network            Drop the whole network userspace (netmngr daemon
#                           plus ping, udpecho, route, iptable) from the build
#                           and the image. Init skips the missing daemon.
#   --no-audio              Drop the audio userspace (deoaud daemon and
#                           AudioPlayer) from the build and the image.
#   --no-virtio-serial      Skip the virtio-serial controller and its one
#                           virtserialport, so the guest's virtio_console
#                           driver has no device to probe. It is on by
#                           default, like --virtio-rng: the driver under
#                           test needs something to be there.
#   --virtio-serial-port=N  Host TCP port for the virtserialport socket
#                           (default 43211, or $VIRTIO_SERIAL_PORT).
#   --no-clipboard          Keep the virtio-serial controller but drop its
#                           virtserialport, so nothing opens the host socket
#                           and clip_bridge.py has nothing to talk to. The
#                           clipboard port is on by default.
#   --clipboard             Force the clipboard port back on.
#   --no-doom               Drop the Doom addon (doom.exe + doom2.wad) from
#                           the image.
#   --no-netsurf            Drop the NetSurf browser (netsurf.exe) from
#                           the image.
#   --bt                    Attach Bluetooth on the second serial. The socket
#                           is /tmp/bt-server-bredr (btproxy), or
#                           /tmp/xeneva-bt-virt when XENEVA_BT_VIRT=1.
#                           XENEVA_BT_SERIAL overrides that path. If the
#                           socket is missing, QEMU still starts.
#                           Without --bt the guest has no Bluetooth socket.
#   XENEVA_BT_VIRT=1        With --bt, the second serial is a virtual headset
#                           (tests/bt/virt_headset), not BlueZ btproxy.
#                           hci0 stays up.
#   --no-boot-menu          Skip the EFI resolution menu and boot the default
#                           mode (NOMENU marker on the ESP). The menu stays on
#                           by default; headless, egl-headless and xr-demo
#                           runs always skip it.
#   --force-legacy-build    Reuse an existing initrd2.img instead of rebuilding it.
#   --install-deps          Install required host packages for this distro.
#   --initrd-size-mb=N      Override the auto-computed initrd2.img size.
#   --data-size-mb=N        Size of the persistent data.img virtio-blk disk
#                           (default 256). Created once and reused, so files
#                           written to /data survive across runs.
#   --no-data-disk          Don't attach the persistent data disk.
#   --force-data-disk       Recreate data.img, wiping persisted files.
#   --headless              Run QEMU with -display none, bounded by a timeout,
#                           instead of opening a GTK window. Ordinary builds
#                           stop at the interactive resolution menu; bleed
#                           boots the default resolution automatically. Bleed
#                           also trims compositor buffers.
#   --term [cmd args...]    Open the QEMU window with a framebuffer TTY (no
#                           compositor). Without extra args, init starts
#                           xesh.exe on /dev/console. With extra args, init
#                           runs the specified app. Guest flags like ping -6
#                           are collected; stop at the next host --option.
#                           e.g. --term ping -6 fec0::2
#   --test-musl             Build the musl clib guest and the rust hello
#                           guest and pack them as /musl.elf and /hello.elf.
#                           Without this flag both are left out of the image.
#                           --skip-build reuses existing guest binaries.
#   --iso[=PATH]            Package the assembled ESP (fat.img) as a UEFI
#                           El Torito bootable ISO instead of launching QEMU.
#                           Defaults to xeneva.iso at the repo root. Test it
#                           with: qemu-system-aarch64 -bios <firmware> -cdrom
#                           <iso> ... (same other flags as the -drive form).
#   -h, --help              Show this help and exit.
#
# Known gap: x86_64 (Boot/Kernel) has no QEMU boot path here yet.

ORIG_PWD="$PWD"
ORIG_ARGS=("$@")

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPT_DIR="$REPO_ROOT/Scripts/Linux"
USERSPACE_PROFILE_STAMP="$REPO_ROOT/Build/aarch64-userspace.profile"

source "$SCRIPT_DIR/lib/enviorment_variables.sh"
source "$SCRIPT_DIR/lib/functions.sh"

TOOLCHAIN=llvm
SKIP_BUILD=0
BUILD_USER_APPS=0
FORCE_LEGACY_BUILD=0
INSTALL_DEPS=0
HEADLESS=0
BLEED=0
DEBUG_ALLOC=0
SOAK=0
DIRECT_SCANOUT=0
UNIKERNEL=0
STATIC_COMPONENTS=${STATIC_COMPONENTS:-0}   # --static-components: virtio drivers linked into the kernel
OPENXR=0
XR_DEMO=0
XR_DEMO_MENU=0
EGL_HEADLESS=0
XR_HANDS=1
XR_CONTROLLERS=1
# The viewer is pinned to WiVRn's GPU, so its hand swapchains share the same
# Mesa device and can be enabled with the default hand input path. --axiss
XR_HAND_MESH=1
XR_RESOLUTION="1024x768"
XR_GAIN=20000
XR_SCALE="sharp"
XR_VNC=1
XR_TELNET=0
TERM_MODE=0
TERM_CMD=""
TEST_MUSL=0
INITRD_SIZE_MB=""
ISO=0
ISO_OUTPUT=""
TUI=0
NO_NETWORK=0
NO_AUDIO=0
NO_BOOT_MENU=0
NO_DOOM=0
BT_SERIAL="${XENEVA_BT_SERIAL:-}"
NO_BT=1
NO_NETSURF=0
VIRTIO_RNG=1
# virtio-serial-pci is the controller; virtserialport is the guest-visible
# endpoint the vendored virtio_console.c binds (1AF4:1043). The host end is
# a TCP socket rather than a pty or a pipe so a test can connect, exchange
# one round, and leave -- server=on/wait=off means QEMU never blocks on a
# client that has not arrived yet. On by default for the same reason
# VIRTIO_RNG is: the driver under test needs something to probe.
VIRTIO_SERIAL=1
VIRTIO_SERIAL_PORT="${VIRTIO_SERIAL_PORT:-43211}"
# The virtserialport endpoint is the clipboard wire: clipd holds the guest
# end, Tests/clip_bridge.py the host end. A switch of its own so the
# endpoint can be dropped while the controller stays for virtio_console to
# probe -- --no-virtio-serial still takes both.
CLIPBOARD=1
DATA_SIZE_MB=256
NO_DATA_DISK=0
FORCE_DATA_DISK=0
EXTRA_DISKS=()
LOG_ENABLED=1
LOG_DIR="${XENEVA_LOG_DIR:-}"
LOG_KEEP=5
LOG_FILE=""

abs_path() {
    case "$1" in
        "~") printf '%s\n' "$HOME" ;;
        "~/"*) printf '%s\n' "$HOME/${1#\~/}" ;;
        /*) printf '%s\n' "$1" ;;
        *) printf '%s\n' "$ORIG_PWD/$1" ;;
    esac
}

add_extra_disk() {
    local mode="$1" path="$2"
    if [ -z "$path" ]; then
        printf "${STY_RED}[%s]: --disk/--disk-ro needs a path.${STY_RST}\n" "$0"
        exit 1
    fi
    EXTRA_DISKS+=("$mode:$(abs_path "$path")")
}

disk_format_for() {
    local lower
    lower="$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')"
    case "$lower" in
        *.qcow2|*.qcow) echo qcow2 ;;
        *.vmdk) echo vmdk ;;
        *.vdi) echo vdi ;;
        *.vhdx) echo vhdx ;;
        *.vhd|*.vpc) echo vpc ;;
        *) echo raw ;;
    esac
}

rotate_logs() {
    local dir="$1" keep="$2" i excess
    local logs=()
    local had_nullglob=0
    shopt -q nullglob && had_nullglob=1
    shopt -s nullglob
    logs=("$dir"/run-*.log)
    [ "$had_nullglob" -eq 1 ] || shopt -u nullglob
    excess=$(( ${#logs[@]} - keep ))
    for (( i = 0; i < excess; i++ )); do
        rm -f -- "${logs[$i]}"
    done
}

start_logging() {
    [ "$LOG_ENABLED" -eq 1 ] || return 0
    local dir="${LOG_DIR:-$REPO_ROOT/Build/logs}"
    if ! mkdir -p "$dir" 2>/dev/null || [ ! -w "$dir" ]; then
        printf "${STY_YELLOW}[%s]: cannot write logs to %s; continuing without a log file.${STY_RST}\n" "$0" "$dir"
        return 0
    fi
    LOG_FILE="$dir/run-$(date +%Y%m%d-%H%M%S)-$$.log"
    {
        echo "=== XenevaOS build_and_run_qemu.sh ==="
        echo "Started : $(date '+%Y-%m-%d %H:%M:%S %z')"
        echo "Command : $0 ${ORIG_ARGS[*]}"
        echo "Cwd     : $ORIG_PWD"
        echo "Commit  : $(git -C "$REPO_ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
        echo "======================================"
    } > "$LOG_FILE"
    rotate_logs "$dir" "$LOG_KEEP"
    exec > >(trap '' INT TERM; exec 9>&-; exec tee -a "$LOG_FILE") 2>&1
    echo "[+] Logging this run to $LOG_FILE (keeping the last $LOG_KEEP)"
}

print_help(){
    printf "${STY_CYAN}"
    sed -n '3,/^# Known gap:/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    printf "${STY_RST}\n"
}

run_build_tui() {
    if [ ! -t 0 ] || [ ! -t 1 ]; then
        echo "build_and_run_qemu: --tui needs a terminal." >&2
        echo "Pass flags directly for the noninteractive flow." >&2
        exit 2
    fi

    local toolchain_choice="$TOOLCHAIN"
    local profile_choice="normal"
    [ "$BLEED" -eq 1 ] && profile_choice="bleed"
    local runmode_choice="gtk"
    if [ "$HEADLESS" -eq 1 ]; then
        runmode_choice="headless"
    elif [ "$EGL_HEADLESS" -eq 1 ]; then
        runmode_choice="egl-headless"
    elif [ "$TERM_MODE" -eq 1 ]; then
        runmode_choice="term"
    elif [ "$ISO" -eq 1 ]; then
        runmode_choice="iso"
    fi
    local userapps_choice="$BUILD_USER_APPS"
    local scanout_choice="$DIRECT_SCANOUT"
    local unikernel_choice="$UNIKERNEL"
    local soak_choice="$SOAK"
    local debugalloc_choice="$DEBUG_ALLOC"
    local network_on=1 audio_on=1 bootmenu_on=1 datadisk_on=1
    [ "$NO_NETWORK" -eq 1 ] && network_on=0
    [ "$NO_AUDIO" -eq 1 ] && audio_on=0
    [ "$NO_BOOT_MENU" -eq 1 ] && bootmenu_on=0
    [ "$NO_DATA_DISK" -eq 1 ] && datadisk_on=0
    local memory_choice="default"

    local items=(toolchain profile runmode userapps scanout unikernel soak debugalloc network audio bootmenu doom netsurf datadisk memory launch quit)
    local selected=0
    local tui_done=0

    tui_on_off() {
        if [ "$1" -eq 1 ]; then
            printf '\033[1;32mON \033[0m'
        else
            printf '\033[2;31moff\033[0m'
        fi
    }

    tui_cycle_runmode() {
        case "$runmode_choice" in
            gtk) runmode_choice="headless" ;;
            headless) runmode_choice="egl-headless" ;;
            egl-headless) runmode_choice="term" ;;
            term) runmode_choice="iso" ;;
            iso) runmode_choice="xr-demo" ;;
            xr-demo) runmode_choice="gtk" ;;
        esac
    }

    tui_cycle_memory() {
        case "$memory_choice" in
            default) memory_choice="384M" ;;
            384M) memory_choice="1024M" ;;
            1024M) memory_choice="2048M" ;;
            2048M) memory_choice="default" ;;
        esac
    }

    tui_reset_defaults() {
        toolchain_choice="llvm"
        profile_choice="normal"
        runmode_choice="gtk"
        userapps_choice=0
        scanout_choice=0
        unikernel_choice=0
        soak_choice=0
        debugalloc_choice=0
        network_on=1
        audio_on=1
        bootmenu_on=1
        datadisk_on=1
        NO_DOOM=0
        NO_NETSURF=0
        memory_choice="default"
    }

    tui_restore() {
        printf '\033[?25h\033[0m'
    }

    tui_apply_and_launch() {
        TOOLCHAIN="$toolchain_choice"
        BLEED=0
        [ "$profile_choice" = bleed ] && BLEED=1
        HEADLESS=0
        EGL_HEADLESS=0
        TERM_MODE=0
        ISO=0
        case "$runmode_choice" in
            headless) HEADLESS=1 ;;
            egl-headless) EGL_HEADLESS=1 ;;
            term) TERM_MODE=1 ;;
            iso) ISO=1 ;;
            xr-demo) OPENXR=1; XR_DEMO=1; XR_DEMO_MENU=1 ;;
        esac
        BUILD_USER_APPS="$userapps_choice"
        DIRECT_SCANOUT="$scanout_choice"
        UNIKERNEL="$unikernel_choice"
        SOAK="$soak_choice"
        DEBUG_ALLOC="$debugalloc_choice"
        NO_NETWORK=$((1 - network_on))
        NO_AUDIO=$((1 - audio_on))
        NO_BOOT_MENU=$((1 - bootmenu_on))
        NO_DATA_DISK=$((1 - datadisk_on))
        case "$memory_choice" in
            default) unset XENEVA_QEMU_MEMORY ;;
            *) export XENEVA_QEMU_MEMORY="$memory_choice" ;;
        esac
        trap - EXIT INT TERM
        tui_restore
        tui_done=1
    }

    tui_activate() {
        case "${items[$selected]}" in
            toolchain)
                [ "$toolchain_choice" = llvm ] && toolchain_choice="gcc" || toolchain_choice="llvm"
                ;;
            profile)
                [ "$profile_choice" = normal ] && profile_choice="bleed" || profile_choice="normal"
                ;;
            runmode) tui_cycle_runmode ;;
            userapps) userapps_choice=$((1 - userapps_choice)) ;;
            scanout) scanout_choice=$((1 - scanout_choice)) ;;
            unikernel) unikernel_choice=$((1 - unikernel_choice)) ;;
            soak) soak_choice=$((1 - soak_choice)) ;;
            debugalloc) debugalloc_choice=$((1 - debugalloc_choice)) ;;
            network) network_on=$((1 - network_on)) ;;
            audio) audio_on=$((1 - audio_on)) ;;
            doom) NO_DOOM=$((1 - NO_DOOM)) ;;
            netsurf) NO_NETSURF=$((1 - NO_NETSURF)) ;;
            datadisk) datadisk_on=$((1 - datadisk_on)) ;;
            bootmenu) bootmenu_on=$((1 - bootmenu_on)) ;;
            memory) tui_cycle_memory ;;
            launch) tui_apply_and_launch ;;
            quit)
                tui_restore
                exit 0
                ;;
        esac
    }

    tui_row() {
        local index="$1" label="$2" value="$3"
        if [ "$selected" -eq "$index" ]; then
            printf '\033[48;5;24m\033[38;5;231m│ \033[0m'
            printf '  %-24s ' "$label"
            printf '%s\n' "$value"
        else
            printf '  \033[2m│\033[0m '
            printf '  %-24s ' "$label"
            printf '%s\n' "$value"
        fi
    }

    tui_render() {
        local w=62
        local summary="${toolchain_choice}-${profile_choice} | ${runmode_choice} | mem ${memory_choice}"
        printf '\033[?25l'
        printf '\033[H\033[2J'
        printf '\033[1;36m┌%s┐\033[0m\n' "$(printf '%*s' "$w" | tr ' ' '─')"
        printf '\033[1;36m│\033[0m \033[1m%-*s\033[0m\033[1;36m│\033[0m\n' "$((w - 1))" \
            "XENEVA BUILD + RUN  -  qemu launcher"
        printf '\033[2m│\033[0m %-*s\033[2m│\033[0m\n' "$((w - 1))" "$summary"
        printf '\033[1;36m├%s┤\033[0m\n' "$(printf '%*s' "$w" | tr ' ' '─')"
        tui_row 0 "Toolchain" "$toolchain_choice"
        tui_row 1 "Build profile" "$profile_choice"
        tui_row 2 "Run mode" "$runmode_choice"
        tui_row 3 "Rebuild user apps" "$(tui_on_off "$userapps_choice")"
        tui_row 4 "Direct scanout" "$(tui_on_off "$scanout_choice")"
        tui_row 5 "Unikernel shell" "$(tui_on_off "$unikernel_choice")"
        tui_row 6 "Scheduler soak" "$(tui_on_off "$soak_choice")"
        tui_row 7 "Allocator debug" "$(tui_on_off "$debugalloc_choice")"
        tui_row 8 "Network stack" "$(tui_on_off "$network_on")"
        tui_row 9 "Audio daemon" "$(tui_on_off "$audio_on")"
        tui_row 10 "Boot menu" "$(tui_on_off "$bootmenu_on")"
        tui_row 11 "Doom addon" "$(tui_on_off $((1 - NO_DOOM)))"
        tui_row 12 "NetSurf browser" "$(tui_on_off $((1 - NO_NETSURF)))"
        tui_row 13 "Data disk (/data)" "$(tui_on_off "$datadisk_on")"
        tui_row 14 "Guest memory" "$memory_choice"
        tui_row 15 "Launch" "build + run"
        tui_row 16 "Quit" ""
        printf '\033[1;36m└%s┘\033[0m\n' "$(printf '%*s' "$w" | tr ' ' '─')"
        printf '\n  \033[2mIncompatible combos fail after launch with the usual errors.\033[0m\n'
        printf '  \033[1;33m↑↓\033[0m select  \033[1;33m⏎\033[0m change  \033[1;33mD\033[0m defaults  \033[1;33mQ\033[0m quit\n'
    }

    trap tui_restore EXIT INT TERM

    while [ "$tui_done" -eq 0 ]; do
        tui_render
        # EOF (closed stdin) must quit, not spin: an empty key would
        # otherwise match the activate branch on every failed read. --axiss
        IFS= read -rsn1 key || { tui_restore; exit 2; }
        if [ "$key" = $'\e' ]; then
            IFS= read -rsn2 -t 0.1 rest || true
            key+="$rest"
        fi
        case "$key" in
            $'\e[A'|k) selected=$(( (selected + ${#items[@]} - 1) % ${#items[@]} )) ;;
            $'\e[B'|j) selected=$(( (selected + 1) % ${#items[@]} )) ;;
            ' '|$'\n'|'') tui_activate ;;
            d|D) tui_reset_defaults ;;
            q|Q) tui_restore; exit 0 ;;
        esac
    done
    trap - EXIT INT TERM
}

while [ $# -gt 0 ]; do
    case "$1" in
        --llvm) TOOLCHAIN=llvm ;;
        --gcc) TOOLCHAIN=gcc ;;
        --skip-build) SKIP_BUILD=1 ;;
        --force-user-apps) BUILD_USER_APPS=1 ;;
		--bleed) BLEED=1 ;;
		--debug-alloc) DEBUG_ALLOC=1 ;;
		--soak) SOAK=1 ;;
		--direct-scanout) DIRECT_SCANOUT=1 ;;
		--unikernel) UNIKERNEL=1 ;;
		--static-components) STATIC_COMPONENTS=1 ;;
		--openxr) OPENXR=1 ;;
		-xr-demo|--xr-demo) OPENXR=1; XR_DEMO=1; XR_DEMO_MENU=1 ;;
		--xr-demo-defaults) OPENXR=1; XR_DEMO=1 ;;
		--xr-hands) XR_HANDS=1 ;;
		--xr-no-hands) XR_HANDS=0 ;;
		--xr-controllers) XR_CONTROLLERS=1 ;;
		--xr-no-controllers) XR_CONTROLLERS=0 ;;
		--xr-hand-mesh) XR_HAND_MESH=1 ;;
		--xr-no-hand-mesh) XR_HAND_MESH=0 ;;
		--xr-resolution=*) XR_RESOLUTION="${1#--xr-resolution=}" ;;
		--xr-gain=*) XR_GAIN="${1#--xr-gain=}" ;;
		--xr-scale=*) XR_SCALE="${1#--xr-scale=}" ;;
		--xr-vnc) XR_VNC=1 ;;
		--xr-no-vnc) XR_VNC=0 ;;
		--xr-telnet) XR_TELNET=1 ;;
		--xr-no-telnet) XR_TELNET=0 ;;
		--egl-headless) EGL_HEADLESS=1 ;;
        --force-legacy-build) FORCE_LEGACY_BUILD=1 ;;
        --install-deps) INSTALL_DEPS=1 ;;
        --headless) HEADLESS=1 ;;
        --term)
            TERM_MODE=1
            shift
            # Guest argv (ping -6, iptable -A, ...) may start with '-'.
            # Stop only at the next host option (--foo, -h, -xr-demo).
            # A lone "--" passes everything after it through verbatim,
            # for guest flags that look like host options
            # (e.g. --term -- netsurf --selftest http://example.com/).
            TERM_CMD=""
            while [ $# -gt 0 ]; do
                if [ "$1" = "--" ]; then
                    shift
                    while [ $# -gt 0 ]; do
                        if [ -n "$TERM_CMD" ]; then
                            TERM_CMD="$TERM_CMD $1"
                        else
                            TERM_CMD="$1"
                        fi
                        shift
                    done
                    break
                fi
                case "$1" in
                    --*|-h|-xr-demo) break ;;
                esac
                if [ -n "$TERM_CMD" ]; then
                    TERM_CMD="$TERM_CMD $1"
                else
                    TERM_CMD="$1"
                fi
                shift
            done
            continue
            ;;
        --test-musl) TEST_MUSL=1 ;;
        --initrd-size-mb=*) INITRD_SIZE_MB="${1#--initrd-size-mb=}" ;;
        --iso) ISO=1 ;;
        --iso=*) ISO=1; ISO_OUTPUT="${1#--iso=}" ;;
        --tui) TUI=1 ;;
        --no-network) NO_NETWORK=1 ;;
        --no-doom) NO_DOOM=1 ;;
        --no-netsurf) NO_NETSURF=1 ;;
        --no-audio) NO_AUDIO=1 ;;
        --no-boot-menu) NO_BOOT_MENU=1 ;;
        --bt) NO_BT=0 ;;
        --bt-serial) NO_BT=0; BT_SERIAL="/tmp/bt-server-bredr" ;;
        --bt-serial=*) NO_BT=0; BT_SERIAL="${1#--bt-serial=}" ;;
        --no-bt) NO_BT=1 ;;
        --virtio-rng) VIRTIO_RNG=1 ;;
        --no-virtio-rng) VIRTIO_RNG=0 ;;
        --virtio-serial) VIRTIO_SERIAL=1 ;;
        --no-virtio-serial) VIRTIO_SERIAL=0 ;;
        --virtio-serial-port=*) VIRTIO_SERIAL_PORT="${1#--virtio-serial-port=}" ;;
        --clipboard) CLIPBOARD=1 ;;
        --no-clipboard) CLIPBOARD=0 ;;
        --data-size-mb=*) DATA_SIZE_MB="${1#--data-size-mb=}" ;;
        --no-data-disk) NO_DATA_DISK=1 ;;
        --force-data-disk) FORCE_DATA_DISK=1 ;;
        --disk=*) add_extra_disk rw "${1#--disk=}" ;;
        --disk-ro=*) add_extra_disk ro "${1#--disk-ro=}" ;;
        --disk|--disk-ro)
            mode=rw
            [ "$1" = --disk-ro ] && mode=ro
            add_extra_disk "$mode" "${2:-}"
            [ $# -ge 2 ] && shift
            ;;
        --log-dir=*) LOG_DIR="${1#--log-dir=}" ;;
        --log-keep=*) LOG_KEEP="${1#--log-keep=}" ;;
        --no-log) LOG_ENABLED=0 ;;
        -h|--help) print_help; exit 0 ;;
        *)
            printf "${STY_RED}[%s]: Unknown option \"%s\".${STY_RST}\n" "$0" "$1"
            print_help
            exit 1
        ;;
    esac
    shift
done

[ -n "$ISO_OUTPUT" ] && ISO_OUTPUT="$(abs_path "$ISO_OUTPUT")"
[ -n "$LOG_DIR" ] && LOG_DIR="$(abs_path "$LOG_DIR")"
[ -n "$BT_SERIAL" ] && BT_SERIAL="$(abs_path "$BT_SERIAL")"
if [ -n "${XENEVA_QEMU_FIRMWARE:-}" ]; then
    XENEVA_QEMU_FIRMWARE="$(abs_path "$XENEVA_QEMU_FIRMWARE")"
fi
if [ -n "${XENEVA_XR_RUNTIME_JSON:-}" ]; then
    XENEVA_XR_RUNTIME_JSON="$(abs_path "$XENEVA_XR_RUNTIME_JSON")"
fi

case "$LOG_KEEP" in
    ''|*[!0-9]*|0)
        printf "${STY_RED}[%s]: --log-keep must be a positive whole number.${STY_RST}\n" "$0"
        exit 1
    ;;
esac

if [ "${#EXTRA_DISKS[@]}" -gt 0 ]; then
    seen_disks=" "
    for entry in "${EXTRA_DISKS[@]}"; do
        mode="${entry%%:*}"
        path="${entry#*:}"
        if [ ! -e "$path" ]; then
            printf "${STY_RED}[%s]: --disk: %s does not exist.${STY_RST}\n" "$0" "$path"
            exit 1
        fi
        if [ ! -f "$path" ] && [ ! -b "$path" ]; then
            printf "${STY_RED}[%s]: --disk: %s is not a regular file or block device.${STY_RST}\n" "$0" "$path"
            exit 1
        fi
        if [ ! -r "$path" ] || { [ "$mode" = rw ] && [ ! -w "$path" ]; }; then
            printf "${STY_RED}[%s]: --disk: no permission to open %s %s.${STY_RST}\n" \
                "$0" "$path" "$([ "$mode" = rw ] && echo read-write || echo read-only)"
            [ "$mode" = rw ] && printf "${STY_YELLOW}[%s]: Use --disk-ro=PATH if the guest only needs to read it.${STY_RST}\n" "$0"
            exit 1
        fi
        canon="$(readlink -f -- "$path")"
        for own in fat.img initrd2.img data.img; do
            if [ "$canon" = "$(readlink -f -- "$REPO_ROOT/$own")" ]; then
                printf "${STY_RED}[%s]: --disk: %s is the script's own %s; pick a different image.${STY_RST}\n" "$0" "$path" "$own"
                exit 1
            fi
        done
        case "$seen_disks" in
            *" $canon "*)
                printf "${STY_RED}[%s]: --disk: %s was given twice.${STY_RST}\n" "$0" "$path"
                exit 1
            ;;
        esac
        seen_disks="$seen_disks$canon "
    done
fi

# Interactive picker. Runs after flag parsing so CLI flags preselect menu
# entries, and falls through into the normal flow (including the XR demo
# exec below when run mode xr-demo is picked). --axiss
if [ "$TUI" -eq 1 ] && [ "$XR_DEMO_MENU" -eq 0 ]; then
    run_build_tui
fi

# Mutual exclusion for builders. Concurrent `make clean && make` runs in one
# tree interleave objects and silently link mixed binaries; rebuilding or
# repacking under a live guest corrupts its boot reads. The flock lives on
# an fd held for the whole run (build, pack, and the QEMU session itself),
# so a second build, relaunch, or TUI run fails here instead of corrupting
# there. flock state is kernel-held: no stale lock survives a killed
# process. XENEVA_BUILD_LOCKED skips re-acquiring across the xr-demo exec
# chain, which inherits the held fd. --axiss
if [ -z "${XENEVA_BUILD_LOCKED:-}" ]; then
    mkdir -p "$REPO_ROOT/Build"
    exec 9>"$REPO_ROOT/Build/.build.lock" || exit 1
    if ! flock -n 9; then
        printf "${STY_RED}[$0]: another build or run is holding the tree lock (Build/.build.lock).${STY_RST}\n"
        printf "${STY_YELLOW}[$0]: Finish or stop the other run first -- concurrent builds mix objects.${STY_RST}\n"
        exit 1
    fi
    export XENEVA_BUILD_LOCKED=1
fi

# A live guest owns these images: rebuilding or repacking under it corrupts
# its boot reads (a second QEMU would already fail on the image lock, but
# only after we mangled the files). Plain relaunches are serialized by the
# tree lock above plus QEMU's own lock. --axiss
if command -v fuser >/dev/null 2>&1; then
    live_guests=""
    for img in "$REPO_ROOT/fat.img" "$REPO_ROOT/initrd2.img" "$REPO_ROOT/data.img"; do
        if [ -f "$img" ]; then
            live_guests="$live_guests $(fuser "$img" 2>/dev/null | sed 's/^[^:]*://' || true)"
        fi
    done
    live_guests="$(printf '%s' "$live_guests" | tr -cs '0-9' ' ')"
    case "$live_guests" in
        ''|' ')
            ;;
        *)
            printf "${STY_RED}[$0]: QEMU guest(s) still running against these images (pid${live_guests}).${STY_RST}\n"
            printf "${STY_YELLOW}[$0]: Stop them first -- rebuilding or repacking under a live guest corrupts its boot.${STY_RST}\n"
            exit 1
            ;;
    esac
else
    printf "${STY_YELLOW}[$0]: fuser not found; cannot check for live guests before building.${STY_RST}\n"
fi

if [ "$XR_DEMO_MENU" -eq 1 ]; then
    if [ "$BLEED" -eq 1 ]; then
        XENEVA_XR_TUI_BUILD=bleed
    else
        XENEVA_XR_TUI_BUILD="$TOOLCHAIN"
    fi
    export XENEVA_XR_TUI_BUILD
    export XENEVA_XR_TUI_HANDS="$XR_HANDS"
    export XENEVA_XR_TUI_CONTROLLERS="$XR_CONTROLLERS"
    export XENEVA_XR_TUI_HAND_MESH="$XR_HAND_MESH"
    export XENEVA_XR_TUI_RESOLUTION="$XR_RESOLUTION"
    export XENEVA_XR_TUI_GAIN="$XR_GAIN"
    export XENEVA_XR_TUI_SCALE="$XR_SCALE"
    export XENEVA_XR_TUI_VNC="$XR_VNC"
    export XENEVA_XR_TUI_TELNET="$XR_TELNET"
    export XENEVA_XR_TUI_UNIKERNEL="$UNIKERNEL"
    export XENEVA_XR_TUI_DIRECT_SCANOUT="$DIRECT_SCANOUT"
    export XENEVA_XR_TUI_SOAK="$SOAK"
    exec "$SCRIPT_DIR/xr-demo.sh"
fi

start_logging

case "$XR_RESOLUTION" in
    1024x768|800x600|640x480|manual) ;;
    *)
        printf "${STY_RED}[$0]: --xr-resolution must be 1024x768, 800x600, 640x480, or manual.${STY_RST}\n"
        exit 1
    ;;
esac
case "$XR_GAIN" in
    ''|*[!0-9]*)
        printf "${STY_RED}[$0]: --xr-gain must be a positive integer.${STY_RST}\n"
        exit 1
    ;;
    0)
        printf "${STY_RED}[$0]: --xr-gain must be greater than zero.${STY_RST}\n"
        exit 1
    ;;
esac
case "$XR_SCALE" in
    sharp|smooth) ;;
    *)
        printf "${STY_RED}[$0]: --xr-scale must be sharp or smooth.${STY_RST}\n"
        exit 1
    ;;
esac

if [ "$STATIC_COMPONENTS" = 1 ] && [ "$TOOLCHAIN" != llvm ]; then
    printf "${STY_RED}[%s]: --static-components is supported only by the LLVM build.${STY_RST}\n" "$0"
    exit 1
fi

if [ "$ISO" -eq 1 ] && [ "${#EXTRA_DISKS[@]}" -gt 0 ]; then
    printf "${STY_YELLOW}[%s]: --disk is ignored with --iso (the ISO holds only the ESP).${STY_RST}\n" "$0"
fi

if [ "$BLEED" -eq 1 ]; then
    if [ "$TOOLCHAIN" != llvm ]; then
        printf "${STY_RED}[$0]: --bleed is supported only by the AArch64 LLVM build.${STY_RST}\n"
        exit 1
    fi
    if [ "$SKIP_BUILD" -eq 1 ]; then
        printf "${STY_RED}[$0]: --bleed cannot be combined with --skip-build.${STY_RST}\n"
        exit 1
    fi
    if [ "$FORCE_LEGACY_BUILD" -eq 1 ]; then
        printf "${STY_RED}[$0]: --bleed cannot be combined with --force-legacy-build.${STY_RST}\n"
        printf "${STY_YELLOW}[$0]: Bleed requires a freshly packed, profile-matched initrd.${STY_RST}\n"
        exit 1
    fi
    BUILD_USER_APPS=1
fi

if [ "$DIRECT_SCANOUT" -eq 1 ]; then
	if [ "$SKIP_BUILD" -eq 1 ]; then
		printf "${STY_RED}[$0]: --direct-scanout cannot be combined with --skip-build.${STY_RST}\n"
		exit 1
	fi
	if [ "$FORCE_LEGACY_BUILD" -eq 1 ]; then
		printf "${STY_RED}[$0]: --direct-scanout cannot be combined with --force-legacy-build.${STY_RST}\n"
		exit 1
	fi
	BUILD_USER_APPS=1
fi

if [ "$UNIKERNEL" -eq 1 ]; then
	if [ "$SKIP_BUILD" -eq 1 ]; then
		printf "${STY_RED}[$0]: --unikernel cannot be combined with --skip-build.${STY_RST}\n"
		exit 1
	fi
	if [ "$FORCE_LEGACY_BUILD" -eq 1 ]; then
		printf "${STY_RED}[$0]: --unikernel cannot be combined with --force-legacy-build.${STY_RST}\n"
		exit 1
	fi
	BUILD_USER_APPS=1
fi

if [ "$OPENXR" -eq 1 ]; then
	if [ "$SKIP_BUILD" -eq 1 ]; then
		printf "${STY_RED}[$0]: --openxr cannot be combined with --skip-build.${STY_RST}\n"
		exit 1
	fi
	if [ "$FORCE_LEGACY_BUILD" -eq 1 ]; then
		printf "${STY_RED}[$0]: --openxr cannot be combined with --force-legacy-build.${STY_RST}\n"
		printf "${STY_YELLOW}[$0]: OpenXR requires a freshly packed, profile-matched initrd.${STY_RST}\n"
		exit 1
	fi
	BUILD_USER_APPS=1
fi

if [ "$TEST_MUSL" -eq 1 ] && [ "$FORCE_LEGACY_BUILD" -eq 1 ]; then
	printf "${STY_RED}[$0]: --test-musl needs a freshly packed initrd; it cannot be combined with --force-legacy-build.${STY_RST}\n"
	exit 1
fi

if { [ "$NO_NETWORK" -eq 1 ] || [ "$NO_AUDIO" -eq 1 ] || [ "$NO_DOOM" -eq 1 ] || [ "$NO_NETSURF" -eq 1 ]; } && [ "$FORCE_LEGACY_BUILD" -eq 1 ]; then
	printf "${STY_RED}[$0]: --no-network/--no-audio/--no-doom/--no-netsurf need a freshly packed initrd; they cannot be combined with --force-legacy-build.${STY_RST}\n"
	exit 1
fi

if [ "$XR_DEMO" -eq 1 ] && { [ "$HEADLESS" -eq 1 ] || [ "$EGL_HEADLESS" -eq 1 ] ||
	[ "$TERM_MODE" -eq 1 ] || [ "$ISO" -eq 1 ]; }; then
	printf "${STY_RED}[$0]: -xr-demo/--xr-demo owns the display and cannot be combined with --headless, --egl-headless, --term, or --iso.${STY_RST}\n"
	exit 1
fi

if [ "$HEADLESS" -eq 1 ] && [ "$EGL_HEADLESS" -eq 1 ]; then
	printf "${STY_RED}[$0]: --headless and --egl-headless are mutually exclusive.${STY_RST}\n"
	exit 1
fi

requested_userspace_profile="${TOOLCHAIN}-normal"
if [ "$BLEED" -eq 1 ]; then
	requested_userspace_profile="${TOOLCHAIN}-bleed"
fi
if [ "$DIRECT_SCANOUT" -eq 1 ]; then
	requested_userspace_profile="${requested_userspace_profile}-direct-scanout"
fi
if [ "$UNIKERNEL" -eq 1 ]; then
	requested_userspace_profile="${requested_userspace_profile}-unikernel"
fi
if [ "$OPENXR" -eq 1 ]; then
	requested_userspace_profile="${requested_userspace_profile}-openxr"
fi

if [ -f "$USERSPACE_PROFILE_STAMP" ]; then
    previous_userspace_profile="$(<"$USERSPACE_PROFILE_STAMP")"
    if [ "$previous_userspace_profile" != "$requested_userspace_profile" ]; then
        if [ "$SKIP_BUILD" -eq 1 ]; then
            printf "${STY_RED}[$0]: --skip-build cannot reuse ${previous_userspace_profile} user space for ${requested_userspace_profile}.${STY_RST}\n"
            printf "${STY_YELLOW}[$0]: Rerun without --skip-build so user space can be cleaned and rebuilt.${STY_RST}\n"
            exit 1
        fi
        echo "[+] User-space profile changed: ${previous_userspace_profile} -> ${requested_userspace_profile}; forcing a clean rebuild."
        BUILD_USER_APPS=1
    fi
fi

if [ "$BUILD_USER_APPS" -eq 0 ] && [ "$FORCE_LEGACY_BUILD" -eq 0 ] && [ ! -f "$REPO_ROOT/Process/Init/init.exe" ]; then
    if [ "$SKIP_BUILD" -eq 1 ]; then
        printf "${STY_RED}[%s]: Process/Init/init.exe is missing, so --skip-build has nothing to pack.${STY_RST}\n" "$0"
        printf "${STY_YELLOW}[%s]: Rerun without --skip-build so user space can be built.${STY_RST}\n" "$0"
        exit 1
    fi
    echo "[+] Process/Init/init.exe not found; building user space automatically (same as --force-user-apps)."
    BUILD_USER_APPS=1
fi

if [ -n "$INITRD_SIZE_MB" ]; then
    case "$INITRD_SIZE_MB" in
        *[!0-9]*)
            printf "${STY_RED}[$0]: --initrd-size-mb must be a whole number.${STY_RST}\n"
            exit 1
        ;;
    esac
    if [ "$BLEED" -eq 1 ] && [ "$INITRD_SIZE_MB" -lt 48 ]; then
        printf "${STY_RED}[$0]: bleed initrds must be at least 48 MiB for the AArch64 initrd plus userspace.${STY_RST}\n"
        exit 1
    fi
fi

case "$DATA_SIZE_MB" in
    ''|*[!0-9]*|0)
        printf "${STY_RED}[$0]: --data-size-mb must be a positive whole number.${STY_RST}\n"
        exit 1
    ;;
esac
if [ "$DATA_SIZE_MB" -lt 64 ]; then
    printf "${STY_RED}[$0]: --data-size-mb must be at least 64; smaller volumes fall below FAT32's minimum cluster count and the guest will not mount them.${STY_RST}\n"
    exit 1
fi

cd "$REPO_ROOT"

if [ "$INSTALL_DEPS" -eq 1 ]; then
    source "$SCRIPT_DIR/lib/dist_determine.sh"
    for function in "${print_os_group_id_functions[@]}"; do
        "$function"
    done
    pause
    sudo_session
    trap sudo_stop EXIT INT TERM
    source "$SCRIPT_DIR/lib/distro/${OS_GROUP_ID}.sh" -d "--$TOOLCHAIN"
fi

# --- Preflight ---

echo "[+] Checking required host tools..."
MISSING_TOOLS=()
REQUIRED_TOOLS=(mkfs.vfat mcopy mmd)
if [ "$ISO" -eq 1 ]; then
    REQUIRED_TOOLS+=(xorriso)
else
    REQUIRED_TOOLS+=(qemu-system-aarch64)
fi
if [ "$XR_DEMO" -eq 1 ]; then
    REQUIRED_TOOLS+=(gdbus make)
fi
for tool in "${REQUIRED_TOOLS[@]}"; do
    command -v "$tool" >/dev/null 2>&1 || MISSING_TOOLS+=("$tool")
done
if [ "$TOOLCHAIN" == llvm ]; then
    command -v clang >/dev/null 2>&1 || MISSING_TOOLS+=("clang")
else
    command -v aarch64-linux-gnu-gcc >/dev/null 2>&1 || MISSING_TOOLS+=("aarch64-linux-gnu-gcc")
fi
if [ "${#MISSING_TOOLS[@]}" -gt 0 ]; then
    printf "${STY_RED}[$0]: Missing required tools: ${MISSING_TOOLS[*]}${STY_RST}\n"
    printf "${STY_YELLOW}[$0]: Run with --install-deps, or install them manually.${STY_RST}\n"
    exit 1
fi

XR_RT_JSON="${XENEVA_XR_RUNTIME_JSON:-/usr/share/openxr/1/openxr_wivrn.json}"
if [ "$XR_DEMO" -eq 1 ]; then
    if [ ! -f "$XR_RT_JSON" ]; then
        printf "${STY_RED}[$0]: OpenXR runtime manifest not found: $XR_RT_JSON${STY_RST}\n"
        printf "${STY_YELLOW}[$0]: Start/install WiVRn or set XENEVA_XR_RUNTIME_JSON.${STY_RST}\n"
        exit 1
    fi
    if [ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ]; then
        printf "${STY_RED}[$0]: XR demo requires a graphical session D-Bus (DBUS_SESSION_BUS_ADDRESS is unset).${STY_RST}\n"
        exit 1
    fi
    # org.qemu is a singleton on the session bus. Check before building or
    # rewriting fat.img: a second invocation must not disturb the live guest
    # and then mistake its D-Bus owner for the new QEMU becoming ready. --axiss
    if gdbus introspect --session --dest org.qemu --object-path /org/qemu/Display1 \
        >/dev/null 2>&1; then
        printf "${STY_RED}[$0]: org.qemu is already owned on the session bus; stop the existing XR/QEMU run first.${STY_RST}\n"
        exit 1
    fi
fi

if [ "$SKIP_BUILD" -eq 0 ] && [ "$TOOLCHAIN" != llvm ] && [ ! -d "$REPO_ROOT/gnu-efi" ]; then
    printf "${STY_RED}[$0]: gnu-efi not found at repo-root/gnu-efi.${STY_RST}\n"
    printf "${STY_YELLOW}[$0]: Clone it first: git clone https://github.com/vathpela/gnu-efi.git${STY_RST}\n"
    exit 1
fi

resolve_qemu_firmware(){
    local candidates=(
        "${XENEVA_QEMU_FIRMWARE:-}"
        "/usr/share/edk2/aarch64/QEMU_EFI.fd"
        "/usr/share/qemu-efi-aarch64/QEMU_EFI.fd"
    )
    for candidate in "${candidates[@]}"; do
        if [ -n "$candidate" ] && [ -f "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

if [ "$ISO" -eq 0 ]; then
    QEMU_FIRMWARE="$(resolve_qemu_firmware)" || {
        printf "${STY_RED}[$0]: Could not find AArch64 UEFI firmware (QEMU_EFI.fd).${STY_RST}\n"
        printf "${STY_YELLOW}[$0]: Install it (Arch: edk2-aarch64, Debian/Ubuntu: qemu-efi-aarch64),\n"
        printf "    or point XENEVA_QEMU_FIRMWARE at the .fd file.${STY_RST}\n"
        exit 1
    }
    echo "[+] Using QEMU firmware: $QEMU_FIRMWARE"
fi

# --- Build ---

if [ "$SKIP_BUILD" -eq 0 ]; then
    echo "[+] Building bootloader + kernel (+ apps if requested) with $TOOLCHAIN..."
	export STATIC_COMPONENTS BUILD_USER_APPS BLEED SOAK DEBUG_ALLOC DIRECT_SCANOUT UNIKERNEL OPENXR NO_NETWORK NO_AUDIO NO_DOOM NO_NETSURF
    pushd "$SCRIPT_DIR" >/dev/null
    if [ "$TOOLCHAIN" == llvm ]; then
        source ./lib/llvm.sh
    else
        source ./lib/gcc.sh
    fi
    popd >/dev/null

    # Build external drivers (requires kernel to be built first for KernelAA64.lib)
    if [ "$TOOLCHAIN" == llvm ]; then
        if [ "$STATIC_COMPONENTS" -eq 1 ]; then
            # unikernel profile: virtnet/virtgpu/virtsnd/virtblk are linked into the
            # kernel; make sure no stale .dll could shadow them in the image
            echo "[+] Static components: virtio drivers are part of the kernel image, not packed as .dll files."
            rm -f "$REPO_ROOT/Resources/resources/virtnet.dll" "$REPO_ROOT/Resources/resources/virtgpu.dll" \
                  "$REPO_ROOT/Resources/resources/virtsnd.dll" "$REPO_ROOT/Resources/resources/virtblk.dll"
        else
        echo "[+] Building external drivers..."
        ( cd "$REPO_ROOT/Drivers/Net/virtionet" && make clean && make )
        cp -f "$REPO_ROOT/Drivers/Net/virtionet/virtnet.dll" "$REPO_ROOT/Resources/resources/"
        ( cd "$REPO_ROOT/Drivers/GPU/virtiogpu" && make clean && make )
        cp -f "$REPO_ROOT/Drivers/GPU/virtiogpu/virtgpu.dll" "$REPO_ROOT/Resources/resources/"
        ( cd "$REPO_ROOT/Drivers/Sound/virtiosnd" && make clean && make llvm )
        cp -f "$REPO_ROOT/Drivers/Sound/virtiosnd/virtsnd.dll" "$REPO_ROOT/Resources/resources/"
        fi
        ( cd "$REPO_ROOT/Drivers/Net/wifi" && make clean && make )
        cp -f "$REPO_ROOT/Drivers/Net/wifi/wifi.dll" "$REPO_ROOT/Resources/resources/"
        echo "[+] External drivers built and deployed."
    fi

    # virtio-blk storage driver (virtblk.dll). Its Makefile is clang-only, so
    # build it whenever clang++ exists, independent of $TOOLCHAIN: without it
    # the persistent data disk attaches but is never mounted. --axiss
    if [ "$STATIC_COMPONENTS" -eq 1 ]; then
        echo "[+] virtblk is a static component; no virtblk.dll."
    elif command -v clang++ >/dev/null 2>&1; then
        echo "[+] Building virtio-blk storage driver..."
        ( cd "$REPO_ROOT/Drivers/virtioblk" && make clean && make )
        cp -f "$REPO_ROOT/Drivers/virtioblk/virtblk.dll" "$REPO_ROOT/Resources/resources/"
        rm -f "$REPO_ROOT/Drivers/virtioblk/virtblk.lib"
        echo "[+] virtblk.dll deployed."
    else
        echo "[+] clang++ not found; skipping virtblk.dll (data disk will attach but stay unmounted)."
    fi

    if [ "$BUILD_USER_APPS" -eq 1 ]; then
        mkdir -p "$(dirname "$USERSPACE_PROFILE_STAMP")"
        printf '%s\n' "$requested_userspace_profile" > "$USERSPACE_PROFILE_STAMP"
    fi
    if [ "$TERM_MODE" -eq 1 ]; then
        term_flags="-D__XENEVA_TERM__"
        if [ "$BLEED" -eq 1 ]; then
            term_flags="-D__XENEVA_BLEED__ -D__XENEVA_TERM__"
        fi
        echo "[+] Rebuilding init.exe for framebuffer TTY..."
        ( cd "$REPO_ROOT/Process/Init" && make clean && make BLEED_FLAGS="$term_flags" llvm )
        cp -f "$REPO_ROOT/Process/Init/init.exe" "$REPO_ROOT/Resources/resources/"
        if [ -n "$TERM_CMD" ]; then
            echo "[+] Writing /shell.cnf -> \"$TERM_CMD\""
            printf '%s\n' "$TERM_CMD" > "$REPO_ROOT/Resources/resources/shell.cnf"
        else
            echo "[+] No command specified, init will launch xesh.exe"
            rm -f "$REPO_ROOT/Resources/resources/shell.cnf"
            ( cd "$REPO_ROOT/Process/XEShell" && make clean && make llvm )
            cp -f "$REPO_ROOT/Process/XEShell/xesh.exe" "$REPO_ROOT/Resources/resources/"
        fi
    fi
else
    echo "[+] --skip-build passed, reusing existing build artifacts."
fi

if [ "$TEST_MUSL" -eq 1 ]; then
    guest_dir="$REPO_ROOT/Tests/linux-musl"
    rust_elf="$guest_dir/rust-hello/target/aarch64-xeneva/release/rust-hello"
    if [ "$SKIP_BUILD" -eq 0 ]; then
        echo "[+] Building musl and rust guests..."
        ( cd "$guest_dir" && make musl rust )
    else
        if [ -f "$rust_elf" ]; then
            cp -f "$rust_elf" "$REPO_ROOT/Resources/resources/hello.elf"
        fi
        if [ -f "$guest_dir/musl.elf" ]; then
            cp -f "$guest_dir/musl.elf" "$REPO_ROOT/Resources/resources/musl.elf"
        fi
        if [ ! -f "$REPO_ROOT/Resources/resources/hello.elf" ] || [ ! -f "$REPO_ROOT/Resources/resources/musl.elf" ]; then
            printf "${STY_RED}[$0]: --test-musl found no guest elf. Run without --skip-build.${STY_RST}\n"
            exit 1
        fi
        echo "[+] --skip-build: packing hello.elf and musl.elf."
    fi
fi

for artifact in "BootAA64/Build/EFI/BOOT/BOOTAA64.efi" "KernelAA64/KernelAA64.exe"; do
    if [ ! -f "$REPO_ROOT/$artifact" ]; then
        printf "${STY_RED}[$0]: Expected build artifact missing: $artifact${STY_RST}\n"
        printf "${STY_YELLOW}[$0]: Run without --skip-build, or build it manually first.${STY_RST}\n"
        exit 1
    fi
done

# --- Assemble initrd2.img ---

if [ "$FORCE_LEGACY_BUILD" -eq 0 ]; then
    if [ -n "$INITRD_SIZE_MB" ]; then
        initrd_size_mb="$INITRD_SIZE_MB"
    elif [ "$BLEED" -eq 1 ]; then
        initrd_size_mb=48
    else
        resources_mb=$(du -sm Resources/resources | cut -f1)
        computed_mb=$(( (resources_mb * 3 + 1) / 2 ))  # ceil(resources_mb * 1.5)
        initrd_size_mb=$(( computed_mb > 96 ? computed_mb : 96 ))
    fi
    echo "[+] Creating ${initrd_size_mb}MB FAT32 initrd2.img and packing resources..."
    dd if=/dev/zero of=initrd2.img bs=1M count="$initrd_size_mb"
    mkfs.vfat -F 32 initrd2.img
    # Microkernel rule: a removed component stays out of the image even if
    # a stale binary lingers in Resources/ from an earlier full build. --axiss
    pack_excluded() {
        case "$1" in
            MUSIC|ARCH_X64|snd.wav|RoLight.ttf|RoLiIta.ttf|RoThin.ttf|corbel.ttf)
                [ "$BLEED" -eq 1 ] && return 0 || return 1 ;;
            netmngr.exe|route.exe|iptable.exe|ping.exe|udpecho.exe|dig.exe|nslook.exe|telnet.exe|finger.exe|gopher.exe|ntpd.exe|curl.exe|wifictl.exe)
                [ "$NO_NETWORK" -eq 1 ] && return 0 || return 1 ;;
            doom.exe|doom2.wad)
                [ "$NO_DOOM" -eq 1 ] && return 0 || return 1 ;;
            netsurf.exe)
                [ "$NO_NETSURF" -eq 1 ] && return 0 || return 1 ;;
            hello.elf|musl.elf)
                [ "$TEST_MUSL" -eq 1 ] && return 1 || return 0 ;;
            deoaud.exe|audplr.exe)
                [ "$NO_AUDIO" -eq 1 ] && return 0 || return 1 ;;
        esac
        return 1
    }
    if [ "$BLEED" -eq 1 ]; then
        echo "[bleed] Trimming fonts and nonessential payloads; keeping network, audio, and glass."
    fi
    if [ "$NO_NETWORK" -eq 1 ]; then
        echo "[+] Network userspace excluded from the image (--no-network)."
    fi
    if [ "$NO_AUDIO" -eq 1 ]; then
        echo "[+] Audio userspace excluded from the image (--no-audio)."
    fi
    if [ "$TEST_MUSL" -eq 1 ]; then
        echo "[+] hello.elf and musl.elf added to the image (--test-musl)."
    fi
    for resource in Resources/resources/*; do
        if pack_excluded "$(basename "$resource")"; then
            continue
        fi
        mcopy -o -s -i initrd2.img "$resource" ::/
    done
    mcopy -o -i initrd2.img Process/Init/init.exe ::/init.exe
else
    if [ ! -f initrd2.img ]; then
        printf "${STY_RED}[%s]: --force-legacy-build needs an existing initrd2.img, and there is none in %s.${STY_RST}\n" "$0" "$REPO_ROOT"
        exit 1
    fi
    echo "[+] Found pre-built initrd2.img, skipping manual creation."
    echo "    (Omit --force-legacy-build to rebuild it.)"
fi

# --- Assemble data.img (persistent virtio-blk disk) ---
# Unlike fat.img/initrd2.img above, this image is created once and reused:
# files the guest writes to /data survive across runs. --axiss
if [ "$NO_DATA_DISK" -eq 0 ]; then
    if [ ! -f data.img ] || [ "$FORCE_DATA_DISK" -eq 1 ]; then
        echo "[+] Creating ${DATA_SIZE_MB}MB FAT32 data.img (persistent guest disk)..."
        dd if=/dev/zero of=data.img bs=1M count="$DATA_SIZE_MB"
        mkfs.vfat -F 32 -n XENEVADATA data.img
        echo "[+] data.img ready; guest mounts it at /data (label XENEVADATA)."
    else
        echo "[+] Reusing existing data.img (persistent). Pass --force-data-disk to wipe it."
    fi
else
    echo "[+] Persistent data disk omitted (--no-data-disk)."
fi

# --- Assemble fat.img (ESP) ---

echo "[+] Creating 512MB FAT32 image..."
dd if=/dev/zero of=fat.img bs=1M count=512
mkfs.vfat fat.img

echo "[+] Copying EFI Bootloader and Kernel via mtools..."
mmd -i fat.img ::/EFI
mmd -i fat.img ::/EFI/BOOT
mmd -i fat.img ::/EFI/XENEVA

mcopy -o -i fat.img BootAA64/Build/EFI/BOOT/BOOTAA64.efi ::/EFI/BOOT/BOOTAA64.EFI
mcopy -o -i fat.img KernelAA64/KernelAA64.exe ::/EFI/XENEVA/xnkrnl.exe
mcopy -o -i fat.img initrd2.img ::/initrd2.img

# Boot-menu marker: interactive runs honor --no-boot-menu (the menu is on by
# default). Headless, egl-headless and xr-demo always skip -- nothing can
# answer there, and the XR flow drives the menu over the monitor socket.
# The ISO inherits the marker inside efiboot.img. --axiss
if [ "$XR_DEMO" -eq 0 ] && { [ "$NO_BOOT_MENU" -eq 1 ] || [ "$HEADLESS" -eq 1 ] || [ "$EGL_HEADLESS" -eq 1 ]; }; then
    : > /tmp/xeneva-nomenu-marker
    mcopy -o -i fat.img /tmp/xeneva-nomenu-marker ::/NOMENU
    rm -f /tmp/xeneva-nomenu-marker
    echo "[+] Boot menu skipped (NOMENU marker)."
fi

# --- Package as a bootable ISO instead of launching QEMU ---

if [ "$ISO" -eq 1 ]; then
    iso_output="${ISO_OUTPUT:-$REPO_ROOT/xeneva.iso}"
    iso_staging="$(mktemp -d)"
    trap 'rm -rf "$iso_staging"' EXIT

    # fat.img *is* the ESP: it already has /EFI/BOOT/BOOTAA64.EFI at the path
    # UEFI firmware looks for by default, so it doubles as the El Torito
    # "no emulation" EFI boot image -- no separate boot loader stage needed.
    cp -f fat.img "$iso_staging/efiboot.img"

    # Some UEFI firmware (mainly real hardware, not QEMU/OVMF) reads
    # \EFI\BOOT\BOOTAA64.EFI straight off the ISO9660 filesystem instead of
    # loading the El Torito image above -- mirror the same tree so that path
    # also finds xnkrnl.exe/initrd2.img via the loader's usual relative
    # lookup, not just a boot stub that immediately fails to find them --axiss
    mkdir -p "$iso_staging/EFI/BOOT" "$iso_staging/EFI/XENEVA"
    cp -f BootAA64/Build/EFI/BOOT/BOOTAA64.efi "$iso_staging/EFI/BOOT/BOOTAA64.EFI"
    cp -f KernelAA64/KernelAA64.exe "$iso_staging/EFI/XENEVA/xnkrnl.exe"
    cp -f initrd2.img "$iso_staging/initrd2.img"

    echo "[+] Writing bootable ISO to $iso_output ..."
    xorriso -as mkisofs \
        -V "XENEVAOS" \
        -o "$iso_output" \
        -eltorito-alt-boot \
        -e efiboot.img \
        -no-emul-boot \
        -isohybrid-gpt-basdat \
        "$iso_staging"

    rm -rf "$iso_staging"
    trap - EXIT

    echo "[+] ISO ready: $iso_output"
    echo "[+] Test it with:"
    echo "    qemu-system-aarch64 -machine virt,gic-version=2,highmem=off -cpu cortex-a72 -m 1024M \\"
    echo "        -bios <path-to-QEMU_EFI.fd> -cdrom \"$iso_output\" -serial stdio"
    exit 0
fi

# --- Launch QEMU ---

if [ "$BLEED" -eq 1 ]; then
    qemu_memory="${XENEVA_QEMU_MEMORY:-384M}"
else
    qemu_memory="${XENEVA_QEMU_MEMORY:-1024M}"
fi
echo "[+] Image ready! Booting QEMU..."
echo "[+] Guest memory: $qemu_memory"

# A QEMU built without PulseAudio only exposes 'none'/'wav'. Asking it for
# 'pa' aborts the emulator before the guest ever boots, so ask the binary
# which backends it actually has and fall back to a silent one. The
# virtio-sound-pci device itself is still added, so the guest sees the same
# audio hardware either way. --axiss
QEMU_AUDIO_BACKEND="pa"
if ! qemu-system-aarch64 -audiodev help 2>&1 | grep -qw 'pa'; then
    QEMU_AUDIO_BACKEND="none"
    echo "[+] Audio: this QEMU has no 'pa' backend, using 'none' (silent guest sound)."
fi

NET_DEVICE="virtio-net-pci,netdev=net0"
case "${NETSRV:-0}" in
    0|'') ;;
    *) NET_DEVICE="$NET_DEVICE,disable-legacy=on" ;;
esac

QEMU_ARGS=(
    -machine virt,gic-version=2,highmem=off
    -cpu cortex-a72
    -m "$qemu_memory"
    -bios "$QEMU_FIRMWARE"
    # explicit modern-only virtio-blk-pci (disable-legacy=on) instead of the
    # if=virtio shorthand, so it always shows up at PCI ID 1af4:1042 -- the
    # kernel's virtio-blk driver matches that ID, not the transitional
    # 1af4:1001 the shorthand defaults to --axiss
    -drive file=fat.img,format=raw,if=none,id=blk0
    -device virtio-blk-pci,drive=blk0,disable-legacy=on
    -netdev user,id=net0,ipv4=on,net=10.0.2.0/24,host=10.0.2.2,dhcpstart=10.0.2.15,dns=10.0.2.3,ipv6=on,ipv6-net=fec0::/64,ipv6-host=fec0::2
    -device "$NET_DEVICE"     # NETSRV=1: modern-only so it is 1af4:1041 (the hwdev allowlist id)
    -object filter-dump,id=netdump,netdev=net0,file=/tmp/xeneva-net.pcap,queue=all
    -monitor unix:/tmp/xeneva-mon.sock,server,nowait
    # Register virtio-gpu first so QEMU's VNC server follows the display the
    # Xeneva compositor drives after boot. ramfb remains as a fallback GOP.
    -device virtio-gpu-pci,disable-legacy=on,id=gpu0
    -device ramfb,id=ramfb
    -device virtio-keyboard-pci
    -device virtio-tablet-pci
    -device qemu-xhci
    -device usb-kbd
    -serial stdio
)

# virtio-rng-pci gives the DCL virtio bus a real entropy device to match
# against the virtio_rng driver registered by the loaded .ko. --axiss
if [ "$VIRTIO_RNG" -eq 1 ]; then
    QEMU_ARGS+=(-device virtio-rng-pci,disable-legacy=on,id=rng0)
fi

# virtio-serial-pci is the controller the vendored virtio_console.c probes
# (Vendored/drivers/char/virtio_console.c); virtserialport is the single
# endpoint on it, and its name= is what the host side addresses a port by.
#
# disable-legacy=on for the same reason virtio-blk-pci above carries it: the
# transitional default reports PCI ID 1AF4:1003, and DCL's scan matches the
# modern 1AF4:1043 (0x1040 + VIRTIO_ID_CONSOLE), the way virtio_rng_detect()
# matches 1AF4:1044.
#
# The host end is a TCP socket with wait=off so QEMU starts whether or not a
# client has connected; a test that wants the guest's probe to see
# host_connected=1 connects to it first. VIRTIO_SERIAL_PORT (or
# --virtio-serial-port=N) moves it when 43211 is taken.
# --no-clipboard keeps the controller and drops the socket plus the
# endpoint, which only the clipboard has a user for. --axiss
if [ "$VIRTIO_SERIAL" -eq 1 ]; then
    QEMU_ARGS+=(-device virtio-serial-pci,disable-legacy=on,id=virtio-serial0)
    if [ "$CLIPBOARD" -eq 1 ]; then
        QEMU_ARGS+=(-chardev socket,id=vsp0,host=127.0.0.1,port="$VIRTIO_SERIAL_PORT",server=on,wait=off)
        QEMU_ARGS+=(-device virtserialport,chardev=vsp0,name=org.xeneva.test)
        echo "[+] virtio-serial: org.xeneva.test listening on 127.0.0.1:$VIRTIO_SERIAL_PORT"
    else
        echo "[+] virtio-serial: controller only, clipboard port omitted (--no-clipboard)"
    fi
else
    echo "[+] virtio-serial omitted (--no-virtio-serial)"
fi

# Second virtio-blk drive: the persistent guest data disk (mounted at /data by
# virtblk.dll). writethrough so host-side data.img is authoritative even
# though the guest has no block-cache flush path yet. Never recreated
# implicitly -- only by --force-data-disk. --axiss
if [ "$NO_DATA_DISK" -eq 0 ]; then
    if [ ! -f data.img ]; then
        printf "${STY_RED}[$0]: data.img missing and --no-data-disk not given; this should not happen.${STY_RST}\n"
        exit 1
    fi
    QEMU_ARGS+=(-drive file=data.img,format=raw,if=none,id=blk1,cache=writethrough)
    QEMU_ARGS+=(-device virtio-blk-pci,drive=blk1,disable-legacy=on)
    echo "[+] Persistent data disk attached (data.img -> guest /data)."
fi

disk_index=0
for entry in "${EXTRA_DISKS[@]}"; do
    mode="${entry%%:*}"
    path="${entry#*:}"
    fmt="$(disk_format_for "$path")"
    drive_opts="file=${path//,/,,},format=$fmt,if=none,id=xdisk$disk_index"
    if [ "$mode" = ro ]; then
        drive_opts="$drive_opts,readonly=on"
    else
        drive_opts="$drive_opts,cache=writethrough"
    fi
    QEMU_ARGS+=(-drive "$drive_opts")
    QEMU_ARGS+=(-device "virtio-blk-pci,drive=xdisk$disk_index,disable-legacy=on")
    echo "[+] Extra disk $disk_index: $path ($fmt, $([ "$mode" = ro ] && echo read-only || echo read-write))"
    disk_index=$((disk_index + 1))
done

# virtio-sound-pci (vendor 1AF4/device 1059) matches audrv.cnf's
# [04,03]/virtsnd.dll class entry (with [6900,4185] vendor fallback)
# and the virtsnd driver's AuPCIEScanClass probe. Skipped under --no-audio
# so the guest never enumerates a sound card it was told not to have. --axiss
if [ "$NO_AUDIO" -eq 1 ]; then
    echo "[+] Audio: virtio-sound-pci omitted (--no-audio)"
else
    QEMU_ARGS+=(-audiodev "$QEMU_AUDIO_BACKEND",id=snd0)
    QEMU_ARGS+=(-device virtio-sound-pci,audiodev=snd0,disable-legacy=on)
fi
bt_power_off_for_proxy() {
    local i prop
    command -v busctl >/dev/null 2>&1 || return 0
    for i in 1 2 3 4 5 6 7 8 9 10; do
        busctl call org.bluez /org/bluez/hci0 org.freedesktop.DBus.Properties \
            Set ssv org.bluez.Adapter1 Powered b false >/dev/null 2>&1 || true
        prop=$(busctl get-property org.bluez /org/bluez/hci0 org.bluez.Adapter1 Powered 2>/dev/null || true)
        case "$prop" in
            "b false") return 0 ;;
        esac
        sleep 0.2
    done
    echo "[+] Bluetooth: adapter still powered; btproxy may report 'Device or resource busy'"
}

if [ "$NO_BT" -eq 0 ] && [ -z "$BT_SERIAL" ]; then
    if [ "${XENEVA_BT_VIRT:-0}" = "1" ]; then
        BT_SERIAL="/tmp/xeneva-bt-virt"
    else
        BT_SERIAL="/tmp/bt-server-bredr"
    fi
fi
if [ "$NO_BT" -eq 1 ]; then
    # UART1 stays present so a guest probe of 0x09040000 does not fault.
    # Nothing is connected to it.
    QEMU_ARGS+=(-serial null)
    echo "[+] Bluetooth: off (pass --bt for /tmp/bt-server-bredr)"
elif [ -S "$BT_SERIAL" ]; then
    QEMU_ARGS+=(-serial "unix:$BT_SERIAL")
    if [ "${XENEVA_BT_VIRT:-0}" = "1" ]; then
        echo "[+] Bluetooth: virtual headset at $BT_SERIAL (hci0 left up)"
    else
        bt_power_off_for_proxy
        echo "[+] Bluetooth: second serial -> $BT_SERIAL (BlueZ via btproxy)"
    fi
else
    # Missing socket must not stop the VM. Same UART sink as --no-bt.
    QEMU_ARGS+=(-serial null)
    echo "[+] Bluetooth: no $BT_SERIAL, continuing without it"
fi
if [ "$XR_DEMO" -eq 1 ]; then
	# Core XR transport: D-Bus exports Console_0 (gpu0) to the viewer, using either
	# CPU scanout with VNC or EGL DMA-BUF scanout. The Unix monitor handles
	# boot/menu control. Optional services are independent. --axiss
    echo "[+] Building host XR tools..."
    make -C "$REPO_ROOT/Tools/xeneva-xr-view"
    make -C "$REPO_ROOT/Tools/xeneva-xr-client"
    XR_MON_SOCK="${XENEVA_XR_MONITOR_SOCKET:-/tmp/qemu-mon.sock}"
    XR_LOG="${XENEVA_XR_QEMU_LOG:-/tmp/xeneva-xr-demo-qemu.log}"
    XR_VIEW_LOG="${XENEVA_XR_VIEW_LOG:-/tmp/xeneva-xr-demo-view.log}"
    rm -f "$XR_MON_SOCK"
    if [ "$XR_VNC" -eq 1 ]; then
        # QEMU rejects VNC whenever a GL display context exists. Use its
        # CPU-backed D-Bus scanout so VNC and the XR capture listener can
        # consume the same console concurrently. --axiss
        QEMU_ARGS+=(-display dbus,gl=off -vnc 127.0.0.1:0)
        XR_DISPLAY_DESC="D-Bus CPU scanout + VNC localhost:0"
        XR_QEMU_CONSOLE=0
    else
        QEMU_ARGS+=(-display egl-headless -display dbus,gl=on)
        XR_DISPLAY_DESC="egl-headless + D-Bus DMA-BUF"
        XR_QEMU_CONSOLE=0
    fi
    QEMU_ARGS+=(-monitor "unix:$XR_MON_SOCK,server,nowait")
    if [ "$XR_TELNET" -eq 1 ]; then
        QEMU_ARGS+=(-monitor telnet:127.0.0.1:4444,server,nowait)
    fi
    echo "[+] XR core: $XR_DISPLAY_DESC + monitor $XR_MON_SOCK (log $XR_LOG)"
    qemu-system-aarch64 "${QEMU_ARGS[@]}" >"$XR_LOG" 2>&1 &
    XR_QEMU_PID=$!
    XR_VIEW_PID=""
    xr_demo_cleanup() {
        trap - EXIT INT TERM
        if [ -n "$XR_VIEW_PID" ] && kill -0 "$XR_VIEW_PID" 2>/dev/null; then
            kill "$XR_VIEW_PID" 2>/dev/null || true
            wait "$XR_VIEW_PID" 2>/dev/null || true
        fi
        if kill -0 "$XR_QEMU_PID" 2>/dev/null; then
            kill "$XR_QEMU_PID" 2>/dev/null || true
            wait "$XR_QEMU_PID" 2>/dev/null || true
        fi
        rm -f "$XR_MON_SOCK"
    }
    xr_demo_signal() {
        local exit_code="$1"
        xr_demo_cleanup
        exit "$exit_code"
    }
    trap xr_demo_cleanup EXIT
    trap 'xr_demo_signal 130' INT
    trap 'xr_demo_signal 143' TERM
    echo "[+] QEMU pid $XR_QEMU_PID."
    if [ "$XR_RESOLUTION" = manual ]; then
        echo "[+] Resolution is manual; use the monitor client or optional VNC service."
    else
        echo "[+] Boot resolution: $XR_RESOLUTION (automatic)."
    fi
    echo "[+] XR demo guest is booting. Starting the HMD viewer..."
    # The viewer must register after QEMU owns its bus name; too early and
    # RegisterListener fails while QEMU is still starting. --axiss
    XR_DBUS_READY=0
    for _ in $(seq 1 30); do
        if gdbus introspect --session --dest org.qemu --object-path /org/qemu/Display1 \
            >/dev/null 2>&1; then
            XR_DBUS_READY=1
            break
        fi
        if ! kill -0 "$XR_QEMU_PID" 2>/dev/null; then
            printf "${STY_RED}[$0]: QEMU exited before its XR display became ready; see $XR_LOG.${STY_RST}\n"
            exit 1
        fi
        sleep 2
    done
    if [ "$XR_DBUS_READY" -ne 1 ]; then
        printf "${STY_RED}[$0]: timed out waiting for QEMU's XR D-Bus display; see $XR_LOG.${STY_RST}\n"
        exit 1
    fi

    if [ "$XR_RESOLUTION" != manual ]; then
        # edk2 exposes 640x480, 800x600, 1024x768 in that order. Give the
        # firmware menu time to install its keyboard wait, then select it
        # through the always-on Unix monitor rather than requiring VNC. --axiss
        sleep "${XENEVA_XR_BOOT_DELAY:-5}"
        case "$XR_RESOLUTION" in
            1024x768) XR_BOOT_DOWNS=2 ;;
            800x600) XR_BOOT_DOWNS=1 ;;
            640x480) XR_BOOT_DOWNS=0 ;;
        esac
        for _ in $(seq 1 "$XR_BOOT_DOWNS"); do
            "$REPO_ROOT/Tools/xeneva-xr-client/xeneva-xr-client" \
                --monitor "$XR_MON_SOCK" --exec "sendkey down" >/dev/null
        done
        "$REPO_ROOT/Tools/xeneva-xr-client/xeneva-xr-client" \
            --monitor "$XR_MON_SOCK" --exec "sendkey ret" >/dev/null
        echo "[+] Selected $XR_RESOLUTION in the guest boot menu."
    fi

    XR_VIEW_ARGS=(--egl --mono "--gain" "$XR_GAIN" --filter)
    if [ "$XR_SCALE" = smooth ]; then
        XR_VIEW_ARGS+=(linear)
    else
        XR_VIEW_ARGS+=(nearest)
    fi
    if [ "$XR_HANDS" -eq 1 ]; then
        XR_VIEW_ARGS+=(--hands)
    fi
    if [ "$XR_CONTROLLERS" -eq 1 ]; then
        XR_VIEW_ARGS+=(--controllers)
    fi
    if [ "$XR_HAND_MESH" -eq 1 ]; then
        if [ "$XR_HANDS" -eq 0 ]; then
            XR_VIEW_ARGS+=(--hand-mesh)
        fi
    elif [ "$XR_HANDS" -eq 1 ]; then
        XR_VIEW_ARGS+=(--no-hand-mesh)
    fi
    # WiVRn owns swapchains on the discrete GPU on this host. Keep the viewer
    # on that same Mesa device; cross-GPU GL texture names are invalid. Allow
    # both selections to be overridden for other hosts. --axiss
    DRI_PRIME="${XENEVA_XR_DRI_PRIME:-1}" \
    LIBGL_ALWAYS_SOFTWARE="${XENEVA_XR_SOFTWARE_GL:-false}" \
        XR_RUNTIME_JSON="$XR_RT_JSON" XENEVA_QEMU_CONSOLE="$XR_QEMU_CONSOLE" \
        "$REPO_ROOT/Tools/xeneva-xr-view/xeneva-xr-view" "${XR_VIEW_ARGS[@]}" \
        >"$XR_VIEW_LOG" 2>&1 &
    XR_VIEW_PID=$!
    echo "[+] Viewer pid $XR_VIEW_PID: ${XR_VIEW_ARGS[*]} (log $XR_VIEW_LOG)."
    # A child process cannot export into your shell, so take this with you. --axiss
    printf 'export XR_RUNTIME_JSON="%s"\n' "$XR_RT_JSON" > /tmp/xeneva-xr-demo.env
    printf 'export XENEVA_XR_VIEW_ARGS="%s"\n' "${XR_VIEW_ARGS[*]}" >> /tmp/xeneva-xr-demo.env
    echo "[+] Next:"
    echo "    source /tmp/xeneva-xr-demo.env"
    echo "    Tools/xeneva-xr-client/xeneva-xr-client                     # monitor REPL"
    if [ "$XR_VNC" -eq 1 ]; then
        echo "    gvncviewer localhost:0                                      # optional VNC service"
    fi
    if [ "$XR_TELNET" -eq 1 ]; then
        echo "    telnet 127.0.0.1 4444                                      # optional HMP service"
    fi
    echo "[+] XR demo is live. Press Ctrl-C to stop viewer and QEMU."
    while kill -0 "$XR_QEMU_PID" 2>/dev/null && kill -0 "$XR_VIEW_PID" 2>/dev/null; do
        sleep 1
    done
    child_status=0
    if ! kill -0 "$XR_QEMU_PID" 2>/dev/null; then
        wait "$XR_QEMU_PID" || child_status=$?
        printf "${STY_RED}[$0]: QEMU exited with status $child_status; see $XR_LOG.${STY_RST}\n"
    else
        wait "$XR_VIEW_PID" || child_status=$?
        if [ "$child_status" -ne 0 ]; then
            printf "${STY_RED}[$0]: XR viewer exited with status $child_status; see $XR_VIEW_LOG.${STY_RST}\n"
        else
            echo "[+] XR viewer closed; stopping QEMU."
        fi
    fi
    exit "$child_status"
fi

# Extra QEMU options, for tests that need more than the defaults, e.g. a QMP socket to
# inject absolute pointer events from the host (the human monitor can only do relative):
#   XENEVA_QEMU_EXTRA_ARGS='-qmp unix:/tmp/xeneva-qmp.sock,server=on,wait=off'
if [ -n "${XENEVA_QEMU_EXTRA_ARGS:-}" ]; then
    read -r -a extra_qemu_args <<<"${XENEVA_QEMU_EXTRA_ARGS}"
    QEMU_ARGS+=("${extra_qemu_args[@]}")
fi

if [ "$HEADLESS" -eq 1 ]; then
    QEMU_ARGS+=(-display none -no-reboot)
    # Ordinary builds still block at the interactive resolution menu. Bleed
    # selects its low-memory mode in the bootloader and can therefore complete
    # an automated headless boot; the timeout bounds both cases for CI.
    qemu_timeout="${QEMU_TIMEOUT:-120}"
    qemu_status=0
    timeout --foreground "$qemu_timeout" qemu-system-aarch64 "${QEMU_ARGS[@]}" || qemu_status=$?
    if [ "$qemu_status" -eq 124 ]; then
        echo "[+] Headless run stopped by the ${qemu_timeout}s timeout (QEMU_TIMEOUT)."
    fi
    exit "$qemu_status"
else
    if [ "$EGL_HEADLESS" -eq 1 ]; then
        # addr= is a bus QEMU *connects to*, not a socket it creates.
        # Use the session bus (needs DBUS_SESSION_BUS_ADDRESS). --axiss
        QEMU_ARGS+=(-display egl-headless)
        QEMU_ARGS+=(-display dbus,gl=on)
        echo "[+] QEMU egl-headless + dbus on the session bus (org.qemu)"
        echo "[+] Steal frames: Tools/xeneva-xr-view/xeneva-xr-view --desktop --egl"
        qemu-system-aarch64 "${QEMU_ARGS[@]}"
    else
        # Keep both ramfb (boot/GOP) and virtio-gpu visible. The compositor
        # moves to the second display after boot, while ramfb goes black. --axiss
        QEMU_ARGS+=(-display gtk,zoom-to-fit=on,show-tabs=on)
        qemu-system-aarch64 "${QEMU_ARGS[@]}"
    fi
fi