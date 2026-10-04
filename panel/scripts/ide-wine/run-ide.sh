#!/usr/bin/env bash
# Launch the Tuya MiniApp IDE installed by setup.sh. Wine output goes to
# $WINEPREFIX/wine-run.log (it may contain session tokens: do not share it).
set -euo pipefail
export WINEPREFIX=${WINEPREFIX:-$HOME/.wine-tuya} WINEARCH=win64 WINEDEBUG=-all
cd "$WINEPREFIX/drive_c/users/$USER/AppData/Local/Programs/Tuya MiniApp IDE"
exec wine "Tuya MiniApp IDE.exe" --no-sandbox --disable-gpu "$@" >"$WINEPREFIX/wine-run.log" 2>&1
