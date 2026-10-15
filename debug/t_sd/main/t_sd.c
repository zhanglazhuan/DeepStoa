/* DeepStoa V1 SDMMC + FATFS hardware test.
 *
 * This is a standalone debug image.  It validates the actual board wiring
 * without changing the production application startup sequence.
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

#include "deepstoa_v1.h"
#include "aw9523.h"

static const char *TAG = "t_sd";
#define MOUNT_POINT "/sdcard"
#define TEST_FILE   MOUNT_POINT "/deepstoa_sd_test.txt"
#define TEST_TEXT   "DeepStoa V1 SD test OK\n"

static bool test_file_io(void)
{
    FILE *f = fopen(TEST_FILE, "w");
    if (!f) {
        ESP_LOGE(TAG, "cannot open %s for write", TEST_FILE);
        return false;
    }
    size_t len = strlen(TEST_TEXT);
    bool ok = fwrite(TEST_TEXT, 1, len, f) == len;
    fclose(f);
    if (!ok) return false;

    char buf[sizeof(TEST_TEXT)] = {0};
    f = fopen(TEST_FILE, "r");
    if (!f) return false;
    ok = fread(buf, 1, len, f) == len && memcmp(buf, TEST_TEXT, len) == 0;
    fclose(f);
    unlink(TEST_FILE);
    return ok;
}

static sdmmc_card_t *mount_sd(void)
{
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

    esp_vfs_fat_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_card_t *card = NULL;
    esp_err_t err = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot,
                                             &mount, &card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount failed: %s (0x%x)", esp_err_to_name(err), err);
        return NULL;
    }
    return card;
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== DeepStoa V1 SDMMC test ===");
    ESP_LOGI(TAG, "CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d DET=%d",
             DEEPV1_PIN_SD_CLK, DEEPV1_PIN_SD_CMD, DEEPV1_PIN_SD_DAT0,
             DEEPV1_PIN_SD_DAT1, DEEPV1_PIN_SD_DAT2, DEEPV1_PIN_SD_DAT3,
             DEEPV1_PIN_SD_DETECT);

    esp_err_t err = aw9523_init(DEEPV1_I2C_PORT, DEEPV1_PIN_I2C_SDA,
                                DEEPV1_PIN_I2C_SCL, DEEPV1_PIN_IO_RESET,
                                DEEPV1_AW9523_ADDR);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AW9523 init failed: %s", esp_err_to_name(err));
        goto fail;
    }
    aw9523_set_pin(DEEPV1_AW_PIN_EN_POWER, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    sdmmc_card_t *card = mount_sd();
    if (!card) goto fail;
    sdmmc_card_print_info(stdout, card);
    if (!test_file_io()) {
        ESP_LOGE(TAG, "file write/read verification failed");
        goto fail;
    }

    ESP_LOGI(TAG, "SD TEST PASSED");
    while (1) vTaskDelay(pdMS_TO_TICKS(5000));

fail:
    ESP_LOGE(TAG, "SD TEST FAILED");
    while (1) vTaskDelay(pdMS_TO_TICKS(5000));
}
