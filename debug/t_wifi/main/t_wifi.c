#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "lwip/netdb.h"
#include "ping/ping_sock.h"

#include "wifi_credentials.h"

static const char *TAG = "t_wifi";
static EventGroupHandle_t s_events;

#define GOT_IP_BIT BIT0
#define DISCONNECTED_BIT BIT1
#define PING_DONE_BIT BIT2

static void ping_success(esp_ping_handle_t ping, void *arg)
{
    (void)arg;
    uint16_t seq = 0;
    uint32_t elapsed = 0;
    esp_ping_get_profile(ping, ESP_PING_PROF_SEQNO, &seq, sizeof(seq));
    esp_ping_get_profile(ping, ESP_PING_PROF_TIMEGAP, &elapsed, sizeof(elapsed));
    ESP_LOGI(TAG, "www.baidu.com: reply seq=%u time=%" PRIu32 " ms", seq, elapsed);
}

static void ping_timeout(esp_ping_handle_t ping, void *arg)
{
    (void)arg;
    uint16_t seq = 0;
    esp_ping_get_profile(ping, ESP_PING_PROF_SEQNO, &seq, sizeof(seq));
    ESP_LOGW(TAG, "www.baidu.com: timeout seq=%u", seq);
}

static void ping_end(esp_ping_handle_t ping, void *arg)
{
    (void)ping;
    (void)arg;
    xEventGroupSetBits(s_events, PING_DONE_BIT);
}

static void test_internet(const char *ssid, const char *profile)
{
    const char *host = "www.baidu.com";
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_DGRAM};
    struct addrinfo *result = NULL;
    ESP_LOGI(TAG, "%s / %s: resolving %s", ssid, profile, host);
    int dns_err = getaddrinfo(host, NULL, &hints, &result);
    if (dns_err != 0 || result == NULL) {
        ESP_LOGE(TAG, "%s / %s: DNS FAIL (error=%d)", ssid, profile, dns_err);
        if (result) freeaddrinfo(result);
        return;
    }

    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    const struct sockaddr_in *addr = (const struct sockaddr_in *)result->ai_addr;
    ip_addr_set_ip4_u32(&config.target_addr, addr->sin_addr.s_addr);
    freeaddrinfo(result);
    config.count = 4;
    config.interval_ms = 1000;
    config.timeout_ms = 1000;
    ESP_LOGI(TAG, "%s / %s: DNS OK %s -> %s; ping count=4",
             ssid, profile, host, ipaddr_ntoa(&config.target_addr));

    esp_ping_callbacks_t callbacks = {
        .on_ping_success = ping_success,
        .on_ping_timeout = ping_timeout,
        .on_ping_end = ping_end,
    };
    esp_ping_handle_t ping = NULL;
    xEventGroupClearBits(s_events, PING_DONE_BIT);
    esp_err_t err = esp_ping_new_session(&config, &callbacks, &ping);
    if (err == ESP_OK) err = esp_ping_start(ping);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s / %s: ping API failed: %s", ssid, profile, esp_err_to_name(err));
        if (ping) esp_ping_delete_session(ping);
        return;
    }
    // The finite session signals completion after all four replies/timeouts.
    xEventGroupWaitBits(s_events, PING_DONE_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    uint32_t sent = 0, received = 0;
    esp_ping_get_profile(ping, ESP_PING_PROF_REQUEST, &sent, sizeof(sent));
    esp_ping_get_profile(ping, ESP_PING_PROF_REPLY, &received, sizeof(received));
    ESP_LOGI(TAG, "%s / %s: ping %s sent=%" PRIu32 " received=%" PRIu32 " loss=%" PRIu32 "%%",
             ssid, profile, received ? "PASS" : "FAIL", sent, received,
             sent ? (sent - received) * 100 / sent : 100);
    if (!received) ESP_LOGW(TAG, "No ICMP replies; this alone does not prove Internet access is unavailable");
    esp_ping_delete_session(ping);
}

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

