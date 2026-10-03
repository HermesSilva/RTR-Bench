#!/usr/bin/env bash
# RTR-Bench - builds on Linux with the system compiler and Ninja into build/.
#
#   --clean    removes build/ first
#   --debug    debug build (default: Release)
#   --no-tidy  skips clang-tidy
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
TYPE=Release
TIDY=ON
for arg in "$@"; do
    case "$arg" in
        --clean) rm -rf "$BUILD" ;;
        --debug) TYPE=Debug ;;
        --no-tidy) TIDY=OFF ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE="$TYPE" -DRTR_BENCH_TIDY="$TIDY"
cmake --build "$BUILD"
