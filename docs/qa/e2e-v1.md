# Plan de pruebas E2E v1 (Lenovo ThinkStation P3 Ultra SFF G2)

Historia: MESP-US-0033. Criterio de aceptación: todos los casos ejecutados, con resultado y evidencia, y **0 fallos bloqueantes**.

## Datos de la ejecución

| Campo | Valor |
|---|---|
| Fecha(s) | 2026-10-04 (parcial) |
| Ejecutado por | |
| Versión del firmware (`!version`) | 0.2.0 |
| Versión del agente (`microesp-agent version`) | f53ecc1 |
| Commit del repositorio | e9cc1c8 |
| PC / versión de la BIOS (`sudo dmidecode -s bios-version`) | ThinkStation P3 Ultra SFF G2 / |
| SO / kernel (`uname -r`) | |
| Puerto USB del dongle | |
| Ajustes de BIOS aplicados ([`bios-lenovo.md`](../usuario/bios-lenovo.md) §2) | |
| NIC / WOL | `enp128s31f6` `fc:9d:05:18:ee:32`, `Wake-on:` |

**Leyenda de resultados**: ✅ OK · ❌ Falla (indica si es **bloqueante**) · ⚠️ OK con observaciones · N/A no aplica.
**Evidencias**: guárdalas en `docs/qa/evidencias/e2e-v1/` con el id del caso (por ejemplo `E2E-05-journal.txt`, `E2E-05-app.png`). Pueden ser capturas de la app, salidas de `!status`/`!dp`/`!log`, `journalctl -u microesp-agent` o fotos de la pantalla.

**Seguridad**: haz todas las pruebas de apagado y reinicio primero con `dry_run = true` en el agente, y cierra el trabajo abierto en el PC antes de las pruebas reales. El botón de encendido físico es siempre la vía de recuperación.

## 1. Instalación y emparejado

