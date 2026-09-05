#pragma once

#include <lvgl.h>
#include <string.h>

// Image storage macros — ported from EPOS epd_display_control.h
// When CONFIG_STORE_IMAGES_EXTERNAL_FLASH is enabled (Zephyr Kconfig),
// images are stored as files on external flash (e.g. "S:battery_0.bin").
// DeepStoa currently stores everything in internal flash, so the
// internal-flash path is always used. External flash support can be
// added later by wrapping these with an ESP-IDF Kconfig option.

#define EPOS_LV_IMG_DECLARE(var_name)                        LV_IMG_DECLARE(var_name)
#define EPOS_LV_IMG_USE(var_name)                            &var_name
#define EPOS_LV_IMG_USE_WITH_MOUNT(var_name, mount_letter)   &var_name
