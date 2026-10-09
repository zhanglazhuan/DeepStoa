#ifndef USB_MSC_MANAGER_H
#define USB_MSC_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t usb_msc_start_for_sdcard(void);
esp_err_t usb_msc_stop_for_sdcard(void);
bool usb_msc_is_running(void);
bool usb_msc_is_host_using_storage(void);

#ifdef __cplusplus
}
#endif

#endif // USB_MSC_MANAGER_H
