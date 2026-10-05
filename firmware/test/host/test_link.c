#include <stdio.h>
#include <string.h>

#include "helpers.h"
#include "unity.h"

static uint8_t K[32];
static harness_t H;

static char *vmsg(int idx) /* vectors.messages.valid[idx] serialised (caller frees) */
{
    cJSON *v = cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetObjectItem(vectors(), "messages"), "valid"), idx);
    TEST_ASSERT_NOT_NULL(v);
    return cJSON_PrintUnformatted(v);
}

static cJSON *vmsg_obj(int idx)
{
    return cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetObjectItem(vectors(), "messages"), "valid"), idx);
}

static void rx_vmsg(int idx, uint32_t now)
{
    char *s = vmsg(idx);
    h_rx(&H, s, now);
    cJSON_free(s);
}

static void assert_err(const char *code)
{
    char c[40];
    TEST_ASSERT_EQUAL_STRING("err", h_last_t(&H, c, sizeof(c)));
    TEST_ASSERT_EQUAL_STRING(code, c);
}

/* JSON objects equal on the fields of `want` */
static void assert_fields(const cJSON *want, const char *got_line)
{
    cJSON *g = cJSON_Parse(got_line);
    TEST_ASSERT_NOT_NULL_MESSAGE(g, got_line);
    const cJSON *w;
    cJSON_ArrayForEach(w, want)
    {
        cJSON *x = cJSON_GetObjectItem(g, w->string);
        TEST_ASSERT_NOT_NULL_MESSAGE(x, w->string);
        TEST_ASSERT_TRUE_MESSAGE(cJSON_Compare(w, x, 1), w->string);
    }
    cJSON_Delete(g);
}

static void session_up(void)
{
    hex2bin(vstr("session", "key_hex"), K, 32);
    h_init(&H, K);
    h_push_random_hex(&H, vstr("session", "dongle_nonce"));
    rx_vmsg(0, 1000); /* hello */
    rx_vmsg(2, 1100); /* auth */
}

static void test_handshake_vectors(void)
{
    hex2bin(vstr("session", "key_hex"), K, 32);
    h_init(&H, K);
    h_push_random_hex(&H, vstr("session", "dongle_nonce"));
    rx_vmsg(0, 1000);
    TEST_ASSERT_EQUAL(1, H.nout);
    assert_fields(vmsg_obj(1), H.out[0]); /* welcome: v, fw, dev, nonce, sig */
    TEST_ASSERT_FALSE(link_ready(&H.l));
    TEST_ASSERT_EQUAL(0, h_count_ev(&H, LINK_EV_HELLO)); /* hello data only trusted after auth */
    rx_vmsg(2, 1100);
    TEST_ASSERT_EQUAL_STRING("{\"t\":\"ready\"}", h_last(&H));
    TEST_ASSERT_TRUE(link_ready(&H.l));
    TEST_ASSERT_TRUE(link_online(&H.l));
    const link_ev_t *e = h_find_ev(&H, LINK_EV_HELLO);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_STRING("thinkstation", e->hello.host);
    TEST_ASSERT_EQUAL(1, e->hello.nmacs);
    const uint8_t mac[6] = {0xfc, 0x9d, 0x05, 0x18, 0xee, 0x32};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(mac, e->hello.macs[0], 6);
    TEST_ASSERT_EQUAL(1, h_count_ev(&H, LINK_EV_READY));
    e = h_find_ev(&H, LINK_EV_ONLINE);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_TRUE(e->online);
}

static void test_cmd_signing_vectors_and_ack(void)
{
    session_up();
    h_clear(&H);
    uint32_t id = link_send_cmd(&H.l, "shutdown", 2000);
    TEST_ASSERT_EQUAL_UINT32(1, id);
    assert_fields(vmsg_obj(7), h_last(&H)); /* cmd id 1 shutdown + vector sig */
    TEST_ASSERT_EQUAL_UINT32(0, link_send_cmd(&H.l, "reboot", 2001)); /* one pending at a time */
    rx_vmsg(8, 2500); /* ack id 1 ok */
    const link_ev_t *e = h_find_ev(&H, LINK_EV_ACK);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT32(1, e->ack.id);
    TEST_ASSERT_TRUE(e->ack.ok);
    /* second cmd: id 2 reboot, vector sig */
    id = link_send_cmd(&H.l, "reboot", 3000);
    TEST_ASSERT_EQUAL_UINT32(2, id);
    cJSON *c2 = cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetObjectItem(vectors(), "session"), "cmds"), 1);
    cJSON *g = cJSON_Parse(h_last(&H));
    TEST_ASSERT_EQUAL_STRING(cJSON_GetObjectItem(c2, "sig")->valuestring, cJSON_GetObjectItem(g, "sig")->valuestring);
    TEST_ASSERT_EQUAL_STRING("reboot", cJSON_GetObjectItem(g, "action")->valuestring);
    cJSON_Delete(g);
    /* ack !ok bad_sig for id 2 (vector) */
    h_clear(&H);
    rx_vmsg(9, 3500);
    e = h_find_ev(&H, LINK_EV_ACK);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_FALSE(e->ack.ok);
    TEST_ASSERT_EQUAL_STRING("bad_sig", e->ack.err);
    TEST_ASSERT_EQUAL(0, link_send_cmd(&H.l, "format_disk", 4000));
}

