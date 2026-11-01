#!/usr/bin/env python3
"""One-command network benchmark: XenevaOS vs an Alpine aarch64 control.

Both guests run under the same QEMU machine/CPU/NIC on the host.  The
harness interleaves rounds, optionally captures QEMU ``filter-dump`` pcaps,
and prints a rich comparison table at the end.  Results are also written as
JSON and CSV under ``Tests/netbench/results/``.

Examples
--------
    python3 Tests/netbench/netbench.py --reps 5 --rounds 3
    python3 Tests/netbench/netbench.py --reps 5 --rounds 3 --pcap
    python3 Tests/netbench/netbench.py --reps 1 --rounds 1 --no-pcap
    python3 Tests/netbench/netbench.py --skip-xeneva --reps 1
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import platform
import shutil
import signal
import socket
import statistics
import subprocess
import sys
import time
import urllib.request
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
DEFAULT_RESULTS = HERE / "results"
DEFAULT_FIRMWARE = Path("/usr/share/edk2/aarch64/QEMU_EFI.fd")
DEFAULT_ISO = HERE / "alpine-virt-aarch64.iso"
ISO_URL = (
    "https://dl-cdn.alpinelinux.org/alpine/v3.22/releases/aarch64/"
    "alpine-virt-3.22.2-aarch64.iso"
)
ISO_SHA256 = "096a0ce0d9cf3489d5d284d4e3c55c499727318b11da41f9de726d0f2e11b50c"

sys.path.insert(0, str(HERE))

import linux_guest
import pcap
import xeneva_guest

try:
    from rich.console import Console
    from rich.panel import Panel
    from rich.table import Table
    from rich.text import Text

    _console = Console(highlight=False)
    _HAVE_RICH = True
except Exception:  # pragma: no cover - fallback for minimal hosts
    _console = None
    _HAVE_RICH = False


def _out(message: str = "") -> None:
    if _HAVE_RICH:
        _console.print(message)
    else:
        print(message)


def _rule(title: str) -> None:
    if _HAVE_RICH:
        _console.rule(title)
    else:
        print(f"\n=== {title} ===")


def _cmd(args: list[str]) -> str:
    try:
        return subprocess.check_output(
            args, text=True, stderr=subprocess.DEVNULL, timeout=10
        ).strip()
    except Exception:
        return ""


def _median(values):
    clean = [v for v in values if v is not None]
    if not clean:
        return None
    return int(round(statistics.median(clean)))


def _mean(values):
    clean = [v for v in values if v is not None]
    if not clean:
        return None
    return int(round(statistics.fmean(clean)))


def _delta(a, b):
    if a is None or b in (None, 0):
        return None
    return (a - b) / b * 100.0


def _pct(value, digits: int = 1) -> str:
    if value is None:
        return "n/a"
    return f"{value:+.{digits}f}%"


def _kib(value) -> str:
    if value is None:
        return "n/a"
    return f"{value:,}"


def _port_free(port: int) -> bool:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.bind(("0.0.0.0", port))
        return True
    except OSError:
        return False
    finally:
        sock.close()


def _require_ports(ports: dict[str, int]) -> None:
    busy = {name: port for name, port in ports.items() if not _port_free(port)}
    if busy:
        detail = ", ".join(f"{name}={port}" for name, port in busy.items())
        raise RuntimeError(f"host ports already in use: {detail}")


def _host_environment() -> dict:
    cpu = ""
    for line in Path("/proc/cpuinfo").read_text(errors="replace").splitlines():
        if line.lower().startswith("model name"):
            cpu = line.split(":", 1)[1].strip()
            break
    mhz = ""
    for line in Path("/proc/cpuinfo").read_text(errors="replace").splitlines():
        if line.lower().startswith("cpu mhz"):
            mhz = line.split(":", 1)[1].strip()
            break
    try:
        load = os.getloadavg()
    except OSError:
        load = (0.0, 0.0, 0.0)
    return {
        "hostname": platform.node(),
        "platform": platform.platform(),
        "cpu": cpu,
        "cpu_count": os.cpu_count(),
        "cpu_mhz": mhz,
        "loadavg": [round(x, 2) for x in load],
        "qemu": _cmd(["qemu-system-aarch64", "--version"]).splitlines()[0]
        if shutil.which("qemu-system-aarch64")
        else "",
    }


def _download_iso(dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    part = dest.with_name(dest.name + ".part")
    digest = hashlib.sha256()
    total = 0
    _out(f"Downloading Alpine ISO:\n  {ISO_URL}\n  -> {dest}")
    try:
        request = urllib.request.Request(
            ISO_URL, headers={"User-Agent": "netbench/1.0"}
        )
        with (
            urllib.request.urlopen(request, timeout=60) as response,
            part.open("wb") as fh,
        ):
            total = int(response.headers.get("Content-Length") or 0)
            done = 0
            last_pct = -1
            while True:
                chunk = response.read(256 * 1024)
                if not chunk:
                    break
                fh.write(chunk)
                digest.update(chunk)
                done += len(chunk)
                if total:
                    pct = done * 100 // total
                    if pct != last_pct:
                        print(
                            f"\r  {pct:3d}%  {done / (1024 * 1024):6.1f} / "
                            f"{total / (1024 * 1024):.1f} MiB",
                            end="", file=sys.stderr, flush=True,
                        )
                        last_pct = pct
        if total:
            print(file=sys.stderr)
        actual = digest.hexdigest()
        if actual != ISO_SHA256:
            raise RuntimeError(
                f"checksum mismatch (expected {ISO_SHA256}, got {actual})"
            )
        part.replace(dest)
    except BaseException:
        part.unlink(missing_ok=True)
        raise


def _ensure_iso(assume_yes: bool) -> Path | None:
    if DEFAULT_ISO.exists():
        return DEFAULT_ISO
    if not assume_yes:
        if not sys.stdin.isatty():
            return None
        answer = input(
            f"Alpine ISO not found at {DEFAULT_ISO}.\n"
            f"Download it now from {ISO_URL} (~77 MiB)? [Y/n] "
        ).strip().lower()
        if answer not in ("", "y", "yes"):
            return None
    try:
        _download_iso(DEFAULT_ISO)
    except Exception as exc:
        _out(f"ISO download failed: {exc}")
        return None
    return DEFAULT_ISO if DEFAULT_ISO.exists() else None


def _resolve_iso(
    arg: str | None, *, allow_download: bool, assume_yes: bool
) -> Path | None:
    if arg:
        path = Path(arg).expanduser()
        return path if path.exists() else None
    env = os.environ.get("NETBENCH_ISO")
    if env:
        path = Path(env).expanduser()
        return path if path.exists() else None
    if DEFAULT_ISO.exists():
        return DEFAULT_ISO
    if allow_download:
        return _ensure_iso(assume_yes)
    return None


def _resolve_firmware(arg: str | None) -> Path:
    if arg:
        return Path(arg).expanduser()
    env = os.environ.get("NETBENCH_FIRMWARE")
    if env:
        return Path(env).expanduser()
    return DEFAULT_FIRMWARE


def _summarize(result: dict) -> dict:
    runs = [int(v) for v in result.get("runs", []) if v is not None]
    return {
        "runs": runs,
        "median": _median(runs),
        "mean": _mean(runs),
        "min": min(runs) if runs else None,
        "max": max(runs) if runs else None,
        "loopback": result.get("loopback") or result.get("loopback_client"),
        "loopback_server": result.get("loopback_server"),
        "guest": result.get("guest", {}),
        "suite": result.get("suite"),
        "error": result.get("error"),
        "log": result.get("log"),
        "pcap": result.get("pcap"),
    }


def _run_xeneva(guest, *, reps: int, pcap_path, log_path: Path) -> dict:
    result = guest.run_pass(
        reps=reps, pcap_path=pcap_path, log_path=log_path
    )
    return _summarize(result)


def _run_linux(args, *, reps: int, pcap_path, log_path: Path) -> dict:
    result = linux_guest.run_linux(
        iso=args.iso,
        firmware=args.firmware,
        qemu=args.qemu,
        reps=reps,
        pcap_path=pcap_path,
        log_path=log_path,
        http_port=args.http_port,
        speed_port=args.speed_port,
        big_port=args.big_port,
        speed_mib=args.speed_mib,
        big_mib=args.big_mib,
        loopback_mib=args.loopback_mib,
        timeout=args.timeout,
    )
    return _summarize(result)


def _order_for(index: int, mode: str) -> list[str]:
    if mode == "xeneva-first":
        return ["xeneva", "linux"]
    if mode == "linux-first":
        return ["linux", "xeneva"]
    return ["xeneva", "linux"] if index % 2 == 0 else ["linux", "xeneva"]


def _print_round(round_no: int, total: int, order: list[str], results: dict) -> None:
    _rule(f"Round {round_no}/{total}  ({' -> '.join(order)})")
    for name, label in (("xeneva", "XenevaOS"), ("linux", "Alpine Linux")):
        if name not in results:
            continue
        res = results[name]
        if res.get("error") == "skipped":
            _out(f"  {label}: skipped")
        elif res.get("error"):
            _out(f"  {label}: ERROR {res['error']}")
        else:
            _out(
                f"  {label}: median {_kib(res['median'])} KiB/s "
                f"(min {_kib(res['min'])}, max {_kib(res['max'])})"
            )


def _throughput_table(rounds: list[dict], speed_mib: int) -> Table:
    table = Table(
        title=f"Throughput — {speed_mib} MiB host -> guest (KiB/s)",
        expand=False,
        padding=(0, 1),
    )
    table.add_column("Guest", style="bold", no_wrap=True)
    for i in range(len(rounds)):
        table.add_column(f"R{i + 1}", justify="right", no_wrap=True)
    table.add_column("Median", justify="right", no_wrap=True)
    table.add_column("Delta vs Linux", justify="right", no_wrap=True)

    xeneva = [r["xeneva"].get("median") for r in rounds]
    linux = [r["linux"].get("median") for r in rounds]
    overall_x = _median(xeneva)
    overall_l = _median(linux)
    show_x = any(r["xeneva"].get("error") != "skipped" for r in rounds)
    show_l = any(r["linux"].get("error") != "skipped" for r in rounds)

    if show_x:
        table.add_row(
            "XenevaOS",
            *[_kib(v) for v in xeneva],
            f"[bold]{_kib(overall_x)}[/bold]",
            f"[bold]{_pct(_delta(overall_x, overall_l))}[/bold]"
            if show_l else "—",
        )
    if show_l:
        table.add_row(
            "Alpine Linux",
            *[_kib(v) for v in linux],
            f"[bold]{_kib(overall_l)}[/bold]",
            "—",
        )
    if show_x and show_l:
        deltas = [_pct(_delta(x, l)) for x, l in zip(xeneva, linux)]
        table.add_row(
            "Delta", *deltas, _pct(_delta(overall_x, overall_l)), "—"
        )
    return table


def _pcap_table(entries: dict, speed_mib: int) -> Table:
    table = Table(
        title=f"Pcap — ACK / window behaviour ({speed_mib} MiB connection)"
    )
    table.add_column("Metric", style="bold")
    table.add_column("XenevaOS", justify="right")
    table.add_column("Alpine Linux", justify="right")

    def row(label, key, fmt=lambda v: "n/a" if v is None else str(v)):
        x = entries.get("xeneva") or {}
        l = entries.get("linux") or {}
        table.add_row(label, fmt(x.get(key)), fmt(l.get(key)))

    row("Rate (MiB/s, pcap overhead)", "rate_mib_s",
        lambda v: "n/a" if v is None else f"{v:.2f}")
    row("Matching connections", "connections",
        lambda v: "n/a" if v is None else f"{v}")
    row("Data packets", "data_pkts", lambda v: "n/a" if v is None else f"{v:,}")
    row("Pure guest ACKs", "ack_count", lambda v: "n/a" if v is None else f"{v:,}")
    row("Bytes per ACK", "bytes_per_ack",
        lambda v: "n/a" if v is None else f"{v:,.0f}")
    row("Advertised window", "window", lambda v: "n/a" if v is None else f"{v:,}")
    row("ACK latency p50 (ms)", "ack_p50_ms",
        lambda v: "n/a" if v is None else f"{v:.3f}")
    row("ACK latency p90 (ms)", "ack_p90_ms",
        lambda v: "n/a" if v is None else f"{v:.3f}")
    row("Gaps > 0.4 ms", "gaps_over_04", lambda v: "n/a" if v is None else f"{v}")
    row("Gap time > 0.4 ms (ms)", "gaps_over_04_ms",
        lambda v: "n/a" if v is None else f"{v:.1f}")
    row("Retransmitted segments", "retransmits", lambda v: "n/a" if v is None else f"{v}")
    row("Window scaling", "wscale_negotiated",
        lambda v: "n/a" if v is None else ("yes" if v else "no"))
    row("SACK", "sack_negotiated",
        lambda v: "n/a" if v is None else ("yes" if v else "no"))
    row("MSS client", "mss_client", lambda v: "n/a" if v is None else str(v))
    row("MSS server", "mss_server", lambda v: "n/a" if v is None else str(v))
    return table


def _print_report(report: dict) -> None:
    env = report["environment"]
    cfg = report["config"]
    rounds = report["rounds"]

    if _HAVE_RICH:
        head = Text()
        head.append("XenevaOS vs Alpine Linux — aarch64 under QEMU TCG\n", style="bold")
        head.append(
            f"Host: {env['cpu'] or 'unknown'} ({env['cpu_count']} CPUs), "
            f"load {env['loadavg']}\n"
        )
        head.append(f"QEMU: {env['qemu'] or 'unknown'}\n")
        head.append(
            f"Mode: {'pcap' if cfg['pcap'] else 'no-pcap'} | "
            f"payload {cfg['speed_mib']} MiB | reps {cfg['reps']} | "
            f"rounds {cfg['rounds']}"
        )
        _console.print(Panel(head, title="netbench", border_style="cyan"))
    else:
        print("XenevaOS vs Alpine Linux — aarch64 under QEMU TCG")
        print(f"Host: {env['cpu']} ({env['cpu_count']} CPUs), load {env['loadavg']}")
        print(f"QEMU: {env['qemu']}")
        print(
            f"Mode: {'pcap' if cfg['pcap'] else 'no-pcap'} | "
            f"payload {cfg['speed_mib']} MiB | reps {cfg['reps']} | "
            f"rounds {cfg['rounds']}"
        )

    if rounds:
        if _HAVE_RICH:
            _console.print(_throughput_table(rounds, cfg["speed_mib"]))
        else:
            print("\nThroughput (KiB/s)")
            for r in rounds:
                print(
                    f"  R{r['round']}: Xeneva {r['xeneva'].get('median')} "
                    f"Linux {r['linux'].get('median')}"
                )

    suite_rows = [
        r["xeneva"].get("suite") for r in rounds if r["xeneva"].get("suite")
    ]
    if suite_rows:
        last = suite_rows[-1]
        _out(
            f"\nXeneva network suite: pass={last['pass']} "
            f"fail={last['fail']} skip={last['skip']}"
        )

    pcap_entries = report.get("pcap") or {}
    if pcap_entries:
        errors = {
            name: metrics["error"]
            for name, metrics in pcap_entries.items()
            if isinstance(metrics, dict) and metrics.get("error")
        }
        if _HAVE_RICH:
            _console.print(_pcap_table(pcap_entries, cfg["speed_mib"]))
        else:
            print("\nPcap")
            for guest, metrics in pcap_entries.items():
                print(f"  {guest}: {metrics}")
        for name, error in errors.items():
            _out(f"  pcap {name}: {error}")

    summary = report["summary"]
    if summary.get("xeneva_overall") is not None or summary.get("linux_overall") is not None:
        _out("\nOverall (median of round medians):")
        if summary.get("xeneva_overall") is not None:
            _out(f"  XenevaOS      {_kib(summary['xeneva_overall'])} KiB/s")
        if summary.get("linux_overall") is not None:
            _out(f"  Alpine Linux  {_kib(summary['linux_overall'])} KiB/s")
        if summary.get("delta_pct") is not None:
            _out(f"  Delta         {_pct(summary['delta_pct'])}")
    if summary.get("xeneva_loopback") or summary.get("linux_loopback"):
        _out(
            f"Loopback 1 MiB: XenevaOS {_kib(summary.get('xeneva_loopback'))} KiB/s, "
            f"Alpine Linux client {_kib(summary.get('linux_loopback'))} KiB/s"
        )
    if report.get("artifacts", {}).get("directory"):
        _out(f"Artifacts: {report['artifacts']['directory']}")


def _write_artifacts(report: dict, out_dir: Path, stamp: str) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    json_path = out_dir / f"netbench-{stamp}.json"
    reps_path = out_dir / f"netbench-{stamp}-reps.csv"
    summary_path = out_dir / f"netbench-{stamp}-summary.csv"
    report["artifacts"] = {
        "directory": str(out_dir),
        "json": str(json_path),
        "reps_csv": str(reps_path),
        "summary_csv": str(summary_path),
    }

    json_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    with reps_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["round", "order", "guest", "rep", "kibs"])
        for r in report["rounds"]:
            for guest in ("xeneva", "linux"):
                for i, value in enumerate(r[guest].get("runs", [])):
                    writer.writerow([r["round"], r["order"], guest, i + 1, value])

    with summary_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["metric", "xeneva", "linux", "delta_pct"])
        summary = report["summary"]
        writer.writerow(
            [
                "median_of_rounds_kibs",
                summary["xeneva_overall"],
                summary["linux_overall"],
                summary["delta_pct"],
            ]
        )
        writer.writerow(
            [
                "loopback_1m_kibs",
                summary.get("xeneva_loopback"),
                summary.get("linux_loopback"),
                None,
            ]
        )
        for metric in (
            "rate_mib_s", "ack_count", "bytes_per_ack", "ack_p50_ms",
            "gaps_over_04", "gaps_over_04_ms", "retransmits",
        ):
            writer.writerow(
                [
                    f"pcap_{metric}",
                    (report.get("pcap", {}).get("xeneva") or {}).get(metric),
                    (report.get("pcap", {}).get("linux") or {}).get(metric),
                    None,
                ]
            )


def _build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--pcap", action="store_true",
                      help="also run one pcap pass per guest (default: no pcap)")
    mode.add_argument("--no-pcap", action="store_true",
                      help="throughput only, no pcap (default)")
    ap.add_argument("--pcap-only", action="store_true",
                    help="skip the throughput rounds and only capture pcaps")
    ap.add_argument("--reps", type=int, default=5,
                    help="host->guest transfers per guest per round (default: 5)")
    ap.add_argument("--rounds", type=int, default=3,
                    help="interleaved rounds (default: 3)")
    ap.add_argument("--pcap-reps", type=int, default=1,
                    help="transfers in the pcap pass (default: 1)")
    ap.add_argument("--speed-mib", type=int, default=8,
                    help="payload size in MiB (default: 8)")
    ap.add_argument("--big-mib", type=int, default=0,
                    help="optional extra payload test size in MiB (default: 0)")
    ap.add_argument("--loopback-mib", type=int, default=1,
                    help="loopback payload in MiB (default: 1)")
    ap.add_argument("--order", choices=["alternate", "xeneva-first", "linux-first"],
                    default="alternate",
                    help="guest order per round (default: alternate)")
    ap.add_argument("--skip-xeneva", action="store_true")
    ap.add_argument("--skip-linux", action="store_true")
    ap.add_argument("--build-xeneva", action="store_true",
                    help="rebuild netsock.exe before the run")
    ap.add_argument("--iso", default=None,
                    help="Alpine aarch64 ISO (default: $NETBENCH_ISO or "
                         "Tests/netbench/alpine-virt-aarch64.iso)")
    ap.add_argument("-y", "--yes", action="store_true",
                    help="download the Alpine ISO without prompting if missing")
    ap.add_argument("--firmware", default=None,
                    help="aarch64 UEFI firmware (default: $NETBENCH_FIRMWARE)")
    ap.add_argument("--qemu", default="qemu-system-aarch64")
    ap.add_argument("--http-port", type=int, default=8000)
    ap.add_argument("--speed-port", type=int, default=9000)
    ap.add_argument("--big-port", type=int, default=9002)
    ap.add_argument("--timeout", type=int, default=240,
                    help="per-boot timeout in seconds (default: 240)")
    ap.add_argument("--output-dir", default=str(DEFAULT_RESULTS))
    ap.add_argument("--no-artifacts", action="store_true",
                    help="do not write JSON/CSV files")
    return ap


def main(argv: list[str] | None = None) -> int:
    ap = _build_parser()
    args = ap.parse_args(argv)

    if args.reps < 1 or args.rounds < 0 or args.pcap_reps < 1:
        ap.error("--reps and --pcap-reps must be >= 1, --rounds must be >= 0")
    if args.speed_mib < 1:
        ap.error("--speed-mib must be >= 1")
    if args.skip_xeneva and args.skip_linux:
        ap.error("both guests are skipped")

    pcap_enabled = args.pcap or args.pcap_only
    if args.pcap_only and args.no_pcap:
        ap.error("--pcap-only cannot be combined with --no-pcap")
    if args.pcap_only:
        args.rounds = 0

    args.firmware = _resolve_firmware(args.firmware)
    if not args.firmware.exists():
        ap.error(f"UEFI firmware not found: {args.firmware}")
    if not shutil.which(args.qemu):
        ap.error(f"QEMU not found: {args.qemu}")

    args.iso = _resolve_iso(
        args.iso,
        allow_download=not args.skip_linux,
        assume_yes=args.yes,
    )
    if not args.skip_linux:
        if args.iso is None or not args.iso.exists():
            ap.error(
                "Alpine ISO not found; pass --iso PATH, set NETBENCH_ISO, or "
                "run with --yes to download it automatically "
                f"({ISO_URL})"
            )

    ports = {
        "http": args.http_port,
        "speed": args.speed_port,
    }
    if args.big_mib > 0:
        ports["big"] = args.big_port
    _require_ports(ports)

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = Path(args.output_dir).expanduser()
    run_dir = out_dir / f"run-{stamp}"
    run_dir.mkdir(parents=True, exist_ok=True)

    environment = _host_environment()
    config = {
        "pcap": pcap_enabled,
        "pcap_only": args.pcap_only,
        "reps": args.pcap_reps if args.pcap_only else args.reps,
        "rounds": args.rounds,
        "pcap_reps": args.pcap_reps,
        "speed_mib": args.speed_mib,
        "big_mib": args.big_mib,
        "loopback_mib": args.loopback_mib,
        "order": args.order,
        "iso": str(args.iso) if args.iso else None,
        "firmware": str(args.firmware),
        "qemu": args.qemu,
        "skip_xeneva": args.skip_xeneva,
        "skip_linux": args.skip_linux,
    }

    report = {
        "generated": datetime.now().isoformat(timespec="seconds"),
        "config": config,
        "environment": environment,
        "rounds": [],
        "pcap": {},
        "pcap_runs": {},
        "summary": {},
        "artifacts": {},
    }

    xeneva = None
    if not args.skip_xeneva:
        xeneva = xeneva_guest.XenevaGuest(
            firmware=args.firmware,
            qemu=args.qemu,
            speed_port=args.speed_port,
            reps=args.reps,
            speed_mib=args.speed_mib,
            build=args.build_xeneva,
            timeout=args.timeout,
        )

    def _sigterm(_signum, _frame):
        raise KeyboardInterrupt

    try:
        signal.signal(signal.SIGTERM, _sigterm)
    except ValueError:
        pass

    try:
        if not args.pcap_only and args.rounds > 0:
            for index in range(args.rounds):
                order = [
                    name for name in _order_for(index, args.order)
                    if not (name == "xeneva" and args.skip_xeneva)
                    and not (name == "linux" and args.skip_linux)
                ]
                if not order:
                    continue
                _require_ports(ports)
                round_result = {
                    "round": index + 1,
                    "order": " -> ".join(order),
                    "xeneva": {"runs": [], "median": None, "error": "skipped"},
                    "linux": {"runs": [], "median": None, "error": "skipped"},
                }
                for name in order:
                    if name == "xeneva":
                        _out(f"Round {index + 1}: booting XenevaOS ...")
                        round_result["xeneva"] = _run_xeneva(
                            xeneva,
                            reps=args.reps,
                            pcap_path=None,
                            log_path=run_dir / f"r{index + 1}-xeneva.log",
                        )
                    else:
                        _out(f"Round {index + 1}: booting Alpine Linux ...")
                        round_result["linux"] = _run_linux(
                            args,
                            reps=args.reps,
                            pcap_path=None,
                            log_path=run_dir / f"r{index + 1}-linux.log",
                        )
                    time.sleep(1.0)
                report["rounds"].append(round_result)
                _print_round(index + 1, args.rounds, order, round_result)

        if pcap_enabled:
            _rule(f"Pcap pass ({args.pcap_reps} rep per guest)")
            pcap_paths = {}
            for name in ("xeneva", "linux"):
                if name == "xeneva" and args.skip_xeneva:
                    continue
                if name == "linux" and args.skip_linux:
                    continue
                _require_ports(ports)
                path = run_dir / f"pcap-{name}.pcap"
                _out(f"Pcap: booting {'XenevaOS' if name == 'xeneva' else 'Alpine Linux'} ...")
                if name == "xeneva":
                    pcap_run = _run_xeneva(
                        xeneva,
                        reps=args.pcap_reps,
                        pcap_path=path,
                        log_path=run_dir / "pcap-xeneva.log",
                    )
                else:
                    pcap_run = _run_linux(
                        args,
                        reps=args.pcap_reps,
                        pcap_path=path,
                        log_path=run_dir / "pcap-linux.log",
                    )
                report["pcap_runs"][name] = pcap_run
                pcap_paths[name] = path
                time.sleep(1.0)

            for name, path in pcap_paths.items():
                if not path.exists():
                    report["pcap"][name] = {"error": "pcap file missing"}
                    continue
                try:
                    entries = pcap.analyze_pcap(
                        path, expected_bytes=args.speed_mib * 1024 * 1024
                    )
                    aggregated = pcap.aggregate(entries)
                    if aggregated is None:
                        report["pcap"][name] = {
                            "error": "no matching 8 MiB connection in pcap"
                        }
                    else:
                        report["pcap"][name] = aggregated
                except Exception as exc:
                    report["pcap"][name] = {
                        "error": f"{type(exc).__name__}: {exc}"
                    }
    except KeyboardInterrupt:
        _out("\nInterrupted; restoring images and writing partial results ...")
    finally:
        if xeneva is not None:
            xeneva.teardown()

    round_medians_x = [r["xeneva"].get("median") for r in report["rounds"]]
    round_medians_l = [r["linux"].get("median") for r in report["rounds"]]
    overall_x = _median(round_medians_x)
    overall_l = _median(round_medians_l)
    report["summary"] = {
        "xeneva_round_medians": round_medians_x,
        "linux_round_medians": round_medians_l,
        "xeneva_overall": overall_x,
        "linux_overall": overall_l,
        "delta_pct": _delta(overall_x, overall_l),
        "xeneva_loopback": next(
            (r["xeneva"].get("loopback") for r in report["rounds"]
             if r["xeneva"].get("loopback")), None
        ),
        "linux_loopback": next(
            (r["linux"].get("loopback") for r in report["rounds"]
             if r["linux"].get("loopback")), None
        ),
    }

    if not args.no_artifacts:
        _write_artifacts(report, run_dir, stamp)
    _print_report(report)

    failed = False
    for round_result in report["rounds"]:
        for name in ("xeneva", "linux"):
            error = round_result[name].get("error")
            if error and error != "skipped":
                failed = True
        suite = round_result["xeneva"].get("suite")
        if suite and suite.get("fail"):
            failed = True
    for run_result in report.get("pcap_runs", {}).values():
        if run_result.get("error"):
            failed = True
        suite = run_result.get("suite")
        if suite and suite.get("fail"):
            failed = True
    for metrics in report.get("pcap", {}).values():
        if isinstance(metrics, dict) and metrics.get("error"):
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
