# Changelog

Formato basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/). Versionado [SemVer](https://semver.org/lang/es/). Firmware y agente comparten el número de versión (etiqueta `vX.Y.Z`, que debe coincidir con `CONFIG_PROJECT_VERSION` de `firmware/app_default.config`).

## [Sin publicar] - 0.1.0

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
