#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <lvgl.h>
#include "lv_ui_style_guide.h"
#include "lv_theme_hardcore.h"
#include "lv_toast.h"
#include "esp_log.h"
#include "esp_system.h"

#include "../app.h"
#include "../view.h"
#include "../controller.h"
#include "view_history.h"

static const char *TAG = "chatbot_view_history";

/* 待删除的会话 id，供确认弹窗使用 */
static uint32_t s_pending_delete;

static void rebuild_list(struct ChatbotApp *app);

/* ── 打开会话 ─────────────────────────────────────────────────────── */

static void open_session_cb(lv_event_t *e)
{
    uint32_t sid = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    ChatbotApp *app = &g_chatbot_app;

    chatbot_controller_open_session(sid);
    page_navigator_navigate_pop(&app->view->page_nav, app);
}

/* ── 删除会话（二次确认）──────────────────────────────────────────── */

static void confirm_delete_cb(lv_event_t *e)
{
    lv_obj_t *dialog = lv_event_get_user_data(e);
    ChatbotApp *app = &g_chatbot_app;

    chatbot_controller_delete_session(s_pending_delete);
    s_pending_delete = 0;

    lv_obj_delete(dialog);
    rebuild_list(app);
}

static void dismiss_dialog_cb(lv_event_t *e)
{
    s_pending_delete = 0;
    lv_obj_delete(lv_event_get_user_data(e));
}

static void show_delete_dialog(struct ChatbotApp *app, const ChatSession *s)
{
    s_pending_delete = s->id;

    lv_obj_t *dialog = lv_obj_create(lv_screen_active());
    lv_obj_set_size(dialog, LV_PCT(86), LV_SIZE_CONTENT);
    lv_obj_center(dialog);
    lv_obj_set_flex_flow(dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(dialog, 16, 0);
    lv_obj_set_style_pad_row(dialog, 12, 0);
    lv_obj_set_style_radius(dialog, 2, 0);
    lv_obj_set_style_bg_color(dialog, lv_color_white(), 0);
    lv_obj_set_style_border_width(dialog, 2, 0);
    lv_obj_set_style_border_color(dialog, lv_color_black(), 0);
    lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *msg = lv_label_create(dialog);
    lv_label_set_text_fmt(msg, "Delete \"%s\"?\nEvery message in this chat will be removed.", s->title);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg, LV_PCT(100));

    lv_obj_t *row = lv_obj_create(dialog);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_AROUND,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cancel = lv_btn_create(row);
    ui_style_set_btn_secondary(cancel);
    lv_obj_set_size(cancel, 120, 48);
    lv_obj_set_style_radius(cancel, 2, 0);
    lv_obj_t *cl = lv_label_create(cancel);
    lv_label_set_text(cl, "Cancel");
    lv_obj_center(cl);
    lv_obj_add_event_cb(cancel, dismiss_dialog_cb, LV_EVENT_CLICKED, dialog);

    lv_obj_t *ok = lv_btn_create(row);
    ui_style_set_btn_primary(ok);
    lv_obj_set_size(ok, 120, 48);
    lv_obj_set_style_radius(ok, 2, 0);
    lv_obj_t *ol = lv_label_create(ok);
    lv_label_set_text(ol, "Delete");
    lv_obj_center(ol);
    lv_obj_add_event_cb(ok, confirm_delete_cb, LV_EVENT_CLICKED, dialog);

    (void)app;
}

static void delete_btn_cb(lv_event_t *e)
{
    uint32_t sid = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    ChatbotApp *app = &g_chatbot_app;

    ChatSession *s = chatbot_model_find_session(app->model, sid);
    if (!s) return;
    show_delete_dialog(app, s);
}

/* ── 新建对话 ─────────────────────────────────────────────────────── */

static void new_session_cb(lv_event_t *e)
{
    (void)e;
    ChatbotApp *app = &g_chatbot_app;
    chatbot_controller_new_session();
    page_navigator_navigate_pop(&app->view->page_nav, app);
}

/* ── 列表 ─────────────────────────────────────────────────────────── */

static void fmt_when(char *out, size_t len, uint32_t ts)
{
    if (ts == 0) { snprintf(out, len, "—"); return; }

    time_t t = (time_t)ts, now = time(NULL);
    struct tm tm_msg, tm_now;
    localtime_r(&t, &tm_msg);
    localtime_r(&now, &tm_now);

    int dday = tm_now.tm_yday - tm_msg.tm_yday;
    if (tm_msg.tm_year == tm_now.tm_year && dday == 0) {
        snprintf(out, len, "Today %02d:%02d", tm_msg.tm_hour, tm_msg.tm_min);
    } else if (tm_msg.tm_year == tm_now.tm_year && dday == 1) {
        snprintf(out, len, "Yesterday %02d:%02d", tm_msg.tm_hour, tm_msg.tm_min);
    } else {
        snprintf(out, len, "%02d-%02d", tm_msg.tm_mon + 1, tm_msg.tm_mday);
    }
}

