#include "esp_system.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "app.h"
#include "model.h"
#include "storage.h"

static const char *TAG = "chatbot_model";

static uint32_t now_ts(void) { return (uint32_t)time(NULL); }

const char *chatbot_err_text(ChatErrCode err)
{
    switch (err) {
    case CHAT_ERR_MIC_INIT:      return "Microphone unavailable - use the keyboard";
    case CHAT_ERR_REC_TOO_SHORT: return "Released too soon - hold to talk";
    case CHAT_ERR_REC_TOO_LONG:  return "Maximum recording length reached";
    case CHAT_ERR_REC_SILENT:    return "No sound detected - move closer to the mic";
    case CHAT_ERR_REC_OVERFLOW:  return "Recording failed - try something shorter";
    case CHAT_ERR_NET_OFFLINE:   return "No network connection";
    case CHAT_ERR_NET_TIMEOUT:   return "Network timed out";
    case CHAT_ERR_NET_FAIL:      return "Cannot reach the server";
    case CHAT_ERR_ASR_EMPTY:     return "Could not make that out";
    case CHAT_ERR_ASR_FAIL:      return "Speech recognition failed";
    case CHAT_ERR_LLM_RATE:      return "Too many requests - try again shortly";
    case CHAT_ERR_LLM_FAIL:      return "The model did not respond";
    case CHAT_ERR_STORE_FULL:    return "Storage full - oldest chat removed";
    default:                     return "Something went wrong";
    }
}

void chatbot_model_init(ChatbotApp* app) {
    app->model = malloc(sizeof(ChatbotModel));
    if (!app->model) {
        ESP_LOGE(TAG, "Failed to allocate memory for ChatbotModel");
        return;
    }
    memset(app->model, 0, sizeof(ChatbotModel));
    app->model->id_generator = 1;

    /* 从 flash 恢复会话索引 + 上次的当前会话 */
    chatbot_storage_load(app->model);

    if (app->model->session_count == 0) {
        chatbot_model_new_session(app->model);
    }

    ESP_LOGI(TAG, "model init: %u sessions, active=%lu, %u msgs",
             app->model->session_count,
             (unsigned long)app->model->active_session_id,
             app->model->msg_count);
}

void chatbot_model_deinit(ChatbotApp* app) {
    if (app->model) {
        chatbot_storage_save_messages(app->model);
        chatbot_storage_save_index(app->model);
        free(app->model);
        app->model = NULL;
    }
    ESP_LOGI(TAG, "ChatbotModel deinitialized");
}

/* ── 消息 ─────────────────────────────────────────────────────────── */

uint32_t chatbot_model_add_message(ChatbotModel *model, MsgRole role,
                                   MsgState state, const char *text,
                                   uint16_t audio_ms)
{
    if (!model) return 0;

    if (model->msg_count >= MAX_CHAT_HISTORY) {
        /* 滚动淘汰最旧的一条 */
        memmove(&model->messages[0], &model->messages[1],
                sizeof(ChatMessage) * (MAX_CHAT_HISTORY - 1));
        model->msg_count = MAX_CHAT_HISTORY - 1;
    }

    ChatMessage *m = &model->messages[model->msg_count];
    memset(m, 0, sizeof(*m));
    m->id        = model->id_generator++;
    m->role      = (uint8_t)role;
    m->state     = (uint8_t)state;
    m->timestamp = now_ts();
    m->audio_ms  = audio_ms;
    if (text) {
        strncpy(m->text, text, MAX_MSG_LENGTH - 1);
        m->text[MAX_MSG_LENGTH - 1] = '\0';
    }
    model->msg_count++;

    chatbot_model_touch_session(model);
    return m->id;
}

ChatMessage *chatbot_model_find_message(ChatbotModel *model, uint32_t msg_id)
{
    if (!model) return NULL;
    for (uint16_t i = 0; i < model->msg_count; i++) {
        if (model->messages[i].id == msg_id) return &model->messages[i];
    }
    return NULL;   /* 可能已被滚动淘汰 */
}

bool chatbot_model_resolve_message(ChatbotModel *model, uint32_t msg_id,
                                   MsgState state, const char *text,
                                   ChatErrCode err)
{
    ChatMessage *m = chatbot_model_find_message(model, msg_id);
    if (!m) return false;

    m->state    = (uint8_t)state;
    m->err_code = (uint8_t)err;

    if (text) {
        size_t len = strlen(text);
        m->truncated = (len >= MAX_MSG_LENGTH) ? 1 : 0;
        strncpy(m->text, text, MAX_MSG_LENGTH - 1);
        m->text[MAX_MSG_LENGTH - 1] = '\0';
    }

    chatbot_model_touch_session(model);
    return true;
}

/* ── 会话 ─────────────────────────────────────────────────────────── */