static void test_ack_timeout_and_stale_ack(void)
{
    session_up();
    uint32_t id = link_send_cmd(&H.l, "reboot", 5000);
    TEST_ASSERT_EQUAL_UINT32(1, id);
    h_clear(&H);
    h_rx(&H, "{\"t\":\"hb\"}", 9000);
    h_rx(&H, "{\"t\":\"ack\",\"id\":7,\"ok\":true}", 9001); /* stale id: ignored */
    TEST_ASSERT_EQUAL(0, h_count_ev(&H, LINK_EV_ACK));
    TEST_ASSERT_EQUAL(0, H.nout);
    link_tick(&H.l, 14999);
    TEST_ASSERT_EQUAL(0, h_count_ev(&H, LINK_EV_ACK_TIMEOUT));
    link_tick(&H.l, 15000);
    TEST_ASSERT_EQUAL(1, h_count_ev(&H, LINK_EV_ACK_TIMEOUT));
    /* late ack after the timeout is ignored */
    h_rx(&H, "{\"t\":\"ack\",\"id\":1,\"ok\":true}", 15100);
    TEST_ASSERT_EQUAL(0, h_count_ev(&H, LINK_EV_ACK));
}

/* Regression: a session dropped with a cmd pending (agent restarted / port closed /
 * re-hello) used to clear cmd_pending silently, so no ack nor timeout ever reached the
 * power flow (stuck in WAIT_ACK: every later shutdown/reboot was "busy"). */
static void test_pending_cmd_resolved_on_session_drop(void)
{
    session_up();
    TEST_ASSERT_EQUAL_UINT32(1, link_send_cmd(&H.l, "shutdown", 5000));
    h_clear(&H);
    link_close_session(&H.l);
    TEST_ASSERT_EQUAL(1, h_count_ev(&H, LINK_EV_ACK_TIMEOUT));
    const link_ev_t *e = h_find_ev(&H, LINK_EV_ACK_TIMEOUT);
    TEST_ASSERT_EQUAL_UINT32(1, e->ack.id);
    TEST_ASSERT_FALSE(e->ack.ok);
    /* no second timeout later */
    link_tick(&H.l, 20000);
    TEST_ASSERT_EQUAL(1, h_count_ev(&H, LINK_EV_ACK_TIMEOUT));
    /* without a pending cmd, dropping emits nothing */
    session_up();
    h_clear(&H);
    link_close_session(&H.l);
    TEST_ASSERT_EQUAL(0, h_count_ev(&H, LINK_EV_ACK_TIMEOUT));
    /* re-hello with a cmd pending also resolves it */
    session_up();
    TEST_ASSERT_EQUAL_UINT32(1, link_send_cmd(&H.l, "reboot", 5000));
    h_clear(&H);
    h_push_random_hex(&H, vstr("session", "dongle_nonce"));
    rx_vmsg(0, 6000); /* hello again (agent restarted) */
    TEST_ASSERT_EQUAL(1, h_count_ev(&H, LINK_EV_ACK_TIMEOUT));
}

static void test_tele_maps_and_validates(void)
{
    session_up();
    h_clear(&H);
    rx_vmsg(4, 2000);
    const link_ev_t *e = h_find_ev(&H, LINK_EV_TELE);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL(123, e->tele.cpu);
    TEST_ASSERT_EQUAL(456, e->tele.mem);
    TEST_ASSERT_EQUAL(789, e->tele.disk_free);
    TEST_ASSERT_EQUAL_UINT32(3600, e->tele.uptime);
    TEST_ASSERT_EQUAL(0, H.nout);
    h_rx(&H, "{\"t\":\"tele\",\"seq\":2,\"cpu\":-1,\"mem\":0,\"disk_free\":0,\"uptime\":0}", 2100);
    assert_err("bad_msg");
    h_rx(&H, "{\"t\":\"tele\",\"seq\":2,\"cpu\":1.5,\"mem\":0,\"disk_free\":0,\"uptime\":0}", 2100);
    assert_err("bad_msg");
    h_rx(&H, "{\"t\":\"tele\",\"seq\":2,\"cpu\":1,\"mem\":0,\"disk_free\":0}", 2100);
    assert_err("bad_msg");
    h_rx(&H, "{\"t\":\"tele\",\"seq\":4294967296,\"cpu\":1,\"mem\":0,\"disk_free\":0,\"uptime\":0}", 2100);
    assert_err("bad_msg");
    h_rx(&H, "{\"t\":\"tele\",\"seq\":4294967295,\"cpu\":1000,\"mem\":0,\"disk_free\":0,\"uptime\":4294967295,\"x\":[1]}",
         2100);
    TEST_ASSERT_EQUAL(2, h_count_ev(&H, LINK_EV_TELE)); /* unknown fields ignored */
}

