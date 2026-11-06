#!/bin/bash
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/deodhai-resize-XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

"${CXX:-g++}" -std=c++17 -O1 -g -Wall -Wextra -Werror \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
	-I"$REPO/Process/DeodhaiXR" -I"$REPO/Libs/Chitralekha" \
	-idirafter "$REPO/Libs/XEClib/includes" \
	"$REPO/Tests/test_deodhai_resize.cpp" "$REPO/Process/DeodhaiXR/resize.cpp" \
	-o "$TMP/test_resize"
"$TMP/test_resize"

"${CXX:-g++}" -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-variable \
	-ffunction-sections -fdata-sections -fsanitize=address,undefined -fno-sanitize-recover=all \
	-fno-omit-frame-pointer -I"$REPO/Process/DeodhaiXR" \
	-I"$REPO/Libs/Chitralekha" -idirafter "$REPO/Libs/XEClib/includes" \
	-include cstddef -include "$REPO/Libs/XEClib/includes/sys/mman.h" \
	"$REPO/Tests/test_deodhai_buffers.cpp" "$REPO/Process/DeodhaiXR/window.cpp" \
	-Wl,--gc-sections -o "$TMP/test_buffers"
"$TMP/test_buffers"
