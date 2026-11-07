#!/bin/bash
# Compile each tree's circbuf.c and drive the TCP RX path.
#
# Usage:
#   bash Tests/bench_compare.sh
#   bash Tests/bench_compare.sh HEAD~1 HEAD
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
CC="${CC:-clang}"
CFLAGS="-O2 -Wall -Wextra"
PREV_REV="${1:-HEAD~1}"
CURR_REV="${2:-HEAD}"
PREV_WT=""
CURR_WT=""

cleanup() {
	if [[ -n "$PREV_WT" ]]; then
		git -C "$REPO" worktree remove --force "$PREV_WT" >/dev/null 2>&1 || true
		rm -rf "$PREV_WT"
	fi
	if [[ -n "$CURR_WT" ]]; then
		git -C "$REPO" worktree remove --force "$CURR_WT" >/dev/null 2>&1 || true
		rm -rf "$CURR_WT"
	fi
}
trap cleanup EXIT

checkout_rev() {
	local rev="$1"
	local wt
	wt="$(mktemp -d /tmp/xeneva-bench-XXXXXX)"
	git -C "$REPO" worktree add --detach "$wt" "$rev" >/dev/null
	echo "$wt"
}

tcp_uses_bulk() {
	grep -q 'AuCircBufWrite' "$1/KernelAA64/Net/tcp.c"
}

rx_buf_sz() {
	local n
	n="$(sed -n 's/^#define TCP_RX_BUF_SZ \([0-9][0-9]*\).*/\1/p' "$1/BaseHdr/Net/tcp.h" | head -1)"
	echo "${n:-16384}"
}

run_rev() {
	local label="$1"
	local rev="$2"
	local wt="$3"
	local tmp flags sz out
	tmp="$(mktemp -d /tmp/xeneva-bench-build-XXXXXX)"
	flags=""
	if tcp_uses_bulk "$wt"; then
		flags="-DTCP_USES_BULK"
		echo "=== $label $(git -C "$REPO" rev-parse --short "$rev")  [tcp: bulk] ==="
	else
		echo "=== $label $(git -C "$REPO" rev-parse --short "$rev")  [tcp: byte] ==="
	fi
	sz="$(rx_buf_sz "$wt")"
	$CC $CFLAGS -iquote "$wt/BaseHdr" $flags -DTCP_RX_BUF_SZ="$sz" \
		-c "$REPO/Tests/bench_circbuf.c" -o "$tmp/bench.o"
	$CC $CFLAGS -I "$wt/BaseHdr" \
		-Wno-incompatible-library-redeclaration \
		-Wno-incompatible-pointer-types-discards-qualifiers \
		-c "$wt/KernelAA64/circbuf.c" -o "$tmp/circbuf.o"
	out="$tmp/bench"
	$CC $CFLAGS -o "$out" "$tmp/bench.o" "$tmp/circbuf.o" -lrt
	"$out"
	echo ""
	rm -rf "$tmp"
}

echo "clang: $($CC --version | head -1)"
echo "checkout $PREV_REV -> worktree, then $CURR_REV -> worktree"
echo ""

PREV_WT="$(checkout_rev "$PREV_REV")"
CURR_WT="$(checkout_rev "$CURR_REV")"

run_rev "PREV" "$PREV_REV" "$PREV_WT"
run_rev "CURR" "$CURR_REV" "$CURR_WT"
