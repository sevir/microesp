#!/usr/bin/env bash
# Flash the last firmware build to the dongle, from any directory.
#
#   scripts/dongle-flash.sh          # app image + otadata (keeps NVS: Wi-Fi, TuyaLink, agent key)
#   scripts/dongle-flash.sh --full   # bootloader, partition table, app and models
#
# Stops microesp-agent while flashing and starts it again at the end (sudo).
# Build first with: cd firmware && ./build.sh
set -euo pipefail
# shellcheck source=lib/dongle-common.sh
source "$(dirname "$0")/lib/dongle-common.sh"

MODE=--app
case "${1:-}" in
"" | --app) ;;
--full) MODE= ;;
*)
	echo "usage: $0 [--app|--full]" >&2
	exit 2
	;;
esac

agent_release
echo "==> flashing (${MODE:-full})"
with_dialout "$FW/tools/flash.sh" ${MODE:+"$MODE"}
echo "==> waiting for the dongle to come back"
wait_cdc && sleep 2
