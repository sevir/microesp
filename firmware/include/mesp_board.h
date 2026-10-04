/*
 * MicroESP — board pins and panel parameters for the Pocket-Dongle-S3
 * (LilyGO T-Dongle-S3 clone). SINGLE place to change them.
 *
 * LCD wiring, panel init, offsets and MADCTL were read from the FACTORY firmware running
 * on this board via the built-in USB-JTAG (GPIO matrix + SPI2 register trace, 2026-10-04,
 * see hw/pinout.md). They differ from the LilyGO T-Dongle-S3 reference. The LED type/pin
 * is still unverified (the factory firmware does not drive any LED).
 * Every value can also be overridden with -D at build time.
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
#define MESP_LCD_PIN_MOSI 11 /* factory: FSPID via GPIO matrix */
#define MESP_LCD_PIN_SCLK 10 /* factory: FSPICLK via GPIO matrix */
#define MESP_LCD_PIN_CS   12 /* factory: software CS, low during transfers */
#define MESP_LCD_PIN_DC   13 /* factory: low during command bytes */
#define MESP_LCD_PIN_RST  14 /* factory: ~20-60 ms low pulse at boot, then high */
#ifndef MESP_LCD_PIN_BL
#define MESP_LCD_PIN_BL   (-1) /* none: factory never drives a backlight GPIO (hard-wired on) */
#endif
#ifndef MESP_LCD_BL_ACTIVE_LOW
#define MESP_LCD_BL_ACTIVE_LOW 0
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
#define MESP_LCD_MADCTL 0x68 /* MX | MV | BGR: factory orientation rotated 180° (USB plug faces the other way when mounted); 0xA8 = factory */
#endif
#ifndef MESP_LCD_INVERT
#define MESP_LCD_INVERT 1 /* factory sends INVON (0x21) */
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
#define MESP_LED_MAX_BRIGHTNESS 48 /* 0..255 cap: "low brightness by default" */
#endif
