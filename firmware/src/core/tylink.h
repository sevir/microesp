/*
 * MicroESP — TuyaLink (Tuya's open MQTT device protocol) codec. Pure C (cJSON +
 * mbedTLS via mesp_crypto), host-tested; the MQTT/TLS transport lives in the HAL
 * (esp_components/mesp_hal/hal_cloud.c) and the glue in src/cloud.c.
 *
 * Connection (broker m1.tuya<region>.com:8883, TLS with server verification):
 *   clientId  tuyalink_<deviceId>
 *   username  <deviceId>|signMethod=hmacSha256,timestamp=<unix s>,secureMode=1,accessType=1
 *   password  hex(HMAC-SHA256(deviceSecret, "deviceId=<id>,timestamp=<ts>,secureMode=1,accessType=1"))
 * Topics (base tylink/<deviceId>/thing/):
 *   publish   property/report        {"msgId":..,"time":<ms>,"data":{"<code>":{"value":V,"time":<ms>}}}
 *   subscribe property/set           {"msgId":..,"time":..,"data":{"<code>":V,...}}
 *   publish   property/set_response  {"msgId":<same>,"time":<ms>,"code":0}
 *   subscribe action/execute         (no actions in the thing model: answered with an error code)
 *   publish   model/get / subscribe model/get_response, property/report_response
 * Property codes and types are those of dp_model.c (the numeric ids are the abilityIds):
 * bool -> JSON bool, value/bitmap -> JSON integer, enum -> JSON string (range name),
 * string -> JSON string (escaped; e.g. DP 115 scripts carries compact JSON as a string).
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dp_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TYL_PORT        8883
#define TYL_MSGID_MAX   32
#define TYL_ID_MAX      32 /* productId / deviceId */
#define TYL_SECRET_MAX  64
#define TYL_PASS_HEX    64
#define TYL_USER_MAX    (TYL_ID_MAX + 80)
#define TYL_TOPIC_MAX   (TYL_ID_MAX + 48)
/* property/report payload buffer: every DP at its widest value (64-byte hostname of
 * control characters, each escaped to 6 bytes, 221-byte scripts list) fits with margin, and
 * with the MQTT header stays within the esp-mqtt 2048-byte buffer (hal_cloud.c). */
#define TYL_REPORT_MAX  1920
#define TYL_CODE_OK     0
#define TYL_CODE_FAIL   1 /* generic failure (invalid property, unsupported action) */

typedef enum {
    TYL_T_UNKNOWN = 0,
    TYL_T_PROP_SET,
    TYL_T_ACTION_EXEC,
    TYL_T_MODEL_GET_RESP,
    TYL_T_REPORT_RESP,
} tyl_topic_t;

/* "eu" "us" "cn" "in" -> broker host, NULL if unknown. */
const char *tyl_broker_host(const char *region);
/* productId / deviceId: [A-Za-z0-9] 8..32. deviceSecret: printable ASCII without
 * spaces 8..64. */
bool tyl_valid_id(const char *s);
bool tyl_valid_secret(const char *s);

/* Lengths without NUL, or -1 if it does not fit. */
int tyl_client_id(const char *dev_id, char *out, size_t n);
int tyl_username(const char *dev_id, uint32_t ts, char *out, size_t n);
/* 0 on success; out = 64 lowercase hex chars. */
int tyl_password(const char *secret, const char *dev_id, uint32_t ts, char out[TYL_PASS_HEX + 1]);
/* "tylink/<dev>/thing/<suffix>" */
int tyl_topic(const char *dev_id, const char *suffix, char *out, size_t n);
tyl_topic_t tyl_topic_kind(const char *topic, size_t len, const char *dev_id);
/* Unique-enough msgId (16 lowercase hex chars) from a counter and a random word. */
void tyl_msgid(uint32_t counter, uint32_t rnd, char out[TYL_MSGID_MAX + 1]);

/* One property to report. s is used for string DPs only. */
typedef struct {
    uint8_t id;
    int32_t v;
    const char *s;
} tyl_prop_t;

/* property/report payload. Returns the length, or -1 (unknown DP, bad enum value,
 * string longer than the DP maximum or buffer too small). */
int tyl_build_report(const tyl_prop_t *p, int n, const char *msgid, int64_t time_ms, char *out, size_t cap);
/* {"msgId":..,"time":..,"code":N} for property/set_response and action/execute_response */
int tyl_build_response(const char *msgid, int64_t time_ms, int code, char *out, size_t cap);
/* model/get request {"msgId":..,"time":..,"data":{"format":"complex"}} */
int tyl_build_model_get(const char *msgid, int64_t time_ms, char *out, size_t cap);

typedef enum {
    TYL_W_OK = 0,
    TYL_W_UNKNOWN_CODE,
    TYL_W_READ_ONLY,
    TYL_W_BAD_TYPE,
    TYL_W_OUT_OF_RANGE,
    TYL_W_TOO_MANY,
} tyl_wres_t;
const char *tyl_wres_name(tyl_wres_t r);

typedef struct {
    uint8_t id;
    int32_t v;                  /* normalised (bool 0/1, enum index); 0 for strings */
    char s[DPM_WSTR_MAX + 1];   /* string DPs (validated by dpm_decode_write_str) */
} tyl_write_t;

#define TYL_MAX_WRITES DPM_COUNT
typedef struct {
    char msgid[TYL_MSGID_MAX + 1];
    int nwrites;
    tyl_write_t w[TYL_MAX_WRITES];
    int nrejected;
    tyl_wres_t first_reject; /* reason of the first rejected property */
    char first_reject_code[24];
} tyl_set_t;

/* Parse property/set. 0 = valid envelope (msgId 1..32 chars + "data" object; the
 * accepted writes are in w[], rejected properties counted), -1 = malformed message
 * (no answer possible). Unknown codes, read-only properties, wrong JSON types and
 * out-of-range values (strings: too long or bad charset) are rejected one by one; the
 * valid ones are still applied. */
int tyl_parse_set(const char *json, size_t len, tyl_set_t *out);
/* msgId of any message (action/execute). 0 / -1. */
int tyl_parse_msgid(const char *json, size_t len, char msgid[TYL_MSGID_MAX + 1]);
/* "code" of a *_response message (missing -> -1 returned). */
int tyl_parse_code(const char *json, size_t len, int *code);

#ifdef __cplusplus
}
#endif
