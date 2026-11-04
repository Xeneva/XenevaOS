#!/usr/bin/env python3
"""Generate the xHCI/USB progress chart for XenevaOS.

The chart follows the visual language of AnyPS5's progress page: one outer
rectangle per subsystem, subdivided into one cell per tracked capability.
Cells are green (implemented and covered by the DCL xHCI gate), amber
(implemented but not exercised by the gate, or only conditionally tested), or
gray (not implemented).

The feature matrix below is deliberately small and curated.  Each feature
carries a code marker that must exist in KernelAA64/Usb/xhci.c and, unless it
is a boot-path or missing feature, a test marker that must exist in
Tests/dcl_test_all.sh or Tests/dcl_input_inject.py.  The generator fails when
a marker disappears, so the chart cannot silently claim coverage that the
gate no longer has.

Usage:
    python3 Tools/xhci-progress.py
    python3 Tools/xhci-progress.py --out Docs/Development
    python3 Tools/xhci-progress.py --check
"""

from __future__ import annotations

import argparse
import datetime as _dt
import json
import re
from dataclasses import dataclass
from html import escape
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SOURCE = ROOT / "KernelAA64" / "Usb" / "xhci.c"
DEFAULT_GATE = ROOT / "Tests" / "dcl_test_all.sh"
DEFAULT_HOST = ROOT / "Tests" / "dcl_input_inject.py"
DEFAULT_OUT = ROOT / "Docs" / "Development"

# AnyPS5-style palette: green = done, amber = partial, gray = missing.
DONE_COLOR = "#2ea043"
PARTIAL_COLOR = "#d29922"
TODO_COLOR = "#6e7681"
BORDER = "#0d1117"
GROUP_BORDER = "#010409"
GROUP_BG = "#161b22"
TEXT = "#ffffff"
MUTED = "#8b949e"
PANEL_BORDER = "#30363d"

PANEL_W = 600
HEADER = 54
MAP_H = 430
FOOTER = 34
GAP = 16
WIDTH = 2 * PANEL_W + GAP
HEIGHT = HEADER + MAP_H + FOOTER

PANELS = (
    (
        "Controller and transport",
        (
            "Bringup",
            "Ports and hubs",
            "Enumeration",
            "Transfers and recovery",
            "Lifecycle",
            "Power management",
        ),
    ),
    (
        "Device classes and verification",
        (
            "HID and input",
            "Bulk-Only storage",
            "UAS storage",
            "Serial",
            "Bluetooth",
            "Other USB classes",
        ),
    ),
)


@dataclass(frozen=True)
class Feature:
    group: str
    name: str
    code: str | None
    test: str | None = None
    kind: str = "boot"  # boot, gate, host, conditional, none
    note: str = ""


# ---------------------------------------------------------------------------
# Feature matrix
#
# kind="boot" means every xHCI gate boot necessarily exercises the path and
# several assertions depend on it, but there is no single log line that names
# it.  kind="gate" and kind="host" require an exact string in the gate or the
# host input helper.  kind="conditional" is a real test that the gate skips
# when QEMU lacks a test-only device model.  kind="none" is untested code or a
# missing feature.

