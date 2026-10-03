# Firmware MicroESP (fase A)

Firmware de producción del dongle **MicroESP** (Pocket-Dongle-S3, clon de LilyGO T-Dongle-S3) sobre **TuyaOpen v1.9.0 / ESP-IDF v5.4**, board `POCKET_DONGLE_S3`. Reutiliza lo validado en el spike `hw/spikes/tuya-usb/` (TinyUSB dentro de TuyaOpen, board de 16 MB, reflasheo sin BOOT).

Estado: fase A completa (todo salvo el pulido de la interfaz). Pendiente de confirmar por el usuario: cableado y offsets del LCD, polaridad de la retroiluminación y tipo y pin del LED (`hw/pinout.md`). Faltan además las credenciales y el PID de Tuya.

Historias: MESP-US-0009, 0010, 0011, 0012, 0013, 0014, 0015, 0016, 0017/0018/0020 (versión básica), 0022 y 0023.

## Estructura

```
firmware/
├── app_default.config     # board, LVGL y VERSIÓN (CONFIG_PROJECT_VERSION, única fuente)
├── sdkconfig.microesp     # overlay ESP-IDF: TinyUSB y rollback OTA
├── Kconfig                # opción MESP_DEV_CLI (CLI de desarrollo; n = release)
├── build.sh               # compila (tos.py): release por defecto, "dev", "clean" y "test"
├── install-board.sh       # registra board/POCKET_DONGLE_S3 en el checkout de TuyaOpen
├── board/POCKET_DONGLE_S3 # board TuyaOpen (16 MB, PSRAM)
├── include/
│   ├── mesp_board.h       # PINES y parámetros de LCD/LED/botón (un único sitio)
│   ├── mesp_hal.h         # API entre la app TuyaOpen y el componente ESP-IDF
│   └── tuya_secrets.h.example
├── src/                   # app TuyaOpen (no ve cabeceras ESP-IDF)
│   ├── app_main.c         # arranque, bus de eventos y tarea de aplicación
│   ├── tuya_dp.c          # cliente Tuya, credenciales y capa de DPs
│   ├── usb_composite.c    # eventos USB → app y hid_not_armed
│   ├── agent_link.c       # sesión cdc-v1 con el agente
│   ├── pairing.c          # modo emparejado del agente (código de 6 dígitos)
│   ├── state.c            # pc_state (DP 101) y bitmap de fallos (DP 114)
│   ├── wake.c             # encendido HID / WOL
│   ├── power.c            # apagado/reinicio con cuenta atrás
│   ├── button.c led.c display.c ota.c cli.c
│   └── core/              # lógica en C puro, SIN dependencias de RTOS/IDF (tests en host)
│       ├── link_proto.c   # protocolo cdc-v1 (parser, sesión, firma, emparejado)
│       ├── mesp_crypto.c  # HMAC-SHA256 y HKDF-SHA256 (mbedTLS)
│       ├── pc_state.c     # máquina de estados del PC
│       ├── powercmd.c     # flujo de cuenta atrás → cmd → ack
│       ├── wake_fsm.c     # secuencia de encendido y paquete mágico WOL
│       ├── button_fsm.c   # gestos del botón
│       ├── dp_model.c     # tabla de DPs, umbrales y throttling
│       ├── cli_policy.c   # qué comandos CLI acepta cada compilación y ventana de aprovisionamiento
│       └── log_redact.c   # censura de líneas de log con secretos
├── esp_components/mesp_hal/  # componente ESP-IDF: TinyUSB, NVS, OTA, LCD, LED, botón, WOL
├── schema/dp.json         # descripción de los DPs (sincronizada con dp_model.c, lo comprueba un test)
├── test/host/             # tests Unity en el PC (gcc + Makefile, ASan/UBSan)
└── tools/                 # flash.sh, touch1200.py, mesp_cdc.py, set_build_mode.py
```

## Arquitectura

