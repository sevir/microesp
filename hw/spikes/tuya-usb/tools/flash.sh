#!/usr/bin/env bash
# Flash the tuya-usb spike WITHOUT pressing BOOT.
#   1. If the device runs TinyUSB (303a:4002), ask it to enter ROM download mode
#      (1200-baud touch on its CDC port) -> it re-appears as 303a:1001.
#   2. esptool (IDF env) with default reset sequence writes the images and resets.
# Usage: tools/flash.sh            (needs dialout: wrap with  sg dialout -c "...")
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
D="$HERE/dist/tuya-usb_1.0.0"
# shellcheck source=/dev/null
source /www/MicroESP/tools/idf-env.sh >/dev/null 2>&1
shopt -s nullglob
CDCS=(/dev/serial/by-id/usb-MicroESP_MicroESP_MESP-*-if01)
CDC=${CDCS[0]:-}
if [ -n "$CDC" ]; then
  echo "TinyUSB device found ($CDC): 1200-baud touch -> ROM download mode"
  python "$HERE/tools/touch1200.py" "$CDC"
  for _ in $(seq 1 40); do lsusb | grep -q "303a:1001" && break; sleep 0.25; done
  sleep 1
fi
PORTS=(/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_*-if00)
PORT=${PORTS[0]:-}
[ -n "$PORT" ] || { echo "No USB-Serial/JTAG port found. Fallback: hold BOOT while plugging in." >&2; exit 1; }
python -m esptool --chip esp32s3 -p "$PORT" -b 921600 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0 "$D/bootloader.bin" 0x8000 "$D/partition-table.bin" 0xd000 "$D/ota_data_initial.bin" \
  0x10000 "$D/tuya-usb.bin" 0xed0000 "$D/srmodels.bin"
