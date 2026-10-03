/*
 * MicroESP — board pins and panel parameters for the Pocket-Dongle-S3
 * (LilyGO T-Dongle-S3 clone). SINGLE place to change them.
 *
 * Values are the T-Dongle-S3 reference ones from hw/pinout.md. The LCD wiring/offsets,
 * the backlight polarity and the LED type/pin are PENDING USER VISUAL CONFIRMATION
 * (hw/pinout.md checklist). Every value can also be overridden with -D at build time.
 */
#pragma once

/* ---- button ---- */
#ifndef MESP_PIN_BUTTON
#define MESP_PIN_BUTTON 0 /* BOOT, active low, internal pull-up */
#endif

/* ---- LCD ST7735(S) 80x160 used in landscape 160x80, SPI2 ---- */
#ifndef MESP_LCD_ENABLE
#define MESP_LCD_ENABLE 1
#endif
#define MESP_LCD_PIN_MOSI 3
#define MESP_LCD_PIN_SCLK 5
#define MESP_LCD_PIN_CS   4
#define MESP_LCD_PIN_DC   2
#define MESP_LCD_PIN_RST  1
#define MESP_LCD_PIN_BL   38
#ifndef MESP_LCD_BL_ACTIVE_LOW
#define MESP_LCD_BL_ACTIVE_LOW 1
#endif
#define MESP_LCD_W 160
#define MESP_LCD_H 80
#ifndef MESP_LCD_X_OFF
#define MESP_LCD_X_OFF 1 /* column offset in landscape (= row offset in portrait) */
#endif
#ifndef MESP_LCD_Y_OFF
#define MESP_LCD_Y_OFF 26 /* row offset in landscape (= column offset in portrait) */
#endif
#ifndef MESP_LCD_MADCTL
#define MESP_LCD_MADCTL 0x68 /* MX | MV | BGR */
#endif
#ifndef MESP_LCD_INVERT
#define MESP_LCD_INVERT 1
#endif
#define MESP_LCD_SPI_HZ (26 * 1000 * 1000)

/* ---- status LED ---- */
#define MESP_LED_NONE   0
#define MESP_LED_WS2812 1 /* RMT, GRB */
#define MESP_LED_APA102 2 /* bit-banged DI/CLK */
#ifndef MESP_LED_TYPE
#define MESP_LED_TYPE MESP_LED_WS2812
#endif
#ifndef MESP_LED_PIN
#define MESP_LED_PIN 40 /* WS2812 data / APA102 DI */
#endif
#ifndef MESP_LED_PIN_CLK
#define MESP_LED_PIN_CLK 39 /* APA102 CLK only */
#endif
#ifndef MESP_LED_MAX_BRIGHTNESS
#define MESP_LED_MAX_BRIGHTNESS 48 /* 0..255 cap: "brillo bajo por defecto" */
#endif
