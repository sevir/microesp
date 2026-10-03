idf_component_register(SRCS "usb_composite.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_tinyusb
                       PRIV_REQUIRES esp_system esp_hw_support hal soc esp_timer log esp_wifi bt driver)
# The TuyaOpen app library references usb_composite_start(); link the whole archive so the
# symbol is always resolved regardless of link order.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u usb_composite_start")
# Safety for first bring-up: fall back to USB-Serial/JTAG if no CDC command within N s (0 = off; first bring-up used 180).
target_compile_definitions(${COMPONENT_LIB} PRIVATE USB_NO_CMD_TIMEOUT_S=0)
