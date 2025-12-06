#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ASSETS_DIR="${ASSETS_DIR:-${PROJECT_ROOT}/assets/haarcascades}"
mkdir -p "${ASSETS_DIR}"

echo "Updating repositories..."
sudo apt update

# --- CAMBIOS REALIZADOS ---
# 1. Agregado 'libopencv-contrib-dev' para el reconocimiento facial (LBPH).
# 2. Cambiado 'libatlas-base-dev' por 'libopenblas-dev' (más compatible con RPi moderna).
# 3. 'libpam0g-dev' sigue ahí para la autenticación.
echo "Installing dependencies (OpenCV + PAM + Contrib)..."
sudo apt install -y git cmake g++ build-essential pkg-config \
    libopencv-dev libopencv-contrib-dev \
    libcamera-dev libopenblas-dev curl \
    libpam0g-dev

echo "[OK] Dependencies installed"

CASCADE_FILE="${ASSETS_DIR}/haarcascade_frontalface_default.xml"

# --- LIMPIEZA AUTOMÁTICA ---
# Si existe y es el archivo "Placeholder" falso, lo borramos para bajar el real.
if [ -f "${CASCADE_FILE}" ]; then
    if grep -q "Placeholder" "${CASCADE_FILE}"; then
        echo "⚠️  Detectado archivo 'placeholder' inválido. Eliminando para descargar el real..."
        rm "${CASCADE_FILE}"
    fi
fi

if [ ! -f "${CASCADE_FILE}" ]; then
    echo "Downloading haarcascade_frontalface_default.xml..."
    curl -L -o "${CASCADE_FILE}" \
      "https://raw.githubusercontent.com/opencv/opencv/master/data/haarcascades/haarcascade_frontalface_default.xml"
    echo "Downloaded to: ${CASCADE_FILE}"
else
    echo "Cascade already exists at: ${CASCADE_FILE}"
fi

echo "Installation complete. Run build with: scripts/build.sh"