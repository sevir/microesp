#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "dp_model.h"
#include "unity.h"

static dp_model_t M;

static int collect_mark(uint32_t now, uint8_t *ids)
{
    int n = dpm_collect(&M, now, ids, DPM_COUNT);
    for (int i = 0; i < n; i++) dpm_mark_reported(&M, ids[i], now);
    return n;
}

static bool has(const uint8_t *ids, int n, uint8_t id)
{
    for (int i = 0; i < n; i++)
        if (ids[i] == id) return true;
    return false;
}

static void test_table_matches_spec(void)
{
    const uint8_t ids[] = {101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114};
    TEST_ASSERT_EQUAL(sizeof(ids), DPM_COUNT);
    for (size_t i = 0; i < sizeof(ids); i++) TEST_ASSERT_NOT_NULL(dpm_desc(ids[i]));
    TEST_ASSERT_NULL(dpm_desc(115)); /* unknown */
    TEST_ASSERT_EQUAL(DPT_ENUM, dpm_desc(DP_PC_STATE)->type);
    TEST_ASSERT_EQUAL(5, dpm_desc(DP_PC_STATE)->max);
    TEST_ASSERT_EQUAL(DPT_BITMAP, dpm_desc(DP_FAULT)->type);
    TEST_ASSERT_EQUAL(0x0f, dpm_desc(DP_FAULT)->max); /* 4 bits, no vbus_low */
    TEST_ASSERT_EQUAL(60, dpm_desc(DP_CMD_COUNTDOWN)->max);
    TEST_ASSERT_EQUAL(DPT_STR, dpm_desc(DP_PC_HOSTNAME)->type);
    for (int i = 0; i < DPM_COUNT; i++) TEST_ASSERT_TRUE(dpm_desc_at(i)->min_interval_ms >= 300); /* <=200/min */
}

static void test_schema_json_in_sync(void)
{
    FILE *f = fopen(SCHEMA_PATH, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "schema/dp.json missing");
    static char buf[16384];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    cJSON *j = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(j);
    cJSON *dps = cJSON_GetObjectItem(j, "dps");
    TEST_ASSERT_EQUAL(DPM_COUNT, cJSON_GetArraySize(dps));
    cJSON *d;
    cJSON_ArrayForEach(d, dps)
    {
        const dpm_desc_t *x = dpm_desc((uint8_t)cJSON_GetObjectItem(d, "id")->valueint);
        TEST_ASSERT_NOT_NULL(x);
        TEST_ASSERT_EQUAL_STRING(x->code, cJSON_GetObjectItem(d, "code")->valuestring);
        const char *types[] = {"bool", "value", "enum", "string", "bitmap"};
        TEST_ASSERT_EQUAL_STRING(types[x->type], cJSON_GetObjectItem(d, "type")->valuestring);
        TEST_ASSERT_EQUAL(x->writable, !strcmp(cJSON_GetObjectItem(d, "mode")->valuestring, "rw"));
        cJSON *r = cJSON_GetObjectItem(d, "range");
        if (x->type == DPT_ENUM) TEST_ASSERT_EQUAL(x->max + 1, cJSON_GetArraySize(r));
        if (x->type == DPT_BITMAP) TEST_ASSERT_EQUAL(4, cJSON_GetArraySize(cJSON_GetObjectItem(d, "label")));
        if (x->type == DPT_VALUE) {
            TEST_ASSERT_EQUAL(x->min, cJSON_GetObjectItem(d, "min")->valueint);
            TEST_ASSERT_EQUAL(x->max, cJSON_GetObjectItem(d, "max")->valueint);
        }
    }
    cJSON_Delete(j);
}