```
 Tuya cloud ⇄ [hilo tuya_app_main: tuya_iot_yield, BLE/AP, OTA] ─┐ eventos
 PC (agente) ⇄ CDC ⇄ [HAL: TinyUSB, despachador de líneas, supervisor] ─┤ (cola)
 Botón GPIO0 ─────────────────────────── sondeo cada 20 ms ─┐        │
                                                            ▼        ▼
                         [tarea mesp_app]: dueña de TODO el estado de la aplicación
                          link · power · wake · state · DPs · LED · CLI · OTA
                                         │ instantánea (mutex)
                                         ▼
                         [hilo mesp_ui]: LVGL v9 → ST7735 (SPI + DMA)
```

- **Bus de eventos**: cola TuyaOpen (`tal_queue`) de `app_ev_t` (`app_post()`, nunca bloquea). Los callbacks de TinyUSB, de Tuya y del despachador CDC solo publican eventos. La tarea `mesp_app` los consume y ejecuta un tick de 20 ms (botón) y otro de 100 ms (timeouts, estado, reporte de DPs, LED y pantalla). Así toda la lógica corre en un solo hilo y no necesita locks.
- **Llamadas a Tuya en un solo hilo**: el cliente MQTT de TuyaOpen (coreMQTT y la lista de publicaciones pendientes) no es thread-safe. Por eso `tuya_iot_dp_obj_report()` y `tuya_iot_reset()` se ejecutan en el hilo `tuya_app_main`, entre dos `tuya_iot_yield()`. La tarea `mesp_app` prepara una instantánea de los DPs que tocan, la deja en un buzón (un reporte en vuelo como máximo) y recibe el resultado como `EV_REPORT_DONE`. Latencia: hasta ~2 s (el bloqueo de `tuya_iot_yield`).
- **Watchdog software**: la tarea `mesp_app` avisa al supervisor del HAL en cada vuelta. Si pasa más de 60 s sin hacerlo, el supervisor reinicia el chip.
- **División app/HAL**: el código de `src/` lo compila el CMake de TuyaOpen, que no ve las cabeceras de ESP-IDF. Todo lo que es específico de IDF está en `esp_components/mesp_hal` (componente enlazado con `WHOLE_ARCHIVE`), y la app lo usa a través de `include/mesp_hal.h`, que solo contiene tipos de C estándar.
- **Núcleo testeable**: `src/core/` es C puro (cJSON + mbedTLS) y se prueba en el PC.
- **Versión**: `CONFIG_PROJECT_VERSION` en `app_default.config` → `PROJECT_VERSION` (TuyaOpen) → `MESP_FW_VERSION` (`src/mesp_version.h`). Se usa en Tuya/OTA, en el `welcome.fw`, en la pantalla y en `!version`.
- **Particiones**: `partitions_16M.csv` de TuyaOpen con OTA dual: nvs, otadata, `ota_0` y `ota_1` de 7,4 MB cada una, `model`, KV `tuya` y `factory_nvs`.

## Compilar

Entorno: `docs/dev-setup.md` (TuyaOpen en `/www/MicroESP/tools/TuyaOpen`).

```bash
cd firmware
./build.sh            # incremental, RELEASE (MESP_DEV_CLI=n: CLI restringida, log NOTICE)
./build.sh dev        # incremental, DESARROLLO (MESP_DEV_CLI=y: CLI completa, log DEBUG)
./build.sh clean [dev] # tras cambiar sdkconfig.microesp, board o app_default.config
./build.sh test       # solo tests en el host (no necesita toolchain)
# salida: dist/microesp_<ver>/{bootloader.bin, partition-table.bin, ota_data_initial.bin, microesp.bin, srmodels.bin, microesp_QIO_<ver>.bin}
```

Las dos variantes escriben en el mismo `dist/`: vale la última que se compile. `tools/set_build_mode.py` regenera `.build/cache/using.config` (y borra `using.cmake` y `tuya_kconfig.h`) solo cuando cambia la variante, y `build.sh` comprueba al final que la cabecera generada coincide con la pedida. En la placa se ve la variante en `!help`, en `!status` (`cli: build=...`) y en la línea de arranque del log (`cli=dev|release`).

