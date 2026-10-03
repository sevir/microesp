# Protocolo MicroESP CDC v1 (agente ↔ dongle)

Contrato entre `microesp-agent` (Go, en el PC) y el firmware del dongle, sobre el puerto CDC ACM del dispositivo USB compuesto. Vectores de prueba normativos: `protocol/testdata/vectors.json`.

## 1. Transporte

- USB CDC ACM. Baudrate irrelevante (se abre a 115200 8N1).
- Identificación USB: VID `0x303A`, PID `0x4002`, product string `MicroESP`, serial `MESP-<mac12hex>` (MAC Wi-Fi en minúsculas sin separadores). Linux: `/dev/serial/by-id/usb-*MicroESP*`, symlink udev opcional `/dev/microesp`.
- Framing: un objeto JSON UTF-8 por línea, terminado en `\n` (se tolera `\r\n`). **Máximo 512 bytes** por línea incluido el terminador. Línea más larga → se descarta hasta el siguiente `\n` y se responde `err{code:"too_long"}`.
- Todo mensaje es un objeto con campo `t` (tipo). Campos desconocidos se ignoran (compatibilidad hacia delante). Tipo desconocido → `err{code:"bad_msg"}`.
- Hex siempre en minúsculas. Nonces: 8 bytes aleatorios = 16 caracteres hex.

## 2. Criptografía

- Clave compartida `K`: 32 bytes, establecida en el emparejado (§4). Dongle: NVS. Agente: `/etc/microesp/agent.key` (hex, modo 0600).
- `sig = hex(HMAC-SHA256(K, mensaje_ascii))`, 64 caracteres. Comparación en tiempo constante.

## 3. Sesión

```
agente                          dongle
  hello{v,host,os,agent_ver,macs,nonce=Na}  →
                                ←  welcome{v,fw,dev,nonce=Nd,sig=HMAC(K,"welcome|Na|Nd")}
  [agente verifica sig; si falla cierra y reintenta con backoff]
  auth{sig=HMAC(K,"auth|Nd|Na")}           →
                                ←  ready            (o err{code:"unauth"})
  tele / hb ...                             →
                                ←  notice / cmd
  ack{id,ok,err?}                           →
```

- `v` debe ser `1`; otro valor → `err{code:"unsupported_version"}`.
- Si el dongle no tiene clave: responde a `hello` con `err{code:"not_paired"}`.
- Antes de `ready`, el dongle ignora `tele`/`hb`/`ack` y responde `err{code:"unauth"}`.
- Un `hello` nuevo reinicia la sesión (el agente se reconectó).
- El dongle guarda las MACs de `hello.macs` (máx. 4) en NVS para Wake-on-LAN y `host` para el DP `pc_hostname`.

### Mensajes agente → dongle

| t | Campos | Notas |
|---|---|---|
| `hello` | `v`, `host` (≤64), `os` (`linux`/`windows`), `agent_ver`, `macs` (array `aa:bb:..`), `nonce` | inicia sesión |
| `auth` | `sig` | |
| `tele` | `seq` (uint32), `cpu`, `mem`, `disk_free` (enteros 0..1000 = décimas de %), `uptime` (s, uint32) | cada 10 s o cambio >20 décimas |
| `hb` | — | cada 5 s si no hubo otro mensaje |
| `ack` | `id`, `ok` (bool), `err` opcional (`bad_sig`, `replay`, `exec_failed`, `unknown_action`) | respuesta a `cmd`, antes de ejecutar |
| `pair`, `pair_confirm` | ver §4 | |

### Mensajes dongle → agente

| t | Campos | Notas |
|---|---|---|
| `welcome` | `v`, `fw`, `dev` (mac12hex), `nonce`, `sig` | |
| `ready` | — | sesión autenticada |
| `notice` | `action` (`shutdown`/`reboot`), `in` (s) | aviso de cuenta atrás; `in:0` con `action:"cancel"` = cancelado |
| `cmd` | `id` (uint32 creciente por sesión, empieza en 1), `action` (`shutdown`/`reboot`), `sig=HMAC(K,"cmd|id|action|Na|Nd")` | |
| `pair_chal`, `pair_ok` | ver §4 | |
| `err` | `code` (`bad_msg`, `too_long`, `unauth`, `not_paired`, `unsupported_version`, `pair_failed`) | |

### Reglas de comandos (agente)

- Verificar `sig` con Na/Nd de la sesión actual; fallo → `ack{ok:false,err:"bad_sig"}`.
- `id` debe ser mayor que el último aceptado en la sesión; si no → `ack{ok:false,err:"replay"}`.
- `action` desconocida → `ack{ok:false,err:"unknown_action"}`.
- Si todo es válido: enviar `ack{ok:true}` **y después** ejecutar.

### Timeouts

- Dongle: sin mensajes del agente durante 15 s → `agent_online=false`.
- Dongle: `cmd` sin `ack` en 10 s → `last_result=cmd_rejected`.
- Agente: sin `welcome` en 3 s → reintento con backoff exponencial (1 s … 30 s).

## 4. Emparejado

El dongle entra en modo emparejado si no tiene clave o tras pulsación larga (3 s) del botón; muestra un código de 6 dígitos aleatorio en pantalla durante 120 s.

```
agente (usuario teclea código C)        dongle (muestra C)
  pair{v:1, nonce=Na}                →
                                    ←  pair_chal{nonce=Nd}
  K = HKDF-SHA256(ikm=C ascii, salt=bytes(Na)||bytes(Nd), info="microesp-pair-v1", L=32)
  pair_confirm{sig=HMAC(K,"pair|Na|Nd")} →
                                    ←  pair_ok{sig=HMAC(K,"pair_ok|Nd|Na")}   (o err{code:"pair_failed"})
```

- El dongle guarda K solo tras verificar `pair_confirm`; el agente guarda K solo tras verificar `pair_ok`.
- 3 intentos fallidos → el dongle sale del modo emparejado.
- Un emparejado nuevo sustituye la clave anterior.
- Limitación conocida: un atacante que capture el tráfico USB podría forzar el código de 6 dígitos offline. Aceptado: requiere acceso físico/root al PC.

## 5. Mapeo a DPs Tuya (dongle)

Número = `abilityId`; en la nube (TuyaLink) cada DP viaja por su código de propiedad (`firmware/schema/dp.json`).

| Mensaje | DP |
|---|---|
| `tele.cpu` / `mem` / `disk_free` | 105 `cpu_usage` / 106 `mem_usage` / 107 `disk_free` (value, escala 1) |
| `tele.uptime` | 110 `pc_uptime` |
| `hello.host` | 111 `pc_hostname` |
| sesión `ready` y heartbeat vivo | 108 `agent_online` |
| DP 103 `power_off` / 104 `reboot` tras la cuenta atrás del DP 112 `cmd_countdown` | `cmd` shutdown / reboot |
| resultado de `ack` | 113 `last_result` |
