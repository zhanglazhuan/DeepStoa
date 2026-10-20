#ifndef CHAT_VIEW_H
#define CHAT_VIEW_H

#include <lvgl.h>
#include "page_navigator.h"
#include "lv_bottom_sheet.h"
#include "lv_page.h"

#include "model.h"
#include "controller.h"

struct ChatApp;

typedef enum {
    PAGE_CHAT_NONE = 0,
    PAGE_CHAT_MAIN,
    PAGE_CHAT_HISTORY,
    PAGE_CHAT_ID_MAX
} ChatPageID;

#define CHAT_PAGE_ID_MAX 6

typedef struct ChatView {
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
} ChatView;

void chat_view_init(struct ChatApp *app);
void chat_view_deinit(struct ChatApp *app);

/** 按 model 重建气泡列表。EPD 层做逐像素 diff，重建不会造成多余刷新。 */
void chat_view_refresh(struct ChatApp *app);

/** 按状态机更新底部按钮文案与可用性 */
void chat_view_set_state(struct ChatApp *app, ChatState state);

/** L3 横幅；text 为 NULL 或 show=false 时隐藏 */
void chat_view_set_banner(struct ChatApp *app, const char *text, bool show);

/** 录音中：更新秒数与音量格（1Hz 级别，只刷这一小块） */
void chat_view_update_recording(struct ChatApp *app, uint32_t ms, uint8_t level);

#endif // CHAT_VIEW_H
