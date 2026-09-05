/**
 * @file ota.c
 * @brief WiFi 固件升级实现 —— 见 ota.h 的流程和线程约定。
 */

#include "ota.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lvgl.h>
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "flash_store.h"
#include "wifi_manager.h"

static const char *TAG = "ota";

/* flash_store 里记的“上一次升级的目标”，下次开机拿来判断升级成没成 */
#define OTA_NS          "ota"
#define KEY_PEND_TO     "pend_to"
#define KEY_PEND_FROM   "pend_from"
#define KEY_PEND_URL    "pend_url"

#define MANIFEST_MAX     4096
#define HTTP_TIMEOUT_MS  15000
#define OTA_TIMEOUT_MS   120000
#define WORKER_STACK     8192
#define WORKER_PRIO      4

/* ── 模块状态 ───────────────────────────────────────────────────────────
 * s_state / s_progress 由 worker 写、LVGL 线程读，都是单字，volatile 足够。
 * s_pending 只在 LVGL 线程里写（结果都经 lv_async_call 弹回去）。 */
static volatile ota_state_t s_state    = OTA_IDLE;
static volatile int         s_progress = 0;
static volatile bool        s_busy     = false;
static int                  s_expected_size = 0;   /* manifest 给的字节数，算百分比用 */
static char                 s_error[96]     = "";
static ota_update_info_t    s_pending;
static bool                 s_has_pending   = false;
static char                 s_device_id[13] = "";
/* 最近一次查更新用的 manifest 地址，刷完后拿来推导回报地址 */
static char                 s_manifest_url[OTA_URL_LEN] = "";

/* ── 小工具 ─────────────────────────────────────────────────────────── */

/** 比较 "x.y.z"。a 新返回 >0，旧返回 <0，相同返回 0。
 *  任一侧不是三段数字就退化成 strcmp（至少能判出“不一样”）。 */
static int version_compare(const char *a, const char *b)
{
    int va[3] = {0, 0, 0}, vb[3] = {0, 0, 0};
    if (sscanf(a, "%d.%d.%d", &va[0], &va[1], &va[2]) < 1 ||
        sscanf(b, "%d.%d.%d", &vb[0], &vb[1], &vb[2]) < 1) {
        return strcmp(a, b);
    }
    for (int i = 0; i < 3; i++) {
        if (va[i] != vb[i]) return va[i] - vb[i];
    }
    return 0;
}

static bool url_is_http(const char *url)
{
    return url && (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0);
}

static void set_error(const char *msg)
{
    snprintf(s_error, sizeof(s_error), "%s", msg ? msg : "");
}

/** GET url，响应体写进新分配的 buf（调用方 free）。 */
static bool http_get(const char *url, char **out_buf, int *out_len)
{
    *out_buf = NULL;
    *out_len = 0;

    esp_http_client_config_t cfg = {
        .url               = url,
        .method            = HTTP_METHOD_GET,
        .timeout_ms        = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) return false;

    bool  ok  = false;
    char *buf = NULL;

    if (esp_http_client_open(cli, 0) != ESP_OK) {
        ESP_LOGE(TAG, "open %s failed", url);
        goto out;
    }
    if (esp_http_client_fetch_headers(cli) < 0) goto out;

    int status = esp_http_client_get_status_code(cli);
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "HTTP %d for %s", status, url);
        goto out;
    }

    buf = malloc(MANIFEST_MAX);
    if (!buf) goto out;

    int len = 0;
    for (;;) {
        int r = esp_http_client_read(cli, buf + len, MANIFEST_MAX - len - 1);
        if (r < 0) goto out;
        if (r == 0) break;
        len += r;
        if (len >= MANIFEST_MAX - 1) {   /* manifest 不该有这么大，当异常处理 */
            ESP_LOGE(TAG, "manifest over %d bytes", MANIFEST_MAX);
            goto out;
        }
    }
    buf[len] = '\0';

    *out_buf = buf;
    *out_len = len;
    buf = NULL;     /* 所有权交出去 */
    ok  = true;

