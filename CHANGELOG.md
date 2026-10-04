# Changelog

Formato basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/). Versionado [SemVer](https://semver.org/lang/es/). Firmware y agente comparten el número de versión (etiqueta `vX.Y.Z`, que debe coincidir con `CONFIG_PROJECT_VERSION` de `firmware/app_default.config`).

## [Sin publicar] - 0.2.0

### Cambiado

- **Encendido siempre enviado y Alt+P (Lenovo Smart Power On).** Con *Smart Power On* la BIOS mantiene enumerado el teclado en S5 y el dongle creía que el PC estaba encendido, así que ignoraba la orden. Ahora la orden de encendido (app, botón, `!wake force`) se envía siempre: con el bus activo pulsa Alt+P; suspendido, *remote wakeup* y Alt+P al reanudarse; sin montar, *resume* forzado; con 2 reintentos y el WOL según `wake_method`. Éxito = agente en línea o nueva enumeración del dongle tras el Alt+P. Una orden durante un encendido en curso reenvía el paso HID. `mhal_hid_tap` espera a que el host lea el informe antes de soltar la tecla (evita teclas pegadas con hosts lentos). `!status` muestra `hid_proto`.
- **Nube: TuyaLink en lugar de TuyaOS** (ADR-5). Las licencias TuyaOS (UUID/AuthKey) no se pueden conseguir. El firmware deja de usar el cliente `tuya_iot` de TuyaOpen y habla TuyaLink (MQTT 3.1.1 sobre TLS, puerto 8883, verificación del servidor con el bundle de CAs de ESP-IDF) con un cliente propio sobre `esp-mqtt`:
  - firma HMAC-SHA256 del usuario/contraseña con la hora de SNTP en cada intento; reconexión con espera 2, 4, 8, 16 y 32 s y luego cada 120 s;
  - `property/report` con los códigos del modelo de cosa (enums como cadena, `fault` como entero), un reporte en vuelo confirmado por PUBACK, mismos umbrales y throttling, todo reportado en cada conexión;
  - `property/set` con varias propiedades, validación una a una y respuesta `property/set_response`; `action/execute` responde error (sin acciones); `model/get` en cada conexión.
  - TuyaOpen se mantiene como marco (RTOS, LVGL, compilación). Imagen de 1,59 a 1,36 MB y ~90 KB más de heap interno (sin BLE ni `tuya_iot`).
- **Aprovisionamiento** por la CLI del CDC: `!tylink <región> <productId> <deviceId> <deviceSecret>` y `!wifi <ssid> <contraseña...>` (la contraseña es el resto de la línea), en NVS, con la política release de antes (solo sin aprovisionar o en la ventana de 120 s del botón de 5 s; en desarrollo siempre, y `!tylink clear` / `!wifi clear`).
- `!status` muestra región, productId, deviceId enmascarado, estado de Wi-Fi/MQTT/SNTP, último error, reportes y órdenes recibidas, sin secretos.
- El fallo `cloud_lost` también se activa si la nube nunca llega a conectar (60 s desde el arranque).
- El censor de logs cubre ahora las líneas de ESP-IDF (Wi-Fi, MQTT, TLS) y registra el deviceSecret, la contraseña Wi-Fi y la contraseña MQTT de cada intento.

### Eliminado

- Emparejado BLE/AP con Smart Life, `!auth`, `!pid` y `!reset-tuya` (responden "not used with TuyaLink"), `include/tuya_secrets.h(.example)`, el reset de Tuya con el botón (10-20 s ya no hace nada) y la CLI de TuyaOpen en UART0.
- OTA por la nube (la hacía `tuya_iot`): pendiente con los topics OTA de TuyaLink; actualización por USB.

### Añadido

- `src/core/tylink.c` (TuyaLink en C puro) y 14 tests nuevos en el host (80 en total): vectores HMAC calculados con Python, JSON de reporte y respuestas, `property/set` con varias propiedades, códigos desconocidos, tipos incorrectos, fuera de rango, mensajes malformados y truncados; análisis de `!wifi` y comandos obsoletos.

### Corregido

- `schema/dp.json`: los ids de la política de reporte seguían la numeración antigua (106-108/111); ahora 105-107/110.

## 0.1.0

### Añadido

- **Firmware** (TuyaOpen v1.9.0 / ESP-IDF v5.4, board `POCKET_DONGLE_S3` de 16 MB):
  - dispositivo USB compuesto HID teclado + CDC ACM con *remote wakeup*;
  - encendido por HID con Wake-on-LAN de respaldo (`hid_then_wol`);
  - apagado y reinicio con cuenta atrás cancelable;
  - máquina de estados del PC (DP 101) y DPs 101–115 (salvo el 105);
  - emparejado BLE/AP con Smart Life;
  - OTA con rollback;
  - botón (gestos), LED de estado y pantalla ST7735 con LVGL (versión básica);
  - CLI por el CDC y redes de seguridad para reflashear sin BOOT.
- **Protocolo cdc-v1** (`docs/protocol/cdc-v1.md`): sesión autenticada con HMAC-SHA256, emparejado con código de 6 dígitos (HKDF) y vectores normativos en `protocol/testdata/vectors.json`.
- **Agente** `microesp-agent` (Go):
  - telemetría (CPU, memoria, disco libre, uptime, hostname, MACs) y latido;
  - ejecución de las órdenes firmadas mediante systemd/logind con polkit mínimo;
  - instalador idempotente con udev, polkit y unidad systemd con sandbox;
  - servicio de Windows opcional.
- **Tests**: 59 tests Unity del núcleo del firmware en el host (ASan/UBSan) y tests del agente con `-race`.
- **CI** (GitHub Actions): vet/staticcheck/gofmt/tests del agente, tests del firmware en el host, compilación del firmware, gitleaks y shellcheck. La release por etiqueta usa goreleaser y publica un `SHA256SUMS` común.
- **Documentación**: README raíz, guía de instalación, guía de BIOS/SO para el Lenovo ThinkStation P3 Ultra SFF G2 y plan de pruebas E2E.

### Pendiente

- Validar el encendido desde S4/S5 en el Lenovo (MESP-US-0002) y ejecutar el plan E2E (`docs/qa/e2e-v1.md`).
- PID y credenciales reales de Tuya; OTA real desde la plataforma Tuya.
- Pulido de la interfaz (fase B) y medida de VBUS (DP 105).
