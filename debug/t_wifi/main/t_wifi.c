#include <string.h>
#include <stdlib.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

#include "wifi_credentials.h"

static const char *TAG = "t_wifi";
static EventGroupHandle_t s_events;

#define GOT_IP_BIT BIT0
#define DISCONNECTED_BIT BIT1

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "associated; waiting for IP");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *ev = data;
        ESP_LOGW(TAG, "disconnected: reason=%u rssi=%d bssid=" MACSTR,
                 ev->reason, ev->rssi, MAC2STR(ev->bssid));
        xEventGroupSetBits(s_events, DISCONNECTED_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ev = data;
        ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        xEventGroupSetBits(s_events, GOT_IP_BIT);
    }
}

static void scan_target(void)
{
    wifi_scan_config_t scan = {
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
        .show_hidden = true,
    };
    esp_err_t err = esp_wifi_scan_start(&scan, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "scan failed: %s", esp_err_to_name(err));
        return;
    }

    uint16_t count = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&count));
    if (count == 0) {
        ESP_LOGW(TAG, "scan found no APs");
        return;
    }
    wifi_ap_record_t *aps = calloc(count, sizeof(*aps));
    if (!aps) {
        ESP_LOGE(TAG, "scan result allocation failed");
        return;
    }
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&count, aps));
    bool found = false;
    for (uint16_t i = 0; i < count; ++i) {
        const wifi_ap_record_t *ap = &aps[i];
        if (strcmp((const char *)ap->ssid, TEST_WIFI_SSID) == 0) {
            ESP_LOGI(TAG, "target scan: auth=%d channel=%u rssi=%d bssid=" MACSTR,
                     ap->authmode, ap->primary, ap->rssi, MAC2STR(ap->bssid));
            found = true;
        }
    }
    free(aps);
    if (!found) ESP_LOGW(TAG, "target SSID not found in %u APs", count);
}

static void try_connect(const char *profile, bool project_config)
{
    wifi_config_t cfg = {0};
    size_t ssid_len = strlen(TEST_WIFI_SSID);
    size_t pwd_len = strlen(TEST_WIFI_PASSWORD);
    if (ssid_len == 0 || ssid_len >= sizeof(cfg.sta.ssid) ||
        pwd_len < 8 || pwd_len >= sizeof(cfg.sta.password)) {
        ESP_LOGE(TAG, "invalid credential length: ssid=%u password=%u",
                 (unsigned)ssid_len, (unsigned)pwd_len);
        return;
    }
    memcpy(cfg.sta.ssid, TEST_WIFI_SSID, ssid_len + 1);
    memcpy(cfg.sta.password, TEST_WIFI_PASSWORD, pwd_len + 1);

    if (project_config) {
        cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        cfg.sta.pmf_cfg.capable = true;
        cfg.sta.pmf_cfg.required = false;
        cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    }

    ESP_LOGI(TAG, "%s: connecting to %s (password length=%u)",
             profile, TEST_WIFI_SSID, (unsigned)pwd_len);
    xEventGroupClearBits(s_events, GOT_IP_BIT | DISCONNECTED_BIT);
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err == ESP_OK) err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: API failed: %s", profile, esp_err_to_name(err));
        return;
    }

    EventBits_t bits = xEventGroupWaitBits(s_events, GOT_IP_BIT | DISCONNECTED_BIT,
                                          pdTRUE, pdFALSE, pdMS_TO_TICKS(15000));
    if (bits & GOT_IP_BIT) ESP_LOGI(TAG, "%s: PASS", profile);
    else if (bits & DISCONNECTED_BIT) ESP_LOGE(TAG, "%s: FAIL (disconnected)", profile);
    else ESP_LOGE(TAG, "%s: FAIL (15 s timeout)", profile);

    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));
    xEventGroupClearBits(s_events, GOT_IP_BIT | DISCONNECTED_BIT);
}

void app_main(void)
{
    s_events = xEventGroupCreate();
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_start());

    scan_target();
    try_connect("DeepStoa config", true);
    try_connect("ESP-IDF defaults", false);
    ESP_LOGI(TAG, "test finished");
}