FEATURES = [
    # ------------------------------------------------------------------
    # Controller and transport
    # ------------------------------------------------------------------
    Feature(
        "Bringup",
        "PCIe discovery and BAR mapping",
        "AuPCIEScanClassIF(0x0c, 0x03, 0x30",
        kind="boot",
        note="Every gate assertion depends on the controller being found and its BAR mapped.",
    ),
    Feature(
        "Bringup",
        "Controller reset and start",
        "wr(caplen, rd(caplen) | 2u);",
        kind="boot",
        note="HCRST is waited out before any ring or slot is touched.",
    ),
    Feature(
        "Bringup",
        "Command and event rings",
        "erst[0] = (uint32_t)evt_phys;",
        kind="boot",
    ),
    Feature(
        "Bringup",
        "MSI-X interrupt handling",
        "AuPCIEAllocMSI(pci, (size_t)spi",
        test="[xhci]: irq delivered",
        kind="gate",
    ),
    Feature(
        "Bringup",
        "Extended capabilities and protocols",
        "xhci_ext_caps((uint32_t)((hcc1 >> 16)",
        test="[xhci]: ext cap 2 protocol",
        kind="gate",
    ),
    Feature(
        "Bringup",
        "32-bit DMA ceiling",
        "dma_ceiling = 0xffffffffull;",
        kind="none",
        note="Implemented, but QEMU advertises 64-bit addressing, so the low-4-GiB path never runs.",
    ),
    Feature(
        "Bringup",
        "Controller page size",
        "page_bytes = controller_page_bytes",
        kind="none",
        note="Implemented, but every test boots with 4 KiB controller pages.",
    ),
    Feature(
        "Bringup",
        "Scratchpad buffers",
        "scratchpad_setup(scratchpad_count(hcc2))",
        kind="none",
        note="QEMU reports zero scratchpads; allocating a non-zero scratchpad array is not exercised.",
    ),
    Feature(
        "Bringup",
        "BIOS legacy handoff",
        "xhci_legacy_handoff",
        kind="none",
        note="Implemented; QEMU exposes no legacy capability, so BIOS handoff never runs.",
    ),
    Feature(
        "Bringup",
        "Debug capability",
        None,
        kind="none",
        note="The xHCI debug capability is not implemented.",
    ),
    Feature(
        "Ports and hubs",
        "Root-port power and reset",
        "wr(base, rd(base) | (1u << 9));",
        kind="boot",
    ),
    Feature(
        "Ports and hubs",
        "Root-port hotplug",
        "static void xhci_port_scan(void)",
        test="[xhci-hotplug]: enumerated port",
        kind="gate",
    ),
    Feature(
        "Ports and hubs",
        "USB 2.0 hub",
        "desc_type = d->speed >= 4 ? 0x2a : HUB_DESC_TYPE;",
        test="[xhci]: hub ready on port",
        kind="gate",
    ),
    Feature(
        "Ports and hubs",
        "USB 3.0 hub",
        "if (d->speed >= 4 && ctrl(d, 0x20, 0x0c",
        kind="none",
        note="The SuperSpeed hub path exists, but the gate attaches only a USB 2.0 usb-hub.",
    ),
    Feature(
        "Ports and hubs",
        "Multi-tier hub scan",
        "for (pass = 0; pass < HUB_MAX_TIER; pass++)",
        test="[xhci-hub]: child enumerated on port 8.8.1",
        kind="gate",
        note="A second hub under usb-hub0 puts a Wacom tablet at 8.8.1, so the route string and log path carry two hub tiers.",
    ),
    Feature(
        "Ports and hubs",
        "Hub port power and reset",
        "static int hub_port_reset",
        test="[xhci-hub]: child enumerated on port",
        kind="gate",
    ),
    Feature(
        "Ports and hubs",
        "Overcurrent recovery",
        "HUB_CHANGE_OVERCURRENT",
        kind="none",
        note="Overcurrent changes are cleared and logged; no test injects an overcurrent event.",
    ),
    Feature(
        "Enumeration",
        "Device descriptor",
        "ctrl(d, 0x80, 6, 0x0100, 0, devdesc, 18, 1)",
        kind="boot",
    ),
    Feature(
        "Enumeration",
        "Configuration descriptor",
        "ctrl(d, 0x80, 6, 0x0200, 0, cfg, cfg_len, 1)",
        kind="boot",
    ),
    Feature(
        "Enumeration",
        "String descriptors",
        "usb_read_strings(d, devdesc)",
        test="[xhci]: product QEMU USB Keyboard",
        kind="gate",
    ),
    Feature(
        "Enumeration",
        "Set configuration",
        "ctrl(d, 0x00, 9, cfgval",
        kind="boot",
    ),
    Feature(
        "Enumeration",
        "Class-driver dispatch",
        "usb_classes[i].probe(d, cfg, cfg_len, cfgval)",
        test="[xhci]: HID keyboard ready on port",
        kind="gate",
    ),
    Feature(
        "Enumeration",
        "Unsupported class handling",
        "has no supported interface",
        test="has no supported interface",
        kind="host",
        note="The input helper attaches usb-audio, waits for the no-driver verdict, then removes it.",
    ),
    Feature(
        "Enumeration",
        "SuperSpeed companion descriptors",
        "usb_log_companions(cfg, cfg_len)",
        test="[xhci]: ss companion ep",
        kind="gate",
    ),
    Feature(
        "Enumeration",
        "Transaction translator for low/full speed",
        "TT Port Number is the hub port",
        kind="none",
        note="TT fields exist for low/full-speed children behind a high-speed hub; the gate's hub is full speed, so the path is unused.",
    ),
    Feature(
        "Enumeration",
        "EP0 max packet update",
        "static int ep0_set_mps",
        test="[xhci]: ep0 max packet 64",
        kind="gate",
    ),
    Feature(
        "Enumeration",
        "Large configuration descriptors",
        "if (cfg_len > sizeof cfg_buf)",
        test="[xhci]: product QEMU USB BIGCFG DISK",
        kind="conditional",
        note="The big-config boot is skipped unless QEMU provides the test-only usb-storage-bigcfg model.",
    ),
    Feature(
        "Transfers and recovery",
        "Control transfers",
        "static int ctrl_locked",
        kind="boot",
    ),
    Feature(
        "Transfers and recovery",
        "Interrupt IN transfers",
        "static int intr_xfer",
        test="[xhci-hid]: poll thread running",
        kind="gate",
    ),
    Feature(
        "Transfers and recovery",
        "Bulk IN transfers",
        "static int bulk_in_locked",
        test="[xhci-msc]: read lba0",
        kind="gate",
    ),
    Feature(
        "Transfers and recovery",
        "Bulk OUT transfers",
        "static int xhci_bulk_out",
        test="[xhci-msc]: write lba0 ok",
        kind="gate",
    ),
    Feature(
        "Transfers and recovery",
        "Transfer timeout handling",
        "transfer timed out",
        kind="none",
        note="Timeouts are logged and the command ring can recover, but no test forces a timed-out transfer.",
    ),
    Feature(
        "Transfers and recovery",
        "Endpoint halt recovery",
        "static int ep_recover_locked",
        kind="none",
        note="Stop/Reset/Set-Dequeue recovery exists, but no test halts an endpoint.",
    ),
    Feature(
        "Transfers and recovery",
        "Command ring abort and restart",
        "static void cmd_ring_abort",
        kind="none",
        note="Ring abort and restart are implemented for stuck commands; no test forces a command timeout.",
    ),
    Feature(
        "Transfers and recovery",
        "Isochronous OUT",
        "static int xhci_iso_out",
        kind="none",
        note="Wired for USB Bluetooth SCO; no test adapter emits isochronous traffic.",
    ),
    Feature(
        "Transfers and recovery",
        "Isochronous IN",
        None,
        kind="none",
        note="Only isochronous OUT exists; there is no isochronous IN ring.",
    ),
    Feature(
        "Lifecycle",
        "Slot allocation and addressing",
        "TRB_ENABLE_SLOT << 10",
        kind="boot",
    ),
    Feature(
        "Lifecycle",
        "Disconnect and release",
        "static void dev_disconnect",
        test="[xhci-hotplug]: disconnect on port",
        kind="gate",
    ),
    Feature(
        "Lifecycle",
        "DMA reclaim and replug reuse",
        "vdisk reuse slot",
        test="[xhci-msc]: vdisk reuse slot",
        kind="host",
    ),
    Feature(
        "Lifecycle",
        "Hotplug while HID polling",
        "xhci_hid_thread",
        test="[xhci-hid]: key 30 down",
        kind="host",
        note="The helper removes the boot keyboard and tablet while the poll thread is running, then decodes input from the replacements.",
    ),
    Feature(
        "Power management",
        "Suspend and resume",
        None,
        kind="none",
        note="Port suspend/resume and U3 transitions are not implemented.",
    ),
    Feature(
        "Power management",
        "USB link power management",
        None,
        kind="none",
        note="USB link power management (LPM) is not implemented.",
    ),
    Feature(
        "Power management",
        "ACPI power transitions",
        None,
        kind="none",
        note="There is no ACPI power-transition handling for the controller.",
    ),
    # ------------------------------------------------------------------
    # Device classes and verification
    # ------------------------------------------------------------------
    Feature(
        "HID and input",
        "Boot keyboard",
        "static void hid_keyboard_report",
        test="[xhci]: HID keyboard ready on port",
        kind="gate",
    ),
    Feature(
        "HID and input",
        "Boot pointer/tablet",
        "static void hid_pointer_report",
        test="[xhci]: HID pointer ready on port",
        kind="gate",
    ),
    Feature(
        "HID and input",
        "HID report descriptor parser",
        "static int hid_parse_report",
        test="[xhci-hid]: report iface",
        kind="gate",
    ),
    Feature(
        "HID and input",
        "Report ID support",
        "p->rep.has_ids",
        test="[xhci-hid]: report iface 0 len 110 fields 4 kind pointer ids 1",
        kind="gate",
        note="QEMU's PenPartner tablet report descriptor carries report IDs; the parser records the ID stream and the device decodes through it.",
    ),
    Feature(
        "HID and input",
        "Generic keyboard decoder",
        "static void hid_keyboard_generic",
        test="[xhci-hid]: key 1E down",
        kind="gate",
    ),
    Feature(
        "HID and input",
        "Generic absolute pointer decoder",
        "static void hid_pointer_generic",
        test="[xhci-hid]: pointer",
        kind="host",
    ),
    Feature(
        "HID and input",
        "Relative pointer and wheel",
        "have_wheel",
        kind="none",
        note="Relative motion and wheel fields are decoded, but the gate's tablet is absolute and has no wheel.",
    ),
    Feature(
        "HID and input",
        "Keyboard LED output",
        "static int hid_set_leds",
        test="[xhci-hid]: leds 2",
        kind="host",
        note="The host helper presses Caps Lock and checks the SET_REPORT path.",
    ),
    Feature(
        "HID and input",
        "Composite HID interfaces",
        "hid_collect(desc, len, ifaces, XHCI_MAX_HID)",
        kind="none",
        note="Multiple HID interfaces per device are collected and armed; the gate devices are single-interface.",
    ),
    Feature(
        "HID and input",
        "HID poll thread",
        "static void xhci_hid_thread",
        test="[xhci-hid]: poll thread running",
        kind="gate",
    ),
    Feature(
        "HID and input",
        "Gamepad and consumer controls",
        None,
        kind="none",
        note="No gamepad, consumer-control, or other HID usage maps to the input API.",
    ),
    Feature(
        "Bulk-Only storage",
        "Bulk-Only CBW/CSW transport",
        "static int msc_transport_locked",
        test="[xhci-msc]: inquiry",
        kind="gate",
    ),
    Feature(
        "Bulk-Only storage",
        "SCSI INQUIRY",
        "static int msc_inquiry",
        test="[xhci-msc]: inquiry",
        kind="gate",
    ),
    Feature(
        "Bulk-Only storage",
        "READ CAPACITY (10/16)",
        "static int msc_capacity",
        test="[xhci-msc]: capacity 3145728 MB x 512 bytes",
        kind="gate",
    ),
    Feature(
        "Bulk-Only storage",
        "READ/WRITE (10/16)",
        "static int msc_rw_blocks",
        test="[xhci-msc]: read lba0 58 45 4E 45",
        kind="gate",
    ),
    Feature(
        "Bulk-Only storage",
        "64-bit LBA access",
        "cdb[0] = in ? 0x88 : 0x8a",
        test="[xhci-msc]: read last lba ok",
        kind="gate",
    ),
    Feature(
        "Bulk-Only storage",
        "REQUEST SENSE / CHECK CONDITION",
        "static int msc_sense_locked",
        test="[xhci-msc]: sense status 1 key 7 asc 27",
        kind="gate",
    ),
    Feature(
        "Bulk-Only storage",
        "BOT reset recovery",
        "static int msc_reset_locked",
        kind="none",
        note="Reset recovery exists, but no test forces a transport or phase error.",
    ),
    Feature(
        "Bulk-Only storage",
        "REPORT LUNS and multi-LUN",
        "static int msc_report_luns",
        test="[xhci-msc]: report luns 1",
        kind="gate",
    ),
    Feature(
        "Bulk-Only storage",
        "Vdisk reuse across replug",
        "vdisk reuse slot",
        test="[xhci-msc]: vdisk reuse slot",
        kind="host",
    ),
    Feature(
        "Bulk-Only storage",
        "SYNCHRONIZE CACHE",
        "static int msc_sync",
        kind="none",
        note="SYNCHRONIZE CACHE runs after writes and on vdisk flush, but no gate assertion checks the flush result.",
    ),
    Feature(
        "Bulk-Only storage",
        "GPT partition scan",
        "AuVDiskRegister(vd)",
        test="[vdisk]: USB QEMU HARDDISK gpt partition 1 lba 2048",
        kind="gate",
    ),
    Feature(
        "Bulk-Only storage",
        "FAT32 mount",
        "AuVDiskRegister(vd)",
        test="[fat]: USB QEMU HARDDISK mounted at",
        kind="gate",
    ),
    Feature(
        "Bulk-Only storage",
        "SCSI UNMAP/TRIM",
        None,
        kind="none",
        note="SCSI UNMAP/TRIM is not implemented.",
    ),
    Feature(
        "UAS storage",
        "UAS endpoint discovery",
        "static int claim_uas",
        test="[xhci-uas]: inquiry QEMU HARDDISK",
        kind="gate",
    ),
    Feature(
        "UAS storage",
        "Stream context arrays",
        "static void ep_init_streams",
        test="[xhci]: primary stream arrays",
        kind="gate",
    ),
    Feature(
        "UAS storage",
        "Streams enabled",
        "streams enabled, max",
        test="[xhci-uas]: streams enabled",
        kind="gate",
    ),
    Feature(
        "UAS storage",
        "No-stream shared rings",
        "streams off, shared rings",
        test="[xhci-uas]: streams off, shared rings",
        kind="gate",
    ),
    Feature(
        "UAS storage",
        "Command and status IUs",
        "static int uas_transport_locked",
        test="[xhci-uas]: read lba0 58 45 4E 45",
        kind="gate",
    ),
    Feature(
        "UAS storage",
        "Data IUs",
        "static int uas_cmd_out_locked",
        test="[xhci-uas]: write lba0 ok",
        kind="gate",
    ),
    Feature(
        "UAS storage",
        "Multi-LUN discovery",
        'msc_publish_luns(d, m, "uas")',
        test="[xhci-uas]: report luns 1",
        kind="gate",
    ),
    Feature(
        "UAS storage",
        "UAS sense handling",
        "static int msc_uas_cmd_locked",
        test="[xhci-uas]: capacity",
        kind="gate",
        note="The probe's unit-attention retry is what reaches the capacity assertion.",
    ),
    Feature(
        "UAS storage",
        "Stream release on replug",
        "msc_release_uas",
        test="[xhci-uas]: vdisk reuse slot",
        kind="host",
    ),
    Feature(
        "Serial",
        "FTDI probe and init",
        "static int ftdi_init",
        test="[xhci-serial]: FTDI ready",
        kind="gate",
    ),
    Feature(
        "Serial",
        "CDC ACM probe and line coding",
        "static int cdc_init",
        kind="none",
        note="CDC ACM parsing and line coding exist; the gate only attaches QEMU's FTDI clone.",
    ),
    Feature(
        "Serial",
        "ttyUSB VFS node",
        "static void serial_register",
        test="[xhci-serial]: /dev/ttyUSB",
        kind="gate",
    ),
    Feature(
        "Serial",
        "Serial read/write path",
        "static size_t serial_read",
        kind="none",
        note="The VFS read/write callbacks exist, but no test opens /dev/ttyUSB and moves bytes.",
    ),
    Feature(
        "Serial",
        "CDC notifications and flow control",
        None,
        kind="none",
        note="The CDC notification pipe is configured but never polled; flow control is not implemented.",
    ),
    Feature(
        "Bluetooth",
        "USB HCI class probe",
        "static int claim_bt",
        kind="none",
        note="USB HCI class probing exists; no gate test attaches a Bluetooth adapter.",
    ),
    Feature(
        "Bluetooth",
        "HCI event and bulk pipes",
        "d->intr_len = BT_HCI_EVENT_MAX",
        kind="none",
        note="Event and bulk pipes are wired for HCI; untested without a USB adapter.",
    ),
    Feature(
        "Bluetooth",
        "SCO isochronous pipes",
        "static int xhci_iso_out",
        kind="none",
        note="Isochronous OUT is wired for SCO; isochronous IN is missing and no adapter is tested.",
    ),
    Feature(
        "Bluetooth",
        "Kernel Bluetooth bridge",
        "AuBtUsbReady(&bt_ops)",
        kind="none",
        note="The bridge to the kernel Bluetooth stack exists; no USB adapter is tested.",
    ),
    Feature(
        "Bluetooth",
        "USB BT hotplug attach/detach",
        "AuBtUsbDetach()",
        kind="none",
        note="Attach/detach hooks exist; no USB Bluetooth hotplug test.",
    ),
    Feature(
        "Other USB classes",
        "USB audio class",
        None,
        kind="none",
        note="No USB audio class driver.",
    ),
    Feature(
        "Other USB classes",
        "USB network (CDC ECM/NCM)",
        None,
        kind="none",
        note="No CDC ECM/NCM network class driver.",
    ),
    Feature(
        "Other USB classes",
        "USB video class",
        None,
        kind="none",
        note="No USB video class driver.",
    ),
    Feature(
        "Other USB classes",
        "USB printer class",
        None,
        kind="none",
        note="No USB printer class driver.",
    ),
]