out:
    free(buf);
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);
    return ok;
}

static void json_copy_str(char *dst, size_t dst_len, const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    snprintf(dst, dst_len, "%s", cJSON_IsString(v) && v->valuestring ? v->valuestring : "");
}

/** 解析 manifest。version 和 url 缺一不可，其余字段可选。 */
static bool parse_manifest(const char *json, ota_update_info_t *info)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;

    memset(info, 0, sizeof(*info));
    json_copy_str(info->version,   sizeof(info->version),   root, "version");
    json_copy_str(info->url,       sizeof(info->url),       root, "url");
    json_copy_str(info->changelog, sizeof(info->changelog), root, "changelog");
    json_copy_str(info->date,      sizeof(info->date),      root, "date");

    const cJSON *size = cJSON_GetObjectItemCaseSensitive(root, "size");
    if (cJSON_IsNumber(size)) info->size = size->valueint;

    info->mandatory = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "mandatory"));

    const cJSON *batt = cJSON_GetObjectItemCaseSensitive(root, "min_battery");
    info->min_battery = cJSON_IsNumber(batt) ? batt->valueint : 0;

    cJSON_Delete(root);
    return info->version[0] != '\0' && url_is_http(info->url);
}

/* ── 查更新 ─────────────────────────────────────────────────────────── */

typedef struct {
    char           url[OTA_URL_LEN];
    ota_check_cb_t cb;
    void          *user_data;
    int            delay_ms;
} check_job_t;

typedef struct {
    ota_check_result_t result;
    ota_update_info_t  info;
    ota_check_cb_t     cb;
    void              *user_data;
} check_result_t;

/** 在 LVGL 线程执行：先更新 pending 缓存，再回调 UI。 */
static void deliver_check(void *arg)
{
    check_result_t *res = arg;

    if (res->result == OTA_CHECK_UPDATE_AVAILABLE) {
        s_pending     = res->info;
        s_has_pending = true;
    } else if (res->result == OTA_CHECK_UP_TO_DATE) {
        s_has_pending = false;
    }
    if (s_state == OTA_CHECKING) s_state = OTA_IDLE;

    if (res->cb) {
        res->cb(res->result,
                res->result == OTA_CHECK_UPDATE_AVAILABLE ? &res->info : NULL,
                res->user_data);
    }
    free(res);
}

static void post_check(check_result_t *res)
{
    if (lv_async_call(deliver_check, res) != LV_RESULT_OK) {
        ESP_LOGE(TAG, "lv_async_call failed, dropping check result");
        free(res);
    }
}

static void check_task(void *arg)
{
    check_job_t *job = arg;

    if (job->delay_ms > 0) vTaskDelay(pdMS_TO_TICKS(job->delay_ms));

    check_result_t *res = calloc(1, sizeof(check_result_t));
    if (!res) {
        ESP_LOGE(TAG, "no memory for check result");
        goto done;
    }
    res->cb        = job->cb;
    res->user_data = job->user_data;

    if (!wifi_is_connected()) {
        res->result = OTA_CHECK_ERR_OFFLINE;
        post_check(res);
        goto done;
    }

    /* 带上本机版本和设备号，服务端可以据此统计 —— 但版本比较端侧自己做，
     * 不依赖服务端返回的 update_available。 */
    char url[OTA_URL_LEN + 96];
    snprintf(url, sizeof(url), "%s%cversion=%s&device=%s",
             job->url, strchr(job->url, '?') ? '&' : '?',
             ota_running_version(), ota_device_id());
    ESP_LOGI(TAG, "GET %s", url);

    char *body = NULL;
    int   body_len = 0;
    if (!http_get(url, &body, &body_len)) {
        res->result = OTA_CHECK_ERR_NETWORK;
        post_check(res);
        goto done;
    }

    if (!parse_manifest(body, &res->info)) {
        ESP_LOGE(TAG, "bad manifest: %.128s", body);
        res->result = OTA_CHECK_ERR_PARSE;
    } else {
        const char *running = ota_running_version();
        ESP_LOGI(TAG, "running=%s remote=%s", running, res->info.version);
        res->result = version_compare(res->info.version, running) > 0
                          ? OTA_CHECK_UPDATE_AVAILABLE
                          : OTA_CHECK_UP_TO_DATE;
    }
    free(body);
    post_check(res);

done:
    free(job);
    s_busy = false;
    vTaskDelete(NULL);
}

