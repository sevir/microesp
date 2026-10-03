---
id: MESP-US-0010
type: story
title: Conexión cloud, emparejado BLE y capa de DPs
status: in_review
priority: high
parent: MESP-EP-0003
milestone: MESP-M-0002
author: mcp
labels: [firmware, tuya]
estimate: 5
created: 2026-10-03T08:58:23Z
updated: 2026-10-03T11:38:27Z
started: 2026-10-03T10:59:23Z
---

## Description
tuya_iot_init/start/yield con UUID/AuthKey desde NVS; emparejado BLE (NETCFG_TUYA_BLE) y AP como respaldo; módulo tuya_dp con tabla de DPs tipada, reporte asíncrono con umbral de cambio y throttling (≤200 reportes/DP/min), despacho de TUYA_EVENT_DP_RECEIVE_OBJ a handlers.

## Acceptance Criteria
- Emparejado en Smart Life desde cero en <2 min.
- Reconexión automática tras caída Wi-Fi/cloud; DP 115 bit cloud_lost refleja el estado.
- Tests unitarios del codificador/decodificador de DPs (host).