# ---------------------------------------------------------------------------
# Feature resolution and summaries


def resolve_features(source: str, gate: str, host: str) -> list[dict]:
    states = []
    for feature in FEATURES:
        if feature.kind not in {"boot", "gate", "host", "conditional", "none"}:
            raise ValueError(f"{feature.name}: unknown kind {feature.kind!r}")
        has_code = feature.code is not None
        if has_code and feature.code not in source:
            raise ValueError(
                f"{feature.name}: code marker missing from KernelAA64/Usb/xhci.c: "
                f"{feature.code!r}"
            )
        if feature.kind in {"gate", "conditional"}:
            if not feature.test:
                raise ValueError(f"{feature.name}: {feature.kind} evidence needs a test marker")
            if feature.test not in gate:
                raise ValueError(
                    f"{feature.name}: gate marker missing from Tests/dcl_test_all.sh: "
                    f"{feature.test!r}"
                )
        elif feature.kind == "host":
            if not feature.test:
                raise ValueError(f"{feature.name}: host evidence needs a test marker")
            if feature.test not in host:
                raise ValueError(
                    f"{feature.name}: host marker missing from Tests/dcl_input_inject.py: "
                    f"{feature.test!r}"
                )
        elif feature.test:
            raise ValueError(f"{feature.name}: {feature.kind} evidence must not set a marker")
        if not has_code:
            state = "todo"
        elif feature.kind in {"boot", "gate", "host"}:
            state = "done"
        else:
            state = "partial"
        states.append(
            {
                "group": feature.group,
                "name": feature.name,
                "state": state,
                "kind": feature.kind,
                "marker": feature.test,
                "code": feature.code,
                "note": feature.note,
            }
        )
    return states


