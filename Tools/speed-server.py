#!/usr/bin/env python3
"""Tiny host-side TCP source for the XenevaOS network speed test.

Listens on a port and, for each connection, sends a fixed number of MiB as
fast as the host can write, then shuts the write side down. Run it on the
host and point `netsock --speed-host 10.0.2.2 --speed-port 9000` at it from
a QEMU guest; 10.0.2.2 is QEMU user networking's host gateway.

Usage:
    python3 Tools/speed-server.py --port 9000 --mib 8
    python3 Tools/speed-server.py --once
"""

from __future__ import annotations

import argparse
import socket
import sys


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--bind", default="0.0.0.0", help="bind address (default: 0.0.0.0)")
    ap.add_argument("--port", type=int, default=9000, help="TCP port (default: 9000)")
    ap.add_argument("--mib", type=int, default=8, help="MiB per connection (default: 8)")
    ap.add_argument(
        "--count",
        type=int,
        default=0,
        help="serve this many connections, then exit (default: forever)",
    )
    ap.add_argument("--once", action="store_true", help="serve one connection and exit")
    args = ap.parse_args()

    if args.mib <= 0:
        ap.error("--mib must be positive")
    limit = 1 if args.once else args.count
    if limit < 0:
        ap.error("--count must not be negative")

    # 64 KiB pattern; content is irrelevant because the guest only counts bytes.
    pattern = bytes((i * 13 + 7) & 0xFF for i in range(65536))
    total = args.mib * 1024 * 1024

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.bind, args.port))
    srv.listen(4)
    print(
        f"speed-server: listening on {args.bind}:{args.port}, "
        f"{args.mib} MiB per connection",
        flush=True,
    )

    served = 0
    try:
        while not limit or served < limit:
            conn, addr = srv.accept()
            served += 1
            conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            print(f"speed-server: client {addr[0]}:{addr[1]}", flush=True)
            sent = 0
            try:
                while sent < total:
                    n = conn.send(pattern[: min(len(pattern), total - sent)])
                    if n <= 0:
                        break
                    sent += n
                conn.shutdown(socket.SHUT_WR)
            except (BrokenPipeError, ConnectionResetError) as exc:
                print(f"speed-server: client aborted: {exc}", flush=True)
            finally:
                conn.close()
            print(f"speed-server: sent {sent} bytes", flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        srv.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
