#!/usr/bin/env bash
# Build the RP2350-USB-CM RCM injector UF2 on macOS/Linux.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Pico SDK (sibling checkout by default).
export PICO_SDK_PATH="${PICO_SDK_PATH:-$HERE/../pico-sdk}"

# --- Locate an arm-none-eabi toolchain that actually ships newlib -----------
# The bare Homebrew `arm-none-eabi-gcc` formula has no libc/libg; the RP2350
# boot2 link then fails with "cannot find -lc / -lg". Pick a gcc whose driver
# resolves libc.a to a real file.
has_newlib() {
    local gcc="$1"
    [ -x "$gcc" ] || return 1
    local libc
    libc="$("$gcc" -print-file-name=libc.a 2>/dev/null || true)"
    [ -n "$libc" ] && [ -f "$libc" ]
}

CANDIDATES=()
# Explicit override wins.
if [ -n "${PICO_TOOLCHAIN_PATH:-}" ]; then
    CANDIDATES+=("$PICO_TOOLCHAIN_PATH/bin/arm-none-eabi-gcc")
fi
# Official Arm GNU Toolchain (Homebrew cask `gcc-arm-embedded` / manual install).
for d in /Applications/ArmGNUToolchain/*/arm-none-eabi/bin \
         "$HOME"/Applications/ArmGNUToolchain/*/arm-none-eabi/bin; do
    [ -e "$d/arm-none-eabi-gcc" ] && CANDIDATES+=("$d/arm-none-eabi-gcc")
done
# Whatever is already on PATH, then the Homebrew formulae (likely no newlib).
CANDIDATES+=("$(command -v arm-none-eabi-gcc 2>/dev/null || true)")
CANDIDATES+=(/opt/homebrew/opt/arm-none-eabi-gcc/bin/arm-none-eabi-gcc)
CANDIDATES+=(/usr/local/opt/arm-none-eabi-gcc/bin/arm-none-eabi-gcc)

TOOLCHAIN_BIN=""
for gcc in "${CANDIDATES[@]}"; do
    [ -n "$gcc" ] || continue
    if has_newlib "$gcc"; then
        TOOLCHAIN_BIN="$(cd "$(dirname "$gcc")" && pwd)"
        break
    fi
done

if [ -z "$TOOLCHAIN_BIN" ]; then
    cat >&2 <<'EOF'
ERROR: no arm-none-eabi toolchain with newlib (libc.a) was found.

The Homebrew `arm-none-eabi-gcc` formula does NOT include the C library, so the
RP2350 boot2 link fails with "cannot find -lc / -lg".

Install the official Arm GNU toolchain (bundles newlib), then re-run this script:

    brew install --cask gcc-arm-embedded

Or set PICO_TOOLCHAIN_PATH to a toolchain whose bin/ has arm-none-eabi-gcc
with newlib, then re-run.
EOF
    exit 1
fi

export PATH="$TOOLCHAIN_BIN:$PATH"
export PICO_TOOLCHAIN_PATH="$(cd "$TOOLCHAIN_BIN/.." && pwd)"

for tool in cmake ninja arm-none-eabi-gcc python3; do
    command -v "$tool" >/dev/null 2>&1 || { echo "missing tool: $tool" >&2; exit 1; }
done

echo "PICO_SDK_PATH   = $PICO_SDK_PATH"
echo "Toolchain       = $TOOLCHAIN_BIN"

# A stale build dir configured with a different compiler must be wiped, or CMake
# keeps the old (newlib-less) toolchain from its cache.
CACHE="$HERE/build/CMakeCache.txt"
if [ -f "$CACHE" ]; then
    CACHED_CC="$(sed -n 's/^CMAKE_C_COMPILER:[^=]*=//p' "$CACHE" | head -n1)"
    if [ "$CACHED_CC" != "$TOOLCHAIN_BIN/arm-none-eabi-gcc" ]; then
        echo "Toolchain changed; wiping $HERE/build"
        rm -rf "$HERE/build"
    fi
fi

cmake -S "$HERE" -B "$HERE/build" -G Ninja -DPICO_BOARD=pico2
cmake --build "$HERE/build"

UF2="$HERE/build/pico_rcm_injector.uf2"
if [ -f "$UF2" ]; then
    echo "UF2 ready: $UF2"
    ls -l "$UF2"
else
    echo "Build finished but UF2 not found" >&2
    exit 1
fi
