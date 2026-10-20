#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "aw9523.h"
#include "deepstoa_v1.h"
#include "fs_control.h"

static const char *TAG = "fs_control";

static sdmmc_card_t *s_card;
static bool        s_mounted;

esp_err_t fs_control_init(void)
{
    if (s_mounted) return ESP_OK;

    /* The SD socket shares the board peripheral power rail. */
    aw9523_set_pin(DEEPV1_AW_PIN_EN_POWER, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_4BIT;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = DEEPV1_PIN_SD_CLK;
    slot.cmd = DEEPV1_PIN_SD_CMD;
    slot.d0 = DEEPV1_PIN_SD_DAT0;
    slot.d1 = DEEPV1_PIN_SD_DAT1;
    slot.d2 = DEEPV1_PIN_SD_DAT2;
    slot.d3 = DEEPV1_PIN_SD_DAT3;
    slot.width = 4;

    const esp_vfs_fat_mount_config_t cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t err = esp_vfs_fat_sdmmc_mount(FS_MOUNT_POINT, &host, &slot,
                                             &cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD mount at %s failed: %s", FS_MOUNT_POINT,
                 esp_err_to_name(err));
        s_card = NULL;
        return err;
    }

    s_mounted = true;

    uint64_t total = 0, used = 0;
    fs_control_get_usage(&total, &used);
    ESP_LOGI(TAG, "SD mounted at %s (%llu/%llu KB used)",
             FS_MOUNT_POINT, used / 1024, total / 1024);
    return ESP_OK;
}

void fs_control_deinit(void)
{
    if (!s_mounted) return;
    esp_vfs_fat_sdcard_unmount(FS_MOUNT_POINT, s_card);
    s_card = NULL;
    s_mounted = false;
}

bool fs_control_is_mounted(void) { return s_mounted; }

void fs_control_get_usage(uint64_t *total, uint64_t *used)
{
    if (total) *total = 0;
    if (used)  *used  = 0;
    if (!s_mounted) return;

    uint64_t t = 0, f = 0;
    if (esp_vfs_fat_info(FS_MOUNT_POINT, &t, &f) != ESP_OK) return;
    if (total) *total = t;
    if (used)  *used  = t - f;
}
