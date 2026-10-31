#!/usr/bin/env python3
"""Serial / UNIX-socket Bluetooth bridge (Zephyr-style, BlueZ -> QEMU).

Replaces the old /dev/vhci + virtio-serial bridge, which fought BlueZ
for the adapter and had no QEMU wiring. Instead the host Bluetooth
stack is piped into the guest's emulated serial line:

  host hci0 --(HCI_USER)--> btproxy --(unix socket)--> QEMU -serial --> guest UART1 (H4)

Host setup (once per boot):
  sudo btproxy -u -i 0          # listens on /tmp/bt-server-bredr
  # HCI_CHANNEL_USER binds only while hci0 is DOWN. bluetoothd powers it
  # back up after each client disconnect; build_and_run_qemu.sh turns it
  # off over D-Bus before QEMU connects. "Error from host descriptor" is
  # btproxy seeing the UNIX client hang up. "Device or resource busy" means
  # the adapter was still UP when the next client arrived.

Guest launch (handled by build_and_run_qemu.sh):
  -serial stdio -serial unix:/tmp/bt-server-bredr
  # first -serial  = UART0 console, second -serial = UART1 BT H4

This script is a thin helper around that flow:
  * if BlueZ `btproxy` exists (normal case) it execs it, so behaviour
    matches Zephyr docs exactly;
  * with --python it runs a pure-Python HCI_USER <-> unix-socket proxy
    (same H4 framing as btproxy) for hosts without the btproxy binary.

Usage:
  sudo python3 Scripts/Linux/bt_bridge.py [--python] [socket-path] [hci-index]
  sudo python3 Scripts/Linux/bt_bridge.py --python /tmp/bt-server-bredr 0
"""
import ctypes
import ctypes.util
import os
import select
import shutil
import socket
import sys

SOCK_DEFAULT = "/tmp/bt-server-bredr"
HCI_DEFAULT = 0

H4_CMD = 0x01
H4_ACL = 0x02
H4_SCO = 0x03
H4_EVT = 0x04
H4_ISO = 0x05


def h4_packet_len(buf):
    """Return full H4 packet length if buf holds one, else 0. Skips 0x00 pad."""
    i = 0
    while i < len(buf) and buf[i] == 0x00:
        i += 1
    if i >= len(buf):
        return 0 if i == 0 else -i  # need more, or consume pads
    t = buf[i]
    if t == H4_CMD:
        if len(buf) - i < 4:
            return 0
        return i + 4 + buf[i + 3]
    if t == H4_ACL or t == H4_ISO:
        if len(buf) - i < 5:
            return 0
        dlen = buf[i + 3] | (buf[i + 4] << 8)
        return i + 5 + dlen
    if t == H4_SCO:
        if len(buf) - i < 4:
            return 0
        return i + 4 + buf[i + 3]
    if t == H4_EVT:
        if len(buf) - i < 3:
            return 0
        return i + 3 + buf[i + 2]
    # Unknown type (btproxy reports "unknown host packet type"): drop 1 byte.
    return i + 1 if len(buf) - i == 1 else -(i + 1)


def open_hci_user(index):
    """Open AF_BLUETOOTH HCI_CHANNEL_USER like btproxy's open_channel()."""
    AF_BLUETOOTH = 31  # from linux/bluetooth.h
    BTPROTO_HCI = 1
    HCI_CHANNEL_USER = 1
    fd = os.open("/dev/null", os.O_RDONLY)  # placeholder to keep mypy calm
    os.close(fd)
    s = socket.socket(AF_BLUETOOTH, socket.SOCK_RAW, BTPROTO_HCI)
    # Python can't bind sockaddr_hci, so call libc bind() directly.
    libcname = ctypes.util.find_library("c") or "libc.so.6"
    libc = ctypes.CDLL(libcname, use_errno=True)
    import struct

    addr = struct.pack("<HHH", AF_BLUETOOTH, index, HCI_CHANNEL_USER)
    # bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
    libc.bind.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    libc.bind.restype = ctypes.c_int
    rc = libc.bind(s.fileno(), addr, len(addr))
    if rc != 0:
        err = ctypes.get_errno()
        s.close()
        raise OSError(err, f"bind HCI_CHANNEL_USER hci{index} failed: {os.strerror(err)} "
                           f"(is hci{index} down? run: sudo hciconfig hci{index} down; "
                           f"is bluetooth.service stopped?)")
    return s


