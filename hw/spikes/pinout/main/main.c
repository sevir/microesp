/*
 * MicroESP - Pinout spike for Pocket-Dongle-S3 (clone of LilyGO T-Dongle-S3)
 *
 * 1. Bit-banged 3-wire read of ST7735 ID/status registers through the SDA (MOSI)
 *    line -> software verification of MOSI/SCLK/CS/DC/RST wiring.
 * 2. esp_lcd SPI init + test pattern (border, corner markers, color bars, text).
 * 3. Repeating probe cycle shown on screen AND in the serial log:
 *      backlight polarity, WS2812 on candidate GPIOs, APA102 on 40/39.
 * 4. BOOT button (GPIO0) press logging + on-screen counter.
 * 5. TF card SDMMC 4-bit mount attempt.
 */
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "esp_mac.h"
#include "esp_rom_sys.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "led_strip.h"
#include "font5x7.h"

static const char *TAG = "pinout";

/* Reference T-Dongle-S3 pins (under test) */
#define PIN_LCD_MOSI 3
#define PIN_LCD_SCLK 5
#define PIN_LCD_CS   4
#define PIN_LCD_DC   2
#define PIN_LCD_RST  1
#define PIN_LCD_BL   38
#define PIN_BTN      0

/* Panel geometry: native 80x160 portrait inside 132x162 RAM.
 * We drive it in landscape (MV=1): 160 wide x 80 high. */
#define LCD_W 160
#define LCD_H 80
#define LCD_X_OFF 1   /* column offset in landscape (= row offset portrait) */
#define LCD_Y_OFF 26  /* row offset in landscape (= col offset portrait) */
#define LCD_MADCTL 0x68 /* MX | MV | BGR */
#define LCD_INVERT 1

static uint16_t *fb;
static esp_lcd_panel_io_handle_t io;
static volatile int btn_count;
static char status_line[32] = "BOOT";

/* ---------------------------------------------------------------- bit-bang */
static void bb_delay(void) { esp_rom_delay_us(2); }

/* Generic 3-wire read used by the permutation scan: returns raw bits. */
static uint64_t g_read(int mosi, int sclk, int cs, int dc, uint8_t cmd, int nbits, bool pulldown)
{
    gpio_config_t o = {.pin_bit_mask = BIT64(sclk) | BIT64(cs) | BIT64(dc), .mode = GPIO_MODE_OUTPUT};
    gpio_config(&o);
    gpio_config_t m = {.pin_bit_mask = BIT64(mosi), .mode = GPIO_MODE_INPUT_OUTPUT,
                       .pull_up_en = !pulldown, .pull_down_en = pulldown};
    gpio_config(&m);
    gpio_set_level(cs, 1);
    gpio_set_level(sclk, 0);
    esp_rom_delay_us(5);
    gpio_set_level(cs, 0);
    gpio_set_level(dc, 0);
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(sclk, 0);
        gpio_set_level(mosi, (cmd >> i) & 1);
        bb_delay();
        gpio_set_level(sclk, 1);
        bb_delay();
    }
    gpio_set_level(sclk, 0);
    gpio_set_direction(mosi, GPIO_MODE_INPUT);
    gpio_set_level(dc, 1);
    uint64_t v = 0;
    for (int i = 0; i < nbits; i++) {
        gpio_set_level(sclk, 0);
        bb_delay();
        gpio_set_level(sclk, 1);
        bb_delay();
        v = (v << 1) | (gpio_get_level(mosi) & 1);
    }
    gpio_set_level(sclk, 0);
    gpio_set_level(cs, 1);
    return v;
}

