/*
 * MicroESP — display (US-0017/US-0018, basic version; polish pending).
 *
 * ST7735 160x80 landscape through the HAL (esp_lcd SPI + DMA, 20-line internal
 * buffer), LVGL v9 from TuyaOpen in its own thread ("mesp_ui"). The app task only
 * publishes a snapshot (ui_snap_t) under a mutex; LVGL objects are only touched by
 * the UI thread.
 *
 * Screens: status (pc_state, host, Wi-Fi/cloud/agent), telemetry bars (CPU/MEM/disk
 * free), countdown (automatic while counting), agent pairing code (automatic while
 * pairing). Short press cycles status/telemetry; with the agent online they also
 * rotate every 5 s. A one-line toast shows transient messages.
 */
#include <stdio.h>
#include <string.h>

#include "mesp_board.h"
#include "mesp_hal.h"
#include "modules.h"
#include "tal_api.h"

#if defined(ENABLE_LIBLVGL) && (ENABLE_LIBLVGL == 1)
#include "lvgl.h"
#define HAVE_LVGL 1
#else
#define HAVE_LVGL 0
#endif

typedef struct {
    int pc_state;
    bool agent, cloud, wifi, provisioned, ota;
    char host[24];
    int cpu, mem, disk;
    int countdown;
    char action[10];
    bool pairing;
    char code[7];
    int pair_left;
    int screen;
    char toast[32];
    char fw[16];
    int last_result;
    uint32_t faults;
} ui_snap_t;

static MUTEX_HANDLE s_mx;
static ui_snap_t s_pub;
static volatile uint32_t s_pub_ver;
static ui_snap_t s_last;
static uint32_t s_rotate_ms;
static bool s_ok;

void display_next_screen(void)
{
    g_app.ui_screen = (g_app.ui_screen + 1) % 2;
    s_rotate_ms = app_now_ms();
}

/* ------------------------------------------------------------------ app task side */
void display_tick(uint32_t now)
{
    if (!s_ok) return;
    if (link_online(&g_app.link) && now - s_rotate_ms >= 5000) display_next_screen();
    ui_snap_t s;
    memset(&s, 0, sizeof(s));
    s.pc_state = g_app.pcs.state;
    s.agent = link_online(&g_app.link);
    s.cloud = g_app.cloud_connected;
    s.wifi = g_app.wifi_up;
    s.provisioned = g_app.cloud_provisioned;
    s.ota = g_app.ota_running;
    snprintf(s.host, sizeof(s.host), "%.23s", g_app.hostname[0] ? g_app.hostname : "-");
    s.cpu = g_app.cpu;
    s.mem = g_app.mem;
    s.disk = g_app.disk_free;
    s.countdown = pwr_remaining_s(&g_app.pwr, now);
    snprintf(s.action, sizeof(s.action), "%s", pwr_action_name(g_app.pwr.action));
    s.pairing = pairing_active();
    snprintf(s.code, sizeof(s.code), "%s", pairing_code());
    s.pair_left = pairing_remaining_s();
    s.screen = g_app.ui_screen;
    if ((int32_t)(g_app.toast_until - now) > 0) snprintf(s.toast, sizeof(s.toast), "%s", g_app.toast);
    snprintf(s.fw, sizeof(s.fw), "%s", MESP_FW_VERSION);
    s.last_result = g_app.have_last_result ? (int)g_app.last_result : -1;
    s.faults = g_app.faults;
    if (!memcmp(&s, &s_last, sizeof(s))) return;
    s_last = s;
    tal_mutex_lock(s_mx);
    s_pub = s;
    s_pub_ver++;
    tal_mutex_unlock(s_mx);
}

#if HAVE_LVGL
/* ------------------------------------------------------------------ UI thread */
#if defined(LV_FONT_MONTSERRAT_28) && LV_FONT_MONTSERRAT_28
#define FONT_BIG (&lv_font_montserrat_28)
#else
#define FONT_BIG (&lv_font_montserrat_16)
#endif
#if defined(LV_FONT_MONTSERRAT_12) && LV_FONT_MONTSERRAT_12
#define FONT_SMALL (&lv_font_montserrat_12)
#else
#define FONT_SMALL (&lv_font_montserrat_14)
#endif
#define FONT_MID (&lv_font_montserrat_16)

