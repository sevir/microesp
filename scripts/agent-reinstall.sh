#!/usr/bin/env bash
# Test, build and reinstall the PC agent from this checkout, from any directory.
#
#   scripts/agent-reinstall.sh
#
# The binary is stamped with CONFIG_PROJECT_VERSION (firmware/app_default.config).
# install.sh keeps /etc/microesp (agent.toml, key) and the scripts runner, and
# restarts microesp-agent.service (sudo).
set -euo pipefail
# shellcheck source=/dev/null
source "$(dirname "$0")/lib/dongle-common.sh"

VER=$(sed -n 's/^CONFIG_PROJECT_VERSION="\(.*\)"/\1/p' "$FW/app_default.config")
echo "==> testing and building microesp-agent v$VER"
make -C "$ROOT/agent" test
make -C "$ROOT/agent" build VERSION="v$VER"
echo "==> installing"
sudo "$ROOT/agent/deploy/install.sh" --binary "$ROOT/agent/microesp-agent"
systemctl --no-pager --lines=3 status "$AGENT_UNIT" || true
