# Spike MESP-US-0003 — TuyaOpen + TinyUSB (HID + CDC) en ESP32-S3

App TuyaOpen basada en `apps/tuya_cloud/switch_demo` (TuyaOpen v1.9.0, ESP-IDF v5.4) que además levanta un dispositivo USB compuesto con `esp_tinyusb` 2.3.0. Entorno y comandos: `docs/dev-setup.md`.

## Veredicto

**TuyaOpen puede alojar TinyUSB sin modificar el SDK.** La plataforma ESP32 de TuyaOpen compila un proyecto ESP-IDF (`platform/ESP32/tuya_open_sdk`) y admite componentes ESP-IDF propios de la app en `<app>/esp_components/` (variable `TUYAOS_EXTRA_COMPONENT_DIRS`, soportada por `tos.py`). Las opciones Kconfig de TinyUSB se inyectan con `SDKCONFIG_DEFAULTS` (ver `build.sh`). No hace falta plan B (proyecto ESP-IDF propio + librerías cloud de TuyaOpen).

Puntos de fricción (todos resueltos sin tocar el código de TuyaOpen):
- La tabla de 16 MB requiere un board propio (`board/POCKET_DONGLE_S3`, registrado con `install-board.sh`: enlace simbólico + entrada en `boards/ESP32/Kconfig` del checkout local).
- Hay que fijar `espressif/esp-sr==2.4.7` (la última versión arrastra esp-dl incompatible con IDF v5.4.0).
- El código de la app (`src/`) se compila con CMake de TuyaOpen y no ve las cabeceras ESP-IDF: llama a los componentes por prototipos `extern`; el componente fuerza el enlace con `-u usb_composite_start`.

## Qué hay

| Ruta | Contenido |
|---|---|
| `src/tuya_main.c` | switch_demo + `usb_composite_start()` + `pinout_probe_start()` + log TuyaOpen duplicado a un buffer |
| `esp_components/usb_composite/` | descriptores, HID teclado de arranque, CDC con eco/comandos, entrada a modo descarga, redes de seguridad |
| `esp_components/pinout_probe/` | reutiliza `hw/spikes/pinout/main/main.c` (patrón LCD + ciclo LED/retroiluminación) |
| `board/POCKET_DONGLE_S3/` | board TuyaOpen (16 MB, PSRAM) |
| `sdkconfig.microesp` | `TINYUSB_CDC_ENABLED`, `HID_COUNT=1`, callbacks suspend/resume |
| `tools/flash.sh`, `tools/touch1200.py`, `tools/mesp_cdc.py` | flasheo sin BOOT y consola CDC |
| `src/tuya_config_secrets.h` | **ignorado por git**; UUID/AuthKey de relleno |

## Evidencias (2026-10-03, placa real)

Enumeración (`lsusb -v -d 303a:4002`, sysfs `/sys/bus/usb/devices/3-4`):

```
303a:4002 MicroESP MicroESP, serial MESP-907069f662dc, bcdDevice 1.00, USB 2.00 FS (12 Mb/s)
bmAttributes 0xa0 (Remote Wakeup), MaxPower 100mA, power/wakeup=enabled
3-4:1.0 class 03/01/01 (HID boot keyboard)  driver=usbhid
3-4:1.1 class 02/02/00 (CDC ACM)            driver=cdc_acm
3-4:1.2 class 0a       (CDC data)           driver=cdc_acm
/dev/serial/by-id/usb-MicroESP_MicroESP_MESP-907069f662dc-if01
/dev/input/by-id/usb-MicroESP_MicroESP_MESP-907069f662dc-event-kbd
```

