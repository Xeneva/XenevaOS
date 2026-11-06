#!/usr/bin/env python3
"""Generate a treemap progress chart for the WebKit PE plan.

The chart follows the visual language of AnyPS5's tools/progress.py:
one outer rectangle per subsystem, subdivided into one cell per syscall or
library.  Cells are green (fully done), amber (partial/prebuilt/external/
headers-only/prototype), or gray (missing).
The SVG, HTML table, JSON snapshot, status badges, and a daily progress-history
page are generated from the tables in Docs/Development/WebKitPE-Plan.md.

Usage:
    python3 Tools/webkit-progress.py
    python3 Tools/webkit-progress.py --out Docs/Development
    python3 Tools/webkit-progress.py --check
"""

from __future__ import annotations

import argparse
import datetime as _dt
import json
import re
import sys
from html import escape
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_PLAN = ROOT / "Docs" / "Development" / "WebKitPE-Plan.md"
DEFAULT_OUT = ROOT / "Docs" / "Development"

# Proposed syscalls (§4.3) with a complete implementation, XEClib entry point
# and timed conformance coverage. sendmsg/recvmsg use the XEClib wrappers over
# SYS_NET_SEND/SYS_NET_RECEIVE; the kernel msghdr path handles iovecs, names,
# flags and validates control buffers. Keep in sync with the status paragraph
# in §H and the syscall table in BaseHdr/Serv/syscall.h.
IMPLEMENTED_NEW_SYSCALLS = frozenset(
    {179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 190, 191, 192, 193}
)
# Calls that exist but are still missing required semantics.
PARTIAL_NEW_SYSCALLS = frozenset()

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
MAP_H = 410
FOOTER = 34
GAP = 16
WIDTH = 2 * PANEL_W + GAP
HEIGHT = HEADER + MAP_H + FOOTER

SYSCALL_GROUP_ORDER = (
    "Files/VFS",
    "Core/process",
    "Time",
    "Network",
    "Threads",
    "Memory",
    "Credentials",
    "Poll/events",
    "Signals",
    "System/legacy",
)

LIBRARY_GROUP_ORDER = (
    "Runtime",
    "Rendering",
    "Data/text",
    "Networking",
    "WebKit/WPE",
    "Images",
    "Optional",
)

LIBRARY_GROUPS = {
    "Runtime": {
        "XEClib",
        "libc++ / libc++abi",
        "compiler-rt / builtins",
        "pthread",
        "libm",
    },
    "Rendering": {"Cairo", "pixman", "FreeType", "HarfBuzz", "Fontconfig"},
    "Data/text": {
        "ICU 76",
        "libxml2",
        "expat",
        "SQLite",
        "zlib",
        "brotli",
        "tzdata",
        "Noto fonts",
    },
    "Networking": {
        "mbedTLS",
        "libcurl + nghttp2 + libpsl + libidn2 + c-ares",
        "CA bundle",
    },
    "WebKit/WPE": {"libwpe", "WPEBackend-xeneva", "WebKit"},
    "Images": {"libpng", "libjpeg-turbo", "libwebp"},
    "Optional": {
        "GStreamer",
        "FFmpeg",
        "libepoxy / EGL / GLES / Mesa",
        "libdrm / GBM",
        "libunwind",
    },
}

LIBRARY_LABELS = {
    "libc++ / libc++abi": "libc++",
    "compiler-rt / builtins": "compiler-rt",
    "libcurl + nghttp2 + libpsl + libidn2 + c-ares": "libcurl stack",
    "libepoxy / EGL / GLES / Mesa": "EGL/GLES",
    "libdrm / GBM": "libdrm/GBM",
    "ICU 76": "ICU",
}

# Override the text heuristic where the plan's wording is not enough.
LIBRARY_STATUS_OVERRIDE = {
    # Kernel DRM stubs are not a userspace libdrm/GBM port.
    "libdrm / GBM": "todo",
}

SKIP_LIBRARIES = {"Wayland"}


def split_row(line: str) -> list[str]:
    return [cell.strip() for cell in line.strip().strip("|").split("|")]


def clean(value: str) -> str:
    return re.sub(r"[`*]", "", value).strip()


