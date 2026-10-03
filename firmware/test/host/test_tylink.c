/* TuyaLink codec (core/tylink.c): auth, topics, report/response encoding and
 * property/set decoding. All credentials here are FICTITIOUS test values; the HMAC
 * vectors were computed with Python:
 *   hmac.new(secret, f"deviceId={did},timestamp={ts},secureMode=1,accessType=1", sha256).hexdigest()
 */
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "dp_model.h"
#include "pc_state.h"
#include "powercmd.h"
#include "tylink.h"
#include "unity.h"
#include "wake_fsm.h"

#define DID "6cfake0device0id00test"
#define SEC "FakeSecret12345x"
#define TS  1759536000u

static void test_auth_vectors(void)
{
    char pw[TYL_PASS_HEX + 1];
    TEST_ASSERT_EQUAL(0, tyl_password(SEC, DID, TS, pw));
    TEST_ASSERT_EQUAL_STRING("a7800dea634b3f52f07cb0b8459eed1c2d00f0870743284e6eb687f4a394ee1c", pw);
    TEST_ASSERT_EQUAL(0, tyl_password("abcdefgh", "dev12345", 0, pw));
    TEST_ASSERT_EQUAL_STRING("88604e5e53fae787dbf940ec9a4132f6174eb69f137f22d868f5f251630cdd93", pw);
    TEST_ASSERT_EQUAL(64, strlen(pw));
    TEST_ASSERT_EQUAL(-1, tyl_password(NULL, DID, TS, pw));

    char u[TYL_USER_MAX];
    TEST_ASSERT_TRUE(tyl_username(DID, TS, u, sizeof(u)) > 0);
    TEST_ASSERT_EQUAL_STRING(DID "|signMethod=hmacSha256,timestamp=1759536000,secureMode=1,accessType=1", u);
    TEST_ASSERT_EQUAL(-1, tyl_username(DID, TS, u, 20)); /* does not fit */
    char c[64];
    TEST_ASSERT_TRUE(tyl_client_id(DID, c, sizeof(c)) > 0);
    TEST_ASSERT_EQUAL_STRING("tuyalink_" DID, c);
}

static void test_validation_and_hosts(void)
{
    TEST_ASSERT_EQUAL_STRING("m1.tuyaeu.com", tyl_broker_host("eu"));
    TEST_ASSERT_EQUAL_STRING("m1.tuyaus.com", tyl_broker_host("us"));
    TEST_ASSERT_EQUAL_STRING("m1.tuyacn.com", tyl_broker_host("cn"));
    TEST_ASSERT_EQUAL_STRING("m1.tuyain.com", tyl_broker_host("in"));
    TEST_ASSERT_NULL(tyl_broker_host("EU"));
    TEST_ASSERT_NULL(tyl_broker_host("xx"));
    TEST_ASSERT_NULL(tyl_broker_host(NULL));
    TEST_ASSERT_TRUE(tyl_valid_id(DID));
    TEST_ASSERT_FALSE(tyl_valid_id("short"));
    TEST_ASSERT_FALSE(tyl_valid_id("has space 123"));
    TEST_ASSERT_FALSE(tyl_valid_id("abc/def/ghi"));      /* would break the topic */
    TEST_ASSERT_FALSE(tyl_valid_id("0123456789abcdef0123456789abcdef0")); /* 33 */
    TEST_ASSERT_TRUE(tyl_valid_secret(SEC));
    TEST_ASSERT_TRUE(tyl_valid_secret("a!b#c$d%e"));
    TEST_ASSERT_FALSE(tyl_valid_secret("with space"));
    TEST_ASSERT_FALSE(tyl_valid_secret("1234567"));
    TEST_ASSERT_FALSE(tyl_valid_secret(NULL));
}