static void test_unauth_before_ready(void)
{
    hex2bin(vstr("session", "key_hex"), K, 32);
    h_init(&H, K);
    rx_vmsg(4, 100);
    assert_err("unauth");
    rx_vmsg(5, 100);
    assert_err("unauth");
    rx_vmsg(8, 100);
    assert_err("unauth");
    rx_vmsg(15, 100); /* scripts before ready */
    assert_err("unauth");
    TEST_ASSERT_EQUAL(0, h_count_ev(&H, LINK_EV_SCRIPTS));
    TEST_ASSERT_FALSE(H.l.scripts_known);
    rx_vmsg(2, 100); /* auth without hello */
    assert_err("unauth");
    TEST_ASSERT_EQUAL(0, h_count_ev(&H, LINK_EV_TELE));
    TEST_ASSERT_FALSE(link_online(&H.l));
    TEST_ASSERT_EQUAL(0, link_send_cmd(&H.l, "reboot", 100));
    TEST_ASSERT_FALSE(link_send_notice(&H.l, "reboot", 10));
}

static void test_wrong_auth_and_wrong_key(void)
{
    hex2bin(vstr("session", "key_hex"), K, 32);
    h_init(&H, K);
    h_push_random_hex(&H, vstr("session", "dongle_nonce"));
    rx_vmsg(0, 100);
    h_rx(&H, "{\"t\":\"auth\",\"sig\":\"0000000000000000000000000000000000000000000000000000000000000000\"}", 200);
    assert_err("unauth");
    TEST_ASSERT_FALSE(link_ready(&H.l));
    rx_vmsg(2, 300); /* correct sig but session already reset */
    assert_err("unauth");
    /* other key: welcome sig differs from vector */
    uint8_t k2[32] = {1};
    h_init(&H, k2);
    h_push_random_hex(&H, vstr("session", "dongle_nonce"));
    rx_vmsg(0, 100);
    cJSON *g = cJSON_Parse(h_last(&H));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(vstr("session", "welcome_sig"), cJSON_GetObjectItem(g, "sig")->valuestring));
    cJSON_Delete(g);
    rx_vmsg(2, 200);
    assert_err("unauth");
}

static void test_hello_errors(void)
{
    h_init(&H, NULL);
    rx_vmsg(0, 100);
    assert_err("not_paired");
    hex2bin(vstr("session", "key_hex"), K, 32);
    h_init(&H, K);
    h_rx(&H, "{\"t\":\"hello\",\"v\":2,\"nonce\":\"a1b2c3d4e5f60718\"}", 1);
    assert_err("unsupported_version");
    h_rx(&H, "{\"t\":\"hello\",\"host\":\"h\",\"os\":\"linux\",\"agent_ver\":\"1\",\"macs\":[],\"nonce\":\"a1b2c3d4e5f60718\"}",
         1);
    assert_err("bad_msg"); /* missing v */
    h_rx(&H, "{\"t\":\"hello\",\"v\":1,\"host\":\"h\",\"os\":\"macos\",\"agent_ver\":\"1\",\"macs\":[],\"nonce\":\"a1b2c3d4e5f60718\"}",
         1);
    assert_err("bad_msg");
    h_rx(&H, "{\"t\":\"hello\",\"v\":1,\"host\":\"h\",\"os\":\"linux\",\"agent_ver\":\"1\",\"macs\":[\"FC:9D:05:18:EE:32\"],"
             "\"nonce\":\"a1b2c3d4e5f60718\"}",
         1);
    assert_err("bad_msg"); /* uppercase MAC */
    h_rx(&H, "{\"t\":\"hello\",\"v\":1,\"host\":\"h\",\"os\":\"linux\",\"agent_ver\":\"1\",\"macs\":[\"aa:bb:cc:dd:ee:01\","
             "\"aa:bb:cc:dd:ee:02\",\"aa:bb:cc:dd:ee:03\",\"aa:bb:cc:dd:ee:04\",\"aa:bb:cc:dd:ee:05\"],"
             "\"nonce\":\"a1b2c3d4e5f60718\"}",
         1);
    assert_err("bad_msg"); /* > 4 macs */
    char host[100], line[300];
    memset(host, 'h', 65);
    host[65] = 0;
    snprintf(line, sizeof(line),
             "{\"t\":\"hello\",\"v\":1,\"host\":\"%s\",\"os\":\"linux\",\"agent_ver\":\"1\",\"macs\":[],\"nonce\":"
             "\"a1b2c3d4e5f60718\"}",
             host);
    h_rx(&H, line, 1);
    assert_err("bad_msg"); /* host > 64 */
    host[64] = 0;
    snprintf(line, sizeof(line),
             "{\"t\":\"hello\",\"v\":1,\"host\":\"%s\",\"os\":\"windows\",\"agent_ver\":\"1\",\"macs\":[],\"nonce\":"
             "\"a1b2c3d4e5f60718\"}",
             host);
    h_rx(&H, line, 1);
    TEST_ASSERT_EQUAL_STRING("welcome", h_last_t(&H, NULL, 0));
}

