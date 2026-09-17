#include <stdio.h>
#include <string.h>
#include "esp_log.h"

#include "storage.h"
#include "flash_control.h"   // 提供 g_kvdb

static const char *TAG = "chatbot_storage";

extern struct fdb_kvdb g_kvdb;

#define KV_SESS_IDX   "cb_sess_idx"

static bool s_last_write_failed = false;

/* 索引 blob：会话数组 + 游离状态一起存，省一个 key */
typedef struct {
    ChatSession sessions[CHATBOT_MAX_SESSIONS];
    uint8_t     session_count;
    uint32_t    active_session_id;
    uint32_t    id_generator;
} chatbot_index_blob_t;

/* 消息 blob：定长数组 + 有效条数 */
typedef struct {
    ChatMessage messages[MAX_CHAT_HISTORY];
    uint16_t    msg_count;
} chatbot_msgs_blob_t;

static void msg_key(char *out, size_t out_len, uint32_t session_id)
{
    snprintf(out, out_len, "cb_m%lu", (unsigned long)session_id);
}

bool chatbot_storage_last_write_failed(void)
{
    bool v = s_last_write_failed;
    s_last_write_failed = false;   /* 读取即清除，UI 只提示一次 */
    return v;
}

/* ── 索引 ─────────────────────────────────────────────────────────── */

bool chatbot_storage_save_index(ChatbotModel *model)
{
    if (!model) return false;

    chatbot_index_blob_t idx;
    memset(&idx, 0, sizeof(idx));
    memcpy(idx.sessions, model->sessions, sizeof(idx.sessions));
    idx.session_count     = model->session_count;
    idx.active_session_id = model->active_session_id;
    idx.id_generator      = model->id_generator;

    struct fdb_blob blob;
    fdb_err_t err = fdb_kv_set_blob(&g_kvdb, KV_SESS_IDX,
                                    fdb_blob_make(&blob, &idx, sizeof(idx)));
    if (err != FDB_NO_ERR) {
        ESP_LOGE(TAG, "save index failed: %d", (int)err);
        s_last_write_failed = true;
        return false;
    }
    return true;
}

/* ── 消息 ─────────────────────────────────────────────────────────── */

bool chatbot_storage_save_messages(ChatbotModel *model)
{
    if (!model || model->active_session_id == 0) return false;

    chatbot_msgs_blob_t b;
    memset(&b, 0, sizeof(b));
    memcpy(b.messages, model->messages, sizeof(b.messages));
    b.msg_count = model->msg_count;

    char key[24];
    msg_key(key, sizeof(key), model->active_session_id);

    struct fdb_blob blob;
    fdb_err_t err = fdb_kv_set_blob(&g_kvdb, key,
                                    fdb_blob_make(&blob, &b, sizeof(b)));
    if (err != FDB_NO_ERR) {
        ESP_LOGE(TAG, "save messages(%s) failed: %d", key, (int)err);
        s_last_write_failed = true;
        return false;
    }
    return true;
}

bool chatbot_storage_load_messages(ChatbotModel *model, uint32_t session_id)
{
    if (!model || session_id == 0) return false;

    char key[24];
    msg_key(key, sizeof(key), session_id);

    chatbot_msgs_blob_t b;
    memset(&b, 0, sizeof(b));

    struct fdb_blob blob;
    size_t read_len = fdb_kv_get_blob(&g_kvdb, key,
                                      fdb_blob_make(&blob, &b, sizeof(b)));
    if (read_len != sizeof(b)) {
        /* 新会话或旧格式：当作空会话，不是错误 */
        model->msg_count = 0;
        memset(model->messages, 0, sizeof(model->messages));
        return false;
    }

    if (b.msg_count > MAX_CHAT_HISTORY) b.msg_count = MAX_CHAT_HISTORY;
    memcpy(model->messages, b.messages, sizeof(model->messages));
    model->msg_count = b.msg_count;

    /* 上次断电时可能有请求停在半路，恢复后不该再显示"识别中…" */
    for (uint16_t i = 0; i < model->msg_count; i++) {
        if (model->messages[i].state == MSG_STATE_PENDING) {
            model->messages[i].state    = MSG_STATE_FAILED;
            model->messages[i].err_code = CHAT_ERR_NET_FAIL;
        }
    }
    return true;
}

bool chatbot_storage_delete_messages(uint32_t session_id)
{
    if (session_id == 0) return false;
    char key[24];
    msg_key(key, sizeof(key), session_id);
    fdb_kv_del(&g_kvdb, key);
    return true;
}

/* ── 开机恢复 ─────────────────────────────────────────────────────── */

bool chatbot_storage_load(ChatbotModel *model)
{
    if (!model) return false;

    chatbot_index_blob_t idx;
    memset(&idx, 0, sizeof(idx));

    struct fdb_blob blob;
    size_t read_len = fdb_kv_get_blob(&g_kvdb, KV_SESS_IDX,
                                      fdb_blob_make(&blob, &idx, sizeof(idx)));
    if (read_len != sizeof(idx)) {
        ESP_LOGI(TAG, "no saved index (first run)");
        return false;
    }

    if (idx.session_count > CHATBOT_MAX_SESSIONS) idx.session_count = CHATBOT_MAX_SESSIONS;
    memcpy(model->sessions, idx.sessions, sizeof(model->sessions));
    model->session_count     = idx.session_count;
    model->active_session_id = idx.active_session_id;
    model->id_generator      = idx.id_generator ? idx.id_generator : 1;

    if (model->session_count > 0 && model->active_session_id != 0) {
        chatbot_storage_load_messages(model, model->active_session_id);
    }
    return true;
}
