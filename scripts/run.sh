#!/usr/bin/env bash
set -euo pipefail

# ------------------------------------------------------------------
# CONFIGURATION
# ------------------------------------------------------------------
BUILD_DIR="build"
EXEC="${BUILD_DIR}/sistema_final"
CAM_INDEX="${1:-0}"
CASCADE="${2:-assets/haarcascades/haarcascade_frontalface_default.xml}"
LOG_PATH="${3:-logs/event_log.csv}"

# ------------------------------------------------------------------
# VALIDATION
# ------------------------------------------------------------------
if [[ ! -x "${EXEC}" ]]; then
  echo "Error: Executable ${EXEC} not found."
  echo "Please run 'scripts/build.sh' first."
  exit 1
fi

# ------------------------------------------------------------------
# ROOT PRIVILEGE CHECK
# ------------------------------------------------------------------
# Framebuffer access (/dev/fb0) and PAM authentication usually 
# require root privileges. We auto-elevate using sudo if needed.
if [ "$EUID" -ne 0 ]; then
  echo "Warning: This demo requires root privileges for Framebuffer/PAM."
  echo "Elevating permissions (sudo)..."
  exec sudo "$0" "$@"
  exit
fi

# ------------------------------------------------------------------
# EXECUTION
# ------------------------------------------------------------------
echo "Starting Face Access Demo (Headless Mode)..."
echo "Camera Index: ${CAM_INDEX}"
echo "Web Server Port: 8080"

# Run the executable directly
"${EXEC}" "${CAM_INDEX}" "${CASCADE}" "${LOG_PATH}"