def section(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    finish = text.index(end, begin)
    return text[begin:finish]


def parse_baseline(text: str) -> str:
    match = re.search(r"\*\*Baseline:\*\*\s*(.+)", text)
    return clean(match.group(1)) if match else "unknown"


def parse_existing_syscalls(text: str) -> list[dict]:
    part = section(text, "### 4.2 Existing native syscalls", "### 4.3 New native syscalls")
    rows = []
    for line in part.splitlines():
        if not re.match(r"\|\s*\d+\s*\|", line):
            continue
        cells = split_row(line)
        number = int(cells[0])
        name = clean(cells[1])
        raw_status = clean(cells[2]).upper()
        priority = clean(cells[4])
        if raw_status in ("OK", "LEGACY"):
            state = "done"
        elif raw_status == "PARTIAL":
            state = "partial"
        elif raw_status == "STUB":
            state = "todo"
        else:
            raise ValueError(f"unknown syscall status {raw_status!r} for {name}")
        rows.append(
            {
                "number": number,
                "name": name,
                "state": state,
                "status": raw_status,
                "priority": priority,
            }
        )
    if len(rows) != 84:
        raise ValueError(f"expected 84 existing syscalls, found {len(rows)}")
    return rows


def parse_new_syscalls(text: str) -> list[dict]:
    """Parse proposed syscall rows and attach their implementation state.

    The §4.3 table has no status column, so keep the implemented sets here.
    A call is green when its kernel path, XEClib entry point and conformance
    test all exist. sendmsg/recvmsg are implemented as XEClib wrappers over
    SYS_NET_SEND/SYS_NET_RECEIVE; SCM_RIGHTS ancillary data is tracked as a
    separate P1 follow-up rather than blocking the syscall ABI.
    """
    part = section(text, "### 4.3 New native syscalls", "### 4.4 Syscall work packages")
    rows = []
    for line in part.splitlines():
        if not re.match(r"\|\s*\d+\s*\|", line):
            continue
        cells = split_row(line)
        number = int(cells[0])
        if number in IMPLEMENTED_NEW_SYSCALLS:
            state, status = "done", "OK"
        elif number in PARTIAL_NEW_SYSCALLS:
            state, status = "partial", "PARTIAL"
        else:
            state, status = "todo", "TODO"
        rows.append(
            {
                "number": number,
                "name": clean(cells[1]),
                "state": state,
                "status": status,
                "priority": clean(cells[4]),
            }
        )
    if len(rows) != 111:
        raise ValueError(f"expected 111 proposed syscalls, found {len(rows)}")
    return rows


def syscall_group(number: int) -> str:
    if number in (2, 3, 32, 79, 81) or 94 <= number <= 107:
        return "Threads"
    if number in (24, 25, 35, 80) or 108 <= number <= 115:
        return "Signals"
    if number in (23, 26, 36, 45) or 39 <= number <= 42 or 70 <= number <= 74 or 77 <= number <= 78 or 116 <= number <= 123:
        return "Time"
    if number == 83 or 124 <= number <= 133:
        return "Poll/events"
    if 46 <= number <= 53 or 179 <= number <= 188 or 190 <= number <= 193:
        return "Network"
    if 58 <= number <= 67 or 175 <= number <= 178:
        return "Credentials"
    if number in (12, 31, 33, 43, 44, 54, 189, 194) or 16 <= number <= 22 or 37 <= number <= 38 or 134 <= number <= 159:
        return "Files/VFS"
    if 9 <= number <= 11 or 13 <= number <= 15 or number in (30, 34) or 84 <= number <= 93:
        return "Memory"
    if number in (0, 4, 5, 6, 7, 8, 75, 76, 82) or 160 <= number <= 174:
        return "Core/process"
    if number in (1, 27, 28, 29, 55, 56, 57, 68, 69):
        return "System/legacy"
    raise ValueError(f"unassigned syscall number {number}")


def parse_libraries(text: str) -> list[dict]:
    end = "### 6.2.1 Missing-library syscall and toolchain matrix"
    if end not in text:
        end = "### 6.3 HTTP/network stack decision"
    part = section(text, "### 6.2 Library matrix", end)
    rows = []
    for line in part.splitlines():
        if not line.startswith("| **"):
            continue
        cells = split_row(line)
        raw_name = clean(cells[0])
        if raw_name in SKIP_LIBRARIES:
            continue
        state_text = clean(cells[1])
        priority = clean(cells[4])
        if raw_name in LIBRARY_STATUS_OVERRIDE:
            state = LIBRARY_STATUS_OVERRIDE[raw_name]
        elif state_text.startswith("Built"):
            state = "done"
        elif state_text.startswith("Absent") or state_text.startswith("Not present"):
            state = "todo"
        elif "Kernel DRM stubs only" in state_text:
            state = "todo"
        else:
            # External static, prebuilt, prototype, headers-only, embedded,
            # and partial libraries are present but not yet reproducible or
            # fully integrated, so they are amber rather than green.
            state = "partial"
        rows.append(
            {
                "name": raw_name,
                "label": LIBRARY_LABELS.get(raw_name, raw_name),
                "state": state,
                "status_text": state_text,
                "priority": priority,
            }
        )
    if len(rows) != 32:
        raise ValueError(f"expected 32 libraries after excluding Wayland, found {len(rows)}")
    return rows


def group_records(records: list[dict], group_fn, order: tuple[str, ...]) -> list[dict]:
    groups: dict[str, list[dict]] = {}
    for record in records:
        group = group_fn(record)
        groups.setdefault(group, []).append(record)
    missing = set(groups) - set(order)
    if missing:
        raise ValueError(f"unexpected groups: {sorted(missing)}")
    result = []
    for name in order:
        cells = groups.get(name, [])
        done = sum(c["state"] == "done" for c in cells)
        partial = sum(c["state"] == "partial" for c in cells)
        todo = sum(c["state"] == "todo" for c in cells)
        result.append(
            {
                "name": name,
                "done": done,
                "partial": partial,
                "todo": todo,
                "total": len(cells),
                "cells": cells,
            }
        )
    return result


def summarize(groups: list[dict]) -> dict:
    done = sum(g["done"] for g in groups)
    partial = sum(g["partial"] for g in groups)
    todo = sum(g["todo"] for g in groups)
    total = done + partial + todo
    return {
        "done": done,
        "partial": partial,
        "todo": todo,
        "total": total,
        "percent": round(100 * done / total, 1) if total else 0.0,
        "partial_percent": round(100 * partial / total, 1) if total else 0.0,
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
    areas = [v * w * h / total for v in values]
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
    stroke = f'stroke="{BORDER}" stroke-width="{stroke_width}" paint-order="stroke"' if stroke_width else ""
    return (
        f'<text x="{x:.2f}" y="{y:.2f}" font-family="sans-serif" font-size="{size}" '
        f'font-weight="{weight}" text-anchor="{anchor}" fill="{fill}" {stroke}>{escape(value)}</text>'
    )


def fit_label(value: str, width: float) -> str:
    max_chars = int((width - 8) / 6.2)
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
            f'{data["percent"]}% green · {data["done"]}/{data["total"]} done · '
            f'{data["partial"]} partial · {data["todo"]} missing',
            11,
            fill=MUTED,
        ),
    ]
    groups = [g for g in data["groups"] if g["total"]]
    groups.sort(key=lambda g: (-g["total"], g["name"]))
    boxes = squarify([g["total"] for g in groups], left, HEADER, PANEL_W, MAP_H)
    for group, (x, y, w, h) in zip(groups, boxes):
        # Keep the state blocks contiguous, but carry the matching cell
        # records with them so hover labels stay attached to the right box.
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
        for index, (cx, cy, cw, ch) in enumerate(cells(len(states), x + 1, y + 1, w - 2, h - 2)):
            state = states[index]
            color = {
                "done": DONE_COLOR,
                "partial": PARTIAL_COLOR,
                "todo": TODO_COLOR,
            }[state]
            name = cells_for_group[index]["name"] if index < len(cells_for_group) else ""
            parts.append(
                f'<g><title>{escape(name)} — {state}</title>'
                f'{rect(cx, cy, cw, ch, color, stroke=BORDER, sw=0.4)}</g>'
            )
        parts.append(rect(x, y, w, h, "none", stroke=GROUP_BORDER, sw=1.5))
        label = fit_label(group["name"], w)
        if label and h >= 18:
            parts.append(text(x + 5, y + 14, label, 11, weight="bold", stroke_width=3))
        parts.append("</g>")
    return parts


