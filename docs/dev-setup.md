# Entorno de desarrollo MicroESP (MESP-US-0005)

Verificado en Linux (Pop!_OS / Ubuntu 24.04, x86_64) el 2026-10-03, sin `sudo`. Todo se instala en espacio de usuario bajo `/www/MicroESP/tools`.

## 1. Versiones

| Componente | Versión | Ubicación |
|---|---|---|
| TuyaOpen | **v1.9.0** (commit `b80932d`) | `/www/MicroESP/tools/TuyaOpen` |
| Entorno Python de TuyaOpen | uv 0.11.18 + Python 3.12.13 (`.venv/`) | dentro de TuyaOpen |
| Plataforma ESP32 de TuyaOpen | `TuyaOpen-esp32` commit `e2b4b26` | `TuyaOpen/platform/ESP32` |
| ESP-IDF (descargado por TuyaOpen) | **v5.4** (tag `67c1de1e`; aparece como `v5.4-dirty` porque TuyaOpen sustituye `tools/idf_tools.py`) | `TuyaOpen/platform/ESP32/esp-idf` |
| Herramientas IDF | xtensa-esp-elf-gcc 14.2.0 (esp-14.2.0_20241119), ninja 1.12.1, esptool.py 4.12.0 | `TuyaOpen/platform/ESP32/.espressif` |
| Componentes IDF usados | esp_tinyusb 2.3.0, tinyusb 0.21.0~2, led_strip 3.0.3, esp-sr **2.4.7 (fijado)** | gestor de componentes |
| Go | 1.26.6 (ya instalado en el sistema) | — |
| esptool independiente (opcional) | 5.4.0 en un venv propio | cualquier venv |

Espacio en disco: ~2 GB TuyaOpen + 2 GB esp-idf + 3,8 GB herramientas `.espressif`.

## 2. Instalación

```bash
mkdir -p /www/MicroESP/tools && cd /www/MicroESP/tools
git clone --depth 1 --branch v1.9.0 https://github.com/tuya/TuyaOpen
cd TuyaOpen
mkdir -p .cache && touch .cache/.dont_prompt_update_platform   # evita prompts interactivos
. ./export.sh            # crea .venv con uv, instala Python 3.12.13, deja tos.py en el PATH
tos.py check             # git/cmake/make/ninja + submódulos
```

La plataforma ESP32 y su ESP-IDF se descargan solos en el primer `tos.py build` de una app ESP32 (clona `TuyaOpen-esp32`, ESP-IDF v5.4 con submódulos y ejecuta `install.sh esp32s3`). Tarda ~10–15 min.

Scripts de entorno (creados en `/www/MicroESP/tools`):

```bash
source /www/MicroESP/tools/tos-env.sh   # entorno TuyaOpen (tos.py)
source /www/MicroESP/tools/idf-env.sh   # ESP-IDF "pelado" reutilizando el de TuyaOpen (idf.py)
```

`idf-env.sh` exporta `IDF_PATH=/www/MicroESP/tools/TuyaOpen/platform/ESP32/esp-idf` e `IDF_TOOLS_PATH=…/platform/ESP32/.espressif`. ninja no venía en las herramientas instaladas por TuyaOpen; se añadió con:

```bash
source /www/MicroESP/tools/idf-env.sh
python $IDF_PATH/tools/idf_tools.py install ninja
```

No mezclar los dos entornos en la misma shell: usar uno por terminal (o subshell).

### Problemas encontrados y arreglos

1. **esp-sr / esp-dl incompatibles con IDF v5.4.0**: TuyaOpen pide `espressif/esp-sr ^2.0.0` sin fichero de bloqueo; hoy se resuelve a esp-sr 2.5.5 → esp-dl 3.3.x, que usa `MALLOC_CAP_SIMD` (no existe en IDF v5.4.0) y la compilación falla. Arreglo: el componente de la app fija `espressif/esp-sr: "==2.4.7"` (`hw/spikes/tuya-usb/esp_components/usb_composite/idf_component.yml`). Si se cambia, borrar `TuyaOpen/platform/ESP32/tuya_open_sdk/dependencies.lock`.
2. **Tamaño de flash**: el board genérico `ESP32-S3` de TuyaOpen usa la tabla de 4 MB. Para la de 16 MB (OTA dual de 7,4 MB) hace falta un board que seleccione `PLATFORM_FLASHSIZE_16M`. Se creó el board `POCKET_DONGLE_S3` (fuente en `hw/spikes/tuya-usb/board/`), registrado en el checkout de TuyaOpen con `hw/spikes/tuya-usb/install-board.sh` (enlace simbólico + entrada en `boards/ESP32/Kconfig`; idempotente, `build.sh` lo ejecuta).
3. **sdkconfig**: TuyaOpen copia un `sdkconfig` fijo por chip (`sdkconfig_esp32s3_uart`). Las opciones propias (TinyUSB) se añaden con la variable de entorno estándar de ESP-IDF `SDKCONFIG_DEFAULTS="<plataforma>/sdkconfig.defaults;<app>/sdkconfig.microesp"`, que exporta `build.sh`. Tras editar `sdkconfig.microesp`, compilar con `./build.sh clean`.
4. `idf.py size` dentro de `tuya_open_sdk/` reconfigura sin el entorno de TuyaOpen: no usarlo. Para medir memoria: `python -m esp_idf_size dist/tuya-usb_1.0.0/tuya-usb_1.0.0.map` (entorno IDF).

