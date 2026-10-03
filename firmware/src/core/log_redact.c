#include "log_redact.h"

#include <string.h>

static char s_secret[LR_SLOTS][LR_SECRET_MAX + 1];

static const char *const k_words[] = {"authkey", "auth_key", "localkey", "local_key", "seckey", "sec_key",
                                      "secret",  "regist_key", "passwd", "password",  "token",  "psk"};

void lr_set_secret(int slot, const char *secret)
{
    if (slot < 0 || slot >= LR_SLOTS) return;
    size_t n = secret ? strlen(secret) : 0;
    if (n < LR_SECRET_MIN || n > LR_SECRET_MAX) {
        s_secret[slot][0] = 0;
        return;
    }
    s_secret[slot][0] = 0; /* never expose a half-written value as a valid string */
    memcpy(&s_secret[slot][1], secret + 1, n - 1);
    s_secret[slot][n] = 0;
    s_secret[slot][0] = secret[0];
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

/* case-insensitive substring; word is lowercase */
static bool contains_ci(const char *s, const char *word)
{
    size_t wl = strlen(word);
    for (; *s; s++) {
        size_t i = 0;
        while (i < wl && s[i] && lower(s[i]) == word[i]) i++;
        if (i == wl) return true;
    }
    return false;
}

bool lr_sensitive(const char *line)
{
    if (!line) return false;
    for (int i = 0; i < LR_SLOTS; i++)
        if (s_secret[i][0] && strstr(line, s_secret[i])) return true;
    for (size_t i = 0; i < sizeof(k_words) / sizeof(k_words[0]); i++)
        if (contains_ci(line, k_words[i])) return true;
    return false;
}

const char *lr_filter(const char *line) { return lr_sensitive(line) ? LR_REDACTED : line; }
