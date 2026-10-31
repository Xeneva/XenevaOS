#!/bin/bash
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/deodhai-overlay-XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

"${CXX:-g++}" -std=c++17 -O1 -g -Wall -Wextra \
	-Wno-unused-variable -Wno-unused-parameter -Wno-sign-compare \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
	-ffunction-sections -fdata-sections -I"$REPO/Process/DeodhaiXR" \
	-I"$REPO/Libs/Chitralekha" -idirafter "$REPO/Libs/XEClib/includes" \
	-include "$REPO/Tests/stubs/compositor_host.h" \
	-include "$REPO/Libs/XEClib/includes/sys/mman.h" \
	"$REPO/Tests/test_deodhai_overlay.cpp" "$REPO/Process/DeodhaiXR/compose.cpp" \
	"$REPO/Process/DeodhaiXR/alpha.cpp" "$REPO/Process/DeodhaiXR/clip.cpp" \
	"$REPO/Process/DeodhaiXR/rect.cpp" "$REPO/Libs/Chitralekha/color.cpp" \
	-Wl,--gc-sections -o "$TMP/test_overlay"
"$TMP/test_overlay"
