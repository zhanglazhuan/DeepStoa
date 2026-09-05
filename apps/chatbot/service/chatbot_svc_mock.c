/**
 * @file chatbot_svc_mock.c
 * @brief chatbot_svc.h 的 mock 实现
 *
 * 刻意做成"时序模拟器"而不是返回一句固定话：
 *   - 用 lv_timer 延迟 800~2000ms 回调 → 验证 PENDING 占位气泡是否正确显示和替换
 *   - 故障注入 → 逐条走通错误码表里的每个 UI 分支
 *   - 变长回复（含一条超过 MAX_MSG_LENGTH 的）→ 验证长气泡换行、截断标记、滚动
 *
 * 阶段 1 接真实 API 时新增 chatbot_svc_http.c，本文件整份替换掉。
 */

#include <stdio.h>
#include <string.h>
#include <lvgl.h>
#include "esp_log.h"

#include "chatbot_svc.h"
#include "../model.h"

static const char *TAG = "chatbot_svc_mock";

#define MOCK_ASR_DELAY_MS   900
#define MOCK_LLM_DELAY_MS   1800
#define MOCK_TEXT_MAX       320   /* 故意大于 MAX_MSG_LENGTH，用来测截断 */

typedef struct {
    uint32_t              msg_id;
    chatbot_svc_text_cb_t cb;
    void                 *user_data;
    chatbot_svc_err_t     err;
    char                  text[MOCK_TEXT_MAX];
    lv_timer_t           *timer;
    bool                  aborted;
} mock_req_t;

/* 同时最多两个在途请求（一次 ASR + 一次 LLM），够用且不用动态分配 */
#define MOCK_MAX_REQ 2
static mock_req_t s_reqs[MOCK_MAX_REQ];

static chatbot_svc_err_t s_injected = CHATBOT_SVC_OK;
static uint16_t          s_reply_len = 0;

/* mock 回复池：长度递增，覆盖短/中/长/超长四档 */
static const char *k_replies[] = {
    "Sure.",
    "I am here. What can I help you with?",
    "Three angles on that: pin down the goal, break it into steps, then schedule the time. Which part first?",
    "This reply is deliberately long so it can exercise word wrapping in the bubble, "
    "scrolling to the bottom of the list, and the truncation marker that appears once a "
    "message runs past MAX_MSG_LENGTH. If you can see an ellipsis and a truncated tag at "
    "the end of this text then that path works. If the bubble overflows the screen, or the "
    "list does not stop at the newest message, the layout still needs work.",
};
#define REPLY_COUNT (sizeof(k_replies) / sizeof(k_replies[0]))

static mock_req_t *alloc_req(void)
{
    for (int i = 0; i < MOCK_MAX_REQ; i++) {
        if (s_reqs[i].timer == NULL) return &s_reqs[i];
    }
    return NULL;
}

static mock_req_t *find_req(uint32_t msg_id)
{
    for (int i = 0; i < MOCK_MAX_REQ; i++) {
        if (s_reqs[i].timer && s_reqs[i].msg_id == msg_id) return &s_reqs[i];
    }
    return NULL;
}

