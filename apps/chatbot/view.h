#ifndef CHATBOT_VIEW_H
#define CHATBOT_VIEW_H

#include <lvgl.h>
#include "page_navigator.h"
#include "lv_bottom_sheet.h"
#include "lv_page.h"

#include "model.h"
#include "controller.h"

struct ChatbotApp;

typedef enum {
    PAGE_CHATBOT_NONE = 0,
    PAGE_CHATBOT_MAIN,
    PAGE_CHATBOT_HISTORY,
    PAGE_CHATBOT_ID_MAX
} ChatbotPageID;

#define CHATBOT_PAGE_ID_MAX 6

typedef struct ChatbotView {
    page_navigator_t page_nav;

    lv_obj_t *chat_container;
    lv_obj_t *btn_record;
    lv_obj_t *lbl_record;

    /* L3 页面级横幅（麦克风不可用 / 未联网） */
    lv_obj_t *banner;
    lv_obj_t *banner_label;

    /* 录音状态行：秒数 + 离散音量格 */
    lv_obj_t *rec_status;
    lv_obj_t *rec_time_label;
    lv_obj_t *rec_level[5];

    /* 文字输入兜底 */
    lv_obj_t *text_input;
    lv_obj_t *keyboard;
    lv_obj_t *btn_mode;      /* 语音 / 键盘 切换 */
    bool      text_mode;
} ChatbotView;

void chatbot_view_init(struct ChatbotApp *app);
void chatbot_view_deinit(struct ChatbotApp *app);

/** 按 model 重建气泡列表。EPD 层做逐像素 diff，重建不会造成多余刷新。 */
void chatbot_view_refresh(struct ChatbotApp *app);

/** 按状态机更新底部按钮文案与可用性 */
void chatbot_view_set_state(struct ChatbotApp *app, ChatState state);

/** L3 横幅；text 为 NULL 或 show=false 时隐藏 */
void chatbot_view_set_banner(struct ChatbotApp *app, const char *text, bool show);

/** 录音中：更新秒数与音量格（1Hz 级别，只刷这一小块） */
void chatbot_view_update_recording(struct ChatbotApp *app, uint32_t ms, uint8_t level);

#endif // CHATBOT_VIEW_H
