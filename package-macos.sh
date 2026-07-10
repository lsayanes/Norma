#!/bin/bash
# Empaqueta Norma como Norma.app lista para usar en macOS.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

echo "=== Packaging Norma.app ==="

# 1) Icono .icns
if [ ! -f resources/Norma.icns ] || [ resources/Norma.png -nt resources/Norma.icns ]; then
  echo "Generating Norma.icns..."
  chmod +x ./generate-icns.sh
  ./generate-icns.sh
fi

# 2) Build Release
mkdir -p build-app
cd build-app

CMAKE_EXTRA_ARGS=""
QT_PREFIX=""
if command -v brew >/dev/null 2>&1; then
  QT_PREFIX=$(brew --prefix qt 2>/dev/null || true)
  if [ -n "$QT_PREFIX" ] && [ -d "$QT_PREFIX/lib/cmake/Qt6" ]; then
    echo "Using Qt6: $QT_PREFIX"
    CMAKE_EXTRA_ARGS="-DCMAKE_PREFIX_PATH=$QT_PREFIX"
  fi
fi

cmake .. -DCMAKE_BUILD_TYPE=Release $CMAKE_EXTRA_ARGS
CORES=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
cmake --build . --config Release -j"$CORES"

APP="$ROOT/build-app/Norma.app"
if [ ! -d "$APP" ]; then
  echo "Norma.app was not created. Check CMake MACOSX_BUNDLE settings."
  exit 1
fi

# 3) Embebér frameworks Qt
MACDEPLOYQT="${QT_PREFIX}/bin/macdeployqt"
if [ ! -x "$MACDEPLOYQT" ]; then
  MACDEPLOYQT="$(command -v macdeployqt || true)"
fi
if [ -z "$MACDEPLOYQT" ] || [ ! -x "$MACDEPLOYQT" ]; then
  echo "macdeployqt not found"
  exit 1
fi

QTBASE_LIB="$(brew --prefix qtbase 2>/dev/null)/lib"
BREW_LIB="$(brew --prefix)/lib"
BROTLI_LIB="$(brew --prefix brotli 2>/dev/null)/lib"
WEBP_LIB="$(brew --prefix webp 2>/dev/null)/lib"

echo "Running macdeployqt..."
"$MACDEPLOYQT" "$APP" -always-overwrite \
  -libpath="$QTBASE_LIB" \
  -libpath="$BREW_LIB" \
  -libpath="$BROTLI_LIB" \
  -libpath="$WEBP_LIB" || true

FRAMEWORKS="$APP/Contents/Frameworks"
mkdir -p "$FRAMEWORKS"

copy_lib() {
  local src="$1"
  local base
  base="$(basename "$src")"
  if [ -f "$src" ] && [ ! -f "$FRAMEWORKS/$base" ]; then
    echo "  + $base"
    cp "$src" "$FRAMEWORKS/$base"
    chmod u+w "$FRAMEWORKS/$base"
    install_name_tool -id "@executable_path/../Frameworks/$base" "$FRAMEWORKS/$base" 2>/dev/null || true
  fi
}

echo "Copying Homebrew runtime libs..."
for lib in \
  "$BROTLI_LIB"/libbrotlicommon.1.dylib \
  "$BROTLI_LIB"/libbrotlidec.1.dylib \
  "$WEBP_LIB"/libwebp.7.dylib \
  "$WEBP_LIB"/libsharpyuv.0.dylib \
  "$BREW_LIB"/libwebp.7.dylib \
  "$BREW_LIB"/libsharpyuv.0.dylib
do
  [ -f "$lib" ] && copy_lib "$lib"
done

# Quitar plugins innecesarios que arrastran QML/VirtualKeyboard/PDF
echo "Pruning unused plugins..."
rm -rf \
  "$APP/Contents/PlugIns/virtualkeyboard" \
  "$APP/Contents/PlugIns/qmltooling" \
  "$APP/Contents/PlugIns/sqldrivers" \
  2>/dev/null || true

# Frameworks no necesarios para Norma (Widgets + Network)
for fw in QtQml QtQmlMeta QtQmlModels QtQmlWorkerScript QtQuick QtPdf QtSvg \
          QtVirtualKeyboard QtVirtualKeyboardQml
