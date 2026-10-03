# Re-uses the stand-alone pinout spike (hw/spikes/pinout/main/main.c) inside the TuyaOpen
# firmware so the visual LCD/LED test pattern runs alongside TuyaOpen + TinyUSB.
set(PINOUT_SRC "${CMAKE_CURRENT_LIST_DIR}/../../../pinout/main")
idf_component_register(SRCS "${PINOUT_SRC}/main.c" "pinout_probe.c"
                       INCLUDE_DIRS "${PINOUT_SRC}"
                       PRIV_REQUIRES driver esp_lcd esp_psram esp_timer fatfs sdmmc spi_flash)
target_compile_definitions(${COMPONENT_LIB} PRIVATE app_main=pinout_probe_main PINOUT_NO_TF=1)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u pinout_probe_start")