static void test_online_timeout_and_rehello(void)
{
    session_up(); /* ready at t=1100 */
    h_clear(&H);
    link_tick(&H.l, 1100 + 14999);
    TEST_ASSERT_TRUE(link_online(&H.l));
    link_tick(&H.l, 1100 + 15000);
    TEST_ASSERT_FALSE(link_online(&H.l));
    TEST_ASSERT_TRUE(link_ready(&H.l)); /* session kept: a later message revives it */
    h_rx(&H, "{\"t\":\"hb\"}", 20000);
    TEST_ASSERT_TRUE(link_online(&H.l));
    TEST_ASSERT_EQUAL(2, h_count_ev(&H, LINK_EV_ONLINE));
    /* a new hello resets the session and drops agent_online until auth */
    h_push_random_hex(&H, vstr("session", "dongle_nonce"));
    rx_vmsg(0, 21000);
    TEST_ASSERT_FALSE(link_online(&H.l));
    TEST_ASSERT_FALSE(link_ready(&H.l));
    rx_vmsg(2, 21100);
    TEST_ASSERT_TRUE(link_ready(&H.l));
    /* cmd ids restart at 1 per session */
    TEST_ASSERT_EQUAL_UINT32(1, link_send_cmd(&H.l, "shutdown", 21200));
}

static void test_pairing_vectors(void)
{
    h_init(&H, NULL);
    link_pair_start(&H.l, vstr("pairing", "code"), 0);
    h_push_random_hex(&H, vstr("pairing", "dongle_nonce"));
    rx_vmsg(11, 10); /* pair */
    assert_fields(vmsg_obj(12), h_last(&H)); /* pair_chal nonce */
    rx_vmsg(13, 20); /* pair_confirm */
    assert_fields(vmsg_obj(14), h_last(&H)); /* pair_ok sig */
    TEST_ASSERT_TRUE(H.saved);
    uint8_t want[32];
    hex2bin(vstr("pairing", "key_hex"), want, 32);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, H.saved_key, 32);
    TEST_ASSERT_TRUE(H.l.has_key);
    TEST_ASSERT_FALSE(link_pairing(&H.l));
    TEST_ASSERT_EQUAL(1, h_count_ev(&H, LINK_EV_PAIRED));
    TEST_ASSERT_EQUAL(1, h_count_ev(&H, LINK_EV_PAIR_END));
    /* the new key works for a session (session vectors use the same key) */
    h_push_random_hex(&H, vstr("session", "dongle_nonce"));
    rx_vmsg(0, 100);
    rx_vmsg(2, 110);
    TEST_ASSERT_TRUE(link_ready(&H.l));
}

static void test_pairing_three_failures(void)
{
    h_init(&H, NULL);
    link_pair_start(&H.l, "111111", 0); /* agent will use the vector code 482913 -> wrong */
    for (int i = 1; i <= 3; i++) {
        h_push_random_hex(&H, vstr("pairing", "dongle_nonce"));
        H.rnd_i = H.nrnd - 1;
        rx_vmsg(11, i * 10);
        TEST_ASSERT_EQUAL_STRING("pair_chal", h_last_t(&H, NULL, 0));
        rx_vmsg(13, i * 10 + 1);
        assert_err("pair_failed");
        TEST_ASSERT_EQUAL(i, h_count_ev(&H, LINK_EV_PAIR_FAIL));
    }
    TEST_ASSERT_FALSE(link_pairing(&H.l));
    TEST_ASSERT_EQUAL(1, h_count_ev(&H, LINK_EV_PAIR_END));
    TEST_ASSERT_FALSE(H.saved);
    rx_vmsg(11, 100);
    assert_err("pair_failed"); /* no longer in pairing mode */
    rx_vmsg(13, 101);
    assert_err("pair_failed");
}