static void lcd_permutation_scan(void)
{
    const int cand[] = {1, 2, 3, 4, 5};
    const int n = 5;
    int hits = 0;
    ESP_LOGI(TAG, "SCAN: RDDID over all (MOSI,SCLK,CS,DC) permutations of GPIO 1..5 (others high)");
    for (int a = 0; a < n; a++) for (int b = 0; b < n; b++) for (int c = 0; c < n; c++) for (int d = 0; d < n; d++) {
        if (a == b || a == c || a == d || b == c || b == d || c == d) continue;
        int mosi = cand[a], sclk = cand[b], cs = cand[c], dc = cand[d];
        for (int k = 0; k < n; k++) {
            int p = cand[k];
            if (p != mosi && p != sclk && p != cs && p != dc) {
                gpio_reset_pin(p);
                gpio_set_direction(p, GPIO_MODE_OUTPUT);
                gpio_set_level(p, 1); /* would be RST: keep released */
            }
        }
        uint64_t up = g_read(mosi, sclk, cs, dc, 0x04, 25, false);
        uint64_t dn = g_read(mosi, sclk, cs, dc, 0x04, 25, true);
        if (up == dn) {
            hits++;
            ESP_LOGW(TAG, "SCAN HIT: MOSI=%d SCLK=%d CS=%d DC=%d -> RDDID raw25=0x%07llx (line actively driven)",
                     mosi, sclk, cs, dc, up);
        }
    }
    for (int k = 0; k < n; k++) gpio_reset_pin(cand[k]);
    ESP_LOGW(TAG, "SCAN done: %d permutations with a driven SDA line", hits);
}

static void bb_write8(uint8_t v)
{
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(PIN_LCD_SCLK, 0);
        gpio_set_level(PIN_LCD_MOSI, (v >> i) & 1);
        bb_delay();
        gpio_set_level(PIN_LCD_SCLK, 1);
        bb_delay();
    }
    gpio_set_level(PIN_LCD_SCLK, 0);
}

static void bb_cmd(uint8_t cmd, const uint8_t *data, int len)
{
    gpio_set_direction(PIN_LCD_MOSI, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_LCD_CS, 0);
    gpio_set_level(PIN_LCD_DC, 0);
    bb_write8(cmd);
    gpio_set_level(PIN_LCD_DC, 1);
    for (int i = 0; i < len; i++) bb_write8(data[i]);
    gpio_set_level(PIN_LCD_CS, 1);
}

/* Send command then clock in nbits from SDA (no dummy handling: raw). */
static uint64_t bb_read(uint8_t cmd, int nbits)
{
    uint64_t v = 0;
    gpio_set_direction(PIN_LCD_MOSI, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_LCD_CS, 0);
    gpio_set_level(PIN_LCD_DC, 0);
    bb_write8(cmd);
    gpio_set_direction(PIN_LCD_MOSI, GPIO_MODE_INPUT);
    gpio_set_level(PIN_LCD_DC, 1);
    for (int i = 0; i < nbits; i++) {
        gpio_set_level(PIN_LCD_SCLK, 0);
        bb_delay();
        gpio_set_level(PIN_LCD_SCLK, 1);
        bb_delay();
        v = (v << 1) | (gpio_get_level(PIN_LCD_MOSI) & 1);
    }
    gpio_set_level(PIN_LCD_SCLK, 0);
    gpio_set_level(PIN_LCD_CS, 1);
    gpio_set_direction(PIN_LCD_MOSI, GPIO_MODE_OUTPUT);
    return v;
}

