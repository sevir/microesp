#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void pinout_probe_main(void);

static void probe_task(void *arg)
{
    pinout_probe_main(); /* never returns */
    vTaskDelete(NULL);
}

/* Start the LCD/LED/backlight visual probe cycle (see hw/pinout.md). */
int pinout_probe_start(void)
{
    return xTaskCreate(probe_task, "pinout_probe", 8192, NULL, 3, NULL) == pdPASS ? 0 : -1;
}
