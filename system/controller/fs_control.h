/**
 * @file fs_control.h
 * @brief Physical SD card FAT filesystem mounted through the 4-bit SDMMC bus.
 */

#ifndef FS_CONTROL_H
#define FS_CONTROL_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FS_MOUNT_POINT      "/sdcard"
/** Mount the SD card. Call once after AW9523 initialization. */
esp_err_t fs_control_init(void);
void      fs_control_deinit(void);

bool      fs_control_is_mounted(void);

/** 容量，单位字节。未挂载时写 0。 */
void      fs_control_get_usage(uint64_t *total, uint64_t *used);

#ifdef __cplusplus
}
#endif

#endif /* FS_CONTROL_H */
