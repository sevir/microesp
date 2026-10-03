#include "tylink.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "mesp_crypto.h"

static const struct {
    const char *region, *host;
} k_brokers[] = {
    {"eu", "m1.tuyaeu.com"},
    {"us", "m1.tuyaus.com"},
    {"cn", "m1.tuyacn.com"},
    {"in", "m1.tuyain.com"},
};

const char *tyl_broker_host(const char *region)
{
    for (size_t i = 0; region && i < sizeof(k_brokers) / sizeof(k_brokers[0]); i++)
        if (!strcmp(region, k_brokers[i].region)) return k_brokers[i].host;
    return NULL;
}

bool tyl_valid_id(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    if (n < 8 || n > TYL_ID_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
    }
    return true;
}

bool tyl_valid_secret(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    if (n < 8 || n > TYL_SECRET_MAX) return false;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)s[i] <= 0x20 || (unsigned char)s[i] >= 0x7f) return false;
    return true;
}

static int fit(int w, size_t n) { return (w < 0 || (size_t)w >= n) ? -1 : w; }

int tyl_client_id(const char *dev_id, char *out, size_t n) { return fit(snprintf(out, n, "tuyalink_%s", dev_id), n); }

int tyl_username(const char *dev_id, uint32_t ts, char *out, size_t n)
{
    return fit(snprintf(out, n, "%s|signMethod=hmacSha256,timestamp=%lu,secureMode=1,accessType=1", dev_id,
                        (unsigned long)ts),
               n);
}

int tyl_password(const char *secret, const char *dev_id, uint32_t ts, char out[TYL_PASS_HEX + 1])
{
    char msg[TYL_ID_MAX + 80];
    uint8_t mac[32];
    out[0] = 0;
    if (!secret || !dev_id) return -1;
    int w = snprintf(msg, sizeof(msg), "deviceId=%s,timestamp=%lu,secureMode=1,accessType=1", dev_id,
                     (unsigned long)ts);
    if (fit(w, sizeof(msg)) < 0) return -1;
    int rc = mc_hmac_sha256((const uint8_t *)secret, strlen(secret), msg, (size_t)w, mac);
    if (rc == 0) mc_hex(mac, sizeof(mac), out);
    memset(mac, 0, sizeof(mac));
    return rc ? -1 : 0;
}

int tyl_topic(const char *dev_id, const char *suffix, char *out, size_t n)
{
    return fit(snprintf(out, n, "tylink/%s/thing/%s", dev_id, suffix), n);
}

tyl_topic_t tyl_topic_kind(const char *topic, size_t len, const char *dev_id)
{
    static const struct {
        const char *suffix;
        tyl_topic_t kind;
    } k[] = {
        {"property/set", TYL_T_PROP_SET},
        {"action/execute", TYL_T_ACTION_EXEC},
        {"model/get_response", TYL_T_MODEL_GET_RESP},
        {"property/report_response", TYL_T_REPORT_RESP},
    };
    char pre[TYL_TOPIC_MAX];
    if (!topic || !dev_id) return TYL_T_UNKNOWN;
    int pl = snprintf(pre, sizeof(pre), "tylink/%s/thing/", dev_id);
    if (fit(pl, sizeof(pre)) < 0 || len <= (size_t)pl || memcmp(topic, pre, (size_t)pl)) return TYL_T_UNKNOWN;
    const char *s = topic + pl;
    size_t sl = len - (size_t)pl;
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++)
        if (strlen(k[i].suffix) == sl && !memcmp(s, k[i].suffix, sl)) return k[i].kind;
    return TYL_T_UNKNOWN;
}

void tyl_msgid(uint32_t counter, uint32_t rnd, char out[TYL_MSGID_MAX + 1])
{
    snprintf(out, TYL_MSGID_MAX + 1, "%08lx%08lx", (unsigned long)rnd, (unsigned long)counter);
}

/* ------------------------------------------------------------------ encoder */
typedef struct {
    char *b;
    size_t cap, o;
    bool err;
} wbuf_t;

