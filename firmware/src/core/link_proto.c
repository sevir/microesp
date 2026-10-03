#include "link_proto.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

#define ELAPSED(now, t) ((int32_t)((uint32_t)(now) - (uint32_t)(t)))

/* ------------------------------------------------------------------ helpers */
static void emit(link_t *l, const link_ev_t *ev)
{
    if (l->cb.event) l->cb.event(l->cb.ctx, ev);
}

static void send_line(link_t *l, const char *s)
{
    if (l->cb.send) l->cb.send(l->cb.ctx, s);
}

static void send_err(link_t *l, const char *code)
{
    char b[64];
    snprintf(b, sizeof(b), "{\"t\":\"err\",\"code\":\"%s\"}", code);
    send_line(l, b);
    link_ev_t ev = {.type = LINK_EV_PROTO_ERR, .err_code = code};
    emit(l, &ev);
}

static void new_nonce(link_t *l, char out[MC_NONCE_HEX + 1])
{
    uint8_t n[MC_NONCE_LEN] = {0};
    if (l->cb.random) l->cb.random(l->cb.ctx, n, sizeof(n));
    mc_hex(n, sizeof(n), out);
}

static void set_online(link_t *l, bool on)
{
    if (l->online == on) return;
    l->online = on;
    link_ev_t ev = {.type = LINK_EV_ONLINE, .online = on};
    emit(l, &ev);
}

static void drop_session(link_t *l)
{
    l->st = LINK_IDLE;
    if (l->cmd_pending) {
        /* The session that should ack this cmd is gone (port closed, agent restarted,
         * re-paired): an ack can no longer arrive, so resolve it now instead of
         * leaving the power flow waiting forever. */
        l->cmd_pending = false;
        link_ev_t ev = {.type = LINK_EV_ACK_TIMEOUT};
        ev.ack.id = l->cmd_pending_id;
        ev.ack.ok = false;
        ev.ack.err = "session_closed";
        emit(l, &ev);
    }
    set_online(l, false);
}

/* Max nesting depth of objects/arrays, ignoring brackets inside strings. */
static int json_depth(const char *s, size_t n)
{
    int depth = 0, max = 0;
    bool in_str = false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (in_str) {
            if (c == '\\') i++;
            else if (c == '"') in_str = false;
            continue;
        }
        if (c == '"') in_str = true;
        else if (c == '{' || c == '[') {
            if (++depth > max) max = depth;
        } else if (c == '}' || c == ']') depth--;
    }
    return max;
}

/* Integral number within [lo, hi]. */
static bool get_int(const cJSON *o, const char *k, double lo, double hi, double *out)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, k);
    if (!cJSON_IsNumber(it)) return false;
    double v = it->valuedouble;
    if (!(v >= lo && v <= hi) || floor(v) != v) return false;
    *out = v;
    return true;
}

static const char *get_str(const cJSON *o, const char *k)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(it) && it->valuestring ? it->valuestring : NULL;
}

int link_parse_mac(const char *s, uint8_t mac[6])
{
    if (!s || strlen(s) != 17) return -1;
    for (int i = 0; i < 6; i++) {
        char h[3] = {s[3 * i], s[3 * i + 1], 0};
        if (i < 5 && s[3 * i + 2] != ':') return -1;
        if (mc_unhex(h, &mac[i], 1) != 0) return -1;
    }
    return 0;
}

/* "v" present and numeric: returns 1 ok, 0 missing/invalid (bad_msg), -1 other version. */
static int check_version(const cJSON *o)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, "v");
    if (!cJSON_IsNumber(v)) return 0;
    return v->valuedouble == 1.0 ? 1 : -1;
}

/* ------------------------------------------------------------------ public */
void link_init(link_t *l, const link_cbs_t *cb, const char *fw, const char *dev12hex, const uint8_t *key)
{
    memset(l, 0, sizeof(*l));
    if (cb) l->cb = *cb;
    snprintf(l->fw, sizeof(l->fw), "%s", fw && *fw ? fw : "0.0.0");
    snprintf(l->dev, sizeof(l->dev), "%s", dev12hex ? dev12hex : "000000000000");
    if (key) {
        memcpy(l->key, key, MC_KEY_LEN);
        l->has_key = true;
    }
}

void link_close_session(link_t *l)
{
    drop_session(l);
    l->pair_chal = false;
}

void link_forget_key(link_t *l)
{
    memset(l->key, 0, sizeof(l->key));
    l->has_key = false;
    drop_session(l);
}