def summarize(records: list[dict], order: tuple[str, ...]) -> dict:
    groups = []
    for name in order:
        cells = [record for record in records if record["group"] == name]
        done = sum(cell["state"] == "done" for cell in cells)
        partial = sum(cell["state"] == "partial" for cell in cells)
        todo = sum(cell["state"] == "todo" for cell in cells)
        groups.append(
            {
                "name": name,
                "done": done,
                "partial": partial,
                "todo": todo,
                "total": len(cells),
                "cells": cells,
            }
        )
    done = sum(group["done"] for group in groups)
    partial = sum(group["partial"] for group in groups)
    todo = sum(group["todo"] for group in groups)
    total = done + partial + todo
    return {
        "done": done,
        "partial": partial,
        "todo": todo,
        "total": total,
        "percent": round(100 * done / total, 1) if total else 0.0,
        "groups": groups,
    }


# ---------------------------------------------------------------------------
# Treemap / SVG


def worst_ratio(row: list[float], side: float) -> float:
    area = sum(row)
    return max(max(side * side * r / area**2, area**2 / (side * side * r)) for r in row)


def place(row: list[float], x: float, y: float, w: float, h: float, rects: list) -> tuple:
    thickness = sum(row) / min(w, h)
    offset = 0.0
    for area in row:
        length = area / thickness
        if w >= h:
            rects.append((x, y + offset, thickness, length))
        else:
            rects.append((x + offset, y, length, thickness))
        offset += length
    if w >= h:
        return x + thickness, y, w - thickness, h
    return x, y + thickness, w, h - thickness


def squarify(values: list[float], x: float, y: float, w: float, h: float) -> list[tuple]:
    total = sum(values)
    if not total:
        return []
    areas = [value * w * h / total for value in values]
    rects: list[tuple] = []
    row: list[float] = []
    while areas:
        side = min(w, h)
        if not row or worst_ratio(row + [areas[0]], side) <= worst_ratio(row, side):
            row.append(areas.pop(0))
            continue
        x, y, w, h = place(row, x, y, w, h, rects)
        row = []
    if row:
        place(row, x, y, w, h, rects)
    return rects


def cells(count: int, x: float, y: float, w: float, h: float) -> list[tuple]:
    if not count or w <= 0 or h <= 0:
        return []
    rows = max(1, min(count, round((count * h / w) ** 0.5)))
    result = []
    for r in range(rows):
        in_row = count // rows + (r < count % rows)
        cw = w / in_row
        result += [(x + c * cw, y + r * h / rows, cw, h / rows) for c in range(in_row)]
    return result


def rect(x: float, y: float, w: float, h: float, color: str, stroke: str = BORDER, sw: float = 0.5) -> str:
    return (
        f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" '
        f'fill="{color}" stroke="{stroke}" stroke-width="{sw}"/>'
    )


def text(
    x: float,
    y: float,
    value: str,
    size: float = 12,
    *,
    fill: str = TEXT,
    weight: str = "normal",
    anchor: str = "start",
    stroke_width: float = 0,
) -> str:
    stroke = (
        f'stroke="{BORDER}" stroke-width="{stroke_width}" paint-order="stroke"'
        if stroke_width
        else ""
    )
    return (
        f'<text x="{x:.2f}" y="{y:.2f}" font-family="sans-serif" font-size="{size}" '
        f'font-weight="{weight}" text-anchor="{anchor}" fill="{fill}" {stroke}>'
        f"{escape(value)}</text>"
    )


def fit_label(value: str, width: float, size: float = 11) -> str:
    max_chars = int((width - 8) / (size * 0.56))
    if max_chars < 4:
        return ""
    if len(value) <= max_chars:
        return value
    return value[: max(1, max_chars - 1)] + "…"


