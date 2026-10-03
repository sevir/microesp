# MicroESP hardware abstraction (ESP-IDF side). The TuyaOpen application (firmware/src)
# cannot see ESP-IDF headers; it calls this component through include/mesp_hal.h.
idf_component_register(SRCS "hal_usb.c" "hal_sys.c" "hal_io.c" "hal_net.c" "hal_cloud.c"
                       INCLUDE_DIRS "../../include"
                       REQUIRES esp_tinyusb
                       PRIV_REQUIRES esp_system esp_hw_support hal soc esp_timer log driver esp_driver_gpio
                                     esp_driver_spi esp_lcd nvs_flash app_update esp_netif lwip
                                     esp_wifi esp_event mqtt mbedtls
                       WHOLE_ARCHIVE)