static THREAD_HANDLE s_thr;
static lv_obj_t *s_scr[4]; /* status, tele, countdown, pairing */
static lv_obj_t *l_state, *l_host, *l_icons, *l_fw;
static lv_obj_t *b_bar[3], *l_bar[3];
static lv_obj_t *l_cd_num, *l_cd_act, *l_cd_hint;
static lv_obj_t *l_pair_code, *l_pair_left, *l_pair_title;
static lv_obj_t *l_toast;

static uint32_t tick_cb(void) { return (uint32_t)tal_system_get_millisecond(); }

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    uint32_t n = (uint32_t)lv_area_get_width(a) * (uint32_t)lv_area_get_height(a);
    lv_draw_sw_rgb565_swap(px, n);
    mhal_lcd_draw(a->x1, a->y1, a->x2, a->y2, px);
    lv_display_flush_ready(d);
}

static lv_obj_t *mk_screen(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, MESP_LCD_W, MESP_LCD_H);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}

static lv_obj_t *mk_label(lv_obj_t *p, const lv_font_t *f, lv_color_t c, lv_align_t al, int x, int y)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, "");
    lv_obj_align(l, al, x, y);
    return l;
}

static void build(void)
{
    lv_obj_t *root = lv_screen_active();
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 4; i++) s_scr[i] = mk_screen(root);
    lv_color_t white = lv_color_white(), grey = lv_color_hex(0x9e9e9e), cyan = lv_color_hex(0x40c4ff);
    /* status */
    l_state = mk_label(s_scr[0], FONT_MID, white, LV_ALIGN_TOP_MID, 0, 4);
    l_host = mk_label(s_scr[0], FONT_SMALL, cyan, LV_ALIGN_TOP_MID, 0, 28);
    l_icons = mk_label(s_scr[0], FONT_SMALL, grey, LV_ALIGN_BOTTOM_LEFT, 4, -4);
    l_fw = mk_label(s_scr[0], FONT_SMALL, grey, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
    /* telemetry */
    static const char *names[3] = {"CPU", "MEM", "DISK"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *n = mk_label(s_scr[1], FONT_SMALL, white, LV_ALIGN_TOP_LEFT, 4, 6 + i * 25);
        lv_label_set_text(n, names[i]);
        b_bar[i] = lv_bar_create(s_scr[1]);
        lv_obj_set_size(b_bar[i], 80, 12);
        lv_obj_align(b_bar[i], LV_ALIGN_TOP_LEFT, 40, 8 + i * 25);
        lv_bar_set_range(b_bar[i], 0, 1000);
        lv_obj_set_style_bg_color(b_bar[i], lv_color_hex(0x303030), LV_PART_MAIN);
        lv_obj_set_style_bg_color(b_bar[i], i == 2 ? lv_color_hex(0x00c853) : lv_color_hex(0x40c4ff),
                                  LV_PART_INDICATOR);
        l_bar[i] = mk_label(s_scr[1], FONT_SMALL, white, LV_ALIGN_TOP_RIGHT, -2, 6 + i * 25);
    }
    /* countdown */
    l_cd_act = mk_label(s_scr[2], FONT_SMALL, lv_color_hex(0xffab40), LV_ALIGN_TOP_MID, 0, 2);
    l_cd_num = mk_label(s_scr[2], FONT_BIG, lv_color_hex(0xff5252), LV_ALIGN_CENTER, 0, 0);
    l_cd_hint = mk_label(s_scr[2], FONT_SMALL, grey, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_label_set_text(l_cd_hint, "Pulsa para cancelar");
    /* pairing */
    l_pair_title = mk_label(s_scr[3], FONT_SMALL, cyan, LV_ALIGN_TOP_MID, 0, 2);
    lv_label_set_text(l_pair_title, "Codigo agente");
    l_pair_code = mk_label(s_scr[3], FONT_BIG, white, LV_ALIGN_CENTER, 0, 2);
    l_pair_left = mk_label(s_scr[3], FONT_SMALL, grey, LV_ALIGN_BOTTOM_MID, 0, -2);
    /* toast (top layer) */
    l_toast = lv_label_create(lv_layer_top());
    lv_obj_set_style_text_font(l_toast, FONT_SMALL, 0);
    lv_obj_set_style_text_color(l_toast, lv_color_black(), 0);
    lv_obj_set_style_bg_color(l_toast, lv_color_hex(0xffd740), 0);
    lv_obj_set_style_bg_opa(l_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(l_toast, 4, 0);
    lv_obj_align(l_toast, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(l_toast, LV_OBJ_FLAG_HIDDEN);
}

static const char *state_text(int s)
{
    static const char *t[] = {"PC apagado", "PC suspendido", "Arrancando", "Sin agente", "PC encendido", "Desconocido"};
    return (unsigned)s < 6 ? t[s] : "?";
}

static void apply(const ui_snap_t *s)
{
    char b[48];
    int show = s->countdown > 0 ? 2 : s->pairing ? 3 : s->screen;
    for (int i = 0; i < 4; i++) {
        if (i == show) lv_obj_remove_flag(s_scr[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_scr[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(l_state, state_text(s->pc_state));
    lv_label_set_text(l_host, s->provisioned ? s->host : "Nube: !wifi/!tylink");
    snprintf(b, sizeof(b), LV_SYMBOL_WIFI "%s " LV_SYMBOL_UPLOAD "%s " LV_SYMBOL_USB "%s", s->wifi ? "" : "x",
             s->cloud ? "" : "x", s->agent ? "" : "x");
    lv_label_set_text(l_icons, b);
    lv_label_set_text(l_fw, s->fw);
    const int v[3] = {s->cpu, s->mem, s->disk};
    for (int i = 0; i < 3; i++) {
        lv_bar_set_value(b_bar[i], v[i] < 0 ? 0 : v[i], LV_ANIM_OFF);
        if (v[i] < 0) snprintf(b, sizeof(b), "--");
        else snprintf(b, sizeof(b), "%d%%", (v[i] + 5) / 10);
        lv_label_set_text(l_bar[i], b);
    }
    snprintf(b, sizeof(b), "%s en", !strcmp(s->action, "shutdown") ? "Apagado" : "Reinicio");
    lv_label_set_text(l_cd_act, b);
    snprintf(b, sizeof(b), "%d", s->countdown);
    lv_label_set_text(l_cd_num, b);
    lv_label_set_text(l_pair_code, s->code);
    snprintf(b, sizeof(b), "%d s", s->pair_left);
    lv_label_set_text(l_pair_left, b);
    if (s->toast[0]) {
        lv_label_set_text(l_toast, s->toast);
        lv_obj_remove_flag(l_toast, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(l_toast, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ui_thread(void *arg)
{
    lv_init();
    lv_tick_set_cb(tick_cb);
    size_t bytes = 0;
    void *buf = mhal_lcd_buf(&bytes);
    lv_display_t *d = lv_display_create(MESP_LCD_W, MESP_LCD_H);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, buf, NULL, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);
    build();
    lv_timer_handler();
    mhal_lcd_backlight(true);
    uint32_t seen = 0;
    ui_snap_t s;
    for (;;) {
        if (seen != s_pub_ver) {
            tal_mutex_lock(s_mx);
            s = s_pub;
            seen = s_pub_ver;
            tal_mutex_unlock(s_mx);
            apply(&s);
        }
        lv_timer_handler();
        tal_system_sleep(40);
    }
}
#endif

void display_init(void)
{
    if (mhal_lcd_init() != 0) {
        PR_ERR("display: LCD init failed (display disabled)");
        return;
    }
#if HAVE_LVGL
    tal_mutex_create_init(&s_mx);
    THREAD_CFG_T cfg = {.stackDepth = 8192, .priority = THREAD_PRIO_3, .thrdname = "mesp_ui"};
    if (tal_thread_create_and_start(&s_thr, NULL, NULL, ui_thread, NULL, &cfg) == OPRT_OK) s_ok = true;
    PR_NOTICE("display: LVGL UI %s", s_ok ? "started" : "thread failed");
#else
    PR_WARN("display: built without LVGL (CONFIG_ENABLE_LIBLVGL)");
#endif
}