static void test_topics(void)
{
    char t[TYL_TOPIC_MAX];
    TEST_ASSERT_TRUE(tyl_topic(DID, "property/report", t, sizeof(t)) > 0);
    TEST_ASSERT_EQUAL_STRING("tylink/" DID "/thing/property/report", t);
    const char *set = "tylink/" DID "/thing/property/set";
    TEST_ASSERT_EQUAL(TYL_T_PROP_SET, tyl_topic_kind(set, strlen(set), DID));
    const char *ae = "tylink/" DID "/thing/action/execute";
    TEST_ASSERT_EQUAL(TYL_T_ACTION_EXEC, tyl_topic_kind(ae, strlen(ae), DID));
    const char *mg = "tylink/" DID "/thing/model/get_response";
    TEST_ASSERT_EQUAL(TYL_T_MODEL_GET_RESP, tyl_topic_kind(mg, strlen(mg), DID));
    const char *rr = "tylink/" DID "/thing/property/report_response";
    TEST_ASSERT_EQUAL(TYL_T_REPORT_RESP, tyl_topic_kind(rr, strlen(rr), DID));
    /* not NUL-terminated: the length rules */
    TEST_ASSERT_EQUAL(TYL_T_UNKNOWN, tyl_topic_kind(set, strlen(set) - 1, DID));
    const char *other = "tylink/otherdevice12/thing/property/set";
    TEST_ASSERT_EQUAL(TYL_T_UNKNOWN, tyl_topic_kind(other, strlen(other), DID));
    const char *sub = "tylink/" DID "/thing/property/set_response";
    TEST_ASSERT_EQUAL(TYL_T_UNKNOWN, tyl_topic_kind(sub, strlen(sub), DID));
    TEST_ASSERT_EQUAL(TYL_T_UNKNOWN, tyl_topic_kind(NULL, 0, DID));
    char m1[TYL_MSGID_MAX + 1], m2[TYL_MSGID_MAX + 1];
    tyl_msgid(1, 0xdeadbeef, m1);
    tyl_msgid(2, 0xdeadbeef, m2);
    TEST_ASSERT_EQUAL_STRING("deadbeef00000001", m1);
    TEST_ASSERT_TRUE(strcmp(m1, m2) != 0);
    TEST_ASSERT_TRUE(strlen(m1) <= TYL_MSGID_MAX);
}

static void test_enum_names_match_core(void)
{
    for (int i = 0; i < PCS__COUNT; i++) TEST_ASSERT_EQUAL_STRING(pcs_name((pcs_t)i), dpm_enum_name(DP_PC_STATE, i));
    for (int i = 0; i < WM__COUNT; i++)
        TEST_ASSERT_EQUAL_STRING(wake_method_name((wake_method_t)i), dpm_enum_name(DP_WAKE_METHOD, i));
    for (int i = 0; i < LR__COUNT; i++)
        TEST_ASSERT_EQUAL_STRING(lr_name((last_result_t)i), dpm_enum_name(DP_LAST_RESULT, i));
    TEST_ASSERT_NULL(dpm_enum_name(DP_PC_STATE, 6));
    TEST_ASSERT_NULL(dpm_enum_name(DP_PC_STATE, -1));
    TEST_ASSERT_NULL(dpm_enum_name(DP_CPU, 0));
    TEST_ASSERT_EQUAL(2, dpm_enum_parse(DP_WAKE_METHOD, "hid_then_wol"));
    TEST_ASSERT_EQUAL(-1, dpm_enum_parse(DP_WAKE_METHOD, "HID"));
    TEST_ASSERT_EQUAL(-1, dpm_enum_parse(DP_CPU, "hid"));
    /* every DP code resolves, and back */
    for (int i = 0; i < DPM_COUNT; i++)
        TEST_ASSERT_EQUAL_PTR(dpm_desc_at(i), dpm_desc_by_code(dpm_desc_at(i)->code));
    TEST_ASSERT_NULL(dpm_desc_by_code("vbus"));
}

