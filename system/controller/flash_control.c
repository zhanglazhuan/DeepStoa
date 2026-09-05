// system/controller/flash_control.c
// FlashDB KV databases — FAL mode on partitions "fdb_sys" / "fdb_user"

#include <stdio.h>
#include <string.h>
#include <fal.h>
#include <flashdb.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_partition.h"
#include "esp_system.h"

#include "flash_control.h"

static const char *TAG = "flash_ctrl";

struct fdb_kvdb g_kvdb;       /* 用户区 */
struct fdb_kvdb g_sys_kvdb;   /* 厂家区 */

#define SYS_KEY_DEVICE_ID  "dev_id"
#define DEVICE_ID_MAX      24

/**
 * 初始化一个 KVDB，但先确认 FAL 分区真的存在。
 *
 * 不能直接调 fdb_kvdb_init()：分区找不到时它内部会走到
 * _fdb_init_finish() 里 printf("%s", _fdb_db_path(db))，而那时
 * db->storage.part 是 NULL —— ROM 的 strlen(NULL) 直接 LoadProhibited，
 * 设备无限重启，backtrace 还指在 ROM 里，极难定位。
 * 分区表在开发期还会变（SD、音频分区…），这个坑必须挡住。
 */
static bool init_kvdb(struct fdb_kvdb *db, const char *name, const char *part)
{
    if (!fal_partition_find(part)) {
        ESP_LOGE(TAG, "FAL partition '%s' not found — "
                      "partition table does not match the firmware - run idf.py erase-flash flash", part);
        return false;
    }

    fdb_err_t err = fdb_kvdb_init(db, name, part, NULL, NULL);
    if (err != FDB_NO_ERR) {
        ESP_LOGE(TAG, "KVDB '%s' on '%s' init failed: %d", name, part, (int)err);
        return false;
    }
    return true;
}

void flash_control_init(void)
{
    fal_init();

    /* 用户区。数据库名沿用 "env"，现有数据布局不变。 */
    if (init_kvdb(&g_kvdb, "env", "fdb_user")) {
        ESP_LOGI(TAG, "FlashDB user KV ready on 'fdb_user'");
    }

    /* 厂家区 */
    if (init_kvdb(&g_sys_kvdb, "sys", "fdb_sys")) {
        ESP_LOGI(TAG, "FlashDB sys KV ready on 'fdb_sys' (device %s)",
                 flash_sys_device_id());
    }
}

void flash_control_deinit(void)
{
    fdb_kvdb_deinit(&g_kvdb);
    fdb_kvdb_deinit(&g_sys_kvdb);
    ESP_LOGI(TAG, "FlashDB deinitialized");
}

/* ── 厂家区 ───────────────────────────────────────────────────────── */

bool flash_sys_get_str(const char *key, char *out, size_t out_len)
{
    if (!key || !out || out_len == 0) return false;

    struct fdb_blob blob;
    size_t read = fdb_kv_get_blob(&g_sys_kvdb, key,
                                  fdb_blob_make(&blob, out, out_len - 1));
    if (read == 0) { out[0] = '\0'; return false; }

    out[read < out_len ? read : out_len - 1] = '\0';
    return out[0] != '\0';
}

bool flash_sys_set_str(const char *key, const char *val)
{
    if (!key || !val) return false;

    struct fdb_blob blob;
    fdb_err_t err = fdb_kv_set_blob(&g_sys_kvdb, key,
                                    fdb_blob_make(&blob, val, strlen(val) + 1));
    if (err != FDB_NO_ERR) {
        ESP_LOGE(TAG, "sys set '%s' failed: %d", key, (int)err);
        return false;
    }
    return true;
}

const char *flash_sys_device_id(void)
{
    static char s_id[DEVICE_ID_MAX];
    if (s_id[0] != '\0') return s_id;

    if (flash_sys_get_str(SYS_KEY_DEVICE_ID, s_id, sizeof(s_id))) return s_id;

    /* 产线还没刷机器码：用 eFuse MAC 推一个稳定的占位值。
     * 不写回 flash —— 写了产线就分不清"已刷"和"自动生成"了。 */
    uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);
    snprintf(s_id, sizeof(s_id), "DS-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return s_id;
}

/* ── 恢复出厂设置 ─────────────────────────────────────────────────── */

void flash_control_factory_reset(void)
{
    ESP_LOGW(TAG, "Factory reset: erasing user partition (sys preserved)");

    /* 先关掉两个库，避免擦除过程中还有人在读写 */
    flash_control_deinit();

    const esp_partition_t *user = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "fdb_user");
    if (!user) {
        ESP_LOGE(TAG, "fdb_user partition not found!");
        esp_restart();
    }

    esp_err_t ret = esp_partition_erase_range(user, 0, user->size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "erase failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGW(TAG, "User data erased (%lu bytes). fdb_sys untouched. Rebooting...",
                 (unsigned long)user->size);
    }

    esp_restart();
}
