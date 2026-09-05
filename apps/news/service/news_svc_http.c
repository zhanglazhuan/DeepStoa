/**
 * @file news_svc_http.c
 * @brief news_svc.h 的真实实现 —— 连自建 news 服务端（见 server/news）
 *
 * 结构：LVGL 线程投递 job 到队列 → worker task 阻塞做 HTTP + JSON →
 *       结果堆分配后 lv_async_call 弹回 LVGL 线程派发回调（契约 1）。
 *
 * 服务端接口（server/news/app.py）：
 *   GET /api/news?page=1&page_size=6
 *     { "page":1, "page_size":6, "total":18, "has_more":true,
 *       "items":[ {"id","title","source","date","summary"}, ... ] }
 *   GET /api/news/{id}
 *     { "id","title","source","date","content" }
 *
 * 编译进来：idf.py -DNEWS_SVC=http build
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lvgl.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "wifi_manager.h"

#include "news_svc.h"
#include "../model.h"

static const char *TAG = "news_svc_http";

/* ── 服务端地址 ───────────────────────────────────────────────────────
 * 指向 PC 上的 server/run.py（news / ota / logs 都在这一个端口上）。
 *
 * 192.168.137.1 是 Windows「移动热点」的固定网关（ICS 默认网段）：板子连上
 * 本机热点后拿到 192.168.137.x，访问这个地址就是跑服务的那台 PC。写死它就
 * 不用每次开机去查本机 IP。
 * 换成别的组网方式（路由器 / Linux 热点）时，改成 run.py 启动横幅里打印的
 * 那个地址。 */
#define NEWS_API_BASE     "http://192.168.137.1:8000"

#define HTTP_TASK_STACK   6144
#define HTTP_TASK_PRIO    4
#define HTTP_QUEUE_DEPTH  4
#define HTTP_RX_MAX       (16 * 1024)   /* 单次响应体上限 */
#define HTTP_RX_CHUNK     1024

/* ── job / result ────────────────────────────────────────────────────── */

typedef enum { JOB_STOP = 0, JOB_LIST, JOB_DETAIL } job_kind_t;

typedef struct {
    job_kind_t            kind;
    uint32_t              req_id;
    news_svc_list_cb_t    list_cb;
    news_svc_detail_cb_t  detail_cb;
    void                 *user_data;
    uint16_t              page;
    uint16_t              page_size;
    char                  article_id[NEWS_ID_LEN];
} http_job_t;

typedef struct {
    job_kind_t            kind;
    uint32_t              req_id;
    news_svc_list_cb_t    list_cb;
    news_svc_detail_cb_t  detail_cb;
    void                 *user_data;
    news_svc_err_t        err;

    /* list */
    bool                  has_more;
    uint16_t              count;
    NewsArticle           items[NEWS_PAGE_SIZE];
    /* detail */
    char                 *content;      /* 堆上，派发后 free */
} http_result_t;

static QueueHandle_t      s_queue;
static TaskHandle_t       s_task;
static SemaphoreHandle_t  s_stopped;

/* abort 列表只在 LVGL 线程读写（news_svc_abort 与派发回调都在该线程），无需加锁 */
#define ABORT_SLOTS 4
static uint32_t s_aborted[ABORT_SLOTS];

static bool take_aborted(uint32_t req_id)
{
    for (int i = 0; i < ABORT_SLOTS; i++) {
        if (s_aborted[i] == req_id && req_id != 0) {
            s_aborted[i] = 0;
            return true;
        }
    }
    return false;
}

/* ── HTTP ─────────────────────────────────────────────────────────────── */

/** GET url，响应体写进新分配的 buf（调用方 free）。失败时 *err 已填好。 */
static bool http_get(const char *url, char **out_buf, int *out_len, news_svc_err_t *err)
{
    *out_buf = NULL;
    *out_len = 0;
    *err     = NEWS_SVC_ERR_NETWORK;

    esp_http_client_config_t cfg = {
        .url         = url,
        .method      = HTTP_METHOD_GET,
        .timeout_ms  = NEWS_SVC_TIMEOUT_MS,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) return false;

    bool ok = false;
    char *buf = NULL;

    esp_err_t e = esp_http_client_open(cli, 0);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "open %s failed: %s", url, esp_err_to_name(e));
        *err = (e == ESP_ERR_TIMEOUT) ? NEWS_SVC_ERR_TIMEOUT : NEWS_SVC_ERR_NETWORK;
        goto out;
    }

    if (esp_http_client_fetch_headers(cli) < 0) {
        *err = NEWS_SVC_ERR_NETWORK;
        goto out;
    }

    int status = esp_http_client_get_status_code(cli);
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "HTTP %d for %s", status, url);
        *err = NEWS_SVC_ERR_SERVER;
        goto out;
    }

    int cap = HTTP_RX_CHUNK, len = 0;
    buf = malloc(cap);
    if (!buf) { *err = NEWS_SVC_ERR_BUSY; goto out; }

    for (;;) {
        if (len + HTTP_RX_CHUNK + 1 > cap) {
            if (cap >= HTTP_RX_MAX) {
                ESP_LOGE(TAG, "response over %d bytes, giving up", HTTP_RX_MAX);
                *err = NEWS_SVC_ERR_BAD_REPLY;
                goto out;
            }
            int ncap = cap * 2;
            if (ncap > HTTP_RX_MAX) ncap = HTTP_RX_MAX;
            char *nb = realloc(buf, ncap);
            if (!nb) { *err = NEWS_SVC_ERR_BUSY; goto out; }
            buf = nb;
            cap = ncap;
        }

        int r = esp_http_client_read(cli, buf + len, cap - len - 1);
        if (r < 0) { *err = NEWS_SVC_ERR_NETWORK; goto out; }
        if (r == 0) break;                       /* 读完 */
        len += r;
    }
    buf[len] = '\0';

    *out_buf = buf;
    *out_len = len;
    *err     = NEWS_SVC_OK;
    buf      = NULL;      /* 所有权交出去 */
    ok       = true;

