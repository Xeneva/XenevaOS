#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/xeneva-gpuproxy-XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
"${CC:-cc}" -std=c11 -O1 -g -Wall -Wextra -Werror \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
	-DARCH_ARM64 -D__STDC_LIMIT_MACROS -idirafter "$ROOT/BaseHdr" \
	"$ROOT/Tests/test_gpuproxy.c" -o "$OUT/test_gpuproxy"
"$OUT/test_gpuproxy"
