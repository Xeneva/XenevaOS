#!/usr/bin/env python3
"""Host half of the XenevaOS <-> host clipboard (guest side is clipd).

Text crosses QEMU's virtserialport chardev, which build_and_run_qemu.sh
exposes as a TCP socket on 127.0.0.1:43211. A stream socket has no message
boundaries of its own, so both ends frame it the same way:

    4 bytes  payload length, little endian
    1 byte   type -- 0 = clipboard contents, 1 = hello
    n bytes  payload

The guest sends the hello first and keeps resending it until a write is
accepted; that is what tells us a guest is listening, and it is also what
keeps a push from arriving before clipd has the port open (QEMU drops what
comes in before then).

Which direction this end acts on is --dir:

  host2vm   push, ignore what comes back          (copy host -> guest)
  vm2host   push nothing, apply what arrives      (copy guest -> host)
  bi        do both

The guest side is always both directions, so one image serves every mode
and the choice lives entirely here. An incoming frame equal to the last
thing we pushed is the guest echoing us back -- that is the ack, and
skipping it is what stops the two ends from relaying to each other forever.

Usage:
  Tests/clip_bridge.py --dir=bi --text 'hello from the host'
  Tests/clip_bridge.py --dir=bi                      # host clipboard
  Tests/clip_bridge.py --dir=vm2host --watch
  Tests/clip_bridge.py --roundtrip my-token          # what the gate runs

Received text goes to --out (default: stdout) and, with --to-clipboard, to
the host's own clipboard through wl-copy/xclip/pbpaste when present.
--dir=bi implies --to-clipboard: doing both halves means what the guest
copies has to be allowed to reach the host clipboard, and without the flag
the guest->host half silently stopped at stdout instead.
In --watch mode a lost connection is re-attached rather than fatal, so a
QEMU restart does not quietly retire the bridge.
Exit status: 0 on success, 1 on timeout or a dead connection.
"""
import argparse
import select
import shutil
import socket
import struct
import subprocess
import sys
import time

HOST = "127.0.0.1"
PORT_DEFAULT = 43211

TYPE_TEXT = 0
TYPE_HELLO = 1
HDR = struct.Struct("<IB")
LEN_MAX = 4096

# Host clipboard tools, first one that exists wins. Absent is normal on a
# headless machine, in which case the clipboard is only reachable through
# --text and --out.
CLIP_READ = (
    ["wl-paste", "--no-newline"],
    ["xclip", "-selection", "clipboard", "-out"],
    ["pbpaste"],
)
CLIP_WRITE = (
    ["wl-copy"],
    ["xclip", "-selection", "clipboard", "-in"],
)


def host_clipboard_read():
    for cmd in CLIP_READ:
        if shutil.which(cmd[0]):
            try:
                out = subprocess.run(cmd, capture_output=True, timeout=5).stdout
            except (OSError, subprocess.SubprocessError):
                continue
            # Capped at what the guest can hold: a longer frame is dropped by
            # length alone, so the guest would keep the clipboard it had while
            # this end recorded the value as pushed -- and the next echo could
            # never match it again. Capping keeps the tracked value and the
            # returned value the same bytes.
            return out[:LEN_MAX]
    return None


def host_clipboard_write(data):
    """Hand the data to the host clipboard and move on.

    wl-copy detaches only when its output is not a pipe -- handed a pipe it
    stays in the foreground holding the value, so a capture never completes:
    run() sat until its timeout and then killed the client that had just
    become the selection owner, which is what left the clipboard with no
    owner for the reader to find. DEVNULL lets it fork and return, and the
    exit status is now worth something."""
    for cmd in CLIP_WRITE:
        if shutil.which(cmd[0]):
            try:
                run = subprocess.run(
                    cmd, input=data, timeout=5,
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                )
                return run.returncode == 0
            except (OSError, subprocess.SubprocessError):
                continue
    return False


def connect(port, timeout):
    """Retry until QEMU's chardev socket exists; it binds once QEMU runs."""
    deadline = time.time() + timeout
    while True:
        try:
            sock = socket.create_connection((HOST, port), timeout=5)
            sock.setblocking(False)
            return sock
        except OSError:
            if time.time() >= deadline:
                raise SystemExit(
                    "clip_bridge: no listener on %s:%d after %ss"
                    % (HOST, port, timeout)
                )
            time.sleep(0.25)