bool link_ready(const link_t *l) { return l->st == LINK_READY; }
bool link_online(const link_t *l) { return l->online; }
bool link_pairing(const link_t *l) { return l->pair_mode; }

uint32_t link_pair_remaining_ms(const link_t *l, uint32_t now_ms)
{
    if (!l->pair_mode) return 0;
    int32_t r = ELAPSED(l->pair_until_ms, now_ms);
    return r > 0 ? (uint32_t)r : 0;
}

void link_pair_start(link_t *l, const char *code, uint32_t now_ms)
{
    snprintf(l->pair_code, sizeof(l->pair_code), "%s", code);
    l->pair_mode = true;
    l->pair_chal = false;
    l->pair_fails = 0;
    l->pair_until_ms = now_ms + LINK_PAIR_WINDOW;
}

static void pair_end(link_t *l)
{
    bool was = l->pair_mode;
    l->pair_mode = false;
    l->pair_chal = false;
    memset(l->pair_code, 0, sizeof(l->pair_code));
    if (was) {
        link_ev_t ev = {.type = LINK_EV_PAIR_END};
        emit(l, &ev);
    }
}

void link_pair_stop(link_t *l) { pair_end(l); }

void link_rx_too_long(link_t *l, uint32_t now_ms)
{
    (void)now_ms;
    l->rx_bad++;
    send_err(l, "too_long");
}

static void on_hello(link_t *l, const cJSON *o)
{
    int v = check_version(o);
    if (v < 0) { send_err(l, "unsupported_version"); return; }
    static const char *req[] = {"host", "os", "agent_ver", "macs", "nonce"};
    if (v == 0) { send_err(l, "bad_msg"); return; }
    for (size_t i = 0; i < sizeof(req) / sizeof(req[0]); i++)
        if (!cJSON_HasObjectItem(o, req[i])) { send_err(l, "bad_msg"); return; }
    const char *host = get_str(o, "host"), *os = get_str(o, "os"), *av = get_str(o, "agent_ver");
    const char *nonce = get_str(o, "nonce");
    const cJSON *macs = cJSON_GetObjectItemCaseSensitive(o, "macs");
    if (!host || strlen(host) > LINK_MAX_HOST || !os || (strcmp(os, "linux") && strcmp(os, "windows")) || !av ||
        !*av || !cJSON_IsArray(macs) || cJSON_GetArraySize(macs) > LINK_MAX_MACS || !mc_is_hex(nonce, MC_NONCE_HEX))
        { send_err(l, "bad_msg"); return; }
    uint8_t m[LINK_MAX_MACS][6];
    int nm = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, macs)
    {
        if (!cJSON_IsString(it) || link_parse_mac(it->valuestring, m[nm]) != 0) { send_err(l, "bad_msg"); return; }
        nm++;
    }
    if (!l->has_key) {
        drop_session(l);
        { send_err(l, "not_paired"); return; }
    }
    /* A new hello restarts the session (agent reconnected). */
    drop_session(l);
    snprintf(l->na, sizeof(l->na), "%s", nonce);
    new_nonce(l, l->nd);
    snprintf(l->pend_host, sizeof(l->pend_host), "%s", host);
    memcpy(l->pend_macs, m, sizeof(m));
    l->pend_nmacs = nm;
    char msg[64], sig[MC_SIG_HEX + 1], out[256];
    snprintf(msg, sizeof(msg), "welcome|%s|%s", l->na, l->nd);
    mc_sign(l->key, msg, sig);
    snprintf(out, sizeof(out), "{\"t\":\"welcome\",\"v\":1,\"fw\":\"%s\",\"dev\":\"%s\",\"nonce\":\"%s\",\"sig\":\"%s\"}",
             l->fw, l->dev, l->nd, sig);
    l->st = LINK_WAIT_AUTH;
    send_line(l, out);
}