out:
    free(buf);
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);
    return ok;
}

/* ── JSON ─────────────────────────────────────────────────────────────── */

static void copy_str(char *dst, size_t dst_len, const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    snprintf(dst, dst_len, "%s", cJSON_IsString(v) && v->valuestring ? v->valuestring : "");
}

static news_svc_err_t parse_list(const char *json, http_result_t *res)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return NEWS_SVC_ERR_BAD_REPLY;

    news_svc_err_t err = NEWS_SVC_ERR_BAD_REPLY;

    const cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
    if (!cJSON_IsArray(items)) goto out;

    const cJSON *it = NULL;
    cJSON_ArrayForEach(it, items) {
        if (res->count >= NEWS_PAGE_SIZE) break;
        if (!cJSON_IsObject(it)) continue;
        NewsArticle *a = &res->items[res->count++];
        memset(a, 0, sizeof(*a));
        copy_str(a->id,      sizeof(a->id),      it, "id");
        copy_str(a->title,   sizeof(a->title),   it, "title");
        copy_str(a->source,  sizeof(a->source),  it, "source");
        copy_str(a->date,    sizeof(a->date),    it, "date");
        copy_str(a->summary, sizeof(a->summary), it, "summary");
    }

    const cJSON *hm = cJSON_GetObjectItemCaseSensitive(root, "has_more");
    res->has_more = cJSON_IsTrue(hm);
    err = NEWS_SVC_OK;

out:
    cJSON_Delete(root);
    return err;
}

static news_svc_err_t parse_detail(const char *json, http_result_t *res)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return NEWS_SVC_ERR_BAD_REPLY;

    news_svc_err_t err = NEWS_SVC_ERR_BAD_REPLY;

    const cJSON *c = cJSON_GetObjectItemCaseSensitive(root, "content");
    if (!cJSON_IsString(c) || !c->valuestring) goto out;

    res->content = malloc(NEWS_CONTENT_LEN);
    if (!res->content) { err = NEWS_SVC_ERR_BUSY; goto out; }
    snprintf(res->content, NEWS_CONTENT_LEN, "%s", c->valuestring);   /* 超长即截断 */
    err = NEWS_SVC_OK;

out:
    cJSON_Delete(root);
    return err;
}

/* ── 派发（LVGL 线程） ────────────────────────────────────────────────── */

static void deliver_cb(void *arg)
{
    http_result_t *res = arg;

    if (take_aborted(res->req_id)) {     /* 契约 2 的唯一例外 */
        ESP_LOGI(TAG, "req %lu aborted, dropping result", (unsigned long)res->req_id);
    } else if (res->kind == JOB_LIST) {
        res->list_cb(res->req_id,
                     res->err == NEWS_SVC_OK ? res->items : NULL,
                     res->err == NEWS_SVC_OK ? res->count : 0,
                     res->err == NEWS_SVC_OK ? res->has_more : false,
                     res->err, res->user_data);
    } else {
        res->detail_cb(res->req_id, res->err == NEWS_SVC_OK ? res->content : NULL,
                       res->err, res->user_data);
    }

    free(res->content);
    free(res);
}

/** 结果一定要走这里，保证契约 2：每个请求恰好回调一次。 */
static void post_result(http_result_t *res)
{
    if (lv_async_call(deliver_cb, res) != LV_RESULT_OK) {
        ESP_LOGE(TAG, "lv_async_call failed, dropping req %lu",
                 (unsigned long)res->req_id);
        free(res->content);
        free(res);
    }
}

/* ── worker task ──────────────────────────────────────────────────────── */

