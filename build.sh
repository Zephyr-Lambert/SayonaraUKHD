#!/bin/sh
# Builds build/d3d11.dll (32-bit). Needs a MinGW-w64 i686 cross compiler:
#   - GCC:   i686-w64-mingw32-gcc   (e.g. Fedora: mingw32-gcc, Debian/Ubuntu: gcc-mingw-w64-i686)
#   - Clang: llvm-mingw (https://github.com/mstorsjo/llvm-mingw), msvcrt variant
# Override the compiler with CC=..., e.g.  CC=/opt/llvm-mingw/bin/i686-w64-mingw32-clang ./build.sh
set -eu
cd "$(dirname "$0")"
CC="${CC:-i686-w64-mingw32-gcc}"
OUT="${OUT:-build/d3d11.dll}"
mkdir -p "$(dirname "$OUT")"
"$CC" -std=c11 -O2 -Wall -Wextra -Werror -Wno-cast-function-type \
    -Isrc/policy -Isrc/third_party -Ishaders \
    -shared -static-libgcc src/main.c src/exports.def -luser32 \
    -Wl,--no-insert-timestamp -o "$OUT"
echo "Built $OUT"
