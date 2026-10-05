#!/usr/bin/env bash
# Idempotent installer for microesp-agent (Linux + systemd).
#
#   sudo ./install.sh [--binary PATH] [--no-start] [--scripts-user NAME]
#   sudo ./install.sh --uninstall [--purge]
#   ./install.sh --dry-run [...]        # shows the actions, does not require root
#
# --scripts-user NAME (or MICROESP_SCRIPTS_USER=NAME) also installs the user
# scripts runner (microesp-scripts.socket/.service) running as that existing
# user, and points scripts_socket in agent.toml at it. The agent service is
# unchanged. A reinstall keeps an installed runner and its user.
#
# Looks for the binary in: --binary, ../microesp-agent (release tarball) or
# builds it with Go when run from the source tree.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

PREFIX_BIN=/usr/local/bin/microesp-agent
CONF_DIR=/etc/microesp
CONF_FILE=$CONF_DIR/agent.toml
KEY_FILE=$CONF_DIR/agent.key
UNIT_FILE=/etc/systemd/system/microesp-agent.service
POLKIT_FILE=/etc/polkit-1/rules.d/50-microesp.rules
UDEV_FILE=/etc/udev/rules.d/99-microesp.rules
RUNNER_SOCKET_FILE=/etc/systemd/system/microesp-scripts.socket
RUNNER_SERVICE_FILE=/etc/systemd/system/microesp-scripts.service
RUNNER_SOCKET=microesp-scripts.socket
RUNNER_SERVICE=microesp-scripts.service
SCRIPTS_SOCK=/run/microesp/scripts.sock
SCRIPTS_USER="${MICROESP_SCRIPTS_USER:-}"
SVC_USER=microesp
SVC_GROUP=dialout
SERVICE=microesp-agent.service

DRY_RUN=0
UNINSTALL=0
PURGE=0
START=1
BINARY=""

usage() {
	sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'
}

log() { printf '==> %s\n' "$*"; }
warn() { printf 'WARNING: %s\n' "$*" >&2; }
die() {
	printf 'ERROR: %s\n' "$*" >&2
	exit 1
}

# run executes a command or, with --dry-run, only prints it.
run() {
	if [ "$DRY_RUN" -eq 1 ]; then
		printf '[dry-run]'
		printf ' %q' "$@"
		printf '\n'
	else
		"$@"
	fi
}

while [ $# -gt 0 ]; do
	case "$1" in
	--dry-run) DRY_RUN=1 ;;
	--uninstall) UNINSTALL=1 ;;
	--purge) PURGE=1 ;;
	--no-start) START=0 ;;
	--binary)
		[ $# -ge 2 ] || die "--binary needs a path"
		BINARY="$2"
		shift
		;;
	--scripts-user)
		[ $# -ge 2 ] || die "--scripts-user needs a user name"
		SCRIPTS_USER="$2"
		shift
		;;
	-h | --help)
		usage
		exit 0
		;;
	*) die "unknown option: $1 (use --help)" ;;
	esac
	shift
done

if [ "$DRY_RUN" -eq 0 ] && [ "$(id -u)" -ne 0 ]; then
	die "must be run as root (sudo $0 ...) or with --dry-run"
fi

have() { command -v "$1" >/dev/null 2>&1; }

systemd_running() { [ -d /run/systemd/system ]; }

reload_udev() {
	if have udevadm; then
		run udevadm control --reload-rules
		run udevadm trigger --action=change --subsystem-match=tty
		run udevadm trigger --action=add --subsystem-match=usb --attr-match=idVendor=303a --attr-match=idProduct=4002
	fi
}

# resolve_scripts_user validates --scripts-user, or takes the user of an
# installed runner unit on reinstall. Empty = no runner.
resolve_scripts_user() {
	if [ -z "$SCRIPTS_USER" ] && [ -f "$RUNNER_SERVICE_FILE" ]; then
		SCRIPTS_USER="$(sed -n 's/^User=//p' "$RUNNER_SERVICE_FILE" | head -n 1)"
		if [ -n "$SCRIPTS_USER" ]; then
			log "keeping the scripts runner as user $SCRIPTS_USER (installed unit)"
		fi
	fi
	[ -n "$SCRIPTS_USER" ] || return 0
	printf '%s' "$SCRIPTS_USER" | grep -Eq '^[a-z_][a-z0-9_.-]*[$]?$' || die "invalid user name: $SCRIPTS_USER"
	case "$SCRIPTS_USER" in
	root | "$SVC_USER") die "--scripts-user must be a regular user, not $SCRIPTS_USER" ;;
	esac
	if ! id "$SCRIPTS_USER" >/dev/null 2>&1; then
		if [ "$DRY_RUN" -eq 1 ]; then
			warn "user $SCRIPTS_USER does not exist (dry run continues)"
		else
			die "user $SCRIPTS_USER does not exist (--scripts-user needs an existing account)"
		fi
	fi
}

