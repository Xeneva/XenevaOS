#!/usr/bin/env python3
"""Boot XenevaOS and run its native network suite plus the host->guest test.

The guest command is injected through ``/shell.cnf``; the original file is
put back into ``initrd2.img`` (and the copy embedded in ``fat.img``) when the
run finishes, including on Ctrl-C.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
BUILD = HERE / "build"
FAT = REPO / "fat.img"
INITRD = REPO / "initrd2.img"
NETSOCK = REPO / "Process" / "netsystest" / "netsock.exe"
SHELL_CNF = REPO / "Resources" / "resources" / "shell.cnf"
# init.exe built with -D__XENEVA_TERM__. Only that binary reads /shell.cnf.
# A desktop init starts the compositor and ignores the injected command.
TERM_INIT = BUILD / "init-term.exe"
_TERM_MARK = b"skipping compositor"


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


def _mcopy(src: Path, image: Path, dest: str) -> None:
    subprocess.run(["mcopy", "-o", "-i", str(image), str(src), dest], check=True)


def _mcopy_out(image: Path, src: str, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists():
        dest.unlink()
    subprocess.run(["mcopy", "-i", str(image), src, str(dest)], check=True)


def _term_init_stale() -> bool:
    """True when the cached TERM init is missing or older than its sources."""
    if not TERM_INIT.exists() or _TERM_MARK not in TERM_INIT.read_bytes():
        return True
    init_dir = REPO / "Process" / "Init"
    sources = list(init_dir.glob("*.cpp")) + list(init_dir.glob("*.s"))
    if not sources:
        return True
    newest = max(path.stat().st_mtime for path in sources)
    return newest > TERM_INIT.stat().st_mtime


def _build_term_init() -> None:
    """Build a TERM init without leaving the desktop init.exe replaced.

    ``/shell.cnf`` is read only by an init compiled with ``__XENEVA_TERM__``.
    The desktop image built by a normal ``--force-user-apps`` run does not
    define that, so netsock never starts. The build writes
    ``Process/Init/init.exe``; that file is put back afterwards so the next
    desktop pack stays a compositor image.
    """
    init_dir = REPO / "Process" / "Init"
    init_exe = init_dir / "init.exe"
    backup = BUILD / "init-desktop.exe"
    BUILD.mkdir(parents=True, exist_ok=True)
    if init_exe.exists():
        shutil.copy2(init_exe, backup)
    print("netbench: building TERM init so /shell.cnf runs netsock", flush=True)
    try:
        subprocess.run(["make", "clean"], cwd=init_dir, check=True)
        subprocess.run(
            ["make", "BLEED_FLAGS=-D__XENEVA_TERM__", "llvm"],
            cwd=init_dir, check=True,
        )
        data = init_exe.read_bytes()
        if _TERM_MARK not in data:
            raise RuntimeError("built init.exe does not skip the compositor")
        shutil.copy2(init_exe, TERM_INIT)
    finally:
        if backup.exists():
            shutil.copy2(backup, init_exe)


def _build_netsock() -> None:
    lib = REPO / "Libs" / "XEClib" / "libXEClib.a"
    if not lib.exists():
        subprocess.run(["make", "-C", "Libs/XEClib", "llvm"], cwd=REPO, check=True)
    subprocess.run(["make", "-C", "Process/netsystest", "llvm"], cwd=REPO, check=True)


class XenevaGuest:
    def __init__(
        self,
        *,
        firmware: Path,
        qemu: str,
        speed_port: int,
        reps: int,
        speed_mib: int = 8,
        build: bool = False,
        timeout: int = 180,
    ):
        self.firmware = firmware
        self.qemu = qemu
        self.speed_port = speed_port
        self.speed_mib = speed_mib
        self.reps = reps
        self.timeout = timeout
        self.setup_done = False
        self._build = build
        self._injected_reps: int | None = None
        self._saved_init: Path | None = None

    def _ensure_term_init(self) -> None:
        """Swap a TERM init into the image when the packed one is a desktop init."""
        extracted = BUILD / "init-from-image.exe"
        _mcopy_out(INITRD, "::/init.exe", extracted)
        if _TERM_MARK in extracted.read_bytes():
            self._saved_init = None
            return
        if _term_init_stale() or self._build:
            _build_term_init()
        if not TERM_INIT.exists():
            raise RuntimeError("TERM init.exe was not produced")
        self._saved_init = extracted
        _mcopy(TERM_INIT, INITRD, "::/init.exe")

    def setup(self) -> None:
        if not FAT.exists() or not INITRD.exists():
            raise RuntimeError(
                "fat.img/initrd2.img not found; build the XenevaOS image first"
            )
        if not NETSOCK.exists() or self._build:
            _build_netsock()
        if not NETSOCK.exists():
            raise RuntimeError(f"missing guest probe: {NETSOCK}")
        if not shutil.which("mcopy"):
            raise RuntimeError("mtools/mcopy not found")

        BUILD.mkdir(parents=True, exist_ok=True)
        _mcopy(NETSOCK, INITRD, "::/netsock.exe")
        self._ensure_term_init()
        self.setup_done = True
        self._inject_reps(self.reps)

    def _inject_reps(self, reps: int) -> None:
        """Write /shell.cnf and embed it in both images."""
        shell = BUILD / "shell.cnf"
        shell.write_text(
            f"netsock --speed-host 10.0.2.2 --speed-port {self.speed_port} "
            f"--speed-mib {self.speed_mib} --speed-reps {reps}\n",
            encoding="utf-8",
        )
        _mcopy(shell, INITRD, "::/shell.cnf")
        _mcopy(INITRD, FAT, "::/initrd2.img")
        self._injected_reps = reps

    def run_pass(
        self,
        *,
        reps: int,
        pcap_path: Path | None,
        log_path: Path,
        timeout: int | None = None,
    ) -> dict:
        """Run one QEMU boot with the configured shell command."""
        result = {
            "runs": [],
            "median": None,
            "min": None,
            "max": None,
            "loopback": None,
            "suite": None,
            "log": str(log_path),
            "pcap": str(pcap_path) if pcap_path else None,
            "timed_out": False,
            "error": None,
        }
        server = None
        server_log = None
        try:
            if not self.setup_done:
                self.setup()
            if self._injected_reps != reps:
                self._inject_reps(reps)
            if pcap_path:
                pcap_path.parent.mkdir(parents=True, exist_ok=True)
                pcap_path.unlink(missing_ok=True)

            server_log_path = log_path.with_suffix(".speed.log")
            server_log = open(server_log_path, "w", encoding="utf-8")
            server = subprocess.Popen(
                [sys.executable, "-u", str(REPO / "Tools" / "speed-server.py"),
                 "--bind", "0.0.0.0", "--port", str(self.speed_port),
                 "--mib", str(self.speed_mib), "--count", str(reps + 2)],
                stdout=server_log, stderr=subprocess.STDOUT,
            )
            _wait_log(server_log_path, "listening on", server)

            argv = [
                self.qemu,
                "-machine", "virt,gic-version=2,highmem=off",
                "-cpu", "cortex-a72",
                "-m", "1024M",
                "-bios", str(self.firmware),
                "-drive", f"file={FAT},format=raw,if=none,id=blk0",
                "-device", "virtio-blk-pci,drive=blk0,disable-legacy=on",
                "-netdev",
                "user,id=net0,ipv4=on,net=10.0.2.0/24,host=10.0.2.2,"
                "dhcpstart=10.0.2.15,dns=10.0.2.3,ipv6=on,"
                "ipv6-net=fec0::/64,ipv6-host=fec0::2",
                "-device", "virtio-net-pci,netdev=net0",
                "-device", "virtio-rng-pci,disable-legacy=on,id=rng0",
                "-nographic", "-no-reboot",
            ]
            if pcap_path:
                argv += [
                    "-object",
                    f"filter-dump,id=netdump,netdev=net0,file={pcap_path},queue=all",
                ]

            limit = timeout or self.timeout
            early_error = None
            with open(log_path, "w", encoding="utf-8") as log:
                proc = subprocess.Popen(
                    argv, stdout=log, stderr=subprocess.STDOUT,
                )
                deadline = time.time() + limit
                finished = False
                try:
                    while time.time() < deadline:
                        if proc.poll() is not None:
                            break
                        try:
                            text = log_path.read_text(
                                encoding="utf-8", errors="replace"
                            )
                        except FileNotFoundError:
                            text = ""
                        if "RESULT pass=" in text:
                            finished = True
                            # Let the shell settle and QEMU flush the pcap.
                            time.sleep(0.5)
                            break
                        # A desktop init never reads /shell.cnf. Under this
                        # -nographic machine the compositor then faults and
                        # the 240s wait would end with no numbers.
                        if (
                            "Synchronous Exception" in text
                            and "skipping compositor" not in text
                        ):
                            early_error = (
                                "guest faulted before netsock; /shell.cnf "
                                "is only read by a TERM init"
                            )
                            break
                        time.sleep(0.2)
                finally:
                    if proc.poll() is None:
                        proc.terminate()
                        try:
                            proc.wait(timeout=10)
                        except subprocess.TimeoutExpired:
                            proc.kill()
                            proc.wait(timeout=5)
                if not finished:
                    result["timed_out"] = True

            text = log_path.read_text(encoding="utf-8", errors="replace")
            for line in text.splitlines():
                m = re.search(r"perf\.tcp\.host\.rep\.kibs=(\d+)", line)
                if m:
                    result["runs"].append(int(m.group(1)))
                    continue
                m = re.search(r"perf\.tcp\.host\.kibs\.median=(\d+)", line)
                if m:
                    result["median"] = int(m.group(1))
                    continue
                m = re.search(r"perf\.tcp\.host\.kibs\.min=(\d+)", line)
                if m:
                    result["min"] = int(m.group(1))
                    continue
                m = re.search(r"perf\.tcp\.host\.kibs\.max=(\d+)", line)
                if m:
                    result["max"] = int(m.group(1))
                    continue
                m = re.search(r"perf\.tcp\.loopback\.1m=(\d+)", line)
                if m:
                    result["loopback"] = int(m.group(1))
                    continue
                m = re.search(r"RESULT pass=(\d+) fail=(\d+) skip=(\d+)", line)
                if m:
                    result["suite"] = {
                        "pass": int(m.group(1)),
                        "fail": int(m.group(2)),
                        "skip": int(m.group(3)),
                    }
            if not result["runs"] and result["median"] is not None:
                result["runs"] = [result["median"]]
            if not result["runs"]:
                if early_error:
                    result["error"] = early_error
                elif result["timed_out"]:
                    result["error"] = f"QEMU timed out after {limit}s with no results"
                else:
                    result["error"] = "no perf.tcp.host.rep.kibs lines in log"
        except Exception as exc:
            result["error"] = f"{type(exc).__name__}: {exc}"
        finally:
            if server is not None:
                server.terminate()
                try:
                    server.wait(timeout=5)
                except Exception:
                    server.kill()
            if server_log is not None:
                server_log.close()
        return result

    def teardown(self) -> None:
        if not self.setup_done:
            return
        try:
            if self._saved_init is not None and self._saved_init.exists():
                _mcopy(self._saved_init, INITRD, "::/init.exe")
            if SHELL_CNF.exists():
                _mcopy(SHELL_CNF, INITRD, "::/shell.cnf")
            else:
                subprocess.run(
                    ["mdel", "-i", str(INITRD), "::/shell.cnf"], check=False
                )
            _mcopy(INITRD, FAT, "::/initrd2.img")
        finally:
            self.setup_done = False
            self._injected_reps = None
            self._saved_init = None
