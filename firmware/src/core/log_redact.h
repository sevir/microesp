/*
 * MicroESP — log redaction (security hardening), pure C and host-tested.
 *
 * Every TuyaOpen/app log line goes to UART0 and to the RAM ring dumped by "!log" on
 * the CDC port. A line is replaced by a fixed marker when it contains a registered
 * secret (Tuya AuthKey, the agent pairing code) or a sensitive keyword (TuyaOpen's
 * debug logs can print activation payloads, tokens or keys). Matching is
 * case-insensitive for keywords and exact for secrets.
 *
 * Secrets are set from one task while other tasks log: a torn read can only make a
 * single match fail transiently; the registered values never appear in logs by
 * construction anyway (this is defence in depth).
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LR_SLOTS      4
#define LR_SECRET_MAX 64
#define LR_SECRET_MIN 6 /* shorter strings are never registered (false positives) */

enum { LR_SLOT_TUYA_AUTHKEY = 0, LR_SLOT_PAIR_CODE = 1 };

#define LR_REDACTED "[redacted: sensitive log line]\n"

/* NULL or "" clears the slot. */
void lr_set_secret(int slot, const char *secret);
bool lr_sensitive(const char *line);
/* line, or LR_REDACTED if it must not be output */
const char *lr_filter(const char *line);

#ifdef __cplusplus
}
#endif