Memoria (`python -m esp_idf_size dist/microesp_0.1.0/microesp_0.1.0.map` en el entorno IDF; no usar `idf.py size`, que reconfigura el proyecto):

| | v0.1.0 |
|---|---|
| Imagen app (release) | 1 594 224 B, 1,59 MB (21 % de un slot OTA de 7,4 MB); dev: +~350 B |
| Flash `.text` / `.rodata` | 1 147 KB / 304 KB |
| IRAM estática | 16 383 / 16 384 B (igual que el spike; no se ha añadido código `IRAM_ATTR`) |
| D/IRAM estática | 144 KB usados / 198 KB libres |
| Heap interno en ejecución | ~64,5 KB libres (mínimo 62,6 KB) con Wi-Fi, BLE, TinyUSB y LVGL; PSRAM libre 8,2 MB |

## Flashear (sin pulsar BOOT)

```bash
sg dialout -c "tools/flash.sh"        # completo (bootloader, tabla, otadata, app, srmodels)
sg dialout -c "tools/flash.sh --app"  # solo otadata + app
```

`flash.sh` hace un *1200-baud touch* sobre el CDC: el firmware pasa a modo descarga ROM (`303a:1001`). Después ejecuta esptool con la secuencia de reset **por defecto**. No uses nunca `--before no_reset`: el reset final por RTS dejaría la placa otra vez en modo descarga.

| Vía al modo descarga / USJ | Efecto |
|---|---|
| 1200-baud touch (`tools/touch1200.py`) o `!dfu` | modo descarga ROM |
| `!usj` | reinicia una vez sin TinyUSB (USB-Serial/JTAG, esptool normal) |
| BOOT mantenido ≥ 20 s | modo descarga (lo gestiona el supervisor del HAL, funciona aunque la app esté colgada) |
| Último recurso | BOOT pulsado al enchufar |

Verificado en placa: 1200-baud touch, `!dfu` y `!usj` seguidos de un esptool normal.

**Redes de seguridad** (heredadas del spike y ampliadas):
- Si no se enumera en 20 s, arranca una vez sin TinyUSB (USJ). Si en ese arranque no hay host USB (no llegan SOF durante 60 s, por ejemplo con el PC apagado y "Always On USB"), vuelve a TinyUSB con este fallback inhibido. Así el HID sigue disponible para encender el PC.
- Tras 3 reinicios seguidos por crash, arranca sin TinyUSB.
- `!log` vuelca el buffer de log de 64 KB en PSRAM (el log de TuyaOpen va a UART0, que no es accesible).

## Aprovisionamiento Tuya

Prioridad de credenciales: **NVS** (CLI) > almacén de licencias de TuyaOpen > `include/tuya_secrets.h` > valores de relleno. Con los valores de relleno el firmware compila y arranca, pero no llega a la nube.

```bash
cp include/tuya_secrets.h.example include/tuya_secrets.h   # ignorado por git; editar PID/UUID/AuthKey
# o en caliente por el CDC (se guardan en NVS, namespace "microesp"):
!auth <uuid> <authkey>
!pid <pid>
!reboot
```

**Aprovisionamiento en release**: `!auth` y `!pid` solo se aceptan si ese dato aún no está aprovisionado (la primera vez, con un dispositivo nuevo) o durante la **ventana de aprovisionamiento**: 120 s después de mantener el botón **entre 5 y 10 s** y soltarlo (la pantalla muestra "Suelta: aprovisionar" y luego "Aprovisionar 120 s"; `!status` indica `provisioning_window=<s>`). Fuera de la ventana responden `err: ... is locked`. "Aprovisionado" quiere decir credenciales en `tuya_secrets.h`, en el almacén de licencias o en NVS (también las escritas desde el arranque). En desarrollo se aceptan siempre y `!auth clear` borra de NVS el UUID, la AuthKey y el PID.

La AuthKey nunca se imprime: `!status` solo muestra el PID, los 6 primeros caracteres del UUID y el origen de las credenciales. Además se registra en el censor de logs (ver "Logs").

