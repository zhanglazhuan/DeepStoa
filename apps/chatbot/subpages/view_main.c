#include <stdio.h>
#include <string.h>
#include <time.h>
#include <lvgl.h>
#include "lv_ui_style_guide.h"
#include "ui_utils.h"
#include "lv_theme_hardcore.h"
#include "lv_toast.h"
#include "widgets/lv_keyboard.h"
#include "esp_log.h"

#include "../app.h"
#include "../view.h"
#include "../controller.h"
#include "../service/chatbot_audio.h"
#include "../service/chatbot_svc.h"
#include "view_main.h"

EPOS_LV_IMG_DECLARE(mic_on);

static const char *TAG = "chatbot_view_main";

/* 墨水屏：1bit 屏上大圆角有明显锯齿，统一用 2 */
#define BUBBLE_RADIUS 2

/* ── 菜单 ─────────────────────────────────────────────────────────── */

static void close_sheet_wrapper_cb(lv_event_t * e) {
    lv_bottom_sheet_close(lv_event_get_user_data(e));
}

static void new_chatbot_wrapper_cb(lv_event_t * e) {
    (void)e;
    ESP_LOGI(TAG, "Menu: New Chatbot");
    chatbot_controller_new_session();
}

static void open_history_wrapper_cb(lv_event_t * e) {
    ChatbotApp *app = lv_event_get_user_data(e);
    if (chatbot_controller_is_busy()) {
        lv_toast_show("Working - please wait", 1500);
        return;
    }
    PAGE_NAVIGATE_TO(app, PAGE_CHATBOT_HISTORY, NULL);
}

/* ── Mock 调试面板：连点标题 5 次进入 ───────────────────────────────
 * 用来逐条验证错误码表里的每个 UI 分支。接真实后端后整段删掉即可。 */

typedef struct { const char *name; int kind; int code; } mock_fault_t;
#define FAULT_AUDIO 0
#define FAULT_SVC   1

static const mock_fault_t k_faults[] = {
    {"Mic unavailable",   FAULT_AUDIO, CHATBOT_AUDIO_ERR_INIT},
    {"Silent recording",       FAULT_AUDIO, CHATBOT_AUDIO_ERR_SILENT},
    {"Recording overflow",       FAULT_AUDIO, CHATBOT_AUDIO_ERR_OVERFLOW},
    {"Offline",         FAULT_SVC,   CHATBOT_SVC_ERR_OFFLINE},
    {"Request timeout",       FAULT_SVC,   CHATBOT_SVC_ERR_TIMEOUT},
    {"Server unreachable",   FAULT_SVC,   CHATBOT_SVC_ERR_NETWORK},
    {"Empty transcript",       FAULT_SVC,   CHATBOT_SVC_ERR_ASR_EMPTY},
    {"Server error",     FAULT_SVC,   CHATBOT_SVC_ERR_SERVER},
    {"Rate limited (429)",       FAULT_SVC,   CHATBOT_SVC_ERR_RATE_LIMIT},
};
#define FAULT_COUNT ((int)(sizeof(k_faults) / sizeof(k_faults[0])))

static void fault_pick_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const mock_fault_t *f = &k_faults[idx];

    if (f->kind == FAULT_AUDIO) {
        if (f->code == CHATBOT_AUDIO_ERR_INIT) {
            chatbot_audio_mock_set_available(false);
        } else {
            chatbot_audio_mock_inject((chatbot_audio_err_t)f->code);
        }
    } else {
        chatbot_svc_mock_inject((chatbot_svc_err_t)f->code);
    }
    lv_toast_show("Next request will hit this error", 1800);
}

static void fault_reset_cb(lv_event_t *e)
{
    (void)e;
    chatbot_audio_mock_set_available(true);
    chatbot_audio_mock_inject(CHATBOT_AUDIO_OK);
    chatbot_svc_mock_inject(CHATBOT_SVC_OK);
    lv_toast_show("Back to normal", 1500);
}

