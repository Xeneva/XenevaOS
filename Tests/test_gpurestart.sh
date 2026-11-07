#!/usr/bin/env bash
set -euo pipefail

# End-to-end GPU server restart test. Unlike test_gpuproxy.sh, this boots the
# AArch64 guest and verifies real virtio-GPU transfer/flush before and after a
# supervised process crash. It is intentionally separate from `make test`:
# it needs LLVM, gnu-efi, FAT tools, QEMU and AArch64 firmware.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG="$(mktemp "${TMPDIR:-/tmp}/xeneva-gpurestart.XXXXXX.log")"
trap 'rm -f "$LOG"' EXIT

set +e
GPUSRV=1 SVCTEST=gpurestart QEMU_TIMEOUT="${QEMU_TIMEOUT:-45}" \
  bash "$ROOT/Scripts/Linux/build_and_run_qemu.sh" \
  --llvm --force-user-apps --bleed --headless --no-audio --no-network \
  --no-doom --no-netsurf --no-data-disk --no-boot-menu >"$LOG" 2>&1
rc=$?
set -e
if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
	cat "$LOG"
	printf 'gpurestart: QEMU/build failed with exit %d\n' "$rc" >&2
	exit "$rc"
fi

python3 - "$LOG" <<'PY'
import re
import sys

text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
markers = [
    r"\[gpusrv\] acquired generation=1",
    r"\[gpusrv\] presented generation=1 frame=",
    r"\[gpusrv\] test hook: crashing on purpose",
    r"\[hwdev\].*virtio-gpu reset complete",
    r"\[svcmgr\].*gpusrv is DOWN",
    r"\[svcmgr\].*gpusrv will RESTART",
    r"\[svcmgr\].*gpusrv is BACK",
    r"\[gpusrv\] acquired generation=2",
    r"\[gpusrv\] presented generation=2 frame=",
]
pos = -1
for marker in markers:
    match = re.search(marker, text[pos + 1:])
    if not match:
        print("MISSING", marker, file=sys.stderr)
        print(text, file=sys.stderr)
        raise SystemExit(1)
    pos += 1 + match.end()
    print("PASS", marker)
fatal_markers = [
    r"(?im)^\s*(?:kernel\s+)?panic\b",
    r"(?im)^\s*unhandled(?:\s+exception)?\b",
    r"(?m)^\s*(?:\[[^\]]+\]\s*)?(?:PANIC|UNHANDLED|FAILED)\b",
]
for fatal_marker in fatal_markers:
    if re.search(fatal_marker, text):
        print("guest reported a fatal marker", file=sys.stderr)
        raise SystemExit(1)
PY

printf 'gpurestart: PASS (QEMU exit %d; 124 is the intentional timeout)\n' "$rc"
