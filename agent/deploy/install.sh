#!/usr/bin/env bash
# Instalador idempotente de microesp-agent (Linux + systemd).
#
#   sudo ./install.sh [--binary RUTA] [--no-start]
#   sudo ./install.sh --uninstall [--purge]
#   ./install.sh --dry-run [...]        # muestra las acciones, no requiere root
#
# Busca el binario en: --binary, ../microesp-agent (tarball de release) o lo
# compila con Go si se ejecuta desde el árbol de fuentes.
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
SVC_USER=microesp
SVC_GROUP=dialout
SERVICE=microesp-agent.service

DRY_RUN=0
UNINSTALL=0
PURGE=0
START=1
BINARY=""

usage() {
	sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
}

log() { printf '==> %s\n' "$*"; }
warn() { printf 'AVISO: %s\n' "$*" >&2; }
die() {
	printf 'ERROR: %s\n' "$*" >&2
	exit 1
}

# run ejecuta un comando o, en --dry-run, solo lo muestra.
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
		[ $# -ge 2 ] || die "--binary necesita una ruta"
		BINARY="$2"
		shift
		;;
	-h | --help)
		usage
		exit 0
		;;
	*) die "opción desconocida: $1 (usa --help)" ;;
	esac
	shift
done

if [ "$DRY_RUN" -eq 0 ] && [ "$(id -u)" -ne 0 ]; then
	die "debe ejecutarse como root (sudo $0 ...) o con --dry-run"
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

resolve_binary() {
	if [ -n "$BINARY" ]; then
		[ -f "$BINARY" ] || die "no existe el binario $BINARY"
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
		log "compilando microesp-agent $version" >&2
		run env CGO_ENABLED=0 go -C "$SRC_DIR" build -trimpath \
			-ldflags "-s -w -X main.version=$version" -o "$out" ./cmd/microesp-agent >&2
		printf '%s\n' "$out"
		return
	fi
	die "no se encontró el binario: usa --binary RUTA o instala Go"
}

do_install() {
	local bin
	bin="$(resolve_binary)"

	log "grupo $SVC_GROUP y usuario de servicio $SVC_USER"
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

	log "binario -> $PREFIX_BIN"
	run install -D -m 0755 -o root -g root "$bin" "$PREFIX_BIN"

	log "configuración en $CONF_DIR"
	run install -d -m 0750 -o root -g "$SVC_USER" "$CONF_DIR"
	if [ -e "$CONF_FILE" ]; then
		log "se conserva $CONF_FILE existente"
	else
		run install -m 0640 -o root -g "$SVC_USER" "$SCRIPT_DIR/agent.toml.example" "$CONF_FILE"
	fi

	log "reglas udev y polkit"
	run install -D -m 0644 -o root -g root "$SCRIPT_DIR/udev/99-microesp.rules" "$UDEV_FILE"
	reload_udev
	if [ -d /etc/polkit-1/rules.d ] || [ "$DRY_RUN" -eq 1 ]; then
		run install -D -m 0644 -o root -g root "$SCRIPT_DIR/polkit/50-microesp.rules" "$POLKIT_FILE"
	else
		warn "polkit no encontrado: el servicio no podrá apagar/reiniciar sin root"
	fi

	log "unidad systemd $SERVICE"
	run install -D -m 0644 -o root -g root "$SCRIPT_DIR/systemd/microesp-agent.service" "$UNIT_FILE"
	if systemd_running || [ "$DRY_RUN" -eq 1 ]; then
		run systemctl daemon-reload
		if [ "$START" -eq 1 ]; then
			run systemctl enable "$SERVICE"
			# restart (no "start") para recoger un binario nuevo al reinstalar.
			run systemctl restart "$SERVICE"
		else
			run systemctl enable "$SERVICE"
		fi
	else
		warn "systemd no está activo; habilita $SERVICE manualmente"
	fi

	if [ ! -e "$KEY_FILE" ]; then
		cat <<EOF

Instalación completada. Falta emparejar con el dongle:
  sudo systemctl stop $SERVICE
  sudo $PREFIX_BIN pair          # introduce el código de 6 dígitos de la pantalla
  sudo systemctl start $SERVICE
EOF
	else
		log "instalación completada (clave existente en $KEY_FILE)"
	fi
}

do_uninstall() {
	log "desinstalando microesp-agent"
	if systemd_running || [ "$DRY_RUN" -eq 1 ]; then
		if [ -e "$UNIT_FILE" ] || [ "$DRY_RUN" -eq 1 ]; then
			run systemctl disable --now "$SERVICE" || true
		fi
	fi
	for f in "$UNIT_FILE" "$POLKIT_FILE" "$UDEV_FILE" "$PREFIX_BIN"; do
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
		log "se conserva $CONF_DIR (configuración y clave); usa --purge para borrarlo"
	fi
	log "desinstalación completada"
}

if [ "$UNINSTALL" -eq 1 ]; then
	do_uninstall
else
	do_install
fi
