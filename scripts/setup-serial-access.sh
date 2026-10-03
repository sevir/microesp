#!/usr/bin/env bash
# Grant current user access to ESP32 serial ports (/dev/ttyACM*, /dev/ttyUSB*).
set -euo pipefail

TARGET_USER="${SUDO_USER:-$USER}"

if ! id -nG "$TARGET_USER" | tr ' ' '\n' | grep -qx dialout; then
  sudo usermod -aG dialout "$TARGET_USER"
  echo "Added $TARGET_USER to dialout (permanent after re-login)."
else
  echo "$TARGET_USER already in dialout."
fi

# Espressif native USB (303a): accessible without re-login via uaccess + dialout
sudo tee /etc/udev/rules.d/99-espressif.rules >/dev/null <<'RULES'
SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", MODE="0660", GROUP="dialout", TAG+="uaccess"
SUBSYSTEM=="usb", ATTRS{idVendor}=="303a", MODE="0660", GROUP="dialout", TAG+="uaccess"
RULES
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=tty --subsystem-match=usb --attr-match=idVendor=303a || true

ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || true
