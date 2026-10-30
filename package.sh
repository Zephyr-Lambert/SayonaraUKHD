#!/bin/sh
# Builds the DLL (if needed) and creates dist/SayonaraUKHD-<version>.zip containing
# only what players need: d3d11.dll, SayonaraUKHD.ini and readme.txt.
set -eu
cd "$(dirname "$0")"
VERSION="$(sed -n 's/^#define UMI_VERSION "\(.*\)"/\1/p' src/core/common.h)"
[ -f build/d3d11.dll ] || ./build.sh
NAME="SayonaraUKHD-$VERSION"
rm -rf "dist/$NAME" "dist/$NAME.zip"
mkdir -p "dist/$NAME"
cp build/d3d11.dll release/SayonaraUKHD.ini release/readme.txt "dist/$NAME/"
(cd dist && zip -qr -X "$NAME.zip" "$NAME")
echo "Created dist/$NAME.zip"
