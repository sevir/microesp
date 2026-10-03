idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_lcd esp_psram esp_timer fatfs sdmmc spi_flash)
