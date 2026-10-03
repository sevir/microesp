# Guía de instalación de MicroESP

Historia: MESP-US-0034. Pasos de principio a fin para dejar funcionando un dongle MicroESP con el PC: flashear el firmware, cargar las credenciales de Tuya, emparejar con Smart Life, instalar el agente, emparejar el agente con el dongle y verificar el conjunto.

Necesitas:

- El dongle Pocket-Dongle-S3 (ESP32-S3 con 16 MB de flash, pantalla ST7735).
- El PC destino con Linux y systemd (probado en Pop!_OS / Ubuntu 24.04). Windows es opcional: ver [`agent/README.md`](../../agent/README.md#windows-opcional).
- Una cuenta en la app **Smart Life** (o Tuya Smart) y Wi-Fi de **2,4 GHz**.
- Las credenciales de Tuya del producto MicroESP: **PID** del producto y una licencia TuyaOpen (**UUID** + **AuthKey**). Son secretas: no las publiques ni las subas al repositorio.
- Acceso de administrador (`sudo`) en el PC.

> Si compilas desde el código fuente, prepara antes el entorno con [`docs/dev-setup.md`](../dev-setup.md).

## 0. Copia de seguridad del firmware de fábrica (una sola vez)

Antes del primer flasheo, guarda la flash original completa (16 MB) para poder volver atrás. Pon el dongle en modo descarga (mantén **BOOT** pulsado mientras lo enchufas) y ejecuta:

```bash
python -m esptool --chip esp32s3 -p /dev/ttyACM0 -b 921600 read_flash 0x0 0x1000000 pocket-dongle-s3_factory_16MB.bin
sha256sum pocket-dongle-s3_factory_16MB.bin > SHA256SUMS
```

Guárdala fuera del repositorio (git ignora `hw/factory-backup/*.bin`). Para restaurarla, sigue [`docs/dev-setup.md` §6](../dev-setup.md#6-restaurar-el-firmware-de-fábrica).

## 1. Flashear el firmware

Tu usuario debe pertenecer al grupo `dialout` (`scripts/setup-serial-access.sh`). Si acabas de añadirlo, antepón `sg dialout -c "..."` a los comandos hasta que vuelvas a iniciar sesión.

### Opción A: desde una release

Descarga de la release `vX.Y.Z` el fichero `microesp-firmware_X.Y.Z_merged.bin` y `SHA256SUMS`, y comprueba la suma:

```bash
sha256sum -c --ignore-missing SHA256SUMS
```

Pon el dongle en modo descarga. La primera vez, mantén **BOOT** pulsado al enchufarlo. Si ya tiene MicroESP, sirven `!dfu`, el *1200-baud touch* o mantener BOOT ≥ 20 s. Después:

```bash
python -m esptool --chip esp32s3 -p /dev/ttyACM0 -b 921600 write_flash 0x0 microesp-firmware_X.Y.Z_merged.bin
```

Desenchufa el dongle y vuelve a enchufarlo.

### Opción B: desde el código fuente

```bash
cd firmware
cp include/tuya_secrets.h.example include/tuya_secrets.h   # opcional: credenciales en compilación (§2)
./build.sh
sg dialout -c tools/flash.sh      # sin pulsar BOOT si ya corre MicroESP; si no, BOOT al enchufar
```

**Comprobación:** `lsusb | grep 303a:4002` muestra el dispositivo `MicroESP`, y aparece `/dev/serial/by-id/usb-MicroESP_MicroESP_MESP-*-if01`. La pantalla muestra el estado y el LED parpadea en azul porque el dongle aún no está aprovisionado.

## 2. Cargar las credenciales de Tuya

Prioridad: **NVS** (CLI) > almacén de licencias de TuyaOpen > `include/tuya_secrets.h` > valores de relleno. Con los valores de relleno el dongle arranca, pero no llega a la nube.

La forma recomendada es por la CLI del puerto CDC. Las credenciales se guardan en NVS y no quedan en ningún fichero:

```bash
source /www/MicroESP/tools/idf-env.sh        # o cualquier Python con pyserial
sg dialout -c "python firmware/tools/mesp_cdc.py '!auth <uuid> <authkey>' '!pid <pid>' '!reboot'"
```

Si el agente ya está instalado, páralo antes (`sudo systemctl stop microesp-agent`), porque abre el puerto en exclusiva. Para comprobar las credenciales, ejecuta `mesp_cdc.py '!status'`: la línea de Tuya debe mostrar el PID cargado.

## 3. Emparejar con Smart Life

1. Activa el Bluetooth y la ubicación del móvil, y conéctalo a la Wi-Fi de 2,4 GHz.
2. En Smart Life: **+** → **Añadir dispositivo**. El dongle se anuncia por **BLE** (con **AP** como respaldo) mientras el LED parpadea en azul.
3. Introduce la contraseña de la Wi-Fi y espera a que se complete el emparejado.
4. Si no aparece: mantén pulsado el botón del dongle **10 s** y suéltalo (reset de Tuya, vuelve al modo BLE/AP), o ejecuta `!reset-tuya` por la CLI.

**Comprobación:** en la app aparece el dispositivo con `pc_state` (DP 101) y el resto de DPs.

## 4. Instalar el agente en el PC

Desde una release (tarball `microesp-agent_X.Y.Z_linux_amd64.tar.gz`):

```bash
mkdir microesp-agent && tar xzf microesp-agent_X.Y.Z_linux_amd64.tar.gz -C microesp-agent && cd microesp-agent
sudo ./deploy/install.sh --binary ./microesp-agent
```

Desde el código fuente (necesita Go ≥ 1.23):

```bash
sudo agent/deploy/install.sh          # compila e instala; idempotente
./agent/deploy/install.sh --dry-run   # solo muestra lo que haría
```

El instalador crea el usuario `microesp`, instala el binario en `/usr/local/bin`, la configuración en `/etc/microesp/agent.toml`, la regla udev (permisos del puerto, `/dev/microesp` y `power/wakeup`), la regla polkit (solo apagar y reiniciar) y el servicio `microesp-agent`. Los detalles están en [`agent/README.md`](../../agent/README.md).

## 5. Emparejar el agente con el dongle

1. Pon el dongle en modo emparejado: **mantén el botón 3 s y suéltalo**. Si el dongle no tiene clave, entra solo en este modo al arrancar. La pantalla muestra un código de 6 dígitos durante 120 s.
2. En el PC:
   ```bash
   sudo systemctl stop microesp-agent
   sudo microesp-agent pair              # pide el código; o --code 123456
   sudo systemctl start microesp-agent
   ```
3. Tras 3 códigos erróneos el dongle sale del modo de emparejado. Vuelve al paso 1.

## 6. Configurar la BIOS y el sistema para el encendido

Sigue [`bios-lenovo.md`](bios-lenovo.md): ErP desactivado, Wake on LAN activado, USB alimentado en S4/S5, WOL en la NIC (`nmcli ... 802-3-ethernet.wake-on-lan magic`) y `power/wakeup` del dongle.

## 7. Verificación

| Comprobación | Cómo | Esperado |
|---|---|---|
| Servicio activo | `systemctl status microesp-agent`, `journalctl -u microesp-agent -n 50` | `active (running)`, sesión `ready` en el log |
| Estado del agente | `sudo microesp-agent status` | dongle detectado, clave presente, backend `systemd` |
| App | Smart Life | `pc_state = on`, `agent_online = true`, CPU/MEM/disco y hostname actualizados |
| Apagado sin riesgo | En `/etc/microesp/agent.toml` pon `dry_run = true`, reinicia el servicio y pulsa **Apagar** en la app | Cuenta atrás en el dongle; en el log, la orden recibida (sin ejecutarla); `last_result = ok` |
| Cancelación | Pulsa **Apagar** y, durante la cuenta atrás, haz una pulsación corta en el botón | `last_result = cancelled` |
| Encendido | [`bios-lenovo.md` §4](bios-lenovo.md#4-cómo-probarlo-sin-riesgo-primero-s3-luego-s5) | El PC despierta desde S3 (y desde S5 si la BIOS lo permite) |

Cuando todo funcione, vuelve a poner `dry_run = false`. El plan de pruebas completo está en [`docs/qa/e2e-v1.md`](../qa/e2e-v1.md).

## 8. Solución de problemas

| Síntoma | Causa probable / solución |
|---|---|
| No aparece `303a:4002` tras flashear | El firmware arrancó sin TinyUSB (red de seguridad: no se enumeró en 20 s o hubo 3 crashes seguidos). Aparece como `303a:1001`. Desenchufa, vuelve a enchufar y revisa `!log`. |
| `esptool` no conecta | El dongle no está en modo descarga. Mantén BOOT pulsado al enchufar. No uses `--before no_reset`. |
| `permission denied` en `/dev/ttyACM*` | El usuario no está en `dialout`, o no se ha aplicado la regla udev: `sudo udevadm trigger` o `sg dialout -c ...`. |
| `device or resource busy` | El agente tiene el puerto abierto: `sudo systemctl stop microesp-agent`. |
| El dongle no aparece en Smart Life | Wi-Fi de 5 GHz, Bluetooth o ubicación desactivados, o credenciales de relleno (`!status`). Haz el reset de Tuya (botón 10 s) y repite. |
| `cloud_lost` (bit 3 del DP 115) o iconos de nube tachados | No hay Wi-Fi o MQTT. Revisa la cobertura y que la licencia (UUID/AuthKey) sea válida. |
| `dongle error: not_paired` / `welcome signature invalid` | Vuelve a emparejar (§5). La clave anterior se sustituye en los dos lados. |
| `agent_offline` al apagar | El agente no está conectado: `systemctl status microesp-agent`. |
| `cmd_rejected` | El agente rechazó o no confirmó la orden en 10 s. Revisa en `journalctl -u microesp-agent` si hay `bad_sig`/`replay`. |
| `Access denied` / `interactive authentication required` al apagar | Falta la regla polkit, o hay un inhibidor activo (`systemd-inhibit --list`). |
| `hid_not_armed` (bit 2 del DP 115) | El host suspendió el USB sin armar el remote wakeup: revisa `power/wakeup` ([`bios-lenovo.md` §3.1](bios-lenovo.md#31-permitir-que-el-dongle-despierte-el-equipo-s3)). |
| `wake_failed` | En 120 s el PC no montó el bus USB. Revisa la BIOS (ErP, Always On USB), el WOL de la NIC y que el dongle tenga las MACs (`macs=` en `!status`). |
| ModemManager envía `AT` al dongle | Falta la regla udev (`ID_MM_DEVICE_IGNORE`). Reinstala el agente. |
| Recuperación total | Restaura el firmware de fábrica ([`docs/dev-setup.md` §6](../dev-setup.md#6-restaurar-el-firmware-de-fábrica)). |

Más casos del agente en [`agent/README.md`](../../agent/README.md#solución-de-problemas). Comandos de la CLI y significado del LED en [`firmware/README.md`](../../firmware/README.md).
