# Bluetooth

The AArch64 kernel speaks Bluetooth through UART1 using a Zephyr-style
serial bridge, with the in-tree mock controller as a fallback.

## Serial bridge (real hardware, preferred)

Pipe the host Bluetooth stack (BlueZ) into the guest's emulated serial
line, exactly like Zephyr's QEMU setup:

Host (once per boot):

```sh
sudo btproxy -u -i 0          # listens on /tmp/bt-server-bredr
```

`btproxy` opens `HCI_CHANNEL_USER` when QEMU connects. That bind only
succeeds while the adapter is down. `bluetoothd` turns it back on as
soon as a client disconnects, and the next connection then logs
`No controller available: Device or resource busy`. The launch script
powers `hci0` off over D-Bus before attaching the socket. A disconnect
also logs `Error from host descriptor`; that is `btproxy` seeing the
UNIX client hang up, not a failed controller.

QEMU (handled by `Scripts/Linux/build_and_run_qemu.sh`):

```sh
-serial stdio -serial unix:/tmp/bt-server-bredr
```

The first `-serial` is UART0 (console). The second `-serial` creates
QEMU `virt`'s second NonSecure PL011 (UART1 @ `0x09040000`, IRQ 8),
which only exists when the backend is given. Guest UART1 speaks raw H4
(`0x01` CMD, `0x02` ACL, `0x03` SCO, `0x04` EVT, `0x05` ISO) to `btproxy`,
which owns the host adapter via `HCI_CHANNEL_USER`.

`Scripts/Linux/bt_bridge.py` is a helper around this flow: it execs
`btproxy` when present, otherwise bridges `HCI_CHANNEL_USER` to the
UNIX socket itself (`--python`). `build_and_run_qemu.sh` auto-attaches
the second serial when the socket exists (`--bt-serial=PATH`,
`--no-bt`, or `XENEVA_BT_SERIAL` override); without the socket the
guest falls back to the mock below.

`0x00` pad bytes at startup are ignored (Zephyr's `btproxy -z`
behaviour), and SCO/ISO RX frames the host stack does not use are
consumed silently.

## Mock controller (fallback)

`KernelAA64/Drivers/virtio_bt.c` implements `AuBtUsbOps` with an
in-guest HCI simulator. When UART1 answers `HCI_RESET` the real adapter
is used; otherwise `AuBtInitialize` falls back to the mock
automatically (also when no `virtio-serial` PCI device (`1af4:1043`)
is found).

The mock responds to every HCI command the BT stack sends (`HCI_RESET`,
`HCI_READ_VER`, `LE_SET_SCAN_EN`, `LE_CREATE_CONN`, etc.) with correct
`0x0E`/`0x0F` events and simulates:

| Feature | What the mock provides |
| --- | --- |
| Identity | BD address `01:00:00:00:00:02`, BT 5.0, ISO support |
| LE scan | Two advertisers: `XenevaSink` (`11:22:33:44:55:01`), `XenevaHead` (`11:22:33:44:55:02`) |
| Connection | `LE_CREATE_CONN` → `LE_CONN_COMPLETE` on handle `0x0001` |
| ATT | Read By Type for Device Name (UUID `0x2A00`) returns `XenevaSink` |
| Pairing | LTK reply/neg acknowledged |
| LE Audio | ISO buffer sizes report 40-octet SDUs (16 kHz / 10 ms) |

## `/dev/bt0` and `btctl`

`btctl` opens `/dev/bt0`.

| Command | What it does |
| --- | --- |
| `btctl info` | Local address and `iso=0` or `iso=1` |
| `btctl scan` | LE advertisers, address, RSSI, name |
| `btctl connect AA:BB:CC:DD:EE:FF` | Connect. Append `r` for a random address |
| `btctl disconnect` | Disconnect |
| `btctl name` | Read the peer Device Name over ATT |
| `btctl pair` | Pair. Legacy Just Works, or LE Secure Connections when the peer asks for it |
| `btctl confirm` | Accept the six-digit number from `pair` |
| `btctl passkey 123456` | Enter a six-digit passkey |
| `btctl audio` | Start unicast audio, or print `no LE Audio` |

Bonds are stored in `/bt/bonds.bin`. A later connection to the same device
encrypts with the stored long-term key. Resolvable private addresses are
matched with the stored identity resolving key.

`/bt/btsnoop.log` is a BTSnoop capture (H4 datalink type) for Wireshark.

## UART

The HCI host accepts H4 bytes: `0x01` command, `0x02` ACL, `0x03` SCO,
`0x04` event, `0x05` ISO. `AuBtH4Push` feeds one byte (interrupt-driven
second UART on boards that have one); on QEMU `virt` the same UART1 is
polled. The console UART (UART0) is never used for this. A board with a
second UART (Raspberry Pi PL011, i.MX UART4, QEMU `virt` UART1) is the
attachment point.

## LE Audio

`btctl audio` prints `no LE Audio` when the controller reports no ISO
packets. Scan, GATT, and pairing still work.

When ISO is present the host sets a CIG, creates a CIS, configures the
16 kHz / 10 ms / 40-octet unicast path, and registers a sound card named
`ble0`. PCM written to that card is one 40-octet frame per ISO SDU. The
frame is produced by the in-tree encoder for that configuration
(`tests/bt` checks that the frame is stable and that decoding it preserves
the signal energy). A controller without an ISO pipe never registers the
card.