- CDC: eco de líneas (`hola` → `echo: hola`), líneas de 300 B, comandos `!status`/`!log`; tras basura binaria (esptool contra el CDC) el parser descarta la línea y sigue funcionando.
- HID: `!key` envía Shift izq. pulsar/soltar; leído en `/dev/input/event26` como `EV_KEY 42 1` / `EV_KEY 42 0`.
- `!wake` sin bus suspendido → `ESP_ERR_NOT_ALLOWED` (esperado). **Remote wakeup real no probado**: exigiría suspender este PC (prohibido en este entorno); queda para MESP-US-0002.
- TuyaOpen en paralelo (log por `!log`): Wi-Fi inicializado en softAP `SmartLife-62DD` (netcfg AP), controlador BT habilitado (`esp_bt_controller_get_status()=2`), `tuya ble init success`, `Start Adv`, `tuya_iot STATE_START`. Sin credenciales válidas (`tuyaopen_license_read read failure`), así que no llega a la nube: esperado.
- Reflasheo sin BOOT verificado 5 veces (`!dfu` ×3, 1200-baud touch ×2) y `!usj` + esptool normal verificado.

## Memoria y particiones

Tabla (`partitions_16M.csv` de TuyaOpen, OTA dual):

| Partición | Offset | Tamaño |
|---|---|---|
| nvs | 0x9000 | 16 KB |
| otadata | 0xd000 | 8 KB |
| phy_init | 0xf000 | 4 KB |
| ota_0 | 0x10000 | 7,4 MB (0x760000) |
| ota_1 | 0x770000 | 7,4 MB |
| model (spiffs, esp-sr) | 0xED0000 | 960 KB |
| tuya (KV) | 0xFC0000 | 240 KB |
| factory_nvs | 0xFFC000 | 16 KB |

- Imagen app: **1,21 MB** (16 % de un slot OTA; 84 % libre). Sin `pinout_probe` el tamaño es prácticamente igual.
- Estático (`esp_idf_size`): IRAM 16 383 / 16 384 B (**100 %**), D/IRAM 142 KB usados / 199 KB libres, flash .text 884 KB + .rodata 195 KB.
- Heap en ejecución (Wi-Fi AP + BLE + TinyUSB): interno libre **~115 KB** (mín. 110 KB); con `pinout_probe` (framebuffer 25 KB en RAM interna + SPI/RMT) ~62–93 KB. PSRAM libre 8,2 MB.
- `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` **desactivado** en el sdkconfig de TuyaOpen (relevante para MESP-US-0011).

## Reflasheo sin pulsar BOOT

TinyUSB se queda con el PHY (desaparece `303a:1001`). El firmware ofrece:

| Vía | Efecto |
|---|---|
| 1200-baud touch (`tools/touch1200.py`), `!dfu`, BOOT ≥2 s | PHY → USB-Serial/JTAG, `RTC_CNTL_FORCE_DOWNLOAD_BOOT`, `esp_restart()` → ROM en modo descarga en `303a:1001` |
| `!usj` | reinicia una vez sin TinyUSB (devuelve el mux del PHY al hardware) → esptool con reset normal |
| Sin enumerar en 20 s / 3 crashes seguidos | igual que `!usj` (automático) |

Después: esptool con la secuencia de reset **por defecto** (`tools/flash.sh` hace todo). Con `--before no_reset` el reset final por RTS deja la placa otra vez en modo descarga (DTR queda activo); se sale abriendo el puerto con DTR=0 y pulsando RTS, o con otro esptool normal.

## Riesgos

- IRAM al 100 %: añadir código en IRAM (ISR, `IRAM_ATTR`) fallará al enlazar; habrá que desactivar opciones `*_IRAM_OPT` de Wi-Fi/LWIP.
- La consola de TuyaOpen va a UART0 (GPIO43/44, no accesible) y al USJ, que desaparece con TinyUSB: los logs solo se ven con `!log` (buffer de 64 KB en PSRAM). Para el firmware real conviene un segundo CDC o multiplexar logs en el protocolo.
- Board y fijación de esp-sr viven fuera del árbol de TuyaOpen pero modifican el checkout local (`install-board.sh`); al actualizar TuyaOpen hay que re-ejecutarlo.
- Si la placa queda en un estado sin USB (p. ej. bloqueo con el PHY en OTG), el único recurso es BOOT al enchufar.
