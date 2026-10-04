# MicroESP CDC v1 protocol (agent ↔ dongle)

Contract between `microesp-agent` (Go, on the PC) and the dongle firmware, over the CDC ACM port of the composite USB device. Normative test vectors: `protocol/testdata/vectors.json`.

## 1. Transport

- USB CDC ACM. Baud rate irrelevant (opened at 115200 8N1).
- USB identification: VID `0x303A`, PID `0x4002`, product string `MicroESP`, serial `MESP-<mac12hex>` (Wi-Fi MAC in lowercase without separators). Linux: `/dev/serial/by-id/usb-*MicroESP*`, optional udev symlink `/dev/microesp`.
- Framing: one UTF-8 JSON object per line, terminated by `\n` (`\r\n` is tolerated). **Maximum 512 bytes** per line including the terminator. Longer line → discarded up to the next `\n` and answered with `err{code:"too_long"}`.
- Every message is an object with a `t` (type) field. Unknown fields are ignored (forward compatibility). Unknown type → `err{code:"bad_msg"}`.
- Hex is always lowercase. Nonces: 8 random bytes = 16 hex characters.

## 2. Cryptography

- Shared key `K`: 32 bytes, established during pairing (§4). Dongle: NVS. Agent: `/etc/microesp/agent.key` (hex, mode 0600).
- `sig = hex(HMAC-SHA256(K, ascii_message))`, 64 characters. Constant-time comparison.

## 3. Session

```
agent                           dongle
  hello{v,host,os,agent_ver,macs,nonce=Na}  →
                                ←  welcome{v,fw,dev,nonce=Nd,sig=HMAC(K,"welcome|Na|Nd")}
  [agent verifies sig; on failure closes and retries with backoff]
  auth{sig=HMAC(K,"auth|Nd|Na")}           →
                                ←  ready            (or err{code:"unauth"})
  tele / hb ...                             →
                                ←  notice / cmd
  ack{id,ok,err?}                           →
```

- `v` must be `1`; any other value → `err{code:"unsupported_version"}`.
- If the dongle has no key: it answers `hello` with `err{code:"not_paired"}`.
- Before `ready`, the dongle ignores `tele`/`hb`/`ack` and answers `err{code:"unauth"}`.
- A new `hello` restarts the session (the agent reconnected).
- The dongle stores the MACs from `hello.macs` (max. 4) in NVS for Wake-on-LAN and `host` for the `pc_hostname` DP.

### Agent → dongle messages

| t | Fields | Notes |
|---|---|---|
| `hello` | `v`, `host` (≤64), `os` (`linux`/`windows`), `agent_ver`, `macs` (array `aa:bb:..`), `nonce` | starts the session |
| `auth` | `sig` | |
| `tele` | `seq` (uint32), `cpu`, `mem`, `disk_free` (integers 0..1000 = tenths of %), `uptime` (s, uint32) | every 10 s or change >20 tenths |
| `hb` | — | every 5 s if there was no other message |
| `ack` | `id`, `ok` (bool), optional `err` (`bad_sig`, `replay`, `exec_failed`, `unknown_action`) | reply to `cmd`, before executing |
| `pair`, `pair_confirm` | see §4 | |

### Dongle → agent messages

| t | Fields | Notes |
|---|---|---|
| `welcome` | `v`, `fw`, `dev` (mac12hex), `nonce`, `sig` | |
| `ready` | — | authenticated session |
| `notice` | `action` (`shutdown`/`reboot`), `in` (s) | countdown notice; `in:0` with `action:"cancel"` = cancelled |
| `cmd` | `id` (uint32 increasing per session, starts at 1), `action` (`shutdown`/`reboot`), `sig=HMAC(K,"cmd|id|action|Na|Nd")` | |
| `pair_chal`, `pair_ok` | see §4 | |
| `err` | `code` (`bad_msg`, `too_long`, `unauth`, `not_paired`, `unsupported_version`, `pair_failed`) | |

### Command rules (agent)

- Verify `sig` with the Na/Nd of the current session; failure → `ack{ok:false,err:"bad_sig"}`.
- `id` must be greater than the last one accepted in the session; otherwise → `ack{ok:false,err:"replay"}`.
- Unknown `action` → `ack{ok:false,err:"unknown_action"}`.
- If everything is valid: send `ack{ok:true}` **and then** execute.

### Timeouts

- Dongle: no messages from the agent for 15 s → `agent_online=false`.
- Dongle: `cmd` without `ack` in 10 s → `last_result=cmd_rejected`.
- Agent: no `welcome` in 3 s → retry with exponential backoff (1 s … 30 s).

## 4. Pairing

The dongle enters pairing mode if it has no key or after a long press (3 s) of the button; it shows a random 6-digit code on the screen for 120 s.

```
agent (user types code C)               dongle (shows C)
  pair{v:1, nonce=Na}                →
                                    ←  pair_chal{nonce=Nd}
  K = HKDF-SHA256(ikm=C ascii, salt=bytes(Na)||bytes(Nd), info="microesp-pair-v1", L=32)
  pair_confirm{sig=HMAC(K,"pair|Na|Nd")} →
                                    ←  pair_ok{sig=HMAC(K,"pair_ok|Nd|Na")}   (or err{code:"pair_failed"})
```

- The dongle stores K only after verifying `pair_confirm`; the agent stores K only after verifying `pair_ok`.
- 3 failed attempts → the dongle leaves pairing mode.
- A new pairing replaces the previous key.
- Known limitation: an attacker who captures the USB traffic could brute-force the 6-digit code offline. Accepted: it requires physical/root access to the PC.

## 5. Mapping to Tuya DPs (dongle)

Number = `abilityId`; in the cloud (TuyaLink) each DP travels under its property code (`firmware/schema/dp.json`).

| Message | DP |
|---|---|
| `tele.cpu` / `mem` / `disk_free` | 105 `cpu_usage` / 106 `mem_usage` / 107 `disk_free` (value, scale 1) |
| `tele.uptime` | 110 `pc_uptime` |
| `hello.host` | 111 `pc_hostname` |
| session `ready` and live heartbeat | 108 `agent_online` |
| DP 103 `power_off` / 104 `reboot` after the DP 112 `cmd_countdown` countdown | `cmd` shutdown / reboot |
| result of `ack` | 113 `last_result` |