static void show_mock_panel(void)
{
    lv_bottom_sheet_t *sheet = lv_bottom_sheet_create(NULL);
    lv_obj_t *content = lv_bottom_sheet_get_content(sheet);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

    for (int i = 0; i < FAULT_COUNT + 1; i++) {
        lv_obj_t *btn = lv_btn_create(content);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, 52);
        lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
        lv_obj_set_style_text_color(btn, lv_color_black(), 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(btn, lv_color_black(), 0);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, i < FAULT_COUNT ? k_faults[i].name : "Reset to normal");
        lv_obj_center(lbl);

        if (i < FAULT_COUNT) {
            lv_obj_add_event_cb(btn, fault_pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        } else {
            lv_obj_add_event_cb(btn, fault_reset_cb, LV_EVENT_CLICKED, NULL);
        }
        lv_obj_add_event_cb(btn, close_sheet_wrapper_cb, LV_EVENT_CLICKED, sheet);
    }
}

static void title_tap_cb(lv_event_t *e)
{
    (void)e;
    static uint8_t  taps;
    static uint32_t last_ms;
    uint32_t now = lv_tick_get();

    if (now - last_ms > 1500) taps = 0;
    last_ms = now;

    if (++taps >= 5) { taps = 0; show_mock_panel(); }
}

static void menu_sheet_event_cb(lv_event_t * e) {
    ChatbotApp *app = lv_event_get_user_data(e);

    lv_bottom_sheet_t *sheet = lv_bottom_sheet_create(NULL);
    lv_obj_t * content = lv_bottom_sheet_get_content(sheet);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

    const char* options[] = {"New Chatbot", "Chatbot History", "Cancel"};
    void (*callbacks[])(lv_event_t *) = {
        new_chatbot_wrapper_cb,
        open_history_wrapper_cb,
        NULL
    };
    int option_count = sizeof(options) / sizeof(options[0]);

    for(int i = 0; i < option_count; i++) {
        lv_obj_t *btn = lv_btn_create(content);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, 60);
        lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
        lv_obj_set_style_text_color(btn, lv_color_black(), 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);

        if (i < option_count - 1) {
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
            lv_obj_set_style_border_color(btn, lv_color_black(), 0);
        }

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, options[i]);
        lv_obj_center(lbl);

        if (callbacks[i] != NULL) {
            lv_obj_add_event_cb(btn, callbacks[i], LV_EVENT_CLICKED, app);
        }
        lv_obj_add_event_cb(btn, close_sheet_wrapper_cb, LV_EVENT_CLICKED, sheet);
    }
}

/* ── 气泡 ─────────────────────────────────────────────────────────── */

static void retry_btn_cb(lv_event_t *e)
{
    chatbot_controller_retry((uint32_t)(uintptr_t)lv_event_get_user_data(e));
}

