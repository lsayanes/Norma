#!/bin/bash
# Genera resources/Norma.icns a partir de resources/Norma.png
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
PNG="$ROOT/resources/Norma.png"
ICONSET="$ROOT/build/Norma.iconset"
OUT="$ROOT/resources/Norma.icns"

if [ ! -f "$PNG" ]; then
  echo "Missing $PNG"
  exit 1
fi

rm -rf "$ICONSET"
mkdir -p "$ICONSET" "$ROOT/build"

make_icon() {
  local size="$1"
  local name="$2"
  sips -z "$size" "$size" "$PNG" --out "$ICONSET/$name" >/dev/null
}

make_icon 16   icon_16x16.png
make_icon 32   icon_16x16@2x.png
make_icon 32   icon_32x32.png
make_icon 64   icon_32x32@2x.png
make_icon 128  icon_128x128.png
make_icon 256  icon_128x128@2x.png
make_icon 256  icon_256x256.png
make_icon 512  icon_256x256@2x.png
make_icon 512  icon_512x512.png
make_icon 1024 icon_512x512@2x.png

iconutil -c icns "$ICONSET" -o "$OUT"
echo "Created $OUT"