static bool start_check(const char *manifest_url, ota_check_cb_t cb,
                        void *user_data, int delay_ms)
{
    const char *url = (manifest_url && manifest_url[0]) ? manifest_url
                                                        : OTA_DEFAULT_MANIFEST_URL;
    if (!url_is_http(url)) {
        if (cb) cb(OTA_CHECK_ERR_NO_URL, NULL, user_data);
        return false;
    }
    if (s_busy) {
        if (cb) cb(OTA_CHECK_ERR_BUSY, NULL, user_data);
        return false;
    }

    check_job_t *job = calloc(1, sizeof(check_job_t));
    if (!job) {
        if (cb) cb(OTA_CHECK_ERR_NETWORK, NULL, user_data);
        return false;
    }
    snprintf(job->url, sizeof(job->url), "%s", url);
    snprintf(s_manifest_url, sizeof(s_manifest_url), "%s", url);
    job->cb        = cb;
    job->user_data = user_data;
    job->delay_ms  = delay_ms;

    s_busy  = true;
    s_state = OTA_CHECKING;
    if (xTaskCreate(check_task, "ota_check", WORKER_STACK, job, WORKER_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create check task");
        free(job);
        s_busy  = false;
        s_state = OTA_IDLE;
        if (cb) cb(OTA_CHECK_ERR_NETWORK, NULL, user_data);
        return false;
    }
    return true;
}

bool ota_check_async(const char *manifest_url, ota_check_cb_t cb, void *user_data)
{
    return start_check(manifest_url, cb, user_data, 0);
}

void ota_boot_check(const char *manifest_url, int delay_ms)
{
    start_check(manifest_url, NULL, NULL, delay_ms);
}

const ota_update_info_t *ota_get_pending(void)
{
    return s_has_pending ? &s_pending : NULL;
}

void ota_clear_pending(void)
{
    s_has_pending = false;
}

/* ── 下载 & 刷写 ────────────────────────────────────────────────────── */

static void on_ota_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base != ESP_HTTPS_OTA_EVENT) return;

    switch (id) {
    case ESP_HTTPS_OTA_START:
        ESP_LOGI(TAG, "download started");
        s_progress = 0;
        break;
    case ESP_HTTPS_OTA_WRITE_FLASH: {
        /* event_data 是“累计已写字节数”的 int*，不是本次增量 */
        int written = data ? *(const int *)data : 0;
        if (s_expected_size > 0) {
            int pct = (int)((written * 100LL) / s_expected_size);
            s_progress = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
        }
        break;
    }
    case ESP_HTTPS_OTA_UPDATE_BOOT_PARTITION:
        ESP_LOGI(TAG, "boot partition switched");
        break;
    case ESP_HTTPS_OTA_FINISH:
        s_progress = 100;
        break;
    case ESP_HTTPS_OTA_ABORT:
        ESP_LOGE(TAG, "download aborted");
        break;
    default:
        break;
    }
}

/** manifest 没给 size 时补一次 HEAD，拿不到就退化成“进度条不动百分比”。 */
static int probe_content_length(const char *url)
{
    esp_http_client_config_t cfg = {
        .url               = url,
        .timeout_ms        = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) return 0;

    esp_http_client_set_method(cli, HTTP_METHOD_HEAD);
    int len = 0;
    if (esp_http_client_perform(cli) == ESP_OK) {
        len = (int)esp_http_client_get_content_length(cli);
    }
    esp_http_client_cleanup(cli);
    return len > 0 ? len : 0;
}