static void fmt_time(char *out, size_t len, uint32_t ts)
{
    if (ts == 0) { out[0] = '\0'; return; }
    time_t t = (time_t)ts;
    struct tm tm;
    localtime_r(&t, &tm);
    snprintf(out, len, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

/* 气泡正文：按状态决定显示什么 */
static void bubble_body_text(const ChatMessage *m, char *out, size_t len)
{
    switch (m->state) {
    case MSG_STATE_PENDING:
        if (m->role == MSG_ROLE_USER) {
            snprintf(out, len, LV_SYMBOL_AUDIO " %u.%u\"  transcribing...",
                     m->audio_ms / 1000, (m->audio_ms % 1000) / 100);
        } else {
            snprintf(out, len, "Thinking...");
        }
        break;
    case MSG_STATE_CANCELLED:
        snprintf(out, len, "Cancelled");
        break;
    case MSG_STATE_FAILED:
        if (m->text[0] != '\0') snprintf(out, len, "%s", m->text);
        else if (m->audio_ms)   snprintf(out, len, LV_SYMBOL_AUDIO " %u.%u\"",
                                         m->audio_ms / 1000, (m->audio_ms % 1000) / 100);
        else                    snprintf(out, len, " ");
        break;
    default:
        snprintf(out, len, "%s%s", m->text, m->truncated ? "... (truncated)" : "");
        break;
    }
}

static void build_bubble(lv_obj_t *parent, const ChatMessage *m)
{
    bool is_user = (m->role == MSG_ROLE_USER);
    bool failed  = (m->state == MSG_STATE_FAILED);
    bool muted   = (m->state == MSG_STATE_PENDING || m->state == MSG_STATE_CANCELLED);

    /* 整行容器负责左右对齐，气泡本身只管自己的宽度 */
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_bottom(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          is_user ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                          is_user ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START);

    lv_obj_t *bubble = lv_obj_create(row);
    lv_obj_set_width(bubble, LV_PCT(80));
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(bubble, 10, 0);
    lv_obj_set_style_radius(bubble, BUBBLE_RADIUS, 0);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(bubble, 4, 0);

    if (failed) {
        /* L2 失败态：白底细框，绝不用实心黑，和正常用户气泡区分开 */
        lv_obj_set_style_bg_color(bubble, lv_color_white(), 0);
        lv_obj_set_style_text_color(bubble, lv_color_black(), 0);
        lv_obj_set_style_border_width(bubble, 1, 0);
        lv_obj_set_style_border_color(bubble, lv_color_black(), 0);
    } else if (is_user && !muted) {
        lv_obj_set_style_bg_color(bubble, lv_color_black(), 0);
        lv_obj_set_style_text_color(bubble, lv_color_white(), 0);
        lv_obj_set_style_border_width(bubble, 0, 0);
    } else {
        lv_obj_set_style_bg_color(bubble, lv_color_white(), 0);
        lv_obj_set_style_text_color(bubble, lv_color_black(), 0);
        lv_obj_set_style_border_width(bubble, muted ? 1 : 2, 0);
        lv_obj_set_style_border_color(bubble, lv_color_black(), 0);
    }

    char body[MAX_MSG_LENGTH + 32];
    bubble_body_text(m, body, sizeof(body));

    lv_obj_t *label = lv_label_create(bubble);
    lv_label_set_text(label, body);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(100));

    /* 失败原因 + 重试 */
    if (failed) {
        lv_obj_t *err = lv_label_create(bubble);
        lv_label_set_text_fmt(err, LV_SYMBOL_WARNING " %s",
                              chatbot_err_text((ChatErrCode)m->err_code));
        lv_label_set_long_mode(err, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(err, LV_PCT(100));
        lv_obj_set_style_text_font(err, LV_FONT_SMALL, 0);

        lv_obj_t *retry = lv_btn_create(bubble);
        lv_obj_set_size(retry, 96, 40);
        lv_obj_set_style_radius(retry, BUBBLE_RADIUS, 0);
        lv_obj_set_style_bg_color(retry, lv_color_white(), 0);
        lv_obj_set_style_border_width(retry, 2, 0);
        lv_obj_set_style_border_color(retry, lv_color_black(), 0);
        lv_obj_add_event_cb(retry, retry_btn_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)m->id);
        lv_obj_t *rl = lv_label_create(retry);
        lv_label_set_text(rl, "Retry");
        lv_obj_set_style_text_color(rl, lv_color_black(), 0);
        lv_obj_center(rl);
    }

    /* 时间戳：气泡外侧小字 */
    char ts[8];
    fmt_time(ts, sizeof(ts), m->timestamp);
    if (ts[0]) {
        lv_obj_t *time_lbl = lv_label_create(row);
        lv_label_set_text(time_lbl, ts);
        lv_obj_set_style_text_font(time_lbl, LV_FONT_SMALL, 0);
        lv_obj_set_style_pad_top(time_lbl, 2, 0);
    }
}

/* 日期分隔行：今天 / 昨天 / MM-DD */
static void build_day_divider(lv_obj_t *parent, uint32_t ts)
{
    time_t t = (time_t)ts, now = time(NULL);
    struct tm tm_msg, tm_now;
    localtime_r(&t, &tm_msg);
    localtime_r(&now, &tm_now);

    char text[24];
    int dday = tm_now.tm_yday - tm_msg.tm_yday;
    if (tm_msg.tm_year == tm_now.tm_year && dday == 0)      snprintf(text, sizeof(text), "Today");
    else if (tm_msg.tm_year == tm_now.tm_year && dday == 1) snprintf(text, sizeof(text), "Yesterday");
    else snprintf(text, sizeof(text), "%02d-%02d", tm_msg.tm_mon + 1, tm_msg.tm_mday);

    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, LV_FONT_SMALL, 0);
    lv_obj_set_style_pad_ver(lbl, 6, 0);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
}

