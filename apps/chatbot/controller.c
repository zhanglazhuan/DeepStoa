/**
 * @file controller.c
 * @brief ChatBot 状态机 —— 全应用唯一的一个
 *
 *   IDLE ──press──> RECORDING ──release(≥MIN)──> TRANSCRIBING ──> THINKING ──> IDLE
 *                       │                            │               │
 *                cancel/press_lost/太短               └──err──────────┴──> IDLE + 失败气泡
 *
 * 不变量：
 *   1. 任何状态都有出口（异步阶段的超时由 service 层保证）
 *   2. RECORDING 期间禁用菜单 / 新对话 / 切页
 *   3. TRANSCRIBING / THINKING 期间录音按钮变"取消"
 *   4. 进入 IDLE 必须清干净：取消录音、停计时 timer、恢复按钮文案
 */

#include "esp_log.h"
#include <lvgl.h>
#include <string.h>
#include <stdlib.h>

#include "app.h"
#include "controller.h"
#include "view.h"
#include "storage.h"
#include "service/chatbot_audio.h"
#include "service/chatbot_svc.h"
#include "wifi_manager.h"
#include "lv_toast.h"

static const char *TAG = "chatbot_controller";

#define REC_TICK_MS 500   /* 计时 + 音量格刷新间隔 */

static ChatbotApp *s_app;   /* service 回调没有 app 指针，这里存一份 */

static void enter_idle(void);
static void start_llm(uint32_t user_msg_id);

/* ── 小工具 ───────────────────────────────────────────────────────── */

static ChatbotController *ctrl(void)
{
    return (s_app && s_app->controller) ? s_app->controller : NULL;
}

static void toast_err(ChatErrCode err)
{
    lv_toast_show(chatbot_err_text(err), 2000);
}

/** 存储写失败时给一次提示（storage 层记录，读取即清除） */
static void check_store(void)
{
    if (chatbot_storage_last_write_failed()) {
        toast_err(CHAT_ERR_STORE_FULL);
    }
}

/** 网络横幅：只反映真实状态，不阻断录音 —— 调试期设备常常没连网 */
static void refresh_banner(void)
{
    if (!s_app) return;
    if (!chatbot_audio_is_available()) {
        chatbot_view_set_banner(s_app, chatbot_err_text(CHAT_ERR_MIC_INIT), true);
    } else if (!wifi_is_connected()) {
        chatbot_view_set_banner(s_app, chatbot_err_text(CHAT_ERR_NET_OFFLINE), true);
    } else {
        chatbot_view_set_banner(s_app, NULL, false);
    }
}

/* ── service 错误码 → 展示用错误码 ─────────────────────────────────── */

static ChatErrCode map_svc_err(chatbot_svc_err_t e, bool is_asr)
{
    switch (e) {
    case CHATBOT_SVC_ERR_OFFLINE:    return CHAT_ERR_NET_OFFLINE;
    case CHATBOT_SVC_ERR_TIMEOUT:    return CHAT_ERR_NET_TIMEOUT;
    case CHATBOT_SVC_ERR_NETWORK:    return CHAT_ERR_NET_FAIL;
    case CHATBOT_SVC_ERR_ASR_EMPTY:  return CHAT_ERR_ASR_EMPTY;
    case CHATBOT_SVC_ERR_RATE_LIMIT: return CHAT_ERR_LLM_RATE;
    case CHATBOT_SVC_ERR_SERVER:
    case CHATBOT_SVC_ERR_BAD_REPLY:  return is_asr ? CHAT_ERR_ASR_FAIL : CHAT_ERR_LLM_FAIL;
    default:                         return CHAT_ERR_NET_FAIL;
    }
}

static ChatErrCode map_audio_err(chatbot_audio_err_t e)
{
    switch (e) {
    case CHATBOT_AUDIO_ERR_INIT:      return CHAT_ERR_MIC_INIT;
    case CHATBOT_AUDIO_ERR_TOO_SHORT: return CHAT_ERR_REC_TOO_SHORT;
    case CHATBOT_AUDIO_ERR_SILENT:    return CHAT_ERR_REC_SILENT;
    case CHATBOT_AUDIO_ERR_OVERFLOW:  return CHAT_ERR_REC_OVERFLOW;
    default:                          return CHAT_ERR_NONE;
    }
}

/* ── 状态迁移 ─────────────────────────────────────────────────────── */

static void stop_rec_timer(void)
{
    ChatbotController *c = ctrl();
    if (c && c->rec_timer) {
        lv_timer_delete(c->rec_timer);
        c->rec_timer = NULL;
    }
}