## 3. Compilar

Spike TuyaOpen + TinyUSB (app TuyaOpen con componentes ESP-IDF propios en `esp_components/`):

```bash
cd /www/MicroESP/microesp/hw/spikes/tuya-usb
./build.sh          # incremental;  ./build.sh clean  tras tocar sdkconfig.microesp o el board
# salida: dist/tuya-usb_1.0.0/{bootloader.bin,partition-table.bin,ota_data_initial.bin,tuya-usb.bin,srmodels.bin,tuya-usb_QIO_1.0.0.bin}
```

Las credenciales van en `src/tuya_config_secrets.h` (ignorado por git; `build.sh` lo crea desde `.example` con valores de relleno).

Spike de pinout (ESP-IDF puro):

```bash
source /www/MicroESP/tools/idf-env.sh
cd /www/MicroESP/microesp/hw/spikes/pinout
idf.py set-target esp32s3   # solo la primera vez
idf.py build
```

## 4. Acceso al puerto serie (`sg dialout`)

El usuario ya está en el grupo `dialout` (`scripts/setup-serial-access.sh`), pero las shells abiertas antes no lo tienen activo. Hasta volver a iniciar sesión, envolver **todo** comando que abra el puerto:

```bash
sg dialout -c "comando ..."
```

Puertos (estables por número de serie):

| Estado del dongle | USB | Puerto |
|---|---|---|
| Firmware con TinyUSB (MicroESP) | `303a:4002` | `/dev/serial/by-id/usb-MicroESP_MicroESP_MESP-<mac>-if01` (CDC) |
| USB-Serial/JTAG (firmware sin TinyUSB, o ROM en modo descarga) | `303a:1001` | `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_<MAC>-if00` |

## 5. Flashear

### 5.1 Firmware sin TinyUSB (USB-Serial/JTAG activo)

```bash
sg dialout -c "bash -c 'source /www/MicroESP/tools/idf-env.sh; cd /www/MicroESP/microesp/hw/spikes/pinout; idf.py -p /dev/ttyACM0 flash monitor'"
```

### 5.2 Firmware con TinyUSB (sin pulsar BOOT) — verificado

Con TinyUSB activo el PHY USB lo usa el USB-OTG y desaparece el USB-Serial/JTAG. El firmware entra en **modo descarga ROM** por cualquiera de estas vías:

- *1200-baud touch*: abrir el CDC a 1200 bps y bajar DTR (`hw/spikes/tuya-usb/tools/touch1200.py`).
- Comando `!dfu` por el CDC.
- Mantener BOOT pulsado ≥2 s con el firmware en marcha.

El firmware devuelve el PHY al USB-Serial/JTAG, escribe `RTC_CNTL_FORCE_DOWNLOAD_BOOT` y reinicia: el dongle reaparece como `303a:1001` en modo descarga. Después se flashea con la secuencia de reset **por defecto** de esptool (con `--before no_reset` el reset final vuelve a dejarlo en modo descarga). Todo junto:

```bash
sg dialout -c /www/MicroESP/microesp/hw/spikes/tuya-usb/tools/flash.sh
```

Otros comandos CDC: `!usj` (reinicia **una vez** sin TinyUSB → esptool normal funciona), `!status`, `!log`, `!key`, `!wake`, `!reboot`.

```bash
sg dialout -c "bash -c 'source /www/MicroESP/tools/idf-env.sh; python /www/MicroESP/microesp/hw/spikes/tuya-usb/tools/mesp_cdc.py !status !log'"
```

Redes de seguridad del firmware: si el host no lo enumera en 20 s, o si hay 3 reinicios seguidos por crash, arranca sin TinyUSB (USB-Serial/JTAG disponible).

### 5.3 Modo descarga manual (último recurso)

1. Desenchufar el dongle.
2. Mantener pulsado **BOOT** (GPIO0).
3. Enchufar el dongle sin soltar BOOT; soltar tras 1 s.
4. Aparece `303a:1001`; flashear con esptool o `idf.py flash`.
5. Desenchufar y volver a enchufar (o reset de esptool) para arrancar el firmware.

## 6. Restaurar el firmware de fábrica

Backup completo de 16 MB: `hw/factory-backup/pocket-dongle-s3_factory_16MB.bin` (comprobar desde la raíz del repo con `sha256sum -c hw/factory-backup/SHA256SUMS`).

```bash
# Si corre un firmware con TinyUSB, ponerlo antes en modo descarga (touch 1200 / !dfu / BOOT al enchufar)
sg dialout -c "bash -c 'source /www/MicroESP/tools/idf-env.sh; python -m esptool --chip esp32s3 -p /dev/ttyACM0 -b 921600 write_flash 0x0 /www/MicroESP/microesp/hw/factory-backup/pocket-dongle-s3_factory_16MB.bin'"
```

Escribir la imagen completa restaura también la NVS y la tabla de particiones originales.