| Caso | Pasos | Esperado | Resultado | Evidencia |
|---|---|---|---|---|
| E2E-01 Flasheo inicial | Flashear la imagen fusionada ([instalación §1](../usuario/instalacion.md#1-flashear-el-firmware)); desenchufar y enchufar | Enumera como `303a:4002` «MicroESP»; la pantalla muestra el estado; el LED parpadea en azul | OK | Flasheado 0.2.0 por USB (tools/flash.sh); enumera 303a:4002 `MESP-907069f662dc` |
| E2E-02 Credenciales TuyaLink y Wi-Fi | `!tylink <región> <productId> <deviceId> <deviceSecret>`, `!wifi <ssid> <contraseña>`, `!reboot`; `!status` | `tylink: ... provisioned=1 mqtt=connected`, `wifi: ... up=1 ... time_synced=1`; sin secretos en `!status` ni en `!log` | OK | `!status`: `wifi up=1`, `mqtt=connected` (eu), `time_synced=1`; secretos no aparecen en `!log` |
| E2E-03 Dispositivo en Smart Life | Abrir la app (vinculado desde la plataforma; sin BLE) | El dispositivo aparece en línea; propiedades `pc_state` … `fault` visibles | OK | Vinculado por QR (+ → Escanear) con producto TuyaLink Central EU; valores visibles tras cambiar el panel |
| E2E-04 Instalación del agente | `sudo agent/deploy/install.sh` | Servicio activo; `/dev/microesp` existe; `power/wakeup=enabled` en el dongle | OK | `install.sh --binary ./microesp-agent --no-start`; servicio activo; arranca solo al reenumerar el dongle |
| E2E-05 Emparejado agente ↔ dongle | Botón 3 s → código; `sudo microesp-agent pair` | `pair_ok`; existe `/etc/microesp/agent.key` (0600, `microesp`); tras arrancar el servicio, `agent_online=true`, `pc_state=on` | OK | Código en pantalla (botón 3-5 s); `sudo microesp-agent pair`; journal: `session ready` 02:06:32 |
| E2E-06 Emparejado con código erróneo | 3 códigos incorrectos | El dongle sale del modo de emparejado; la clave anterior no cambia | | |
| E2E-07 Re-emparejado | Emparejar de nuevo | La clave nueva funciona en los dos lados; la anterior queda invalidada | | |

## 2. Encendido

Antes de cada caso: el PC en el estado indicado y el dongle conectado a la nube. Anota el tiempo desde que se pulsa en la app hasta `pc_state=on`.

| Caso | Pasos | Esperado | Resultado | Evidencia |
|---|---|---|---|---|
| E2E-10 Encendido desde S3 (HID) | `sudo rtcwake -m mem -s 180`; `pc_state=sleep`; app → Encender (`wake_method=hid`) | Despierta antes de 180 s; `last_result=wake_sent` → `pc_state=on`; sin `hid_not_armed` | | |
| E2E-11 Encendido desde S3 (botón) | Suspender; doble pulsación en el botón del dongle | Despierta | | |
| E2E-12 Encendido desde S4 (HID) | `systemctl hibernate` (si está configurado); app → Encender | Despierta, o N/A si no hay hibernación | | |
| E2E-13 Encendido desde S5 (HID) | BIOS *Smart Power On* activado y dongle en el conector *smart power on*; `systemctl poweroff`; `wake_method=hid`; app → Encender | El dongle envía Alt+P y el PC arranca. Si no: `wake_failed` a los 120 s; anota si el puerto da 5 V en S5 y `hid_proto` en `!status` | | |
| E2E-14 Encendido desde S5 (WOL) | `wake_method=wol`; apagar; app → Encender | Arranca por WOL | | |
| E2E-15 Encendido desde S5 (`hid_then_wol`) | Método por defecto; apagar; app → Encender | Arranca (por HID o por WOL a los 20 s) | | |
| E2E-16 Encender con el PC ya encendido | PC `on`; app → Encender | La orden se envía igualmente (Alt+P llega a la aplicación en primer plano); con el agente conectado se da por buena enseguida; el PC no se ve afectado | | |
| E2E-17 Fallo de encendido | Desactivar el WOL en la NIC y apagar con el puerto sin alimentación; app → Encender | `wake_failed` y bit 1 de `fault` a los 120 s; se borra en el siguiente encendido correcto | | |

## 3. Apagado y reinicio

| Caso | Pasos | Esperado | Resultado | Evidencia |
|---|---|---|---|---|
| E2E-20 Apagado (dry-run) | `dry_run = true`; app → Apagar | Cuenta atrás en la pantalla (DP 112) y LED rojo; `cmd` firmado → `ack ok`; log `dry-run`; `last_result=ok`; DP 103 vuelve a `false` | | |
| E2E-21 Apagado real | `dry_run = false`; app → Apagar | El PC se apaga tras la cuenta atrás; `pc_state` → `off` | | |
| E2E-22 Reinicio real | app → Reiniciar | El PC reinicia; el agente reconecta (`on`) | | |
| E2E-23 Cancelar con el botón | app → Apagar; pulsación corta durante la cuenta atrás | `last_result=cancelled`; `notice{cancel}`; el PC sigue encendido | OK (con Reiniciar) | journal 2026-10-04: `notice action=reboot in=10` 02:12:32 → `notice action=cancel` 02:12:35; PC sigue encendido |
| E2E-24 Cancelar desde la app | app → Apagar; DP 103 a `false` durante la cuenta atrás | `cancelled` | | |
| E2E-25 Apagado sin agente | `systemctl stop microesp-agent`; app → Apagar | `last_result=agent_offline`; no hay cuenta atrás | OK | Servicio parado: app → Reiniciar → `last_result=agent_offline`, `rx_cmds=2` |
| E2E-26 Inhibidor activo | `systemd-inhibit --what=shutdown sleep 600 &`; app → Apagar | `ack` y luego `exec_failed` en el log, o `cmd_rejected`; el PC **no** se apaga | | |
| E2E-27 Seguridad de las órdenes | Revisar los tests (`go test ./internal/link`, `firmware/build.sh test`): firma incorrecta y repetición | `bad_sig` / `replay` rechazados (cubierto por tests) | | |

## 4. Estado y robustez

| Caso | Pasos | Esperado | Resultado | Evidencia |
|---|---|---|---|---|
| E2E-30 Agente caído | `sudo systemctl stop microesp-agent` (o `kill -9`) | En ≤ 15 s: `agent_online=false`, `pc_state=on_no_agent`, `fault` bit 0 `agent_lost`; LED ámbar | | |
| E2E-31 Agente recuperado | `sudo systemctl start microesp-agent` | `on`, `agent_online=true`, se borra `agent_lost` | | |
| E2E-32 Dongle desenchufado/reenchufado | Desenchufar 10 s y volver a enchufar con el PC encendido | El agente reintenta con backoff y reconecta solo; `pc_state=on` en < 30 s | | |
| E2E-33 Pérdida de Wi-Fi | Apagar el AP 2 min y volver a encenderlo | `cloud_lost` (bit 3) mientras dura; reconecta solo; reporte completo de DPs al reconectar | | |
| E2E-34 Reinicio del PC | Reiniciar el PC desde el SO | Secuencia `off`/`booting` → `on`; el agente arranca con el sistema | | |
| E2E-35 Suspensión vista por el dongle | `systemctl suspend` | `pc_state=sleep` (≤ 3 s de histéresis) | | |
| E2E-36 Corte de luz | Quitar la alimentación del PC y volver a ponerla | Comportamiento según «After Power Loss»; el dongle no se resetea a Tuya | | |

## 5. Telemetría

| Caso | Pasos | Esperado | Resultado | Evidencia |
|---|---|---|---|---|
| E2E-40 CPU | Carga con `stress-ng --cpu 0 -t 60s`; comparar con `top`/`mpstat 5` | DP 105 dentro de ±5 puntos | | |
| E2E-41 Memoria | Comparar con `free -m` (usada sin caché ni buffers) | DP 106 dentro de ±2 puntos | | |
| E2E-42 Disco libre | Comparar con `df -h /` (y los discos configurados) | DP 107 = menor % libre, ±1 punto | | |
| E2E-43 Uptime y hostname | Comparar con `uptime -p` y `hostname` | DP 110 (≤ 1 reporte/min) y DP 111 correctos | | |
| E2E-44 MACs para WOL | `!status` (con el agente parado) | `macs≥1`; incluye `fc:9d:05:18:ee:32` | | |

## 6. OTA

| Caso | Pasos | Esperado | Resultado | Evidencia |
|---|---|---|---|---|
| E2E-50 OTA correcta | Subir la versión N+1 a la plataforma Tuya y lanzar la actualización desde la app | «Actualizando...»; arranca N+1; `image=valid` tras el health check (≥ 30 s y MQTT/USB) | | |
| E2E-51 Rollback | Instalar por OTA una imagen que no supera el health check (de prueba) | El bootloader vuelve a la versión N en ≤ 10 min | | |
| E2E-52 Configuración tras la OTA | Tras E2E-50 | Credenciales, clave del agente, `wake_method` y `cmd_countdown` se conservan | | |

## 7. Estabilidad (24 h)

| Caso | Pasos | Esperado | Resultado | Evidencia |
|---|---|---|---|---|
| E2E-60 24 h encendido | PC encendido con el agente durante 24 h; registrar `!status` (línea `heap`) al principio, cada ~6 h y al final | Sin reinicios del dongle (`uptime` continuo, `reset=` sin crash); heap libre estable; **heap mínimo registrado:** ____ | | |
| E2E-61 24 h con ciclos | Durante las 24 h: ≥ 5 ciclos de suspensión/encendido y ≥ 2 apagado/encendido | Todos correctos; sin fugas (heap mínimo sin tendencia a la baja) | | |
| E2E-62 Agente 24 h | `systemctl status microesp-agent`, `journalctl` y RSS (`ps -o rss`) | Sin reinicios del servicio ni errores repetidos; RSS estable | | |

## Resumen

| Total | ✅ | ⚠️ | ❌ (bloqueantes) | N/A |
|---|---|---|---|---|
| | | | | |

Incidencias abiertas (id, caso y descripción):

-
