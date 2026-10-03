/*
 * MicroESP — status LED (US-0020 basic). Driver/pin: include/mesp_board.h.
 *
 * Priority (first match):
 *   countdown running            -> red, fast blink (4 Hz)
 *   wake sent (in progress)      -> white pulse (breathing)
 *   agent pairing / cloud not provisioned (!tylink / !wifi) -> blue blink (1 Hz)
 *   error (fault bits except hid_not_armed) -> red steady
 *   PC on + agent                -> green
 *   PC on without agent / booting -> amber (booting blinks)
 *   PC off / sleep / unknown     -> dim white
 * Brightness is capped by MESP_LED_MAX_BRIGHTNESS ("low by default").
 */
#include "mesp_board.h"
#include "mesp_hal.h"
#include "modules.h"

static uint8_t s_r = 1, s_g = 1, s_b = 1; /* force first write */

static uint8_t sc(int v) { return (uint8_t)((v * MESP_LED_MAX_BRIGHTNESS) / 255); }

void led_init(void)
{
    mhal_led_init();
    mhal_led_set(0, 0, 0);
}

void led_tick(uint32_t now)
{
    int r = 0, g = 0, b = 0;
    bool blink1 = (now / 500) & 1, blink4 = (now / 125) & 1;
    uint32_t ph = now % 2000;
    int breath = ph < 1000 ? (int)(ph * 255 / 1000) : (int)((2000 - ph) * 255 / 1000);
    if (g_app.pwr.st == PWR_COUNTDOWN) {
        r = blink4 ? 255 : 0;
    } else if (g_app.wake.active) {
        r = g = b = breath;
    } else if (pairing_active() || !g_app.cloud_provisioned) {
        b = blink1 ? 255 : 0;
    } else if (g_app.faults & ~FAULT_HID_NOT_ARMED) {
        r = 255;
    } else {
        switch (g_app.pcs.state) {
        case PCS_ON: g = 255; break;
        case PCS_ON_NO_AGENT: r = 255, g = 120; break;
        case PCS_BOOTING: r = blink1 ? 255 : 0, g = blink1 ? 120 : 0; break;
        default: r = g = b = 40; break; /* dim */
        }
    }
    uint8_t R = sc(r), G = sc(g), B = sc(b);
    if (R == s_r && G == s_g && B == s_b) return;
    s_r = R, s_g = G, s_b = B;
    mhal_led_set(R, G, B);
}