ChatSession *chatbot_model_find_session(ChatbotModel *model, uint32_t session_id)
{
    if (!model) return NULL;
    for (uint8_t i = 0; i < model->session_count; i++) {
        if (model->sessions[i].id == session_id) return &model->sessions[i];
    }
    return NULL;
}

void chatbot_model_touch_session(ChatbotModel *model)
{
    ChatSession *s = chatbot_model_find_session(model, model->active_session_id);
    if (!s) return;

    s->updated_at = now_ts();
    s->msg_count  = model->msg_count;

    /* 标题还是默认值时，用首条 state==OK 的用户消息生成 */
    if (s->title[0] != '\0' && strcmp(s->title, "New Chat") != 0) return;

    for (uint16_t i = 0; i < model->msg_count; i++) {
        ChatMessage *m = &model->messages[i];
        if (m->role != MSG_ROLE_USER || m->state != MSG_STATE_OK) continue;
        if (m->text[0] == '\0') continue;
        strncpy(s->title, m->text, CHATBOT_TITLE_MAX - 1);
        s->title[CHATBOT_TITLE_MAX - 1] = '\0';
        return;
    }
}

uint32_t chatbot_model_new_session(ChatbotModel *model)
{
    if (!model) return 0;

    /* 当前会话先落盘，避免切走后丢失 */
    if (model->active_session_id != 0) {
        chatbot_storage_save_messages(model);
    }

    if (model->session_count >= CHATBOT_MAX_SESSIONS) {
        /* 淘汰 updated_at 最早的一个 */
        uint8_t oldest = 0;
        for (uint8_t i = 1; i < model->session_count; i++) {
            if (model->sessions[i].updated_at < model->sessions[oldest].updated_at) {
                oldest = i;
            }
        }
        uint32_t victim = model->sessions[oldest].id;
        ESP_LOGW(TAG, "session full, evicting %lu", (unsigned long)victim);
        chatbot_storage_delete_messages(victim);
        memmove(&model->sessions[oldest], &model->sessions[oldest + 1],
                sizeof(ChatSession) * (model->session_count - oldest - 1));
        model->session_count--;
    }

    ChatSession *s = &model->sessions[model->session_count];
    memset(s, 0, sizeof(*s));
    s->id         = model->id_generator++;
    s->created_at = now_ts();
    s->updated_at = s->created_at;
    strncpy(s->title, "New Chat", CHATBOT_TITLE_MAX - 1);
    model->session_count++;

    model->active_session_id = s->id;
    model->msg_count = 0;
    memset(model->messages, 0, sizeof(model->messages));

    chatbot_storage_save_index(model);
    ESP_LOGI(TAG, "new session %lu", (unsigned long)s->id);
    return s->id;
}

bool chatbot_model_load_session(ChatbotModel *model, uint32_t session_id)
{
    if (!model) return false;
    if (!chatbot_model_find_session(model, session_id)) return false;
    if (model->active_session_id == session_id) return true;

    chatbot_storage_save_messages(model);   /* 旧会话落盘 */

    model->active_session_id = session_id;
    model->msg_count = 0;
    memset(model->messages, 0, sizeof(model->messages));
    chatbot_storage_load_messages(model, session_id);

    chatbot_storage_save_index(model);
    ESP_LOGI(TAG, "load session %lu (%u msgs)",
             (unsigned long)session_id, model->msg_count);
    return true;
}

bool chatbot_model_delete_session(ChatbotModel *model, uint32_t session_id)
{
    if (!model) return false;

    int idx = -1;
    for (uint8_t i = 0; i < model->session_count; i++) {
        if (model->sessions[i].id == session_id) { idx = i; break; }
    }
    if (idx < 0) return false;

    chatbot_storage_delete_messages(session_id);
    memmove(&model->sessions[idx], &model->sessions[idx + 1],
            sizeof(ChatSession) * (model->session_count - idx - 1));
    model->session_count--;

    if (model->active_session_id == session_id) {
        /* 删的是当前会话：切到最近一个，没有就新建 */
        model->msg_count = 0;
        memset(model->messages, 0, sizeof(model->messages));
        model->active_session_id = 0;

        if (model->session_count > 0) {
            uint8_t newest = 0;
            for (uint8_t i = 1; i < model->session_count; i++) {
                if (model->sessions[i].updated_at > model->sessions[newest].updated_at) {
                    newest = i;
                }
            }
            model->active_session_id = model->sessions[newest].id;
            chatbot_storage_load_messages(model, model->active_session_id);
        } else {
            chatbot_model_new_session(model);
            return true;   /* new_session 内部已存过索引 */
        }
    }

    chatbot_storage_save_index(model);
    ESP_LOGI(TAG, "deleted session %lu", (unsigned long)session_id);
    return true;
}