def panel(title: str, data: dict, left: float) -> list[str]:
    parts = [
        text(left + 6, 24, title, 17, weight="bold", stroke_width=3),
        text(
            left + 6,
            42,
            f'{data["percent"]:g}% green · {data["done"]}/{data["total"]} done · '
            f'{data["partial"]} partial · {data["todo"]} missing',
            11,
            fill=MUTED,
        ),
    ]
    groups = [group for group in data["groups"] if group["total"]]
    groups.sort(key=lambda group: (-group["total"], group["name"]))
    boxes = squarify([group["total"] for group in groups], left, HEADER, PANEL_W, MAP_H)
    for group, (x, y, w, h) in zip(groups, boxes):
        cells_for_group = sorted(
            group["cells"],
            key=lambda cell: {"done": 0, "partial": 1, "todo": 2}[cell["state"]],
        )
        states = [cell["state"] for cell in cells_for_group]
        parts.append(
            f'<g><title>{escape(group["name"])}: {group["done"]} done, '
            f'{group["partial"]} partial, {group["todo"]} missing, '
            f'{group["total"]} total</title>'
        )
        parts.append(rect(x, y, w, h, GROUP_BG, stroke=GROUP_BORDER, sw=1.5))
        for index, (cx, cy, cw, ch) in enumerate(
            cells(len(states), x + 1, y + 1, w - 2, h - 2)
        ):
            state = states[index]
            color = {
                "done": DONE_COLOR,
                "partial": PARTIAL_COLOR,
                "todo": TODO_COLOR,
            }[state]
            name = cells_for_group[index]["name"] if index < len(cells_for_group) else ""
            parts.append(
                f'<g><title>{escape(name)} — {state}</title>'
                f"{rect(cx, cy, cw, ch, color, stroke=BORDER, sw=0.4)}</g>"
            )
        parts.append(rect(x, y, w, h, "none", stroke=GROUP_BORDER, sw=1.5))
        label = fit_label(group["name"], w)
        if label and h >= 18:
            parts.append(text(x + 5, y + 14, label, 11, weight="bold", stroke_width=3))
        if h >= 34 and w >= 95:
            parts.append(
                text(
                    x + 5,
                    y + 28,
                    f'{group["done"]}/{group["total"]} green',
                    9,
                    fill=MUTED,
                    stroke_width=2,
                )
            )
        parts.append("</g>")
    return parts


def render_svg(core: dict, classes: dict, generated: str) -> str:
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {WIDTH} {HEIGHT}" '
        f'width="{WIDTH}" height="{HEIGHT}">',
        rect(0, 0, WIDTH, HEIGHT, BORDER, stroke=BORDER, sw=0),
        f'<rect x="0.5" y="0.5" width="{WIDTH - 1}" height="{HEIGHT - 1}" fill="none" '
        f'stroke="{PANEL_BORDER}" stroke-width="1"/>',
    ]
    parts += panel(PANELS[0][0], core, 0)
    parts += panel(PANELS[1][0], classes, PANEL_W + GAP)
    parts.append(
        f'<line x1="{PANEL_W}" y1="0" x2="{PANEL_W}" y2="{HEIGHT - FOOTER}" '
        f'stroke="{PANEL_BORDER}" stroke-width="1"/>'
    )
    legend_y = HEIGHT - 20
    for x, color, label in (
        (12, DONE_COLOR, "green — implemented + gate-tested"),
        (248, PARTIAL_COLOR, "amber — implemented, untested"),
        (462, TODO_COLOR, "gray — missing"),
    ):
        parts.append(rect(x, legend_y - 9, 9, 9, color, stroke=BORDER, sw=0.5))
        parts.append(text(x + 14, legend_y, label, 11, fill=MUTED))
    parts.append(
        text(
            WIDTH - 12,
            legend_y,
            f"generated {generated} · source: xhci.c + dcl_test_all.sh",
            10,
            fill=MUTED,
            anchor="end",
        )
    )
    parts.append("</svg>")
    return "\n".join(parts)


# ---------------------------------------------------------------------------
# HTML


def state_word(state: str) -> str:
    return {"done": "green", "partial": "partial", "todo": "missing"}[state]


def overview_table(core: dict, classes: dict) -> str:
    rows = [
        "<table>",
        "<tr><th>Panel</th><th>Group</th><th>Green</th><th>Partial</th>"
        "<th>Missing</th><th>Total</th><th>Green share</th></tr>",
    ]
    for panel_name, data in ((PANELS[0][0], core), (PANELS[1][0], classes)):
        for group in data["groups"]:
            percent = f'{100 * group["done"] / group["total"]:.0f}%' if group["total"] else "—"
            rows.append(
                f"<tr><td>{escape(panel_name)}</td><td>{escape(group['name'])}</td>"
                f"<td>{group['done']}</td><td>{group['partial']}</td>"
                f"<td>{group['todo']}</td><td>{group['total']}</td><td>{percent}</td></tr>"
            )
    total = core["done"] + classes["done"]
    partial = core["partial"] + classes["partial"]
    todo = core["todo"] + classes["todo"]
    count = core["total"] + classes["total"]
    percent = f"{100 * total / count:.0f}%" if count else "—"
    rows.append(
        f'<tr class="total"><th>All</th><th>Total</th><th>{total}</th>'
        f"<th>{partial}</th><th>{todo}</th><th>{count}</th><th>{percent}</th></tr>"
    )
    rows.append("</table>")
    return "\n".join(rows)


def bar(done: int, partial: int, total: int) -> str:
    completed = 100 * done / total if total else 0
    pending = 100 * partial / total if total else 0
    missing = max(0.0, 100 - completed - pending)
    return (
        f'<span class="bar"><i style="width:{completed:.1f}%"></i>'
        f'<b style="width:{pending:.1f}%"></b>'
        f'<span class="rest" style="width:{missing:.1f}%"></span></span>'
    )


def still_needed(core: dict, classes: dict) -> str:
    rows = ["<h2>Still needed</h2>"]
    for panel_name, data in ((PANELS[0][0], core), (PANELS[1][0], classes)):
        rows.append(f"<h3>{escape(panel_name)}</h3>")
        for group in data["groups"]:
            if not group["total"]:
                continue
            rows.append(
                f'<div class="needrow"><span class="needlabel">{escape(group["name"])}</span>'
                f'{bar(group["done"], group["partial"], group["total"])}'
                f'<span class="neednum">{group["done"]}/{group["total"]}</span></div>'
            )
            items = [
                cell
                for cell in group["cells"]
                if cell["state"] != "done"
            ]
            if not items:
                continue
            rows.append('<ul class="needlist">')
            for cell in items:
                note = f' — {escape(cell["note"])}' if cell["note"] else ""
                rows.append(
                    f'<li><strong>{escape(cell["name"])}</strong> '
                    f'<span class="tag {cell["state"]}">{state_word(cell["state"])}</span>{note}</li>'
                )
            rows.append("</ul>")
    return "\n".join(rows)


def evidence_text(cell: dict) -> str:
    if cell["kind"] == "boot":
        return "exercised by every gate boot"
    if cell["kind"] == "conditional":
        return f"conditional gate check <code>{escape(cell['marker'] or '')}</code>"
    if cell["kind"] == "host":
        return f"host helper check <code>{escape(cell['marker'] or '')}</code>"
    if cell["kind"] == "gate":
        return f"gate assertion <code>{escape(cell['marker'] or '')}</code>"
    return "no test"