Emparejado con Smart Life: el dispositivo arranca en modo de configuración de red **BLE**, con **AP** como respaldo (`NETCFG_TUYA_BLE | NETCFG_TUYA_WIFI_AP`) mientras no esté activado. El LED parpadea en azul. Para desvincularlo: botón 10-20 s o `!reset-tuya` (solo desarrollo). El reinicio por 3 cortes de alimentación del spike se ha eliminado, porque los reinicios del PC lo disparaban.

## Emparejado del agente (cdc-v1 §4)

El dongle entra en modo emparejado si no tiene clave al arrancar o con el botón mantenido entre 3 y 5 s (presencia física). En desarrollo también con `!pair`. El código de 6 dígitos aleatorio se muestra **solo en pantalla** durante 120 s: nunca se registra en el log ni se envía por el CDC, y además está registrado en el censor de logs. Con 3 códigos erróneos sale del modo. `!unpair` (solo desarrollo) borra la clave; en release, un nuevo emparejado por botón sustituye la clave anterior.

```bash
microesp-agent pair --config agent.toml [--code 123456]
```

## CLI por el CDC

Las líneas que empiezan por `!` son CLI y las que empiezan por `{` son protocolo. El puerto es el mismo que usa el agente, que lo abre en exclusiva: para usar la CLI hay que parar antes el agente. El dongle cierra la sesión del agente en cuanto el host cierra el puerto (DTR baja).

Cualquier proceso con acceso al puerto (grupo `dialout` o root) puede escribir en la CLI. Por eso hay dos variantes de compilación (`Kconfig`: `MESP_DEV_CLI`; política en `src/core/cli_policy.c`, con tests):

| Comando | Release | Dev | Qué hace |
|---|---|---|---|
| `!status` | sí | sí | resumen: versión, USB, pc_state, fallos, agente, telemetría, power, wake, emparejado, variante/ventana, Tuya, heap. Sin secretos (ni claves ni código) |
| `!version`, `!dp`, `!help` | sí | sí | versión / valores actuales de los DPs (JSON) / ayuda |
| `!log` | sí | sí | buffer de log (HAL), censurado (ver "Logs") |
| `!cancel` | sí | sí | cancela la cuenta atrás o el `!cmd` programado |
| `!reboot`, `!dfu`, `!usj` | sí | sí | reinicio / modo descarga / un arranque sin TinyUSB (HAL) |
| `!auth <uuid> <authkey>`, `!pid <pid>` | sin aprovisionar o en la ventana de 120 s | sí | credenciales Tuya → NVS (se aplican con `!reboot`) |
| `!auth clear` | no | sí | borra de NVS el UUID, la AuthKey y el PID |
| `!pair`, `!unpair` | no | sí | modo emparejado del agente / olvidar la clave |
| `!wake [force]` | no | sí | sin `force` solo muestra el plan (simulación); con `force` ejecuta el encendido. Se ignora si el PC está encendido |
| `!method <hid\|wol\|hid_then_wol>`, `!countdown <0..60>` | no | sí | DP 109 / DP 112 sin pasar por la nube |
| `!cmd <shutdown\|reboot> [cuenta_atrás] [retardo_s]` | no | sí | simula DP 103/104. El retardo permite arrancar el agente antes de que se dispare |
| `!key` | no | sí | pulsa y suelta Shift izquierda (inofensivo; solo con el bus activo) |
| `!reset-tuya` | no | sí | desvincula de Tuya |

En release, un comando de desarrollo responde `err: <cmd> needs a development build` y uno de aprovisionamiento bloqueado `err: <cmd> is locked`. Cada comando se registra en el log solo por su nombre (nunca los argumentos), marcando `(refused)` si se rechaza. `!dfu` y el *1200-baud touch* siguen disponibles en release para poder reflashear sin botón. Esto supone que quien tiene acceso al puerto puede instalar otro firmware; es el mismo límite de confianza que root en el PC.

### Logs

