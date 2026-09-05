#include <string.h>
#include <stdio.h>

#include <lvgl.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "settings_app.h"
#include "settings_model.h"
#include "settings_view.h"
#include "subpages/view_wifi.h"
#include "controller_wifi.h"
#include "wifi_manager.h"
#include "wifi_store.h"

static const char *TAG = "settings_ctrl_wifi";

#define WORKER_STACK    4096
#define WORKER_PRIO     4

typedef enum {
    REQ_ENABLE = 0,
    REQ_DISABLE,
    REQ_SCAN,
    REQ_CONNECT,
    REQ_DISCONNECT,
    REQ_FORGET,
} wifi_req_op_t;

typedef struct {
    wifi_req_op_t op;
    char ssid[WIFI_SSID_MAX];
    char pwd[WIFI_PWD_MAX];
} wifi_req_t;

/* 扫描完自动连"信号最强的已知网络"。
 * 只在当前没连上时做 —— 手动点"重新扫描"不该把用户从当前网络上踢走。
 * 这比"只重连最后一个"实用：从家里到公司会自己切过去。 */
static void auto_connect_best_known(settings_model_wifi_t *w);

static SettingsApp   *s_app;
static TaskHandle_t   s_worker;
static QueueHandle_t  s_queue;

/* worker 把结果写这里，再 lv_async_call 弹回 LVGL 线程消费。
 * 同一时刻只有一个请求在跑（队列长度 1 + busy 标志），所以单份缓冲够用。 */
static struct {
    wifi_req_op_t op;
    bool          ok;
    char          err[64];
    wifi_ap_record_t recs[WIFI_SCAN_MAX];
    uint16_t      found;
} s_result;

/* ── LVGL 线程：把结果落到 model 并刷新 UI ─────────────────────────── */

static void refresh_ui(void)
{
    if (!s_app || !s_app->view) return;
    settings_view_wifi_update_visibility(s_app);
    settings_view_wifi_update_connected(s_app);
    settings_view_wifi_update_scanned_list(s_app);
}

static void apply_result_cb(void *arg)
{
    (void)arg;
    if (!s_app || !s_app->model) return;
    settings_model_wifi_t *w = &s_app->model->wifi;

    w->busy = false;
    w->busy_text[0] = '\0';
    snprintf(w->last_error, sizeof(w->last_error), "%s", s_result.ok ? "" : s_result.err);

    switch (s_result.op) {
    case REQ_ENABLE:
        w->enabled = s_result.ok ? 1 : 0;
        settings_model_save_wifi(s_app);
        /* 先扫一遍，扫完再挑"信号最强的已知网络"连过去。
         * 不直接重连最后一个 —— 那个可能根本不在附近。 */
        if (s_result.ok) settings_controller_wifi_scan();
        break;

    case REQ_DISABLE:
        w->enabled       = 0;
        w->state         = WIFI_UI_DISCONNECTED;
        w->scanned_count = 0;
        memset(&w->current, 0, sizeof(w->current));
        settings_model_save_wifi(s_app);
        break;

    case REQ_SCAN:
        w->scanned_count = 0;
        for (uint16_t i = 0; i < s_result.found && w->scanned_count < WIFI_SCAN_MAX; i++) {
            const wifi_ap_record_t *r = &s_result.recs[i];
            if (r->ssid[0] == '\0') continue;              /* 隐藏 SSID，跳过 */

            /* 同名 AP（多个 band / mesh 节点）只留信号最强的那个 */
            bool dup = false;
            for (uint8_t k = 0; k < w->scanned_count; k++) {
                if (strcmp(w->scanned[k].ssid, (const char *)r->ssid) == 0) {
                    if (r->rssi > w->scanned[k].rssi) {
                        w->scanned[k].rssi    = r->rssi;
                        w->scanned[k].channel = r->primary;
                    }
                    dup = true;
                    break;
                }
            }
            if (dup) continue;

            wifi_scan_entry_t *e = &w->scanned[w->scanned_count++];
            snprintf(e->ssid, sizeof(e->ssid), "%s", (const char *)r->ssid);
            e->rssi    = r->rssi;
            e->channel = r->primary;
            e->secured = (r->authmode != WIFI_AUTH_OPEN);
        }
        ESP_LOGI(TAG, "scan done: %u APs", w->scanned_count);
        auto_connect_best_known(w);
        break;

    case REQ_CONNECT:
        if (s_result.ok) {
            w->state = WIFI_UI_CONNECTED;
            wifi_get_info(&w->current);
        } else {
            w->state = WIFI_UI_DISCONNECTED;
            memset(&w->current, 0, sizeof(w->current));
        }
        break;

    case REQ_DISCONNECT:
    case REQ_FORGET:
        w->state = WIFI_UI_DISCONNECTED;
        memset(&w->current, 0, sizeof(w->current));
        break;
    }

    refresh_ui();
}

