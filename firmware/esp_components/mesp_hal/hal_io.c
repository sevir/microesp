/* MicroESP HAL — BOOT button, status LED (WS2812 via RMT or APA102 bit-bang) and the
 * ST7735 LCD (esp_lcd SPI panel IO, landscape 160x80). Pins: include/mesp_board.h. */
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "mesp_board.h"
#include "mesp_hal.h"

static const char *TAG = "mhal_io";

/* ------------------------------------------------------------------ button */
void mhal_button_init(void)
{
    gpio_config_t b = {.pin_bit_mask = BIT64(MESP_PIN_BUTTON), .mode = GPIO_MODE_INPUT, .pull_up_en = 1};
    gpio_config(&b);
}

bool mhal_button_pressed(void) { return gpio_get_level(MESP_PIN_BUTTON) == 0; }

/* ------------------------------------------------------------------ LED */
#if MESP_LED_TYPE == MESP_LED_WS2812
static led_strip_handle_t s_strip;
#endif
static bool s_led_ok;

#if MESP_LED_TYPE == MESP_LED_APA102
static void apa_byte(uint8_t v)
{
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(MESP_LED_PIN, (v >> i) & 1);
        esp_rom_delay_us(1);
        gpio_set_level(MESP_LED_PIN_CLK, 1);
        esp_rom_delay_us(1);
        gpio_set_level(MESP_LED_PIN_CLK, 0);
    }
}
#endif

int mhal_led_init(void)
{
#if MESP_LED_TYPE == MESP_LED_WS2812
    led_strip_config_t sc = {.strip_gpio_num = MESP_LED_PIN,
                             .max_leds = 1,
                             .led_model = LED_MODEL_WS2812,
                             .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB};
    led_strip_rmt_config_t rc = {.clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = 10 * 1000 * 1000};
    if (led_strip_new_rmt_device(&sc, &rc, &s_strip) != ESP_OK) {
        ESP_LOGE(TAG, "WS2812 init failed on GPIO%d", MESP_LED_PIN);
        return -1;
    }
    led_strip_clear(s_strip);
#elif MESP_LED_TYPE == MESP_LED_APA102
    gpio_config_t o = {.pin_bit_mask = BIT64(MESP_LED_PIN) | BIT64(MESP_LED_PIN_CLK), .mode = GPIO_MODE_OUTPUT};
    gpio_config(&o);
    gpio_set_level(MESP_LED_PIN_CLK, 0);
#else
    return -1;
#endif
    s_led_ok = true;
    ESP_LOGI(TAG, "LED type %d on GPIO%d", MESP_LED_TYPE, MESP_LED_PIN);
    return 0;
}

void mhal_led_set(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_led_ok) return;
#if MESP_LED_TYPE == MESP_LED_WS2812
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
#elif MESP_LED_TYPE == MESP_LED_APA102
    for (int i = 0; i < 4; i++) apa_byte(0x00);
    apa_byte(0xE0 | 31);
    apa_byte(b);
    apa_byte(g);
    apa_byte(r);
    for (int i = 0; i < 4; i++) apa_byte(0xFF);
#endif
}

/* ------------------------------------------------------------------ LCD */
#define LCD_BUF_LINES 20
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_lcd_done;
static void *s_lcd_buf;
static size_t s_lcd_buf_sz;

static bool lcd_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ed, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_lcd_done, &woken);
    return woken == pdTRUE;
}

void mhal_lcd_backlight(bool on)
{
#if MESP_LCD_ENABLE
    gpio_set_level(MESP_LCD_PIN_BL, MESP_LCD_BL_ACTIVE_LOW ? !on : on);
#endif
}

static void tx(uint8_t cmd, const uint8_t *d, size_t n) { esp_lcd_panel_io_tx_param(s_io, cmd, d, n); }

