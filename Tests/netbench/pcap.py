#!/usr/bin/env python3
"""Small pcap reader and TCP metric extractor for the netbench harness.

The captures are produced by QEMU's ``filter-dump`` object on the host side,
so they contain Ethernet frames (linktype 1).  We only need IPv4/TCP and we
only care about the connection that carried the fixed-size host->guest
payload (8 MiB by default).
"""

from __future__ import annotations

import bisect
import statistics
import struct
from collections import Counter
from pathlib import Path


def _read_pcap(path: Path):
    raw = path.read_bytes()
    magic = raw[:4]
    if magic == b"\xd4\xc3\xb2\xa1":
        endian, nano = "<", False
    elif magic == b"\xa1\xb2\xc3\xd4":
        endian, nano = ">", False
    elif magic == b"\x4d\x3c\xb2\xa1":
        endian, nano = "<", True
    elif magic == b"\xa1\xb2\x3c\x4d":
        endian, nano = ">", True
    else:
        raise ValueError(f"unknown pcap magic {magic!r}")
    link = struct.unpack_from(endian + "I", raw, 20)[0]
    off = 24
    packets = []
    while off + 16 <= len(raw):
        ts, tus, incl, _orig = struct.unpack_from(endian + "IIII", raw, off)
        off += 16
        data = raw[off:off + incl]
        off += incl
        packets.append((ts + tus / (1e9 if nano else 1e6), data))
    return link, packets


def _parse_tcp(data: bytes, link: int):
    if link == 1:
        if len(data) < 14:
            return None
        ethertype = struct.unpack_from(">H", data, 12)[0]
        p = 14
        while ethertype == 0x8100 and len(data) >= p + 4:
            ethertype = struct.unpack_from(">H", data, p + 2)[0]
            p += 4
        if ethertype != 0x0800:
            return None
    elif link == 113:  # Linux cooked capture
        if len(data) < 16:
            return None
        if struct.unpack_from(">H", data, 14)[0] != 0x0800:
            return None
        p = 16
    else:
        return None

    if len(data) < p + 20:
        return None
    ihl = (data[p] & 0x0F) * 4
    if data[p + 9] != 6 or len(data) < p + ihl + 20:
        return None
    total = struct.unpack_from(">H", data, p + 2)[0]
    src = ".".join(str(b) for b in data[p + 12:p + 16])
    dst = ".".join(str(b) for b in data[p + 16:p + 20])
    t = p + ihl
    sport, dport = struct.unpack_from(">HH", data, t)
    seq, ack = struct.unpack_from(">II", data, t + 4)
    doff = (data[t + 12] >> 4) * 4
    flags = data[t + 13]
    win = struct.unpack_from(">H", data, t + 14)[0]
    payload = max(0, total - ihl - doff)
    opts = data[t + 20:t + doff]
    return {
        "src": src,
        "dst": dst,
        "sport": sport,
        "dport": dport,
        "seq": seq,
        "ack": ack,
        "flags": flags,
        "win": win,
        "payload": payload,
        "opts": opts,
    }


def _tcp_options(opts: bytes):
    out = {}
    i = 0
    while i < len(opts):
        kind = opts[i]
        if kind == 0:
            break
        if kind == 1:
            i += 1
            continue
        if i + 1 >= len(opts):
            break
        length = opts[i + 1]
        if length < 2 or i + length > len(opts):
            break
        value = opts[i + 2:i + length]
        if kind == 2 and len(value) == 2:
            out["mss"] = struct.unpack(">H", value)[0]
        elif kind == 3 and len(value) == 1:
            out["ws"] = value[0]
        elif kind == 4:
            out["sackok"] = True
        elif kind == 8 and len(value) == 8:
            out["ts"] = struct.unpack(">II", value)
        i += length
    return out


def _pct(values, p):
    if not values:
        return 0.0
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(len(ordered) * p))]


