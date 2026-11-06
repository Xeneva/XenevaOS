#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=c11 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
    -ffunction-sections -fdata-sections -fno-builtin -DARCH_ARM64 \
    -I "$root/Tests/stubs" -idirafter "$root/BaseHdr" \
    -include "$root/Tests/stubs/lifecycle_host.h" \
    "$root/Tests/process_teardown.c" "$root/KernelAA64/clean.c" \
    "$root/KernelAA64/process.c" "$root/KernelAA64/list.c" \
    "$root/KernelAA64/Mm/vmmngr.c" "$root/KernelAA64/Mm/shm.c" \
    "$root/KernelAA64/ftmngr.c" \
    -Wl,--gc-sections -o "$out/process_teardown"
"$out/process_teardown"