static void enter_idle(void)
{
    ChatbotController *c = ctrl();
    if (!c) return;

    stop_rec_timer();
    chatbot_audio_cancel();
    c->state = CHAT_STATE_IDLE;
    c->pending_user_msg = 0;
    c->pending_ai_msg   = 0;

    chatbot_view_set_state(s_app, CHAT_STATE_IDLE);
    refresh_banner();
}

/* 1Hz 计时：更新秒数与音量格；到 MAX 自动停止并发送 */
static void rec_tick_cb(lv_timer_t *t)
{
    (void)t;
    ChatbotController *c = ctrl();
    if (!c || c->state != CHAT_STATE_RECORDING) return;

    uint32_t ms = chatbot_audio_elapsed_ms();
    chatbot_view_update_recording(s_app, ms, chatbot_audio_level());

    if (ms >= CHATBOT_REC_MAX_MS) {
        ESP_LOGW(TAG, "max record length reached, auto-sending");
        toast_err(CHAT_ERR_REC_TOO_LONG);
        chatbot_controller_on_talk_release();
    }
}

/* ── ASR / LLM 回调（service 保证在 LVGL 线程）────────────────────── */

static void on_llm_done(uint32_t msg_id, const char *text,
                        chatbot_svc_err_t err, void *user_data)
{
    (void)user_data;
    ChatbotController *c = ctrl();
    if (!c || c->pending_ai_msg != msg_id) return;   /* 已被取消或换了会话 */

    if (err == CHATBOT_SVC_OK) {
        chatbot_model_resolve_message(c->model, msg_id, MSG_STATE_OK, text, CHAT_ERR_NONE);
    } else {
        ChatErrCode ec = map_svc_err(err, false);
        ESP_LOGW(TAG, "LLM failed: svc=%d -> %d", (int)err, (int)ec);
        chatbot_model_resolve_message(c->model, msg_id, MSG_STATE_FAILED, NULL, ec);
    }

    chatbot_storage_save_messages(c->model);
    chatbot_storage_save_index(c->model);
    check_store();

    enter_idle();
    chatbot_view_refresh(s_app);
}

static void on_asr_done(uint32_t msg_id, const char *text,
                        chatbot_svc_err_t err, void *user_data)
{
    (void)user_data;
    ChatbotController *c = ctrl();
    if (!c || c->pending_user_msg != msg_id) return;

    if (err != CHATBOT_SVC_OK) {
        ChatErrCode ec = map_svc_err(err, true);
        ESP_LOGW(TAG, "ASR failed: svc=%d -> %d", (int)err, (int)ec);
        chatbot_model_resolve_message(c->model, msg_id, MSG_STATE_FAILED, NULL, ec);
        chatbot_storage_save_messages(c->model);
        check_store();
        enter_idle();
        chatbot_view_refresh(s_app);
        return;
    }

    /* 用户气泡就地从"识别中…"替换成文本 */
    chatbot_model_resolve_message(c->model, msg_id, MSG_STATE_OK, text, CHAT_ERR_NONE);
    chatbot_view_refresh(s_app);

    start_llm(msg_id);
}

/* 发起大模型请求：插入 PENDING 的 AI 气泡，带上裁剪后的上下文 */
static void start_llm(uint32_t user_msg_id)
{
    ChatbotController *c = ctrl();
    if (!c) return;

    uint32_t ai_id = chatbot_model_add_message(c->model, MSG_ROLE_AI,
                                               MSG_STATE_PENDING, NULL, 0);
    if (ai_id == 0) { enter_idle(); return; }

    c->pending_ai_msg   = ai_id;
    c->pending_user_msg = user_msg_id;
    c->state = CHAT_STATE_THINKING;

    chatbot_view_set_state(s_app, CHAT_STATE_THINKING);
    chatbot_view_refresh(s_app);

    /* 上下文窗口：只带最近 CHATBOT_CTX_MESSAGES 条，且跳过刚插入的占位气泡 */
    uint16_t total = c->model->msg_count > 0 ? (uint16_t)(c->model->msg_count - 1) : 0;
    uint16_t take  = total > CHATBOT_CTX_MESSAGES ? CHATBOT_CTX_MESSAGES : total;
    const ChatMessage *ctx = &c->model->messages[total - take];

    chatbot_svc_complete(c->model->active_session_id, ai_id, ctx, take,
                         on_llm_done, NULL);
}

