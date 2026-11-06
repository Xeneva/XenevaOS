#!/usr/bin/env python3
"""Boot the Alpine aarch64 control guest and measure host->guest TCP.

The guest probe is a tiny static aarch64 binary built from ``tcpbench.c``.
It uses the same 64 KiB receive buffer as XenevaOS's netsystest so the
userspace read pattern is comparable.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

import pexpect

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
BUILD = HERE / "build"
PROMPT = "NETBENCH> "
LOOPBACK_PORT = 5002


def _wait_log(log_path: Path, needle: str, proc, timeout: float = 15.0) -> None:
    """Wait until a background server has printed its readiness line."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if proc.poll() is not None:
            text = log_path.read_text(encoding="utf-8", errors="replace")
            raise RuntimeError(
                f"server exited before becoming ready (rc={proc.returncode}):\n{text}"
            )
        try:
            text = log_path.read_text(encoding="utf-8", errors="replace")
        except FileNotFoundError:
            text = ""
        if needle in text:
            return
        time.sleep(0.05)
    raise RuntimeError(f"server did not print {needle!r} within {timeout:.0f}s")


def build_bench() -> Path:
    BUILD.mkdir(parents=True, exist_ok=True)
    src = HERE / "tcpbench.c"
    out = BUILD / "bench"
    if out.exists() and out.stat().st_mtime >= src.stat().st_mtime:
        return out
    cc = shutil.which("aarch64-linux-gnu-gcc")
    if not cc:
        raise RuntimeError(
            "aarch64-linux-gnu-gcc not found; install gcc-aarch64-linux-gnu "
            "or set the cross compiler in PATH"
        )
    subprocess.run(
        [cc, "-O2", "-static", "-s", "-o", str(out), str(src)],
        check=True,
    )
    return out


class Guest:
    def __init__(self, argv: list[str], log_path: Path, timeout: int):
        self.log = open(log_path, "w", encoding="utf-8")
        self.child = pexpect.spawn(
            argv[0], argv[1:], encoding="utf-8", codec_errors="replace",
            timeout=timeout,
        )
        self.child.logfile = self.log
        self.seq = 0

    def close(self) -> None:
        try:
            self.child.close(force=True)
        except Exception:
            pass
        self.log.close()

    def wait_login(self) -> None:
        idx = self.child.expect([r"login:", r"grub>", r"Press ESCAPE"], timeout=300)
        if idx == 1:
            self.child.sendline("")
            self.child.expect(r"login:", timeout=180)
        elif idx == 2:
            self.child.sendline("")
            self.child.expect(r"login:", timeout=180)
        self.child.sendline("root")
        idx = self.child.expect([r"# ", r"Password:"], timeout=120)
        if idx == 1:
            self.child.sendline("")
            self.child.expect(r"# ", timeout=60)
        self.child.sendline("stty -echo")
        self.child.expect(r"# ", timeout=30)
        self.child.sendline("export PS1='%s'" % PROMPT)
        self.child.expect(PROMPT, timeout=30)

    def run(self, command: str, timeout: int = 300, check: bool = False) -> str:
        self.seq += 1
        marker = "__NETBENCH_DONE_%d__" % self.seq
        self.child.sendline(
            "%s; rc=$?; echo __NETBENCH_RC=$rc; echo %s" % (command, marker)
        )
        self.child.expect(marker, timeout=timeout)
        text = self.child.before.replace(PROMPT, "")
        match = re.search(r"__NETBENCH_RC=(\d+)", text)
        rc = int(match.group(1)) if match else None
        text = re.sub(r"__NETBENCH_RC=\d+\s*", "", text)
        if check and rc not in (0, None):
            raise RuntimeError(f"command failed (rc={rc}): {command}\n{text}")
        return text

    def send(self, command: str) -> None:
        self.child.sendline(command)


def _parse_kibs(text: str):
    match = re.search(r"got (\d+) bytes in ([\d.]+) s = ([\d.]+) KiB/s", text)
    if match:
        return int(float(match.group(3)))
    match = re.search(r"sent (\d+) bytes in ([\d.]+) s = ([\d.]+) KiB/s", text)
    if match:
        return int(float(match.group(3)))
    return None


