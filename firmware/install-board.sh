#!/usr/bin/env bash
# Register the out-of-tree board POCKET_DONGLE_S3 into the local TuyaOpen checkout
# (TuyaOpen v1.9.0 only discovers boards under boards/<PLATFORM>/). Idempotent.
set -euo pipefail
APP_DIR="$(cd "$(dirname "$0")" && pwd)"
TOS=${OPEN_SDK_ROOT:-/www/MicroESP/tools/TuyaOpen}
B=$TOS/boards/ESP32
ln -sfn "$APP_DIR/board/POCKET_DONGLE_S3" "$B/POCKET_DONGLE_S3"
printf 'CONFIG_BOARD_CHOICE_ESP32=y\nCONFIG_BOARD_CHOICE_POCKET_DONGLE_S3=y\n' > "$B/config/POCKET_DONGLE_S3.config"
if ! grep -q BOARD_CHOICE_POCKET_DONGLE_S3 "$B/Kconfig"; then
  python3 - "$B/Kconfig" <<'PY'
import sys
p=sys.argv[1]; s=open(p).read()
marker='# <new-board-add: This line cannot be deleted or modified>'
entry='''    config BOARD_CHOICE_POCKET_DONGLE_S3
        bool "POCKET_DONGLE_S3"
        if (BOARD_CHOICE_POCKET_DONGLE_S3)
            rsource "./POCKET_DONGLE_S3/Kconfig"
        endif

'''
s=s.replace(marker, entry+marker,1); open(p,'w').write(s)
PY
fi
echo "POCKET_DONGLE_S3 registered in $B"