/** 记下这次升级的目标，下次开机 ota_init() 据此判断成没成并回报服务端。 */
static void remember_attempt(const ota_update_info_t *info)
{
    flash_set_str(OTA_NS, KEY_PEND_TO, info->version);
    flash_set_str(OTA_NS, KEY_PEND_FROM, ota_running_version());
    flash_set_str(OTA_NS, KEY_PEND_URL,
                  s_manifest_url[0] ? s_manifest_url : OTA_DEFAULT_MANIFEST_URL);
}

static void install_task(void *arg)
{
    ota_update_info_t *info = arg;

    s_state    = OTA_DOWNLOADING;
    s_progress = 0;
    set_error("");

    s_expected_size = info->size > 0 ? info->size : probe_content_length(info->url);
    ESP_LOGI(TAG, "installing v%s from %s (%d bytes)",
             info->version, info->url, s_expected_size);

    esp_http_client_config_t http_cfg = {
        .url               = info->url,
        .timeout_ms        = OTA_TIMEOUT_MS,
        .buffer_size       = 4096,
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config      = &http_cfg,
        .bulk_flash_erase = true,
    };

    esp_err_t err = esp_https_ota(&ota_cfg);
    if (err == ESP_OK) {
        remember_attempt(info);
        s_progress = 100;
        s_state    = OTA_READY;
        ESP_LOGI(TAG, "v%s written, waiting for reboot", info->version);
    } else {
        ESP_LOGE(TAG, "install failed: %s", esp_err_to_name(err));
        switch (err) {
        case ESP_ERR_OTA_VALIDATE_FAILED: set_error("Image check failed"); break;
        case ESP_ERR_NO_MEM:              set_error("Out of memory");      break;
        case ESP_ERR_INVALID_VERSION:     set_error("Bad image version");  break;
        default:                          set_error("Download interrupted"); break;
        }
        s_state = OTA_FAILED;
    }

    free(info);
    s_busy = false;
    vTaskDelete(NULL);
}

