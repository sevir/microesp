#!/usr/bin/env bash
# Install the Tuya MiniApp IDE (Windows build) under Wine, with Node for
# Windows and the build patch. Tested with IDE 0.10.9, Wine 11 and Node 22.
#
# Usage: setup.sh "<path to Tuya MiniApp IDE Setup X.Y.Z.exe>"
# Needs: wine (64-bit), 7zz (or 7z), curl, sha256sum, node (to run the patch).
# Env: WINEPREFIX (default ~/.wine-tuya), NODE_VERSION (default v22.23.3).
set -euo pipefail

installer=${1:?usage: setup.sh <Tuya MiniApp IDE Setup .exe>}
here=$(cd "$(dirname "$0")" && pwd)
export WINEPREFIX=${WINEPREFIX:-$HOME/.wine-tuya} WINEARCH=win64 WINEDEBUG=-all
node_version=${NODE_VERSION:-v22.23.3}
sevenzip=$(command -v 7zz || command -v 7z) || { echo "7zz or 7z is required" >&2; exit 1; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

echo "== Wine prefix $WINEPREFIX"
[ -d "$WINEPREFIX/drive_c" ] || wineboot --init

# The NSIS installer fails under Wine: unpack the app from its payload instead.
echo "== Extracting the IDE"
ide="$WINEPREFIX/drive_c/users/$USER/AppData/Local/Programs/Tuya MiniApp IDE"
"$sevenzip" x -y -o"$work/nsis" "$installer" >/dev/null
rm -rf "$ide" && mkdir -p "$ide"
"$sevenzip" x -y -o"$ide" "$work/nsis/\$PLUGINSDIR/app-64.7z" >/dev/null

# The IDE refuses to build without Node >= 16 on the Windows PATH.
echo "== Node $node_version for Windows"
zip="node-$node_version-win-x64.zip"
curl -sSL -o "$work/$zip" "https://nodejs.org/dist/$node_version/$zip"
(cd "$work" && curl -sSL "https://nodejs.org/dist/$node_version/SHASUMS256.txt" | grep " $zip\$" | sha256sum -c - >/dev/null)
nodedir="$WINEPREFIX/drive_c/Program Files/nodejs"
"$sevenzip" x -y -o"$work/node" "$work/$zip" >/dev/null
rm -rf "$nodedir" && mv "$work/node/node-$node_version-win-x64" "$nodedir"

key='HKLM\System\CurrentControlSet\Control\Session Manager\Environment'
path=$(wine reg query "$key" /v PATH 2>/dev/null | tr -d '\r' | awk -F'REG_EXPAND_SZ|REG_SZ' '/PATH/{gsub(/^ +/,"",$2); print $2}')
case "$path" in
  *'Program Files\nodejs'*) ;;
  *)
    mkdir -p "$WINEPREFIX/drive_c/users/$USER/AppData/Roaming/npm"
    wine reg add "$key" /v PATH /t REG_EXPAND_SZ /f \
      /d "$path;C:\\Program Files\\nodejs;C:\\users\\$USER\\AppData\\Roaming\\npm" >/dev/null
    ;;
esac
wineserver -w
wine cmd /c "node --version" | tr -d '\r'

echo "== Build patch"
node "$here/patch-ide.mjs" "$ide"

echo "== Done. Next: cd panel && npm install && npm run ide:win-natives, then $here/run-ide.sh"
