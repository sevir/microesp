# microesp-agent

Agente residente en el PC para el dongle USB **MicroESP**. Habla con el dongle por el puerto CDC (`/dev/ttyACM*`) según el protocolo [`cdc-v1`](../docs/protocol/cdc-v1.md) y:

- le envía la **telemetría** del PC: CPU %, memoria usada %, menor % libre de los discos configurados, uptime, hostname y MACs de las NIC físicas (para Wake-on-LAN);
- mantiene un **latido** para que el dongle sepa que el agente está vivo;
- ejecuta las órdenes **firmadas** de apagado y reinicio que llegan desde la app Tuya a través del dongle.

Cada orden se verifica (HMAC-SHA256 con la clave de emparejado y los nonces de la sesión, id creciente contra repeticiones, acción conocida). El agente responde con `ack` **antes** de ejecutarla.

## Requisitos

- Linux con systemd y polkit (probado en Ubuntu/Pop!_OS 24.04), o Windows 10/11 (opcional).
- Dongle con firmware MicroESP (USB `303a:4002`, producto `MicroESP`).
- Go ≥ 1.23 solo si se compila desde el código fuente.

## Compilar

```sh
make build             # ./microesp-agent (versión según git describe)
make build VERSION=1.0.0
make cross             # dist/: linux-amd64, linux-arm64, windows-amd64.exe
make test lint cover   # tests con -race, go vet, staticcheck, shellcheck, cobertura ≥70 %
```

Las releases se generan con `goreleaser release` (`.goreleaser.yaml`). Cada archivo incluye el binario, este README y `deploy/`.

## Instalación en Linux

```sh
sudo ./deploy/install.sh                  # compila (o usa ../microesp-agent) e instala
sudo ./deploy/install.sh --binary ./microesp-agent
./deploy/install.sh --dry-run             # solo muestra lo que haría (no requiere root)
```

El instalador es idempotente; puedes ejecutarlo de nuevo para actualizar. Hace lo siguiente:

| Paso | Resultado |
|---|---|
| usuario de sistema | `microesp` (sin shell ni home), miembro de `dialout` |
| binario | `/usr/local/bin/microesp-agent` |
| configuración | `/etc/microesp/agent.toml` (no se sobrescribe si ya existe), directorio `root:microesp 0750` |
| udev | `/etc/udev/rules.d/99-microesp.rules`: grupo `dialout`, symlink `/dev/microesp`, ModemManager ignora el puerto, `power/wakeup=enabled` para que el teclado HID del dongle pueda despertar el PC |
| polkit | `/etc/polkit-1/rules.d/50-microesp.rules`: el usuario `microesp` solo puede apagar o reiniciar (`org.freedesktop.login1.power-off`, `power-off-multiple-sessions`, `reboot`, `reboot-multiple-sessions`) |
| systemd | `/etc/systemd/system/microesp-agent.service` habilitado y arrancado |

La unidad corre sin privilegios y con un sandbox estricto (`ProtectSystem=strict`, `DevicePolicy=closed` + `DeviceAllow=char-ttyACM rw`, sin red IP, `NoNewPrivileges`). `systemd-analyze security microesp-agent` da una exposición de **1.2**.

Para desinstalar:

```sh
sudo ./deploy/install.sh --uninstall           # conserva /etc/microesp (config y clave)
sudo ./deploy/install.sh --uninstall --purge   # lo borra todo
```

## Emparejar con el dongle

Si el dongle no tiene clave, o si mantienes pulsado su botón 3 s, muestra en pantalla un código de 6 dígitos durante 120 s.

```sh
sudo systemctl stop microesp-agent        # el puerto serie se abre en exclusiva
sudo microesp-agent pair                  # pide el código (o: --code 123456)
sudo systemctl start microesp-agent
```

`pair` deriva la clave con HKDF-SHA256 a partir del código y de los nonces de ambos lados, y la guarda en `/etc/microesp/agent.key` (hex, modo `0600`, propietario `microesp`) solo después de verificar la respuesta `pair_ok` del dongle. Si vuelves a emparejar, la clave anterior queda sustituida en los dos lados. Tras 3 códigos erróneos el dongle sale del modo de emparejado.

## Configuración

Fichero TOML (por defecto `/etc/microesp/agent.toml`; en Windows `C:\ProgramData\MicroESP\agent.toml`). Puedes ver un ejemplo comentado en [`deploy/agent.toml.example`](deploy/agent.toml.example).

| Clave | Por defecto | Variable de entorno | Flag |
|---|---|---|---|
| `device` | `"auto"` | `MICROESP_DEVICE` | `--device` |
| `key_file` | `/etc/microesp/agent.key` | `MICROESP_KEY_FILE` | `--key-file` |
| `disks` | `["/"]` | `MICROESP_DISKS` (lista separada por comas) | — |
| `telemetry_interval` | `"10s"` | `MICROESP_TELEMETRY_INTERVAL` | — |
| `heartbeat_interval` | `"5s"` | `MICROESP_HEARTBEAT_INTERVAL` | — |
| `dry_run` | `false` | `MICROESP_DRY_RUN` | `--dry-run` |
| `power_backend` | `"systemd"` (`"windows"` en Windows) | `MICROESP_POWER_BACKEND` | `--power-backend` |
| `log_level` | `"info"` | `MICROESP_LOG_LEVEL` | `--log-level` |

