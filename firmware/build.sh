#!/usr/bin/env bash
# Build the MicroESP firmware (TuyaOpen app + ESP-IDF components).
#   ./build.sh            -> incremental RELEASE build (MESP_DEV_CLI=n: restricted CDC CLI)
#   ./build.sh dev        -> incremental DEVELOPMENT build (MESP_DEV_CLI=y: full CLI, debug logs)
#   ./build.sh clean [dev]-> clean first (needed after editing sdkconfig.microesp / board / app_default.config)
#   ./build.sh test       -> host unit tests only (no toolchain needed)
# Both flavours write dist/microesp_<ver>/ (the last build wins; check "!help" or the
# boot log line "cli=dev|release" on the device).
# Environment:
#   OPEN_SDK_ROOT  TuyaOpen checkout (default /www/MicroESP/tools/TuyaOpen; CI sets it)
#   TOS_ENV        script sourced to get tos.py in PATH (default /www/MicroESP/tools/tos-env.sh,
#                  falling back to "$OPEN_SDK_ROOT/export.sh")
set -euo pipefail
APP_DIR="$(cd "$(dirname "$0")" && pwd)"
TOS_ROOT="${OPEN_SDK_ROOT:-/www/MicroESP/tools/TuyaOpen}"
if [ "${1:-}" = "test" ]; then exec make -C "$APP_DIR/test/host" TOS="${TOS:-$TOS_ROOT}"; fi
TOS_ENV="${TOS_ENV:-/www/MicroESP/tools/tos-env.sh}"
if [ -f "$TOS_ENV" ]; then
  # shellcheck source=/dev/null
  source "$TOS_ENV"
else
  # shellcheck source=/dev/null
  . "$TOS_ROOT/export.sh" </dev/null >/dev/null
fi
command -v tos.py >/dev/null || { echo "tos.py not found: check OPEN_SDK_ROOT / TOS_ENV" >&2; exit 1; }
export OPEN_SDK_ROOT="$TOS_ROOT"
PLAT="$TOS_ROOT/platform/ESP32/tuya_open_sdk"
# Platform defaults (copied by TuyaOpen from sdkconfig_esp32s3_uart) + our overlay (later wins).
export SDKCONFIG_DEFAULTS="$PLAT/sdkconfig.defaults;$APP_DIR/sdkconfig.microesp"
cd "$APP_DIR"
"$APP_DIR/install-board.sh" >/dev/null
MODE=release
for a in "$@"; do [ "$a" = "dev" ] && MODE=dev; done
if [ "${1:-}" = "clean" ]; then tos.py clean -f 2>/dev/null || tos.py clean; fi
python3 "$APP_DIR/tools/set_build_mode.py" "$MODE" "$APP_DIR" "$TOS_ROOT"
tos.py build
grep -q "^#define MESP_DEV_CLI 1" "$APP_DIR/.build/include/tuya_kconfig.h" && GOT=dev || GOT=release
[ "$GOT" = "$MODE" ] || { echo "build mode mismatch: wanted $MODE, header says $GOT" >&2; exit 1; }
echo "MicroESP $MODE build OK"

