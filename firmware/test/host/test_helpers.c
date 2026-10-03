#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "helpers.h"
#include "unity.h"

static cJSON *s_vec;

cJSON *vectors(void)
{
    if (s_vec) return s_vec;
    FILE *f = fopen(VECTORS_PATH, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", VECTORS_PATH);
        abort();
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) abort();
    b[n] = 0;
    fclose(f);
    s_vec = cJSON_Parse(b);
    free(b);
    if (!s_vec) abort();
    return s_vec;
}

const char *vstr(const char *section, const char *key)
{
    cJSON *s = cJSON_GetObjectItem(vectors(), section);
    cJSON *v = cJSON_GetObjectItem(s, key);
    if (!cJSON_IsString(v)) {
        fprintf(stderr, "vector %s.%s missing\n", section, key);
        abort();
    }
    return v->valuestring;
}

void hex2bin(const char *hex, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(hex + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

static void cb_send(void *ctx, const char *line)
{
    harness_t *h = ctx;
    TEST_ASSERT_TRUE_MESSAGE(strlen(line) + 1 <= 512, "dongle line over 512 bytes");
    TEST_ASSERT_NULL_MESSAGE(strchr(line, '\n'), "dongle line contains newline");
    if (h->nout < H_MAX_OUT) snprintf(h->out[h->nout++], sizeof(h->out[0]), "%s", line);
}

static void cb_random(void *ctx, uint8_t *buf, size_t n)
{
    harness_t *h = ctx;
    if (h->rnd_i < h->nrnd) {
        memcpy(buf, h->rnd[h->rnd_i++], n < 8 ? n : 8);
    } else {
        for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)(0x40 + i);
    }
}

static bool cb_save(void *ctx, const uint8_t key[32])
{
    harness_t *h = ctx;
    if (h->save_fail) return false;
    memcpy(h->saved_key, key, 32);
    h->saved = true;
    return true;
}

static void cb_event(void *ctx, const link_ev_t *ev)
{
    harness_t *h = ctx;
    if (h->nev >= H_MAX_EV) return;
    h->ev[h->nev] = *ev;
    if (ev->type == LINK_EV_HELLO) {
        snprintf(h->ev_host[h->nev], sizeof(h->ev_host[0]), "%s", ev->hello.host);
        h->ev[h->nev].hello.host = h->ev_host[h->nev];
    }
    if (ev->type == LINK_EV_PROTO_ERR) {
        snprintf(h->ev_err[h->nev], sizeof(h->ev_err[0]), "%s", ev->err_code);
        h->ev[h->nev].err_code = h->ev_err[h->nev];
    }
    if (ev->type == LINK_EV_ACK || ev->type == LINK_EV_ACK_TIMEOUT) {
        snprintf(h->ev_err[h->nev], sizeof(h->ev_err[0]), "%s", ev->ack.err ? ev->ack.err : "");
        h->ev[h->nev].ack.err = h->ev_err[h->nev];
    }
    h->nev++;
}

void h_init(harness_t *h, const uint8_t *key)
{
    memset(h, 0, sizeof(*h));
    link_cbs_t cb = {.send = cb_send, .random = cb_random, .save_key = cb_save, .event = cb_event, .ctx = h};
    link_init(&h->l, &cb, "1.0.0", "907069f662dc", key);
}

void h_push_random_hex(harness_t *h, const char *hex16)
{
    hex2bin(hex16, h->rnd[h->nrnd++], 8);
}

void h_rx(harness_t *h, const char *line, uint32_t now) { link_rx_line(&h->l, line, strlen(line), now); }

const char *h_last(const harness_t *h) { return h->nout ? h->out[h->nout - 1] : ""; }

const char *h_last_t(const harness_t *h, char *code, size_t n)
{
    static char t[32];
    t[0] = 0;
    if (code && n) code[0] = 0;
    cJSON *o = cJSON_Parse(h_last(h));
    if (!o) return t;
    cJSON *tt = cJSON_GetObjectItem(o, "t");
    if (cJSON_IsString(tt)) snprintf(t, sizeof(t), "%s", tt->valuestring);
    cJSON *c = cJSON_GetObjectItem(o, "code");
    if (code && cJSON_IsString(c)) snprintf(code, n, "%s", c->valuestring);
    cJSON_Delete(o);
    return t;
}

int h_count_ev(const harness_t *h, link_ev_type_t t)
{
    int n = 0;
    for (int i = 0; i < h->nev; i++) n += h->ev[i].type == t;
    return n;
}

const link_ev_t *h_find_ev(const harness_t *h, link_ev_type_t t)
{
    for (int i = h->nev - 1; i >= 0; i--)
        if (h->ev[i].type == t) return &h->ev[i];
    return NULL;
}

void h_clear(harness_t *h)
{
    h->nout = 0;
    h->nev = 0;
}
