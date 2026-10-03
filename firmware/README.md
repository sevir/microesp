# Firmware MicroESP (fase A)

Firmware de producción del dongle **MicroESP** (Pocket-Dongle-S3, clon de LilyGO T-Dongle-S3) sobre **TuyaOpen v1.9.0 / ESP-IDF v5.4**, board `POCKET_DONGLE_S3`. Reutiliza lo validado en el spike `hw/spikes/tuya-usb/` (TinyUSB dentro de TuyaOpen, board de 16 MB, reflasheo sin BOOT).

**Nube: TuyaLink** (desde 0.2.0, ADR-5 de `docs/analisis/00-analisis-arquitectura.md`). Las licencias TuyaOS (UUID/AuthKey) no se pueden conseguir, así que el firmware ya no usa el cliente `tuya_iot` de TuyaOpen: habla el protocolo abierto TuyaLink (MQTT sobre TLS) con un cliente propio sobre `esp-mqtt`. TuyaOpen se mantiene solo como marco (RTOS/`tal_*`, LVGL, compilación, tabla de particiones).

Estado: fase A completa (todo salvo el pulido de la interfaz). Conectado a la nube Tuya EU con TuyaLink y verificado en placa el 2026-10-04. Cableado, offsets y orientación del LCD verificados leyendo el firmware de fábrica por USB-JTAG (`hw/pinout.md`). Pendiente: tipo y pin del LED.

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
│   └── mesp_hal.h         # API entre la app TuyaOpen y el componente ESP-IDF
├── src/                   # app TuyaOpen (no ve cabeceras ESP-IDF)
│   ├── app_main.c         # arranque, bus de eventos y tarea de aplicación
│   ├── cloud.c            # cliente TuyaLink: aprovisionamiento, reportes y órdenes
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
│       ├── tylink.c       # TuyaLink: firma de credenciales, topics, JSON de reporte/órdenes
│       ├── cli_policy.c   # qué comandos CLI acepta cada compilación y ventana de aprovisionamiento
│       └── log_redact.c   # censura de líneas de log con secretos
├── esp_components/mesp_hal/  # componente ESP-IDF: TinyUSB, NVS, OTA, LCD, LED, botón, WOL,
│                             #   Wi-Fi + SNTP + MQTT/TLS (hal_cloud.c)
├── schema/dp.json         # descripción de los DPs (sincronizada con dp_model.c, lo comprueba un test)
├── test/host/             # tests Unity en el PC (gcc + Makefile, ASan/UBSan)
└── tools/                 # flash.sh, touch1200.py, mesp_cdc.py, set_build_mode.py
```

## Arquitectura

```
 Tuya cloud ⇄ TLS ⇄ [HAL: mesp_cloud (Wi-Fi, SNTP, ciclo MQTT) + esp-mqtt] ─┐ eventos
 PC (agente) ⇄ CDC ⇄ [HAL: TinyUSB, despachador de líneas, supervisor] ─┤ (cola)
 Botón GPIO0 ─────────────────────────── sondeo cada 20 ms ─┐        │
                                                            ▼        ▼
                         [tarea mesp_app]: dueña de TODO el estado de la aplicación
                          link · power · wake · state · DPs · LED · CLI · OTA
                                         │ instantánea (mutex)
                                         ▼
                         [hilo mesp_ui]: LVGL v9 → ST7735 (SPI + DMA)