/* 录音结束回调（audio 层保证在 LVGL 线程）*/
static void on_audio_done(const chatbot_audio_clip_t *clip,
                          chatbot_audio_err_t err, void *user_data)
{
    (void)user_data;
    ChatbotController *c = ctrl();
    if (!c || c->state != CHAT_STATE_RECORDING) return;

    stop_rec_timer();

    if (err != CHATBOT_AUDIO_OK) {
        ChatErrCode ec = map_audio_err(err);
        ESP_LOGW(TAG, "record failed: audio=%d -> %d", (int)err, (int)ec);

        if (err == CHATBOT_AUDIO_ERR_TOO_SHORT || err == CHATBOT_AUDIO_ERR_SILENT) {
            /* L1：瞬时问题，不产生消息 */
            toast_err(ec);
            enter_idle();
            return;
        }
        /* L2：产生一条失败的用户消息，可重试 */
        chatbot_model_add_message(c->model, MSG_ROLE_USER, MSG_STATE_FAILED, NULL,
                                  clip ? (uint16_t)clip->duration_ms : 0);
        ChatMessage *m = &c->model->messages[c->model->msg_count - 1];
        m->err_code = (uint8_t)ec;
        enter_idle();
        chatbot_view_refresh(s_app);
        return;
    }

    /* 关键交互：用户气泡先上屏（"识别中…"），ASR 回来后就地替换 */
    uint32_t uid = chatbot_model_add_message(c->model, MSG_ROLE_USER,
                                             MSG_STATE_PENDING, NULL,
                                             (uint16_t)clip->duration_ms);
    if (uid == 0) { enter_idle(); return; }

    c->pending_user_msg = uid;
    c->state = CHAT_STATE_TRANSCRIBING;

    chatbot_view_set_state(s_app, CHAT_STATE_TRANSCRIBING);
    chatbot_view_refresh(s_app);

    chatbot_svc_transcribe(uid, clip, on_asr_done, NULL);
}

/* ── 录音触发（屏幕按钮 / 未来的物理按键共用）──────────────────────── */

void chatbot_controller_on_talk_press(void)
{
    ChatbotController *c = ctrl();
    if (!c) return;

    /* 等待中再按 = 取消 */
    if (c->state == CHAT_STATE_TRANSCRIBING || c->state == CHAT_STATE_THINKING) {
        chatbot_controller_abort();
        return;
    }
    if (c->state != CHAT_STATE_IDLE) return;

    if (!chatbot_audio_is_available()) {
        toast_err(CHAT_ERR_MIC_INIT);
        refresh_banner();
        return;
    }

    if (chatbot_audio_start() != ESP_OK) {
        toast_err(CHAT_ERR_MIC_INIT);
        return;
    }

    c->state = CHAT_STATE_RECORDING;
    chatbot_view_set_state(s_app, CHAT_STATE_RECORDING);

    stop_rec_timer();
    c->rec_timer = lv_timer_create(rec_tick_cb, REC_TICK_MS, NULL);
    ESP_LOGI(TAG, "recording");
}

void chatbot_controller_on_talk_release(void)
{
    ChatbotController *c = ctrl();
    if (!c || c->state != CHAT_STATE_RECORDING) return;

    stop_rec_timer();
    chatbot_audio_stop(on_audio_done, NULL);
}

void chatbot_controller_on_talk_cancel(void)
{
    ChatbotController *c = ctrl();
    if (!c || c->state != CHAT_STATE_RECORDING) return;

    ESP_LOGI(TAG, "recording cancelled by user");
    enter_idle();
}

/* LVGL 事件适配：把按钮事件翻译成上面三个动作。
 * PRESS_LOST 必须处理 —— 手指按住后滑出按钮区域时 LVGL 只发 PRESS_LOST，
 * 不发 RELEASED，漏掉会永久卡在 RECORDING。 */
void chatbot_controller_record_event_cb(lv_event_t *e)
{
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:     chatbot_controller_on_talk_press();   break;
    case LV_EVENT_RELEASED:    chatbot_controller_on_talk_release(); break;
    case LV_EVENT_PRESS_LOST:  chatbot_controller_on_talk_cancel();  break;
    default: break;
    }
}

/* ── 文字输入兜底 ─────────────────────────────────────────────────── */

void chatbot_controller_send_text(const char *text)
{
    ChatbotController *c = ctrl();
    if (!c || !text || text[0] == '\0') return;
    if (c->state != CHAT_STATE_IDLE) return;

    uint32_t uid = chatbot_model_add_message(c->model, MSG_ROLE_USER,
                                             MSG_STATE_OK, text, 0);
    if (uid == 0) return;

    chatbot_view_refresh(s_app);
    start_llm(uid);
}

/* ── 取消 / 重试 ──────────────────────────────────────────────────── */

