#include "lxj_legacy_apps.h"

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "app_manager.h"
#include "alarm_service.h"
#include "fs_control.h"
#include "sd_control.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "lvgl.h"
#include "wifi_manager.h"
#include "lxj_weather_bmp.h"
#include "battery.h"
#include "time_service.h"
#include "fiction_epub_adapter.h"
#include "fiction_epub_rich_adapter.h"
#include "app.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "lxj_apps";
static lv_obj_t *s_root;
static lv_obj_t *s_network_status;
static lv_obj_t *s_network_list;
static lv_obj_t *s_alarm_list;
static lv_obj_t *s_settings_status;
static lv_obj_t *s_fiction_list;
static lv_obj_t *s_fiction_detail;
static FILE *s_fiction_file;
static lv_obj_t *s_fiction_toc_panel;
static lv_obj_t *s_fiction_image;
static lv_image_dsc_t s_fiction_image_dsc;
static uint8_t *s_fiction_image_data;
static char s_fiction_paths[16][256];
static uint32_t s_fiction_path_count;
static uint32_t s_fiction_selected;
static long s_fiction_offset;
static lv_obj_t *s_mistake_list;
static lv_obj_t *s_mistake_detail;
static lv_obj_t *s_weather_status;
static lv_obj_t *s_weather_image;
static lv_obj_t *s_weather_city;
static lv_obj_t *s_pomodoro_time;
static lv_obj_t *s_pomodoro_state;
static lv_timer_t *s_pomodoro_timer;
static uint32_t s_pomodoro_remaining = 25U * 60U;
static bool s_pomodoro_running;
static int64_t s_pomodoro_last_tick_us;
static bool s_fiction_rich_active;

typedef struct {
    const char *name;
    const char *code;
} lxj_weather_city_t;

static const lxj_weather_city_t s_weather_cities[] = {
    { "Beijing", "101010100" },
    { "Shanghai", "101020100" },
    { "Shenzhen", "101280601" },
};

static QueueHandle_t s_weather_queue;
static TaskHandle_t s_weather_worker;
static volatile bool s_weather_busy;
static struct {
    esp_err_t result;
    char city[48];
    char type[32];
    char temperature[16];
    char humidity[16];
    char quality[32];
    char high[16];
    char low[16];
} s_weather_result;

typedef enum {
    LXJ_NETWORK_SCAN = 0,
    LXJ_NETWORK_RECONNECT,
    LXJ_NETWORK_DISCONNECT,
} lxj_network_request_t;

static QueueHandle_t s_network_queue;
static TaskHandle_t s_network_worker;
static volatile bool s_network_busy;
static struct {
    lxj_network_request_t request;
    esp_err_t result;
    wifi_ap_record_t records[16];
    uint16_t found;
} s_network_result;

#define LXJ_MISTAKE_MAX_CARDS 32U
#define LXJ_MISTAKE_FIELD_LEN 160U

typedef struct {
    char subject[LXJ_MISTAKE_FIELD_LEN];
    char title[LXJ_MISTAKE_FIELD_LEN];
    char question[LXJ_MISTAKE_FIELD_LEN];
    char student_answer[LXJ_MISTAKE_FIELD_LEN];
    char correct_answer[LXJ_MISTAKE_FIELD_LEN];
    char knowledge_point[LXJ_MISTAKE_FIELD_LEN];
    char error_type[LXJ_MISTAKE_FIELD_LEN];
    char explanation[LXJ_MISTAKE_FIELD_LEN];
    bool review_pending;
} lxj_mistake_card_t;

static lxj_mistake_card_t s_mistake_cards[LXJ_MISTAKE_MAX_CARDS];
static uint32_t s_mistake_card_count;
static uint32_t s_mistake_selected;
static void lxj_legacy_close(lv_event_t *event);
static void lxj_legacy_stop(void);
static bool lxj_legacy_back(void);
static void lxj_alarm_toggle(lv_event_t *event);
static void lxj_mistake_select(lv_event_t *event);
static void lxj_mistake_mark_reviewed(lv_event_t *event);
static void lxj_mistake_previous(lv_event_t *event);
static void lxj_mistake_next(lv_event_t *event);
static void lxj_fiction_show_file(uint32_t index);
static void lxj_fiction_select(lv_event_t *event);
static void lxj_fiction_page(lv_event_t *event);
static void lxj_fiction_show_toc(lv_event_t *event);
static void lxj_fiction_toc_select(lv_event_t *event);
static void lxj_fiction_toc_close(lv_event_t *event);
static void lxj_fiction_refresh_image(void);

static void lxj_fiction_refresh_image(void)
{
    if (!s_fiction_rich_active || !s_root || !lv_obj_is_valid(s_root)) return;
    if (s_fiction_image && lv_obj_is_valid(s_fiction_image)) {
        lv_obj_delete(s_fiction_image);
        s_fiction_image = NULL;
    }
    if (s_fiction_image_data) {
        fiction_epub_rich_free_page_image(s_fiction_image_data);
        s_fiction_image_data = NULL;
    }

    size_t size = 0;
    if (!fiction_epub_rich_read_page_image(&s_fiction_image_data, &size) ||
        !s_fiction_image_data || size == 0) return;

    memset(&s_fiction_image_dsc, 0, sizeof(s_fiction_image_dsc));
    s_fiction_image_dsc.header.cf = LV_COLOR_FORMAT_RAW;
    s_fiction_image_dsc.header.w = 1;
    s_fiction_image_dsc.header.h = 1;
    s_fiction_image_dsc.data_size = size;
    s_fiction_image_dsc.data = s_fiction_image_data;
    s_fiction_image = lv_image_create(s_root);
    lv_image_set_src(s_fiction_image, &s_fiction_image_dsc);
    lv_obj_set_size(s_fiction_image, 260, 180);
    lv_obj_align(s_fiction_image, LV_ALIGN_TOP_RIGHT, -20, 48);
}

#define LXJ_FICTION_PAGE_BYTES 1200U

static bool lxj_fiction_is_epub(const char *path)
{
    const char *extension = strrchr(path, '.');
    return extension && (!strcasecmp(extension, ".epub"));
}

static bool lxj_fiction_is_text(const char *path)
{
    const char *extension = strrchr(path, '.');
    return extension && (!strcasecmp(extension, ".txt"));
}

static const char *lxj_weather_json_string(const cJSON *object, const char *key)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) && value->valuestring ? value->valuestring : "";
}

static const lv_image_dsc_t *lxj_weather_icon(const char *type)
{
    if (strstr(type, "雷") || strstr(type, "thunder")) return &thunderstorm;
    if (strstr(type, "暴") || strstr(type, "storm")) return &rainstorm;
    if (strstr(type, "大雨") || strstr(type, "heavy")) return &heavy_rain;
    if (strstr(type, "中雨") || strstr(type, "moderate")) return &moderate_rain;
    if (strstr(type, "小雨") || strstr(type, "light")) return &light_rain;
    if (strstr(type, "雨") || strstr(type, "rain")) return &light_rain;
    if (strstr(type, "雪") || strstr(type, "snow")) return &snow;
    if (strstr(type, "雾") || strstr(type, "霾") || strstr(type, "haze")) return &haze;
    if (strstr(type, "沙") || strstr(type, "dust")) return &dust_storm;
    if (strstr(type, "风") || strstr(type, "gale")) return &gale;
    if (strstr(type, "阴") || strstr(type, "overcast")) return &overcast;
    if (strstr(type, "云") || strstr(type, "cloud")) return &partly_cloudy;
    return &clear;
}