static void test_pairing_window_and_replace(void)
{
    hex2bin(vstr("session", "key_hex"), K, 32);
    session_up();
    TEST_ASSERT_TRUE(link_ready(&H.l));
    link_pair_start(&H.l, vstr("pairing", "code"), 5000);
    TEST_ASSERT_EQUAL_UINT32(LINK_PAIR_WINDOW, link_pair_remaining_ms(&H.l, 5000));
    link_tick(&H.l, 5000 + LINK_PAIR_WINDOW - 1);
    TEST_ASSERT_TRUE(link_pairing(&H.l));
    link_tick(&H.l, 5000 + LINK_PAIR_WINDOW);
    TEST_ASSERT_FALSE(link_pairing(&H.l));
    /* re-pair while a session is up: old session dropped, key replaced */
    link_pair_start(&H.l, "000000", 200000);
    H.nrnd = 0;
    H.rnd_i = 0;
    h_push_random_hex(&H, "0f1e2d3c4b5a6978");
    h_rx(&H, "{\"t\":\"pair\",\"v\":1,\"nonce\":\"a1b2c3d4e5f60718\"}", 200001);
    uint8_t k[32];
    char sig[65], line[160];
    TEST_ASSERT_EQUAL(0, mc_derive_pair_key("000000", "a1b2c3d4e5f60718", "0f1e2d3c4b5a6978", k));
    mc_sign(k, "pair|a1b2c3d4e5f60718|0f1e2d3c4b5a6978", sig);
    snprintf(line, sizeof(line), "{\"t\":\"pair_confirm\",\"sig\":\"%s\"}", sig);
    h_rx(&H, line, 200002);
    TEST_ASSERT_EQUAL_STRING("pair_ok", h_last_t(&H, NULL, 0));
    TEST_ASSERT_FALSE(link_ready(&H.l));
    TEST_ASSERT_FALSE(link_online(&H.l));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(k, H.l.key, 32);
    /* old key no longer authenticates */
    h_push_random_hex(&H, vstr("session", "dongle_nonce"));
    rx_vmsg(0, 200100);
    rx_vmsg(2, 200110);
    assert_err("unauth");
}

static void test_pair_confirm_without_chal_and_save_fail(void)
{
    h_init(&H, NULL);
    link_pair_start(&H.l, vstr("pairing", "code"), 0);
    rx_vmsg(13, 1);
    assert_err("pair_failed");
    H.save_fail = true;
    h_push_random_hex(&H, vstr("pairing", "dongle_nonce"));
    rx_vmsg(11, 2);
    rx_vmsg(13, 3);
    assert_err("pair_failed"); /* could not persist -> not paired */
    TEST_ASSERT_FALSE(H.l.has_key);
}

static void test_notice_and_forget(void)
{
    session_up();
    h_clear(&H);
    TEST_ASSERT_TRUE(link_send_notice(&H.l, "shutdown", 10));
    assert_fields(vmsg_obj(6), h_last(&H));
    TEST_ASSERT_TRUE(link_send_notice(&H.l, "cancel", 0));
    TEST_ASSERT_EQUAL_STRING("{\"t\":\"notice\",\"action\":\"cancel\",\"in\":0}", h_last(&H));
    link_forget_key(&H.l);
    TEST_ASSERT_FALSE(link_ready(&H.l));
    rx_vmsg(0, 5000);
    assert_err("not_paired");
}

static void test_invalid_vectors(void)
{
    session_up(); /* READY, so range checks (not unauth) apply */
    cJSON *inv = cJSON_GetObjectItem(cJSON_GetObjectItem(vectors(), "messages"), "invalid");
    const char *expect[] = {"bad_msg", "bad_msg", "bad_msg", "unsupported_version", "bad_msg", "too_long", "bad_msg",
                            /* scripts: no list, > 5, bad id, id > 12, empty label, label > 24, quote, dup */
                            "bad_msg", "bad_msg", "bad_msg", "bad_msg", "bad_msg", "bad_msg", "bad_msg", "bad_msg"};
    int i = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, inv)
    {
        TEST_ASSERT_TRUE(i < (int)(sizeof(expect) / sizeof(expect[0])));
        const char *line = cJSON_GetObjectItem(it, "line")->valuestring;
        h_clear(&H);
        h_rx(&H, line, 3000);
        TEST_ASSERT_EQUAL_MESSAGE(1, H.nout, cJSON_GetObjectItem(it, "why")->valuestring);
        assert_err(expect[i]);
        TEST_ASSERT_EQUAL(0, h_count_ev(&H, LINK_EV_TELE));
        TEST_ASSERT_EQUAL(0, h_count_ev(&H, LINK_EV_SCRIPTS));
        i++;
    }
    TEST_ASSERT_EQUAL(15, i);
    TEST_ASSERT_TRUE(link_ready(&H.l)); /* garbage does not kill the session */
}

static void test_dongle_types_from_agent_rejected(void)
{
    session_up();
    int idx[] = {1, 3, 6, 7, 10, 12, 14, 17}; /* welcome ready notice cmd err pair_chal pair_ok cmd(script) */
    for (size_t i = 0; i < sizeof(idx) / sizeof(idx[0]); i++) {
        h_clear(&H);
        rx_vmsg(idx[i], 3000);
        assert_err("bad_msg");
    }
}

static void test_close_session(void)
{
    session_up();
    TEST_ASSERT_TRUE(link_online(&H.l));
    link_close_session(&H.l);
    TEST_ASSERT_FALSE(link_ready(&H.l));
    TEST_ASSERT_FALSE(link_online(&H.l));
    h_clear(&H);
    rx_vmsg(4, 5000); /* tele after the port was closed: must re-authenticate */
    assert_err("unauth");
    TEST_ASSERT_EQUAL(0, link_send_cmd(&H.l, "reboot", 5000));
    TEST_ASSERT_TRUE(H.l.has_key); /* key kept */
}