static void test_build_report(void)
{
    char buf[1536];
    tyl_prop_t p[] = {
        {DP_PC_STATE, PCS_ON, NULL},    {DP_POWER_ON, 0, NULL},       {DP_CPU, 123, NULL},
        {DP_FAULT, 0x09, NULL},         {DP_WAKE_METHOD, 2, NULL},    {DP_PC_HOSTNAME, 0, "pc \"1\"\\\n"},
        {DP_AGENT_ONLINE, 1, NULL},     {DP_LAST_RESULT, LR_OK, NULL},
    };
    int n = tyl_build_report(p, 8, "m1", 1759536000123LL, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL(n, strlen(buf));
    cJSON *j = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(j, buf);
    TEST_ASSERT_EQUAL_STRING("m1", cJSON_GetObjectItem(j, "msgId")->valuestring);
    TEST_ASSERT_TRUE(cJSON_GetObjectItem(j, "time")->valuedouble == 1759536000123.0);
    cJSON *d = cJSON_GetObjectItem(j, "data");
    TEST_ASSERT_EQUAL(8, cJSON_GetArraySize(d));
#define VAL(code) cJSON_GetObjectItem(cJSON_GetObjectItem(d, code), "value")
    TEST_ASSERT_EQUAL_STRING("on", VAL("pc_state")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsFalse(VAL("power_on")));
    TEST_ASSERT_EQUAL(123, VAL("cpu_usage")->valueint);
    TEST_ASSERT_EQUAL(9, VAL("fault")->valueint); /* bitmap as an integer mask */
    TEST_ASSERT_EQUAL_STRING("hid_then_wol", VAL("wake_method")->valuestring);
    TEST_ASSERT_EQUAL_STRING("pc \"1\"\\\n", VAL("pc_hostname")->valuestring); /* escaped round trip */
    TEST_ASSERT_TRUE(cJSON_IsTrue(VAL("agent_online")));
    TEST_ASSERT_EQUAL_STRING("ok", VAL("last_result")->valuestring);
    TEST_ASSERT_TRUE(cJSON_GetObjectItem(cJSON_GetObjectItem(d, "cpu_usage"), "time")->valuedouble == 1759536000123.0);
#undef VAL
    cJSON_Delete(j);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"cpu_usage\":{\"value\":123,\"time\":1759536000123}"));

    /* errors: unknown DP, invalid enum value, buffer too small, nothing to report */
    tyl_prop_t bad = {115, 0, NULL};
    TEST_ASSERT_EQUAL(-1, tyl_build_report(&bad, 1, "m", 1, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("", buf);
    tyl_prop_t badenum = {DP_PC_STATE, 9, NULL};
    TEST_ASSERT_EQUAL(-1, tyl_build_report(&badenum, 1, "m", 1, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(-1, tyl_build_report(p, 8, "m1", 1, buf, 40));
    TEST_ASSERT_EQUAL(-1, tyl_build_report(p, 0, "m1", 1, buf, sizeof(buf)));
}

static void test_report_all_fits(void)
{
    /* every DP with its widest value (64-char hostname) fits the firmware buffer */
    char host[DPM_STR_MAX + 1];
    memset(host, 'h', DPM_STR_MAX);
    host[DPM_STR_MAX] = 0;
    tyl_prop_t p[DPM_COUNT];
    for (int i = 0; i < DPM_COUNT; i++) {
        const dpm_desc_t *d = dpm_desc_at(i);
        p[i] = (tyl_prop_t){d->id, d->type == DPT_ENUM ? 0 : d->max, host};
    }
    char buf[1536];
    int n = tyl_build_report(p, DPM_COUNT, "0123456789abcdef", 1759536000123LL, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0 && n < 1200);
    cJSON *j = cJSON_Parse(buf);
    TEST_ASSERT_EQUAL(DPM_COUNT, cJSON_GetArraySize(cJSON_GetObjectItem(j, "data")));
    cJSON_Delete(j);
}

static void test_build_response_and_model_get(void)
{
    char buf[128];
    TEST_ASSERT_TRUE(tyl_build_response("abc", 1700000000000LL, 0, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"msgId\":\"abc\",\"time\":1700000000000,\"code\":0}", buf);
    TEST_ASSERT_TRUE(tyl_build_response("a\"b", 5, TYL_CODE_FAIL, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"msgId\":\"a\\\"b\",\"time\":5,\"code\":1}", buf);
    TEST_ASSERT_EQUAL(-1, tyl_build_response("abc", 5, 0, buf, 10));
    TEST_ASSERT_TRUE(tyl_build_model_get("q1", 7, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"msgId\":\"q1\",\"time\":7,\"data\":{\"format\":\"complex\"}}", buf);
}

static int parse(const char *s, tyl_set_t *o) { return tyl_parse_set(s, strlen(s), o); }

static void test_parse_set_single_and_multiple(void)
{
    tyl_set_t o;
    TEST_ASSERT_EQUAL(0, parse("{\"msgId\":\"45lkj3551234001\",\"time\":1626197189638,\"data\":{\"power_on\":true}}", &o));
    TEST_ASSERT_EQUAL_STRING("45lkj3551234001", o.msgid);
    TEST_ASSERT_EQUAL(1, o.nwrites);
    TEST_ASSERT_EQUAL(0, o.nrejected);
    TEST_ASSERT_EQUAL(DP_POWER_ON, o.w[0].id);
    TEST_ASSERT_EQUAL(1, o.w[0].v);

    /* real EU downlink captured 2026-10-04: msgId is a JSON number */
    TEST_ASSERT_EQUAL(0, parse("{\"data\":{\"power_on\":true},\"msgId\":3,\"time\":1791068546}", &o));
    TEST_ASSERT_EQUAL_STRING("3", o.msgid);
    TEST_ASSERT_EQUAL(1, o.nwrites);
    TEST_ASSERT_EQUAL(DP_POWER_ON, o.w[0].id);
    TEST_ASSERT_EQUAL(-1, parse("{\"data\":{\"power_on\":true},\"msgId\":-1}", &o));
    TEST_ASSERT_EQUAL(-1, parse("{\"data\":{\"power_on\":true},\"msgId\":1.5}", &o));

    TEST_ASSERT_EQUAL(0, parse("{\"msgId\":\"m\",\"time\":1,\"data\":{\"wake_method\":\"wol\",\"cmd_countdown\":30,"
                               "\"reboot\":false,\"power_off\":true}}",
                               &o));
    TEST_ASSERT_EQUAL(4, o.nwrites);
    TEST_ASSERT_EQUAL(0, o.nrejected);
    TEST_ASSERT_EQUAL(DP_WAKE_METHOD, o.w[0].id);
    TEST_ASSERT_EQUAL(WM_WOL, o.w[0].v);
    TEST_ASSERT_EQUAL(DP_CMD_COUNTDOWN, o.w[1].id);
    TEST_ASSERT_EQUAL(30, o.w[1].v);
    TEST_ASSERT_EQUAL(DP_REBOOT, o.w[2].id);
    TEST_ASSERT_EQUAL(0, o.w[2].v);
    TEST_ASSERT_EQUAL(DP_POWER_OFF, o.w[3].id);
    TEST_ASSERT_EQUAL(1, o.w[3].v);
    /* empty data: valid envelope, nothing to do */
    TEST_ASSERT_EQUAL(0, parse("{\"msgId\":\"m\",\"data\":{}}", &o));
    TEST_ASSERT_EQUAL(0, o.nwrites + o.nrejected);
    /* not NUL-terminated input: only len bytes are parsed */
    const char *s = "{\"msgId\":\"m\",\"data\":{\"power_on\":true}}GARBAGE";
    TEST_ASSERT_EQUAL(0, tyl_parse_set(s, strlen(s) - 7, &o));
    TEST_ASSERT_EQUAL(1, o.nwrites);
}

static void test_parse_set_rejections(void)
{
    tyl_set_t o;
    /* unknown code + read-only + valid one: the valid one is kept */
    TEST_ASSERT_EQUAL(0, parse("{\"msgId\":\"m\",\"data\":{\"vbus\":1,\"cpu_usage\":5,\"power_on\":true}}", &o));
    TEST_ASSERT_EQUAL(1, o.nwrites);
    TEST_ASSERT_EQUAL(DP_POWER_ON, o.w[0].id);
    TEST_ASSERT_EQUAL(2, o.nrejected);
    TEST_ASSERT_EQUAL(TYL_W_UNKNOWN_CODE, o.first_reject);
    TEST_ASSERT_EQUAL_STRING("vbus", o.first_reject_code);
    TEST_ASSERT_EQUAL_STRING("unknown_code", tyl_wres_name(o.first_reject));

    struct {
        const char *json;
        tyl_wres_t why;
    } bad[] = {
        {"{\"msgId\":\"m\",\"data\":{\"pc_state\":\"on\"}}", TYL_W_READ_ONLY},
        {"{\"msgId\":\"m\",\"data\":{\"fault\":1}}", TYL_W_READ_ONLY},
        {"{\"msgId\":\"m\",\"data\":{\"power_on\":1}}", TYL_W_BAD_TYPE},          /* bool expected */
        {"{\"msgId\":\"m\",\"data\":{\"power_on\":\"true\"}}", TYL_W_BAD_TYPE},
        {"{\"msgId\":\"m\",\"data\":{\"power_on\":null}}", TYL_W_BAD_TYPE},
        {"{\"msgId\":\"m\",\"data\":{\"cmd_countdown\":\"10\"}}", TYL_W_BAD_TYPE},
        {"{\"msgId\":\"m\",\"data\":{\"cmd_countdown\":10.5}}", TYL_W_BAD_TYPE},
        {"{\"msgId\":\"m\",\"data\":{\"cmd_countdown\":61}}", TYL_W_OUT_OF_RANGE},
        {"{\"msgId\":\"m\",\"data\":{\"cmd_countdown\":-1}}", TYL_W_OUT_OF_RANGE},
        {"{\"msgId\":\"m\",\"data\":{\"cmd_countdown\":1e300}}", TYL_W_OUT_OF_RANGE},
        {"{\"msgId\":\"m\",\"data\":{\"wake_method\":1}}", TYL_W_BAD_TYPE},       /* enum is a string */
        {"{\"msgId\":\"m\",\"data\":{\"wake_method\":\"usb\"}}", TYL_W_OUT_OF_RANGE},
        {"{\"msgId\":\"m\",\"data\":{\"wake_method\":{\"value\":\"wol\"}}}", TYL_W_BAD_TYPE},
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        TEST_ASSERT_EQUAL_MESSAGE(0, parse(bad[i].json, &o), bad[i].json);
        TEST_ASSERT_EQUAL_MESSAGE(0, o.nwrites, bad[i].json);
        TEST_ASSERT_EQUAL_MESSAGE(1, o.nrejected, bad[i].json);
        TEST_ASSERT_EQUAL_MESSAGE(bad[i].why, o.first_reject, bad[i].json);
    }
    /* boundaries accepted */
    TEST_ASSERT_EQUAL(0, parse("{\"msgId\":\"m\",\"data\":{\"cmd_countdown\":0,\"cmd_countdown\":60}}", &o));
    TEST_ASSERT_EQUAL(2, o.nwrites);
    TEST_ASSERT_EQUAL(0, o.w[0].v);
    TEST_ASSERT_EQUAL(60, o.w[1].v);
    /* more properties than DPs: the extra ones are rejected, never overflow */
    char big[2048] = "{\"msgId\":\"m\",\"data\":{";
    for (int i = 0; i < 20; i++) strcat(big, i ? ",\"power_on\":true" : "\"power_on\":true");
    strcat(big, "}}");
    TEST_ASSERT_EQUAL(0, parse(big, &o));
    TEST_ASSERT_EQUAL(TYL_MAX_WRITES, o.nwrites);
    TEST_ASSERT_EQUAL(20 - TYL_MAX_WRITES, o.nrejected);
    TEST_ASSERT_EQUAL(TYL_W_TOO_MANY, o.first_reject);
}

static void test_parse_set_malformed(void)
{
    tyl_set_t o;
    const char *bad[] = {
        "",
        "not json",
        "[]",
        "{\"data\":{\"power_on\":true}}",                                     /* no msgId */
        "{\"msgId\":\"\",\"data\":{\"power_on\":true}}",                       /* empty msgId */
        "{\"msgId\":true,\"data\":{\"power_on\":true}}",                         /* msgId neither string nor integer */
        "{\"msgId\":\"0123456789abcdef0123456789abcdef0\",\"data\":{}}",        /* 33 chars */
        "{\"msgId\":\"m\"}",                                                  /* no data */
        "{\"msgId\":\"m\",\"data\":[1,2]}",                                   /* data not an object */
        "{\"msgId\":\"m\",\"data\":{\"power_on\":true}",                       /* truncated */
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        TEST_ASSERT_EQUAL_MESSAGE(-1, parse(bad[i], &o), bad[i]);
        TEST_ASSERT_EQUAL(0, o.nwrites);
        TEST_ASSERT_EQUAL_STRING("", o.msgid);
    }
    TEST_ASSERT_EQUAL(-1, tyl_parse_set(NULL, 5, &o));
    /* exactly 32 chars is fine */
    TEST_ASSERT_EQUAL(0, parse("{\"msgId\":\"0123456789abcdef0123456789abcdef\",\"data\":{}}", &o));
}

static void test_parse_msgid_and_code(void)
{
    char m[TYL_MSGID_MAX + 1];
    const char *ae = "{\"msgId\":\"a1\",\"time\":1,\"data\":{\"actionCode\":\"x\",\"inputParams\":{}}}";
    TEST_ASSERT_EQUAL(0, tyl_parse_msgid(ae, strlen(ae), m));
    TEST_ASSERT_EQUAL_STRING("a1", m);
    TEST_ASSERT_EQUAL(-1, tyl_parse_msgid("{}", 2, m));
    TEST_ASSERT_EQUAL_STRING("", m);
    int code = 99;
    const char *r = "{\"msgId\":\"x\",\"time\":1,\"code\":0}";
    TEST_ASSERT_EQUAL(0, tyl_parse_code(r, strlen(r), &code));
    TEST_ASSERT_EQUAL(0, code);
    const char *r2 = "{\"code\":2001,\"msg\":\"invalid\"}";
    TEST_ASSERT_EQUAL(0, tyl_parse_code(r2, strlen(r2), &code));
    TEST_ASSERT_EQUAL(2001, code);
    TEST_ASSERT_EQUAL(-1, tyl_parse_code("{\"code\":\"0\"}", 12, &code));
    TEST_ASSERT_EQUAL(-1, tyl_parse_code("x", 1, &code));
}

static void test_parse_set_fuzz(void)
{
    /* every truncation of a valid message is either rejected or a valid subset */
    const char *s = "{\"msgId\":\"m1\",\"time\":1,\"data\":{\"power_on\":true,\"wake_method\":\"wol\",\"cmd_countdown\":5}}";
    tyl_set_t o;
    for (size_t n = 1; n < strlen(s); n++) {
        int rc = tyl_parse_set(s, n, &o);
        TEST_ASSERT_TRUE(rc == -1 || rc == 0);
        TEST_ASSERT_TRUE(o.nwrites <= 3);
    }
    TEST_ASSERT_EQUAL(0, tyl_parse_set(s, strlen(s), &o));
    TEST_ASSERT_EQUAL(3, o.nwrites);
}

void run_tylink_tests(void)
{
    RUN_TEST(test_auth_vectors);
    RUN_TEST(test_validation_and_hosts);
    RUN_TEST(test_topics);
    RUN_TEST(test_enum_names_match_core);
    RUN_TEST(test_build_report);
    RUN_TEST(test_report_all_fits);
    RUN_TEST(test_build_response_and_model_get);
    RUN_TEST(test_parse_set_single_and_multiple);
    RUN_TEST(test_parse_set_rejections);
    RUN_TEST(test_parse_set_malformed);
    RUN_TEST(test_parse_msgid_and_code);
    RUN_TEST(test_parse_set_fuzz);
}