do
  rm -rf "$FRAMEWORKS/${fw}.framework" 2>/dev/null || true
done

echo "Fixing @rpath / Homebrew install names..."
export NORMA_APP="$APP"
python3 - <<'PY'
import os, subprocess

app = os.environ["NORMA_APP"]
frameworks = os.path.join(app, "Contents", "Frameworks")

def is_macho(path):
    try:
        return "Mach-O" in subprocess.check_output(["file", path], text=True)
    except Exception:
        return False

def otool_libs(path):
    try:
        out = subprocess.check_output(["otool", "-L", path], stderr=subprocess.DEVNULL, text=True)
    except Exception:
        return []
    return [line.strip().split(" (")[0] for line in out.splitlines()[1:]]

def change(path, old, new):
    subprocess.run(["chmod", "u+w", path], check=False)
    return subprocess.run(
        ["install_name_tool", "-change", old, new, path],
        capture_output=True,
    ).returncode == 0

bundled = {}
for name in os.listdir(frameworks):
    full = os.path.join(frameworks, name)
    if name.endswith(".dylib"):
        bundled[name] = f"@executable_path/../Frameworks/{name}"
    elif name.endswith(".framework"):
        fw = name[: -len(".framework")]
        binary = os.path.join(full, "Versions", "A", fw)
        if os.path.exists(binary):
            key = f"{name}/Versions/A/{fw}"
            bundled[key] = f"@executable_path/../Frameworks/{key}"

machos = []
for root, _, files in os.walk(os.path.join(app, "Contents")):
    for f in files:
        path = os.path.join(root, f)
        if is_macho(path):
            machos.append(path)

changed = 0
missing = set()
for path in machos:
    for lib in otool_libs(path):
        new = None
        if lib.startswith("@rpath/"):
            rel = lib[len("@rpath/") :]
            if rel in bundled:
                new = bundled[rel]
            elif os.path.basename(rel) in bundled:
                new = bundled[os.path.basename(rel)]
            else:
                missing.add(lib)
        elif lib.startswith("/opt/homebrew/"):
            base = os.path.basename(lib)
            if base in bundled:
                new = bundled[base]
            else:
                missing.add(lib)
        if new and new != lib and change(path, lib, new):
            changed += 1

subprocess.run(
    [
        "install_name_tool",
        "-add_rpath",
        "@executable_path/../Frameworks",
        os.path.join(app, "Contents", "MacOS", "Norma"),
    ],
    capture_output=True,
)
print(f"Updated {changed} install-name references")
if missing:
    print("Still unresolved (may be unused):")
    for m in sorted(missing):
        print(" ", m)
PY

# 4) Firma ad-hoc (necesaria en Apple Silicon)
echo "Ad-hoc codesign..."
codesign --force --deep --sign - "$APP"

# 5) Smoke test
echo "Smoke test..."
"$APP/Contents/MacOS/Norma" >/tmp/norma-app-test.log 2>&1 &
PID=$!
sleep 2
if kill -0 "$PID" 2>/dev/null; then
  echo "App launched OK (pid $PID)"
  kill "$PID" 2>/dev/null || true
  wait "$PID" 2>/dev/null || true
else
  echo "App failed to launch. Log:"
  cat /tmp/norma-app-test.log || true
  exit 1
fi

# 6) Copiar a dist/ y ~/Applications
mkdir -p "$ROOT/dist"
rm -rf "$ROOT/dist/Norma.app"
cp -R "$APP" "$ROOT/dist/Norma.app"

DEST_HOME="$HOME/Applications"
mkdir -p "$DEST_HOME"
rm -rf "$DEST_HOME/Norma.app"
cp -R "$ROOT/dist/Norma.app" "$DEST_HOME/Norma.app"

echo ""
echo "=== Done ==="
echo "App bundle:  $ROOT/dist/Norma.app"
echo "Installed:   $DEST_HOME/Norma.app"
echo ""
echo "Abrir con:"
echo "  open \"$DEST_HOME/Norma.app\""
echo "O arrastrá dist/Norma.app a /Applications"