El log de TuyaOpen y de la app sale por UART0 y se copia al buffer que vuelca `!log`. En release el nivel es NOTICE: los logs DEBUG/INFO de TuyaOpen pueden incluir el payload de activación, tokens o claves. En desarrollo el nivel es DEBUG. En las dos variantes, `src/core/log_redact.c` sustituye por `[redacted: sensitive log line]` cualquier línea que contenga un secreto registrado (AuthKey de Tuya, código de emparejado en curso) o una palabra sensible (`authkey`, `localkey`, `seckey`, `secret`, `regist_key`, `token`, `passwd`, `password`, `psk`...). La clave del agente es binaria y nunca se imprime.

Herramienta: `tools/mesp_cdc.py '!status' '!dp'` (con `sg dialout` y el Python del entorno IDF, que trae pyserial).

## Botón (GPIO0)

| Gesto | Acción |
|---|---|
| Pulsación corta | cancela la cuenta atrás de apagado/reinicio; si no hay, cambia de pantalla |
| Doble pulsación (< 400 ms) | enciende el PC (método del DP 109) |
| Mantener 3-5 s y soltar | modo emparejado del agente |
| Mantener 5-10 s y soltar | ventana de aprovisionamiento: `!auth`/`!pid` aceptados 120 s en release |
| Mantener 10-20 s y soltar | reset de fábrica Tuya (desvincula y vuelve a BLE/AP) |
| Mantener ≥ 20 s | modo descarga ROM (recuperación) |

Antirrebote de 40 ms. Las pulsaciones de 1 a 3 s se ignoran. Ningún gesto se acepta hasta que el botón se ha visto suelto al menos una vez (protege frente a un GPIO0 bloqueado a nivel bajo al arrancar). Mientras se mantiene pulsado, la pantalla indica qué pasará al soltar. Cambio respecto al spike: allí ≥ 2 s activaba el modo descarga.

## LED

Driver seleccionable en `include/mesp_board.h`: `MESP_LED_TYPE` (`WS2812` por RMT o `APA102` por bit-bang), `MESP_LED_PIN` (40 por defecto), `MESP_LED_PIN_CLK` y `MESP_LED_MAX_BRIGHTNESS` (48/255, brillo bajo).

| Prioridad | Situación | Color |
|---|---|---|
| 1 | cuenta atrás de apagado/reinicio | rojo parpadeo rápido (4 Hz) |
| 2 | encendido enviado (esperando al PC) | blanco pulsante |
| 3 | emparejando (agente) o Tuya sin aprovisionar | azul parpadeo (1 Hz) |
| 4 | error (fallos excepto `hid_not_armed`) | rojo fijo |
| 5 | PC encendido con agente | verde |
| 6 | PC encendido sin agente / arrancando | ámbar (arrancando: parpadeo) |
| 7 | PC apagado / suspendido / desconocido | blanco tenue |

## Pantalla (versión básica)

ST7735 en horizontal (160×80) con LVGL v9 de TuyaOpen en su propio hilo. Pines, offsets (`x=1`, `y=26`), `MADCTL=0x68`, inversión y polaridad de la retroiluminación están en `include/mesp_board.h` y se pueden sobrescribir con `-D`.

Pantallas: **estado** (estado del PC, hostname, iconos Wi-Fi/nube/agente y versión), **telemetría** (barras CPU / MEM / disco libre), **cuenta atrás** (automática, con "Pulsa para cancelar") y **código de emparejado** (automática). La pulsación corta alterna estado y telemetría; con el agente conectado rotan solas cada 5 s. Una línea inferior muestra avisos temporales. El pulido (iconos, tipografías, `docs/ui.md`) queda para la fase B.

## DPs

Tabla completa en `schema/dp.json`. Resumen:

