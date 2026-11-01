# netbench — XenevaOS vs Alpine Linux (aarch64)

One command boots both guests under the same QEMU machine, CPU and NIC,
interleaves the throughput rounds, optionally captures QEMU `filter-dump`
pcaps, and prints a comparison table. Both guests are **ARM64**, emulated by
QEMU TCG on the x86-64 host — this is not native ARM hardware.

## Layout

| File | Purpose |
|---|---|
| `netbench.py` | Single entry point: arguments, interleaved rounds, pcap pass, comparison, JSON/CSV |
| `linux_guest.py` | Boots the Alpine control, downloads the probe, runs host→guest and loopback tests |
| `xeneva_guest.py` | Injects `/shell.cnf`, boots XenevaOS, parses the native suite and speed runs, restores images |
| `pcap.py` | Minimal pcap reader and TCP ACK/window/stall/retransmit extractor |
| `tcpbench.c` | Static aarch64 probe used by the Linux guest (64 KiB reads) |

## Prerequisites

Host packages:

```bash
# Debian/Ubuntu names; adjust for your distribution
sudo apt install qemu-system-arm qemu-efi-aarch64 mtools gcc-aarch64-linux-gnu
python3 -m pip install --user pexpect rich
```

`rich` is optional; the harness falls back to plain text. `tabulate` is not
required.

The Alpine control ISO (3.22.2 aarch64) lives in this folder:

```text
Tests/netbench/alpine-virt-aarch64.iso
```

On first use the harness asks whether to download it from the official CDN
(~77 MiB) and verifies its SHA-256. Pass `--yes` to download without a
prompt (useful in CI).

```text
source:  https://dl-cdn.alpinelinux.org/alpine/v3.22/releases/aarch64/alpine-virt-3.22.2-aarch64.iso
sha256:  096a0ce0d9cf3489d5d284d4e3c55c499727318b11da41f9de726d0f2e11b50c
```

The lookup order is:

1. `--iso PATH`
2. `$NETBENCH_ISO`
3. `Tests/netbench/alpine-virt-aarch64.iso` (prompt to download if missing)

The ISO is excluded by the repository's `*.iso` ignore rule. The UEFI
firmware defaults to `/usr/share/edk2/aarch64/QEMU_EFI.fd` (override with
`--firmware` or `$NETBENCH_FIRMWARE`).

The XenevaOS side needs a built `fat.img`/`initrd2.img` and
`Process/netsystest/netsock.exe`. The harness copies `netsock.exe` and a
temporary `/shell.cnf` into the images itself. Rebuild the probe with
`--build-xeneva`, or build the whole tree first:

```bash
make -C Libs/XEClib clean && make -C Libs/XEClib llvm
make -C Process/netsystest clean && make -C Process/netsystest llvm
make -C KernelAA64 BLEED=0 GPUSRV=0 SOAK=0 DEBUG_ALLOC=0 STATIC_COMPONENTS=0 llvm
```

Then deploy the kernel and initrd as usual (`Scripts/Linux/build_and_run_qemu.sh`
does this). Ports **8000, 9000 and 9002** must be free; the harness checks
before each boot.

## Usage

Canonical throughput comparison (no pcap, 5 reps, 3 interleaved rounds):

```bash
python3 Tests/netbench/netbench.py --reps 5 --rounds 3
```

First run on a fresh checkout (downloads the ISO if it is missing):

```bash
python3 Tests/netbench/netbench.py --yes --reps 5 --rounds 3
```

Same run plus one pcap capture per guest:

```bash
python3 Tests/netbench/netbench.py --reps 5 --rounds 3 --pcap
```

Pcap only (no throughput rounds):

```bash
python3 Tests/netbench/netbench.py --pcap-only --pcap-reps 1
```

Quick smoke test:

```bash
python3 Tests/netbench/netbench.py --reps 1 --rounds 1 --no-pcap
```

One guest only:

```bash
python3 Tests/netbench/netbench.py --skip-xeneva --reps 5 --rounds 1
python3 Tests/netbench/netbench.py --skip-linux  --reps 5 --rounds 1
```

### Important options

| Option | Default | Meaning |
|---|---|---|
| `--reps N` | `5` | host→guest transfers per guest per round |
| `--rounds N` | `3` | interleaved A/B rounds |
| `--pcap` | off | run the throughput rounds, then one pcap pass per guest |
| `--no-pcap` | on | throughput only |
| `--pcap-only` | off | skip throughput; capture pcaps only |
| `--pcap-reps N` | `1` | transfers in the pcap pass |
| `--iso PATH` | auto | Alpine ISO; defaults to this folder |
| `-y`, `--yes` | off | download the ISO without prompting |
| `--speed-mib N` | `8` | payload size per transfer |
| `--order` | `alternate` | `alternate`, `xeneva-first`, or `linux-first` |
| `--skip-xeneva` / `--skip-linux` | off | run only one guest |
| `--timeout N` | `240` | per-boot timeout in seconds |
| `--output-dir PATH` | `Tests/netbench/results` | artifact directory |
| `--no-artifacts` | off | do not write JSON/CSV |

