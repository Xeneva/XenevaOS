#!/usr/bin/env python3
# BSD 2-Clause License
#
# Copyright (c) 2022-2026, Manas Kamal Choudhury
# All rights reserved.
#
# Host side of `wifictl air`. The guest connects to 10.0.2.2:9753, which
# QEMU user-net delivers to this process on 127.0.0.1. The scan is the
# laptop radio's cached result (`iw scan dump`). A fresh scan needs root
# and is not attempted, so the host association stays up.

import socket
import subprocess

HOST = "127.0.0.1"
PORT = 9753
IFACE = "wlp2s0"


def run(args):
    try:
        p = subprocess.run(args, capture_output=True, text=True, timeout=5)
    except (OSError, subprocess.TimeoutExpired) as exc:
        return "", str(exc)
    return p.stdout, p.stderr


def channel(freq):
    f = int(float(freq))
    if f == 2484:
        return 14
    if 2412 <= f <= 2472:
        return (f - 2407) // 5
    if 5000 <= f <= 5885:
        return (f - 5000) // 5
    return 0


def security(bss):
    if bss["eap"]:
        return "802.1x"
    if bss["psk"] or bss["rsn"]:
        return "wpa2"
    return "open"


def parse_dump(text):
    cur = None
    found = []

    def finish():
        if cur and cur["bssid"]:
            found.append(cur)

    for raw in text.splitlines():
        line = raw.strip()
        if raw.startswith("BSS "):
            finish()
            mac = raw.split()[1].split("(")[0]
            cur = {
                "bssid": mac,
                "ssid": "",
                "freq": "0",
                "signal": "0",
                "rsn": False,
                "psk": False,
                "eap": False,
                "assoc": "associated" in raw,
            }
            continue
        if cur is None:
            continue
        if line.startswith("freq:"):
            cur["freq"] = line.split(":", 1)[1].strip().split()[0]
        elif line.startswith("signal:"):
            cur["signal"] = line.split(":", 1)[1].strip().split()[0]
        elif line.startswith("SSID:"):
            cur["ssid"] = line.split(":", 1)[1].strip()
        elif line.startswith("RSN:"):
            cur["rsn"] = True
        elif "Authentication suites:" in line and "802.1X" in line:
            cur["eap"] = True
        elif "Authentication suites:" in line and "PSK" in line:
            cur["psk"] = True
    finish()
    return found


def scan_text():
    link, _ = run(["iw", "dev", IFACE, "link"])
    dump, err = run(["iw", "dev", IFACE, "scan", "dump"])
    rows = parse_dump(dump)
    lines = []
    ssid = ""
    for raw in link.splitlines():
        if raw.strip().startswith("SSID:"):
            ssid = raw.split(":", 1)[1].strip()
    if ssid:
        lines.append("host %s associated %s" % (IFACE, ssid))
    elif link.strip().startswith("Not connected"):
        lines.append("host %s not associated" % IFACE)
    else:
        lines.append("host %s link unavailable" % IFACE)
    if not rows and err.strip():
        lines.append("scan dump failed")
    for bss in rows:
        name = bss["ssid"] if bss["ssid"] else "<hidden>"
        sig = bss["signal"].split(".")[0]
        mark = " associated" if bss["assoc"] else ""
        lines.append(
            "%s  ch %d  %s dBm  %s  %s%s"
            % (name, channel(bss["freq"]), sig, security(bss), bss["bssid"], mark)
        )
    return "\n".join(lines) + "\n"


def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((HOST, PORT))
    srv.listen(1)
    print("wifi_proxy listening on %s:%d (%s scan dump)" % (HOST, PORT, IFACE), flush=True)
    while True:
        conn, _addr = srv.accept()
        try:
            data = b""
            conn.settimeout(3)
            while b"\n" not in data and len(data) < 64:
                chunk = conn.recv(64)
                if not chunk:
                    break
                data += chunk
            if data.startswith(b"SCAN"):
                conn.sendall(scan_text().encode())
        except (OSError, socket.timeout):
            pass
        finally:
            conn.close()


if __name__ == "__main__":
    main()
