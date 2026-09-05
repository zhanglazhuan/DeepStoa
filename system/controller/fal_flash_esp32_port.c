// system/controller/fal_flash_esp32_port.c
// FAL → esp_partition bridge for FlashDB on ESP-IDF
//
// 两个设备：sys（厂家区，恢复出厂设置不擦）和 user（用户区）。
// 每个绑定到自己的 esp_partition，所以产线可以单独刷 sys，
// 用户侧的恢复出厂设置也只要整块擦 user 就行。

#include <string.h>
#include <assert.h>
#include <fal.h>
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define FLASH_ERASE_MIN_SIZE (4 * 1024)

static SemaphoreHandle_t s_lock;

#define LOCK()   xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)

static void ensure_lock(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateCounting(1, 1);
        assert(s_lock);
    }
}

/* 两个设备的读写除了目标分区完全一样，用宏生成，免得复制四份函数体 */
#define DEFINE_FAL_DEV(suffix, part_label, dev_name, dev_len)                  \
    static const esp_partition_t *s_part_##suffix;                             \
                                                                               \
    static int init_##suffix(void)                                             \
    {                                                                          \
        ensure_lock();                                                         \
        s_part_##suffix = esp_partition_find_first(                            \
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, part_label);   \
        assert(s_part_##suffix);                                               \
        return 1;                                                              \
    }                                                                          \
                                                                               \
    static int read_##suffix(long offset, uint8_t *buf, size_t size)           \
    {                                                                          \
        LOCK();                                                                \
        esp_err_t ret = esp_partition_read(s_part_##suffix, offset, buf, size);\
        UNLOCK();                                                              \
        return ret;                                                            \
    }                                                                          \
                                                                               \
    static int write_##suffix(long offset, const uint8_t *buf, size_t size)    \
    {                                                                          \
        LOCK();                                                                \
        esp_err_t ret = esp_partition_write(s_part_##suffix, offset, buf, size);\
        UNLOCK();                                                              \
        return ret;                                                            \
    }                                                                          \
                                                                               \
    static int erase_##suffix(long offset, size_t size)                        \
    {                                                                          \
        int32_t n = ((size - 1) / FLASH_ERASE_MIN_SIZE) + 1;                    \
        LOCK();                                                                \
        esp_err_t ret = esp_partition_erase_range(s_part_##suffix, offset,     \
                                                  n * FLASH_ERASE_MIN_SIZE);   \
        UNLOCK();                                                              \
        return ret;                                                            \
    }                                                                          \
                                                                               \
    const struct fal_flash_dev nor_flash_##suffix = {                          \
        .name       = dev_name,                                                \
        .addr       = 0,                                                       \
        .len        = (dev_len),                                               \
        .blk_size   = FLASH_ERASE_MIN_SIZE,                                    \
        .ops        = { init_##suffix, read_##suffix,                          \
                        write_##suffix, erase_##suffix },                      \
        .write_gran = 1,                                                       \
    }

DEFINE_FAL_DEV(sys,  "fdb_sys",  NOR_FLASH_SYS_NAME,   32 * 1024);
DEFINE_FAL_DEV(user, "fdb_user", NOR_FLASH_USER_NAME, 128 * 1024);