static void lcd_probe_bitbang(void)
{
    gpio_config_t o = {
        .pin_bit_mask = BIT64(PIN_LCD_CS) | BIT64(PIN_LCD_DC) | BIT64(PIN_LCD_SCLK) | BIT64(PIN_LCD_RST),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&o);
    gpio_config_t m = {.pin_bit_mask = BIT64(PIN_LCD_MOSI), .mode = GPIO_MODE_INPUT_OUTPUT,
                       .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&m);
    gpio_set_level(PIN_LCD_CS, 1);
    gpio_set_level(PIN_LCD_SCLK, 0);

    /* Hardware reset through RST pin */
    gpio_set_level(PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));

    /* RDDID 0x04: 1 dummy clock + 24 bits. Read 25 bits raw. */
    uint64_t id = bb_read(0x04, 25);
    uint32_t id24 = id & 0xFFFFFF;
    ESP_LOGI(TAG, "PROBE RDDID(0x04) raw25=0x%07llx -> ID1..3 = %02lx %02lx %02lx (ST7735S expects 7C 89 F0)",
             id, (id24 >> 16) & 0xff, (id24 >> 8) & 0xff, id24 & 0xff);
    uint64_t st = bb_read(0x09, 33);
    ESP_LOGI(TAG, "PROBE RDDST(0x09) raw33=0x%09llx -> status=0x%08lx", st, (uint32_t)(st & 0xFFFFFFFF));

    /* MADCTL readback (8-bit regs: try raw 9 bits to see whether a dummy bit is present) */
    bb_cmd(0x11, NULL, 0); /* SLPOUT */
    vTaskDelay(pdMS_TO_TICKS(150));
    uint8_t mad = 0xA8;
    bb_cmd(0x36, &mad, 1);
    uint64_t r1 = bb_read(0x0B, 9);
    ESP_LOGI(TAG, "PROBE wrote MADCTL=0xA8, RDDMADCTL raw9=0x%03llx (with-dummy=0x%02llx no-dummy=0x%02llx)",
             r1, r1 & 0xff, (r1 >> 1) & 0xff);
    uint8_t colmod = 0x05;
    bb_cmd(0x3A, &colmod, 1);
    uint64_t c1 = bb_read(0x0C, 9);
    ESP_LOGI(TAG, "PROBE wrote COLMOD=0x05, RDDCOLMOD raw9=0x%03llx (with-dummy=0x%02llx no-dummy=0x%02llx)",
             c1, c1 & 0xff, (c1 >> 1) & 0xff);

    /* RST verification: pulse RST, MADCTL must return to its reset value 0x00 */
    gpio_set_level(PIN_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
    uint64_t r2 = bb_read(0x0B, 9);
    ESP_LOGI(TAG, "PROBE after RST pulse RDDMADCTL raw9=0x%03llx (expect 0 if GPIO%d is RST)", r2, PIN_LCD_RST);

    bool id_ok = (id24 != 0 && id24 != 0xFFFFFF);
    bool mad_ok = ((r1 & 0xff) == 0xA8) || (((r1 >> 1) & 0xff) == 0xA8);
    bool rst_ok = mad_ok && ((r2 & 0x1fe) == 0) && ((r2 & 0xff) == 0);
    ESP_LOGW(TAG, "PROBE RESULT: SPI read-back %s, MADCTL write/read %s, RST pin %s",
             id_ok ? "OK" : "FAIL", mad_ok ? "OK" : "FAIL", rst_ok ? "OK" : "FAIL/unknown");
    gpio_reset_pin(PIN_LCD_MOSI);
    gpio_reset_pin(PIN_LCD_SCLK);
    gpio_reset_pin(PIN_LCD_CS);
    gpio_reset_pin(PIN_LCD_DC);
}

/* ---------------------------------------------------------------- esp_lcd */
static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    return (c >> 8) | (c << 8); /* big-endian for SPI */
}

static void fill(int x, int y, int w, int h, uint16_t c)
{
    for (int j = y; j < y + h && j < LCD_H; j++)
        for (int i = x; i < x + w && i < LCD_W; i++)
            if (i >= 0 && j >= 0) fb[j * LCD_W + i] = c;
}

static void text(int x, int y, const char *s, uint16_t c, int scale)
{
    for (; *s; s++, x += 6 * scale) {
        int ch = *s;
        if (ch < 32 || ch > 126) ch = '?';
        const uint8_t *g = font5x7[ch - 32];
        for (int r = 0; r < 7; r++)
            for (int b = 0; b < 5; b++)
                if (g[r] & (0x10 >> b)) fill(x + b * scale, y + r * scale, scale, scale, c);
    }
}

static void lcd_flush(void)
{
    uint8_t ca[4] = {0, LCD_X_OFF, 0, LCD_X_OFF + LCD_W - 1};
    uint8_t ra[4] = {0, LCD_Y_OFF, 0, LCD_Y_OFF + LCD_H - 1};
    esp_lcd_panel_io_tx_param(io, 0x2A, ca, 4);
    esp_lcd_panel_io_tx_param(io, 0x2B, ra, 4);
    esp_lcd_panel_io_tx_color(io, 0x2C, fb, LCD_W * LCD_H * 2);
}

