#!/bin/bash
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/xeneva-gpudamage-XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

"${CXX:-g++}" -std=c++17 -O1 -g -Wall -Wextra -Werror \
	-I"$REPO/BaseHdr" "$REPO/Tests/test_gpudamage.cpp" -o "$TMP/test_gpudamage"
"$TMP/test_gpudamage"