def run_python_proxy(sock_path, hci_index):
    try:
        hci = open_hci_user(hci_index)
    except OSError as e:
        print(f"[bt-bridge] {e}", flush=True)
        sys.exit(1)
    print(f"[bt-bridge] HCI_CHANNEL_USER open on hci{hci_index}", flush=True)
    try:
        os.unlink(sock_path)
    except FileNotFoundError:
        pass
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(sock_path)
    os.chmod(sock_path, 0o666)
    srv.listen(1)
    print(f"[bt-bridge] listening on {sock_path}, waiting for QEMU -serial ...", flush=True)
    conn, _ = srv.accept()
    print("[bt-bridge] QEMU connected, bridging H4", flush=True)
    # HCI socket is datagram (one H4 packet per read); unix side is a stream.
    pending = bytearray()
    try:
        while True:
            ready, _, _ = select.select([hci, conn], [], [])
            if hci in ready:
                pkt = hci.recv(4096)
                if not pkt:
                    break
                conn.sendall(pkt)
            if conn in ready:
                data = conn.recv(4096)
                if not data:
                    print("[bt-bridge] QEMU disconnected", flush=True)
                    break
                pending.extend(data)
                # Reassemble stream into H4 packets for the datagram socket.
                while True:
                    n = h4_packet_len(pending)
                    if n == 0:
                        break
                    if n < 0:
                        del pending[:-n]
                        break
                    pkt = bytes(pending[:n])
                    # Strip leading 0x00 pads (Zephyr -z behaviour).
                    k = 0
                    while k < len(pkt) and pkt[k] == 0x00:
                        k += 1
                    pkt = pkt[k:]
                    if len(pkt) > 1:
                        try:
                            hci.send(pkt)
                        except OSError as e:
                            print(f"[bt-bridge] hci send failed: {e}", flush=True)
                    del pending[:n]
    except (BrokenPipeError, ConnectionResetError, OSError):
        pass
    finally:
        conn.close()
        srv.close()
        hci.close()


def main():
    args = [a for a in sys.argv[1:] if a != "--python"]
    use_python = "--python" in sys.argv[1:]
    sock_path = args[0] if len(args) > 0 else SOCK_DEFAULT
    hci_index = int(args[1]) if len(args) > 1 else HCI_DEFAULT
    proxy = shutil.which("btproxy")
    if not use_python and proxy:
        print(f"[bt-bridge] exec {proxy} -u -i {hci_index} "
              f"(listens on {sock_path if sock_path == SOCK_DEFAULT else '/tmp/bt-server-bredr'})",
              flush=True)
        print("[bt-bridge] host pre-req: sudo hciconfig hci0 down (and stop bluetooth.service)",
              flush=True)
        cmd = [proxy, "-u", "-i", str(hci_index)]
        if sock_path != SOCK_DEFAULT:
            # btproxy -u takes optional unix path: -u <path>.
            cmd = [proxy, "-u", sock_path, "-i", str(hci_index)]
        os.execvp(proxy, cmd)
    elif not use_python:
        print("[bt-bridge] btproxy not found, falling back to built-in python proxy", flush=True)
    if os.geteuid() != 0:
        print("[bt-bridge] need root for HCI_CHANNEL_USER (run with sudo)", flush=True)
        sys.exit(1)
    run_python_proxy(sock_path, hci_index)


if __name__ == "__main__":
    if os.path.basename(sys.argv[0]) == "bt_bridge.py" and len(sys.argv) == 2 and sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        sys.exit(0)
    # Back-compat: old path /tmp/xeneva-bt.sock meant the vhci bridge.
    if len(sys.argv) > 1 and sys.argv[1] == "/tmp/xeneva-bt.sock":
        print("[bt-bridge] /tmp/xeneva-bt.sock was the old vhci bridge and is retired.",
              flush=True)
        print(f"[bt-bridge] use {SOCK_DEFAULT} with btproxy + QEMU -serial instead.", flush=True)
        print("[bt-bridge] run: sudo hciconfig hci0 down && sudo btproxy -u -i 0", flush=True)
        sys.exit(2)
    main()
