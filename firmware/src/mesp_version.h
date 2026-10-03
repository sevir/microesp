/*
 * Firmware version: the single source is CONFIG_PROJECT_VERSION in
 * firmware/app_default.config (TuyaOpen generates PROJECT_VERSION in tuya_kconfig.h).
 * It is reported to Tuya (OTA), in the cdc-v1 welcome "fw" field, on screen and by !version.
 */
#pragma once
#include "tuya_kconfig.h"

#ifndef PROJECT_VERSION
#error "PROJECT_VERSION missing: set CONFIG_PROJECT_VERSION in app_default.config"
#endif
#define MESP_FW_VERSION PROJECT_VERSION

/* Build flavour (firmware/Kconfig, "./build.sh dev"): 1 = development CLI. */
#if defined(MESP_DEV_CLI) && MESP_DEV_CLI
#define MESP_DEV 1
#define MESP_BUILD_FLAVOUR "dev"
#else
#define MESP_DEV 0
#define MESP_BUILD_FLAVOUR "release"
#endif
