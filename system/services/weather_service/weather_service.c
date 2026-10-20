#include "weather_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define WEATHER_JSON_CAP 8192U

static const char *TAG = "weather_service";

typedef struct {
    uint32_t token;
    char city_code[16];
} weather_request_t;

static QueueHandle_t s_requests;
static QueueHandle_t s_results;
static TaskHandle_t s_worker;

static const char *json_string(const cJSON *object, const char *key)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) && value->valuestring ? value->valuestring : "";
}

static bool fetch_weather(const char *city_code, weather_report_t *out)
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
    char *body = malloc(WEATHER_JSON_CAP);
    if (!body) {
        esp_http_client_cleanup(client);
        return false;
    }

    bool ok = false;
    if (esp_http_client_open(client, 0) == ESP_OK &&
        esp_http_client_fetch_headers(client) >= 0 &&
        esp_http_client_get_status_code(client) >= 200 &&
        esp_http_client_get_status_code(client) < 300) {
        size_t length = 0;
        int received = 0;
        while (length < WEATHER_JSON_CAP - 1) {
            received = esp_http_client_read(client, body + length,
                                            WEATHER_JSON_CAP - 1 - length);
            if (received <= 0) break;
            length += (size_t)received;
        }
        /* A full buffer may contain a truncated JSON document. */
        if (received == 0 && length > 0 && length < WEATHER_JSON_CAP - 1) {
            body[length] = '\0';
            cJSON *root = cJSON_Parse(body);
            const cJSON *city = cJSON_GetObjectItemCaseSensitive(root, "cityInfo");
            const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
            const cJSON *forecast = cJSON_GetObjectItemCaseSensitive(data, "forecast");
            const cJSON *today = cJSON_IsArray(forecast)
                ? cJSON_GetArrayItem(forecast, 0) : NULL;
            if (cJSON_IsObject(city) && cJSON_IsObject(data) && cJSON_IsObject(today)) {
                snprintf(out->city, sizeof(out->city), "%s", json_string(city, "city"));
                snprintf(out->temperature, sizeof(out->temperature), "%s", json_string(data, "wendu"));
                snprintf(out->humidity, sizeof(out->humidity), "%s", json_string(data, "shidu"));
                snprintf(out->quality, sizeof(out->quality), "%s", json_string(data, "quality"));
                snprintf(out->type, sizeof(out->type), "%s", json_string(today, "type"));
                snprintf(out->high, sizeof(out->high), "%s", json_string(today, "high"));
                snprintf(out->low, sizeof(out->low), "%s", json_string(today, "low"));
                ok = out->city[0] != '\0' && out->type[0] != '\0';
            }
            cJSON_Delete(root);
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(body);
    return ok;
}

static void weather_worker(void *arg)
{
    (void)arg;
    weather_request_t request;
    for (;;) {
        if (xQueueReceive(s_requests, &request, portMAX_DELAY) != pdTRUE) continue;
        weather_result_t result = {.token = request.token};
        result.success = fetch_weather(request.city_code, &result.report);
        if (xQueueSend(s_results, &result, 0) != pdTRUE) {
            /* A closed app may leave old results unread. Keep the newest one. */
            weather_result_t discarded;
            xQueueReceive(s_results, &discarded, 0);
            xQueueSend(s_results, &result, 0);
        }
    }
}

bool weather_service_init(void)
{
    if (s_worker) return true;
    if (!s_requests) s_requests = xQueueCreate(2, sizeof(weather_request_t));
    if (!s_results) s_results = xQueueCreate(4, sizeof(weather_result_t));
    if (!s_requests || !s_results) return false;
    if (xTaskCreate(weather_worker, "weather_fetch", 6144, NULL, 4,
                    &s_worker) != pdPASS) {
        ESP_LOGE(TAG, "failed to start worker");
        s_worker = NULL;
        return false;
    }
    return true;
}

bool weather_service_request(const char *city_code, uint32_t token)
{
    if (!s_worker || !city_code || strlen(city_code) != 9) return false;
    for (const char *p = city_code; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
    }
    weather_request_t request = {.token = token};
    snprintf(request.city_code, sizeof(request.city_code), "%s", city_code);
    return xQueueSend(s_requests, &request, 0) == pdTRUE;
}

bool weather_service_try_take_result(weather_result_t *out)
{
    return out && s_results && xQueueReceive(s_results, out, 0) == pdTRUE;
}