/* ------------------------------------------------------------------ scripts (§3.1) */
static const char *scripts_json(void)
{
    static char b[LINK_SCRIPTS_JSON_MAX + 1];
    TEST_ASSERT_TRUE(link_scripts_json(&H.l, b, sizeof(b)) >= 0);
    return b;
}

static void test_scripts_vectors_and_script_cmd_sig(void)
{
    session_up();
    TEST_ASSERT_FALSE(H.l.scripts_known);
    h_clear(&H);
    rx_vmsg(15, 1200); /* scripts backup + docker-up */
    TEST_ASSERT_EQUAL(0, H.nout); /* no reply on success */
    TEST_ASSERT_EQUAL(1, h_count_ev(&H, LINK_EV_SCRIPTS));
    TEST_ASSERT_TRUE(H.l.scripts_known);
    TEST_ASSERT_EQUAL(2, H.l.nscripts);
    TEST_ASSERT_EQUAL_STRING("[[\"backup\",\"Backup NAS\"],[\"docker-up\",\"Docker up\"]]", scripts_json());
    TEST_ASSERT_NOT_NULL(link_script_find(&H.l, "docker-up"));
    TEST_ASSERT_NULL(link_script_find(&H.l, "nope"));
    /* ids 1 and 2 (power), then the script vector: id 3 script:backup */
    TEST_ASSERT_EQUAL_UINT32(1, link_send_cmd(&H.l, "shutdown", 2000));
    rx_vmsg(8, 2100);
    TEST_ASSERT_EQUAL_UINT32(2, link_send_cmd(&H.l, "reboot", 2200));
    rx_vmsg(9, 2300);
    TEST_ASSERT_EQUAL_UINT32(0, link_send_cmd(&H.l, "script:nope", 2400));   /* not in the list */
    TEST_ASSERT_EQUAL_UINT32(0, link_send_cmd(&H.l, "script:", 2400));
    TEST_ASSERT_EQUAL_UINT32(0, link_send_cmd(&H.l, "script:Backup", 2400));
    TEST_ASSERT_EQUAL_UINT32(0, link_send_cmd(&H.l, "backup", 2400));
    TEST_ASSERT_EQUAL_UINT32(3, link_send_cmd(&H.l, "script:backup", 2500));
    assert_fields(vmsg_obj(17), h_last(&H)); /* id 3, action, vector sig */
    cJSON *c3 = cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetObjectItem(vectors(), "session"), "cmds"), 2);
    cJSON *g = cJSON_Parse(h_last(&H));
    TEST_ASSERT_EQUAL_STRING(cJSON_GetObjectItem(c3, "sig")->valuestring, cJSON_GetObjectItem(g, "sig")->valuestring);
    cJSON_Delete(g);
    TEST_ASSERT_TRUE(link_cmd_pending(&H.l, LINK_CMD_SCRIPT));
    TEST_ASSERT_EQUAL_UINT32(0, link_send_cmd(&H.l, "script:docker-up", 2600)); /* one script cmd at a time */
    h_clear(&H);
    h_rx(&H, "{\"t\":\"ack\",\"id\":3,\"ok\":true}", 2700);
    const link_ev_t *e = h_find_ev(&H, LINK_EV_ACK);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT32(3, e->ack.id);
    TEST_ASSERT_FALSE(link_cmd_pending(&H.l, LINK_CMD_SCRIPT));
    /* empty list vector: valid, replaces the list */
    h_clear(&H);
    rx_vmsg(16, 3000);
    TEST_ASSERT_EQUAL(0, H.nout);
    TEST_ASSERT_EQUAL(1, h_count_ev(&H, LINK_EV_SCRIPTS));
    TEST_ASSERT_EQUAL(0, H.l.nscripts);
    TEST_ASSERT_TRUE(H.l.scripts_known);
    TEST_ASSERT_EQUAL_STRING("[]", scripts_json());
    TEST_ASSERT_EQUAL_UINT32(0, link_send_cmd(&H.l, "script:backup", 3100));
}

/* A script cmd and a power cmd can await their acks at the same time: a script run
 * never blocks a shutdown at the end of its countdown. */
