#ifndef CHATBOT_CONTROLLER_H
#define CHATBOT_CONTROLLER_H

#include <lvgl.h>
#include <stdint.h>
#include "model.h"

struct ChatbotApp;
struct ChatbotView;

/* 全应用唯一的状态机 */
typedef enum {
    CHAT_STATE_IDLE = 0,
    CHAT_STATE_RECORDING,
    CHAT_STATE_TRANSCRIBING,
    CHAT_STATE_THINKING,
} ChatState;

typedef struct ChatbotController {
    struct ChatbotModel *model;
    struct ChatbotView  *view;

    ChatState   state;
    uint32_t    pending_user_msg;   /* 正在识别的用户消息 */
    uint32_t    pending_ai_msg;     /* 正在生成的 AI 消息 */
    lv_timer_t *rec_timer;          /* 1Hz 录音计时 + MAX 超时 */
} ChatbotController;

void chatbot_controller_init(struct ChatbotApp *app);
void chatbot_controller_deinit(struct ChatbotApp *app);

/* ── 录音触发（唯一入口）────────────────────────────────────────────
 * 现在由屏幕按钮的 PRESSED / RELEASED / PRESS_LOST 调用。
 * 阶段 3 物理按键接入后，按键回调调用同样这三个函数，本文件不用改。 */
void chatbot_controller_on_talk_press(void);
void chatbot_controller_on_talk_release(void);
void chatbot_controller_on_talk_cancel(void);

/* ── 文字输入兜底 ──────────────────────────────────────────────────
 * 麦克风到货前，这是唯一能端到端跑通完整对话链路的路径；
 * 同时也是麦克风故障 / 嘈杂环境下的正式降级方案。 */
void chatbot_controller_send_text(const char *text);

/* ── 会话 ─────────────────────────────────────────────────────────── */
void chatbot_controller_new_session(void);
void chatbot_controller_open_session(uint32_t session_id);
void chatbot_controller_delete_session(uint32_t session_id);

/* ── 消息操作 ─────────────────────────────────────────────────────── */
void chatbot_controller_retry(uint32_t msg_id);
void chatbot_controller_abort(void);          /* 取消进行中的请求 */

ChatState chatbot_controller_state(void);
bool      chatbot_controller_is_busy(void);   /* 非 IDLE：UI 需禁用切页/菜单 */

/* LVGL 事件适配层（view 绑定用） */
void chatbot_controller_record_event_cb(lv_event_t *e);

#endif // CHATBOT_CONTROLLER_H