# install_runner_unit SRC DEST renders a runner unit for SCRIPTS_USER.
install_runner_unit() {
	local src="$1" dest="$2" tmp
	if [ "$DRY_RUN" -eq 1 ]; then
		printf '[dry-run] render %q (User %s) > %q\n' "$src" "$SCRIPTS_USER" "$dest"
		return
	fi
	tmp="$(mktemp)"
	sed "s/@SCRIPTS_USER@/$SCRIPTS_USER/g" "$src" >"$tmp"
	install -D -m 0644 -o root -g root "$tmp" "$dest"
	rm -f "$tmp"
}

# set_scripts_socket sets scripts_socket in agent.toml, keeping the rest of
# the file, its owner and its mode. A new key goes before the first table
# ([[scripts]]), where a top-level key must be.
set_scripts_socket() {
	local line="scripts_socket = \"$SCRIPTS_SOCK\"" tmp
	if [ "$DRY_RUN" -eq 1 ]; then
		printf '[dry-run] set %s in %q\n' "$line" "$CONF_FILE"
		return
	fi
	if grep -Eq '^[[:space:]]*scripts_socket[[:space:]]*=' "$CONF_FILE"; then
		sed -i -E "s|^[[:space:]]*scripts_socket[[:space:]]*=.*|$line|" "$CONF_FILE"
		return
	fi
	tmp="$(mktemp)"
	awk -v line="$line" '
		function emit() {
			print "# Set by install.sh --scripts-user: scripts run in microesp-scripts.service."
			print line
			done = 1
		}
		!done && /^[[:space:]]*\[/ { emit(); print "" }
		{ print }
		END { if (!done) { print ""; emit() } }
	' "$CONF_FILE" >"$tmp"
	cat "$tmp" >"$CONF_FILE"
	rm -f "$tmp"
}

resolve_binary() {
	if [ -n "$BINARY" ]; then
		[ -f "$BINARY" ] || die "binary does not exist: $BINARY"
		printf '%s\n' "$BINARY"
		return
	fi
	if [ -f "$SRC_DIR/microesp-agent" ] && [ -x "$SRC_DIR/microesp-agent" ]; then
		printf '%s\n' "$SRC_DIR/microesp-agent"
		return
	fi
	if [ -f "$SRC_DIR/go.mod" ] && have go; then
		local out version
		if [ "$DRY_RUN" -eq 1 ]; then
			out="/tmp/microesp-build.XXXXXX/microesp-agent"
		else
			out="$(mktemp -d)/microesp-agent"
		fi
		version="$(git -C "$SRC_DIR" describe --tags --always --dirty 2>/dev/null || echo dev)"
		log "building microesp-agent $version" >&2
		run env CGO_ENABLED=0 go -C "$SRC_DIR" build -trimpath \
			-ldflags "-s -w -X main.version=$version" -o "$out" ./cmd/microesp-agent >&2
		printf '%s\n' "$out"
		return
	fi
	die "binary not found: use --binary PATH or install Go"
}

do_install() {
	local bin
	resolve_scripts_user
	bin="$(resolve_binary)"

	log "group $SVC_GROUP and service user $SVC_USER"
	if ! getent group "$SVC_GROUP" >/dev/null; then
		run groupadd --system "$SVC_GROUP"
	fi
	if id "$SVC_USER" >/dev/null 2>&1; then
		run usermod -a -G "$SVC_GROUP" "$SVC_USER"
	else
		run useradd --system --user-group --no-create-home --home-dir /nonexistent \
			--shell /usr/sbin/nologin --groups "$SVC_GROUP" \
			--comment "MicroESP agent" "$SVC_USER"
	fi

	log "binary -> $PREFIX_BIN"
	run install -D -m 0755 -o root -g root "$bin" "$PREFIX_BIN"

	log "configuration in $CONF_DIR"
	run install -d -m 0750 -o root -g "$SVC_USER" "$CONF_DIR"
	if [ -e "$CONF_FILE" ]; then
		log "keeping existing $CONF_FILE"
	else
		run install -m 0640 -o root -g "$SVC_USER" "$SCRIPT_DIR/agent.toml.example" "$CONF_FILE"
	fi

	log "udev and polkit rules"
	run install -D -m 0644 -o root -g root "$SCRIPT_DIR/udev/99-microesp.rules" "$UDEV_FILE"
	reload_udev
	if [ -d /etc/polkit-1/rules.d ] || [ "$DRY_RUN" -eq 1 ]; then
		run install -D -m 0644 -o root -g root "$SCRIPT_DIR/polkit/50-microesp.rules" "$POLKIT_FILE"
	else
		warn "polkit not found: the service will not be able to power off/reboot without root"
	fi

	if [ -n "$SCRIPTS_USER" ]; then
		log "user scripts runner as $SCRIPTS_USER ($RUNNER_SOCKET -> $SCRIPTS_SOCK)"
		install_runner_unit "$SCRIPT_DIR/systemd/microesp-scripts.socket" "$RUNNER_SOCKET_FILE"
		install_runner_unit "$SCRIPT_DIR/systemd/microesp-scripts.service" "$RUNNER_SERVICE_FILE"
		set_scripts_socket
	fi

	log "systemd unit $SERVICE"
	run install -D -m 0644 -o root -g root "$SCRIPT_DIR/systemd/microesp-agent.service" "$UNIT_FILE"
	if systemd_running || [ "$DRY_RUN" -eq 1 ]; then
		run systemctl daemon-reload
		if [ "$START" -eq 1 ]; then
			run systemctl enable "$SERVICE"
			# restart (not "start") to pick up a new binary on reinstall.
			run systemctl restart "$SERVICE"
		else
			run systemctl enable "$SERVICE"
		fi
		if [ -n "$SCRIPTS_USER" ]; then
			run systemctl enable --now "$RUNNER_SOCKET"
			# Pick up a new binary or config if the runner is already running.
			run systemctl try-restart "$RUNNER_SERVICE"
		fi
	else
		warn "systemd is not active; enable $SERVICE manually"
	fi

	if [ ! -e "$KEY_FILE" ]; then
		cat <<EOF

Installation complete. Pairing with the dongle is still needed:
  sudo systemctl stop $SERVICE
  sudo $PREFIX_BIN pair          # enter the 6-digit code shown on the screen
  sudo systemctl start $SERVICE
EOF
	else
		log "installation complete (existing key in $KEY_FILE)"
	fi
}

do_uninstall() {
	log "uninstalling microesp-agent"
	if systemd_running || [ "$DRY_RUN" -eq 1 ]; then
		if [ -e "$UNIT_FILE" ] || [ "$DRY_RUN" -eq 1 ]; then
			run systemctl disable --now "$SERVICE" || true
		fi
		if [ -e "$RUNNER_SOCKET_FILE" ] || [ -e "$RUNNER_SERVICE_FILE" ] || [ "$DRY_RUN" -eq 1 ]; then
			run systemctl disable --now "$RUNNER_SOCKET" "$RUNNER_SERVICE" || true
		fi
	fi
	for f in "$UNIT_FILE" "$RUNNER_SOCKET_FILE" "$RUNNER_SERVICE_FILE" "$POLKIT_FILE" "$UDEV_FILE" "$PREFIX_BIN"; do
		if [ -e "$f" ] || [ "$DRY_RUN" -eq 1 ]; then
			run rm -f "$f"
		fi
	done
	if systemd_running || [ "$DRY_RUN" -eq 1 ]; then
		run systemctl daemon-reload
	fi
	reload_udev
	if id "$SVC_USER" >/dev/null 2>&1 || [ "$DRY_RUN" -eq 1 ]; then
		run userdel "$SVC_USER" || true
	fi
	if [ "$PURGE" -eq 1 ]; then
		run rm -rf "$CONF_DIR"
	elif [ -d "$CONF_DIR" ]; then
		log "keeping $CONF_DIR (configuration and key); use --purge to remove it"
	fi
	log "uninstall complete"
}

if [ "$UNINSTALL" -eq 1 ]; then
	do_uninstall
else
	do_install
fi