def render_svg(syscalls: dict, libraries: dict, baseline: str, generated: str) -> str:
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {WIDTH} {HEIGHT}" '
        f'width="{WIDTH}" height="{HEIGHT}">',
        rect(0, 0, WIDTH, HEIGHT, BORDER, stroke=BORDER, sw=0),
        f'<rect x="0.5" y="0.5" width="{WIDTH - 1}" height="{HEIGHT - 1}" fill="none" '
        f'stroke="{PANEL_BORDER}" stroke-width="1"/>',
    ]
    parts += panel("Native syscalls", syscalls, 0)
    parts += panel("WebKit libraries", libraries, PANEL_W + GAP)
    # Panel dividers.
    parts.append(f'<line x1="{PANEL_W}" y1="0" x2="{PANEL_W}" y2="{HEIGHT - FOOTER}" '
                 f'stroke="{PANEL_BORDER}" stroke-width="1"/>')
    # Footer legend.
    legend_y = HEIGHT - 20
    for x, color, label in (
        (12, DONE_COLOR, "green — done"),
        (112, PARTIAL_COLOR, "amber — partial"),
        (226, TODO_COLOR, "gray — missing"),
    ):
        parts.append(rect(x, legend_y - 9, 9, 9, color, stroke=BORDER, sw=0.5))
        parts.append(text(x + 14, legend_y, label, 11, fill=MUTED))
    parts.append(
        text(
            WIDTH - 12,
            legend_y,
            f"baseline {baseline} · generated {generated} · source: WebKitPE-Plan.md",
            10,
            fill=MUTED,
            anchor="end",
        )
    )
    parts.append("</svg>")
    return "\n".join(parts)


