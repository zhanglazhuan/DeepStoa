// chat_model.h
#ifndef CHAT_MODEL_H
#define CHAT_MODEL_H

#include <stdint.h>
#include <stdbool.h>

struct ChatApp;

/* ── 容量与配额 ──────────────────────────────────────────────────────
 * fdb_kvdb 分区只有 128 KB，且 todolist/anki/clock/reader/settings 共用。
 * 一条消息 = MAX_MSG_LENGTH + ~24B 元数据 ≈ 216 B
 * 一个会话 20 条 ≈ 4.2 KB；8 个会话 ≈ 34 KB。
 * 只有"当前会话"常驻 RAM，历史会话按 key 懒加载。 */
#define MAX_CHAT_HISTORY        20
#define MAX_MSG_LENGTH          192
#define CHAT_MAX_SESSIONS     8
#define CHAT_TITLE_MAX       32

/* 发给大模型的上下文窗口：最近 6 轮 */
#define CHAT_CTX_MESSAGES    12

typedef enum {
    MSG_ROLE_AI = 0,
    MSG_ROLE_USER
} MsgRole;

typedef enum {
    MSG_STATE_OK = 0,     /* 正常显示 */
    MSG_STATE_PENDING,    /* 用户消息：识别中；AI 消息：思考中 */
    MSG_STATE_FAILED,     /* 失败，气泡显示原因 + 重试 */
    MSG_STATE_CANCELLED,  /* 用户主动取消，不提供重试 */
} MsgState;

/* 错误码 —— 决定失败气泡显示什么文案，见 chat_err_text() */
typedef enum {
    CHAT_ERR_NONE = 0,
    CHAT_ERR_MIC_INIT,
    CHAT_ERR_REC_TOO_SHORT,
    CHAT_ERR_REC_TOO_LONG,
    CHAT_ERR_REC_SILENT,
    CHAT_ERR_REC_OVERFLOW,
    CHAT_ERR_NET_OFFLINE,
    CHAT_ERR_NET_TIMEOUT,
    CHAT_ERR_NET_FAIL,
    CHAT_ERR_ASR_EMPTY,
    CHAT_ERR_ASR_FAIL,
    CHAT_ERR_LLM_RATE,
    CHAT_ERR_LLM_FAIL,
    CHAT_ERR_STORE_FULL,
} ChatErrCode;

typedef struct ChatMessage {
    uint32_t id;
    uint8_t  role;        /* MsgRole */
    uint8_t  state;       /* MsgState */
    uint8_t  err_code;    /* ChatErrCode，state==FAILED 时有效 */
    uint8_t  truncated;   /* 回复超长被截断 */
    uint32_t timestamp;   /* unix 秒 */
    uint16_t audio_ms;    /* 语音时长；0 表示键盘输入 */
    char     text[MAX_MSG_LENGTH];
} ChatMessage;

typedef struct ChatSession {
    uint32_t id;
    char     title[CHAT_TITLE_MAX];
    uint32_t created_at;
    uint32_t updated_at;
    uint16_t msg_count;
} ChatSession;

typedef struct ChatModel {
    /* 会话索引：常驻，持久化到 KV "cb_sess_idx" */
    ChatSession sessions[CHAT_MAX_SESSIONS];
    uint8_t     session_count;

    /* 当前会话的消息：常驻，持久化到 KV "cb_msg_<id>" */
    uint32_t    active_session_id;
    ChatMessage messages[MAX_CHAT_HISTORY];
    uint16_t    msg_count;

    uint32_t    id_generator;
} ChatModel;

void chat_model_init(struct ChatApp* app);
void chat_model_deinit(struct ChatApp* app);

/* ── 消息 ─────────────────────────────────────────────────────────── */

/** 追加一条消息，返回其 id（0 表示失败）。满了会滚动淘汰最旧的一条。 */
uint32_t chat_model_add_message(ChatModel *model, MsgRole role,
                                   MsgState state, const char *text,
                                   uint16_t audio_ms);

ChatMessage *chat_model_find_message(ChatModel *model, uint32_t msg_id);

/** 原地更新一条消息的文本和状态（PENDING 气泡就地替换成结果） */
bool chat_model_resolve_message(ChatModel *model, uint32_t msg_id,
                                   MsgState state, const char *text,
                                   ChatErrCode err);

/* ── 会话 ─────────────────────────────────────────────────────────── */

/** 新建会话并切为当前会话。会话满时淘汰最旧的一个。 */
uint32_t chat_model_new_session(ChatModel *model);
bool     chat_model_load_session(ChatModel *model, uint32_t session_id);
bool     chat_model_delete_session(ChatModel *model, uint32_t session_id);
ChatSession *chat_model_find_session(ChatModel *model, uint32_t session_id);

/** 用首条用户消息生成标题（"新对话" → "明天天气怎么样"） */
void chat_model_touch_session(ChatModel *model);

/** 错误码 → 给用户看的中文文案 */
const char *chat_err_text(ChatErrCode err);

#endif // CHAT_MODEL_H
