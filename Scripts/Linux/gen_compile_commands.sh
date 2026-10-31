#!/bin/bash
# Regenerates compile_commands.json for the AArch64 LLVM/Clang build of XenevaOS.
#
# Usage:
#   ./Scripts/Linux/gen_compile_commands.sh
#
# Requires `bear` (https://github.com/rizsits/bear) and `clang`. Builds every
# AArch64 component with `make llvm` under bear so clangd / IDE tooling resolve
# the exact freestanding cross-compile invocations (-target aarch64-unknown-windows,
# include paths, defines).

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"

# Keep the generator usable both standalone and when sourced alongside the
# Linux build helpers, which provide colored output variables.
STY_RED="${STY_RED:-}"
STY_RST="${STY_RST:-}"
STY_CYAN="${STY_CYAN:-}"
STY_YELLOW="${STY_YELLOW:-}"
STY_GREEN="${STY_GREEN:-}"

OUT="${BEAR_OUTPUT:-$REPO_ROOT/compile_commands.json}"
rm -f "$OUT"

if ! command -v bear >/dev/null 2>&1; then
    printf "${STY_RED}[ccdb] bear not found; install it (apt/brew/pacman -S bear) to regenerate compile_commands.json.${STY_RST}\n" >&2
    exit 1
fi

# Order matters: the libraries must exist on disk for the application link steps,
# but compile_commands only needs the -c invocations, so order is not critical.
LIB_DIRS=(BootAA64 KernelAA64 Libs/XEClib Libs/Chitralekha)
APPS=(
    "Process/Init"
    "Process/DeodhaiXR"
    "Process/Terminal"
    "Process/Namdapha"
    "Process/XELnch"
    "Process/DeodhaiAudio"
    "Process/Calender"
    "Process/Calculator"
    "Process/AudioPlayer"
    "Process/Files"
    "Process/Control"
)

printf "${STY_CYAN}[ccdb] generating compile_commands.json (clang/AArch64)...${STY_RST}\n"

first=1
run_bear() {
    local dir="$1"
    if [ "$first" -eq 1 ]; then
        bear --output "$OUT" -- make -C "$dir" clean llvm >/dev/null 2>/dev/null
        first=0
    else
        bear --append --output "$OUT" -- make -C "$dir" clean llvm >/dev/null 2>/dev/null
    fi
    if [ $? -ne 0 ]; then
        printf "${STY_YELLOW}[ccdb] warning: ${dir} build failed under bear${STY_RST}\n" >&2
    fi
}

for d in "${LIB_DIRS[@]}"; do
    run_bear "$d"
done
for app in "${APPS[@]}"; do
    run_bear "$app"
done

count=0
if [ -f "$OUT" ]; then
    count=$(python3 -c "import json;print(len(json.load(open('$OUT'))))" 2>/dev/null || echo 0)
fi
printf "${STY_GREEN}[ccdb] compile_commands.json written with %s entries.${STY_RST}\n" "$count"
