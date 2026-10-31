#!/usr/bin/env python3
"""Host-side input injection for the DCL virtio-input gate.

usage: dcl_input_inject.py --log LOG [--marker TEXT] [--timeout SEC]
                           [--monitor SOCK] [--qmp SOCK]

The guest only proves that the vendored input core attached its devices.
This helper supplies the other half: once both devices are up it sends a
key through QEMU's HMP monitor and an absolute move plus click through QMP,
then waits for the guest to log both decoded event classes.  A pass means
the event travelled PCI -> virtio ring -> input core -> the DCL bridge ->
/dev/kybrd and /dev/mice.

Exit codes: 0 passed, 2 guest marker never appeared, 3 monitor/QMP failed,
4 events were sent but the guest did not decode them.
"""

import json
import socket
import sys
import time


def parse_args(argv):
    opts = {}
    i = 0
    while i < len(argv):
        key = argv[i]
        if key.startswith("--") and i + 1 < len(argv):
            opts[key[2:]] = argv[i + 1]
            i += 2
        else:
            raise SystemExit("unexpected argument %r" % (key,))
    return opts


def wait_for_text(path, needles, timeout):
    """True once every needle appears in path, polling until timeout."""
    end = time.time() + timeout
    while time.time() < end:
        try:
            with open(path, "r", errors="replace") as fh:
                text = fh.read()
            if all(needle in text for needle in needles):
                return True
        except OSError:
            pass
        time.sleep(0.4)
    return False


def connect_unix(path, timeout):
    end = time.time() + timeout
    while True:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(2.0)
        try:
            sock.connect(path)
            return sock
        except OSError:
            sock.close()
            if time.time() > end:
                return None
            time.sleep(0.3)


def hmp_command(path, command, timeout):
    """One HMP command over a fresh connection.  True when it was sent."""
    sock = connect_unix(path, timeout)
    if not sock:
        return False

    buf = b""

    def until_prompt():
        nonlocal buf
        end = time.time() + timeout
        while b"(qemu)" not in buf:
            if time.time() > end:
                return False
            try:
                chunk = sock.recv(4096)
            except socket.timeout:
                continue
            if not chunk:
                return False
            buf += chunk
        return True

    try:
        if not until_prompt():
            return False
        buf = b""
        sock.sendall((command + "\n").encode())
        return until_prompt()
    finally:
        sock.close()


class Qmp:
    """The small part of the QMP wire protocol input-send-event needs."""

    def __init__(self, path, timeout):
        self.sock = connect_unix(path, timeout)
        self.buf = b""
        self.timeout = timeout
        if self.sock:
            self._line()  # greeting
            self.command("qmp_capabilities")

    def _line(self):
        end = time.time() + self.timeout
        while b"\n" not in self.buf:
            if time.time() > end:
                return None
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                continue
            if not chunk:
                return None
            self.buf += chunk
        line, _, self.buf = self.buf.partition(b"\n")
        return line

    def command(self, name, args=None):
        request = {"execute": name}
        if args is not None:
            request["arguments"] = args
        self.sock.sendall((json.dumps(request) + "\n").encode())

        end = time.time() + self.timeout
        while time.time() < end:
            line = self._line()
            if line is None:
                return None
            try:
                reply = json.loads(line)
            except ValueError:
                continue
            if reply.get("event"):
                continue
            return reply
        return None


def main():
    args = parse_args(sys.argv[1:])
    log = args.get("log")
    if not log:
        raise SystemExit("--log is required")

    marker = args.get("marker",
                      "[modtest]: virtio_input 2 device(s) registered")
    timeout = float(args.get("timeout", "240"))
    monitor = args.get("monitor", "/tmp/xeneva-mon.sock")
    qmp_path = args.get("qmp", "/tmp/xeneva-qmp.sock")

    if not wait_for_text(log, [marker], timeout):
        print("dcl_input_inject: guest marker never appeared")
        return 2

    if not hmp_command(monitor, "sendkey a", 10):
        print("dcl_input_inject: HMP sendkey failed")
        return 3

    qmp = Qmp(qmp_path, 10)
    if not qmp.sock:
        print("dcl_input_inject: QMP socket unavailable")
        return 3
    try:
        # Absolute coordinates exercise the tablet's ABS path; the button
        # down/up pair exercises the native button bit mapping.  QEMU adds
        # the SYN_REPORT around each command, which is what makes the DCL
        # bridge emit one mouse packet at a time.
        commands = (
            {"events": [{"type": "abs",
                         "data": {"axis": "x", "value": 12345}},
                        {"type": "abs",
                         "data": {"axis": "y", "value": 6789}}]},
            {"events": [{"type": "btn",
                         "data": {"button": "left", "down": True}}]},
            {"events": [{"type": "btn",
                         "data": {"button": "left", "down": False}}]},
        )
        for command in commands:
            reply = qmp.command("input-send-event", command)
            if not reply or "error" in reply:
                print("dcl_input_inject: QMP input-send-event failed: %r"
                      % (reply,))
                return 3
    finally:
        qmp.sock.close()

    # The guest polls the event queue from its timer tick.
    expected = ["[virtio-input]: key code 30 value 1",
                "[virtio-input]: pointer"]
    if not wait_for_text(log, expected, 10):
        print("dcl_input_inject: guest did not decode both event classes")
        return 4

    print("dcl_input_inject: keyboard and tablet events reached the bridge")
    return 0


if __name__ == "__main__":
    sys.exit(main())