def badge(label: str, data: dict) -> str:
    percent = data["percent"]
    value = f"{percent:g}%"
    left, right = 10 + 7 * len(label), max(52, 10 + 7 * len(value))
    width = left + right
    total = data["total"] or 1
    green = right * data["done"] / total
    amber = right * data["partial"] / total
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="20" role="img" '
        f'aria-label="{escape(label)}: {value} green">'
        f'<title>{escape(label)}: {data["done"]} green, {data["partial"]} partial, '
        f'{data["todo"]} missing of {data["total"]}</title>'
        f'<rect width="{left}" height="20" fill="#555"/>'
        f'<rect x="{left}" width="{right}" height="20" fill="{TODO_COLOR}"/>'
        f'<rect x="{left + green:.2f}" width="{amber:.2f}" height="20" fill="{PARTIAL_COLOR}"/>'
        f'<rect x="{left}" width="{green:.2f}" height="20" fill="{DONE_COLOR}"/>'
        f'<g fill="#fff" font-family="Verdana,DejaVu Sans,sans-serif" font-size="11" text-anchor="middle">'
        f'<text x="{left / 2}" y="14">{escape(label)}</text>'
        f'<text x="{left + right / 2}" y="14">{value}</text></g></svg>'
    )


def table(heading: str, data: dict, caption: str) -> str:
    rows = [
        f"<h2>{escape(heading)}</h2>",
        f'<p class="muted">{escape(caption)}</p>',
        "<table>",
        "<tr><th>Group</th><th>Green</th><th>Partial</th><th>Missing</th><th>Total</th><th>Green share</th></tr>",
    ]
    for group in sorted(data["groups"], key=lambda g: (-g["total"], g["name"])):
        pct = f'{100 * group["done"] / group["total"]:.0f}%' if group["total"] else "—"
        rows.append(
            f"<tr><td>{escape(group['name'])}</td><td>{group['done']}</td>"
            f"<td>{group['partial']}</td><td>{group['todo']}</td>"
            f"<td>{group['total']}</td><td>{pct}</td></tr>"
        )
    rows.append(
        f'<tr class="total"><th>Total</th><th>{data["done"]}</th>'
        f'<th>{data["partial"]}</th><th>{data["todo"]}</th>'
        f'<th>{data["total"]}</th><th>{data["percent"]}%</th></tr>'
    )
    rows.append("</table>")
    return "\n".join(rows)


def names_by_state(data: dict, state: str, show_group: bool = True) -> list[str]:
    items = []
    for group in data["groups"]:
        for cell in group["cells"]:
            if cell["state"] == state:
                label = cell.get("label") or cell["name"]
                items.append(f"{label} ({group['name']})" if show_group else label)
    return items


def details(title: str, items: list[str]) -> str:
    if not items:
        return ""
    values = "".join(f"<li><code>{escape(item)}</code></li>" for item in items)
    return f"<details><summary>{escape(title)} ({len(items)})</summary><ul>{values}</ul></details>"


