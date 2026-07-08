#!/bin/bash
set -euo pipefail

# Memory leak test for the RGB Matrix emulator
# Uses Valgrind to detect memory leaks in the emulator code path.
# Filters out known noise from SDL2/LLVM library allocations.
#
# Prerequisites:
#   - libsdl2-dev (or vcpkg with sdl2 installed)
#   - valgrind
#   - cmake
#
# Usage:
#   ./scripts/test-memleaks.sh [--debug] [--show-all]

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_DIR}/build-memleak-test"
DEBUG=false
SHOW_ALL=false

for arg in "$@"; do
    case "$arg" in
        --debug) DEBUG=true ;;
        --show-all) SHOW_ALL=true ;;
        --help)
            echo "Usage: $0 [--debug] [--show-all]"
            echo ""
            echo "  --debug          Keep build directory after test"
            echo "  --show-all       Show all leaks including library noise"
            exit 0
            ;;
    esac
done

echo "=== RGB Matrix Emulator Memory Leak Test ==="
echo ""

VCPKG_ROOT="${VCPKG_ROOT:-}"
VCPKG_TOOLCHAIN=""
if [ -n "$VCPKG_ROOT" ] && [ -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]; then
    VCPKG_TOOLCHAIN="-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    echo "Using vcpkg at: $VCPKG_ROOT"
fi

echo "Building emulator with tests..."
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
    -DENABLE_EMULATOR=ON \
    -DBUILD_TESTS=ON \
    $VCPKG_TOOLCHAIN \
    -DCMAKE_BUILD_TYPE=Debug \
    2>&1 | tail -3

cmake --build "$BUILD_DIR" -j"$(nproc)" 2>&1 | tail -3

TEST_BINARY="$BUILD_DIR/emulator-memleak-test"
if [ ! -f "$TEST_BINARY" ]; then
    echo "ERROR: Test binary not built at $TEST_BINARY"
    exit 1
fi

echo ""
echo "Running Valgrind memory leak check..."
echo ""

VALGRIND_LOG=$(mktemp /tmp/emulator-memleak-XXXXXX.log)
trap 'rm -f "$VALGRIND_LOG"' EXIT

set +e
valgrind \
    --leak-check=full \
    --show-leak-kinds=all \
    --log-file="$VALGRIND_LOG" \
    "$TEST_BINARY" 2>&1
set -e

echo ""
echo "=== Results ==="

DEF_LOST=$(grep "definitely lost:" "$VALGRIND_LOG" | awk '{print $4, $5}')
IND_LOST=$(grep "indirectly lost:" "$VALGRIND_LOG" | awk '{print $4, $5}')
echo "  Definitely lost: $DEF_LOST"
echo "  Indirectly lost:  $IND_LOST"

if [ "$SHOW_ALL" = true ]; then
    echo ""
    echo "--- Full leak report ---"
    grep -A 5 "definitely lost\|indirectly lost" "$VALGRIND_LOG" | head -80
    echo "------------------------"
fi

# Check whether leaks come from our code vs SDL2/LLVM libraries.
# SDL2 leaks occur during InitSDL() / SDL_DBus_Init and show <SDL_*> in the
# first frame after the allocation. Our leaks show operator new → our code.
HAS_OUR_LEAK=$(
    awk '
    BEGIN { our = 0; first_by = ""; in_block = 0 }
    /definitely lost|indirectly lost/ { in_block = 1; first_by = ""; next }
    in_block && /^==[0-9]+==    at / { next }
    in_block && /^==[0-9]+==    by / {
        if (first_by == "") first_by = $0
        next
    }
    in_block && /^==[0-9]+== *$/ { in_block = 0 }
    in_block && !/^==[0-9]+==/ { in_block = 0 }
    END {
        if (first_by != "" && first_by !~ /SDL/ && first_by !~ /LLVM/) our = 1
        print our
    }
    ' "$VALGRIND_LOG"
)

if [ "$HAS_OUR_LEAK" = "1" ]; then
    echo ""
    echo "FAILED: Emulator code has memory leaks!"
    echo ""
    awk '
    /definitely lost|indirectly lost/ { in_block = 1; lines = $0; next }
    in_block {
        lines = lines "\n" $0
        if ($0 ~ /^==[0-9]+== *$/) {
            if (lines !~ /SDL/ && lines !~ /LLVM/) print lines
            in_block = 0
        }
    }
    ' "$VALGRIND_LOG"
    exit 1
fi

echo ""
echo "PASSED: No emulator memory leaks detected."
echo "(SDL2/LLVM library internal allocations are excluded.)"

if [ "$DEBUG" = false ]; then
    rm -rf "$BUILD_DIR"
fi

exit 0