/* lv_timer 回调天然在 LVGL 线程 —— 契约 1 自动满足 */
static void mock_fire_cb(lv_timer_t *t)
{
    mock_req_t *r = lv_timer_get_user_data(t);

    lv_timer_delete(t);
    r->timer = NULL;

    if (r->aborted) {           /* 契约 2 的唯一例外：被 abort 的请求不回调 */
        ESP_LOGI(TAG, "req %lu aborted, dropping result", (unsigned long)r->msg_id);
        memset(r, 0, sizeof(*r));
        return;
    }

    chatbot_svc_text_cb_t cb = r->cb;
    uint32_t  msg_id = r->msg_id;
    void     *ud     = r->user_data;
    chatbot_svc_err_t err = r->err;
    static char text[MOCK_TEXT_MAX];
    strncpy(text, r->text, sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';

    memset(r, 0, sizeof(*r));
    cb(msg_id, err == CHATBOT_SVC_OK ? text : NULL, err, ud);
}

static void schedule(mock_req_t *r, uint32_t delay_ms)
{
    r->timer = lv_timer_create(mock_fire_cb, delay_ms, r);
    lv_timer_set_repeat_count(r->timer, 1);
}

/* 取一次注入的故障；没有注入时返回 OK */
static chatbot_svc_err_t take_injected(void)
{
    chatbot_svc_err_t e = s_injected;
    s_injected = CHATBOT_SVC_OK;
    return e;
}

void chatbot_svc_init(void)
{
    memset(s_reqs, 0, sizeof(s_reqs));
    s_injected  = CHATBOT_SVC_OK;
    s_reply_len = 0;
    ESP_LOGI(TAG, "mock service ready (no real backend)");
}

void chatbot_svc_deinit(void)
{
    for (int i = 0; i < MOCK_MAX_REQ; i++) {
        if (s_reqs[i].timer) lv_timer_delete(s_reqs[i].timer);
    }
    memset(s_reqs, 0, sizeof(s_reqs));
}

void chatbot_svc_transcribe(uint32_t msg_id,
                            const chatbot_audio_clip_t *clip,
                            chatbot_svc_text_cb_t cb, void *user_data)
{
    if (!cb) return;

    mock_req_t *r = alloc_req();
    if (!r) {   /* 契约 2：即使内部资源不足也必须回调 */
        cb(msg_id, NULL, CHATBOT_SVC_ERR_NETWORK, user_data);
        return;
    }

    memset(r, 0, sizeof(*r));
    r->msg_id    = msg_id;
    r->cb        = cb;
    r->user_data = user_data;
    r->err       = take_injected();

    /* 注意：mock 刻意不检查 wifi_is_connected()。
     * 调试阶段设备常常没连网，若在这里硬性拦截就永远跑不出一次成功对话。
     * 想验证离线分支用 chatbot_svc_mock_inject(CHATBOT_SVC_ERR_OFFLINE)。
     * 真实实现 chatbot_svc_http.c 里这个检查是必须有的。 */
    if (r->err == CHATBOT_SVC_OK) {
        uint32_t dur = clip ? clip->duration_ms : 0;
        snprintf(r->text, sizeof(r->text), "(mock transcript) about %lu.%lu seconds of speech",
                 (unsigned long)(dur / 1000), (unsigned long)((dur % 1000) / 100));
    }

    ESP_LOGI(TAG, "transcribe msg=%lu err=%d", (unsigned long)msg_id, (int)r->err);
    schedule(r, MOCK_ASR_DELAY_MS);
}

void chatbot_svc_complete(uint32_t session_id, uint32_t msg_id,
                          const struct ChatMessage *ctx, uint16_t ctx_count,
                          chatbot_svc_text_cb_t cb, void *user_data)
{
    (void)session_id;
    if (!cb) return;

    mock_req_t *r = alloc_req();
    if (!r) {
        cb(msg_id, NULL, CHATBOT_SVC_ERR_NETWORK, user_data);
        return;
    }

    memset(r, 0, sizeof(*r));
    r->msg_id    = msg_id;
    r->cb        = cb;
    r->user_data = user_data;
    r->err       = take_injected();

    if (r->err == CHATBOT_SVC_OK) {
        size_t idx;
        if (s_reply_len > 0) {
            /* 调试面板指定了长度档位 */
            idx = (s_reply_len - 1) % REPLY_COUNT;
        } else {
            /* 按上下文条数轮换，让连续对话能看到不同长度的回复 */
            idx = ctx_count % REPLY_COUNT;
        }
        strncpy(r->text, k_replies[idx], sizeof(r->text) - 1);
        r->text[sizeof(r->text) - 1] = '\0';
    }

    ESP_LOGI(TAG, "complete msg=%lu ctx=%u err=%d",
             (unsigned long)msg_id, ctx_count, (int)r->err);
    schedule(r, MOCK_LLM_DELAY_MS);
}

void chatbot_svc_abort(uint32_t msg_id)
{
    mock_req_t *r = find_req(msg_id);
    if (!r) return;
    r->aborted = true;
    ESP_LOGI(TAG, "abort msg=%lu", (unsigned long)msg_id);
}

/* ── 故障注入 ─────────────────────────────────────────────────────── */

void chatbot_svc_mock_inject(chatbot_svc_err_t err)
{
    s_injected = err;
    ESP_LOGW(TAG, "next request will report err=%d", (int)err);
}

void chatbot_svc_mock_set_reply_len(uint16_t len)
{
    s_reply_len = len;
}