void chatbot_controller_abort(void)
{
    ChatbotController *c = ctrl();
    if (!c) return;

    if (c->state == CHAT_STATE_TRANSCRIBING && c->pending_user_msg) {
        chatbot_svc_abort(c->pending_user_msg);
        chatbot_model_resolve_message(c->model, c->pending_user_msg,
                                      MSG_STATE_CANCELLED, NULL, CHAT_ERR_NONE);
    } else if (c->state == CHAT_STATE_THINKING && c->pending_ai_msg) {
        chatbot_svc_abort(c->pending_ai_msg);
        chatbot_model_resolve_message(c->model, c->pending_ai_msg,
                                      MSG_STATE_CANCELLED, NULL, CHAT_ERR_NONE);
    } else {
        return;
    }

    ESP_LOGI(TAG, "request aborted by user");
    enter_idle();
    chatbot_view_refresh(s_app);
}

void chatbot_controller_retry(uint32_t msg_id)
{
    ChatbotController *c = ctrl();
    if (!c || c->state != CHAT_STATE_IDLE) return;

    ChatMessage *m = chatbot_model_find_message(c->model, msg_id);
    if (!m || m->state != MSG_STATE_FAILED) return;

    /* 省一步：ASR 已经成功过（有文本），直接重发 LLM，不再走一次识别 */
    if (m->role == MSG_ROLE_AI) {
        m->state    = MSG_STATE_PENDING;
        m->err_code = CHAT_ERR_NONE;
        c->pending_ai_msg = msg_id;
        c->state = CHAT_STATE_THINKING;

        chatbot_view_set_state(s_app, CHAT_STATE_THINKING);
        chatbot_view_refresh(s_app);

        uint16_t total = 0;
        for (uint16_t i = 0; i < c->model->msg_count; i++) {
            if (c->model->messages[i].id == msg_id) { total = i; break; }
        }
        uint16_t take = total > CHATBOT_CTX_MESSAGES ? CHATBOT_CTX_MESSAGES : total;
        const ChatMessage *ctx = &c->model->messages[total - take];

        chatbot_svc_complete(c->model->active_session_id, msg_id, ctx, take,
                             on_llm_done, NULL);
        return;
    }

    /* 用户消息失败：mock 阶段没有留存 PCM，只能提示重新说一次 */
    if (m->text[0] != '\0') {
        m->state    = MSG_STATE_OK;
        m->err_code = CHAT_ERR_NONE;
        chatbot_view_refresh(s_app);
        start_llm(msg_id);
    } else {
        lv_toast_show("Please say that again", 2000);
    }
}

/* ── 会话 ─────────────────────────────────────────────────────────── */

void chatbot_controller_new_session(void)
{
    ChatbotController *c = ctrl();
    if (!c) return;
    if (chatbot_controller_is_busy()) {
        lv_toast_show("Working - please wait", 1500);
        return;
    }
    chatbot_model_new_session(c->model);
    check_store();
    chatbot_view_refresh(s_app);
}

void chatbot_controller_open_session(uint32_t session_id)
{
    ChatbotController *c = ctrl();
    if (!c) return;
    if (chatbot_controller_is_busy()) {
        lv_toast_show("Working - please wait", 1500);
        return;
    }
    chatbot_model_load_session(c->model, session_id);
    chatbot_view_refresh(s_app);
}

void chatbot_controller_delete_session(uint32_t session_id)
{
    ChatbotController *c = ctrl();
    if (!c) return;
    if (chatbot_controller_is_busy()) {
        lv_toast_show("Working - please wait", 1500);
        return;
    }
    chatbot_model_delete_session(c->model, session_id);
    check_store();
    chatbot_view_refresh(s_app);
}

/* ── 查询 ─────────────────────────────────────────────────────────── */

ChatState chatbot_controller_state(void)
{
    ChatbotController *c = ctrl();
    return c ? c->state : CHAT_STATE_IDLE;
}

bool chatbot_controller_is_busy(void)
{
    return chatbot_controller_state() != CHAT_STATE_IDLE;
}

/* ── 生命周期 ─────────────────────────────────────────────────────── */

void chatbot_controller_init(struct ChatbotApp *app)
{
    ESP_LOGI(TAG, "chatbot_controller_init");
    app->controller = malloc(sizeof(ChatbotController));
    if (!app->controller) {
        ESP_LOGE(TAG, "Failed to allocate memory for ChatbotController");
        return;
    }
    memset(app->controller, 0, sizeof(ChatbotController));
    app->controller->state = CHAT_STATE_IDLE;

    s_app = app;

    chatbot_audio_init();
    chatbot_svc_init();
}

void chatbot_controller_deinit(struct ChatbotApp *app)
{
    ESP_LOGI(TAG, "chatbot_controller_deinit");

    stop_rec_timer();
    chatbot_svc_deinit();
    chatbot_audio_deinit();

    if (app->controller) {
        free(app->controller);
        app->controller = NULL;
    }
    s_app = NULL;
}