def render_html(syscalls: dict, libraries: dict, baseline: str, generated: str) -> str:
    partial_syscalls = names_by_state(syscalls, "partial")
    missing_syscalls = names_by_state(syscalls, "todo")
    partial_libraries = names_by_state(libraries, "partial", show_group=False)
    missing_libraries = names_by_state(libraries, "todo", show_group=False)
    return "\n".join(
        [
            "<!DOCTYPE html>",
            '<html lang="en"><head><meta charset="utf-8">',
            '<meta name="viewport" content="width=device-width, initial-scale=1">',
            "<title>XenevaOS WebKit PE progress</title>",
            "<style>",
            "body{font-family:system-ui,sans-serif;max-width:1280px;margin:auto;padding:24px;"
            "background:#f6f8fa;color:#1f2328}",
            "img{width:100%;height:auto;border:1px solid #30363d;border-radius:6px;background:#0d1117}",
            "table{border-collapse:collapse;margin:12px 0 24px}",
            "th,td{border:1px solid #d0d7de;padding:4px 10px}",
            "th{background:#eaeef2}td+td,th+th{text-align:right}",
            "tr.total th{background:#d8dee4}",
            ".muted{color:#57606a}code{font-size:90%}",
            "details{margin:8px 0 18px}summary{cursor:pointer;font-weight:600}",
            "</style></head><body>",
            "<h1>XenevaOS WebKit PE readiness</h1>",
            f'<p class="muted">Generated {escape(generated)} from '
            f'<a href="WebKitPE-Plan.md">WebKitPE-Plan.md</a> · baseline {escape(baseline)}. '
            "Green = fully done, amber = partial/prebuilt/external/headers-only/prototype, "
            "gray = missing. Syscall green means OK/LEGACY, amber means PARTIAL, and STUB counts "
            "as missing. Library green means built; amber means present but not reproducible or "
            "fully integrated.</p>",
            '<img src="webkit-progress.svg" alt="WebKit PE syscall and library readiness treemaps">',
            table(
                "Native syscalls",
                syscalls,
                "One cell per tracked native syscall. Existing 0–83 plus proposed 84–193.",
            ),
            table(
                "WebKit libraries",
                libraries,
                "One cell per library row. Green = built; amber = partial, prebuilt, external, prototype, or headers-only; gray = absent.",
            ),
            details("Partial syscalls", partial_syscalls),
            details("Missing syscalls", missing_syscalls),
            details("Partial libraries", partial_libraries),
            details("Missing libraries", missing_libraries),
            '<p><a href="webkit-progress-history.html">Progress history</a> · '
            '<a href="webkit-progress-history.json">history.json</a></p>',
            '<p class="muted">Edit the tables in <code>WebKitPE-Plan.md</code>, then rerun '
            '<code>python3 Tools/webkit-progress.py</code>. The parser fails if a row is added '
            "without a group mapping.</p>",
            "</body></html>",
        ]
    )


HISTORY_JSON = "webkit-progress-history.json"
HISTORY_HTML = "webkit-progress-history.html"
HISTORY_SVG = "webkit-progress-history.svg"


def compact(data: dict) -> dict:
    return {
        "done": data["done"],
        "partial": data["partial"],
        "todo": data["todo"],
        "total": data["total"],
        "percent": data["percent"],
    }