static void rebuild_list(struct ChatbotApp *app)
{
    ViewHisCtx *ctx = (ViewHisCtx *)app->view->page_nav.nav_ctx;
    if (!ctx || !ctx->list_container) return;

    lv_obj_clean(ctx->list_container);

    ChatbotModel *model = app->model;
    if (model->session_count == 0) {
        lv_obj_t *empty = lv_label_create(ctx->list_container);
        lv_label_set_text(empty, "No chats yet");
        lv_obj_set_style_pad_top(empty, 40, 0);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    /* 按 updated_at 倒序：最近的排最前 */
    uint8_t order[CHATBOT_MAX_SESSIONS];
    for (uint8_t i = 0; i < model->session_count; i++) order[i] = i;
    for (uint8_t i = 0; i < model->session_count; i++) {
        for (uint8_t j = i + 1; j < model->session_count; j++) {
            if (model->sessions[order[j]].updated_at > model->sessions[order[i]].updated_at) {
                uint8_t t = order[i]; order[i] = order[j]; order[j] = t;
            }
        }
    }

    for (uint8_t k = 0; k < model->session_count; k++) {
        const ChatSession *s = &model->sessions[order[k]];
        bool active = (s->id == model->active_session_id);

        lv_obj_t *item = lv_obj_create(ctx->list_container);
        lv_obj_set_width(item, LV_PCT(100));
        lv_obj_set_height(item, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(item, 10, 0);
        lv_obj_set_style_pad_bottom(item, 10, 0);
        lv_obj_set_style_radius(item, 2, 0);
        lv_obj_set_style_border_width(item, active ? 2 : 1, 0);
        lv_obj_set_style_border_color(item, lv_color_black(), 0);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        /* 左侧：标题 + 时间/条数，点击打开 */
        lv_obj_t *info = lv_obj_create(item);
        lv_obj_remove_style_all(info);
        lv_obj_set_flex_grow(info, 1);
        lv_obj_set_height(info, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(info, 4, 0);
        lv_obj_add_flag(info, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(info, open_session_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)s->id);

        lv_obj_t *title = lv_label_create(info);
        lv_label_set_text_fmt(title, "%s%s", active ? LV_SYMBOL_OK " " : "", s->title);
        lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(title, LV_PCT(100));

        char when[24];
        fmt_when(when, sizeof(when), s->updated_at);
        lv_obj_t *meta = lv_label_create(info);
        lv_label_set_text_fmt(meta, "%s  -  %u msgs", when, s->msg_count);
        lv_obj_set_style_text_font(meta, LV_FONT_SMALL, 0);

        /* 右侧：删除 */
        lv_obj_t *del = lv_btn_create(item);
        ui_style_set_btn_secondary(del);
        lv_obj_set_size(del, 56, 48);
        lv_obj_set_style_radius(del, 2, 0);
        lv_obj_t *di = lv_label_create(del);
        lv_label_set_text(di, LV_SYMBOL_TRASH);
        lv_obj_center(di);
        lv_obj_add_event_cb(del, delete_btn_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)s->id);
    }
}

/* ── 页面构建 ─────────────────────────────────────────────────────── */

static lv_obj_t* build_history_page(struct ChatbotApp* app, void* user_data) {
    (void)user_data;
    Page page = lv_page_create("Chatbot History", true,
                               page_navigator_navigate_back, &app->view->page_nav);

    ViewHisCtx *ctx = malloc(sizeof(ViewHisCtx));
    if (!ctx) {
        ESP_LOGE(TAG, "Failed to allocate history context");
        return NULL;
    }
    memset(ctx, 0, sizeof(ViewHisCtx));
    app->view->page_nav.nav_ctx = ctx;

    lv_obj_clear_flag(page.container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(page.container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(page.container, 0, 0);

    ctx->list_container = lv_obj_create(page.container);
    lv_obj_set_width(ctx->list_container, LV_PCT(100));
    lv_obj_set_flex_grow(ctx->list_container, 1);
    lv_obj_set_flex_flow(ctx->list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(ctx->list_container, 8, 0);
    lv_obj_set_style_pad_row(ctx->list_container, 10, 0);
    lv_obj_set_style_border_width(ctx->list_container, 0, 0);
    lv_obj_set_scroll_dir(ctx->list_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ctx->list_container, LV_SCROLLBAR_MODE_OFF);

    /* 底部：新建对话 */
    lv_obj_t *footer = lv_obj_create(page.container);
    lv_obj_set_width(footer, LV_PCT(100));
    lv_obj_set_height(footer, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(footer, 1, 0);
    lv_obj_set_style_pad_all(footer, 10, 0);

    ctx->delete_btn = lv_btn_create(footer);
    ui_style_set_btn_primary(ctx->delete_btn);
    lv_obj_set_size(ctx->delete_btn, LV_PCT(100), 50);
    lv_obj_set_style_radius(ctx->delete_btn, 2, 0);
    lv_obj_t *ni = lv_label_create(ctx->delete_btn);
    lv_label_set_text(ni, LV_SYMBOL_PLUS "  New Chat");
    lv_obj_center(ni);
    lv_obj_add_event_cb(ctx->delete_btn, new_session_cb, LV_EVENT_CLICKED, app);

    rebuild_list(app);

    return page.screen;
}

void chatbot_view_history_init_registry(struct ChatbotApp* app) {
    PAGE_REGISTE(app, PAGE_CHATBOT_HISTORY, build_history_page);
}