static void put(wbuf_t *w, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void put(wbuf_t *w, const char *fmt, ...)
{
    if (w->err) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(w->b + w->o, w->cap - w->o, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= w->cap - w->o) {
        w->err = true;
        return;
    }
    w->o += (size_t)n;
}

static void put_str(wbuf_t *w, const char *s)
{
    put(w, "\"");
    for (; s && *s && !w->err; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') put(w, "\\%c", c);
        else if (c < 0x20 || c == 0x7f) put(w, "\\u%04x", c);
        else put(w, "%c", c);
    }
    put(w, "\"");
}

static int finish(wbuf_t *w)
{
    if (w->err) {
        if (w->cap) w->b[0] = 0;
        return -1;
    }
    return (int)w->o;
}

static bool put_value(wbuf_t *w, const tyl_prop_t *p)
{
    const dpm_desc_t *d = dpm_desc(p->id);
    if (!d) return false;
    switch (d->type) {
    case DPT_BOOL: put(w, "%s", p->v ? "true" : "false"); break;
    case DPT_VALUE:
    case DPT_BITMAP: put(w, "%ld", (long)p->v); break;
    case DPT_ENUM: {
        const char *name = dpm_enum_name(p->id, p->v);
        if (!name) return false;
        put_str(w, name);
        break;
    }
    case DPT_STR: put_str(w, p->s ? p->s : ""); break;
    default: return false;
    }
    return true;
}

int tyl_build_report(const tyl_prop_t *p, int n, const char *msgid, int64_t time_ms, char *out, size_t cap)
{
    wbuf_t w = {out, cap, 0, cap == 0};
    if (!p || n <= 0 || !msgid) return -1;
    put(&w, "{\"msgId\":");
    put_str(&w, msgid);
    put(&w, ",\"time\":%lld,\"data\":{", (long long)time_ms);
    for (int i = 0; i < n; i++) {
        const dpm_desc_t *d = dpm_desc(p[i].id);
        if (!d) return finish(&(wbuf_t){out, cap, 0, true});
        put(&w, "%s\"%s\":{\"value\":", i ? "," : "", d->code);
        if (!put_value(&w, &p[i])) return finish(&(wbuf_t){out, cap, 0, true});
        put(&w, ",\"time\":%lld}", (long long)time_ms);
    }
    put(&w, "}}");
    return finish(&w);
}

int tyl_build_response(const char *msgid, int64_t time_ms, int code, char *out, size_t cap)
{
    wbuf_t w = {out, cap, 0, cap == 0};
    put(&w, "{\"msgId\":");
    put_str(&w, msgid ? msgid : "");
    put(&w, ",\"time\":%lld,\"code\":%d}", (long long)time_ms, code);
    return finish(&w);
}

int tyl_build_model_get(const char *msgid, int64_t time_ms, char *out, size_t cap)
{
    wbuf_t w = {out, cap, 0, cap == 0};
    put(&w, "{\"msgId\":");
    put_str(&w, msgid ? msgid : "");
    put(&w, ",\"time\":%lld,\"data\":{\"format\":\"complex\"}}", (long long)time_ms);
    return finish(&w);
}

/* ------------------------------------------------------------------ decoder */
const char *tyl_wres_name(tyl_wres_t r)
{
    static const char *const k[] = {"ok", "unknown_code", "read_only", "bad_type", "out_of_range", "too_many"};
    return (unsigned)r < sizeof(k) / sizeof(k[0]) ? k[r] : "?";
}

static bool get_msgid(const cJSON *root, char msgid[TYL_MSGID_MAX + 1])
{
    const cJSON *m = cJSON_GetObjectItemCaseSensitive(root, "msgId");
    /* The EU cloud sends msgId as a JSON number on downlinks (e.g. "msgId":3). */
    if (cJSON_IsNumber(m)) {
        double d = m->valuedouble;
        if (!isfinite(d) || d < 0 || d != floor(d) || d > 9007199254740991.0) return false;
        snprintf(msgid, TYL_MSGID_MAX + 1, "%.0f", d);
        return true;
    }
    if (!cJSON_IsString(m) || !m->valuestring) return false;
    size_t n = strlen(m->valuestring);
    if (n == 0 || n > TYL_MSGID_MAX) return false;
    memcpy(msgid, m->valuestring, n + 1);
    return true;
}

static bool integral(const cJSON *v, int32_t min, int32_t max, int32_t *out, tyl_wres_t *why)
{
    if (!cJSON_IsNumber(v)) {
        *why = TYL_W_BAD_TYPE;
        return false;
    }
    double d = v->valuedouble;
    if (!isfinite(d) || d != floor(d)) {
        *why = TYL_W_BAD_TYPE;
        return false;
    }
    if (d < (double)min || d > (double)max) {
        *why = TYL_W_OUT_OF_RANGE;
        return false;
    }
    *out = (int32_t)d;
    return true;
}

static tyl_wres_t decode(const cJSON *item, uint8_t *id, int32_t *val)
{
    const dpm_desc_t *d = dpm_desc_by_code(item->string);
    if (!d) return TYL_W_UNKNOWN_CODE;
    if (!d->writable) return TYL_W_READ_ONLY;
    tyl_wres_t why = TYL_W_OK;
    int32_t v = 0;
    switch (d->type) {
    case DPT_BOOL:
        if (!cJSON_IsBool(item)) return TYL_W_BAD_TYPE;
        v = cJSON_IsTrue(item) ? 1 : 0;
        break;
    case DPT_VALUE:
    case DPT_BITMAP:
        if (!integral(item, d->min, d->max, &v, &why)) return why;
        break;
    case DPT_ENUM:
        if (!cJSON_IsString(item)) return TYL_W_BAD_TYPE;
        v = dpm_enum_parse(d->id, item->valuestring);
        if (v < 0) return TYL_W_OUT_OF_RANGE;
        break;
    default: return TYL_W_BAD_TYPE; /* no writable string DP */
    }
    int32_t norm;
    if (dpm_decode_write(d->id, d->type, v, &norm) != 0) return TYL_W_OUT_OF_RANGE;
    *id = d->id;
    *val = norm;
    return TYL_W_OK;
}

int tyl_parse_set(const char *json, size_t len, tyl_set_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!json || !len) return -1;
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return -1;
    int rc = -1;
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (cJSON_IsObject(root) && get_msgid(root, out->msgid) && cJSON_IsObject(data)) {
        rc = 0;
        const cJSON *it;
        cJSON_ArrayForEach(it, data)
        {
            uint8_t id = 0;
            int32_t v = 0;
            tyl_wres_t r = out->nwrites >= TYL_MAX_WRITES ? TYL_W_TOO_MANY : decode(it, &id, &v);
            if (r == TYL_W_OK) {
                out->w[out->nwrites].id = id;
                out->w[out->nwrites].v = v;
                out->nwrites++;
            } else {
                if (!out->nrejected++) {
                    out->first_reject = r;
                    snprintf(out->first_reject_code, sizeof(out->first_reject_code), "%s",
                             it->string ? it->string : "?");
                }
            }
        }
    }
    if (rc) out->msgid[0] = 0;
    cJSON_Delete(root);
    return rc;
}

int tyl_parse_msgid(const char *json, size_t len, char msgid[TYL_MSGID_MAX + 1])
{
    msgid[0] = 0;
    if (!json || !len) return -1;
    cJSON *root = cJSON_ParseWithLength(json, len);
    int rc = root && cJSON_IsObject(root) && get_msgid(root, msgid) ? 0 : -1;
    if (rc) msgid[0] = 0;
    cJSON_Delete(root);
    return rc;
}

int tyl_parse_code(const char *json, size_t len, int *code)
{
    if (!json || !len || !code) return -1;
    cJSON *root = cJSON_ParseWithLength(json, len);
    const cJSON *c = root ? cJSON_GetObjectItemCaseSensitive(root, "code") : NULL;
    int rc = cJSON_IsNumber(c) ? 0 : -1;
    if (!rc) *code = c->valueint;
    cJSON_Delete(root);
    return rc;
}
