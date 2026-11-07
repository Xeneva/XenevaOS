#!/bin/bash
# Build + unit test for the XELnch launcher label paint.
#
#  1. Host unit test: links the real Process/XELnch/button.cpp against
#     stub Chitralekha/kernel functions (Tests/test_xelnch_label.cpp)
#     and checks the label readability contract (white face over an
#     opaque shadow, centered, no scrim rect).
#  2. Target build test: cross-builds XELnch for AArch64/LLVM, the same
#     sources the unikernel DeodhaiXR build consumes.
#
# Usage: bash Tests/test_xelnch_label.sh
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
CXX="${CXX:-clang++}"
TMP="$(mktemp -d /tmp/xeneva-xelnch-test-XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

INCLUDES="-I$REPO/Process/XELnch -I$REPO/Libs/XEClib/includes -I$REPO/Libs/XEClib/includes/c++ -I$REPO/BaseHdr -I$REPO/Libs/Chitralekha -I$REPO/Ports/freetype2/include"
DEFINES="-DARCH_ARM64 -D__TARGET_BOARD_QEMU_VIRT__ -D_USE_DLMALLOC -D__STDC_LIMIT_MACROS -DTHEME_DEFAULT"

echo "== host unit test: LaunchButtonPaint =="
"$CXX" -O1 -Wall -Wextra $DEFINES $INCLUDES \
	-c "$REPO/Process/XELnch/button.cpp" -o "$TMP/button.o"
"$CXX" -O1 -Wall -Wextra $DEFINES $INCLUDES \
	-c "$REPO/Tests/test_xelnch_label.cpp" -o "$TMP/test.o"
"$CXX" -o "$TMP/test_label" "$TMP/button.o" "$TMP/test.o"
"$TMP/test_label"
echo ""

echo "== target build test: XELnch (llvm) =="
make -C "$REPO/Process/XELnch" TOOLCHAIN=llvm all >/dev/null
echo "XELnch llvm build: ok"
echo ""
echo "ALL PASS"
