/* Fuzz-ish robustness tests: truncated, huge, malformed and random lines must never
 * crash (ASan/UBSan enabled), must answer with at most one line each and must not
 * disturb an established session. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "helpers.h"
#include "unity.h"

static harness_t H;

static void up(void)
{
    uint8_t k[32];
    hex2bin(vstr("session", "key_hex"), k, 32);
    h_init(&H, k);
    h_push_random_hex(&H, vstr("session", "dongle_nonce"));
    cJSON *valid = cJSON_GetObjectItem(cJSON_GetObjectItem(vectors(), "messages"), "valid");
    char *hello = cJSON_PrintUnformatted(cJSON_GetArrayItem(valid, 0));
    char *auth = cJSON_PrintUnformatted(cJSON_GetArrayItem(valid, 2));
    h_rx(&H, hello, 1);
    h_rx(&H, auth, 2);
    cJSON_free(hello);
    cJSON_free(auth);
    TEST_ASSERT_TRUE(link_ready(&H.l));
}

static void feed(const char *p, size_t n)
{
    int before = H.nout;
    link_rx_line(&H.l, p, n, 10);
    TEST_ASSERT_TRUE(H.nout - before <= 1);
    if (H.nout > H_MAX_OUT - 4) H.nout = 0;
    H.nev = 0;
}

static void test_truncations_of_every_valid_message(void)
{
    up();
    cJSON *valid = cJSON_GetObjectItem(cJSON_GetObjectItem(vectors(), "messages"), "valid");
    cJSON *it;
    int lines = 0;
    cJSON_ArrayForEach(it, valid)
    {
        char *s = cJSON_PrintUnformatted(it);
        size_t n = strlen(s);
        for (size_t cut = 0; cut < n; cut++) {
            const char *t = cJSON_GetObjectItem(it, "t")->valuestring;
            if (!strcmp(t, "hello") || !strcmp(t, "auth") || !strcmp(t, "pair") || !strcmp(t, "pair_confirm"))
                continue; /* a full hello/auth would legitimately reset the session */
            char *c = malloc(cut + 1);
            memcpy(c, s, cut);
            c[cut] = 0;
            feed(c, cut);
            free(c);
            lines++;
        }
        cJSON_free(s);
    }
    TEST_ASSERT_TRUE(lines > 300);
    TEST_ASSERT_TRUE(link_ready(&H.l));
}

static void test_huge_and_deep(void)
{
    up();
    static char big[100000];
    memset(big, 'x', sizeof(big));
    big[0] = '{';
    feed(big, sizeof(big));
    char c[40];
    TEST_ASSERT_EQUAL_STRING("err", h_last_t(&H, c, sizeof(c)));
    TEST_ASSERT_EQUAL_STRING("too_long", c);
    /* exactly 511 chars (+\n = 512) is allowed, 512 chars is too long */
    char l511[600];
    int pad = 511 - (int)strlen("{\"t\":\"hb\",\"p\":\"\"}");
    snprintf(l511, sizeof(l511), "{\"t\":\"hb\",\"p\":\"%0*d\"}", pad, 0);
    TEST_ASSERT_EQUAL(511, (int)strlen(l511));
    H.nout = 0;
    feed(l511, 511);
    TEST_ASSERT_EQUAL(0, H.nout); /* hb accepted silently */
    char l512[600];
    snprintf(l512, sizeof(l512), "{\"t\":\"hb\",\"p\":\"%0*d\"}", pad + 1, 0);
    feed(l512, 512);
    TEST_ASSERT_EQUAL_STRING("too_long", (h_last_t(&H, c, sizeof(c)), c));
    /* deep nesting is refused before cJSON recursion */
    char deep[512];
    size_t o = 0;
    o += (size_t)snprintf(deep, sizeof(deep), "{\"t\":\"hb\",\"a\":");
    while (o < 400) deep[o++] = '[';
    deep[o] = 0;
    feed(deep, o);
    TEST_ASSERT_EQUAL_STRING("bad_msg", (h_last_t(&H, c, sizeof(c)), c));
    snprintf(deep, sizeof(deep), "{\"t\":\"hb\",\"a\":\"[[[[[[[[[[[[[[[[[[[[[[\"}"); /* brackets inside a string */
    H.nout = 0;
    feed(deep, strlen(deep));
    TEST_ASSERT_EQUAL(0, H.nout);
    TEST_ASSERT_TRUE(link_ready(&H.l));
}