def evidence_details(core: dict, classes: dict) -> str:
    rows = []
    for panel_name, data in ((PANELS[0][0], core), (PANELS[1][0], classes)):
        for group in data["groups"]:
            for cell in group["cells"]:
                if cell["state"] != "done":
                    continue
                rows.append(
                    f'<li><strong>{escape(cell["name"])}</strong> '
                    f'<span class="muted">({escape(panel_name)} / {escape(group["name"])})</span>'
                    f' — {evidence_text(cell)}</li>'
                )
    if not rows:
        return ""
    return (
        '<details><summary>Green features and evidence ('
        + str(len(rows))
        + ")</summary><ul>"
        + "".join(rows)
        + "</ul></details>"
    )


def render_html(core: dict, classes: dict, generated: str) -> str:
    total = core["done"] + classes["done"]
    partial = core["partial"] + classes["partial"]
    todo = core["todo"] + classes["todo"]
    count = core["total"] + classes["total"]
    percent = f"{100 * total / count:.1f}%" if count else "0%"
    return "\n".join(
        [
            "<!DOCTYPE html>",
            '<html lang="en"><head><meta charset="utf-8">',
            '<meta name="viewport" content="width=device-width, initial-scale=1">',
            "<title>XenevaOS xHCI/USB progress</title>",
            "<style>",
            "body{font-family:system-ui,sans-serif;max-width:1280px;margin:auto;padding:24px;"
            "background:#f6f8fa;color:#1f2328}",
            "img{width:100%;height:auto;border:1px solid #30363d;border-radius:6px;background:#0d1117}",
            "table{border-collapse:collapse;margin:12px 0 24px}",
            "th,td{border:1px solid #d0d7de;padding:4px 10px}",
            "th{background:#eaeef2}td+td,th+th{text-align:right}",
            "tr.total th{background:#d8dee4}",
            ".muted{color:#57606a}code{font-size:90%}",
            ".tag{display:inline-block;min-width:54px;padding:0 5px;border-radius:3px;"
            "font-size:85%;text-align:center;color:#fff}",
            ".tag.done{background:#2ea043}.tag.partial{background:#d29922}.tag.todo{background:#6e7681}",
            ".needrow{display:grid;grid-template-columns:190px minmax(120px,1fr) 54px;"
            "gap:8px;align-items:center;margin:4px 0}",
            ".needlabel{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}",
            ".neednum{text-align:right;font-variant-numeric:tabular-nums}",
            ".bar{display:flex;height:14px;background:#6e7681;border-radius:2px;overflow:hidden}",
            ".bar i{display:block;background:#2ea043;height:14px}.bar b{display:block;background:#d29922;height:14px}",
            ".bar .rest{display:block;background:#6e7681;height:14px}",
            ".needlist{margin:4px 0 14px 0}.needlist li{margin:2px 0}",
            "details{margin:8px 0 18px}summary{cursor:pointer;font-weight:600}",
            "@media(max-width:900px){.needrow{grid-template-columns:1fr}}",
            "</style></head><body>",
            "<h1>XenevaOS xHCI/USB readiness</h1>",
            '<p class="muted">Generated ' + escape(generated) + " from "
            '<a href="../../KernelAA64/Usb/xhci.c"><code>KernelAA64/Usb/xhci.c</code></a> and '
            '<a href="../../Tests/dcl_test_all.sh"><code>Tests/dcl_test_all.sh</code></a>. '
            f"<strong>{percent} green</strong> ({total}/{count}): {partial} partial and {todo} missing. "
            "Green means implemented and exercised by the DCL xHCI gate; amber means implemented "
            "but untested or only conditionally tested; gray means absent. One cell is a curated "
            "capability, not a line of code, so the percentage is a readiness summary rather than "
            "an automated coverage number.</p>",
            '<img src="xhci-progress.svg" alt="xHCI controller, transport, and device-class readiness treemaps">',
            overview_table(core, classes),
            still_needed(core, classes),
            evidence_details(core, classes),
            '<p><a href="xhci-progress-history.html">Progress history</a> · '
            '<a href="xhci-progress.json">Raw JSON</a></p>',
            '<p class="muted">Edit the feature matrix in <code>Tools/xhci-progress.py</code>, then '
            "rerun <code>python3 Tools/xhci-progress.py</code>. The generator fails if a declared "
            "code or test marker disappears.</p>",
            "</body></html>",
        ]
    )


# ---------------------------------------------------------------------------
# JSON


def json_snapshot(core: dict, classes: dict, generated: str) -> dict:
    panels = []
    for name, data in ((PANELS[0][0], core), (PANELS[1][0], classes)):
        panels.append(
            {
                "name": name,
                "done": data["done"],
                "partial": data["partial"],
                "todo": data["todo"],
                "total": data["total"],
                "percent": data["percent"],
                "groups": [
                    {
                        "name": group["name"],
                        "done": group["done"],
                        "partial": group["partial"],
                        "todo": group["todo"],
                        "total": group["total"],
                    }
                    for group in data["groups"]
                ],
            }
        )
    features = []
    for panel_name, data in ((PANELS[0][0], core), (PANELS[1][0], classes)):
        for group in data["groups"]:
            for cell in group["cells"]:
                features.append(
                    {
                        "panel": panel_name,
                        "group": group["name"],
                        "name": cell["name"],
                        "state": cell["state"],
                        "evidence": {
                            "code": cell["code"],
                            "kind": cell["kind"],
                            "marker": cell["marker"],
                        },
                        "note": cell["note"],
                    }
                )
    return {
        "generated": generated,
        "source": ["KernelAA64/Usb/xhci.c", "Tests/dcl_test_all.sh", "Tests/dcl_input_inject.py"],
        "legend": {
            "done": "implemented and exercised by the DCL xHCI gate",
            "partial": "implemented but untested or only conditionally tested",
            "todo": "not implemented",
        },
        "totals": {
            "done": core["done"] + classes["done"],
            "partial": core["partial"] + classes["partial"],
            "todo": core["todo"] + classes["todo"],
            "total": core["total"] + classes["total"],
            "percent": round(
                100 * (core["done"] + classes["done"]) / (core["total"] + classes["total"]), 1
            )
            if core["total"] + classes["total"]
            else 0.0,
        },
        "panels": panels,
        "features": features,
    }


# ---------------------------------------------------------------------------
# History


HISTORY_JSON = "xhci-progress-history.json"
HISTORY_HTML = "xhci-progress-history.html"
HISTORY_SVG = "xhci-progress-history.svg"


def history_point(date: str, core: dict, classes: dict) -> dict:
    done = core["done"] + classes["done"]
    partial = core["partial"] + classes["partial"]
    todo = core["todo"] + classes["todo"]
    total = core["total"] + classes["total"]
    return {
        "date": date,
        "done": done,
        "partial": partial,
        "todo": todo,
        "total": total,
        "percent": round(100 * done / total, 1) if total else 0.0,
    }