static void test_change_and_throttle(void)
{
    uint8_t ids[DPM_COUNT];
    dpm_init(&M);
    TEST_ASSERT_EQUAL(0, dpm_collect(&M, 0, ids, DPM_COUNT)); /* nothing valid yet */
    dpm_set(&M, DP_PC_STATE, 4);
    dpm_set(&M, DP_AGENT_ONLINE, 1);
    TEST_ASSERT_EQUAL(2, collect_mark(0, ids));
    TEST_ASSERT_EQUAL(0, collect_mark(100, ids)); /* unchanged */
    dpm_set(&M, DP_PC_STATE, 3);
    TEST_ASSERT_EQUAL(0, collect_mark(500, ids)); /* throttled (1 s) */
    TEST_ASSERT_EQUAL(1, collect_mark(1000, ids));
    TEST_ASSERT_EQUAL(DP_PC_STATE, ids[0]);
    dpm_set(&M, DP_PC_STATE, 99); /* clamped */
    TEST_ASSERT_EQUAL(5, dpm_get(&M, DP_PC_STATE));
}

static void test_telemetry_threshold_and_periodic(void)
{
    uint8_t ids[DPM_COUNT];
    dpm_init(&M);
    dpm_set(&M, DP_CPU, 100);
    TEST_ASSERT_EQUAL(1, collect_mark(0, ids));
    dpm_set(&M, DP_CPU, 119); /* < 20 tenths */
    TEST_ASSERT_EQUAL(0, collect_mark(10000, ids));
    dpm_set(&M, DP_CPU, 121); /* >= 20 tenths but within 5 s throttle */
    TEST_ASSERT_EQUAL(0, collect_mark(4000, ids));
    TEST_ASSERT_EQUAL(1, collect_mark(10000, ids));
    dpm_set(&M, DP_CPU, 125); /* small change: only after 30 s */
    TEST_ASSERT_EQUAL(0, collect_mark(39999, ids));
    TEST_ASSERT_EQUAL(1, collect_mark(40000, ids));
    /* uptime: once a minute */
    dpm_set(&M, DP_PC_UPTIME, 10);
    TEST_ASSERT_EQUAL(1, collect_mark(40000, ids));
    dpm_set(&M, DP_PC_UPTIME, 30);
    TEST_ASSERT_EQUAL(0, collect_mark(99999, ids));
    TEST_ASSERT_EQUAL(1, collect_mark(100000, ids));
}

static void test_force_all_and_strings(void)
{
    uint8_t ids[DPM_COUNT];
    dpm_init(&M);
    dpm_set_str(&M, DP_PC_HOSTNAME, "thinkstation");
    dpm_set(&M, DP_FAULT, 0x1f); /* masked to 4 bits */
    TEST_ASSERT_EQUAL(0x0f, dpm_get(&M, DP_FAULT));
    collect_mark(0, ids);
    TEST_ASSERT_EQUAL(0, collect_mark(5000, ids));
    dpm_force_all(&M);
    int n = collect_mark(5001, ids);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_TRUE(has(ids, n, DP_PC_HOSTNAME));
    dpm_set_str(&M, DP_PC_HOSTNAME, "other");
    n = collect_mark(7000, ids);
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL_STRING("other", dpm_get_str(&M, DP_PC_HOSTNAME));
}

