#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
EXEC="${BUILD_DIR}/train_model"

echo "Building Trainer..."
# Reutilizamos la lógica de build para asegurar que el ejecutable exista
"${SCRIPT_DIR}/build.sh" > /dev/null

echo "Running Training..."
cd "${PROJECT_ROOT}"
if [ -f "$EXEC" ]; then
    "$EXEC"
else
    echo "Error: Trainer executable not found."
    exit 1
fi