def load_history(path: Path) -> list[dict]:
    if not path.exists():
        return []
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise ValueError(f"cannot read {path}: {exc}") from exc
    if not isinstance(payload, dict) or not isinstance(payload.get("snapshots"), list):
        raise ValueError(f"{path}: expected an object with a snapshots list")
    valid = []
    for index, item in enumerate(payload["snapshots"]):
        if not isinstance(item, dict) or not isinstance(item.get("date"), str):
            raise ValueError(f"{path}: snapshot {index} has no date")
        try:
            _dt.date.fromisoformat(item["date"])
        except ValueError as exc:
            raise ValueError(f"{path}: snapshot {index} has invalid date {item['date']!r}") from exc
        if not all(
            isinstance(item.get(field), (int, float))
            for field in ("done", "partial", "todo", "total", "percent")
        ):
            raise ValueError(f"{path}: snapshot {index} has incomplete stats")
        valid.append(item)
    valid.sort(key=lambda item: item["date"])
    return valid


def merge_history(snapshots: list[dict], point: dict) -> list[dict]:
    by_date = {item["date"]: item for item in snapshots}
    by_date[point["date"]] = point
    return [by_date[date] for date in sorted(by_date)]


def short_date(value: str) -> str:
    try:
        return _dt.date.fromisoformat(value).strftime("%b %d").replace(" 0", " ")
    except (ValueError, AttributeError):
        return value