static void test_decode_writes(void)
{
    int32_t v;
    TEST_ASSERT_EQUAL(0, dpm_decode_write(DP_POWER_ON, DPT_BOOL, 1, &v));
    TEST_ASSERT_EQUAL(1, v);
    TEST_ASSERT_EQUAL(0, dpm_decode_write(DP_REBOOT, DPT_BOOL, 7, &v));
    TEST_ASSERT_EQUAL(1, v);
    TEST_ASSERT_EQUAL(0, dpm_decode_write(DP_WAKE_METHOD, DPT_ENUM, 2, &v));
    TEST_ASSERT_EQUAL(-1, dpm_decode_write(DP_WAKE_METHOD, DPT_ENUM, 3, &v));
    TEST_ASSERT_EQUAL(0, dpm_decode_write(DP_CMD_COUNTDOWN, DPT_VALUE, 60, &v));
    TEST_ASSERT_EQUAL(-1, dpm_decode_write(DP_CMD_COUNTDOWN, DPT_VALUE, 61, &v));
    TEST_ASSERT_EQUAL(-1, dpm_decode_write(DP_CMD_COUNTDOWN, DPT_VALUE, -1, &v));
    TEST_ASSERT_EQUAL(-1, dpm_decode_write(DP_CMD_COUNTDOWN, DPT_BOOL, 1, &v)); /* wrong type */
    TEST_ASSERT_EQUAL(-1, dpm_decode_write(DP_PC_STATE, DPT_ENUM, 1, &v));      /* read-only */
    TEST_ASSERT_EQUAL(-1, dpm_decode_write(115, DPT_VALUE, 1, &v));            /* unknown */
}

static void test_json_dump(void)
{
    char b[512];
    dpm_init(&M);
    dpm_set(&M, DP_PC_STATE, 4);
    dpm_set(&M, DP_AGENT_ONLINE, 1);
    dpm_set_str(&M, DP_PC_HOSTNAME, "a\"b");
    dpm_to_json(&M, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("{\"101\":4,\"108\":true,\"111\":\"a\\\"b\"}", b);
    cJSON *j = cJSON_Parse(b);
    TEST_ASSERT_NOT_NULL(j);
    cJSON_Delete(j);
    char small[8];
    dpm_to_json(&M, small, sizeof(small)); /* truncation must stay NUL-terminated */
    TEST_ASSERT_TRUE(strlen(small) < sizeof(small));
}

/* Async report (sent from the Tuya thread): a value that changed while the report was
 * in flight must stay due, and a force set meanwhile must survive. */
static void test_async_mark_keeps_newer_value_due(void)
{
    uint8_t ids[DPM_COUNT];
    dpm_init(&M);
    dpm_set(&M, DP_PC_STATE, 2);
    dpm_set_str(&M, DP_PC_HOSTNAME, "a");
    int n = dpm_collect(&M, 1000, ids, DPM_COUNT);
    TEST_ASSERT_EQUAL(2, n);
    /* snapshot sent: 101=2, 111="a"; meanwhile both change and 113 is forced */
    dpm_set(&M, DP_PC_STATE, 4);
    dpm_set_str(&M, DP_PC_HOSTNAME, "b");
    dpm_mark_reported_as(&M, DP_PC_STATE, 1000, 2, NULL);
    dpm_mark_reported_as(&M, DP_PC_HOSTNAME, 1000, 0, "a");
    n = dpm_collect(&M, 5000, ids, DPM_COUNT);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_TRUE(has(ids, n, DP_PC_STATE));
    TEST_ASSERT_TRUE(has(ids, n, DP_PC_HOSTNAME));
    /* force survives an async mark */
    dpm_mark_reported_as(&M, DP_PC_STATE, 5000, 4, NULL);
    dpm_mark_reported_as(&M, DP_PC_HOSTNAME, 5000, 0, "b");
    TEST_ASSERT_EQUAL(0, dpm_collect(&M, 9000, ids, DPM_COUNT));
    dpm_force(&M, DP_PC_STATE);
    dpm_mark_reported_as(&M, DP_PC_STATE, 9000, 4, NULL);
    TEST_ASSERT_EQUAL(1, dpm_collect(&M, 9100, ids, DPM_COUNT));
}

void run_dp_model_tests(void)
{
    RUN_TEST(test_async_mark_keeps_newer_value_due);
    RUN_TEST(test_table_matches_spec);
    RUN_TEST(test_schema_json_in_sync);
    RUN_TEST(test_change_and_throttle);
    RUN_TEST(test_telemetry_threshold_and_periodic);
    RUN_TEST(test_force_all_and_strings);
    RUN_TEST(test_decode_writes);
    RUN_TEST(test_json_dump);
}