static void test_malformed_catalogue(void)
{
    up();
    const char *cases[] = {
        "{",
        "}",
        "{}",
        "{\"t\":1}",
        "{\"t\":null}",
        "{\"t\":\"\"}",
        "{\"t\":\"tele\"}",
        "{\"t\":\"tele\",\"seq\":\"1\",\"cpu\":1,\"mem\":1,\"disk_free\":1,\"uptime\":1}",
        "{\"t\":\"tele\",\"seq\":1e400,\"cpu\":1,\"mem\":1,\"disk_free\":1,\"uptime\":1}",
        "{\"t\":\"tele\",\"seq\":1,\"cpu\":NaN,\"mem\":1,\"disk_free\":1,\"uptime\":1}",
        "{\"t\":\"tele\",\"seq\":-1,\"cpu\":1,\"mem\":1,\"disk_free\":1,\"uptime\":1}",
        "{\"t\":\"ack\",\"id\":1}",
        "{\"t\":\"ack\",\"id\":1,\"ok\":\"yes\"}",
        "{\"t\":\"ack\",\"id\":1,\"ok\":true,\"err\":5}",
        "{\"t\":\"auth\"}",
        "{\"t\":\"auth\",\"sig\":7}",
        "{\"t\":\"pair\",\"v\":1}",
        "{\"t\":\"pair\",\"v\":\"1\",\"nonce\":\"a1b2c3d4e5f60718\"}",
        "{\"t\":\"pair_confirm\"}",
        "{\"t\":\"hello\",\"v\":1}",
        "{\"t\":\"hello\",\"v\":1,\"host\":5,\"os\":\"linux\",\"agent_ver\":\"1\",\"macs\":[],\"nonce\":\"a1b2c3d4e5f60718\"}",
        "{\"t\":\"hello\",\"v\":1,\"host\":\"h\",\"os\":\"linux\",\"agent_ver\":\"1\",\"macs\":{},\"nonce\":\"a1b2c3d4e5f60718\"}",
        "{\"t\":\"hello\",\"v\":1,\"host\":\"h\",\"os\":\"linux\",\"agent_ver\":\"\",\"macs\":[],\"nonce\":\"a1b2c3d4e5f60718\"}",
        "{\"t\":\"hello\",\"v\":1,\"host\":\"h\",\"os\":\"linux\",\"agent_ver\":\"1\",\"macs\":[1],\"nonce\":\"a1b2c3d4e5f60718\"}",
        "{\"t\":\"hello\",\"v\":1,\"host\":\"h\",\"os\":\"linux\",\"agent_ver\":\"1\",\"macs\":[],\"nonce\":\"A1B2C3D4E5F60718\"}",
        "{\"t\":\"hb\"} trailing",
        "{\"t\":\"hb\",}",
        "{\"t\":\"h\\u0000b\"}",
        "{\"t\":\"hb\",\"x\":\"\\ud800\"}",
        "\xff\xfe\xfd",
        "{\"t\":\"\xc3\x28\"}",
        "!notacli",
        "   {\"t\":\"hb\"}",
        "null",
        "\"str\"",
        "123",
        "{\"t\":\"tele\",\"t\":\"hb\"}",
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) feed(cases[i], strlen(cases[i]));
    /* embedded NUL: length-delimited parse must not read past it */
    const char nul[] = "{\"t\":\"hb\"\0garbage}";
    feed(nul, sizeof(nul) - 1);
    TEST_ASSERT_TRUE(link_ready(&H.l));
}

static void test_random_bytes(void)
{
    up();
    srand(12345);
    char buf[600];
    for (int round = 0; round < 20000; round++) {
        size_t n = (size_t)(rand() % 560);
        int mode = rand() % 3;
        for (size_t i = 0; i < n; i++) {
            if (mode == 0) buf[i] = (char)(rand() & 0xff);
            else {
                static const char alpha[] = "{}[]\":,0123456789.eE+-tfnulrseahbckpoid\\ ";
                buf[i] = alpha[rand() % (int)(sizeof(alpha) - 1)];
            }
        }
        if (mode == 2 && n > 6) memcpy(buf, "{\"t\":\"", 6);
        feed(buf, n);
    }
    TEST_ASSERT_TRUE(link_ready(&H.l));
}

void run_fuzz_tests(void)
{
    RUN_TEST(test_truncations_of_every_valid_message);
    RUN_TEST(test_huge_and_deep);
    RUN_TEST(test_malformed_catalogue);
    RUN_TEST(test_random_bytes);
}