static void test_script_and_power_cmds_independent(void)
{
    session_up();
    rx_vmsg(15, 1200);
    TEST_ASSERT_EQUAL_UINT32(1, link_send_cmd(&H.l, "script:backup", 2000));
    TEST_ASSERT_EQUAL_UINT32(2, link_send_cmd(&H.l, "shutdown", 2500));
    TEST_ASSERT_TRUE(link_cmd_pending(&H.l, LINK_CMD_POWER));
    TEST_ASSERT_TRUE(link_cmd_pending(&H.l, LINK_CMD_SCRIPT));
    h_clear(&H);
    h_rx(&H, "{\"t\":\"ack\",\"id\":2,\"ok\":true}", 2600); /* acks in any order */
    TEST_ASSERT_EQUAL_UINT32(2, h_find_ev(&H, LINK_EV_ACK)->ack.id);
    TEST_ASSERT_FALSE(link_cmd_pending(&H.l, LINK_CMD_POWER));
    TEST_ASSERT_TRUE(link_cmd_pending(&H.l, LINK_CMD_SCRIPT));
    /* the script cmd times out on its own clock (sent at 2000) */
    h_rx(&H, "{\"t\":\"hb\"}", 11000);
    link_tick(&H.l, 11999);
    TEST_ASSERT_EQUAL(0, h_count_ev(&H, LINK_EV_ACK_TIMEOUT));
    link_tick(&H.l, 12000);
    TEST_ASSERT_EQUAL(1, h_count_ev(&H, LINK_EV_ACK_TIMEOUT));
    TEST_ASSERT_EQUAL_UINT32(1, h_find_ev(&H, LINK_EV_ACK_TIMEOUT)->ack.id);
    /* both pending when the session drops: both resolved; the list is kept */
    TEST_ASSERT_EQUAL_UINT32(3, link_send_cmd(&H.l, "script:docker-up", 13000));
    TEST_ASSERT_EQUAL_UINT32(4, link_send_cmd(&H.l, "reboot", 13000));
    h_clear(&H);
    link_close_session(&H.l);
    TEST_ASSERT_EQUAL(2, h_count_ev(&H, LINK_EV_ACK_TIMEOUT));
    TEST_ASSERT_EQUAL(2, H.l.nscripts);
    TEST_ASSERT_TRUE(H.l.scripts_known);
    TEST_ASSERT_EQUAL_UINT32(0, link_send_cmd(&H.l, "script:backup", 14000)); /* no session */
}

static void test_scripts_invalid_keeps_previous_list(void)
{
    session_up();
    rx_vmsg(15, 1200);
    const char *bad[] = {
        "{\"t\":\"scripts\",\"list\":{}}",
        "{\"t\":\"scripts\",\"list\":[\"backup\"]}",
        "{\"t\":\"scripts\",\"list\":[{\"id\":\"a\"}]}",
        "{\"t\":\"scripts\",\"list\":[{\"label\":\"x\"}]}",
        "{\"t\":\"scripts\",\"list\":[{\"id\":1,\"label\":\"x\"}]}",
        "{\"t\":\"scripts\",\"list\":[{\"id\":\"a\",\"label\":7}]}",
        "{\"t\":\"scripts\",\"list\":[{\"id\":\"\",\"label\":\"x\"}]}",
        "{\"t\":\"scripts\",\"list\":[{\"id\":\"a.b\",\"label\":\"x\"}]}",
        "{\"t\":\"scripts\",\"list\":[{\"id\":\"a\",\"label\":\"back\\\\slash\"}]}",
        "{\"t\":\"scripts\",\"list\":[{\"id\":\"a\",\"label\":\"tab\\there\"}]}",
        "{\"t\":\"scripts\",\"list\":[{\"id\":\"a\",\"label\":\"del\x7f\"}]}",
        "{\"t\":\"scripts\",\"list\":[{\"id\":\"a\",\"label\":\"c1\\u0085\"}]}",
        "{\"t\":\"scripts\",\"list\":[{\"id\":\"a\",\"label\":\"q\\u0022\"}]}",
        "{\"t\":\"scripts\",\"list\":[{\"id\":\"a\",\"label\":\"bad\xc3\"}]}",
        "{\"t\":\"scripts\",\"list\":[{\"id\":\"a\",\"label\":\"overlong\xc0\xaf\"}]}",
        "{\"t\":\"scripts\",\"list\":[{\"id\":\"a\",\"label\":\"surrogate\xed\xa0\x80\"}]}",
        "{\"t\":\"scripts\",\"list\":[{\"id\":\"a\",\"label\":\"\xc3\xb1\xc3\xb1\xc3\xb1\xc3\xb1\xc3\xb1\xc3\xb1\xc3\xb1"
        "\xc3\xb1\xc3\xb1\xc3\xb1\xc3\xb1\xc3\xb1x\"}]}", /* 25 bytes */
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        h_clear(&H);
        h_rx(&H, bad[i], 2000);
        TEST_ASSERT_EQUAL_MESSAGE(1, H.nout, bad[i]);
        assert_err("bad_msg");
        TEST_ASSERT_EQUAL_MESSAGE(0, h_count_ev(&H, LINK_EV_SCRIPTS), bad[i]);
        TEST_ASSERT_EQUAL_MESSAGE(2, H.l.nscripts, bad[i]);
    }
    TEST_ASSERT_EQUAL_STRING("[[\"backup\",\"Backup NAS\"],[\"docker-up\",\"Docker up\"]]", scripts_json());
    TEST_ASSERT_TRUE(link_ready(&H.l));
    /* accepted: 24 bytes of UTF-8, extra fields ignored, ids at 12 chars */
    h_clear(&H);
    h_rx(&H,
         "{\"t\":\"scripts\",\"list\":[{\"id\":\"abcdefghijk_\",\"label\":\"\xc3\xb1\xc3\xb1\xc3\xb1\xc3\xb1\xc3\xb1\xc3"
         "\xb1\xc3\xb1\xc3\xb1\xc3\xb1\xc3\xb1\xc3\xb1\xc3\xb1\",\"x\":1},{\"id\":\"0-9\",\"label\":\"Caf\xc3\xa9 "
         "\xe2\x9c\x93 \xf0\x9f\x9a\x80\"}],\"extra\":true}",
         2500);
    TEST_ASSERT_EQUAL(0, H.nout);
    TEST_ASSERT_EQUAL(2, H.l.nscripts);
    TEST_ASSERT_EQUAL_STRING("abcdefghijk_", H.l.scripts[0].id);
}

