#!/bin/sh
# Detect the DRM card that has an HDMI connector.
# On Pi 4 this is always card0; on Pi 5 the RP1/V3D can enumerate as card0
# pushing the display card to card1. Probing /sys avoids hardcoding a card
# number and makes the same image work on both boards.
CARD=""
for dev in /dev/dri/card*; do
    cardname=$(basename "$dev")
    for conn in /sys/class/drm/"${cardname}"-HDMI-A-*; do
        [ -e "$conn" ] && CARD="$dev" && break 2
    done
done

# Fallback to card0 if no HDMI connector found (e.g. vcan0 dev/test env)
: "${CARD:=/dev/dri/card0}"

# Write a per-boot KMS config so Qt eglfs opens the correct DRM device.
KMS_CFG=/run/vcu-dashboard-kms.json
printf '{"device":"%s"}\n' "$CARD" > "$KMS_CFG"
export QT_QPA_EGLFS_KMS_CONFIG="$KMS_CFG"

exec /usr/bin/vcu-dashboard can0