Precedencia: valores por defecto < fichero < entorno < flags. Si no indicas `--config` y el fichero por defecto no existe, se usan los valores por defecto. Una clave desconocida es un error.

- **device**: con `auto` se busca un puerto con VID `303a` y PID `4002` (o cuyo producto contenga `MicroESP`). Si no aparece ninguno, se vuelve a buscar cada 2 s. Los fallos de apertura o de handshake se reintentan con backoff exponencial de 1 s a 30 s.
- **Telemetría**: la CPU se mide en la ventana entre dos muestras (una por cada `heartbeat_interval`). La memoria usada excluye caché y buffers. Para cada disco el % libre se calcula como en `df` y se informa el menor. Los valores se envían cada `telemetry_interval`, o antes si alguno cambia más de 2 puntos (20 décimas).
- **power_backend**:
  - `systemd` ejecuta `systemctl poweroff|reboot`, que pide la acción a logind por D-Bus y la autoriza la regla polkit.
  - `logind-dbus` llama directamente a `org.freedesktop.login1.Manager.PowerOff/Reboot` mediante `busctl`.
  - `windows` ejecuta `shutdown /s|/r /t 0`.
- **dry_run**: registra las órdenes en el log en lugar de ejecutarlas. Úsalo para probar.

## Uso

```sh
microesp-agent                 # = run
microesp-agent run --dry-run --log-level debug
microesp-agent status          # puertos detectados, estado de la clave y del backend
microesp-agent status --handshake   # además abre el puerto y autentica (detén antes el servicio)
microesp-agent version
journalctl -u microesp-agent -f
```

## Windows (opcional)

```powershell
# PowerShell como Administrador, junto a microesp-agent.exe
.\deploy\windows\install.ps1 -Binary .\microesp-agent.exe
Stop-Service MicroESPAgent; & "$env:ProgramFiles\MicroESP\microesp-agent.exe" pair --config "$env:ProgramData\MicroESP\agent.toml"; Start-Service MicroESPAgent
.\deploy\windows\install.ps1 -Uninstall [-Purge]
```

El servicio `MicroESPAgent` corre como LocalSystem y está integrado con el SCM mediante `kardianos/service`. La configuración y la clave se guardan en `C:\ProgramData\MicroESP`, con permisos restringidos a SYSTEM y Administradores.

## Solución de problemas

| Síntoma | Causa probable / solución |
|---|---|
| `dongle not found` en el log | El dongle no está enchufado o no ejecuta el firmware MicroESP. Comprueba `lsusb \| grep 303a` y que aparezca `/dev/microesp`. |
| `open /dev/ttyACM0: permission denied` | Falta la regla udev o el usuario no está en `dialout`. Ejecuta `sudo udevadm trigger` y comprueba `ls -l /dev/ttyACM*`. |
| `device or resource busy` al hacer `pair` o `status --handshake` | El servicio tiene el puerto abierto en exclusiva. Para antes el servicio con `sudo systemctl stop microesp-agent`. |
| `dongle error: not_paired` | Empareja con `microesp-agent pair`. |
| `welcome signature invalid` | La clave no coincide (el dongle se emparejó con otro equipo) o el dispositivo no es legítimo. Vuelve a emparejar. |
| `load key ... permissions too open` | Ejecuta `sudo chmod 600 /etc/microesp/agent.key && sudo chown microesp /etc/microesp/agent.key`. |
| `power action failed ... Access denied` / `interactive authentication required` | Falta la regla polkit o hay un **inhibidor** activo (por ejemplo, una actualización en curso; lo ves con `systemd-inhibit --list`). Por diseño, el agente no puede saltarse los inhibidores. |
| Ack `exec_failed` en la app | No se encontró el binario del backend (`systemctl`, `busctl` o `shutdown`). |
| ModemManager envía comandos `AT` al dongle | Falta la regla udev (`ID_MM_DEVICE_IGNORE`). |
| Pruebas sin riesgo | Arranca con `microesp-agent run --dry-run --log-level debug`. |

Los avisos de cuenta atrás (`notice`) se intentan difundir con `wall`, pero el sandbox de systemd normalmente lo impide. No pasa nada: la cuenta atrás se ve en la pantalla del dongle y en la app.

## Desarrollo

- `internal/proto`: mensajes, framing (512 B por línea) y HMAC/HKDF. Los tests usan los vectores normativos de `../protocol/testdata/vectors.json`.
- `internal/link`: descubrimiento, sesión, heartbeat, reconexión y reglas de comandos. Los tests usan `net.Pipe` y un dongle simulado (`internal/link/dongletest`).
- `internal/telemetry`: muestreo con gopsutil y la interfaz `Collector` (incluye una implementación falsa).
- `internal/power`: interfaz `Executor`, con backends systemd, logind-dbus, Windows y DryRun. Los tests nunca ejecutan una acción real.
- `internal/config`: TOML, entorno y fichero de clave.
