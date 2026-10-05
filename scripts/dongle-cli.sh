#!/usr/bin/env bash
# Run dongle CLI commands over CDC, from any directory.
#
#   scripts/dongle-cli.sh                    # !status
#   scripts/dongle-cli.sh '!status' '!dp'    # several commands
#   scripts/dongle-cli.sh '!log' > dongle.log
#
# Stops microesp-agent while the port is in use and starts it again at the end (sudo).
# Release builds only accept read-only commands (!status !dp !log !version ...).
set -euo pipefail
# shellcheck source=/dev/null
source "$(dirname "$0")/lib/dongle-common.sh"

[ $# -gt 0 ] || set -- '!status'

agent_release
# esptool's Python env from IDF has pyserial
# shellcheck source=/dev/null
source /www/MicroESP/tools/idf-env.sh >/dev/null 2>&1
with_dialout python "$FW/tools/mesp_cdc.py" "$@"
