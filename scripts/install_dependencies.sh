#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ASSETS_DIR="${ASSETS_DIR:-${PROJECT_ROOT}/assets/haarcascades}"
mkdir -p "${ASSETS_DIR}"

echo "Updating repositories..."
sudo apt update

# 1. Added 'libopencv-contrib-dev' for facial recognition (LBPH).
# 2. Switched 'libatlas-base-dev' to 'libopenblas-dev' (better for modern RPi).
# 3. 'libpam0g-dev' is kept for authentication.
echo "Installing dependencies (OpenCV + PAM + Contrib)..."
sudo apt install -y git cmake g++ build-essential pkg-config \
    libopencv-dev libopencv-contrib-dev \
    libcamera-dev libopenblas-dev curl \
    libpam0g-dev

echo "[OK] Dependencies installed"

CASCADE_FILE="${ASSETS_DIR}/haarcascade_frontalface_default.xml"

# --- AUTOMATIC CLEANUP ---
# If the file exists AND is the fake "Placeholder", delete it to download the real one.
if [ -f "${CASCADE_FILE}" ]; then
    if grep -q "Placeholder" "${CASCADE_FILE}"; then
        echo "⚠️  Invalid 'placeholder' file detected. Removing to download the real model..."
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

# ----------------------------------------------------------------
# AUTOMATIC LOGROTATE CONFIGURATION (Log Cleanup)
# ----------------------------------------------------------------
echo "Configuring automatic log rotation for the current user..."

# 1. Detect system variables
CURRENT_USER=$(whoami)
CURRENT_GROUP=$(id -gn)
# Calculate absolute path regardless of where the repo was cloned
PROJECT_ROOT_PATH="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_FILE="${PROJECT_ROOT_PATH}/logs/event_log.csv"
LOGROTATE_CONF_NAME="face_access_logs"
LOGROTATE_DEST="/etc/logrotate.d/${LOGROTATE_CONF_NAME}"

# 2. Create temp file with dynamic configuration
# We use 'cat' with 'EOF' to inject variables into the text
cat > "/tmp/${LOGROTATE_CONF_NAME}" <<EOF
${LOG_FILE} {
    weekly
    rotate 4
    compress
    missingok
    notifempty
    create 0664 ${CURRENT_USER} ${CURRENT_GROUP}
}
EOF

# 3. Move to system folder (Requires sudo)
echo "Installing rule to ${LOGROTATE_DEST}..."
sudo cp "/tmp/${LOGROTATE_CONF_NAME}" "${LOGROTATE_DEST}"
sudo chown root:root "${LOGROTATE_DEST}"
sudo chmod 644 "${LOGROTATE_DEST}"
rm "/tmp/${LOGROTATE_CONF_NAME}"

echo "[OK] Log rotation configured successfully."

# ----------------------------------------------------------------

echo "Installation complete. Run build with: scripts/build.sh"