def run_linux(
    *,
    iso: Path,
    firmware: Path,
    qemu: str,
    reps: int,
    pcap_path: Path | None,
    log_path: Path,
    http_port: int,
    speed_port: int,
    big_port: int,
    speed_mib: int = 8,
    big_mib: int = 0,
    loopback_mib: int = 1,
    timeout: int = 300,
) -> dict:
    """Boot Linux once, run ``reps`` host->guest transfers, return metrics."""
    result = {
        "runs": [],
        "big": None,
        "loopback_client": None,
        "loopback_server": None,
        "guest": {},
        "log": str(log_path),
        "pcap": str(pcap_path) if pcap_path else None,
        "error": None,
    }
    servers = []
    guest = None
    try:
        build_bench()
        if pcap_path:
            pcap_path.parent.mkdir(parents=True, exist_ok=True)
            pcap_path.unlink(missing_ok=True)

        http_log_path = log_path.with_suffix(".http.log")
        http_log = open(http_log_path, "w", encoding="utf-8")
        http = subprocess.Popen(
            [sys.executable, "-u", "-m", "http.server", str(http_port),
             "--bind", "0.0.0.0", "--directory", str(BUILD)],
            stdout=http_log, stderr=subprocess.STDOUT,
        )
        servers.append((http, http_log))

        speed_log_path = log_path.with_suffix(".speed.log")
        speed_log = open(speed_log_path, "w", encoding="utf-8")
        speed = subprocess.Popen(
            [sys.executable, str(REPO / "Tools" / "speed-server.py"),
             "--bind", "0.0.0.0", "--port", str(speed_port),
             "--mib", str(speed_mib), "--count", str(reps + 2)],
            stdout=speed_log, stderr=subprocess.STDOUT,
        )
        servers.append((speed, speed_log))

        big = None
        if big_mib > 0:
            big_log_path = log_path.with_suffix(".big.log")
            big_log = open(big_log_path, "w", encoding="utf-8")
            big = subprocess.Popen(
                [sys.executable, str(REPO / "Tools" / "speed-server.py"),
                 "--bind", "0.0.0.0", "--port", str(big_port),
                 "--mib", str(big_mib), "--count", "2"],
                stdout=big_log, stderr=subprocess.STDOUT,
            )
            servers.append((big, big_log))

        _wait_log(http_log_path, "Serving HTTP", http)
        _wait_log(speed_log_path, "listening on", speed)
        if big is not None:
            _wait_log(big_log_path, "listening on", big)

        argv = [
            qemu,
            "-machine", "virt,gic-version=2,highmem=off",
            "-cpu", "cortex-a72",
            "-m", "1024M",
            "-bios", str(firmware),
            "-cdrom", str(iso),
            "-netdev",
            "user,id=net0,ipv4=on,net=10.0.2.0/24,host=10.0.2.2,"
            "dhcpstart=10.0.2.15,dns=10.0.2.3,ipv6=on,"
            "ipv6-net=fec0::/64,ipv6-host=fec0::2",
            "-device", "virtio-net-pci,netdev=net0",
            "-nographic", "-no-reboot",
        ]
        if pcap_path:
            argv += [
                "-object",
                f"filter-dump,id=netdump,netdev=net0,file={pcap_path},queue=all",
            ]

        guest = Guest(argv, log_path, timeout)
        guest.wait_login()
        guest.run("ip link set lo up", 30, check=True)
        guest.run("ip link set eth0 up", 30, check=True)
        guest.run("udhcpc -i eth0", 90)

        for cmd, key in [
            ("uname -a", "uname"),
            ("head -n 3 /etc/os-release", "os_release"),
            ("grep -m1 'CPU part' /proc/cpuinfo", "cpu_part"),
            ("free -m", "free"),
            ("sysctl net.ipv4.tcp_rmem net.ipv4.tcp_wmem "
             "net.core.rmem_max net.core.wmem_max", "sysctl"),
            ("ip -4 addr show dev eth0", "ipv4"),
        ]:
            result["guest"][key] = guest.run(cmd, 60).strip()

        guest.run(
            f"wget -q -O /tmp/bench http://10.0.2.2:{http_port}/bench",
            timeout=120, check=True,
        )
        guest.run("chmod +x /tmp/bench", 30, check=True)

        total = speed_mib * 1024 * 1024
        for i in range(reps):
            out = guest.run(
                f"/tmp/bench 10.0.2.2 {speed_port} {total}",
                timeout=300, check=True,
            )
            kib = _parse_kibs(out)
            if kib is None:
                raise RuntimeError(f"could not parse Linux run {i}: {out!r}")
            result["runs"].append(kib)
            time.sleep(0.25)

        if big_mib > 0:
            out = guest.run(
                f"/tmp/bench 10.0.2.2 {big_port} {big_mib * 1024 * 1024}",
                timeout=300, check=True,
            )
            result["big"] = _parse_kibs(out)

        if loopback_mib > 0:
            lo_total = loopback_mib * 1024 * 1024
            guest.run(
                f"( /tmp/bench -l {LOOPBACK_PORT} {lo_total} "
                f"> /tmp/lo-server.log 2>&1 & )",
                timeout=30,
            )
            time.sleep(0.5)
            last_error = None
            for _ in range(3):
                out = guest.run(
                    f"/tmp/bench 127.0.0.1 {LOOPBACK_PORT} {lo_total}", timeout=300
                )
                result["loopback_client"] = _parse_kibs(out)
                if result["loopback_client"] is not None:
                    break
                last_error = out
                time.sleep(0.5)
            if result["loopback_client"] is None:
                raise RuntimeError(f"loopback client failed: {last_error!r}")
            time.sleep(0.3)
            server_out = guest.run("cat /tmp/lo-server.log", timeout=30)
            result["loopback_server"] = _parse_kibs(server_out)
    except Exception as exc:  # keep the log path for diagnosis
        result["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        if guest is not None:
            try:
                guest.send("poweroff -f")
                guest.child.expect(pexpect.EOF, timeout=20)
            except Exception:
                pass
            guest.close()
        for proc, log in servers:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except Exception:
                proc.kill()
            log.close()
    return result