void chatbot_view_refresh(struct ChatbotApp *app)
{
    if (!app || !app->view || !app->view->chat_container) return;
    ChatbotModel *model = app->model;

    lv_obj_clean(app->view->chat_container);

    if (model->msg_count == 0) {
        lv_obj_t *hint = lv_label_create(app->view->chat_container);
        /* 不写死换行：硬换行在哪断由文案长度决定，改一个字就错位。
         * 交给 LONG_WRAP 按实际宽度自己折。 */
        lv_label_set_text(hint, "Hold the button below to talk, or tap the keyboard icon to type");
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(hint, LV_PCT(100));
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(hint, 40, 0);
        return;
    }

    int last_yday = -1;
    for (uint16_t i = 0; i < model->msg_count; i++) {
        const ChatMessage *m = &model->messages[i];

        if (m->timestamp) {
            time_t t = (time_t)m->timestamp;
            struct tm tm;
            localtime_r(&t, &tm);
            if (tm.tm_yday != last_yday) {
                build_day_divider(app->view->chat_container, m->timestamp);
                last_yday = tm.tm_yday;
            }
        }
        build_bubble(app->view->chat_container, m);
    }

    lv_obj_update_layout(app->view->chat_container);
    lv_obj_scroll_to_y(app->view->chat_container, LV_COORD_MAX, LV_ANIM_OFF);
}

/* ── 状态与横幅 ───────────────────────────────────────────────────── */