```

- **Bus de eventos**: cola TuyaOpen (`tal_queue`) de `app_ev_t` (`app_post()`, nunca bloquea). Los callbacks de TinyUSB, del cliente MQTT, de Wi-Fi y del despachador CDC solo publican eventos. La tarea `mesp_app` los consume y ejecuta un tick de 20 ms (botón) y otro de 100 ms (timeouts, estado, reporte de DPs, LED y pantalla). Así toda la lógica corre en un solo hilo y no necesita locks.
- **Nube (TuyaLink)**: la tarea HAL `mesp_cloud` es la dueña de la conexión: reintenta la Wi-Fi, espera a la hora de SNTP, pide a la app el usuario/contraseña de cada intento (la firma lleva la hora), crea el cliente `esp-mqtt` (TLS con el bundle de CAs de ESP-IDF), se suscribe y reconecta con espera 2, 4, 8, 16 y 32 s y después cada 120 s (el contador vuelve a 0 tras una conexión de ≥ 60 s). Toda la lógica (qué reportar, órdenes recibidas, respuestas) corre en la tarea `mesp_app`: `mhal_cloud_publish()` solo deja el mensaje en el *outbox* de `esp-mqtt` bajo un mutex, sin E/S de red en quien llama. Un `property/report` en vuelo como máximo: termina con su PUBACK (`EV_CLOUD`), y falla al desconectar o a los 30 s. Los mensajes recibidos llegan como `EV_CLOUD_RX` (copia del payload) y se procesan en la tarea de app.
- **Watchdog software**: la tarea `mesp_app` avisa al supervisor del HAL en cada vuelta. Si pasa más de 60 s sin hacerlo, el supervisor reinicia el chip.
- **División app/HAL**: el código de `src/` lo compila el CMake de TuyaOpen, que no ve las cabeceras de ESP-IDF. Todo lo que es específico de IDF está en `esp_components/mesp_hal` (componente enlazado con `WHOLE_ARCHIVE`), y la app lo usa a través de `include/mesp_hal.h`, que solo contiene tipos de C estándar.
- **Núcleo testeable**: `src/core/` es C puro (cJSON + mbedTLS) y se prueba en el PC.
- **Versión**: `CONFIG_PROJECT_VERSION` en `app_default.config` → `PROJECT_VERSION` (TuyaOpen) → `MESP_FW_VERSION` (`src/mesp_version.h`). Se usa en el `welcome.fw`, en la pantalla, en `!version` y en el log de arranque.
- **Particiones**: `partitions_16M.csv` de TuyaOpen con OTA dual: nvs, otadata, `ota_0` y `ota_1` de 7,4 MB cada una, `model`, KV `tuya` y `factory_nvs`.
- **Por qué se mantiene TuyaOpen**: es la opción de menor riesgo. Todo el código de la app usa `tal_*` (colas, hilos, log, KV), la pantalla usa su LVGL y la compilación, el board de 16 MB y las redes de seguridad USB están validados sobre él. Quitar `tuya_iot` no cambia nada de eso: el enlazador ya no incluye el cliente Tuya ni el BLE (imagen más pequeña y ~90 KB más de heap interno). Dos efectos laterales resueltos: los *hooks* de mutex de mbedTLS (`MBEDTLS_THREADING_ALT` en el sdkconfig de TuyaOpen) los instalaba `tuya_tls_init()`, ahora los instala `hal_cloud.c`; y la CLI de TuyaOpen en UART0 (`auth`) ya no se inicia.

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

| | v0.1.0 (TuyaOS) | v0.2.0 (TuyaLink) |
|---|---|---|
| Imagen app (release) | 1 594 224 B (21 % de un slot de 7,4 MB) | **1 364 192 B** (18 %); dev: +~450 B |
| Flash `.text` / `.rodata` | 1 147 KB / 304 KB | 920 KB / 323 KB |
| IRAM estática | 16 383 / 16 384 B | 16 383 / 16 384 B (sin cambios; nada nuevo en IRAM) |
| D/IRAM estática | 144 KB usados / 198 KB libres | 120 KB usados / 216 KB libres |
| Heap interno en ejecución | ~64,5 KB libres (mín. 62,6 KB) con Wi-Fi, BLE, TinyUSB y LVGL | **~154,8 KB libres** (mín. ~153,9 KB) con Wi-Fi, TLS/MQTT conectados, TinyUSB y LVGL; PSRAM libre 8,18 MB (mbedTLS usa PSRAM) |

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

## Aprovisionamiento TuyaLink y Wi-Fi

El dispositivo se da de alta en la plataforma Tuya (producto con el modelo de DPs de `schema/dp.json`, conexión TuyaLink), que entrega **productId**, **deviceId** y **deviceSecret**. Se vincula a la cuenta de la app desde la propia plataforma (ya hecho para el dispositivo de desarrollo). No hay emparejado BLE/AP: se ha eliminado.

Todo se guarda en NVS (namespace `microesp`: `tl_region`, `tl_pid`, `tl_did`, `tl_dsec`, `wifi_ssid`, `wifi_pass`) por la CLI del CDC y se aplica con `!reboot`. No hay credenciales en compilación (`tuya_secrets.h` ya no se usa).

```bash
!tylink <eu|us|cn|in> <productId> <deviceId> <deviceSecret>
!wifi <ssid> <contraseña>        # la contraseña es el RESTO de la línea: puede llevar espacios
!reboot
```

- Región → broker: `eu` m1.tuyaeu.com, `us` m1.tuyaus.com, `cn` m1.tuyacn.com, `in` m1.tuyain.com (puerto 8883, TLS con verificación del certificado del servidor contra el bundle de CAs de ESP-IDF; `*.tuyaeu.com` lo firma GoDaddy, raíz "Go Daddy Root Certificate Authority - G2").
- Validación: ids alfanuméricos de 8 a 32 caracteres; secreto de 8 a 64 caracteres imprimibles sin espacios; SSID sin espacios de 1 a 32 bytes; contraseña Wi-Fi obligatoria, de 8 a 64 caracteres (no hay soporte de redes abiertas, para que una contraseña olvidada no guarde una red abierta). Solo Wi-Fi de 2,4 GHz.
- Conexión: `clientId=tuyalink_<deviceId>`, `username=<deviceId>|signMethod=hmacSha256,timestamp=<s>,secureMode=1,accessType=1`, `password=hex(HMAC-SHA256(deviceSecret, "deviceId=<id>,timestamp=<s>,secureMode=1,accessType=1"))`, *keepalive* 60 s. La hora sale de SNTP (`pool.ntp.org`, `time.google.com`); no se intenta conectar hasta sincronizarla en cada arranque. Solo puede haber **una conexión por deviceId**: otro cliente con el mismo id (por ejemplo `hw/spikes/tylink_test.py`) expulsa al dongle.

**Aprovisionamiento en release**: `!tylink` y `!wifi` solo se aceptan si ese dato aún no está en NVS (la primera vez) o durante la **ventana de aprovisionamiento**: 120 s después de mantener el botón **entre 5 y 10 s** y soltarlo (la pantalla muestra "Suelta: aprovisionar" y luego "Aprovisionar 120 s"; `!status` indica `provisioning_window=<s>`). Fuera de la ventana responden `err: ... is locked`. En desarrollo se aceptan siempre, y `!tylink clear` / `!wifi clear` borran esos datos de NVS.

Los secretos nunca se imprimen: `!status` muestra la región, el productId, el deviceId enmascarado (`26e0...0z`), el SSID y el estado, nunca el deviceSecret ni la contraseña. El deviceSecret, la contraseña Wi-Fi y cada contraseña MQTT derivada se registran en el censor de logs (ver "Logs"). La CLI registra solo el nombre del comando.

Los comandos de la época TuyaOS (`!auth`, `!pid`, `!reset-tuya`) responden `err: ... is not used with TuyaLink (use !tylink and !wifi)`.

Sin aprovisionar, el LED parpadea en azul y la pantalla muestra "Nube: !wifi/!tylink".

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
| `!status` | sí | sí | resumen: versión, USB, pc_state, fallos, agente, telemetría, power, wake, emparejado, variante/ventana, TuyaLink (región, productId, deviceId enmascarado, estado MQTT, intentos, último error, reportes y hace cuánto el último, órdenes recibidas), Wi-Fi (SSID, IP, RSSI, hora SNTP), heap. Sin secretos |
| `!version`, `!dp`, `!help` | sí | sí | versión / valores actuales de los DPs (JSON) / ayuda |
| `!log` | sí | sí | buffer de log (HAL), censurado (ver "Logs") |
| `!cancel` | sí | sí | cancela la cuenta atrás o el `!cmd` programado |
| `!reboot`, `!dfu`, `!usj` | sí | sí | reinicio / modo descarga / un arranque sin TinyUSB (HAL) |
| `!tylink <región> <productId> <deviceId> <deviceSecret>`, `!wifi <ssid> <contraseña...>` | sin aprovisionar o en la ventana de 120 s | sí | TuyaLink / Wi-Fi → NVS (se aplican con `!reboot`) |
| `!tylink clear`, `!wifi clear` | no | sí | borran esos datos de NVS |
| `!auth`, `!pid`, `!reset-tuya` | no | no | obsoletos con TuyaLink (error claro) |
| `!pair`, `!unpair` | no | sí | modo emparejado del agente / olvidar la clave |
| `!wake [force]` | no | sí | sin `force` solo muestra el plan (simulación); con `force` ejecuta el encendido. Se ignora si el PC está encendido |
| `!method <hid\|wol\|hid_then_wol>`, `!countdown <0..60>` | no | sí | DP 109 / DP 112 sin pasar por la nube |
| `!cmd <shutdown\|reboot> [cuenta_atrás] [retardo_s]` | no | sí | simula DP 103/104. El retardo permite arrancar el agente antes de que se dispare |
| `!key` | no | sí | pulsa y suelta Shift izquierda (inofensivo; solo con el bus activo) |

En release, un comando de desarrollo responde `err: <cmd> needs a development build` y uno de aprovisionamiento bloqueado `err: <cmd> is locked`. Cada comando se registra en el log solo por su nombre (nunca los argumentos), marcando `(refused)` si se rechaza. `!dfu` y el *1200-baud touch* siguen disponibles en release para poder reflashear sin botón. Esto supone que quien tiene acceso al puerto puede instalar otro firmware; es el mismo límite de confianza que root en el PC.

### Logs

El log de TuyaOpen y de la app sale por UART0 y se copia al buffer que vuelca `!log`; los logs de ESP-IDF (Wi-Fi, `esp-mqtt`, TLS) también. En release el nivel de la app es NOTICE y en desarrollo DEBUG (ESP-IDF queda en INFO). En las dos variantes, `src/core/log_redact.c` sustituye por `[redacted: sensitive log line]` cualquier línea que contenga un secreto registrado (deviceSecret de TuyaLink, contraseña Wi-Fi, contraseña MQTT del intento en curso, código de emparejado) o una palabra sensible (`authkey`, `localkey`, `seckey`, `secret`, `regist_key`, `token`, `passwd`, `password`, `psk`...). El filtro se aplica también a las líneas de ESP-IDF (`mhal_log_set_filter`), en el buffer y en UART0; de esas líneas solo se examinan los primeros 255 bytes. La clave del agente es binaria y nunca se imprime.

Herramienta: `tools/mesp_cdc.py '!status' '!dp'` (con `sg dialout` y el Python del entorno IDF, que trae pyserial).

## Botón (GPIO0)

| Gesto | Acción |
|---|---|
| Pulsación corta | cancela la cuenta atrás de apagado/reinicio; si no hay, cambia de pantalla |
| Doble pulsación (< 400 ms) | enciende el PC (método del DP 109) |
| Mantener 3-5 s y soltar | modo emparejado del agente |
| Mantener 5-10 s y soltar | ventana de aprovisionamiento: `!tylink`/`!wifi` aceptados 120 s en release |
| Mantener 10-20 s y soltar | sin acción desde TuyaLink (no hay vínculo BLE/AP que resetear) |
| Mantener ≥ 20 s | modo descarga ROM (recuperación) |

Antirrebote de 40 ms. Las pulsaciones de 1 a 3 s se ignoran. Ningún gesto se acepta hasta que el botón se ha visto suelto al menos una vez (protege frente a un GPIO0 bloqueado a nivel bajo al arrancar). Mientras se mantiene pulsado, la pantalla indica qué pasará al soltar. Cambio respecto al spike: allí ≥ 2 s activaba el modo descarga.

## LED

Driver seleccionable en `include/mesp_board.h`: `MESP_LED_TYPE` (`WS2812` por RMT o `APA102` por bit-bang), `MESP_LED_PIN` (40 por defecto), `MESP_LED_PIN_CLK` y `MESP_LED_MAX_BRIGHTNESS` (48/255, brillo bajo).

| Prioridad | Situación | Color |
|---|---|---|
| 1 | cuenta atrás de apagado/reinicio | rojo parpadeo rápido (4 Hz) |
| 2 | encendido enviado (esperando al PC) | blanco pulsante |
| 3 | emparejando (agente) o nube sin aprovisionar (`!tylink`/`!wifi`) | azul parpadeo (1 Hz) |
| 4 | error (fallos excepto `hid_not_armed`; incluye `cloud_lost`) | rojo fijo |
| 5 | PC encendido con agente | verde |
| 6 | PC encendido sin agente / arrancando | ámbar (arrancando: parpadeo) |
| 7 | PC apagado / suspendido / desconocido | blanco tenue |

## Pantalla (versión básica)

ST7735 en horizontal (160×80) con LVGL v9 de TuyaOpen en su propio hilo. Pines, offsets (`x=1`, `y=26`), `MADCTL=0x68`, inversión y polaridad de la retroiluminación están en `include/mesp_board.h` y se pueden sobrescribir con `-D`.

Pantallas: **estado** (estado del PC, hostname, iconos Wi-Fi/nube/agente y versión), **telemetría** (barras CPU / MEM / disco libre), **cuenta atrás** (automática, con "Pulsa para cancelar") y **código de emparejado** (automática). La pulsación corta alterna estado y telemetría; con el agente conectado rotan solas cada 5 s. Una línea inferior muestra avisos temporales. El pulido (iconos, tipografías, `docs/ui.md`) queda para la fase B.

## DPs

Tabla completa en `schema/dp.json`. En TuyaLink cada DP es una **propiedad** del modelo de cosa identificada por su **código** (`pc_state`, `power_on`...); los números 101-114 son los `abilityId` de la plataforma y se mantienen como clave interna y en la documentación. Codificación JSON: bool → `true/false`; value → entero (escala 1: décimas de %); **enum → cadena** (`"on"`, `"hid_then_wol"`...; verificado con el modelo que devuelve `model/get_response`); bitmap (`fault`) → entero con la máscara; string → cadena. Topics (`tylink/<deviceId>/thing/...`): publica `property/report`, `property/set_response`, `action/execute_response` y `model/get` (una vez por conexión); se suscribe a `property/set`, `action/execute`, `model/get_response` y `property/report_response`.

- **Órdenes (`property/set`)**: puede traer varias propiedades. Cada una se valida (código conocido, escribible, tipo JSON correcto, rango); las válidas se aplican en orden y las demás se rechazan una a una. Respuesta `property/set_response` con el mismo `msgId` y `code` 0 si todas eran válidas o 1 si alguna se rechazó. Un mensaje sin `msgId` (1..32 caracteres) o sin objeto `data` se descarta sin respuesta. `action/execute` responde siempre `code` 1 (el modelo no tiene acciones).

Resumen:

- **101 `pc_state`**: estado del PC. **102 `power_on`**: pulsador; vuelve a `false`. **103/104 `power_off`/`reboot`**: `true` lanza la cuenta atrás y `false` durante la cuenta atrás la cancela; vuelven a `false` al terminar.
- **105/106/107**: CPU, memoria y disco libre en décimas de %, de 0 a 1000. **108 `agent_online`**. **109 `wake_method`**: guardado en NVS. **110 `pc_uptime`**. **111 `pc_hostname`**. **112 `cmd_countdown`**: de 0 a 60, 10 por defecto, guardado en NVS. **113 `last_result`**.
- **114 `fault`**: bit0 `agent_lost`, bit1 `wake_failed`, bit2 `hid_not_armed`, bit3 `cloud_lost` (sin `vbus_low`). IDs seguidos: la plataforma Tuya los asigna en secuencia.
- **Política de reporte**: asíncrono, desde la tarea de aplicación y solo con MQTT conectado.
  - Telemetría: si cambia ≥ 20 décimas (con al menos 5 s entre reportes) o cualquier cambio cada 30 s.
  - Uptime: como mucho uno por minuto.
  - Resto: al cambiar.
  - Cada DP tiene un intervalo mínimo ≥ 300 ms (≤ 200 reportes/DP/min).
  - En cada conexión MQTT se reportan todos.
  - Un reporte se da por bueno con su PUBACK (QoS 1). Si falla (desconexión, sin PUBACK en 30 s o sin conexión al encolarlo), se reintenta a los 5 s.

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

**OTA por la nube: no disponible desde 0.2.0.** La gestionaba el cliente `tuya_iot` de TuyaOpen (`TUYA_EVENT_UPGRADE_NOTIFY`); los topics OTA de TuyaLink aún no están implementados. Las actualizaciones se hacen por USB (`tools/flash.sh`). Se conservan la tabla OTA dual y el rollback:

**Rollback activado**: el sdkconfig de la plataforma lo traía desactivado, y `sdkconfig.microesp` lo activa con `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`; el valor se ha comprobado en el `sdkconfig` generado. Una imagen nueva arranca como `PENDING_VERIFY` y se marca como válida tras el health check: ≥ 30 s en marcha y MQTT conectado si la nube está aprovisionada (una imagen llegada por la nube tiene que demostrar que puede volver a conectarse). Sin aprovisionar, basta con MQTT conectado o USB montado. Si no lo supera en 10 min, el firmware reinicia y el bootloader vuelve a la imagen anterior; un crash antes de marcarla también la revierte.

La OTA por TuyaLink queda pendiente.

## Tests

```bash
./build.sh test        # = make -C test/host
```

80 tests Unity, con ASan y UBSan, que usan Unity, cJSON y mbedTLS del propio checkout de TuyaOpen/IDF:

- **Criptografía y protocolo**: todos los vectores normativos de `protocol/testdata/vectors.json` (clave HKDF, firmas de `pair`/`pair_ok`/`welcome`/`auth`/`cmd`, mensajes válidos e inválidos y la firma de `cmd` incorrecta).
- **Sesión**: handshake, `unauth`, `not_paired`, timeouts de agente en línea y de `ack`, `cmd` pendiente resuelto al caer la sesión, emparejado con 3 fallos y con la ventana de 120 s, re-emparejado y cierre de puerto.
- **TuyaLink** (`test_tylink.c`): contraseña HMAC con vectores calculados en Python, usuario y clientId, hosts por región, validación de ids/secreto, topics y su clasificación, `msgId`; JSON de `property/report` (enums como cadena, bitmap como entero, escapes, todos los DPs caben en el buffer), respuestas y `model/get`; `property/set` con una y varias propiedades, códigos desconocidos, solo lectura, tipos incorrectos, fuera de rango, más propiedades que DPs, sobres malformados y truncados; nombres de enum iguales a los del núcleo y a `schema/dp.json`.
- **Seguridad**: política de la CLI release/dev (qué comandos acepta cada variante, `!tylink`/`!wifi` por dato y ventana física de 120 s, comandos obsoletos, también al dar la vuelta el contador de ms), análisis de `!wifi` (contraseña con espacios = resto de la línea) y censura de logs (secretos registrados, incluidas la contraseña Wi-Fi y la MQTT, y palabras sensibles).
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

### Prueba de extremo a extremo con TuyaLink (2026-10-04)

Dongle aprovisionado con un build de desarrollo (`!tylink` y `!wifi` enviados por un script que lee los secretos sin mostrarlos), región EU:
- Wi-Fi (2,4 GHz, RSSI -58) e IP en ~5 s; SNTP; **MQTT conectado al primer intento** con TLS verificado contra `m1.tuyaeu.com`; suscripciones concedidas; `model/get_response` recibido (1837 B, el modelo de cosa con los 14 códigos); el primer `property/report` (8 DPs) confirmado con PUBACK.
- Agente Go en `dry_run` 80 s: `session ready` (fw 0.2.0, host `lenovop3`, 3 MACs), 9 mensajes de telemetría → 10 `property/report` confirmados (24 DPs reportados en total), 0 fallos, 0 desconexiones.
- `!log` no contiene el deviceSecret, la contraseña Wi-Fi ni el deviceId completo (comprobado por programa).
- Release (lo que queda en la placa): conecta solo tras flashear (NVS conservada); `!tylink`/`!wifi` bloqueados (aprovisionado), `!tylink clear` bloqueado, `!auth`/`!pid`/`!reset-tuya` obsoletos, `!pair` solo en desarrollo; sin líneas DEBUG/INFO de la app.
- Sin probar en placa: una orden real `property/set` desde la app tras el cambio (la decodificación tiene tests; con el spike de Python sí se recibió `{"power_on":true}` desde la app) y la expulsión por un segundo cliente con el mismo deviceId (reconexión con espera).

## Pendiente / limitaciones conocidas

- **Confirmación visual del usuario** (`hw/pinout.md`): pantalla (cableado, offsets, colores), retroiluminación y tipo/pin del LED. Todo se cambia en `include/mesp_board.h`.
- **TuyaLink**: OTA por la nube sin implementar; `property/report_response` no llega en la región EU (tampoco al spike), así que la aceptación de cada valor por la nube no se puede confirmar desde el dispositivo (solo el PUBACK). Verificar en la app que llegan los valores y que `power_on` desde la app enciende.
- **Encendido desde S3/S4/S5 sin validar** en el Lenovo (MESP-US-0002; en este entorno no se puede suspender el PC). El remote wakeup real y el resume forzado no se han probado. El WOL tampoco se ha probado con el PC apagado.
- **Seguridad de la CLI**: resuelto con las variantes release/dev. Sigue abierto:
  - `!dfu` y el *1200-baud touch* permiten reflashear desde el PC (decisión consciente: recuperación sin botón).
  - La NVS no está cifrada (`CONFIG_NVS_ENCRYPTION`) ni hay *secure boot* / *flash encryption*: con acceso físico se puede leer la clave del agente, el deviceSecret y la contraseña Wi-Fi.
- **Latencia de los DPs**: el *outbox* de `esp-mqtt` se vacía en cada vuelta de su tarea (≤ ~1 s); en placa, el PUBACK del primer reporte llega en el mismo segundo de la conexión.
- **`mhal_cdc_write`** puede bloquear la tarea de app hasta ~1,5 s por línea si un programa abre el puerto y no lee. El watchdog software (60 s) cubre un bloqueo total.
- **Heap interno** ~155 KB libres con Wi-Fi, TLS y LVGL (sin BLE).
- **docs/ui.md**, tipografías e iconos definitivos, atenuación nocturna y 30 fps: fase B (MESP-US-0017/0018/0020).