/* ── worker 任务：只干阻塞活，不碰 model / LVGL ───────────────────── */

static void worker_task(void *arg)
{
    (void)arg;
    wifi_req_t req;

    for (;;) {
        if (xQueueReceive(s_queue, &req, portMAX_DELAY) != pdTRUE) continue;

        memset(&s_result, 0, sizeof(s_result));
        s_result.op = req.op;
        esp_err_t err = ESP_OK;

        switch (req.op) {
        case REQ_ENABLE:
            err = wifi_start();
            break;

        case REQ_DISABLE:
            err = wifi_stop();
            break;

        case REQ_SCAN:
            err = wifi_scan(s_result.recs, WIFI_SCAN_MAX, &s_result.found, 0);
            break;

        case REQ_CONNECT:
            /* 密码为空 = 用已保存的凭据重连 */
            err = (req.pwd[0] == '\0') ? wifi_reconnect()
                                       : wifi_connect(req.ssid, req.pwd);
            break;

        case REQ_DISCONNECT:
            err = wifi_disconnect();
            break;

        case REQ_FORGET:
            err = wifi_forget(req.ssid);
            break;
        }

        s_result.ok = (err == ESP_OK);
        if (!s_result.ok) {
            /* 文案面向用户，具体错误码进日志 */
            const char *msg;
            switch (req.op) {
            case REQ_SCAN:    msg = "Scan failed, try again";       break;
            case REQ_CONNECT: msg = "Connect failed, check password";   break;
            case REQ_ENABLE:  msg = "Could not turn on Wi-Fi";        break;
            default:          msg = "Operation failed";              break;
            }
            snprintf(s_result.err, sizeof(s_result.err), "%s", msg);
            ESP_LOGW(TAG, "op=%d failed: %s", (int)req.op, esp_err_to_name(err));
        }

        lv_async_call(apply_result_cb, NULL);
    }
}

/* ── 请求入队 ─────────────────────────────────────────────────────── */

static void submit(wifi_req_op_t op, const char *ssid, const char *pwd,
                   const char *busy_text)
{
    if (!s_app || !s_app->model || !s_queue) return;
    settings_model_wifi_t *w = &s_app->model->wifi;

    if (w->busy) {
        ESP_LOGW(TAG, "busy, ignoring op=%d", (int)op);
        return;
    }

    wifi_req_t req = { .op = op };
    if (ssid) snprintf(req.ssid, sizeof(req.ssid), "%s", ssid);
    if (pwd)  snprintf(req.pwd,  sizeof(req.pwd),  "%s", pwd);

    if (xQueueSend(s_queue, &req, 0) != pdTRUE) {
        ESP_LOGE(TAG, "queue full");
        return;
    }

    w->busy = true;
    snprintf(w->busy_text, sizeof(w->busy_text), "%s", busy_text ? busy_text : "");
    w->last_error[0] = '\0';
    refresh_ui();
}

/* ── 对外接口 ─────────────────────────────────────────────────────── */

void settings_controller_wifi_set_enabled(bool on)
{
    submit(on ? REQ_ENABLE : REQ_DISABLE, NULL, NULL,
           on ? "Turning on..." : "Turning off...");
}

