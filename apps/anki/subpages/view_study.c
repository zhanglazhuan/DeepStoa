#include <string.h>
#include <stdlib.h>
#include "esp_system.h"
#include <lvgl.h>
#include "lv_ui_style_guide.h"
#include "lv_bottom_sheet.h"
#include "view_study.h"
#include "controller_study.h"
#include "../view.h"
#include "../controller.h"


/* sheet 里的动作按钮：primary = 实心（危险/主动作），否则描边。
 * 并排放在按钮行里，尺寸见 UI_SHEET_BTN_*。 */
/* 弹层的动作按钮行：横排，Cancel 在左、主按钮在右。
 * SPACE_EVENLY 让两枚朝中间收，两侧留出边距，不贴屏幕边。
 * 尺寸/约定见 lv_ui_style_guide.h 的 UI_SHEET_BTN_*。 */
static lv_obj_t *study_btn_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
}

static lv_obj_t *study_action_btn(lv_obj_t *parent, const char *text, bool primary,
                        lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    if (primary) ui_style_set_btn_primary(btn);
    else         ui_style_set_btn_secondary(btn);
    lv_obj_set_size(btn, UI_SHEET_BTN_W, UI_SHEET_BTN_H);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, LV_FONT_SMALL, 0);
    lv_obj_center(lbl);
    return btn;
}

/* 建弹层壳子：标题 + 正文，返回内容区供调用方接着加按钮。
 * 刻意不调 lv_bottom_sheet_add_header() —— 它自带关闭 ×，会变成第三个出口。
 * 点遮罩关闭是 widget 自带的无害默认退出，不算一个"选择"。 */