def analyze_pcap(path: Path, expected_bytes: int = 8 * 1024 * 1024):
    """Return one metric dict per connection that carried ``expected_bytes``."""
    link, packets = _read_pcap(path)
    conns = {}
    for t, data in packets:
        r = _parse_tcp(data, link)
        if not r:
            continue
        syn = r["flags"] & 0x02
        ack = r["flags"] & 0x10
        key = (r["src"], r["sport"], r["dst"], r["dport"])
        rev = (r["dst"], r["dport"], r["src"], r["sport"])
        if syn and not ack:
            c = conns.setdefault(key, {
                "client_ip": r["src"],
                "client_port": r["sport"],
                "server_ip": r["dst"],
                "server_port": r["dport"],
                "syn": r,
                "synack": None,
                "data": [],
                "acks": [],
            })
            c["syn"] = r
            continue
        c = conns.get(key)
        if c is None:
            c = conns.get(rev)
        if c is None:
            continue
        if r["src"] == c["server_ip"] and r["sport"] == c["server_port"]:
            if syn and ack and c["synack"] is None:
                c["synack"] = r
            if r["payload"]:
                c["data"].append((t, r))
        elif r["src"] == c["client_ip"] and r["sport"] == c["client_port"]:
            if ack and not r["payload"]:
                c["acks"].append((t, r))

    results = []
    for c in conns.values():
        if not c["data"]:
            continue
        nbytes = sum(r["payload"] for _, r in c["data"])
        if nbytes != expected_bytes:
            continue
        t_first = c["data"][0][0]
        t_last = c["data"][-1][0]
        acks_after = [a for a in c["acks"] if a[0] >= t_first]
        t_end = acks_after[-1][0] if acks_after else t_last
        duration = max(t_end - t_first, 1e-9)
        gaps = [
            (c["data"][i][0] - c["data"][i - 1][0]) * 1000.0
            for i in range(1, len(c["data"]))
        ]
        big_gaps = [g for g in gaps if g > 0.4]

        ends = sorted((r["seq"] + r["payload"], t) for t, r in c["data"])
        end_keys = [e[0] for e in ends]
        latencies = []
        for t, r in acks_after:
            if not (r["flags"] & 0x10):
                continue
            # The segment that completed the acknowledged range.
            i = bisect.bisect_right(end_keys, r["ack"]) - 1
            if i >= 0:
                latencies.append((t - ends[i][1]) * 1000.0)

        seen = set()
        retransmits = 0
        for _, r in c["data"]:
            key = (r["seq"], r["payload"])
            if key in seen:
                retransmits += 1
            seen.add(key)

        syn_opts = _tcp_options(c["syn"]["opts"])
        synack_opts = _tcp_options(c["synack"]["opts"]) if c["synack"] else {}
        win_values = Counter(r["win"] for _, r in acks_after)
        window = win_values.most_common(1)[0][0] if win_values else None

        results.append({
            "client_ip": c["client_ip"],
            "client_port": c["client_port"],
            "server_port": c["server_port"],
            "bytes": nbytes,
            "data_pkts": len(c["data"]),
            "duration_ms": duration * 1000.0,
            "rate_mib_s": nbytes / duration / 1048576.0,
            "ack_count": len(acks_after),
            "bytes_per_ack": nbytes / len(acks_after) if acks_after else None,
            "ack_p50_ms": _pct(latencies, 0.50),
            "ack_p90_ms": _pct(latencies, 0.90),
            "ack_max_ms": max(latencies) if latencies else 0.0,
            "gaps_over_04": len(big_gaps),
            "gaps_over_04_ms": sum(big_gaps),
            "retransmits": retransmits,
            "window": window,
            "mss_client": syn_opts.get("mss"),
            "mss_server": synack_opts.get("mss"),
            "ws_client": syn_opts.get("ws"),
            "ws_server": synack_opts.get("ws"),
            "wscale_negotiated": "ws" in syn_opts and "ws" in synack_opts,
            "sack_negotiated": "sackok" in syn_opts and "sackok" in synack_opts,
        })
    results.sort(key=lambda r: r["duration_ms"])
    return results


def aggregate(entries):
    """Median of the numeric fields across pcap connections."""
    if not entries:
        return None
    numeric = [
        "rate_mib_s", "ack_count", "bytes_per_ack", "ack_p50_ms",
        "ack_p90_ms", "gaps_over_04", "gaps_over_04_ms", "retransmits",
    ]
    out = dict(entries[0])
    for key in numeric:
        values = [e[key] for e in entries if e.get(key) is not None]
        out[key] = statistics.median(values) if values else None
    out["connections"] = len(entries)
    return out
