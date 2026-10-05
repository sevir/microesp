# Shared helpers for the scripts/ dongle and agent wrappers. Source it, do not run it.
# shellcheck shell=bash

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
FW="$ROOT/firmware"
AGENT_UNIT=microesp-agent.service
AGENT_WAS_ACTIVE=0

# with_dialout runs a command with the dialout group: directly if this shell already
# has it, otherwise through sg (shells opened before joining the group).
with_dialout() {
	if id -nG | tr ' ' '\n' | grep -qx dialout; then
		"$@"
	else
		sg dialout -c "$(printf '%q ' "$@")"
	fi
}

# agent_release stops the agent (it opens the CDC port exclusively) and restarts it
# when the script exits, whatever the outcome.
agent_release() {
	if systemctl is-active --quiet "$AGENT_UNIT"; then
		AGENT_WAS_ACTIVE=1
		echo "==> stopping $AGENT_UNIT (it holds the CDC port)"
		sudo systemctl stop "$AGENT_UNIT"
		trap agent_restore EXIT
	fi
}

agent_restore() {
	if [ "$AGENT_WAS_ACTIVE" = 1 ]; then
		echo "==> starting $AGENT_UNIT"
		sudo systemctl start "$AGENT_UNIT"
		AGENT_WAS_ACTIVE=0
	fi
}

# wait_cdc waits until the dongle's CDC port is back after a reset.
wait_cdc() {
	local i
	for i in $(seq 1 60); do
		compgen -G '/dev/serial/by-id/usb-MicroESP_MicroESP_MESP-*-if01' >/dev/null && return 0
		sleep 0.5
	done
	echo "warning: the dongle CDC port did not come back within 30 s" >&2
	return 1
}