def history_point(date: str, baseline: str, syscalls: dict, libraries: dict) -> dict:
    return {
        "date": date,
        "baseline": baseline,
        "syscalls": compact(syscalls),
        "libraries": compact(libraries),
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
        for key in ("syscalls", "libraries"):
            stats = item.get(key)
            if not isinstance(stats, dict):
                raise ValueError(f"{path}: snapshot {index} has no {key} object")
            # Snapshots written before the partial state existed have no
            # "partial" field; keep them readable and let today's merge
            # replace the current date with the full three-state snapshot.
            stats.setdefault("partial", 0)
            if not all(
                isinstance(stats.get(field), (int, float))
                for field in ("done", "partial", "todo", "total", "percent")
            ):
                raise ValueError(f"{path}: snapshot {index} has incomplete {key} stats")
        valid.append(item)
    valid.sort(key=lambda item: item["date"])
    return valid


def merge_history(snapshots: list[dict], point: dict) -> list[dict]:
    # Keep exactly one snapshot per date: the last run for that date wins.
    by_date = {item["date"]: item for item in snapshots}
    by_date[point["date"]] = point
    return [by_date[date] for date in sorted(by_date)]


def short_date(value: str) -> str:
    try:
        date = _dt.date.fromisoformat(value)
        return date.strftime("%b %d").replace(" 0", " ")
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
    """Return a self-contained dark history chart, similar to AnyPS5 history.html."""
    width, height = 900, 416
    left, right = 56, 710
    panel_h = 150
    panels = (
        ("Syscalls", "syscalls", 34),
        ("Libraries", "libraries", 234),
    )
    count = len(snapshots)
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" '
        f'width="{width}" height="{height}" role="img" '
        f'aria-label="Done, partial, and tracked syscalls and libraries over time" '
        f'font-family="sans-serif" font-size="12" fill="#c9d1d9">',
        '<style>.hit{fill:#fff;fill-opacity:0}.hit:hover{fill-opacity:0.06}</style>',
        '<rect width="900" height="416" rx="6" fill="#0d1117"/>',
    ]

    def x_at(index: int) -> float:
        if count <= 1:
            return (left + right) / 2
        return left + index * (right - left) / (count - 1)

    for title, key, top in panels:
        bottom = top + panel_h
        latest = snapshots[-1][key] if snapshots else {
            "done": 0,
            "partial": 0,
            "todo": 0,
            "total": 0,
            "percent": 0,
        }
        max_value = nice_max(max((item[key]["total"] for item in snapshots), default=0))
        parts.append(f'<text x="56" y="{top - 12}" font-size="13" font-weight="bold">{title}</text>')
        for step in range(5):
            value = max_value * step / 4
            y = bottom - panel_h * step / 4
            parts.append(
                f'<line x1="{left}" x2="{right}" y1="{y:.1f}" y2="{y:.1f}" '
                f'stroke="#30363d"/>'
            )
            parts.append(
                f'<text x="{left - 6}" y="{y + 4:.1f}" text-anchor="end" '
                f'fill="#8b949e">{value:g}</text>'
            )
        # Vertical guides for up to six readable date ticks.
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
            partial_top_points = " ".join(
                f'{x_at(i):.1f},{bottom - panel_h * item[key]["partial"] / max_value:.1f}'
                for i, item in enumerate(snapshots)
            )
            parts.append(
                f'<polygon points="{left},{bottom} {partial_top_points} {right},{bottom}" '
                f'fill="{PARTIAL_COLOR}" opacity="0.12"/>'
            )
            done_points = " ".join(
                f'{x_at(i):.1f},{bottom - panel_h * item[key]["done"] / max_value:.1f}'
                for i, item in enumerate(snapshots)
            )
            parts.append(
                f'<polygon points="{left},{bottom} {done_points} {right},{bottom}" '
                f'fill="{DONE_COLOR}" opacity="0.18"/>'
            )
            total_points = " ".join(
                f'{x_at(i):.1f},{bottom - panel_h * item[key]["total"] / max_value:.1f}'
                for i, item in enumerate(snapshots)
            )
            parts.append(
                f'<polyline points="{total_points}" fill="none" stroke="#8b949e" '
                f'stroke-width="2" stroke-dasharray="6 4"/>'
            )
            partial_points = " ".join(
                f'{x_at(i):.1f},{bottom - panel_h * item[key]["partial"] / max_value:.1f}'
                for i, item in enumerate(snapshots)
            )
            parts.append(
                f'<polyline points="{partial_points}" fill="none" stroke="{PARTIAL_COLOR}" stroke-width="2"/>'
            )
            parts.append(
                f'<polyline points="{done_points}" fill="none" stroke="{DONE_COLOR}" stroke-width="2"/>'
            )
        elif count == 1:
            # With a single snapshot, still draw the tracked ceiling so the
            # chart reads as a progress line rather than one isolated dot.
            total_y = bottom - panel_h * latest["total"] / max_value
            parts.append(
                f'<line x1="{left}" x2="{right}" y1="{total_y:.1f}" y2="{total_y:.1f}" '
                f'stroke="#8b949e" stroke-width="2" stroke-dasharray="6 4"/>'
            )
        for index, item in enumerate(snapshots):
            value = item[key]["done"]
            x = x_at(index)
            y = bottom - panel_h * value / max_value
            partial_y = bottom - panel_h * item[key]["partial"] / max_value
            percent = item[key]["percent"]
            parts.append(f'<circle cx="{x:.1f}" cy="{partial_y:.1f}" r="3.5" fill="{PARTIAL_COLOR}"/>')
            parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3.5" fill="{DONE_COLOR}"/>')
            if count == 1:
                parts.append(
                    f'<text x="{x + 8:.1f}" y="{y - 8:.1f}" fill="{DONE_COLOR}" font-weight="bold">'
                    f'{value}/{item[key]["total"]} ({percent:g}%)</text>'
                )
        # One hover target per day, so the chart is useful even with many points.
        if count:
            edges = [left]
            if count > 1:
                edges += [(x_at(i - 1) + x_at(i)) / 2 for i in range(1, count)]
                edges.append(right)
            else:
                edges.append(right)
            for index, item in enumerate(snapshots):
                x0, x1 = edges[index], edges[index + 1]
                stats = item[key]
                parts.append(
                    f'<rect class="hit" x="{x0:.1f}" y="{top}" width="{x1 - x0:.1f}" height="{panel_h}">'
                    f'<title>{escape(item["date"])} · {title}\n'
                    f'Green {stats["done"]}, partial {stats["partial"]}, missing {stats["todo"]} '
                    f'of {stats["total"]} tracked</title></rect>'
                )
        parts.append(
            f'<text x="{right + 8}" y="{top + 12}" fill="#8b949e">Tracked {latest["total"]}</text>'
        )
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
        "<tr><th>Date</th><th>Syscalls done / partial / missing</th><th>Green</th>"
        "<th>Libraries done / partial / missing</th><th>Green</th></tr>",
    ]
    for item in reversed(snapshots):
        sys = item["syscalls"]
        lib = item["libraries"]
        rows.append(
            f'<tr><td>{escape(item["date"])}</td>'
            f'<td>{sys["done"]} / {sys["partial"]} / {sys["todo"]}</td>'
            f'<td>{sys["percent"]}%</td>'
            f'<td>{lib["done"]} / {lib["partial"]} / {lib["todo"]}</td>'
            f'<td>{lib["percent"]}%</td></tr>'
        )
    rows.append("</table>")
    return "\n".join(rows)