static void draw_screen(void)
{
    const uint16_t black = rgb(0, 0, 0), white = rgb(255, 255, 255);
    fill(0, 0, LCD_W, LCD_H, black);
    /* 1px white frame on the outermost pixels: all 4 edges must be visible */
    fill(0, 0, LCD_W, 1, white);
    fill(0, LCD_H - 1, LCD_W, 1, white);
    fill(0, 0, 1, LCD_H, white);
    fill(LCD_W - 1, 0, 1, LCD_H, white);
    /* corner markers: TL red, TR green, BL blue, BR yellow */
    fill(2, 2, 6, 6, rgb(255, 0, 0));
    fill(LCD_W - 8, 2, 6, 6, rgb(0, 255, 0));
    fill(2, LCD_H - 8, 6, 6, rgb(0, 0, 255));
    fill(LCD_W - 8, LCD_H - 8, 6, 6, rgb(255, 255, 0));
    /* title */
    text(32, 4, "MicroESP", white, 2);
    /* color bars with labels R G B W */
    const uint16_t bars[4] = {rgb(255, 0, 0), rgb(0, 255, 0), rgb(0, 0, 255), white};
    const char *lbl[4] = {"R", "G", "B", "W"};
    for (int i = 0; i < 4; i++) {
        fill(12 + i * 35, 22, 32, 24, bars[i]);
        text(12 + i * 35 + 13, 48, lbl[i], white, 1);
    }
    char line[40];
    snprintf(line, sizeof(line), "%s", status_line);
    text(12, 58, line, rgb(0, 255, 255), 1);
    snprintf(line, sizeof(line), "BTN:%d", btn_count);
    text(12, 68, line, rgb(255, 0, 255), 1);
    lcd_flush();
}

static void lcd_init(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = PIN_LCD_MOSI, .miso_io_num = -1, .sclk_io_num = PIN_LCD_SCLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = LCD_W * LCD_H * 2 + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t cfg = {
        .dc_gpio_num = PIN_LCD_DC, .cs_gpio_num = PIN_LCD_CS, .pclk_hz = 26 * 1000 * 1000,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8, .spi_mode = 0, .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &cfg, &io));

    gpio_set_level(PIN_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_lcd_panel_io_tx_param(io, 0x01, NULL, 0); /* SWRESET */
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_lcd_panel_io_tx_param(io, 0x11, NULL, 0); /* SLPOUT */
    vTaskDelay(pdMS_TO_TICKS(150));
    /* frame rate + power settings (ST7735S datasheet typical) */
    esp_lcd_panel_io_tx_param(io, 0xB1, (uint8_t[]){0x01, 0x2C, 0x2D}, 3);
    esp_lcd_panel_io_tx_param(io, 0xB2, (uint8_t[]){0x01, 0x2C, 0x2D}, 3);
    esp_lcd_panel_io_tx_param(io, 0xB3, (uint8_t[]){0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D}, 6);
    esp_lcd_panel_io_tx_param(io, 0xB4, (uint8_t[]){0x07}, 1);
    esp_lcd_panel_io_tx_param(io, 0xC0, (uint8_t[]){0xA2, 0x02, 0x84}, 3);
    esp_lcd_panel_io_tx_param(io, 0xC1, (uint8_t[]){0xC5}, 1);
    esp_lcd_panel_io_tx_param(io, 0xC2, (uint8_t[]){0x0A, 0x00}, 2);
    esp_lcd_panel_io_tx_param(io, 0xC3, (uint8_t[]){0x8A, 0x2A}, 2);
    esp_lcd_panel_io_tx_param(io, 0xC4, (uint8_t[]){0x8A, 0xEE}, 2);
    esp_lcd_panel_io_tx_param(io, 0xC5, (uint8_t[]){0x0E}, 1);
    esp_lcd_panel_io_tx_param(io, LCD_INVERT ? 0x21 : 0x20, NULL, 0); /* INVON/INVOFF */
    esp_lcd_panel_io_tx_param(io, 0x36, (uint8_t[]){LCD_MADCTL}, 1);
    esp_lcd_panel_io_tx_param(io, 0x3A, (uint8_t[]){0x05}, 1); /* RGB565 */
    esp_lcd_panel_io_tx_param(io, 0x13, NULL, 0);              /* NORON */
    esp_lcd_panel_io_tx_param(io, 0x29, NULL, 0);              /* DISPON */
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "LCD init: landscape %dx%d, offset x=%d y=%d, MADCTL=0x%02x, INV=%d",
             LCD_W, LCD_H, LCD_X_OFF, LCD_Y_OFF, LCD_MADCTL, LCD_INVERT);
}

static void gpio0_check(const char *where)
{
    gpio_set_pull_mode(PIN_BTN, GPIO_PULLUP_ONLY);
    gpio_set_direction(PIN_BTN, GPIO_MODE_INPUT);
    ESP_LOGI(TAG, "GPIO0 CHECK [%s]: level=%d", where, gpio_get_level(PIN_BTN));
}

