#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"

#include "fs_control.h"

static const char *TAG = "fs_control";

static wl_handle_t s_wl = WL_INVALID_HANDLE;
static bool        s_mounted;

esp_err_t fs_control_init(void)
{
    if (s_mounted) return ESP_OK;

    const esp_vfs_fat_mount_config_t cfg = {
        /* 镜像是构建时生成好的，正常不该走到格式化这一步。
         * 但如果分区被挪过/擦过，允许自动格式化，免得设备直接不可用。 */
        .format_if_mount_failed = true,
        .max_files              = 4,
        .allocation_unit_size   = CONFIG_WL_SECTOR_SIZE,
    };

    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(FS_MOUNT_POINT,
                                                     FS_PARTITION_LABEL,
                                                     &cfg, &s_wl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount %s failed: %s", FS_MOUNT_POINT, esp_err_to_name(err));
        return err;
    }

    s_mounted = true;

    uint64_t total = 0, used = 0;
    fs_control_get_usage(&total, &used);
    ESP_LOGI(TAG, "mounted %s (%llu/%llu KB used)",
             FS_MOUNT_POINT, used / 1024, total / 1024);

    /* 曲目目录不存在就建一个，省得列表页一上来就是"目录不存在" */
    struct stat st;
    if (stat(FS_MOUNT_POINT "/music", &st) != 0) {
        if (mkdir(FS_MOUNT_POINT "/music", 0777) == 0) {
            ESP_LOGI(TAG, "created " FS_MOUNT_POINT "/music");
        }
    }
    return ESP_OK;
}

void fs_control_deinit(void)
{
    if (!s_mounted) return;
    esp_vfs_fat_spiflash_unmount_rw_wl(FS_MOUNT_POINT, s_wl);
    s_wl = WL_INVALID_HANDLE;
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