## What the run does

For each round, the harness boots one guest, runs `--reps` transfers, shuts
it down, then boots the other guest and does the same. The order alternates
by default (`xeneva -> linux`, then `linux -> xeneva`, …) to cancel warm-up
and host-drift bias. The reported per-round number is the median of that
boot's transfers; the overall number is the **median of the round medians**,
not a pooled mean.

Both guests use the same path:

- QEMU `virt,gic-version=2,highmem=off`, `cortex-a72`, 1024 MiB.
- `virtio-net-pci` behind QEMU slirp user networking.
- Host source: `Tools/speed-server.py`, fixed 8 MiB per connection.
- Guest read buffer: 64 KiB.
- Timing starts after `connect()`.

The Linux guest additionally runs a 1 MiB loopback test. XenevaOS's native
network suite (`netsystest`) runs on every Xeneva boot and its
`pass/fail/skip` line is shown; a non-zero `fail` makes the harness exit
non-zero.

The pcap pass is deliberately separate: QEMU's `filter-dump` adds host
overhead, so its throughput is **not** comparable to the no-pcap numbers.
Use it for ACK counts, bytes per ACK, advertised window, ACK latency, stalls
and retransmits.

## Output

The comparison table shows:

- per-round median throughput for each guest;
- median of the round medians;
- min/max round median;
- percentage delta (XenevaOS vs Alpine Linux);
- pcap ACK/window metrics when `--pcap` is used;
- XenevaOS suite result and loopback numbers.

Artifacts are written to `Tests/netbench/results/run-<timestamp>/`:

```text
netbench-<timestamp>.json          full report (config, environment, rounds, pcap)
netbench-<timestamp>-reps.csv      one row per guest/round/rep
netbench-<timestamp>-summary.csv   overall and pcap summary
r<N>-xeneva.log / r<N>-linux.log   serial logs
pcap-xeneva.pcap / pcap-linux.pcap captures (when enabled)
```

`Tests/netbench/build/` and `Tests/netbench/results/` are git-ignored.

## Interpretation notes

- The host is a shared x86-64 machine running QEMU TCG. Load average, CPU
  frequency and the `powersave` governor move the numbers a lot. Run on an
  idle host, use at least 3 rounds, and trust the interleaved
  median-of-medians more than a single boot.
- Slirp's SYN-ACK does not return window scaling, so the effective window is
  **65,535 bytes** on both guests even though both SYN packets offer scaling.
  This is a property of the test network, not of either TCP stack.
- XenevaOS boots run the native network suite before the speed reps; the
  Alpine control does not run an equivalent suite. The payload and read size
  are identical, but this is a small warm-path difference.
- Loopback numbers use different client tools on the two guests (XenevaOS
  `netsystest` vs the Linux `tcpbench` probe), so treat them as indicative.
- XenevaOS needs `virtio-rng-pci` for TLS; the harness includes it. The
  vendored input probe with `virtio-tablet-pci` crashes QEMU and is not used.

## Image restoration

The harness never edits the tracked `Resources/resources/shell.cnf`. It
writes a temporary `Tests/netbench/build/shell.cnf`, copies it into
`initrd2.img`, and copies `initrd2.img` into `fat.img`. On exit (including
Ctrl-C and SIGTERM) it restores the tracked `shell.cnf` into both images.

If the harness is killed with `SIGKILL`, restore manually:

```bash
mcopy -o -i initrd2.img Resources/resources/shell.cnf ::/shell.cnf
mcopy -o -i fat.img initrd2.img ::/initrd2.img
```

## Troubleshooting

| Symptom | Fix |
|---|---|
| `host ports already in use` | Stop whatever owns 8000/9000/9002, or pass `--http-port`/`--speed-port`/`--big-port` |
| `Alpine ISO not found` | Pass `--iso PATH`, set `NETBENCH_ISO`, or use `--yes` to download it |
| `aarch64-linux-gnu-gcc not found` | Install `gcc-aarch64-linux-gnu` |
| `mtools/mcopy not found` | Install `mtools` |
| `missing guest probe` | Build `Process/netsystest/netsock.exe` or pass `--build-xeneva` |
| Xeneva boot times out | Check `r<N>-xeneva.log`; the guest may still be booting, or the host is overloaded. Raise `--timeout` |
| `no matching 8 MiB connection in pcap` | The speed transfer did not complete; check the pcap pass log and the host server log |