def nice_max(value: int) -> int:
    if value <= 20:
        step = 5
    elif value <= 100:
        step = 10
    else:
        step = 50
    return max(step, ((value + step - 1) // step) * step)


def history_chart(snapshots: list[dict]) -> str:
    width, height = 900, 300
    left, right = 64, 720
    top, bottom = 34, 230
    panel_h = bottom - top
    count = len(snapshots)
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" '
        f'width="{width}" height="{height}" role="img" '
        f'aria-label="Green, partial, and tracked xHCI features over time" '
        f'font-family="sans-serif" font-size="12" fill="#c9d1d9">',
        '<style>.hit{fill:#fff;fill-opacity:0}.hit:hover{fill-opacity:0.06}</style>',
        '<rect width="900" height="300" rx="6" fill="#0d1117"/>',
    ]

    def x_at(index: int) -> float:
        if count <= 1:
            return (left + right) / 2
        return left + index * (right - left) / (count - 1)

    max_value = nice_max(max((item["total"] for item in snapshots), default=0))
    latest = snapshots[-1] if snapshots else {"done": 0, "partial": 0, "todo": 0, "total": 0, "percent": 0}
    parts.append('<text x="64" y="20" font-size="13" font-weight="bold">xHCI/USB features</text>')
    for step in range(5):
        value = max_value * step / 4
        y = bottom - panel_h * step / 4
        parts.append(f'<line x1="{left}" x2="{right}" y1="{y:.1f}" y2="{y:.1f}" stroke="#30363d"/>')
        parts.append(
            f'<text x="{left - 6}" y="{y + 4:.1f}" text-anchor="end" fill="#8b949e">{value:g}</text>'
        )
    if count:
        tick_indices = [0] if count == 1 else sorted(
            {round(i * (count - 1) / min(5, count - 1)) for i in range(min(6, count))}
        )
        for index in tick_indices:
            x = x_at(index)
            parts.append(
                f'<line x1="{x:.1f}" x2="{x:.1f}" y1="{top}" y2="{bottom}" '
                f'stroke="#30363d" stroke-width="0.5"/>'
            )
            parts.append(
                f'<text x="{x:.1f}" y="{bottom + 18}" text-anchor="middle" '
                f'font-size="11" fill="#8b949e">{escape(short_date(snapshots[index]["date"]))}</text>'
            )
    if count > 1:
        partial_points = " ".join(
            f'{x_at(i):.1f},{bottom - panel_h * item["partial"] / max_value:.1f}'
            for i, item in enumerate(snapshots)
        )
        parts.append(
            f'<polygon points="{left},{bottom} {partial_points} {right},{bottom}" '
            f'fill="{PARTIAL_COLOR}" opacity="0.12"/>'
        )
        done_points = " ".join(
            f'{x_at(i):.1f},{bottom - panel_h * item["done"] / max_value:.1f}'
            for i, item in enumerate(snapshots)
        )
        parts.append(
            f'<polygon points="{left},{bottom} {done_points} {right},{bottom}" '
            f'fill="{DONE_COLOR}" opacity="0.18"/>'
        )
        total_points = " ".join(
            f'{x_at(i):.1f},{bottom - panel_h * item["total"] / max_value:.1f}'
            for i, item in enumerate(snapshots)
        )
        parts.append(
            f'<polyline points="{total_points}" fill="none" stroke="#8b949e" '
            f'stroke-width="2" stroke-dasharray="6 4"/>'
        )
        partial_points = " ".join(
            f'{x_at(i):.1f},{bottom - panel_h * item["partial"] / max_value:.1f}'
            for i, item in enumerate(snapshots)
        )
        parts.append(
            f'<polyline points="{partial_points}" fill="none" stroke="{PARTIAL_COLOR}" stroke-width="2"/>'
        )
        parts.append(
            f'<polyline points="{done_points}" fill="none" stroke="{DONE_COLOR}" stroke-width="2"/>'
        )
    elif count == 1:
        total_y = bottom - panel_h * latest["total"] / max_value
        parts.append(
            f'<line x1="{left}" x2="{right}" y1="{total_y:.1f}" y2="{total_y:.1f}" '
            f'stroke="#8b949e" stroke-width="2" stroke-dasharray="6 4"/>'
        )
    for index, item in enumerate(snapshots):
        x = x_at(index)
        y = bottom - panel_h * item["done"] / max_value
        partial_y = bottom - panel_h * item["partial"] / max_value
        parts.append(f'<circle cx="{x:.1f}" cy="{partial_y:.1f}" r="3.5" fill="{PARTIAL_COLOR}"/>')
        parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3.5" fill="{DONE_COLOR}"/>')
        if count == 1:
            parts.append(
                f'<text x="{x + 8:.1f}" y="{y - 8:.1f}" fill="{DONE_COLOR}" font-weight="bold">'
                f'{item["done"]}/{item["total"]} ({item["percent"]:g}%)</text>'
            )
    if count:
        edges = [left]
        if count > 1:
            edges += [(x_at(i - 1) + x_at(i)) / 2 for i in range(1, count)]
            edges.append(right)
        else:
            edges.append(right)
        for index, item in enumerate(snapshots):
            x0, x1 = edges[index], edges[index + 1]
            parts.append(
                f'<rect class="hit" x="{x0:.1f}" y="{top}" width="{x1 - x0:.1f}" height="{panel_h}">'
                f'<title>{escape(item["date"])}\n'
                f'Green {item["done"]}, partial {item["partial"]}, missing {item["todo"]} '
                f'of {item["total"]} tracked</title></rect>'
            )
    parts.append(f'<text x="{right + 8}" y="{top + 12}" fill="#8b949e">Tracked {latest["total"]}</text>')
    parts.append(
        f'<text x="{right + 8}" y="{top + 30}" fill="#c9d1d9" font-weight="bold">'
        f'<tspan fill="{DONE_COLOR}">●</tspan> Green {latest["done"]} ({latest["percent"]:g}%)</text>'
    )
    parts.append(
        f'<text x="{right + 8}" y="{top + 48}" fill="#c9d1d9" font-weight="bold">'
        f'<tspan fill="{PARTIAL_COLOR}">●</tspan> Partial {latest["partial"]}</text>'
    )
    parts.append(
        f'<text x="{right + 8}" y="{top + 66}" fill="#c9d1d9" font-weight="bold">'
        f'<tspan fill="{TODO_COLOR}">●</tspan> Missing {latest["todo"]}</text>'
    )
    parts.append("</svg>")
    return "\n".join(parts)


def history_table(snapshots: list[dict]) -> str:
    rows = [
        "<table>",
        "<tr><th>Date</th><th>Green</th><th>Partial</th><th>Missing</th>"
        "<th>Tracked</th><th>Green share</th></tr>",
    ]
    for item in reversed(snapshots):
        rows.append(
            f'<tr><td>{escape(item["date"])}</td><td>{item["done"]}</td>'
            f'<td>{item["partial"]}</td><td>{item["todo"]}</td>'
            f'<td>{item["total"]}</td><td>{item["percent"]}%</td></tr>'
        )
    rows.append("</table>")
    return "\n".join(rows)


def render_history_html(snapshots: list[dict], generated: str) -> str:
    note = "Each point is the last run for a UTC day."
    if len(snapshots) <= 1:
        note = (
            "Only one snapshot has been recorded so far. The chart will grow each day this "
            "generator runs; re-running on the same day replaces that day's point."
        )
    return "\n".join(
        [
            "<!DOCTYPE html>",
            '<html lang="en"><head><meta charset="utf-8">',
            '<meta name="viewport" content="width=device-width, initial-scale=1">',
            "<title>XenevaOS xHCI/USB progress history</title>",
            "<style>",
            "body{font-family:system-ui,sans-serif;max-width:1000px;margin:auto;padding:24px;"
            "background:#f6f8fa;color:#1f2328}",
            "svg{max-width:100%;height:auto;border:1px solid #30363d;border-radius:6px}",
            "table{border-collapse:collapse;margin:16px 0}",
            "th,td{border:1px solid #d0d7de;padding:4px 10px}",
            "th{background:#eaeef2}td+td,th+th{text-align:right}",
            ".muted{color:#57606a}",
            "</style></head><body>",
            "<h1>xHCI/USB progress history</h1>",
            history_chart(snapshots),
            f'<p class="muted">Generated {escape(generated)} by '
            '<a href="../../Tools/xhci-progress.py">Tools/xhci-progress.py</a>. '
            f"{escape(note)} Green = implemented and gate-tested, amber = implemented but "
            'untested, dashed gray = tracked total. Raw numbers: '
            f'<a href="{HISTORY_JSON}">{HISTORY_JSON}</a>.</p>',
            history_table(snapshots),
            '<p><a href="xhci-progress.html">Back to current progress</a></p>',
            "</body></html>",
        ]
    )


def write_history(out: Path, snapshots: list[dict], generated: str) -> None:
    (out / HISTORY_SVG).write_text(history_chart(snapshots), encoding="utf-8")
    (out / HISTORY_HTML).write_text(render_history_html(snapshots, generated), encoding="utf-8")
    (out / HISTORY_JSON).write_text(
        json.dumps({"generated": generated, "snapshots": snapshots}, indent=2),
        encoding="utf-8",
    )


# ---------------------------------------------------------------------------
# Output


def write_outputs(out: Path, core: dict, classes: dict, generated: str, snapshots: list[dict]) -> None:
    out.mkdir(parents=True, exist_ok=True)
    (out / "xhci-progress.svg").write_text(render_svg(core, classes, generated), encoding="utf-8")
    (out / "xhci-progress.html").write_text(render_html(core, classes, generated), encoding="utf-8")
    (out / "xhci-progress.json").write_text(
        json.dumps(json_snapshot(core, classes, generated), indent=2),
        encoding="utf-8",
    )
    write_history(out, snapshots, generated)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Generate the xHCI/USB readiness treemap from xhci.c and the DCL gate."
    )
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--gate", type=Path, default=DEFAULT_GATE)
    parser.add_argument("--host", type=Path, default=DEFAULT_HOST)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--check", action="store_true", help="verify markers and print totals; write nothing")
    args = parser.parse_args(argv)

    try:
        source = args.source.read_text(encoding="utf-8")
        gate = args.gate.read_text(encoding="utf-8")
        host = args.host.read_text(encoding="utf-8")
    except OSError as exc:
        print(f"xhci-progress: {exc}")
        return 1

    try:
        records = resolve_features(source, gate, host)
    except ValueError as exc:
        print(f"xhci-progress: {exc}")
        return 1

    declared_groups = [group for _, groups in PANELS for group in groups]
    feature_groups = {record["group"] for record in records}
    unknown = feature_groups - set(declared_groups)
    unused = set(declared_groups) - feature_groups
    if unknown:
        print(f"xhci-progress: features in undeclared groups: {sorted(unknown)}")
        return 1
    if unused:
        print(f"xhci-progress: declared groups without features: {sorted(unused)}")
        return 1
    names = [record["name"] for record in records]
    if len(names) != len(set(names)):
        print("xhci-progress: duplicate feature name")
        return 1

    core = summarize(records, PANELS[0][1])
    classes = summarize(records, PANELS[1][1])
    done = core["done"] + classes["done"]
    partial = core["partial"] + classes["partial"]
    todo = core["todo"] + classes["todo"]
    total = core["total"] + classes["total"]
    percent = round(100 * done / total, 1) if total else 0.0

    if args.check:
        print(
            f"xhci {done}/{total} green, {partial} partial, {todo} missing ({percent:g}%)"
        )
        print(
            f"controller {core['done']}/{core['total']} green; "
            f"classes {classes['done']}/{classes['total']} green"
        )
        return 0

    generated = _dt.datetime.now(_dt.timezone.utc).date().isoformat()
    history_path = args.out / HISTORY_JSON
    try:
        snapshots = merge_history(
            load_history(history_path),
            history_point(generated, core, classes),
        )
    except ValueError as exc:
        print(f"xhci-progress: {exc}")
        return 1

    write_outputs(args.out, core, classes, generated, snapshots)
    print(f"wrote {args.out / 'xhci-progress.svg'}")
    print(f"wrote {args.out / 'xhci-progress.html'}")
    print(f"wrote {args.out / 'xhci-progress.json'}")
    print(f"wrote {args.out / HISTORY_HTML}")
    print(f"history: {len(snapshots)} snapshot(s)")
    print(
        f"xhci {done}/{total} green, {partial} partial, {todo} missing ({percent:g}%)"
    )
    print(
        f"controller {core['done']}/{core['total']} green; "
        f"classes {classes['done']}/{classes['total']} green"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