void settings_controller_wifi_toggle(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    settings_controller_wifi_set_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

void settings_controller_wifi_scan(void)
{
    if (!s_app || !s_app->model || !s_app->model->wifi.enabled) return;
    submit(REQ_SCAN, NULL, NULL, "Scanning...");
}

void settings_controller_wifi_connect(const char *ssid, const char *password)
{
    if (!s_app || !s_app->model || !ssid) return;

    settings_model_wifi_t *w = &s_app->model->wifi;
    snprintf(w->pending_ssid, sizeof(w->pending_ssid), "%s", ssid);
    w->state = WIFI_UI_CONNECTING;
    snprintf(w->current.ssid, sizeof(w->current.ssid), "%s", ssid);

    submit(REQ_CONNECT, ssid, password, "Connecting...");
}

void settings_controller_wifi_disconnect(void)
{
    submit(REQ_DISCONNECT, NULL, NULL, "Disconnecting...");
}

void settings_controller_wifi_forget(const char *ssid)
{
    submit(REQ_FORGET, ssid, NULL, "Forgetting...");
}

/* 扫描结果里挑一个"已保存 + 允许自动连 + 信号最强"的连过去 */
static void auto_connect_best_known(settings_model_wifi_t *w)
{
    if (!w->enabled) return;
    if (w->state != WIFI_UI_DISCONNECTED) return;   /* 已连上就别打扰 */

    const wifi_cred_t *best = NULL;
    int8_t best_rssi = -128;

    for (uint8_t i = 0; i < w->scanned_count; i++) {
        const wifi_cred_t *c = wifi_store_find(w->scanned[i].ssid);
        if (!c || !c->auto_connect) continue;
        if (w->scanned[i].rssi <= best_rssi) continue;
        best      = c;
        best_rssi = w->scanned[i].rssi;
    }
    if (!best) return;

    ESP_LOGI(TAG, "auto-connect to known \"%s\" (%d dBm)", best->ssid, best_rssi);
    w->busy = false;   /* submit() 会查 busy，这里是紧接着扫描完的链式调用 */
    settings_controller_wifi_connect(best->ssid, best->password);
}

bool settings_controller_wifi_busy(void)
{
    return (s_app && s_app->model) ? s_app->model->wifi.busy : false;
}

bool settings_controller_wifi_is_saved(const char *ssid)
{
    if (!ssid || ssid[0] == '\0') return false;

    char saved_ssid[WIFI_SSID_MAX] = {0};
    char saved_pwd[WIFI_PWD_MAX]   = {0};
    if (wifi_get_config(saved_ssid, sizeof(saved_ssid),
                        saved_pwd, sizeof(saved_pwd)) != ESP_OK) {
        return false;
    }
    return (saved_ssid[0] != '\0') && (strcmp(saved_ssid, ssid) == 0);
}

/* ── 生命周期 ─────────────────────────────────────────────────────── */

void settings_controller_wifi_init(struct SettingsApp *app)
{
    s_app = app;
    if (s_worker) return;   /* 只建一次 */

    s_queue = xQueueCreate(2, sizeof(wifi_req_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "queue alloc failed");
        return;
    }
    if (xTaskCreate(worker_task, "wifi_ui", WORKER_STACK, NULL,
                    WORKER_PRIO, &s_worker) != pdPASS) {
        ESP_LOGE(TAG, "worker task create failed");
        vQueueDelete(s_queue);
        s_queue = NULL;
        return;
    }

    /* 上次是开着的就恢复电台。REQ_ENABLE 完成后会自动扫描，
     * 扫完再挑信号最强的已知网络连上 —— 用户不该每次开机手动来一遍。 */
    if (app->model && app->model->wifi.enabled) {
        ESP_LOGI(TAG, "restoring Wi-Fi (was enabled, %u saved network(s))",
                 wifi_store_count());
        submit(REQ_ENABLE, NULL, NULL, "Turning on...");
    }
}

void settings_controller_wifi_deinit(void)
{
    /* worker 和队列刻意不销毁：Settings app 可能被反复开关，
     * 而正在进行的连接不该因为退出设置页就被打断。
     * 只是断开和 app 的联系，避免回调里碰到已释放的 model。 */
    s_app = NULL;
}