static bool scan_target(const char *ssid, wifi_ap_record_t *target)
{
    wifi_scan_config_t scan = {
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
        .show_hidden = true,
    };
    ESP_LOGI(TAG, "scanning for %s", ssid);
    esp_err_t err = esp_wifi_scan_start(&scan, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "scan failed: %s", esp_err_to_name(err));
        return false;
    }

    uint16_t count = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&count));
    if (count == 0) {
        ESP_LOGW(TAG, "scan found no APs");
        return false;
    }
    wifi_ap_record_t *aps = calloc(count, sizeof(*aps));
    if (!aps) {
        ESP_LOGE(TAG, "scan result allocation failed");
        esp_wifi_clear_ap_list();
        return false;
    }
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&count, aps));
    bool found = false;
    for (uint16_t i = 0; i < count; ++i) {
        const wifi_ap_record_t *ap = &aps[i];
        ESP_LOGI(TAG, "scan: ssid=%s auth=%d channel=%u rssi=%d bssid=" MACSTR,
                 ap->ssid, ap->authmode, ap->primary, ap->rssi, MAC2STR(ap->bssid));
        if (strcmp((const char *)ap->ssid, ssid) == 0) {
            ESP_LOGI(TAG, "target scan: auth=%d channel=%u rssi=%d bssid=" MACSTR,
                     ap->authmode, ap->primary, ap->rssi, MAC2STR(ap->bssid));
            if (!found || ap->rssi > target->rssi) {
                *target = *ap;
            }
            found = true;
        }
    }
    free(aps);
    if (!found) ESP_LOGW(TAG, "target SSID not found in %u APs", count);
    return found;
}

static void try_connect(const char *ssid, const char *password,
                        const char *profile, bool project_config,
                        const wifi_ap_record_t *target)
{
    wifi_config_t cfg = {0};
    size_t ssid_len = strlen(ssid);
    size_t pwd_len = strlen(password);
    if (ssid_len == 0 || ssid_len >= sizeof(cfg.sta.ssid) ||
        pwd_len < 8 || pwd_len >= sizeof(cfg.sta.password)) {
        ESP_LOGE(TAG, "invalid credential length: ssid=%u password=%u",
                 (unsigned)ssid_len, (unsigned)pwd_len);
        return;
    }
    memcpy(cfg.sta.ssid, ssid, ssid_len + 1);
    memcpy(cfg.sta.password, password, pwd_len + 1);
    cfg.sta.channel = target->primary;
    cfg.sta.bssid_set = true;
    memcpy(cfg.sta.bssid, target->bssid, sizeof(cfg.sta.bssid));

    if (project_config) {
        cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        cfg.sta.pmf_cfg.capable = true;
        cfg.sta.pmf_cfg.required = false;
        cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    }

    ESP_LOGI(TAG, "%s: connecting to %s channel=%u bssid=" MACSTR " (password length=%u)",
             profile, ssid, target->primary, MAC2STR(target->bssid),
             (unsigned)pwd_len);
    xEventGroupClearBits(s_events, GOT_IP_BIT | DISCONNECTED_BIT);
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err == ESP_OK) err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: API failed: %s", profile, esp_err_to_name(err));
        return;
    }

    EventBits_t bits = xEventGroupWaitBits(s_events, GOT_IP_BIT | DISCONNECTED_BIT,
                                          pdTRUE, pdFALSE, pdMS_TO_TICKS(15000));
    if (bits & GOT_IP_BIT) {
        ESP_LOGI(TAG, "%s / %s: PASS", ssid, profile);
        test_internet(ssid, profile);
    }
    else if (bits & DISCONNECTED_BIT) ESP_LOGE(TAG, "%s / %s: FAIL (disconnected)", ssid, profile);
    else ESP_LOGE(TAG, "%s / %s: FAIL (15 s timeout)", ssid, profile);

    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));
    xEventGroupClearBits(s_events, GOT_IP_BIT | DISCONNECTED_BIT);
}

static void test_network(const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "network test started: %s", ssid);
    wifi_ap_record_t target = {0};
    if (!scan_target(ssid, &target)) {
        ESP_LOGE(TAG, "%s: skipped; target AP unavailable", ssid);
        return;
    }
    try_connect(ssid, password, "DeepStoa config", true, &target);
    try_connect(ssid, password, "ESP-IDF defaults", false, &target);
    ESP_LOGI(TAG, "network test finished: %s", ssid);
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

    int8_t tx_power = 0;
    ESP_ERROR_CHECK(esp_wifi_get_max_tx_power(&tx_power));
    ESP_LOGI(TAG, "Wi-Fi startup TX power limit: %d x 0.25 dBm", tx_power);
    // Request the documented 18 dBm level in 0.25 dBm units.
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(72));
    ESP_ERROR_CHECK(esp_wifi_get_max_tx_power(&tx_power));
    ESP_LOGI(TAG, "Wi-Fi TX power limit: %d x 0.25 dBm (requested 18 dBm)", tx_power);

    test_network(TEST_WIFI_SSID, TEST_WIFI_PASSWORD);
    test_network(TEST_ROUTER_SSID, TEST_ROUTER_PASSWORD);
    ESP_LOGI(TAG, "test finished");
}
