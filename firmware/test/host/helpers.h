#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "link_proto.h"

/* protocol/testdata/vectors.json (parsed once) */
cJSON *vectors(void);
const char *vstr(const char *section, const char *key);

/* A link_t wired to capture buffers. */
#define H_MAX_OUT 64
#define H_MAX_EV  64
typedef struct {
    link_t l;
    char out[H_MAX_OUT][520];
    int nout;
    link_ev_t ev[H_MAX_EV];
    char ev_host[H_MAX_EV][72];
    char ev_err[H_MAX_EV][40];
    int nev;
    uint8_t rnd[4][8];
    int nrnd, rnd_i;
    uint8_t saved_key[32];
    bool saved;
    bool save_fail;
} harness_t;

void h_init(harness_t *h, const uint8_t *key);
void h_push_random_hex(harness_t *h, const char *hex16);
void h_rx(harness_t *h, const char *line, uint32_t now);
const char *h_last(const harness_t *h);
/* "t" of the last sent line, err code if t==err */
const char *h_last_t(const harness_t *h, char *code, size_t n);
int h_count_ev(const harness_t *h, link_ev_type_t t);
const link_ev_t *h_find_ev(const harness_t *h, link_ev_type_t t);
void h_clear(harness_t *h);
void hex2bin(const char *hex, uint8_t *out, size_t n);