- **101 `pc_state`**: estado del PC. **102 `power_on`**: pulsador; vuelve a `false`. **103/104 `power_off`/`reboot`**: `true` lanza la cuenta atrás y `false` durante la cuenta atrás la cancela; vuelven a `false` al terminar.
- **105/106/107**: CPU, memoria y disco libre en décimas de %, de 0 a 1000. **108 `agent_online`**. **109 `wake_method`**: guardado en NVS. **110 `pc_uptime`**. **111 `pc_hostname`**. **112 `cmd_countdown`**: de 0 a 60, 10 por defecto, guardado en NVS. **113 `last_result`**.
- **114 `fault`**: bit0 `agent_lost`, bit1 `wake_failed`, bit2 `hid_not_armed`, bit3 `cloud_lost` (sin `vbus_low`). IDs seguidos: la plataforma Tuya los asigna en secuencia.
- **Política de reporte**: asíncrono, desde la tarea de aplicación y solo con MQTT conectado.
  - Telemetría: si cambia ≥ 20 décimas (con al menos 5 s entre reportes) o cualquier cambio cada 30 s.
  - Uptime: como mucho uno por minuto.
  - Resto: al cambiar.
  - Cada DP tiene un intervalo mínimo ≥ 300 ms (≤ 200 reportes/DP/min).
  - En cada conexión MQTT se reportan todos con `DP_REPT_NO_FILTER_FLAG`.
  - Si un reporte falla, se reintenta a los 5 s.

## Estado del PC (US-0015)

Se fusionan cuatro señales: el estado del bus USB (montado/suspendido), si el agente está en línea (heartbeat con timeout de 15 s, o puerto cerrado), si hay un encendido en curso, y si el agente ha confirmado un apagado (`ack`). Se aplica la primera regla que se cumple, con histéresis:

| Condición | Estado | Retención |
|---|---|---|
| agente en línea | `on` | 0 s |
| encendido en curso y bus no activo | `booting` | 0 s |
| montado + suspendido + apagado confirmado | `off` (S5 con "Always On USB" solo se ve como suspensión) | 3 s |
| montado + suspendido | `sleep` | 3 s |
| no montado | `off` | 3 s |
| bus activo y el agente estuvo en línea desde el último flanco de subida | `on_no_agent` (además fallo `agent_lost`) | 2 s |
| bus activo y < 90 s desde el flanco de subida (montaje, reanudación o encendido) | `booting` | 0 s |
| bus activo | `on_no_agent` | 2 s |

Arranca en `unknown` y fija el primer estado a los 3 s.

`shutdown_expected` (apagado confirmado con `ack`) se borra al montar/reanudar el bus y también cuando un agente vuelve a autenticarse: si el agente vuelve, el apagado no llegó a ocurrir y una suspensión posterior es `sleep`, no `off`.

`hid_not_armed` se evalúa en cada suspensión: se activa si el host suspende el bus sin armar el remote wakeup. Es el último valor conocido.

## Encendido (US-0014)

Si el PC ya está encendido (bus montado y no suspendido), la orden se ignora.

- **HID**:
  - Bus suspendido y wakeup armado: `tud_remote_wakeup()`.
  - No montado o no armado (S4/S5): señalización de resume forzada (estado K mediante `dcd_remote_wakeup` del DWC2), 3 intentos separados 2 s. Solo funciona si la BIOS vigila el puerto en S4/S5 ("Wake on USB" / "Always On USB"); falta validarlo en el Lenovo (MESP-US-0002).
- **WOL**: paquete mágico por broadcast UDP a los puertos 9 y 7, a `255.255.255.255` y a la dirección de broadcast de la subred, para cada MAC recibida en el `hello` (como máximo 4, guardadas en NVS solo tras autenticar la sesión).
- **`hid_then_wol`** (por defecto): HID y, si a los 20 s el bus no se ha montado, WOL.
- **Resultado**: `last_result=wake_sent` al enviar. Si en 120 s el bus no se monta: `wake_failed` y bit `wake_failed`, que se borra con el siguiente encendido correcto.

## Apagado / reinicio (US-0016)

