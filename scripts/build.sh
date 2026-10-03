#!/usr/bin/env bash
# RTR-Bench - builds on Linux with the system compiler and Ninja into build/.
#
#   --clean    removes build/ first
#   --debug    debug build (default: Release)
#   --no-tidy  skips clang-tidy
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# On a Windows checkout the Linux build must not share build/ with the Windows one.
BUILD="$ROOT/build"
case "$ROOT" in /mnt/*) BUILD="$ROOT/build-linux" ;; esac
# The RTR-OS tool environment in WSL (micromamba) provides cmake and ninja when
# the system has none; the compiler and the X11/Wayland/GL libraries are the
# system ones, so the binary runs on the system it was built on.
if ! command -v cmake >/dev/null 2>&1 && [ -d "$HOME/rtr-tools/env/bin" ]; then
    PATH="$PATH:$HOME/rtr-tools/env/bin"
    export PKG_CONFIG_PATH="/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/share/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
fi
CC="${CC:-/usr/bin/cc}"
CXX="${CXX:-/usr/bin/c++}"
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

cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE="$TYPE" -DRTR_BENCH_TIDY="$TIDY" \
    -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX"
cmake --build "$BUILD"