static void run_job(const http_job_t *job)
{
    http_result_t *res = calloc(1, sizeof(http_result_t));
    if (!res) {
        ESP_LOGE(TAG, "no memory for result of req %lu", (unsigned long)job->req_id);
        return;   /* 内存都没有了，只能丢；调用方的超时由 UI 上的刷新兜底 */
    }
    res->kind      = job->kind;
    res->req_id    = job->req_id;
    res->list_cb   = job->list_cb;
    res->detail_cb = job->detail_cb;
    res->user_data = job->user_data;

    if (!wifi_is_connected()) {
        res->err = NEWS_SVC_ERR_OFFLINE;
        post_result(res);
        return;
    }

    char url[160];
    if (job->kind == JOB_LIST) {
        snprintf(url, sizeof(url), NEWS_API_BASE "/api/news?page=%u&page_size=%u",
                 (unsigned)job->page, (unsigned)job->page_size);
    } else {
        snprintf(url, sizeof(url), NEWS_API_BASE "/api/news/%s", job->article_id);
    }
    ESP_LOGI(TAG, "GET %s", url);

    char *body = NULL;
    int   body_len = 0;
    news_svc_err_t err = NEWS_SVC_OK;
    if (!http_get(url, &body, &body_len, &err)) {
        res->err = err;
        post_result(res);
        return;
    }

    res->err = (job->kind == JOB_LIST) ? parse_list(body, res) : parse_detail(body, res);
    free(body);
    post_result(res);
}

static void http_task(void *arg)
{
    (void)arg;
    http_job_t job;
    for (;;) {
        if (xQueueReceive(s_queue, &job, portMAX_DELAY) != pdTRUE) continue;
        if (job.kind == JOB_STOP) break;
        run_job(&job);
    }
    xSemaphoreGive(s_stopped);
    vTaskDelete(NULL);
}

/* ── 公开 API ─────────────────────────────────────────────────────────── */

void news_svc_init(void)
{
    if (s_task) return;

    memset(s_aborted, 0, sizeof(s_aborted));
    s_queue   = xQueueCreate(HTTP_QUEUE_DEPTH, sizeof(http_job_t));
    s_stopped = xSemaphoreCreateBinary();
    if (!s_queue || !s_stopped) {
        ESP_LOGE(TAG, "failed to create queue/semaphore");
        return;
    }
    if (xTaskCreate(http_task, "news_http", HTTP_TASK_STACK, NULL,
                    HTTP_TASK_PRIO, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "failed to create worker task");
        s_task = NULL;
        return;
    }
    ESP_LOGI(TAG, "http news source ready, base=%s", NEWS_API_BASE);
}

void news_svc_deinit(void)
{
    if (!s_task) return;

    http_job_t stop = { .kind = JOB_STOP };
    xQueueSend(s_queue, &stop, portMAX_DELAY);
    xSemaphoreTake(s_stopped, pdMS_TO_TICKS(NEWS_SVC_TIMEOUT_MS + 1000));

    vQueueDelete(s_queue);
    vSemaphoreDelete(s_stopped);
    s_queue   = NULL;
    s_stopped = NULL;
    s_task    = NULL;
}

static bool submit(const http_job_t *job)
{
    if (!s_queue) return false;
    return xQueueSend(s_queue, job, 0) == pdTRUE;
}

void news_svc_fetch_list(uint32_t req_id, uint16_t page, uint16_t page_size,
                         news_svc_list_cb_t cb, void *user_data)
{
    if (!cb) return;

    http_job_t job = {
        .kind      = JOB_LIST,
        .req_id    = req_id,
        .list_cb   = cb,
        .user_data = user_data,
        .page      = page,
        .page_size = page_size,
    };
    if (!submit(&job)) {
        cb(req_id, NULL, 0, false, NEWS_SVC_ERR_BUSY, user_data);   /* 契约 2 */
    }
}

void news_svc_fetch_detail(uint32_t req_id, const char *article_id,
                           news_svc_detail_cb_t cb, void *user_data)
{
    if (!cb) return;

    http_job_t job = {
        .kind      = JOB_DETAIL,
        .req_id    = req_id,
        .detail_cb = cb,
        .user_data = user_data,
    };
    snprintf(job.article_id, sizeof(job.article_id), "%s", article_id ? article_id : "");

    if (!submit(&job)) {
        cb(req_id, NULL, NEWS_SVC_ERR_BUSY, user_data);
    }
}

void news_svc_abort(uint32_t req_id)
{
    if (req_id == 0) return;
    for (int i = 0; i < ABORT_SLOTS; i++) {
        if (s_aborted[i] == 0) {
            s_aborted[i] = req_id;
            ESP_LOGI(TAG, "abort req=%lu", (unsigned long)req_id);
            return;
        }
    }
    ESP_LOGW(TAG, "abort slots full, req %lu will still be delivered",
             (unsigned long)req_id);
}

/* http 实现里没有故障注入，留空保持两份实现接口一致 */
void news_svc_mock_inject(news_svc_err_t err)
{
    (void)err;
}