1. DP 103/104 a `true`: si el agente no está en línea, `last_result=agent_offline`.
2. Si está en línea, empieza la cuenta atrás del DP 112 y se envía `notice{action,in}`. Durante la cuenta atrás se puede cancelar con la pulsación corta, con el DP a `false` o con `!cancel`; el resultado es `cancelled` y se envía `notice{cancel}`.
3. Al terminar la cuenta atrás se envía el `cmd` firmado (HMAC con los nonces de la sesión e id creciente por sesión).
4. Con `ack ok`, `last_result=ok`. Con `ack` negativo o sin respuesta en 10 s, `cmd_rejected`.
5. Los DP 103/104 vuelven a `false`.

Si la sesión del agente se cae con un `cmd` pendiente (puerto cerrado, agente reiniciado, nuevo `hello` o re-emparejado), el `ack` ya no puede llegar. Se resuelve en ese momento como `cmd_rejected`. Antes el flujo se quedaba esperando para siempre y rechazaba como "ocupado" cualquier orden posterior.

## OTA y rollback (US-0011)

TuyaOpen gestiona `TUYA_EVENT_UPGRADE_NOTIFY`: descarga la imagen y la escribe en el slot inactivo (`tal_ota` → `esp_ota`). El firmware muestra "Actualizando..." y avisa si falla.

**Rollback activado**: el sdkconfig de la plataforma lo traía desactivado, y `sdkconfig.microesp` lo activa con `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`; el valor se ha comprobado en el `sdkconfig` generado. Una imagen nueva arranca como `PENDING_VERIFY` y se marca como válida tras el health check: ≥ 30 s en marcha y MQTT conectado si el dispositivo está activado en Tuya. La imagen llegó por la nube, así que tiene que demostrar que puede volver a conectarse y recibir la siguiente OTA; con solo el USB montado no basta. Si no está activado (flasheo por cable o desarrollo), basta con MQTT conectado o USB montado. Si no lo supera en 10 min, el firmware reinicia y el bootloader vuelve a la imagen anterior; un crash antes de marcarla también la revierte.

La OTA real desde la plataforma Tuya está pendiente: necesita PID y credenciales.

## Tests

```bash
./build.sh test        # = make -C test/host
```

66 tests Unity, con ASan y UBSan, que usan Unity, cJSON y mbedTLS del propio checkout de TuyaOpen/IDF:

- **Criptografía y protocolo**: todos los vectores normativos de `protocol/testdata/vectors.json` (clave HKDF, firmas de `pair`/`pair_ok`/`welcome`/`auth`/`cmd`, mensajes válidos e inválidos y la firma de `cmd` incorrecta).
- **Sesión**: handshake, `unauth`, `not_paired`, timeouts de agente en línea y de `ack`, `cmd` pendiente resuelto al caer la sesión, emparejado con 3 fallos y con la ventana de 120 s, re-emparejado y cierre de puerto.
- **Seguridad**: política de la CLI release/dev (qué comandos acepta cada variante, aprovisionamiento por dato y ventana física de 120 s, también al dar la vuelta el contador de ms) y censura de logs (secretos registrados y palabras sensibles).
- **Robustez (fuzz)**: todas las truncaciones de cada mensaje válido, líneas gigantes y de exactamente 511/512 bytes, anidamiento profundo, 37 casos malformados y 20 000 líneas aleatorias.
- **Resto del núcleo**: tabla de transiciones del estado del PC, flujo de apagado/reinicio, secuencia de encendido y WOL, gestos del botón (incluido el de 5 s), y modelo de DPs (incluido que coincide con `schema/dp.json` y que un valor que cambia con el reporte en vuelo sigue pendiente).

### Prueba de extremo a extremo con el agente Go (siempre `dry_run`)

El código de emparejado ya no sale por el CDC: hay que leerlo en la pantalla del dongle (o reutilizar una clave ya emparejada que siga en NVS; flashear con `flash.sh` no borra la NVS). `!cmd` solo existe en la compilación de desarrollo.