static void set_status(const char *s)
{
    strlcpy(status_line, s, sizeof(status_line));
    draw_screen();
}

/* ---------------------------------------------------------------- button */
static void button_task(void *arg)
{
    gpio_config_t b = {.pin_bit_mask = BIT64(PIN_BTN), .mode = GPIO_MODE_INPUT, .pull_up_en = 1};
    gpio_config(&b);
    int last = 1;
    int64_t t_down = 0;
    int64_t t_report = 0;
    while (1) {
        if (esp_timer_get_time() - t_report > 10000000) {
            t_report = esp_timer_get_time();
            ESP_LOGI(TAG, "BUTTON GPIO%d raw level=%d", PIN_BTN, gpio_get_level(PIN_BTN));
        }
        int v = gpio_get_level(PIN_BTN);
        if (v != last) {
            vTaskDelay(pdMS_TO_TICKS(20));
            if (gpio_get_level(PIN_BTN) == v) {
                if (v == 0) {
                    t_down = esp_timer_get_time();
                    btn_count++;
                    ESP_LOGW(TAG, "BUTTON GPIO%d PRESSED (count=%d)", PIN_BTN, btn_count);
                } else {
                    ESP_LOGW(TAG, "BUTTON GPIO%d RELEASED after %lld ms", PIN_BTN,
                             (esp_timer_get_time() - t_down) / 1000);
                }
                last = v;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ---------------------------------------------------------------- LEDs */
static void ws2812_test(int gpio)
{
    led_strip_handle_t strip;
    led_strip_config_t sc = {.strip_gpio_num = gpio, .max_leds = 1,
                             .led_model = LED_MODEL_WS2812,
                             .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB};
    led_strip_rmt_config_t rc = {.clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = 10 * 1000 * 1000};
    if (led_strip_new_rmt_device(&sc, &rc, &strip) != ESP_OK) {
        ESP_LOGE(TAG, "led_strip init failed on GPIO%d", gpio);
        return;
    }
    const uint8_t col[3][3] = {{40, 0, 0}, {0, 40, 0}, {0, 0, 40}};
    const char *nm[3] = {"RED", "GREEN", "BLUE"};
    for (int i = 0; i < 3; i++) {
        char s[32];
        snprintf(s, sizeof(s), "WS2812 G%d %s", gpio, nm[i]);
        ESP_LOGI(TAG, "LED: driving %s (t=%lld ms)", s, esp_timer_get_time() / 1000);
        set_status(s);
        led_strip_set_pixel(strip, 0, col[i][0], col[i][1], col[i][2]);
        led_strip_refresh(strip);
        vTaskDelay(pdMS_TO_TICKS(700));
    }
    led_strip_clear(strip);
    vTaskDelay(pdMS_TO_TICKS(50));
    led_strip_del(strip);
    gpio_reset_pin(gpio);
}

static void apa_bit(int di, int ck, int bit)
{
    gpio_set_level(di, bit);
    esp_rom_delay_us(1);
    gpio_set_level(ck, 1);
    esp_rom_delay_us(1);
    gpio_set_level(ck, 0);
}
static void apa_byte(int di, int ck, uint8_t v)
{
    for (int i = 7; i >= 0; i--) apa_bit(di, ck, (v >> i) & 1);
}
static void apa_send(int di, int ck, uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < 4; i++) apa_byte(di, ck, 0x00);
    apa_byte(di, ck, 0xE0 | 4); /* brightness 4/31 */
    apa_byte(di, ck, b);
    apa_byte(di, ck, g);
    apa_byte(di, ck, r);
    for (int i = 0; i < 4; i++) apa_byte(di, ck, 0xFF);
}

static void apa102_test(int di, int ck)
{
    gpio_config_t o = {.pin_bit_mask = BIT64(di) | BIT64(ck), .mode = GPIO_MODE_OUTPUT};
    gpio_config(&o);
    gpio_set_level(ck, 0);
    const uint8_t col[3][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}};
    const char *nm[3] = {"RED", "GREEN", "BLUE"};
    for (int i = 0; i < 3; i++) {
        char s[32];
        snprintf(s, sizeof(s), "APA D%d C%d %s", di, ck, nm[i]);
        ESP_LOGI(TAG, "LED: driving %s (t=%lld ms)", s, esp_timer_get_time() / 1000);
        set_status(s);
        apa_send(di, ck, col[i][0], col[i][1], col[i][2]);
        vTaskDelay(pdMS_TO_TICKS(700));
    }
    apa_send(di, ck, 0, 0, 0);
    gpio_reset_pin(di);
    gpio_reset_pin(ck);
}

static void bl_set(int level)
{
    gpio_reset_pin(PIN_LCD_BL);
    gpio_set_direction(PIN_LCD_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_LCD_BL, level);
}

/* ---------------------------------------------------------------- TF card */
static void tf_test(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = 12; slot.cmd = 16; slot.d0 = 14; slot.d1 = 17; slot.d2 = 21; slot.d3 = 18;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_sdmmc_mount_config_t mc = {.format_if_mount_failed = false, .max_files = 4};
    sdmmc_card_t *card = NULL;
    esp_err_t err = esp_vfs_fat_sdmmc_mount("/sd", &host, &slot, &mc, &card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TF: mount failed (%s) - no card inserted or wrong pins", esp_err_to_name(err));
        return;
    }
    ESP_LOGW(TAG, "TF: MOUNTED OK (4-bit SDMMC CLK12 CMD16 D0 14 D1 17 D2 21 D3 18)");
    sdmmc_card_print_info(stdout, card);
    DIR *d = opendir("/sd");
    struct dirent *e;
    int n = 0;
    while (d && (e = readdir(d)) && n < 10) { ESP_LOGI(TAG, "TF: /sd/%s", e->d_name); n++; }
    if (d) closedir(d);
    esp_vfs_fat_sdcard_unmount("/sd", card);
}

/* ---------------------------------------------------------------- main */
void app_main(void)
{
    esp_chip_info_t ci;
    esp_chip_info(&ci);
    uint32_t fsz = 0;
    esp_flash_get_size(NULL, &fsz);
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "MicroESP pinout spike. chip rev %d, cores %d, flash %lu MB, PSRAM %u KB, MAC %02x%02x%02x%02x%02x%02x",
             ci.revision, ci.cores, fsz >> 20, (unsigned)(esp_psram_get_size() / 1024),
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    gpio0_check("boot");
    bl_set(0); /* active-low hypothesis: on */
    gpio0_check("after BL38=0");
    lcd_probe_bitbang();
    gpio0_check("after probe");
    lcd_permutation_scan();
    /* restore RST high after scan */
    gpio_reset_pin(PIN_LCD_RST);
    gpio_set_direction(PIN_LCD_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_LCD_RST, 1);

    fb = heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    lcd_init();
    gpio0_check("after lcd_init");
    xTaskCreate(button_task, "btn", 3072, NULL, 5, NULL);
    set_status("PATTERN");
#ifndef PINOUT_NO_TF
    gpio0_check("before tf");
    tf_test();
    gpio0_check("after tf");
#endif

    const int ws_pins[] = {40, 39, 48, 38, 21};
    for (int cycle = 1;; cycle++) {
        ESP_LOGI(TAG, "===== probe cycle %d =====", cycle);
        set_status("BL38=0 (LOW)");
        bl_set(0);
        ESP_LOGI(TAG, "BACKLIGHT: GPIO38 LOW for 3 s (t=%lld ms)", esp_timer_get_time() / 1000);
        vTaskDelay(pdMS_TO_TICKS(3000));
        set_status("BL38=1 (HIGH)");
        bl_set(1);
        ESP_LOGI(TAG, "BACKLIGHT: GPIO38 HIGH for 3 s (t=%lld ms)", esp_timer_get_time() / 1000);
        vTaskDelay(pdMS_TO_TICKS(3000));
        bl_set(0);
        for (int i = 0; i < sizeof(ws_pins) / sizeof(ws_pins[0]); i++) {
            ws2812_test(ws_pins[i]);
            if (ws_pins[i] == PIN_LCD_BL) bl_set(0);
        }
        apa102_test(40, 39);
        apa102_test(39, 40);
        set_status("IDLE 5s");
        ESP_LOGI(TAG, "IDLE 5 s (t=%lld ms)", esp_timer_get_time() / 1000);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
