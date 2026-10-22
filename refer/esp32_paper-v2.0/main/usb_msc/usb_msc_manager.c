#include "usb_msc_manager.h"

#include <stdlib.h>

#include "sdkconfig.h"
#include "driver/sdmmc_host.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdmmc_cmd.h"
#include "tinyusb.h"
#include "tusb_msc_storage.h"

static const char *TAG = "usb_msc";

// Keep pin mapping in sync with components/sdcard_bsp/sdcard_bsp.c
#define SDMMC_D0_PIN    15
#define SDMMC_D1_PIN    7
#define SDMMC_D2_PIN    8
#define SDMMC_D3_PIN    18
#define SDMMC_CLK_PIN   16
#define SDMMC_CMD_PIN   17

static sdmmc_card_t *s_usb_msc_card = NULL;
static bool s_usb_msc_running = false;

static void usb_msc_deinit_host_and_card(sdmmc_card_t *card)
{
    if (card == NULL) {
        return;
    }

    if (card->host.flags & SDMMC_HOST_FLAG_DEINIT_ARG) {
        card->host.deinit_p(card->host.slot);
    } else if (card->host.deinit) {
        card->host.deinit();
    }

    free(card);
}

static esp_err_t usb_msc_init_sdmmc_card(sdmmc_card_t **out_card)
{
    esp_err_t ret = ESP_OK;
    bool host_inited = false;
    sdmmc_card_t *card = NULL;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.clk = SDMMC_CLK_PIN;
    slot_config.cmd = SDMMC_CMD_PIN;
    slot_config.d0 = SDMMC_D0_PIN;
    slot_config.d1 = SDMMC_D1_PIN;
    slot_config.d2 = SDMMC_D2_PIN;
    slot_config.d3 = SDMMC_D3_PIN;

    ESP_GOTO_ON_ERROR(sdmmc_host_init(), fail, TAG, "sdmmc_host_init failed");
    host_inited = true;

    ESP_GOTO_ON_ERROR(sdmmc_host_init_slot(host.slot, &slot_config), fail, TAG,
                      "sdmmc_host_init_slot failed");

    card = (sdmmc_card_t *)calloc(1, sizeof(sdmmc_card_t));
    ESP_GOTO_ON_FALSE(card != NULL, ESP_ERR_NO_MEM, fail, TAG,
                      "Failed to allocate sdmmc_card_t");

    ESP_GOTO_ON_ERROR(sdmmc_card_init(&host, card), fail, TAG,
                      "sdmmc_card_init failed");

    *out_card = card;
    return ESP_OK;

fail:
    if (card) {
        free(card);
    }
    if (host_inited) {
        if (host.flags & SDMMC_HOST_FLAG_DEINIT_ARG) {
            host.deinit_p(host.slot);
        } else if (host.deinit) {
            host.deinit();
        }
    }
    return ret;
}

esp_err_t usb_msc_start_for_sdcard(void)
{
#if !CONFIG_TINYUSB_MSC_ENABLED
    ESP_LOGE(TAG, "CONFIG_TINYUSB_MSC_ENABLED is disabled");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_usb_msc_running) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(usb_msc_init_sdmmc_card(&s_usb_msc_card), TAG,
                        "Failed to init SD card for USB MSC");

    const tinyusb_msc_sdmmc_config_t config_sdmmc = {
        .card = s_usb_msc_card,
        .callback_mount_changed = NULL,
        .callback_premount_changed = NULL,
        .mount_config = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 32 * 1024,
            .disk_status_check_enable = false,
            .use_one_fat = false,
        },
    };

    esp_err_t ret = tinyusb_msc_storage_init_sdmmc(&config_sdmmc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_msc_storage_init_sdmmc failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = NULL,
        .string_descriptor_count = 0,
        .external_phy = false,
#if (TUD_OPT_HIGH_SPEED)
        .fs_configuration_descriptor = NULL,
        .hs_configuration_descriptor = NULL,
        .qualifier_descriptor = NULL,
#else
        .configuration_descriptor = NULL,
#endif
        .self_powered = false,
        .vbus_monitor_io = -1,
    };

    ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install failed: %s", esp_err_to_name(ret));
        tinyusb_msc_storage_deinit();
        goto fail;
    }

    s_usb_msc_running = true;
    ESP_LOGI(TAG, "USB MSC started");
    return ESP_OK;

fail:
    usb_msc_deinit_host_and_card(s_usb_msc_card);
    s_usb_msc_card = NULL;
    return ret;
#endif
}

esp_err_t usb_msc_stop_for_sdcard(void)
{
#if !CONFIG_TINYUSB_MSC_ENABLED
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!s_usb_msc_running) {
        return ESP_OK;
    }

    // If host has ejected the disk, esp_tinyusb may have mounted storage locally.
    // Unmount it first so we can cleanly hand ownership back to /sdcard.
    if (!tinyusb_msc_storage_in_use_by_usb_host()) {
        esp_err_t unmount_ret = tinyusb_msc_storage_unmount();
        if (unmount_ret != ESP_OK) {
            ESP_LOGW(TAG, "tinyusb_msc_storage_unmount failed: %s", esp_err_to_name(unmount_ret));
        }
    }

    esp_err_t ret = tinyusb_driver_uninstall();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_uninstall failed: %s", esp_err_to_name(ret));
    }

    tinyusb_msc_storage_deinit();

    usb_msc_deinit_host_and_card(s_usb_msc_card);
    s_usb_msc_card = NULL;
    s_usb_msc_running = false;

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "USB MSC stopped");
    }
    return ret;
#endif
}

bool usb_msc_is_running(void)
{
    return s_usb_msc_running;
}

bool usb_msc_is_host_using_storage(void)
{
    if (!s_usb_msc_running) {
        return false;
    }

    return tinyusb_msc_storage_in_use_by_usb_host();
}