```bash
go build -o $S/mea ./agent/cmd/microesp-agent
# agent.toml: device=/dev/serial/by-id/usb-MicroESP_*-if01, key_file=$S/agent.key, dry_run=true
./build.sh dev && sg dialout -c "tools/flash.sh --app"
tools/mesp_cdc.py '!pair'                                                # o botón 3-5 s; el código, en pantalla
mea pair --config agent.toml --code NNNNNN
tools/mesp_cdc.py '!cmd reboot 3 25'                                     # se dispara 25 s después
timeout 60 mea run --config agent.toml --dry-run --log-level debug       # notice → cmd → ack → "DRY-RUN"
tools/mesp_cdc.py '!status' '!dp'                                        # last_result=ok, 114=0, 104=false
```

Resultado del 2026-10-03:
- Emparejado al primer intento.
- `session ready` con fw 0.1.0, host `lenovop3` y 3 MACs.
- Telemetría recibida (DPs 106, 107, 108, 111 y 112) y `agent_online=true`.
- Al disparar `notice reboot in 3` llegó el `cmd id 1` firmado, el agente lo verificó y respondió `ack ok`, y registró `DRY-RUN: power action not executed action=reboot`. En el dongle quedó `last_result=ok`.
- Con el agente parado, `!cmd shutdown 0 0` dio `last_result=agent_offline`.

Repetido tras el endurecimiento (2026-10-03, misma clave del agente guardada en NVS):
- **Dev**: `!cmd reboot 3 25` → `notice reboot in 3` → `cmd id 1` → `ack ok` → `DRY-RUN: power action not executed action=reboot`; `last_result=ok`. Con `!pair` el log solo dice "code on the display"; varias líneas DEBUG de TuyaOpen (psk, regist_key...) salen como `[redacted: sensitive log line]`.
- **Release** (lo que queda en la placa): enumera como `303a:4002`. `!status` muestra `cli: build=release` y no incluye secretos. `!pair`, `!cmd`, `!wake`, `!key`, `!unpair` y `!reset-tuya` se rechazan. `!auth` se acepta sin aprovisionar y después queda bloqueado; `!pid` igual (los datos de prueba se borraron con `!auth clear` en dev). El log no tiene líneas DEBUG/INFO. Con el agente: `session ready` (host `lenovop3`, 3 MACs) y 4 mensajes de telemetría recibidos (`tele: count=4`).

## Pendiente / limitaciones conocidas

- **Confirmación visual del usuario** (`hw/pinout.md`): pantalla (cableado, offsets, colores), retroiluminación y tipo/pin del LED. Todo se cambia en `include/mesp_board.h`.
- **Credenciales y PID de Tuya**: con valores de relleno el emparejado BLE, la nube, los reportes de DPs y la OTA no se pueden probar.
- **Encendido desde S3/S4/S5 sin validar** en el Lenovo (MESP-US-0002; en este entorno no se puede suspender el PC). El remote wakeup real y el resume forzado no se han probado. El WOL tampoco se ha probado con el PC apagado.
- **Seguridad de la CLI**: resuelto con las variantes release/dev. Sigue abierto:
  - `!dfu` y el *1200-baud touch* permiten reflashear desde el PC (decisión consciente: recuperación sin botón).
  - La UART0 física (si estuviera accesible) tiene la CLI de TuyaOpen (`auth`).
  - La NVS no está cifrada (`CONFIG_NVS_ENCRYPTION`) ni hay *secure boot* / *flash encryption*: con acceso físico se puede leer la clave del agente y la AuthKey.
- **Latencia de los DPs**: hasta ~2 s, por el bloqueo de `tuya_iot_yield` (2 s en MQTT). Sin probar contra la nube real (no hay credenciales).
- **`mhal_cdc_write`** puede bloquear la tarea de app hasta ~1,5 s por línea si un programa abre el puerto y no lee. El watchdog software (60 s) cubre un bloqueo total.
- **Heap interno** ~64,5 KB libres con LVGL (antes ~115 KB). Vigilar si se añaden funciones.
- **docs/ui.md**, tipografías e iconos definitivos, atenuación nocturna y 30 fps: fase B (MESP-US-0017/0018/0020).