static lv_obj_t *study_sheet_body(lv_bottom_sheet_t *bs, const char *title, const char *message)
{
    lv_obj_t *c = lv_bottom_sheet_get_content(bs);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(c, 12, 0);

    lv_obj_t *t = lv_label_create(c);
    lv_obj_set_width(t, LV_PCT(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(t, LV_FONT_NORMAL, 0);
    lv_label_set_text(t, title);

    lv_obj_t *m = lv_label_create(c);
    lv_obj_set_width(m, LV_PCT(100));
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(m, LV_FONT_SMALL, 0);
    lv_obj_set_style_pad_bottom(m, 8, 0);
    lv_label_set_text(m, message);
    return c;
}

static void study_finished_ok_cb(lv_event_t *e)
{
    lv_bottom_sheet_close((lv_bottom_sheet_t *)lv_event_get_user_data(e));
    /* 先关弹层再切页：弹层挂在 lv_layer_top 上，不随 screen 走，
     * 反过来会短暂盖在上一页上面。 */
    AnkiApp *app = &g_anki_app;
    page_navigator_navigate_pop(&app->view->page_nav, app);
}

void anki_view_study_show_finished_msgbox(AnkiApp *app)
{
    AnkiViewStudyCtx * study_ctx = app->view->page_nav.nav_ctx;
    if (study_ctx) {
        if (study_ctx->btn_answer) lv_obj_add_flag(study_ctx->btn_answer, LV_OBJ_FLAG_HIDDEN);
        if (study_ctx->separator) lv_obj_add_flag(study_ctx->separator, LV_OBJ_FLAG_HIDDEN);
        if (study_ctx->lbl_answer) lv_obj_add_flag(study_ctx->lbl_answer, LV_OBJ_FLAG_HIDDEN);
        if (study_ctx->btn_rating) lv_obj_add_flag(study_ctx->btn_rating, LV_OBJ_FLAG_HIDDEN);
    }

    lv_bottom_sheet_t *bs = lv_bottom_sheet_create(lv_layer_top());
    if (!bs) return;

    lv_obj_t *c = study_sheet_body(bs, "Study Completed",
        "Congratulations! You have finished studying this deck for today.");
    /* 只有一个动作，仍然靠右 —— 主按钮位置和其它弹层保持一致 */
    lv_obj_t *row = study_btn_row(c);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    study_action_btn(row, "OK", true, study_finished_ok_cb, bs);
}

void anki_view_study_card_flip(AnkiApp* app) {
    AnkiModel *m = app->model;
    AnkiViewStudyCtx * study_ctx = app->view->page_nav.nav_ctx;

    if(m->is_showing_answer) {
        // 显示答案状态：隐藏显示答案按钮，显示答案和评分按钮
        lv_obj_add_flag(study_ctx->btn_answer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(study_ctx->separator, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(study_ctx->lbl_answer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(study_ctx->btn_rating, LV_OBJ_FLAG_HIDDEN);
    } else {
        // 显示正面状态：显示显示答案按钮，隐藏答案和评分按钮
        lv_obj_clear_flag(study_ctx->btn_answer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(study_ctx->separator, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(study_ctx->lbl_answer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(study_ctx->btn_rating, LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t* build_study_page(AnkiApp* app, void* user_data) {
    Page page = lv_page_create("Study", true, page_navigator_navigate_back, &app->view->page_nav);

    AnkiModel *m = app->model;
    bool has_card = anki_model_load_next_card(m);
    CardPayload *payload = &m->current_card_payload;

    if (app->view->page_nav.nav_ctx != NULL) {
        free(app->view->page_nav.nav_ctx);
        app->view->page_nav.nav_ctx = NULL;
    }
    AnkiViewStudyCtx* study_ctx = malloc(sizeof(AnkiViewStudyCtx));
    memset(study_ctx, 0, sizeof(AnkiViewStudyCtx));
    app->view->page_nav.nav_ctx = study_ctx;

    // 卡片内容容器
    lv_obj_t *content = lv_obj_create(page.container);
    lv_obj_set_size(content, LV_PCT(98), LV_PCT(80));
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content, 20, 0);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);

    // 正面
    lv_obj_t *lbl_front = lv_label_create(content);
    lv_obj_set_width(lbl_front, LV_PCT(100)); 
    lv_label_set_long_mode(lbl_front, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(lbl_front, LV_TEXT_ALIGN_LEFT, 0); 
    if (has_card) {
        lv_label_set_text(lbl_front, payload->front);
    } else {
        lv_label_set_text(lbl_front, "");
    }
    study_ctx->lbl_front = lbl_front;

    // "显示答案" 大按钮
    lv_obj_t *btn_answer = lv_btn_create(page.container);
    ui_style_set_btn_secondary(btn_answer);
    lv_obj_set_style_bg_color(btn_answer, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(btn_answer, LV_OPA_COVER, 0);
    lv_obj_set_size(btn_answer, LV_PCT(100), 60);
    lv_obj_align(btn_answer, LV_ALIGN_BOTTOM_MID, 0, 0);
    study_ctx->btn_answer = btn_answer;

    lv_obj_t *lbl = lv_label_create(btn_answer);
    lv_label_set_text(lbl, "Show Answer");
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn_answer, anki_controller_on_show_answer, LV_EVENT_CLICKED, app);

    // 分割线
    lv_obj_t *separator = lv_obj_create(content);
    lv_obj_remove_style_all(separator); 
    lv_obj_set_size(separator, LV_PCT(30), 2); 
    lv_obj_set_style_bg_color(separator, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, 0);
    study_ctx->separator = separator;

    // 反面文本
    lv_obj_t *lbl_answer = lv_label_create(content);
    lv_label_set_text(lbl_answer, payload->back);
    lv_obj_set_width(lbl_answer, LV_PCT(100));
    lv_obj_set_style_text_align(lbl_answer, LV_TEXT_ALIGN_LEFT, 0);
    study_ctx->lbl_answer = lbl_answer;

    // 底部评分按钮组 (墨水屏 4级灰度区分)
    lv_obj_t *btn_rating = lv_obj_create(page.container);
    lv_obj_set_size(btn_rating, LV_PCT(100), 60);
    lv_obj_align(btn_rating, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(btn_rating, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_rating, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn_rating, 0, 0);
    lv_obj_set_style_border_width(btn_rating, 0, 0);
    study_ctx->btn_rating = btn_rating;

    const char *btn_txt[] = {"Hard", "Good", "Easy"};
    uint32_t grayscale[] = {0x555555, 0xAAAAAA, 0xFFFFFF}; 
    uint32_t txt_color[] = {0xFFFFFF, 0x000000, 0x000000}; 

    for(int i=0; i<3; i++) {
        lv_obj_t *btn = lv_btn_create(btn_rating);
        ui_style_set_btn_secondary(btn);
        lv_obj_set_style_bg_color(btn, lv_color_hex(grayscale[i]), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_size(btn, LV_PCT(32), LV_PCT(90));

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, btn_txt[i]);
        lv_obj_set_style_text_color(lbl, lv_color_hex(txt_color[i]), 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, anki_controller_on_rate_card, LV_EVENT_CLICKED, app);
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
    }

    if (has_card) {
        lv_obj_clear_flag(study_ctx->btn_answer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(study_ctx->separator, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(study_ctx->lbl_answer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(study_ctx->btn_rating, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(study_ctx->btn_answer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(study_ctx->separator, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(study_ctx->lbl_answer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(study_ctx->btn_rating, LV_OBJ_FLAG_HIDDEN);
        anki_view_study_show_finished_msgbox(app);
    }

    return page.screen;
}

void anki_view_study_refresh_card(AnkiApp *app) {
    AnkiModel *m = app->model;
    AnkiViewStudyCtx * study_ctx = app->view->page_nav.nav_ctx;

    CardPayload *payload = &m->current_card_payload;

    // 1. 更新卡片正面内容
    if (study_ctx->lbl_front) {
        lv_label_set_text(study_ctx->lbl_front, payload->front);
    }
    
    // 2. 更新卡片背面内容
    if (study_ctx->lbl_answer) {
        lv_label_set_text(study_ctx->lbl_answer, payload->back);
    }
    
    // 3. 重置显示状态
    anki_view_study_card_flip(app);
}

void anki_view_study_init_registry(struct AnkiApp* app) {
    PAGE_REGISTE(app, PAGE_STUDY, build_study_page);
}