bool ota_install_async(const ota_update_info_t *info)
{
    if (!info || !url_is_http(info->url)) {
        set_error("Invalid firmware URL");
        s_state = OTA_FAILED;
        return false;
    }
    if (s_busy) return false;
    if (!wifi_is_connected()) {
        set_error("WiFi not connected");
        s_state = OTA_FAILED;
        return false;
    }

    ota_update_info_t *copy = malloc(sizeof(*copy));
    if (!copy) {
        set_error("Out of memory");
        s_state = OTA_FAILED;
        return false;
    }
    *copy = *info;

    s_busy = true;
    if (xTaskCreate(install_task, "ota_install", WORKER_STACK, copy, WORKER_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create install task");
        free(copy);
        s_busy = false;
        set_error("Cannot start updater");
        s_state = OTA_FAILED;
        return false;
    }
    return true;
}

ota_state_t ota_get_state(void)    { return s_state; }
int         ota_get_progress(void) { return s_progress; }
const char *ota_get_error(void)    { return s_error; }

void ota_apply_and_reboot(void)
{
    if (s_state != OTA_READY) return;
    ESP_LOGI(TAG, "rebooting into new firmware");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

/* ── 升级结果回报 ───────────────────────────────────────────────────── */

/** ".../api/ota/check?x=y" → ".../api/ota/report"。推不出来返回 false。 */
static bool derive_report_url(const char *manifest_url, char *out, size_t max)
{
    char buf[OTA_URL_LEN];
    snprintf(buf, sizeof(buf), "%s", manifest_url);

    char *q = strchr(buf, '?');
    if (q) *q = '\0';

    size_t len = strlen(buf);
    const char *tail = "/check";
    size_t tail_len = strlen(tail);
    if (len <= tail_len || strcmp(buf + len - tail_len, tail) != 0) return false;

    buf[len - tail_len] = '\0';
    snprintf(out, max, "%s/report", buf);
    return true;
}

static void post_report(const char *url, const char *body)
{
    esp_http_client_config_t cfg = {
        .url               = url,
        .method            = HTTP_METHOD_POST,
        .timeout_ms        = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) return;

    esp_http_client_set_header(cli, "Content-Type", "application/json");
    esp_http_client_set_post_field(cli, body, strlen(body));
    esp_err_t err = esp_http_client_perform(cli);
    ESP_LOGI(TAG, "report %s -> %s (HTTP %d)", url, esp_err_to_name(err),
             esp_http_client_get_status_code(cli));
    esp_http_client_cleanup(cli);
}

/** 开机后台任务：等到有网就把上次升级结果发出去，等不到就静默放弃。
 *  无论发没发成功都清掉记录 —— 回报是尽力而为的观测数据，不值得反复重试。 */
static void report_task(void *arg)
{
    (void)arg;

    char to[OTA_VERSION_LEN], from[OTA_VERSION_LEN], manifest[OTA_URL_LEN];
    flash_get_str(OTA_NS, KEY_PEND_TO, to, sizeof(to), "");
    flash_get_str(OTA_NS, KEY_PEND_FROM, from, sizeof(from), "");
    flash_get_str(OTA_NS, KEY_PEND_URL, manifest, sizeof(manifest), OTA_DEFAULT_MANIFEST_URL);

    const char *running = ota_running_version();
    const char *status  = (strcmp(running, to) == 0) ? "success" : "rollback";

    char url[OTA_URL_LEN];
    if (derive_report_url(manifest, url, sizeof(url))) {
        /* 最多等 5 分钟联网 */
        for (int i = 0; i < 60 && !wifi_is_connected(); i++) {
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
        if (wifi_is_connected()) {
            char body[256];
            snprintf(body, sizeof(body),
                     "{\"device\":\"%s\",\"from\":\"%s\",\"to\":\"%s\","
                     "\"status\":\"%s\",\"error\":\"\"}",
                     ota_device_id(), from, to, status);
            post_report(url, body);
        } else {
            ESP_LOGW(TAG, "no network, dropping %s report for v%s", status, to);
        }
    }

    flash_set_str(OTA_NS, KEY_PEND_TO, "");
    vTaskDelete(NULL);
}

/* ── 生命周期 ───────────────────────────────────────────────────────── */

const char *ota_running_version(void)
{
    return esp_app_get_description()->version;
}

const char *ota_device_id(void)
{
    if (s_device_id[0] == '\0') {
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(s_device_id, sizeof(s_device_id), "%02x%02x%02x%02x%02x%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    return s_device_id;
}

void ota_init(void)
{
    /* 升级记录存在 NVS 里，这里保证它可用（幂等，重复调没关系） */
    flash_store_init();

    /* 开了 bootloader 回滚时，这一句声明“新固件跑起来了”，否则下次重启会滚回去 */
    esp_ota_mark_app_valid_cancel_rollback();

    esp_event_handler_register(ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, on_ota_event, NULL);

    const esp_app_desc_t *desc = esp_app_get_description();
    ESP_LOGI(TAG, "running %s v%s (device %s)",
             desc->project_name, desc->version, ota_device_id());

    char pending_to[OTA_VERSION_LEN];
    flash_get_str(OTA_NS, KEY_PEND_TO, pending_to, sizeof(pending_to), "");
    if (pending_to[0] != '\0') {
        ESP_LOGI(TAG, "last update targeted v%s, reporting result", pending_to);
        xTaskCreate(report_task, "ota_report", 4096, NULL, 2, NULL);
    }
}