def await_hello(sock, port, timeout, bounce):
    """Stay connected until the guest says hello, bouncing the link if not.

    QEMU re-sends a port's PORT_OPEN every time a client attaches, and that
    packet is the only thing that sets host_connected -- the flag the guest's
    write side refuses to send without. The guest drops a PORT_OPEN that
    arrives before its port is registered, so a client that attached during
    boot can stay invisible for the rest of the run, and nothing on the guest
    side can ask for another one. Re-attaching until the guest answers is what
    gets a fresh PORT_OPEN through to a guest that can actually use it.

    Frames that ride along with the hello are dropped: clipd sends the hello
    before anything else, so they are only ever the guest's starting clipboard
    contents, which nothing here has asked for yet.
    """
    deadline = time.time() + timeout
    while True:
        spoke = False
        until = time.time() + min(bounce, max(0.5, deadline - time.time()))
        while True:
            try:
                frames, live = recv_frames(sock, max(0.0, until - time.time()))
            except (ConnectionError, OSError):
                break
            if any(ftype == TYPE_HELLO for ftype, _ in frames):
                spoke = True
                break
            if not live or time.time() >= until:
                break
        if spoke:
            return sock
        sock.close()
        recv_frames.buf = b""  # a partial frame belongs to the dead link
        if time.time() >= deadline:
            raise SystemExit(
                "clip_bridge: no hello from the guest in %ss" % timeout
            )
        sock = connect(port, min(10.0, max(1.0, deadline - time.time())))


def attach(port, timeout, hello_timeout, bounce):
    """Connect and stay attached until the guest says hello.

    Startup and a re-attach after a lost link are the same dance, so they
    share one path: a connection that is up but mute is no use either.
    """
    return await_hello(connect(port, timeout), port, hello_timeout, bounce)


def recv_frames(sock, wait):
    """Whatever is complete in the buffer after waiting up to `wait` seconds."""
    data = recv_frames.buf = getattr(recv_frames, "buf", b"")
    ready, _, _ = select.select([sock], [], [], wait)
    if not ready:
        return [], False
    try:
        chunk = sock.recv(65536)
    except BlockingIOError:
        return [], False
    if chunk == b"":
        raise ConnectionError("guest closed the port")
    if chunk:
        data += chunk

    frames, live = [], True
    while len(data) >= HDR.size:
        length, ftype = HDR.unpack_from(data)
        if length > LEN_MAX:
            # Not ours -- the stream is not resynchronisable, so give up
            # rather than walk bytes hunting for a sync point.
            live = False
            break
        if len(data) < HDR.size + length:
            break
        frames.append((ftype, data[HDR.size : HDR.size + length]))
        data = data[HDR.size + length :]
    recv_frames.buf = data
    return frames, live


def send_frame(sock, ftype, payload):
    sock.sendall(HDR.pack(len(payload), ftype) + payload)


def emit(text, args, received=False):
    """Log text, and --to-clipboard also applies *received* text to the host.

    received=False is a status line: putting one of those on the clipboard
    would overwrite what the user just copied, and the watch loop would then
    push our own diagnostic into the guest as if it were their data."""
    if args.out:
        with open(args.out, "a") as fh:
            fh.write(text.decode("utf-8", "replace"))
    else:
        sys.stdout.write(text.decode("utf-8", "replace"))
        sys.stdout.flush()
    if args.to_clipboard and received:
        host_clipboard_write(text)