static void on_auth(link_t *l, const cJSON *o, uint32_t now)
{
    const char *sig = get_str(o, "sig");
    if (!sig) { send_err(l, "bad_msg"); return; }
    if (l->st != LINK_WAIT_AUTH || !l->has_key) { send_err(l, "unauth"); return; }
    char msg[64];
    snprintf(msg, sizeof(msg), "auth|%s|%s", l->nd, l->na);
    if (!mc_verify(l->key, msg, sig)) {
        l->st = LINK_IDLE;
        { send_err(l, "unauth"); return; }
    }
    l->st = LINK_READY;
    l->cmd_id = 0;
    l->cmd_pending = false;
    l->last_rx_ms = now;
    l->sessions++;
    send_line(l, "{\"t\":\"ready\"}");
    link_ev_t ev = {.type = LINK_EV_HELLO};
    ev.hello.host = l->pend_host;
    memcpy(ev.hello.macs, l->pend_macs, sizeof(ev.hello.macs));
    ev.hello.nmacs = l->pend_nmacs;
    emit(l, &ev);
    ev = (link_ev_t){.type = LINK_EV_READY};
    emit(l, &ev);
    set_online(l, true);
}

static void on_tele(link_t *l, const cJSON *o)
{
    double seq, cpu, mem, disk, up;
    if (!get_int(o, "seq", 0, 4294967295.0, &seq) || !get_int(o, "cpu", 0, 1000, &cpu) ||
        !get_int(o, "mem", 0, 1000, &mem) || !get_int(o, "disk_free", 0, 1000, &disk) ||
        !get_int(o, "uptime", 0, 4294967295.0, &up))
        { send_err(l, "bad_msg"); return; }
    link_ev_t ev = {.type = LINK_EV_TELE};
    ev.tele.seq = (uint32_t)seq;
    ev.tele.cpu = (int)cpu;
    ev.tele.mem = (int)mem;
    ev.tele.disk_free = (int)disk;
    ev.tele.uptime = (uint32_t)up;
    emit(l, &ev);
}

static void on_ack(link_t *l, const cJSON *o)
{
    double id;
    const cJSON *ok = cJSON_GetObjectItemCaseSensitive(o, "ok");
    const cJSON *err = cJSON_GetObjectItemCaseSensitive(o, "err");
    if (!get_int(o, "id", 0, 4294967295.0, &id) || !cJSON_IsBool(ok)) { send_err(l, "bad_msg"); return; }
    if (err && (!cJSON_IsString(err) || strlen(err->valuestring) > 32)) { send_err(l, "bad_msg"); return; }
    if (!l->cmd_pending || (uint32_t)id != l->cmd_pending_id) return; /* stale / unknown: ignored */
    l->cmd_pending = false;
    link_ev_t ev = {.type = LINK_EV_ACK};
    ev.ack.id = (uint32_t)id;
    ev.ack.ok = cJSON_IsTrue(ok);
    ev.ack.err = err ? err->valuestring : "";
    emit(l, &ev);
}

static void on_pair(link_t *l, const cJSON *o)
{
    int v = check_version(o);
    if (v < 0) { send_err(l, "unsupported_version"); return; }
    const char *nonce = get_str(o, "nonce");
    if (v == 0 || !mc_is_hex(nonce, MC_NONCE_HEX)) { send_err(l, "bad_msg"); return; }
    if (!l->pair_mode) { send_err(l, "pair_failed"); return; }
    snprintf(l->pna, sizeof(l->pna), "%s", nonce);
    new_nonce(l, l->pnd);
    l->pair_chal = true;
    char out[64];
    snprintf(out, sizeof(out), "{\"t\":\"pair_chal\",\"nonce\":\"%s\"}", l->pnd);
    send_line(l, out);
}

static void on_pair_confirm(link_t *l, const cJSON *o)
{
    const char *sig = get_str(o, "sig");
    if (!sig) { send_err(l, "bad_msg"); return; }
    if (!l->pair_mode || !l->pair_chal) { send_err(l, "pair_failed"); return; }
    uint8_t k[MC_KEY_LEN];
    char msg[64];
    snprintf(msg, sizeof(msg), "pair|%s|%s", l->pna, l->pnd);
    bool ok = mc_derive_pair_key(l->pair_code, l->pna, l->pnd, k) == 0 && mc_verify(k, msg, sig);
    if (ok && l->cb.save_key && !l->cb.save_key(l->cb.ctx, k)) ok = false;
    l->pair_chal = false;
    if (!ok) {
        memset(k, 0, sizeof(k));
        l->pair_fails++;
        send_err(l, "pair_failed");
        link_ev_t ev = {.type = LINK_EV_PAIR_FAIL, .pair_fails = l->pair_fails};
        emit(l, &ev);
        if (l->pair_fails >= LINK_PAIR_MAX_FAILS) pair_end(l);
        return;
    }
    /* The old key (and any session based on it) is replaced. */
    drop_session(l);
    memcpy(l->key, k, MC_KEY_LEN);
    l->has_key = true;
    char sig2[MC_SIG_HEX + 1], out[128];
    snprintf(msg, sizeof(msg), "pair_ok|%s|%s", l->pnd, l->pna);
    mc_sign(k, msg, sig2);
    memset(k, 0, sizeof(k));
    snprintf(out, sizeof(out), "{\"t\":\"pair_ok\",\"sig\":\"%s\"}", sig2);
    send_line(l, out);
    link_ev_t ev = {.type = LINK_EV_PAIRED};
    emit(l, &ev);
    pair_end(l);
}