static void test_scripts_worst_case_line_and_json(void)
{
    session_up();
    char line[600];
    int o = snprintf(line, sizeof(line), "{\"t\":\"scripts\",\"list\":[");
    for (int i = 0; i < LINK_MAX_SCRIPTS; i++)
        o += snprintf(line + o, sizeof(line) - o, "%s{\"id\":\"%012d\",\"label\":\"%024d\"}", i ? "," : "", i, i);
    o += snprintf(line + o, sizeof(line) - o, "]}");
    TEST_ASSERT_TRUE(o + 1 <= LINK_MAX_LINE); /* "< 512 bytes" (cdc-v1 §3.1) */
    h_clear(&H);
    h_rx(&H, line, 2000);
    TEST_ASSERT_EQUAL(0, H.nout);
    TEST_ASSERT_EQUAL(LINK_MAX_SCRIPTS, H.l.nscripts);
    char b[LINK_SCRIPTS_JSON_MAX + 1];
    TEST_ASSERT_EQUAL(221, LINK_SCRIPTS_JSON_MAX);
    TEST_ASSERT_EQUAL(LINK_SCRIPTS_JSON_MAX, link_scripts_json(&H.l, b, sizeof(b)));
    TEST_ASSERT_EQUAL(-1, link_scripts_json(&H.l, b, sizeof(b) - 1));
    TEST_ASSERT_EQUAL_STRING("", b);
    cJSON *j = cJSON_Parse(scripts_json());
    TEST_ASSERT_EQUAL(LINK_MAX_SCRIPTS, cJSON_GetArraySize(j));
    cJSON_Delete(j);
    /* every action fits: script:<12 chars> */
    TEST_ASSERT_EQUAL_UINT32(1, link_send_cmd(&H.l, "script:000000000004", 2100));
    cJSON *g = cJSON_Parse(h_last(&H));
    TEST_ASSERT_EQUAL_STRING("script:000000000004", cJSON_GetObjectItem(g, "action")->valuestring);
    cJSON_Delete(g);
}

static void test_mac_parse(void)
{
    uint8_t m[6];
    TEST_ASSERT_EQUAL(0, link_parse_mac("fc:9d:05:18:ee:32", m));
    TEST_ASSERT_EQUAL_HEX8(0x32, m[5]);
    TEST_ASSERT_NOT_EQUAL(0, link_parse_mac("fc:9d:05:18:ee", m));
    TEST_ASSERT_NOT_EQUAL(0, link_parse_mac("fc-9d-05-18-ee-32", m));
    TEST_ASSERT_NOT_EQUAL(0, link_parse_mac("fc:9d:05:18:ee:3g", m));
    TEST_ASSERT_NOT_EQUAL(0, link_parse_mac(NULL, m));
}

void run_link_tests(void)
{
    RUN_TEST(test_handshake_vectors);
    RUN_TEST(test_cmd_signing_vectors_and_ack);
    RUN_TEST(test_ack_timeout_and_stale_ack);
    RUN_TEST(test_pending_cmd_resolved_on_session_drop);
    RUN_TEST(test_tele_maps_and_validates);
    RUN_TEST(test_unauth_before_ready);
    RUN_TEST(test_wrong_auth_and_wrong_key);
    RUN_TEST(test_hello_errors);
    RUN_TEST(test_online_timeout_and_rehello);
    RUN_TEST(test_pairing_vectors);
    RUN_TEST(test_pairing_three_failures);
    RUN_TEST(test_pairing_window_and_replace);
    RUN_TEST(test_pair_confirm_without_chal_and_save_fail);
    RUN_TEST(test_notice_and_forget);
    RUN_TEST(test_invalid_vectors);
    RUN_TEST(test_dongle_types_from_agent_rejected);
    RUN_TEST(test_close_session);
    RUN_TEST(test_scripts_vectors_and_script_cmd_sig);
    RUN_TEST(test_script_and_power_cmds_independent);
    RUN_TEST(test_scripts_invalid_keeps_previous_list);
    RUN_TEST(test_scripts_worst_case_line_and_json);
    RUN_TEST(test_mac_parse);
}