def main():
    ap = argparse.ArgumentParser(
        description="XenevaOS host<->VM clipboard bridge",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument(
        "--dir",
        choices=("host2vm", "vm2host", "bi"),
        default="host2vm",
        help="which way this end copies (default: host2vm)",
    )
    ap.add_argument("--port", type=int, default=PORT_DEFAULT)
    ap.add_argument("--text", help="payload to push; default is the host clipboard")
    ap.add_argument(
        "--watch",
        action="store_true",
        help="keep going instead of finishing one copy",
    )
    ap.add_argument("--out", help="append received text here instead of stdout")
    ap.add_argument("--to-clipboard", action="store_true",
                    help="also apply received text to the host clipboard")
    ap.add_argument("--roundtrip", metavar="TOKEN",
                    help="push TOKEN and succeed when the guest echoes it back")
    ap.add_argument("--connect-timeout", type=float, default=120)
    ap.add_argument("--timeout", type=float, default=60,
                    help="seconds to wait for the guest's hello")
    ap.add_argument("--bounce", type=float, default=3.0,
                    help="seconds without a hello before re-attaching")
    args = ap.parse_args()

    # "do both" only happens if what the guest copies is allowed to reach
    # the host clipboard; without this the guest->host half ends at stdout
    # and bidirectional looks like it only ever pushes one way.
    if args.dir == "bi":
        args.to_clipboard = True

    # Nothing is pushed before the guest speaks: the hello is the guest
    # saying a port is open on its side, and anything sent before then can
    # land where nobody will ever read it.
    sock = attach(args.port, args.connect_timeout, args.timeout, args.bounce)

    if args.roundtrip is not None:
        push = args.roundtrip.encode()
    elif args.text is not None:
        push = args.text.encode()
    elif args.dir in ("host2vm", "bi"):
        push = host_clipboard_read()
        if push is None:
            raise SystemExit(
                "clip_bridge: no --text and no host clipboard tool "
                "(install wl-clipboard or xclip)"
            )
    else:
        push = None

    last_pushed = None
    if push is not None:
        send_frame(sock, TYPE_TEXT, push)
        last_pushed = push

    # One copy is done when something has come back; --watch keeps going.
    # The round trip is the exception -- it succeeds on its echo alone, so
    # it runs until that arrives or the clock runs out.
    got = False
    acked = False
    deadline = time.time() + args.timeout
    while True:
        now = time.time()
        if args.watch:
            wait = 0.25
        elif got or now >= deadline:
            break
        else:
            wait = min(0.25, deadline - now)

        try:
            frames, _ = recv_frames(sock, wait)
        except (ConnectionError, OSError):
            if args.watch and args.roundtrip is None:
                # The link died, the job did not: QEMU restarted, or the
                # guest came back with a freshly registered port. Re-attach
                # the way the startup path does, and clear last_pushed so
                # the host clipboard is pushed into the new guest rather
                # than assumed already there.
                emit(b"clip_bridge: connection lost, re-attaching\n", args)
                sock.close()
                recv_frames.buf = b""  # a partial frame belongs to the dead link
                sock = attach(args.port, args.connect_timeout, args.timeout,
                              args.bounce)
                last_pushed = None
                continue
            if args.roundtrip is None and got:
                break
            raise SystemExit("clip_bridge: connection lost")

        for ftype, payload in frames:
            if ftype == TYPE_HELLO:
                continue

            if args.roundtrip is not None:
                # dcltest leaves text in the guest's clipboard at boot and
                # clipd reports it, so anything but our own token back is
                # somebody else's conversation.
                if payload == last_pushed:
                    emit(b"CLIP_ROUNDTRIP ok\n", args)
                    return 0
                continue

            if payload == last_pushed:
                # Our own text coming back -- the ack, and the only proof
                # this end has that the guest wrote it into the clipboard.
                # Re-pushing it would be the relay neither side asked for.
                got = True
                if push is not None and not acked:
                    acked = True
                    emit(b"PUSH ok: guest clipboard updated\n", args)
                continue

            emit(payload, args, received=True)
            got = True
            # Answered, so this end does not echo it straight back.
            last_pushed = payload

            if args.dir == "bi" and args.watch:
                reply = host_clipboard_read()
                if reply is not None and reply != payload:
                    send_frame(sock, TYPE_TEXT, reply)
                    last_pushed = reply

        if args.watch and args.dir in ("host2vm", "bi"):
            reply = host_clipboard_read()
            if reply is not None and reply != last_pushed:
                send_frame(sock, TYPE_TEXT, reply)
                last_pushed = reply

    if args.roundtrip is not None:
        emit(b"CLIP_ROUNDTRIP timeout\n", args)
        return 1
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
