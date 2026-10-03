#!/usr/bin/env bash
# Build the MicroESP TuyaOpen + TinyUSB spike.
#   ./build.sh          -> incremental build
#   ./build.sh clean    -> tos.py clean first (needed after editing sdkconfig.microesp)
set -euo pipefail
APP_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=/dev/null
source /www/MicroESP/tools/tos-env.sh
PLAT=/www/MicroESP/tools/TuyaOpen/platform/ESP32/tuya_open_sdk
# Platform defaults (copied by TuyaOpen from sdkconfig_esp32s3_uart) + our overlay.
export SDKCONFIG_DEFAULTS="$PLAT/sdkconfig.defaults;$APP_DIR/sdkconfig.microesp"
cd "$APP_DIR"
"$APP_DIR/install-board.sh" >/dev/null
[ -f src/tuya_config_secrets.h ] || cp src/tuya_config_secrets.h.example src/tuya_config_secrets.h
if [ "${1:-}" = "clean" ]; then tos.py clean -f 2>/dev/null || tos.py clean; fi
tos.py build