void chatbot_view_set_banner(struct ChatbotApp *app, const char *text, bool show)
{
    if (!app || !app->view || !app->view->banner) return;

    if (show && text) {
        lv_label_set_text(app->view->banner_label, text);
        lv_obj_remove_flag(app->view->banner, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(app->view->banner, LV_OBJ_FLAG_HIDDEN);
    }
}

void chatbot_view_update_recording(struct ChatbotApp *app, uint32_t ms, uint8_t level)
{
    if (!app || !app->view || !app->view->rec_time_label) return;

    lv_label_set_text_fmt(app->view->rec_time_label, "%lu\"", (unsigned long)(ms / 1000));

    /* 离散 5 格而不是连续波形 —— 墨水屏上动画等于持续刷新 */
    uint8_t lit = (uint8_t)((level + 19) / 20);
    if (lit > 5) lit = 5;
    for (int i = 0; i < 5; i++) {
        lv_obj_set_style_bg_opa(app->view->rec_level[i],
                                i < lit ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
}

void chatbot_view_set_state(struct ChatbotApp *app, ChatState state)
{
    if (!app || !app->view || !app->view->lbl_record) return;
    ChatbotView *v = app->view;

    bool recording = (state == CHAT_STATE_RECORDING);
    bool waiting   = (state == CHAT_STATE_TRANSCRIBING || state == CHAT_STATE_THINKING);

    switch (state) {
    case CHAT_STATE_RECORDING:    lv_label_set_text(v->lbl_record, "Release to send"); break;
    case CHAT_STATE_TRANSCRIBING: lv_label_set_text(v->lbl_record, "Transcribing - tap to cancel"); break;
    case CHAT_STATE_THINKING:     lv_label_set_text(v->lbl_record, "Thinking - tap to cancel"); break;
    default:                      lv_label_set_text(v->lbl_record, "Hold to Talk"); break;
    }

    if (v->rec_status) {
        if (recording) lv_obj_remove_flag(v->rec_status, LV_OBJ_FLAG_HIDDEN);
        else           lv_obj_add_flag(v->rec_status, LV_OBJ_FLAG_HIDDEN);
    }

    /* 不变量 2：忙时禁用切换输入方式，避免状态撕裂 */
    if (v->btn_mode) {
        if (recording || waiting) lv_obj_add_state(v->btn_mode, LV_STATE_DISABLED);
        else                      lv_obj_remove_state(v->btn_mode, LV_STATE_DISABLED);
    }
}

/* ── 文字输入兜底 ─────────────────────────────────────────────────── */

static void send_text_cb(lv_event_t *e)
{
    ChatbotApp *app = lv_event_get_user_data(e);
    const char *txt = lv_textarea_get_text(app->view->text_input);
    if (!txt || txt[0] == '\0') return;

    chatbot_controller_send_text(txt);
    lv_textarea_set_text(app->view->text_input, "");
}

static void ta_focus_cb(lv_event_t *e)
{
    ChatbotApp *app = lv_event_get_user_data(e);
    if (!app->view->keyboard) return;
    lv_keyboard_set_textarea(app->view->keyboard, app->view->text_input);
    lv_obj_remove_flag(app->view->keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(app->view->keyboard);
}

static void kb_close_cb(lv_event_t *e)
{
    ChatbotApp *app = lv_event_get_user_data(e);
    lv_obj_add_flag(app->view->keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(app->view->keyboard, NULL);
}

static void toggle_mode_cb(lv_event_t *e)
{
    ChatbotApp *app = lv_event_get_user_data(e);
    ChatbotView *v = app->view;
    if (chatbot_controller_is_busy()) return;

    v->text_mode = !v->text_mode;

    if (v->text_mode) {
        lv_obj_add_flag(v->btn_record, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(v->text_input, LV_OBJ_FLAG_HIDDEN);
        /* 文字模式：按钮的含义是"点我切回语音" -> 显示麦克风 */
        lv_obj_add_flag(lv_obj_get_child(v->btn_mode, 0), LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lv_obj_get_child(v->btn_mode, 1), LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(v->btn_record, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(v->text_input, LV_OBJ_FLAG_HIDDEN);
        /* 语音模式：按钮的含义是"点我切到键盘" -> 显示键盘符号 */
        lv_obj_remove_flag(lv_obj_get_child(v->btn_mode, 0), LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lv_obj_get_child(v->btn_mode, 1), LV_OBJ_FLAG_HIDDEN);
        if (v->keyboard) {
            lv_obj_add_flag(v->keyboard, LV_OBJ_FLAG_HIDDEN);
            lv_keyboard_set_textarea(v->keyboard, NULL);
        }
    }
}

/* ── 页面构建 ─────────────────────────────────────────────────────── */

/* 主页面被销毁时（例如跳到 History）必须把 view 里缓存的对象指针清空。
 * 否则从别的页面调 controller（新建/切换/删除会话）时，
 * chatbot_view_refresh() 会去 lv_obj_clean() 一个已经释放的容器。 */
static void main_page_delete_cb(lv_event_t *e)
{
    ChatbotApp *app = lv_event_get_user_data(e);
    if (!app || !app->view) return;
    ChatbotView *v = app->view;

    v->chat_container = NULL;
    v->btn_record     = NULL;
    v->lbl_record     = NULL;
    v->banner         = NULL;
    v->banner_label   = NULL;
    v->rec_status     = NULL;
    v->rec_time_label = NULL;
    v->text_input     = NULL;
    v->keyboard       = NULL;
    v->btn_mode       = NULL;
    for (int i = 0; i < 5; i++) v->rec_level[i] = NULL;
}

static lv_obj_t* build_chatbot_page(struct ChatbotApp* app, void* user_data) {
    (void)user_data;
    Page page = lv_page_create("AI Chatbot", false, NULL, &app->view->page_nav);
    ChatbotView *v = app->view;

    /* header：右侧汉堡菜单（与 todolist 一致）；标题连点 5 次进 mock 调试面板。
     * 用 Page 暴露的 header_right 槽位，别再用 lv_obj_get_child(header, N)
     * 这种魔法索引 —— lv_page 的 header 结构一变就会静默错位。 */
    lv_obj_t *right_slot = page.header_right;
    if (right_slot) {
        lv_obj_set_style_margin_right(right_slot, 8, 0);
        lv_obj_t *menu_icon = lv_label_create(right_slot);
        lv_label_set_text(menu_icon, LV_SYMBOL_BARS);
        lv_obj_center(menu_icon);
        lv_obj_add_flag(menu_icon, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(menu_icon, 15);
        lv_obj_add_event_cb(menu_icon, menu_sheet_event_cb, LV_EVENT_CLICKED, app);
    }
    lv_obj_t *title = lv_obj_get_child(page.header, 1);
    if (title) {
        lv_obj_add_flag(title, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(title, title_tap_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_clear_flag(page.container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(page.container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(page.container, 0, 0);

    /* 对话区 */
    v->chat_container = lv_obj_create(page.container);
    lv_obj_set_width(v->chat_container, LV_PCT(100));
    lv_obj_set_flex_grow(v->chat_container, 1);
    lv_obj_set_flex_flow(v->chat_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(v->chat_container, 8, 0);
    lv_obj_set_style_border_width(v->chat_container, 0, 0);
    lv_obj_set_scroll_dir(v->chat_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(v->chat_container, LV_SCROLLBAR_MODE_OFF);

    /* L3 横幅：钉在输入区上方 */
    v->banner = lv_obj_create(page.container);
    lv_obj_set_width(v->banner, LV_PCT(100));
    lv_obj_set_height(v->banner, LV_SIZE_CONTENT);
    lv_obj_clear_flag(v->banner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(v->banner, 0, 0);
    lv_obj_set_style_pad_all(v->banner, 8, 0);
    lv_obj_set_style_bg_color(v->banner, lv_color_black(), 0);
    lv_obj_set_style_border_width(v->banner, 0, 0);
    v->banner_label = lv_label_create(v->banner);
    lv_label_set_long_mode(v->banner_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(v->banner_label, LV_PCT(100));
    lv_obj_set_style_text_color(v->banner_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(v->banner_label, LV_FONT_SMALL, 0);
    lv_label_set_text(v->banner_label, "");
    lv_obj_add_flag(v->banner, LV_OBJ_FLAG_HIDDEN);

    /* 录音状态行：秒数 + 5 格离散音量 */
    v->rec_status = lv_obj_create(page.container);
    lv_obj_set_width(v->rec_status, LV_PCT(100));
    lv_obj_set_height(v->rec_status, LV_SIZE_CONTENT);
    lv_obj_clear_flag(v->rec_status, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(v->rec_status, 0, 0);
    lv_obj_set_style_pad_all(v->rec_status, 6, 0);
    lv_obj_set_flex_flow(v->rec_status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(v->rec_status, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(v->rec_status, 4, 0);

    v->rec_time_label = lv_label_create(v->rec_status);
    lv_label_set_text(v->rec_time_label, "0\"");
    lv_obj_set_style_pad_right(v->rec_time_label, 8, 0);

    for (int i = 0; i < 5; i++) {
        v->rec_level[i] = lv_obj_create(v->rec_status);
        lv_obj_remove_style_all(v->rec_level[i]);
        lv_obj_set_size(v->rec_level[i], 10, 10 + i * 4);
        lv_obj_set_style_bg_color(v->rec_level[i], lv_color_black(), 0);
        lv_obj_set_style_bg_opa(v->rec_level[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(v->rec_level[i], 1, 0);
        lv_obj_set_style_border_color(v->rec_level[i], lv_color_black(), 0);
    }
    lv_obj_add_flag(v->rec_status, LV_OBJ_FLAG_HIDDEN);

    /* 底部输入区 */
    lv_obj_t * footer = lv_obj_create(page.container);
    lv_obj_set_width(footer, LV_PCT(100));
    lv_obj_set_height(footer, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(footer, 10, 0);
    lv_obj_set_style_pad_column(footer, 8, 0);
    /* footer 自己有 pad_bottom 10，10 + 22 = UI_BOTTOM_ACTION_GAP —— 让
     * Hold to Talk 离屏幕底边的距离和 Clock 的 Add Alarm 一致。 */
    lv_obj_set_style_margin_bottom(footer, UI_BOTTOM_ACTION_GAP - 10, 0);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(footer, 1, 0);

    /* 语音按钮 */
    v->btn_record = lv_btn_create(footer);
    ui_style_set_btn_primary(v->btn_record);
    lv_obj_set_flex_grow(v->btn_record, 1);
    lv_obj_set_height(v->btn_record, UI_BOTTOM_ACTION_H);
    lv_obj_set_style_radius(v->btn_record, BUBBLE_RADIUS, 0);
    v->lbl_record = lv_label_create(v->btn_record);
    lv_label_set_text(v->lbl_record, "Hold to Talk");
    lv_obj_center(v->lbl_record);

    lv_obj_add_event_cb(v->btn_record, chatbot_controller_record_event_cb, LV_EVENT_PRESSED, app);
    lv_obj_add_event_cb(v->btn_record, chatbot_controller_record_event_cb, LV_EVENT_RELEASED, app);
    /* 手指按住后滑出按钮时 LVGL 只发 PRESS_LOST，漏掉会永久卡在 RECORDING */
    lv_obj_add_event_cb(v->btn_record, chatbot_controller_record_event_cb, LV_EVENT_PRESS_LOST, app);

    /* 文字输入（默认隐藏）*/
    v->text_input = lv_textarea_create(footer);
    lv_textarea_set_one_line(v->text_input, true);
    lv_textarea_set_placeholder_text(v->text_input, "Say something...");
    lv_obj_set_flex_grow(v->text_input, 1);
    lv_obj_set_scrollbar_mode(v->text_input, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(v->text_input, ta_focus_cb, LV_EVENT_FOCUSED, app);
    lv_obj_add_event_cb(v->text_input, ta_focus_cb, LV_EVENT_CLICKED, app);
    lv_obj_add_flag(v->text_input, LV_OBJ_FLAG_HIDDEN);

    /* 语音 / 键盘 切换 */
    v->btn_mode = lv_btn_create(footer);
    ui_style_set_btn_secondary(v->btn_mode);
    lv_obj_set_size(v->btn_mode, 56, UI_BOTTOM_ACTION_H);  /* 同一行，高度跟主按钮走 */
    lv_obj_set_style_radius(v->btn_mode, BUBBLE_RADIUS, 0);
    /* 这个按钮在"切到键盘"和"切回语音"之间来回。
     * LVGL 内置符号里没有麦克风 —— 原来切回语音时用的 LV_SYMBOL_AUDIO 是个
     * 音符（0xF001），语义不对。麦克风改用工程自带的 28x28 图片 mic_on。
     * 图片和文字符号没有共同的 setter，所以两个子对象都建好、靠显隐切换，
     * 免得每次 toggle 重建子对象。索引：[0]=键盘符号 [1]=麦克风图片。 */
    lv_obj_t *mode_lbl = lv_label_create(v->btn_mode);
    lv_label_set_text(mode_lbl, LV_SYMBOL_KEYBOARD);
    lv_obj_center(mode_lbl);

    lv_obj_t *mode_mic = lv_image_create(v->btn_mode);
    lv_image_set_src(mode_mic, EPOS_LV_IMG_USE(mic_on));
    lv_obj_center(mode_mic);
    lv_obj_add_flag(mode_mic, LV_OBJ_FLAG_HIDDEN);   /* 默认语音模式，显示键盘符号 */
    lv_obj_add_event_cb(v->btn_mode, toggle_mode_cb, LV_EVENT_CLICKED, app);

    /* 键盘浮层（默认隐藏）；READY = 回车发送 */
    v->keyboard = lv_keyboard_create(page.screen);
    setup_custom_keyboard(v->keyboard);
    lv_obj_set_size(v->keyboard, LV_PCT(100), 250);
    lv_obj_add_flag(v->keyboard, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(v->keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(v->keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(v->keyboard, send_text_cb, LV_EVENT_READY, app);
    lv_obj_add_event_cb(v->keyboard, kb_close_cb, LV_EVENT_READY, app);
    lv_obj_add_event_cb(v->keyboard, kb_close_cb, LV_EVENT_CANCEL, app);

    v->text_mode = false;
    lv_obj_add_event_cb(page.screen, main_page_delete_cb, LV_EVENT_DELETE, app);

    chatbot_view_refresh(app);
    chatbot_view_set_state(app, chatbot_controller_state());

    return page.screen;
}

void chatbot_view_main_init_registry(struct ChatbotApp* app) {
    PAGE_REGISTE(app, PAGE_CHATBOT_MAIN, build_chatbot_page);
}
