/**
 * @file chatbot_svc.h
 * @brief 云端服务抽象层 —— 语音转写 + 大模型回复
 *
 * 实现有两份，共用本头文件，由 CMakeLists 二选一编译：
 *   chatbot_svc_mock.c   当前阶段
 *   chatbot_svc_http.c   阶段 1，接真实 API（esp_http_client + cJSON 已在构建里）
 *
 * ── 实现方必须遵守的三条契约 ──────────────────────────────────────────
 *
 * 1. 回调必须在 LVGL 线程被调用（真实实现跑在 HTTP 任务里，用 lv_async_call 弹回）。
 * 2. 每个请求必定回调一次。超时由本层内部计时并主动回 ERR_TIMEOUT，
 *    controller 不再自己起 timer。唯一的例外是被 chatbot_svc_abort() 取消的请求。
 * 3. 回调里的 text 指针只在回调期间有效，调用方当场拷走。
 */

#ifndef CHATBOT_SVC_H
#define CHATBOT_SVC_H

#include <stdbool.h>
#include <stdint.h>
#include "chatbot_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 超时（本层内部实现，不要求调用方计时） */
#define CHATBOT_SVC_ASR_TIMEOUT_MS   15000u
#define CHATBOT_SVC_LLM_TIMEOUT_MS   30000u

typedef enum {
    CHATBOT_SVC_OK = 0,
    CHATBOT_SVC_ERR_OFFLINE,     /* wifi 未连接，请求根本没发出去 */
    CHATBOT_SVC_ERR_TIMEOUT,
    CHATBOT_SVC_ERR_NETWORK,     /* DNS / TLS / 连接失败 */
    CHATBOT_SVC_ERR_ASR_EMPTY,   /* 转写结果为空串 */
    CHATBOT_SVC_ERR_SERVER,      /* HTTP 4xx / 5xx */
    CHATBOT_SVC_ERR_RATE_LIMIT,  /* HTTP 429 */
    CHATBOT_SVC_ERR_BAD_REPLY,   /* 响应 JSON 解析失败 */
} chatbot_svc_err_t;

/* 前置声明，避免 service 层反向依赖 model.h 的全部内容 */
struct ChatMessage;

typedef void (*chatbot_svc_text_cb_t)(uint32_t msg_id, const char *text,
                                      chatbot_svc_err_t err, void *user_data);

void chatbot_svc_init(void);
void chatbot_svc_deinit(void);

/** 语音转文本。clip 由本层在调用期间读取完毕，不持有。 */
void chatbot_svc_transcribe(uint32_t msg_id,
                            const chatbot_audio_clip_t *clip,
                            chatbot_svc_text_cb_t cb, void *user_data);

/**
 * 文本 → 大模型回复。
 * @param ctx        要带上的上下文消息（由 controller 按上下文窗口裁剪好）
 * @param ctx_count  ctx 的条数
 */
void chatbot_svc_complete(uint32_t session_id, uint32_t msg_id,
                          const struct ChatMessage *ctx, uint16_t ctx_count,
                          chatbot_svc_text_cb_t cb, void *user_data);

/** 取消。已发出的请求丢弃结果，不再回调。 */
void chatbot_svc_abort(uint32_t msg_id);

/* ── 仅 mock 实现提供：故障注入 ────────────────────────────────────── */
void chatbot_svc_mock_inject(chatbot_svc_err_t err);
void chatbot_svc_mock_set_reply_len(uint16_t len);  /* 0 = 默认长度 */

#ifdef __cplusplus
}
#endif

#endif /* CHATBOT_SVC_H */
