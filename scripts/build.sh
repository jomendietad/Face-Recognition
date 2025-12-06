#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"

# --- AGREGADO: Limpieza automática ---
# Esto elimina la carpeta build existente para forzar una compilación limpia
if [ -d "$BUILD_DIR" ]; then
    echo "⚠️  Limpiando carpeta build antigua para evitar errores de caché..."
    rm -rf "$BUILD_DIR"
fi
# -------------------------------------

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# If there's a top-level CMakeLists.txt use it, otherwise fall back to src/
if [ -f "$ROOT_DIR/CMakeLists.txt" ]; then
	cmake ..
else
	cmake ../src
fi

cmake --build . -- -j$(nproc)

echo "[OK] Build completed. Binaries in: $BUILD_DIR"