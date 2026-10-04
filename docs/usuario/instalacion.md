# Guía de instalación de MicroESP

Historia: MESP-US-0034. Pasos de principio a fin para dejar funcionando un dongle MicroESP con el PC: flashear el firmware, cargar las credenciales de TuyaLink y la Wi-Fi, comprobar que aparece en Smart Life, instalar el agente, emparejar el agente con el dongle y verificar el conjunto.

Necesitas:

- El dongle Pocket-Dongle-S3 (ESP32-S3 con 16 MB de flash, pantalla ST7735).
- El PC destino con Linux y systemd (probado en Pop!_OS / Ubuntu 24.04). Windows es opcional: ver [`agent/README.md`](../../agent/README.md#windows-opcional).
- Una cuenta en la app **Smart Life** (o Tuya Smart) y Wi-Fi de **2,4 GHz** (con contraseña; el SSID no puede tener espacios).
- Las credenciales **TuyaLink** del dispositivo, que da la plataforma Tuya (platform.tuya.com) al crearlo en el producto MicroESP: **región** (`eu`, `us`, `cn` o `in`), **productId**, **deviceId** y **deviceSecret**. El dispositivo se vincula a tu cuenta de la app desde la plataforma. El deviceSecret es secreto: no lo publiques ni lo subas al repositorio. (Desde la versión 0.2.0 ya no hacen falta licencias TuyaOS UUID/AuthKey.)
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
./build.sh
sg dialout -c tools/flash.sh      # sin pulsar BOOT si ya corre MicroESP; si no, BOOT al enchufar
```

**Comprobación:** `lsusb | grep 303a:4002` muestra el dispositivo `MicroESP`, y aparece `/dev/serial/by-id/usb-MicroESP_MicroESP_MESP-*-if01`. La pantalla muestra el estado y el LED parpadea en azul porque el dongle aún no está aprovisionado.

## 2. Cargar las credenciales de TuyaLink y la Wi-Fi

Se cargan por la CLI del puerto CDC y se guardan en NVS (no quedan en ningún fichero). Flashear de nuevo no las borra. Si el agente ya está instalado, páralo antes (`sudo systemctl stop microesp-agent`), porque abre el puerto en exclusiva.

```bash
source /www/MicroESP/tools/idf-env.sh        # o cualquier Python con pyserial
sg dialout -c "python firmware/tools/mesp_cdc.py '!tylink <región> <productId> <deviceId> <deviceSecret>'"
sg dialout -c "python firmware/tools/mesp_cdc.py '!wifi <ssid> <contraseña>' '!reboot'"
```

- La contraseña Wi-Fi es **el resto de la línea** tras el SSID: puede llevar espacios.
- Para que el secreto no quede en el historial de la shell, puedes escribir los comandos en un terminal serie (`python -m serial.tools.miniterm /dev/serial/by-id/usb-MicroESP_*-if01`) en lugar de pasarlos como argumentos.
- En la versión release, `!tylink` y `!wifi` solo se aceptan la primera vez (dato aún no guardado). Para cambiarlos después, **mantén el botón entre 5 y 10 s y suéltalo**: se abre una ventana de 120 s ("Aprovisionar 120 s" en pantalla).
- El dongle nunca muestra el deviceSecret ni la contraseña: ni en `!status` ni en el log.

**Comprobación** (unos 10 s después del reinicio): `mesp_cdc.py '!status'` muestra

```
tylink: region=eu host=m1.tuyaeu.com product=<productId> device=26e0...0z provisioned=1 mqtt=connected ...
wifi: ssid=<ssid> configured=1 up=1 ip=192.168.x.y rssi=-58 ... time_synced=1
```

El LED deja de parpadear en azul y el icono de nube de la pantalla aparece sin tachar.

## 3. Comprobar el dispositivo en Smart Life

Con TuyaLink **no hay emparejado BLE/AP**. Pasos (validados el 2026-10-04 en Central Europe):

1. En la plataforma Tuya, el producto debe ser **TuyaLink** (no TuyaOS) y estar en el **mismo centro de datos** que tu cuenta de Smart Life (en España: Central Europe).
2. Registra el dispositivo en **Device Management** del producto: obtienes `productId`, `deviceId` y `deviceSecret` (los que se cargan con `!tylink`).
3. Con el dongle **conectado** (`!status` → `mqtt=connected`), abre el **QR del dispositivo** en Device Management y en Smart Life pulsa **+ → Escanear** (no "Añadir dispositivo"). Si el dongle está desconectado la vinculación falla.
4. Si en la app solo ves el control de la categoría (p. ej. enchufe) y no CPU/memoria/estado, cambia el **panel** del producto en la plataforma por uno que muestre todas las funciones.

Notas:
- La nube EU acepta los reportes sin confirmarlos (`property/report_response` solo llega si el mensaje pide `"sys":{"ack":1}`).
- Las órdenes de la app llegan como `property/set` con `msgId` numérico.

Solo puede haber **una conexión por deviceId**: si otro programa usa las mismas credenciales (por ejemplo el script de pruebas `hw/spikes/tylink_test.py`), la nube desconecta al dongle, que reintenta solo.

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
| El dongle no aparece en Smart Life | Mira `!status`. `wifi: ... up=0`: SSID o contraseña incorrectos, o red de 5 GHz (`last_reason` da el motivo de Wi-Fi). `time_synced=0`: la red bloquea NTP. `mqtt=connecting` con `last_err=4` o `5` (código CONNACK): credenciales TuyaLink rechazadas (repite `!tylink` con la ventana del botón de 5 s). `last_err=-1`: no hay conexión TLS con el broker (cortafuegos, puerto 8883). Si todo está `connected`, revisa en la plataforma que el dispositivo esté vinculado a tu cuenta. |
| `cloud_lost` (bit 3 del DP 114), LED rojo o iconos de nube tachados | 60 s sin sesión MQTT: igual que la fila anterior. También ocurre si otro cliente usa el mismo deviceId. |
| `dongle error: not_paired` / `welcome signature invalid` | Vuelve a emparejar (§5). La clave anterior se sustituye en los dos lados. |
| `agent_offline` al apagar | El agente no está conectado: `systemctl status microesp-agent`. |
| `cmd_rejected` | El agente rechazó o no confirmó la orden en 10 s. Revisa en `journalctl -u microesp-agent` si hay `bad_sig`/`replay`. |
| `Access denied` / `interactive authentication required` al apagar | Falta la regla polkit, o hay un inhibidor activo (`systemd-inhibit --list`). |
| `hid_not_armed` (bit 2 del DP 114) | El host suspendió el USB sin armar el remote wakeup: revisa `power/wakeup` ([`bios-lenovo.md` §3.1](bios-lenovo.md#31-permitir-que-el-dongle-despierte-el-equipo-s3)). |
| `wake_failed` | En 120 s el PC no montó el bus USB. Revisa la BIOS (ErP, Always On USB), el WOL de la NIC y que el dongle tenga las MACs (`macs=` en `!status`). |
| ModemManager envía `AT` al dongle | Falta la regla udev (`ID_MM_DEVICE_IGNORE`). Reinstala el agente. |
| Recuperación total | Restaura el firmware de fábrica ([`docs/dev-setup.md` §6](../dev-setup.md#6-restaurar-el-firmware-de-fábrica)). |

Más casos del agente en [`agent/README.md`](../../agent/README.md#solución-de-problemas). Comandos de la CLI y significado del LED en [`firmware/README.md`](../../firmware/README.md).
