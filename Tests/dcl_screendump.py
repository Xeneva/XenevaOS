#!/usr/bin/env python3
"""Host side of the DRM scanout pixel proofs (bochs0 and gpu0).

usage:
  dcl_screendump.py wait  --log LOG --marker TEXT --out FILE
                          [--sock SOCK] [--dev DEV] [--timeout SEC]
  dcl_screendump.py check --ppm FILE --rgb RRGGBB

`wait` polls LOG for MARKER -- the guest's own line, printed once the
pattern commit has returned -- then asks QEMU's monitor for a screendump
of DEV into FILE.  Exit 0 once FILE exists; 2 when the marker never
appears; 3 when the monitor socket cannot be reached; 4 when the dump
does not land.

`check` reads that PPM, samples --rgb across the image and exits 0 only
when every sample matches.  Either way it prints one line saying what it
saw.

Why the proof lives here: the guest can only see its own memory.  The
pattern it writes goes into a GEM buffer; the kernel memcpy'd that into
the device's PCI BAR and QEMU built a display surface out of what the
device scan-read from it.  The pixels are QEMU's, so the only place they
can be looked at is on the host.
"""

import os
import re
import socket
import sys
import time

USAGE = __doc__


def die(msg, code=1):
    print("dcl_screendump: %s" % msg)
    sys.exit(code)


def parse_args(argv):
    opts = {}
    i = 0
    while i < len(argv):
        key = argv[i]
        if key.startswith("--") and i + 1 < len(argv):
            opts[key[2:]] = argv[i + 1]
            i += 2
        elif key.startswith("--"):
            opts[key[2:]] = "1"
            i += 1
        else:
            die("unexpected argument %r\n%s" % (key, USAGE), 1)
    return opts


def read_ppm(path):
    with open(path, "rb") as fh:
        data = fh.read()

    def token(pos):
        # skip whitespace and #-comments
        while pos < len(data):
            if data[pos:pos + 1] in b" \t\r\n":
                pos += 1
            elif data[pos:pos + 1] == b"#":
                while pos < len(data) and data[pos:pos + 1] != b"\n":
                    pos += 1
            else:
                break
        start = pos
        while pos < len(data) and data[pos:pos + 1] not in b" \t\r\n":
            pos += 1
        return data[start:pos], pos

    magic, pos = token(0)
    if magic != b"P6":
        die("not a P6 PPM (%r)" % magic, 4)
    width, pos = token(pos)
    height, pos = token(pos)
    maxval, pos = token(pos)
    pos += 1  # single whitespace after maxval
    width, height, maxval = int(width), int(height), int(maxval)
    if maxval != 255:
        die("maxval %d, expected 255" % maxval, 4)
    if pos + width * height * 3 > len(data):
        die("PPM truncated (%d x %d wants %d bytes, has %d)"
            % (width, height, width * height * 3, len(data) - pos), 4)
    return width, height, data[pos:]


def check(args):
    path = args.get("ppm")
    rgb = args.get("rgb")
    if not path or not rgb:
        die("check needs --ppm and --rgb\n%s" % USAGE, 1)
    want = bytes.fromhex(rgb)
    if len(want) != 3:
        die("--rgb takes six hex digits, got %r" % rgb, 1)

    width, height, pixels = read_ppm(path)
    if width < 320 or height < 200:
        die("unexpected surface size %dx%d" % (width, height), 4)

    # A 5x5 grid: corners, centre and the points in between.  One wrong
    # pixel anywhere in this set is a scanout that is not the pattern --
    # a different stride, a stale surface, or nothing drawn at all.
    bad = []
    for gy in range(5):
        for gx in range(5):
            x = (gx * (width - 1)) // 4
            y = (gy * (height - 1)) // 4
            off = (y * width + x) * 3
            got = pixels[off:off + 3]
            if got != want:
                bad.append("(%d,%d)=%s" % (x, y, got.hex()))

    if bad:
        die("%dx%d sampled, %d of 25 wrong, first %s (want %s)"
            % (width, height, len(bad), bad[0], rgb), 4)

    print("dcl_screendump: %dx%d, 25 of 25 samples are %s"
          % (width, height, rgb))
    return 0


def monitor(sock_path, cmd, timeout):
    """One HMP command over a fresh connection; True when it was sent."""
    deadline = time.time() + timeout
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(2.0)
    while True:
        try:
            sock.connect(sock_path)
            break
        except OSError as exc:
            if time.time() > deadline:
                print("dcl_screendump: connect %s: %s" % (sock_path, exc))
                return False
            time.sleep(0.3)

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
            print("dcl_screendump: no monitor prompt")
            return False
        buf = b""
        sock.sendall((cmd + "\n").encode())
        if not until_prompt():
            print("dcl_screendump: no monitor reply to %r" % cmd)
            return False
    finally:
        sock.close()
    return True


def wait(args):
    log = args.get("log")
    marker = args.get("marker")
    out = args.get("out")
    if not log or not marker or not out:
        die("wait needs --log, --marker and --out\n%s" % USAGE, 1)
    sock_path = args.get("sock", "/tmp/xeneva-mon.sock")
    dev = args.get("dev", "bochs0")
    timeout = float(args.get("timeout", "100"))

    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with open(log, "r", errors="replace") as fh:
                if marker in fh.read():
                    break
        except OSError:
            pass
        time.sleep(0.5)
    else:
        print("dcl_screendump: marker %r not seen in %s within %.0fs"
              % (marker, log, timeout))
        return 2

    # The marker is printed after the commit ioctl returns, which is after
    # the dispi registers were written, so the surface is already the
    # pattern; the settle time is for QEMU's console update to land.
    time.sleep(1.5)

    if os.path.exists(out):
        os.unlink(out)
    if not monitor(sock_path, "screendump %s %s" % (out, dev), timeout):
        return 3
    for _ in range(int(timeout * 2)):
        if os.path.exists(out) and os.path.getsize(out) > 0:
            time.sleep(0.2)
            print("dcl_screendump: %s via '%s' %s" % (out, dev, marker))
            return 0
        time.sleep(0.5)
    print("dcl_screendump: screendump never appeared at %s" % out)
    return 4


def main():
    argv = sys.argv[1:]
    if not argv or argv[0] in ("-h", "--help"):
        print(USAGE)
        return 0
    mode = argv[0]
    args = parse_args(argv[1:])
    if mode == "wait":
        return wait(args)
    if mode == "check":
        return check(args)
    die("unknown mode %r\n%s" % (mode, USAGE), 1)


if __name__ == "__main__":
    sys.exit(main())