void link_rx_line(link_t *l, const char *line, size_t len, uint32_t now_ms)
{
    while (len && (line[len - 1] == '\r' || line[len - 1] == '\n')) len--;
    if (!len) return;
    l->rx_lines++;
    if (len + 1 > LINK_MAX_LINE) { link_rx_too_long(l, now_ms); return; }
    if (line[0] != '{' || json_depth(line, len) > LINK_MAX_DEPTH) {
        l->rx_bad++;
        { send_err(l, "bad_msg"); return; }
    }
    cJSON *o = cJSON_ParseWithLength(line, len);
    const char *t = o && cJSON_IsObject(o) ? get_str(o, "t") : NULL;
    if (!t) {
        l->rx_bad++;
        send_err(l, "bad_msg");
        cJSON_Delete(o);
        return;
    }
    bool session_msg = !strcmp(t, "tele") || !strcmp(t, "hb") || !strcmp(t, "ack");
    if (session_msg) {
        if (l->st != LINK_READY) {
            send_err(l, "unauth");
        } else {
            l->last_rx_ms = now_ms;
            set_online(l, true);
            if (!strcmp(t, "tele")) on_tele(l, o);
            else if (!strcmp(t, "ack")) on_ack(l, o);
            /* hb: liveness only */
        }
    } else if (!strcmp(t, "hello")) {
        on_hello(l, o);
    } else if (!strcmp(t, "auth")) {
        on_auth(l, o, now_ms);
    } else if (!strcmp(t, "pair")) {
        on_pair(l, o);
    } else if (!strcmp(t, "pair_confirm")) {
        on_pair_confirm(l, o);
    } else {
        l->rx_bad++;
        send_err(l, "bad_msg"); /* unknown type (or a dongle->agent type) */
    }
    cJSON_Delete(o);
}

void link_tick(link_t *l, uint32_t now_ms)
{
    if (l->online && ELAPSED(now_ms, l->last_rx_ms) >= LINK_ONLINE_TIMEOUT) set_online(l, false);
    if (l->cmd_pending && ELAPSED(now_ms, l->cmd_sent_ms) >= LINK_ACK_TIMEOUT) {
        l->cmd_pending = false;
        link_ev_t ev = {.type = LINK_EV_ACK_TIMEOUT};
        ev.ack.id = l->cmd_pending_id;
        ev.ack.ok = false;
        ev.ack.err = "timeout";
        emit(l, &ev);
    }
    if (l->pair_mode && ELAPSED(now_ms, l->pair_until_ms) >= 0) pair_end(l);
}

bool link_send_notice(link_t *l, const char *action, int in_s)
{
    if (l->st != LINK_READY) return false;
    char out[80];
    snprintf(out, sizeof(out), "{\"t\":\"notice\",\"action\":\"%s\",\"in\":%d}", action, in_s);
    send_line(l, out);
    return true;
}

uint32_t link_send_cmd(link_t *l, const char *action, uint32_t now_ms)
{
    if (l->st != LINK_READY || l->cmd_pending || !l->has_key) return 0;
    if (strcmp(action, "shutdown") && strcmp(action, "reboot")) return 0;
    uint32_t id = ++l->cmd_id;
    char msg[96], sig[MC_SIG_HEX + 1], out[192];
    snprintf(msg, sizeof(msg), "cmd|%lu|%s|%s|%s", (unsigned long)id, action, l->na, l->nd);
    mc_sign(l->key, msg, sig);
    snprintf(out, sizeof(out), "{\"t\":\"cmd\",\"id\":%lu,\"action\":\"%s\",\"sig\":\"%s\"}", (unsigned long)id, action,
             sig);
    l->cmd_pending = true;
    l->cmd_pending_id = id;
    l->cmd_sent_ms = now_ms;
    send_line(l, out);
    return id;
}
