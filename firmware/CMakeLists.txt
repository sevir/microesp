##
# MicroESP firmware — TuyaOpen application (board POCKET_DONGLE_S3).
# ESP-IDF-side code lives in esp_components/ (picked up by tos.py via
# TUYAOS_EXTRA_COMPONENT_DIRS); the app only talks to it through include/mesp_hal.h.
##
set(APP_PATH ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(APP_NAME ${APP_PATH} NAME)

file(GLOB APP_SRCS ${APP_PATH}/src/*.c ${APP_PATH}/src/core/*.c)

set(APP_INC
    ${APP_PATH}/src
    ${APP_PATH}/src/core
    ${APP_PATH}/include
)

add_library(${EXAMPLE_LIB})
target_sources(${EXAMPLE_LIB} PRIVATE ${APP_SRCS})
target_include_directories(${EXAMPLE_LIB} PRIVATE ${APP_INC})
target_compile_options(${EXAMPLE_LIB} PRIVATE -Wall -Wno-unused-function)