int mhal_lcd_init(void)
{
#if !MESP_LCD_ENABLE
    return -1;
#else
    if (s_io) return 0;
    gpio_config_t o = {.pin_bit_mask = BIT64(MESP_LCD_PIN_RST) | BIT64(MESP_LCD_PIN_BL), .mode = GPIO_MODE_OUTPUT};
    gpio_config(&o);
    mhal_lcd_backlight(false);
    s_lcd_buf_sz = MESP_LCD_W * LCD_BUF_LINES * 2;
    s_lcd_buf = heap_caps_malloc(s_lcd_buf_sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_lcd_done = xSemaphoreCreateBinary();
    if (!s_lcd_buf || !s_lcd_done) return -1;
    spi_bus_config_t bus = {
        .mosi_io_num = MESP_LCD_PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = MESP_LCD_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)s_lcd_buf_sz + 8,
    };
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return -1;
    esp_lcd_panel_io_spi_config_t cfg = {
        .dc_gpio_num = MESP_LCD_PIN_DC,
        .cs_gpio_num = MESP_LCD_PIN_CS,
        .pclk_hz = MESP_LCD_SPI_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 4,
        .on_color_trans_done = lcd_trans_done,
    };
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &cfg, &s_io) != ESP_OK) return -1;
    gpio_set_level(MESP_LCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(MESP_LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    tx(0x01, NULL, 0); /* SWRESET */
    vTaskDelay(pdMS_TO_TICKS(150));
    tx(0x11, NULL, 0); /* SLPOUT */
    vTaskDelay(pdMS_TO_TICKS(150));
    tx(0xB1, (const uint8_t[]){0x01, 0x2C, 0x2D}, 3);
    tx(0xB2, (const uint8_t[]){0x01, 0x2C, 0x2D}, 3);
    tx(0xB3, (const uint8_t[]){0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D}, 6);
    tx(0xB4, (const uint8_t[]){0x07}, 1);
    tx(0xC0, (const uint8_t[]){0xA2, 0x02, 0x84}, 3);
    tx(0xC1, (const uint8_t[]){0xC5}, 1);
    tx(0xC2, (const uint8_t[]){0x0A, 0x00}, 2);
    tx(0xC3, (const uint8_t[]){0x8A, 0x2A}, 2);
    tx(0xC4, (const uint8_t[]){0x8A, 0xEE}, 2);
    tx(0xC5, (const uint8_t[]){0x0E}, 1);
    tx(MESP_LCD_INVERT ? 0x21 : 0x20, NULL, 0);
    tx(0x36, (const uint8_t[]){MESP_LCD_MADCTL}, 1);
    tx(0x3A, (const uint8_t[]){0x05}, 1); /* RGB565 */
    tx(0x13, NULL, 0);                     /* NORON */
    tx(0x29, NULL, 0);                     /* DISPON */
    ESP_LOGI(TAG, "LCD ST7735 %dx%d off x=%d y=%d MADCTL=0x%02x INV=%d", MESP_LCD_W, MESP_LCD_H, MESP_LCD_X_OFF,
             MESP_LCD_Y_OFF, MESP_LCD_MADCTL, MESP_LCD_INVERT);
    return 0;
#endif
}

void *mhal_lcd_buf(size_t *bytes)
{
    if (bytes) *bytes = s_lcd_buf_sz;
    return s_lcd_buf;
}

int mhal_lcd_draw(int x1, int y1, int x2, int y2, const void *px)
{
    if (!s_io) return -1;
    uint16_t c1 = MESP_LCD_X_OFF + x1, c2 = MESP_LCD_X_OFF + x2;
    uint16_t r1 = MESP_LCD_Y_OFF + y1, r2 = MESP_LCD_Y_OFF + y2;
    uint8_t ca[4] = {c1 >> 8, c1 & 0xff, c2 >> 8, c2 & 0xff};
    uint8_t ra[4] = {r1 >> 8, r1 & 0xff, r2 >> 8, r2 & 0xff};
    tx(0x2A, ca, 4);
    tx(0x2B, ra, 4);
    size_t n = (size_t)(x2 - x1 + 1) * (size_t)(y2 - y1 + 1) * 2;
    xSemaphoreTake(s_lcd_done, 0);
    if (esp_lcd_panel_io_tx_color(s_io, 0x2C, px, n) != ESP_OK) return -1;
    xSemaphoreTake(s_lcd_done, pdMS_TO_TICKS(200));
    return 0;
}
