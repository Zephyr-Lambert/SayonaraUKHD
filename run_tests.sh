#!/bin/sh
# Host-side unit tests for the pure policy/format code (no game or GPU needed).
# Needs a native C compiler; the FXAA shader test additionally needs numpy and vkd3d-compiler.
set -eu
cd "$(dirname "$0")"
HOSTCC="${HOSTCC:-cc}"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
for t in tests/test_*.c; do
    name="$(basename "$t" .c)"
    "$HOSTCC" -std=c11 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc/policy -Isrc/third_party "$t" -lm -o "$TMP/$name"
    ASAN_OPTIONS=detect_leaks=0 "$TMP/$name"
    echo "ok  $name"
done
if [ -f build/d3d11.dll ] && command -v objdump >/dev/null; then python3 tools/verify_dll.py; fi