def render_history_html(snapshots: list[dict], generated: str) -> str:
    chart = history_chart(snapshots)
    legend = "Green = done, amber = partial, dashed gray = tracked total."
    if len(snapshots) <= 1:
        note = (
            "Only one snapshot has been recorded so far. The chart will grow each day this "
            "generator runs; re-running on the same day replaces that day's point. "
            f"{legend}"
        )
    else:
        note = (
            "Each point is the last run for a UTC day. "
            f"{legend} Hover a day for exact numbers."
        )
    return "\n".join(
        [
            "<!DOCTYPE html>",
            '<html lang="en"><head><meta charset="utf-8">',
            '<meta name="viewport" content="width=device-width, initial-scale=1">',
            "<title>XenevaOS WebKit PE progress history</title>",
            "<style>",
            "body{font-family:system-ui,sans-serif;max-width:1000px;margin:auto;padding:24px;"
            "background:#f6f8fa;color:#1f2328}",
            "svg{max-width:100%;height:auto;border:1px solid #30363d;border-radius:6px}",
            "table{border-collapse:collapse;margin:16px 0}",
            "th,td{border:1px solid #d0d7de;padding:4px 10px}",
            "th{background:#eaeef2}td+td,th+th{text-align:right}",
            ".muted{color:#57606a}",
            "</style></head><body>",
            "<h1>WebKit PE progress history</h1>",
            chart,
            f'<p class="muted">Generated {escape(generated)} by '
            '<a href="../../Tools/webkit-progress.py">Tools/webkit-progress.py</a>. '
            f"{escape(note)} Raw numbers: <a href=\"{HISTORY_JSON}\">{HISTORY_JSON}</a>.</p>",
            history_table(snapshots),
            '<p><a href="webkit-progress.html">Back to current progress</a> · '
            '<a href="WebKitPE-Plan.md">WebKit PE plan</a></p>',
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


def json_snapshot(syscalls: dict, libraries: dict, baseline: str, generated: str) -> dict:
    return {
        "generated": generated,
        "baseline": baseline,
        "source": "Docs/Development/WebKitPE-Plan.md",
        "legend": {
            "done": "fully implemented / built",
            "partial": "partial, prebuilt, external, headers-only, or prototype",
            "todo": "missing",
        },
        "syscalls": syscalls,
        "libraries": libraries,
    }


def write_outputs(
    out: Path,
    syscalls: dict,
    libraries: dict,
    baseline: str,
    generated: str,
    snapshots: list[dict],
) -> None:
    out.mkdir(parents=True, exist_ok=True)
    (out / "webkit-progress.svg").write_text(
        render_svg(syscalls, libraries, baseline, generated), encoding="utf-8"
    )
    (out / "webkit-progress.html").write_text(
        render_html(syscalls, libraries, baseline, generated), encoding="utf-8"
    )
    (out / "webkit-progress.json").write_text(
        json.dumps(json_snapshot(syscalls, libraries, baseline, generated), indent=2),
        encoding="utf-8",
    )
    (out / "badge-syscalls.svg").write_text(badge("syscalls", syscalls), encoding="utf-8")
    (out / "badge-libraries.svg").write_text(badge("libraries", libraries), encoding="utf-8")
    write_history(out, snapshots, generated)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Generate the WebKit PE syscall/library treemap from WebKitPE-Plan.md."
    )
    parser.add_argument("--plan", type=Path, default=DEFAULT_PLAN)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--check", action="store_true", help="verify the plan tables parse; write nothing")
    args = parser.parse_args(argv)

    text = args.plan.read_text(encoding="utf-8")
    baseline = parse_baseline(text)
    existing = parse_existing_syscalls(text)
    proposed = parse_new_syscalls(text)
    records = existing + proposed
    for record in records:
        record["group"] = syscall_group(record["number"])
    if sorted(r["number"] for r in records) != list(range(195)):
        raise ValueError("syscall numbers are not exactly 0..194")
    if len({r["name"] for r in records}) != len(records):
        raise ValueError("duplicate syscall name")
    overlap = IMPLEMENTED_NEW_SYSCALLS & PARTIAL_NEW_SYSCALLS
    if overlap:
        raise ValueError(f"syscall marked both done and partial: {sorted(overlap)}")
    unknown = (
        IMPLEMENTED_NEW_SYSCALLS | PARTIAL_NEW_SYSCALLS
    ) - {r["number"] for r in proposed}
    if unknown:
        raise ValueError(f"implemented syscall missing from §4.3: {sorted(unknown)}")

    syscall_groups = group_records(records, lambda r: r["group"], SYSCALL_GROUP_ORDER)
    syscalls = summarize(syscall_groups)

    libraries_raw = parse_libraries(text)
    grouped_libraries = {name for names in LIBRARY_GROUPS.values() for name in names}
    parsed_library_names = {lib["name"] for lib in libraries_raw}
    if parsed_library_names != grouped_libraries:
        missing = parsed_library_names - grouped_libraries
        extra = grouped_libraries - parsed_library_names
        raise ValueError(f"library group mapping mismatch; missing={sorted(missing)} extra={sorted(extra)}")

    def library_group(record: dict) -> str:
        for group, names in LIBRARY_GROUPS.items():
            if record["name"] in names:
                return group
        raise ValueError(f"no group for library {record['name']}")

    library_groups = group_records(libraries_raw, library_group, LIBRARY_GROUP_ORDER)
    libraries = summarize(library_groups)

    if args.check:
        print(
            f"syscalls {syscalls['done']}/{syscalls['total']} green, "
            f"{syscalls['partial']} partial ({syscalls['percent']}%)"
        )
        print(
            f"libraries {libraries['done']}/{libraries['total']} green, "
            f"{libraries['partial']} partial ({libraries['percent']}%)"
        )
        return 0

    generated = _dt.datetime.now(_dt.timezone.utc).date().isoformat()
    history_path = args.out / HISTORY_JSON
    snapshots = merge_history(load_history(history_path), history_point(generated, baseline, syscalls, libraries))
    write_outputs(args.out, syscalls, libraries, baseline, generated, snapshots)
    print(f"wrote {args.out / 'webkit-progress.svg'}")
    print(f"wrote {args.out / 'webkit-progress.html'}")
    print(f"wrote {args.out / 'webkit-progress.json'}")
    print(f"wrote {args.out / HISTORY_HTML}")
    print(f"history: {len(snapshots)} snapshot(s)")
    print(
        f"syscalls {syscalls['done']}/{syscalls['total']} green, "
        f"{syscalls['partial']} partial ({syscalls['percent']}%)"
    )
    print(
        f"libraries {libraries['done']}/{libraries['total']} green, "
        f"{libraries['partial']} partial ({libraries['percent']}%)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