static bool lxj_weather_fetch(const char *city_code)
{
    char url[128];
    snprintf(url, sizeof(url), "http://t.weather.sojson.com/api/weather/city/%s", city_code);
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;

    char *json = malloc(8192);
    if (!json) {
        esp_http_client_cleanup(client);
        return false;
    }
    size_t length = 0;
    bool ok = false;
    if (esp_http_client_open(client, 0) == ESP_OK &&
        esp_http_client_fetch_headers(client) >= 0 &&
        esp_http_client_get_status_code(client) >= 200 &&
        esp_http_client_get_status_code(client) < 300) {
        while (length + 1 < 8192) {
            int read = esp_http_client_read(client, json + length, 8191 - length);
            if (read <= 0) break;
            length += (size_t)read;
        }
        json[length] = '\0';
        cJSON *root = cJSON_Parse(json);
        cJSON *city_info = root ? cJSON_GetObjectItem(root, "cityInfo") : NULL;
        cJSON *data = root ? cJSON_GetObjectItem(root, "data") : NULL;
        cJSON *forecast = data ? cJSON_GetObjectItem(data, "forecast") : NULL;
        cJSON *today = cJSON_IsArray(forecast) ? cJSON_GetArrayItem(forecast, 0) : NULL;
        if (root && city_info && data && today) {
            snprintf(s_weather_result.city, sizeof(s_weather_result.city), "%s",
                     lxj_weather_json_string(city_info, "city"));
            snprintf(s_weather_result.temperature, sizeof(s_weather_result.temperature), "%s",
                     lxj_weather_json_string(data, "wendu"));
            snprintf(s_weather_result.humidity, sizeof(s_weather_result.humidity), "%s",
                     lxj_weather_json_string(data, "shidu"));
            snprintf(s_weather_result.quality, sizeof(s_weather_result.quality), "%s",
                     lxj_weather_json_string(data, "quality"));
            snprintf(s_weather_result.type, sizeof(s_weather_result.type), "%s",
                     lxj_weather_json_string(today, "type"));
            snprintf(s_weather_result.high, sizeof(s_weather_result.high), "%s",
                     lxj_weather_json_string(today, "high"));
            snprintf(s_weather_result.low, sizeof(s_weather_result.low), "%s",
                     lxj_weather_json_string(today, "low"));
            ok = s_weather_result.city[0] != '\0' && s_weather_result.type[0] != '\0';
        }
        cJSON_Delete(root);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(json);
    return ok;
}

static void lxj_weather_apply_result(void *arg);

static void lxj_weather_worker_task(void *arg)
{
    (void)arg;
    uint8_t city_index;
    for (;;) {
        if (xQueueReceive(s_weather_queue, &city_index, portMAX_DELAY) != pdTRUE) continue;
        memset(&s_weather_result, 0, sizeof(s_weather_result));
        s_weather_result.result = lxj_weather_fetch(s_weather_cities[city_index].code)
                                      ? ESP_OK : ESP_FAIL;
        lv_async_call(lxj_weather_apply_result, NULL);
    }
}

static void lxj_network_apply_result(void *arg);

static void lxj_network_worker_task(void *arg)
{
    (void)arg;
    lxj_network_request_t request;
    for (;;) {
        if (xQueueReceive(s_network_queue, &request, portMAX_DELAY) != pdTRUE) continue;
        memset(&s_network_result, 0, sizeof(s_network_result));
        s_network_result.request = request;
        switch (request) {
        case LXJ_NETWORK_SCAN:
            s_network_result.result = wifi_scan(s_network_result.records, 16,
                                                 &s_network_result.found, 0);
            break;
        case LXJ_NETWORK_RECONNECT:
            s_network_result.result = wifi_reconnect();
            break;
        case LXJ_NETWORK_DISCONNECT:
            s_network_result.result = wifi_disconnect();
            break;
        }
        lv_async_call(lxj_network_apply_result, NULL);
    }
}

static void lxj_network_submit(lxj_network_request_t request)
{
    if (!s_network_queue || s_network_busy) return;
    if (xQueueSend(s_network_queue, &request, 0) != pdTRUE) return;
    s_network_busy = true;
    if (s_network_status && lv_obj_is_valid(s_network_status)) {
        lv_label_set_text(s_network_status, "Working...");
    }
}

static void lxj_mistake_copy_field(char *dst, size_t dst_size, const char *src)
{
    if (!src) src = "";
    snprintf(dst, dst_size, "%s", src);
}

static void lxj_network_refresh(void)
{
    if (!s_network_status || !lv_obj_is_valid(s_network_status)) return;
    wifi_info_t info = {0};
    if (wifi_get_info(&info) != ESP_OK || !info.is_connected) {
        lv_label_set_text(s_network_status, "Wi-Fi disconnected");
    } else {
        lv_label_set_text_fmt(s_network_status,
                              "Connected: %s\nIP: %s\nRSSI: %d dBm\nChannel: %u",
                              info.ssid, info.ip, info.rssi, info.channel);
    }
}

static void lxj_network_show_scan_results(void)
{
    if (!s_network_list || !lv_obj_is_valid(s_network_list)) return;
    lv_obj_clean(s_network_list);
    uint16_t count = s_network_result.found;
    if (s_network_result.result != ESP_OK || count == 0U) {
        lv_obj_t *empty = lv_label_create(s_network_list);
        lv_label_set_text(empty, s_network_result.result == ESP_OK
                                  ? "No networks found"
                                  : "Scan failed");
        return;
    }
    for (uint16_t i = 0; i < count; ++i) {
        wifi_ap_record_t *record = &s_network_result.records[i];
        if (record->ssid[0] == '\0') continue;
        lv_obj_t *row = lv_label_create(s_network_list);
        lv_label_set_text_fmt(row, "%s  (%d dBm, ch %u)%s",
                              (const char *)record->ssid, record->rssi,
                              record->primary,
                              record->authmode == WIFI_AUTH_OPEN ? "  Open" : "  Secured");
    }
}

static void lxj_network_apply_result(void *arg)
{
    (void)arg;
    s_network_busy = false;
    if (!s_network_status || !lv_obj_is_valid(s_network_status)) return;
    if (s_network_result.request == LXJ_NETWORK_SCAN) {
        lxj_network_show_scan_results();
    }
    lxj_network_refresh();
}

static void lxj_network_refresh_event(lv_event_t *event)
{
    (void)event;
    lxj_network_refresh();
}

static void lxj_network_scan_event(lv_event_t *event)
{
    (void)event;
    lxj_network_submit(LXJ_NETWORK_SCAN);
}

static void lxj_network_reconnect_event(lv_event_t *event)
{
    (void)event;
    lxj_network_submit(LXJ_NETWORK_RECONNECT);
}

static void lxj_network_disconnect_event(lv_event_t *event)
{
    (void)event;
    lxj_network_submit(LXJ_NETWORK_DISCONNECT);
}

static void lxj_network_start(lv_obj_t *root, lv_group_t *group)
{
    (void)group;
    s_root = lv_obj_create(root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_root);
    lv_obj_set_style_pad_all(s_root, 24, 0);
    lv_obj_t *heading = lv_label_create(s_root);
    lv_label_set_text(heading, "LXJ Network");
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 16);
    s_network_status = lv_label_create(s_root);
    lv_obj_align(s_network_status, LV_ALIGN_TOP_LEFT, 24, 100);

    s_network_list = lv_obj_create(s_root);
    lv_obj_set_size(s_network_list, LV_PCT(100), 150);
    lv_obj_align(s_network_list, LV_ALIGN_TOP_LEFT, 24, 170);
    lv_obj_set_flex_flow(s_network_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_network_list, 4, 0);

    if (!s_network_queue) {
        s_network_queue = xQueueCreate(1, sizeof(lxj_network_request_t));
        xTaskCreate(lxj_network_worker_task, "lxj_wifi", 4096, NULL, 4,
                    &s_network_worker);
    }

    lv_obj_t *refresh = lv_button_create(s_root);
    lv_obj_set_size(refresh, 140, 50);
    lv_obj_align(refresh, LV_ALIGN_BOTTOM_LEFT, 24, -18);
    lv_obj_add_event_cb(refresh, lxj_network_refresh_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *refresh_label = lv_label_create(refresh);
    lv_label_set_text(refresh_label, "Refresh");
    lv_obj_center(refresh_label);

    lv_obj_t *scan = lv_button_create(s_root);
    lv_obj_set_size(scan, 120, 50);
    lv_obj_align(scan, LV_ALIGN_BOTTOM_LEFT, 174, -18);
    lv_obj_add_event_cb(scan, lxj_network_scan_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_label = lv_label_create(scan);
    lv_label_set_text(scan_label, "Scan");
    lv_obj_center(scan_label);

    lv_obj_t *reconnect = lv_button_create(s_root);
    lv_obj_set_size(reconnect, 130, 50);
    lv_obj_align(reconnect, LV_ALIGN_BOTTOM_LEFT, 304, -18);
    lv_obj_add_event_cb(reconnect, lxj_network_reconnect_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *reconnect_label = lv_label_create(reconnect);
    lv_label_set_text(reconnect_label, "Reconnect");
    lv_obj_center(reconnect_label);

    lv_obj_t *disconnect = lv_button_create(s_root);
    lv_obj_set_size(disconnect, 130, 50);
    lv_obj_align(disconnect, LV_ALIGN_BOTTOM_LEFT, 444, -18);
    lv_obj_add_event_cb(disconnect, lxj_network_disconnect_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *disconnect_label = lv_label_create(disconnect);
    lv_label_set_text(disconnect_label, "Disconnect");
    lv_obj_center(disconnect_label);

    lv_obj_t *close = lv_button_create(s_root);
    lv_obj_set_size(close, 140, 50);
    lv_obj_align(close, LV_ALIGN_BOTTOM_RIGHT, -24, -18);
    lv_obj_add_event_cb(close, lxj_legacy_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_label = lv_label_create(close);
    lv_label_set_text(close_label, "Close");
    lv_obj_center(close_label);
    lxj_network_refresh();
}

static application_t s_lxj_network_application = {
    .name = "LXJ Network",
    .icon = NULL,
    .start_func = lxj_network_start,
    .stop_func = lxj_legacy_stop,
    .back_func = lxj_legacy_back,
    .category = APP_CATEGORY_SYSTEM,
};

void lxj_network_init(void)
{
    app_manager_add_application(&s_lxj_network_application);
    ESP_LOGI(TAG, "registered LXJ Network");
}

static void lxj_alarm_refresh(void)
{
    if (!s_alarm_list || !lv_obj_is_valid(s_alarm_list)) return;
    lv_obj_clean(s_alarm_list);
    uint8_t count = alarm_service_count();
    if (count == 0U) {
        lv_obj_t *empty = lv_label_create(s_alarm_list);
        lv_label_set_text(empty, "No alarms configured");
        return;
    }
    for (uint8_t i = 0; i < count; ++i) {
        const alarm_t *alarm = alarm_service_get(i);
        if (!alarm) continue;
        lv_obj_t *row = lv_button_create(s_alarm_list);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 48);
        lv_obj_add_event_cb(row, lxj_alarm_toggle, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
        lv_obj_t *label = lv_label_create(row);
        lv_label_set_text_fmt(label, "%02u:%02u  %s",
                              (unsigned)alarm->hour, (unsigned)alarm->minute,
                              alarm->enabled ? "ON" : "OFF");
        lv_obj_center(label);
    }
}

static void lxj_alarm_toggle(lv_event_t *event)
{
    uint8_t index = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    alarm_service_toggle(index);
    lxj_alarm_refresh();
}

static void lxj_alarm_start(lv_obj_t *root, lv_group_t *group)
{
    (void)group;
    s_root = lv_obj_create(root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_root);
    lv_obj_set_style_pad_all(s_root, 20, 0);
    lv_obj_t *heading = lv_label_create(s_root);
    lv_label_set_text(heading, "LXJ Alarm");
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 10);
    s_alarm_list = lv_obj_create(s_root);
    lv_obj_set_size(s_alarm_list, LV_PCT(100), 230);
    lv_obj_align(s_alarm_list, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_flex_flow(s_alarm_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_alarm_list, 6, 0);
    lxj_alarm_refresh();

    lv_obj_t *close = lv_button_create(s_root);
    lv_obj_set_size(close, 130, 48);
    lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(close, lxj_legacy_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_label = lv_label_create(close);
    lv_label_set_text(close_label, "Close");
    lv_obj_center(close_label);
}

static application_t s_lxj_alarm_application = {
    .name = "LXJ Alarm",
    .icon = NULL,
    .start_func = lxj_alarm_start,
    .stop_func = lxj_legacy_stop,
    .back_func = lxj_legacy_back,
    .category = APP_CATEGORY_TOOLS,
};

void lxj_alarm_init(void)
{
    app_manager_add_application(&s_lxj_alarm_application);
    ESP_LOGI(TAG, "registered LXJ Alarm");
}

static void lxj_settings_refresh(void)
{
    if (!s_settings_status || !lv_obj_is_valid(s_settings_status)) return;
    wifi_info_t info = {0};
    bool connected = wifi_get_info(&info) == ESP_OK && info.is_connected;
    uint64_t total = 0;
    uint64_t used = 0;
    fs_control_get_usage(&total, &used);
    int battery = battery_get_percent();
    lv_label_set_text_fmt(s_settings_status,
                          "Wi-Fi: %s\n"
                          "Storage: %llu / %llu KB\n"
                          "Battery: %s%s\n"
                          "Time: %02d:%02d:%02d (%s)\n"
                          "SNTP: %s\n"
                          "LXJ settings are isolated from Settings",
                          connected ? info.ssid : "disconnected",
                          (unsigned long long)(used / 1024U),
                          (unsigned long long)(total / 1024U),
                          battery >= 0 ? "available" : "not initialized",
                          battery >= 0 && battery_is_charging() ? " (charging)" : "",
                          time_service_get_hour(), time_service_get_minute(),
                          time_service_get_second(),
                          time_service_get_format_24h() ? "24h" : "12h",
                          time_service_is_synced() ? "synced" : "not synced");
}

static void lxj_settings_refresh_event(lv_event_t *event)
{
    (void)event;
    lxj_settings_refresh();
}

static void lxj_settings_toggle_format_event(lv_event_t *event)
{
    (void)event;
    time_service_set_format_24h(!time_service_get_format_24h());
    lxj_settings_refresh();
}

static void lxj_settings_sync_time_event(lv_event_t *event)
{
    (void)event;
    time_service_sync_sntp();
    lxj_settings_refresh();
}

static void lxj_settings_start(lv_obj_t *root, lv_group_t *group)
{
    (void)group;
    s_root = lv_obj_create(root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_root);
    lv_obj_set_style_pad_all(s_root, 24, 0);
    lv_obj_t *heading = lv_label_create(s_root);
    lv_label_set_text(heading, "LXJ Settings");
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 16);
    s_settings_status = lv_label_create(s_root);
    lv_obj_align(s_settings_status, LV_ALIGN_TOP_LEFT, 24, 90);
    lxj_settings_refresh();

    lv_obj_t *refresh = lv_button_create(s_root);
    lv_obj_set_size(refresh, 130, 48);
    lv_obj_align(refresh, LV_ALIGN_BOTTOM_LEFT, 24, -18);
    lv_obj_add_event_cb(refresh, lxj_settings_refresh_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *refresh_label = lv_label_create(refresh);
    lv_label_set_text(refresh_label, "Refresh");
    lv_obj_center(refresh_label);

    lv_obj_t *format = lv_button_create(s_root);
    lv_obj_set_size(format, 140, 48);
    lv_obj_align(format, LV_ALIGN_BOTTOM_LEFT, 164, -18);
    lv_obj_add_event_cb(format, lxj_settings_toggle_format_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *format_label = lv_label_create(format);
    lv_label_set_text(format_label, "12/24h");
    lv_obj_center(format_label);

    lv_obj_t *sync = lv_button_create(s_root);
    lv_obj_set_size(sync, 140, 48);
    lv_obj_align(sync, LV_ALIGN_BOTTOM_LEFT, 314, -18);
    lv_obj_add_event_cb(sync, lxj_settings_sync_time_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sync_label = lv_label_create(sync);
    lv_label_set_text(sync_label, "Sync time");
    lv_obj_center(sync_label);

    lv_obj_t *close = lv_button_create(s_root);
    lv_obj_set_size(close, 130, 48);
    lv_obj_align(close, LV_ALIGN_BOTTOM_RIGHT, -24, -18);
    lv_obj_add_event_cb(close, lxj_legacy_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_label = lv_label_create(close);
    lv_label_set_text(close_label, "Close");
    lv_obj_center(close_label);
}

static application_t s_lxj_settings_application = {
    .name = "LXJ Settings",
    .icon = NULL,
    .start_func = lxj_settings_start,
    .stop_func = lxj_legacy_stop,
    .back_func = lxj_legacy_back,
    .category = APP_CATEGORY_SYSTEM,
};

void lxj_settings_init(void)
{
    app_manager_add_application(&s_lxj_settings_application);
    ESP_LOGI(TAG, "registered LXJ Settings");
}

static void lxj_fiction_refresh(void)
{
    if (!s_fiction_list || !lv_obj_is_valid(s_fiction_list)) return;
    lv_obj_clean(s_fiction_list);
    s_fiction_path_count = 0;
    s_fiction_selected = 0;
    s_fiction_offset = 0;
    if (s_fiction_file) {
        fclose(s_fiction_file);
        s_fiction_file = NULL;
    }
    fiction_epub_rich_close();
    s_fiction_rich_active = false;
    if (s_fiction_detail && lv_obj_is_valid(s_fiction_detail)) {
        lv_label_set_text(s_fiction_detail, "Select an EPUB or TXT book.");
    }
    if (!fs_control_is_mounted()) {
        lv_obj_t *label = lv_label_create(s_fiction_list);
        lv_label_set_text(label, "SD card is not mounted");
        return;
    }
    file_list_t files = {0};
    if (!sd_control_get_dir_list("/sdcard/fiction", &files, NULL)) {
        lv_obj_t *label = lv_label_create(s_fiction_list);
        lv_label_set_text(label, "No fiction directory");
        return;
    }
    for (uint32_t i = 0; i < files.count; ++i) {
        const char *name = files.nodes[i].name;
        if (files.nodes[i].type != NODE_TYPE_FILE ||
            (!lxj_fiction_is_epub(name) && !lxj_fiction_is_text(name)) ||
            s_fiction_path_count >= 16U) continue;
        snprintf(s_fiction_paths[s_fiction_path_count], sizeof(s_fiction_paths[0]),
                 "/sdcard/fiction/%.239s", name);
        lv_obj_t *button = lv_button_create(s_fiction_list);
        lv_obj_set_width(button, LV_PCT(100));
        lv_obj_add_event_cb(button, lxj_fiction_select, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)s_fiction_path_count);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, name);
        lv_obj_center(label);
        ++s_fiction_path_count;
    }
    if (s_fiction_path_count == 0U) {
        lv_obj_t *label = lv_label_create(s_fiction_list);
        lv_label_set_text(label, "No EPUB/TXT books found");
    }
    sd_control_free_dir_list(&files);
}

static void lxj_fiction_show_file(uint32_t index)
{
    if (index >= s_fiction_path_count || !s_fiction_detail ||
        !lv_obj_is_valid(s_fiction_detail)) return;
    s_fiction_selected = index;
    s_fiction_offset = 0;
    if (s_fiction_file) {
        fclose(s_fiction_file);
        s_fiction_file = NULL;
    }
    if (lxj_fiction_is_epub(s_fiction_paths[index])) {
        if (fiction_epub_rich_open(s_fiction_paths[index])) {
            s_fiction_rich_active = true;
            char page[LXJ_FICTION_PAGE_BYTES + 1U];
            if (fiction_epub_rich_read_page(page, sizeof(page))) {
                lv_label_set_text(s_fiction_detail, page);
                lxj_fiction_refresh_image();
                return;
            }
            fiction_epub_rich_close();
            s_fiction_rich_active = false;
        }
        char cache_path[256];
        char error[128];
        if (!fiction_epub_prepare_cache(s_fiction_paths[index], cache_path,
                                        sizeof(cache_path), error, sizeof(error))) {
            lv_label_set_text_fmt(s_fiction_detail, "EPUB conversion failed: %s",
                                  error[0] ? error : "unknown error");
            return;
        }
        s_fiction_file = fopen(cache_path, "rb");
        if (!s_fiction_file) {
            lv_label_set_text(s_fiction_detail, "EPUB cache could not be opened");
            return;
        }
        lv_label_set_text(s_fiction_detail, "EPUB converted. Use Previous/Next to read.");
    } else {
        s_fiction_file = fopen(s_fiction_paths[index], "rb");
        if (!s_fiction_file) {
            lv_label_set_text(s_fiction_detail, "Unable to open selected book");
            return;
        }
    }
    char page[LXJ_FICTION_PAGE_BYTES + 1U];
    size_t count = fread(page, 1, LXJ_FICTION_PAGE_BYTES, s_fiction_file);
    page[count] = '\0';
    lv_label_set_text(s_fiction_detail, count ? page : "Empty book");
}

static void lxj_fiction_select(lv_event_t *event)
{
    uint32_t index = (uint32_t)(uintptr_t)lv_event_get_user_data(event);
    lxj_fiction_show_file(index);
}

static void lxj_fiction_page(lv_event_t *event)
{
    int32_t direction = (int32_t)(intptr_t)lv_event_get_user_data(event);
    if (s_fiction_rich_active) {
        bool moved = direction > 0 ? fiction_epub_rich_next()
                                   : fiction_epub_rich_prev();
        char page[LXJ_FICTION_PAGE_BYTES + 1U];
        if (moved && fiction_epub_rich_read_page(page, sizeof(page))) {
            lv_label_set_text(s_fiction_detail, page);
            lxj_fiction_refresh_image();
        }
        return;
    }
    if (!s_fiction_file || !s_fiction_detail || !lv_obj_is_valid(s_fiction_detail)) return;
    long target = s_fiction_offset + (direction * (long)LXJ_FICTION_PAGE_BYTES);
    if (target < 0) target = 0;
    if (fseek(s_fiction_file, 0, SEEK_END) != 0) return;
    long file_size = ftell(s_fiction_file);
    if (file_size < 0) return;
    if (target > file_size) target = file_size;
    if (fseek(s_fiction_file, target, SEEK_SET) != 0) return;
    char page[LXJ_FICTION_PAGE_BYTES + 1U];
    size_t count = fread(page, 1, LXJ_FICTION_PAGE_BYTES, s_fiction_file);
    page[count] = '\0';
    s_fiction_offset = target;
    lv_label_set_text(s_fiction_detail, count ? page : "End of book");
}

static void lxj_fiction_toc_close(lv_event_t *event)
{
    (void)event;
    if (s_fiction_toc_panel && lv_obj_is_valid(s_fiction_toc_panel)) {
        lv_obj_delete(s_fiction_toc_panel);
    }
    s_fiction_toc_panel = NULL;
}

static void lxj_fiction_toc_select(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    char page[LXJ_FICTION_PAGE_BYTES + 1U];
    if (fiction_epub_rich_jump_to_toc(index) &&
        fiction_epub_rich_read_page(page, sizeof(page)) &&
        s_fiction_detail && lv_obj_is_valid(s_fiction_detail)) {
        lv_label_set_text(s_fiction_detail, page);
        lxj_fiction_refresh_image();
    }
    lxj_fiction_toc_close(NULL);
}

static void lxj_fiction_show_toc(lv_event_t *event)
{
    (void)event;
    if (!s_fiction_rich_active || !s_root || !lv_obj_is_valid(s_root)) return;

    lxj_fiction_toc_close(NULL);
    s_fiction_toc_panel = lv_obj_create(s_root);
    lv_obj_set_size(s_fiction_toc_panel, LV_PCT(90), LV_PCT(85));
    lv_obj_center(s_fiction_toc_panel);
    lv_obj_set_style_pad_all(s_fiction_toc_panel, 16, 0);

    lv_obj_t *title = lv_label_create(s_fiction_toc_panel);
    lv_label_set_text(title, "Table of Contents");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *list = lv_obj_create(s_fiction_toc_panel);
    lv_obj_set_size(list, LV_PCT(100), 300);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 4, 0);

    int count = fiction_epub_rich_toc_count();
    for (int i = 0; i < count; ++i) {
        char toc_title[160];
        if (!fiction_epub_rich_toc_title(i, toc_title, sizeof(toc_title))) {
            snprintf(toc_title, sizeof(toc_title), "Chapter %d", i + 1);
        }
        lv_obj_t *item = lv_button_create(list);
        lv_obj_set_width(item, LV_PCT(100));
        lv_obj_set_height(item, 40);
        lv_obj_add_event_cb(item, lxj_fiction_toc_select, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        lv_obj_t *label = lv_label_create(item);
        lv_label_set_text(label, toc_title);
        lv_obj_center(label);
    }
    if (count == 0) {
        lv_obj_t *empty = lv_label_create(list);
        lv_label_set_text(empty, "No table of contents");
    }

    lv_obj_t *close = lv_button_create(s_fiction_toc_panel);
    lv_obj_set_size(close, 120, 42);
    lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_add_event_cb(close, lxj_fiction_toc_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_label = lv_label_create(close);
    lv_label_set_text(close_label, "Close");
    lv_obj_center(close_label);
}

static void lxj_fiction_refresh_event(lv_event_t *event)
{
    (void)event;
    lxj_fiction_refresh();
}

static void lxj_fiction_start(lv_obj_t *root, lv_group_t *group)
{
    (void)group;
    s_root = lv_obj_create(root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_root);
    lv_obj_set_style_pad_all(s_root, 20, 0);
    lv_obj_t *heading = lv_label_create(s_root);
    lv_label_set_text(heading, "LXJ Fiction");
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 10);
    s_fiction_list = lv_obj_create(s_root);
    lv_obj_set_size(s_fiction_list, LV_PCT(35), 270);
    lv_obj_align(s_fiction_list, LV_ALIGN_TOP_LEFT, 0, 48);
    lv_obj_set_flex_flow(s_fiction_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_fiction_list, 8, 0);

    s_fiction_detail = lv_label_create(s_root);
    lv_obj_set_size(s_fiction_detail, LV_PCT(60), 270);
    lv_obj_align(s_fiction_detail, LV_ALIGN_TOP_RIGHT, 0, 48);
    lv_label_set_long_mode(s_fiction_detail, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_fiction_detail, "Select an EPUB or TXT book.");
    lxj_fiction_refresh();

    lv_obj_t *refresh = lv_button_create(s_root);
    lv_obj_set_size(refresh, 110, 42);
    lv_obj_align(refresh, LV_ALIGN_BOTTOM_LEFT, 0, -12);
    lv_obj_add_event_cb(refresh, lxj_fiction_refresh_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *refresh_label = lv_label_create(refresh);
    lv_label_set_text(refresh_label, "Refresh");
    lv_obj_center(refresh_label);

    lv_obj_t *previous = lv_button_create(s_root);
    lv_obj_set_size(previous, 110, 42);
    lv_obj_align(previous, LV_ALIGN_BOTTOM_LEFT, 120, -12);
    lv_obj_add_event_cb(previous, lxj_fiction_page, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    lv_obj_t *previous_label = lv_label_create(previous);
    lv_label_set_text(previous_label, "Previous");
    lv_obj_center(previous_label);

    lv_obj_t *next = lv_button_create(s_root);
    lv_obj_set_size(next, 110, 42);
    lv_obj_align(next, LV_ALIGN_BOTTOM_LEFT, 240, -12);
    lv_obj_add_event_cb(next, lxj_fiction_page, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    lv_obj_t *next_label = lv_label_create(next);
    lv_label_set_text(next_label, "Next");
    lv_obj_center(next_label);

    lv_obj_t *toc = lv_button_create(s_root);
    lv_obj_set_size(toc, 110, 42);
    lv_obj_align(toc, LV_ALIGN_BOTTOM_LEFT, 360, -12);
    lv_obj_add_event_cb(toc, lxj_fiction_show_toc, LV_EVENT_CLICKED, NULL);
    lv_obj_t *toc_label = lv_label_create(toc);
    lv_label_set_text(toc_label, "TOC");
    lv_obj_center(toc_label);

    lv_obj_t *close = lv_button_create(s_root);
    lv_obj_set_size(close, 110, 42);
    lv_obj_align(close, LV_ALIGN_BOTTOM_RIGHT, 0, -12);
    lv_obj_add_event_cb(close, lxj_legacy_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_label = lv_label_create(close);
    lv_label_set_text(close_label, "Close");
    lv_obj_center(close_label);
}

static application_t s_lxj_fiction_application = {
    .name = "LXJ Fiction",
    .icon = NULL,
    .start_func = lxj_fiction_start,
    .stop_func = lxj_legacy_stop,
    .back_func = lxj_legacy_back,
    .category = APP_CATEGORY_TOOLS,
};

void lxj_fiction_init(void)
{
    app_manager_add_application(&s_lxj_fiction_application);
    ESP_LOGI(TAG, "registered LXJ Fiction");
}

static void lxj_mistake_refresh(void)
{
    if (!s_mistake_list || !lv_obj_is_valid(s_mistake_list)) return;
    lv_obj_clean(s_mistake_list);
    s_mistake_card_count = 0;
    s_mistake_selected = 0;
    FILE *file = fopen("/sdcard/mistakebook/cards.csv", "r");
    if (!file) {
        lv_obj_t *empty = lv_label_create(s_mistake_list);
        lv_label_set_text(empty, "No mistake cards found");
        if (s_mistake_detail && lv_obj_is_valid(s_mistake_detail)) {
            lv_label_set_text(s_mistake_detail, "No mistake card selected");
        }
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), file) &&
           s_mistake_card_count < LXJ_MISTAKE_MAX_CARDS) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') continue;

        lxj_mistake_card_t *card = &s_mistake_cards[s_mistake_card_count];
        char *save = NULL;
        char *field = strtok_r(line, "|", &save);
        if (!field) continue;
        lxj_mistake_copy_field(card->subject, sizeof(card->subject), field);
        field = strtok_r(NULL, "|", &save);
        lxj_mistake_copy_field(card->title, sizeof(card->title), field);
        field = strtok_r(NULL, "|", &save);
        lxj_mistake_copy_field(card->question, sizeof(card->question), field);
        field = strtok_r(NULL, "|", &save);
        lxj_mistake_copy_field(card->student_answer, sizeof(card->student_answer), field);
        field = strtok_r(NULL, "|", &save);
        lxj_mistake_copy_field(card->correct_answer, sizeof(card->correct_answer), field);
        field = strtok_r(NULL, "|", &save);
        lxj_mistake_copy_field(card->knowledge_point, sizeof(card->knowledge_point), field);
        field = strtok_r(NULL, "|", &save);
        lxj_mistake_copy_field(card->error_type, sizeof(card->error_type), field);
        field = strtok_r(NULL, "|", &save);
        lxj_mistake_copy_field(card->explanation, sizeof(card->explanation), field);
        field = strtok_r(NULL, "|", &save);
        card->review_pending = field ? atoi(field) != 0 : true;

        lv_obj_t *row = lv_button_create(s_mistake_list);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 48);
        lv_obj_add_event_cb(row, lxj_mistake_select, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)s_mistake_card_count);
        lv_obj_t *label = lv_label_create(row);
        lv_label_set_text_fmt(label, "%u. %s / %s",
                              (unsigned)(s_mistake_card_count + 1U),
                              card->subject[0] ? card->subject : "Unknown",
                              card->title[0] ? card->title : "Untitled");
        lv_obj_center(label);
        ++s_mistake_card_count;
    }
    fclose(file);
    if (s_mistake_card_count == 0U) {
        lv_obj_t *empty = lv_label_create(s_mistake_list);
        lv_label_set_text(empty, "No mistake cards found");
    } else {
        lxj_mistake_select(NULL);
    }
}

static void lxj_mistake_select(lv_event_t *event)
{
    if (event) s_mistake_selected = (uint32_t)(uintptr_t)lv_event_get_user_data(event);
    if (!s_mistake_detail || !lv_obj_is_valid(s_mistake_detail) ||
        s_mistake_selected >= s_mistake_card_count) return;

    const lxj_mistake_card_t *card = &s_mistake_cards[s_mistake_selected];
    char detail[1600];
    snprintf(detail, sizeof(detail),
             "[%s] %s\n\nQuestion:\n%s\n\n"
             "Your answer:\n%s\n\nCorrect answer:\n%s\n\n"
             "Knowledge: %s\nError type: %s\n\nExplanation:\n%s\n\n"
             "Review status: %s",
             card->subject, card->title,
             card->question[0] ? card->question : "(not provided)",
             card->student_answer[0] ? card->student_answer : "(not provided)",
             card->correct_answer[0] ? card->correct_answer : "(not provided)",
             card->knowledge_point[0] ? card->knowledge_point : "(not provided)",
             card->error_type[0] ? card->error_type : "(not provided)",
             card->explanation[0] ? card->explanation : "(not provided)",
             card->review_pending ? "pending" : "reviewed");
    lv_label_set_text(s_mistake_detail, detail);
}

static bool lxj_mistake_save_cards(void)
{
    const char *path = "/sdcard/mistakebook/cards.csv";
    const char *temp_path = "/sdcard/mistakebook/cards.csv.tmp";
    FILE *file = fopen(temp_path, "w");
    if (!file) return false;

    for (uint32_t i = 0; i < s_mistake_card_count; ++i) {
        const lxj_mistake_card_t *card = &s_mistake_cards[i];
        if (fprintf(file, "%s|%s|%s|%s|%s|%s|%s|%s|%d\n",
                    card->subject, card->title, card->question,
                    card->student_answer, card->correct_answer,
                    card->knowledge_point, card->error_type,
                    card->explanation, card->review_pending ? 1 : 0) < 0) {
            fclose(file);
            remove(temp_path);
            return false;
        }
    }
    if (fclose(file) != 0) {
        remove(temp_path);
        return false;
    }
    if (rename(temp_path, path) != 0) {
        remove(temp_path);
        return false;
    }
    return true;
}

static void lxj_mistake_mark_reviewed(lv_event_t *event)
{
    (void)event;
    if (s_mistake_selected >= s_mistake_card_count) return;
    s_mistake_cards[s_mistake_selected].review_pending = false;
    if (!lxj_mistake_save_cards()) {
        ESP_LOGW(TAG, "could not persist mistake review state");
        s_mistake_cards[s_mistake_selected].review_pending = true;
        return;
    }
    lxj_mistake_select(NULL);
}

static void lxj_mistake_previous(lv_event_t *event)
{
    (void)event;
    if (s_mistake_card_count == 0U) return;
    s_mistake_selected = s_mistake_selected == 0U
                             ? s_mistake_card_count - 1U
                             : s_mistake_selected - 1U;
    lxj_mistake_select(NULL);
}

static void lxj_mistake_next(lv_event_t *event)
{
    (void)event;
    if (s_mistake_card_count == 0U) return;
    s_mistake_selected = (s_mistake_selected + 1U) % s_mistake_card_count;
    lxj_mistake_select(NULL);
}

static void lxj_mistake_refresh_event(lv_event_t *event)
{
    (void)event;
    lxj_mistake_refresh();
}

static void lxj_mistakebook_start(lv_obj_t *root, lv_group_t *group)
{
    (void)group;
    s_root = lv_obj_create(root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_root);
    lv_obj_set_style_pad_all(s_root, 20, 0);
    lv_obj_t *heading = lv_label_create(s_root);
    lv_label_set_text(heading, "LXJ Mistakebook");
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 10);
    s_mistake_list = lv_obj_create(s_root);
    lv_obj_set_size(s_mistake_list, LV_PCT(42), 270);
    lv_obj_align(s_mistake_list, LV_ALIGN_TOP_LEFT, 20, 48);
    lv_obj_set_flex_flow(s_mistake_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_mistake_list, 8, 0);

    s_mistake_detail = lv_label_create(s_root);
    lv_obj_set_size(s_mistake_detail, LV_PCT(48), 270);
    lv_obj_align(s_mistake_detail, LV_ALIGN_TOP_RIGHT, -20, 48);
    lv_label_set_long_mode(s_mistake_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_mistake_detail, LV_TEXT_ALIGN_LEFT, 0);
    lxj_mistake_refresh();

    lv_obj_t *refresh = lv_button_create(s_root);
    lv_obj_set_size(refresh, 130, 48);
    lv_obj_align(refresh, LV_ALIGN_BOTTOM_LEFT, 24, -12);
    lv_obj_add_event_cb(refresh, lxj_mistake_refresh_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *refresh_label = lv_label_create(refresh);
    lv_label_set_text(refresh_label, "Refresh");
    lv_obj_center(refresh_label);

    lv_obj_t *previous = lv_button_create(s_root);
    lv_obj_set_size(previous, 100, 48);
    lv_obj_align(previous, LV_ALIGN_BOTTOM_LEFT, 164, -12);
    lv_obj_add_event_cb(previous, lxj_mistake_previous, LV_EVENT_CLICKED, NULL);
    lv_obj_t *previous_label = lv_label_create(previous);
    lv_label_set_text(previous_label, "Previous");
    lv_obj_center(previous_label);

    lv_obj_t *next = lv_button_create(s_root);
    lv_obj_set_size(next, 100, 48);
    lv_obj_align(next, LV_ALIGN_BOTTOM_LEFT, 274, -12);
    lv_obj_add_event_cb(next, lxj_mistake_next, LV_EVENT_CLICKED, NULL);
    lv_obj_t *next_label = lv_label_create(next);
    lv_label_set_text(next_label, "Next");
    lv_obj_center(next_label);

    lv_obj_t *reviewed = lv_button_create(s_root);
    lv_obj_set_size(reviewed, 140, 48);
    lv_obj_align(reviewed, LV_ALIGN_BOTTOM_LEFT, 384, -12);
    lv_obj_add_event_cb(reviewed, lxj_mistake_mark_reviewed, LV_EVENT_CLICKED, NULL);
    lv_obj_t *reviewed_label = lv_label_create(reviewed);
    lv_label_set_text(reviewed_label, "Mark reviewed");
    lv_obj_center(reviewed_label);

    lv_obj_t *close = lv_button_create(s_root);
    lv_obj_set_size(close, 130, 48);
    lv_obj_align(close, LV_ALIGN_BOTTOM_RIGHT, -24, -12);
    lv_obj_add_event_cb(close, lxj_legacy_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_label = lv_label_create(close);
    lv_label_set_text(close_label, "Close");
    lv_obj_center(close_label);
}

static application_t s_lxj_mistakebook_application = {
    .name = "LXJ Mistakebook",
    .icon = NULL,
    .start_func = lxj_mistakebook_start,
    .stop_func = lxj_legacy_stop,
    .back_func = lxj_legacy_back,
    .category = APP_CATEGORY_TOOLS,
};

void lxj_mistakebook_init(void)
{
    app_manager_add_application(&s_lxj_mistakebook_application);
    ESP_LOGI(TAG, "registered LXJ Mistakebook");
}

void lxj_chat_init(void)
{
    chatbot_lxj_init();
    ESP_LOGI(TAG, "registered LXJ Chat through shared Chatbot owner");
}

static void lxj_weather_refresh(void)
{
    if (!s_weather_status || !lv_obj_is_valid(s_weather_status)) return;
    wifi_info_t info = {0};
    bool connected = wifi_get_info(&info) == ESP_OK && info.is_connected;
    if (!connected) {
        lv_label_set_text(s_weather_status, "Network offline\nConnect Wi-Fi before fetching weather");
        return;
    }
    if (s_weather_result.result != ESP_OK) {
        lv_label_set_text_fmt(s_weather_status, "Wi-Fi: %s\nWeather request failed\nSelect a city and retry",
                              info.ssid);
        return;
    }
    lv_label_set_text_fmt(s_weather_status,
                          "Wi-Fi: %s\n%s\nTemperature: %s C\nHumidity: %s\n"
                          "Today: %s\nHigh/low: %s / %s\nAir quality: %s",
                          info.ssid, s_weather_result.city,
                          s_weather_result.temperature, s_weather_result.humidity,
                          s_weather_result.type, s_weather_result.high,
                          s_weather_result.low, s_weather_result.quality);
    if (s_weather_image && lv_obj_is_valid(s_weather_image)) {
        lv_image_set_src(s_weather_image, lxj_weather_icon(s_weather_result.type));
    }
}

static void lxj_weather_request(void)
{
    if (!s_weather_queue || s_weather_busy) return;
    uint16_t selected = s_weather_city ? lv_dropdown_get_selected(s_weather_city) : 0;
    if (selected >= sizeof(s_weather_cities) / sizeof(s_weather_cities[0])) selected = 0;
    uint8_t city_index = (uint8_t)selected;
    if (xQueueSend(s_weather_queue, &city_index, 0) != pdTRUE) return;
    s_weather_busy = true;
    lv_label_set_text(s_weather_status, "Fetching weather...");
}

static void lxj_weather_apply_result(void *arg)
{
    (void)arg;
    s_weather_busy = false;
    lxj_weather_refresh();
}

static void lxj_weather_refresh_event(lv_event_t *event)
{
    (void)event;
    lxj_weather_request();
}

static void lxj_weather_start(lv_obj_t *root, lv_group_t *group)
{
    (void)group;
    s_root = lv_obj_create(root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_root);
    lv_obj_set_style_pad_all(s_root, 24, 0);
    lv_obj_t *heading = lv_label_create(s_root);
    lv_label_set_text(heading, "LXJ Weather");
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_t *weather_image = lv_image_create(s_root);
    lv_image_set_src(weather_image, &clear);
    lv_obj_align(weather_image, LV_ALIGN_TOP_RIGHT, -24, 62);
    s_weather_image = weather_image;

    s_weather_city = lv_dropdown_create(s_root);
    lv_dropdown_set_options(s_weather_city, "Beijing\nShanghai\nShenzhen");
    lv_obj_set_width(s_weather_city, 190);
    lv_obj_align(s_weather_city, LV_ALIGN_TOP_LEFT, 24, 62);
    s_weather_status = lv_label_create(s_root);
    lv_obj_align(s_weather_status, LV_ALIGN_TOP_LEFT, 24, 82);
    lxj_weather_request();

    lv_obj_t *refresh = lv_button_create(s_root);
    lv_obj_set_size(refresh, 130, 48);
    lv_obj_align(refresh, LV_ALIGN_BOTTOM_LEFT, 24, -18);
    lv_obj_add_event_cb(refresh, lxj_weather_refresh_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *refresh_label = lv_label_create(refresh);
    lv_label_set_text(refresh_label, "Refresh");
    lv_obj_center(refresh_label);

    if (!s_weather_queue) {
        s_weather_queue = xQueueCreate(1, sizeof(uint8_t));
        if (s_weather_queue) {
            xTaskCreate(lxj_weather_worker_task, "lxj_weather", 6144, NULL, 4,
                        &s_weather_worker);
        }
    }
    lxj_weather_request();

    lv_obj_t *close = lv_button_create(s_root);
    lv_obj_set_size(close, 130, 48);
    lv_obj_align(close, LV_ALIGN_BOTTOM_RIGHT, -24, -18);
    lv_obj_add_event_cb(close, lxj_legacy_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_label = lv_label_create(close);
    lv_label_set_text(close_label, "Close");
    lv_obj_center(close_label);
}

static application_t s_lxj_weather_application = {
    .name = "LXJ Weather",
    .icon = NULL,
    .start_func = lxj_weather_start,
    .stop_func = lxj_legacy_stop,
    .back_func = lxj_legacy_back,
    .category = APP_CATEGORY_SENSORS,
};

void lxj_weather_init(void)
{
    app_manager_add_application(&s_lxj_weather_application);
    ESP_LOGI(TAG, "registered LXJ Weather");
}

static void lxj_pomodoro_update_label(void)
{
    if (!s_pomodoro_time || !lv_obj_is_valid(s_pomodoro_time)) return;
    lv_label_set_text_fmt(s_pomodoro_time, "%02u:%02u",
                          (unsigned)(s_pomodoro_remaining / 60U),
                          (unsigned)(s_pomodoro_remaining % 60U));
}

static void lxj_pomodoro_update_state(const char *state)
{
    if (s_pomodoro_state && lv_obj_is_valid(s_pomodoro_state)) {
        lv_label_set_text(s_pomodoro_state, state);
    }
}

static void lxj_pomodoro_tick(lv_timer_t *timer)
{
    (void)timer;
    int64_t now = esp_timer_get_time();
    if (!s_pomodoro_running || s_pomodoro_remaining == 0U) {
        s_pomodoro_last_tick_us = now;
        return;
    }

    int64_t elapsed = now - s_pomodoro_last_tick_us;
    if (elapsed < 1000000) return;
    uint32_t elapsed_seconds = (uint32_t)(elapsed / 1000000);
    if (elapsed_seconds >= s_pomodoro_remaining) {
        s_pomodoro_remaining = 0;
        s_pomodoro_running = false;
        lxj_pomodoro_update_label();
        lxj_pomodoro_update_state("Complete — take a break");
    } else {
        s_pomodoro_remaining -= elapsed_seconds;
        lxj_pomodoro_update_label();
        lxj_pomodoro_update_state("Running");
    }
    s_pomodoro_last_tick_us += (int64_t)elapsed_seconds * 1000000;
}

static void lxj_pomodoro_toggle(lv_event_t *event)
{
    (void)event;
    if (s_pomodoro_remaining == 0U) {
        s_pomodoro_remaining = 25U * 60U;
    }
    s_pomodoro_running = !s_pomodoro_running;
    s_pomodoro_last_tick_us = esp_timer_get_time();
    lxj_pomodoro_update_state(s_pomodoro_running ? "Running" : "Paused");
}

static void lxj_pomodoro_reset(lv_event_t *event)
{
    (void)event;
    s_pomodoro_running = false;
    s_pomodoro_remaining = 25U * 60U;
    s_pomodoro_last_tick_us = esp_timer_get_time();
    lxj_pomodoro_update_label();
    lxj_pomodoro_update_state("Ready");
}

static void lxj_pomodoro_start(lv_obj_t *root, lv_group_t *group)
{
    (void)group;
    s_pomodoro_remaining = 25U * 60U;
    s_pomodoro_running = false;
    s_pomodoro_last_tick_us = esp_timer_get_time();
    s_root = lv_obj_create(root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_root);
    lv_obj_set_style_pad_all(s_root, 24, 0);

    lv_obj_t *heading = lv_label_create(s_root);
    lv_label_set_text(heading, "LXJ Pomodoro");
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 16);

    s_pomodoro_time = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_pomodoro_time, &lv_font_montserrat_32, 0);
    lv_obj_align(s_pomodoro_time, LV_ALIGN_CENTER, 0, -20);
    lxj_pomodoro_update_label();

    s_pomodoro_state = lv_label_create(s_root);
    lv_label_set_text(s_pomodoro_state, "Ready");
    lv_obj_align(s_pomodoro_state, LV_ALIGN_CENTER, 0, 28);

    lv_obj_t *toggle = lv_button_create(s_root);
    lv_obj_set_size(toggle, 130, 50);
    lv_obj_align(toggle, LV_ALIGN_BOTTOM_LEFT, 24, -18);
    lv_obj_add_event_cb(toggle, lxj_pomodoro_toggle, LV_EVENT_CLICKED, NULL);
    lv_obj_t *toggle_label = lv_label_create(toggle);
    lv_label_set_text(toggle_label, "Start/Pause");
    lv_obj_center(toggle_label);

    lv_obj_t *reset = lv_button_create(s_root);
    lv_obj_set_size(reset, 110, 50);
    lv_obj_align(reset, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_add_event_cb(reset, lxj_pomodoro_reset, LV_EVENT_CLICKED, NULL);
    lv_obj_t *reset_label = lv_label_create(reset);
    lv_label_set_text(reset_label, "Reset");
    lv_obj_center(reset_label);

    lv_obj_t *close = lv_button_create(s_root);
    lv_obj_set_size(close, 100, 50);
    lv_obj_align(close, LV_ALIGN_BOTTOM_RIGHT, -24, -18);
    lv_obj_add_event_cb(close, lxj_legacy_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_label = lv_label_create(close);
    lv_label_set_text(close_label, "Close");
    lv_obj_center(close_label);

    s_pomodoro_timer = lv_timer_create(lxj_pomodoro_tick, 1000, NULL);
}

static application_t s_lxj_pomodoro_application = {
    .name = "LXJ Pomodoro",
    .icon = NULL,
    .start_func = lxj_pomodoro_start,
    .stop_func = lxj_legacy_stop,
    .back_func = lxj_legacy_back,
    .category = APP_CATEGORY_TOOLS,
};

void lxj_pomodoro_init(void)
{
    app_manager_add_application(&s_lxj_pomodoro_application);
    ESP_LOGI(TAG, "registered LXJ Pomodoro");
}

static void lxj_legacy_close(lv_event_t *event)
{
    (void)event;
    app_manager_exit_app();
}

static void lxj_legacy_stop(void)
{
    if (s_pomodoro_timer) lv_timer_delete(s_pomodoro_timer);
    s_pomodoro_timer = NULL;
    if (s_fiction_file) fclose(s_fiction_file);
    s_fiction_file = NULL;
    fiction_epub_rich_close();
    s_fiction_rich_active = false;
    s_fiction_toc_panel = NULL;
    if (s_fiction_image && lv_obj_is_valid(s_fiction_image)) {
        lv_obj_delete(s_fiction_image);
    }
    s_fiction_image = NULL;
    if (s_fiction_image_data) {
        fiction_epub_rich_free_page_image(s_fiction_image_data);
        s_fiction_image_data = NULL;
    }
    s_fiction_path_count = 0;
    s_fiction_selected = 0;
    s_fiction_offset = 0;
    s_pomodoro_time = NULL;
    s_pomodoro_state = NULL;
    s_alarm_list = NULL;
    s_settings_status = NULL;
    s_fiction_list = NULL;
    s_fiction_detail = NULL;
    s_mistake_list = NULL;
    s_mistake_detail = NULL;
    s_weather_status = NULL;
    s_weather_image = NULL;
    s_weather_city = NULL;
    s_pomodoro_running = false;
    if (s_root && lv_obj_is_valid(s_root)) lv_obj_delete(s_root);
    s_root = NULL;
    s_network_status = NULL;
    s_network_list = NULL;
}

static bool lxj_legacy_back(void)
{
    return false;
}
