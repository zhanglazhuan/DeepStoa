#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <lvgl.h>
#include "lv_ui_style_guide.h"
#include "esp_system.h"
#include "esp_log.h"

#include "lv_epd_region.h"
#include "controller_decks.h"

#include "view_deck_form.h"
#include "../app.h"
#include "../model.h"
#include "../view.h"
#include "../controller.h"
#include "widgets/lv_keyboard.h"

static const char *TAG = "anki_view_deck_form";

// 页面私有上下文
typedef struct {
    lv_obj_t* name_input;
    lv_obj_t* keyboard;
    int8_t target_deck_idx;
    DeckFormMode mode;
} DeckFormContext;

static void deck_form_confirm(lv_event_t* e);
static void deck_form_back(lv_event_t* e);
static void ta_event_cb(lv_event_t* e);
static void ta_bind_keyboard(lv_obj_t* kb, lv_obj_t* ta);
static void keyboard_event_cb(lv_event_t* e);
static void bg_click_cb(lv_event_t* e);

// 处理背景空白区域的点击（收起键盘）
static void bg_click_cb(lv_event_t* e) {
    lv_obj_t* target = lv_event_get_target(e);
    lv_obj_t* current_target = lv_event_get_current_target(e);

    // 确保只有直接点击绑定的容器时才触发
    if (target != current_target) return;
    
    DeckFormContext* ctx = lv_event_get_user_data(e);
    if (!ctx || !ctx->keyboard) return;
    
    // 隐藏键盘并清除焦点
    lv_obj_add_flag(ctx->keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(ctx->keyboard, NULL);
    if (ctx->name_input) lv_obj_clear_state(ctx->name_input, LV_STATE_FOCUSED);

    epd_region_end();
}

static void deck_form_confirm(lv_event_t* e) {
    AnkiApp* app = lv_event_get_user_data(e);
    if (!app || !app->view->page_nav.nav_ctx) return;
    
    DeckFormContext* ctx = (DeckFormContext*)app->view->page_nav.nav_ctx;
    
    if (ctx->keyboard) {
        lv_obj_add_flag(ctx->keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(ctx->keyboard, NULL);
        lv_obj_clear_state(ctx->name_input, LV_STATE_FOCUSED);
    }
    
    epd_region_end();

    const char* deck_name = lv_textarea_get_text(ctx->name_input);
    
    // 非空校验
    if (strlen(deck_name) == 0) return;
    
    // --- 根据不同模式执行对应的 Model 数据操作 ---
    if (ctx->mode == DECK_FORM_MODE_ADD_ROOT) {
        ESP_LOGI(TAG, "Add Root Deck: %s", deck_name);
        anki_controller_add_root_deck(app, deck_name);
    } 
    else if (ctx->mode == DECK_FORM_MODE_ADD_SUB) {
        ESP_LOGI(TAG, "Add Sub Deck to parent %d: %s", ctx->target_deck_idx, deck_name);
        anki_controller_add_sub_deck(app, ctx->target_deck_idx, deck_name);
    } 
    else if (ctx->mode == DECK_FORM_MODE_EDIT) {
        ESP_LOGI(TAG, "Edit Deck %d: %s", ctx->target_deck_idx, deck_name);
        anki_controller_edit_deck(app, ctx->target_deck_idx, deck_name);
    }
    
    lv_textarea_set_text(ctx->name_input, "");

    page_navigator_navigate_pop(&app->view->page_nav, app);
}

static void deck_form_back(lv_event_t* e) {
    AnkiApp* app = lv_event_get_user_data(e);
    if (!app) return;
    
    DeckFormContext* ctx = (DeckFormContext*)app->view->page_nav.nav_ctx;
    if (ctx) {
        if (ctx->keyboard) {
            lv_obj_add_flag(ctx->keyboard, LV_OBJ_FLAG_HIDDEN);
            lv_keyboard_set_textarea(ctx->keyboard, NULL);
        }

        epd_region_end();
        
        lv_textarea_set_text(ctx->name_input, "");
    }

    page_navigator_navigate_pop(&app->view->page_nav, app);
}

/* lv_keyboard_set_textarea() 内部会 remove/add LV_STATE_FOCUSED，每调一次就产生两个
 * LV_EVENT_STYLE_CHANGED，而 lv_textarea 会在 STYLE_CHANGED 里重跑
 * lv_textarea_scroll_to_cusor_pos()。绑定目标没变时直接跳过，避免无谓的滚动抖动。 */
static void ta_bind_keyboard(lv_obj_t* kb, lv_obj_t* ta) {
    if (lv_keyboard_get_textarea(kb) == ta) return;
    lv_keyboard_set_textarea(kb, ta);
}

static void ta_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* ta = lv_event_get_target(e);
    DeckFormContext* ctx = lv_event_get_user_data(e);

    if (!ctx || !ctx->keyboard) return;

    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "trigger code=%d kb_hidden=%d", (int)code,
                 (int)lv_obj_has_flag(ctx->keyboard, LV_OBJ_FLAG_HIDDEN));

        if (lv_obj_has_flag(ctx->keyboard, LV_OBJ_FLAG_HIDDEN)) {
            ESP_LOGI(TAG, "SHOW keyboard");
            ta_bind_keyboard(ctx->keyboard, ta);
            lv_obj_clear_flag(ctx->keyboard, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(ctx->keyboard);
            lv_obj_update_layout(ctx->keyboard);

            /* 先整屏打一帧（把键盘这些静态内容打到面板并同步差分基准），
             * 之后窗口收到输入框上，打字只刷输入框那一块。
             * helper 内部走 lv_async_call，不会在本次输入事件里重入 LVGL
             * 的刷新流程；聚焦 outline 的外扩也由它统一处理。 */
            epd_region_begin_focus(lv_screen_active(), ta);
        } else {
            ta_bind_keyboard(ctx->keyboard, ta);
        }

        lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);
    }
}

static void keyboard_event_cb(lv_event_t* e) {
    lv_obj_t* kb = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_indev_t* indev = lv_indev_get_act();
        if (indev) {
            lv_indev_wait_release(indev);
        }

        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

        epd_region_end();

        lv_obj_t* ta = lv_keyboard_get_textarea(kb);
        lv_keyboard_set_textarea(kb, NULL);
        if (ta) {
            lv_obj_clear_state(ta, LV_STATE_FOCUSED); 
        }
    }
}

static lv_obj_t* build_deck_form_page(struct AnkiApp* app, void* user_data) {
    // 接收从 view_decks 传过来的 payload
    DeckFormMode mode = (DeckFormMode)(intptr_t)user_data;

    if (mode < DECK_FORM_MODE_ADD_ROOT || mode > DECK_FORM_MODE_EDIT) {
        ESP_LOGE(TAG, "Invalid deck form mode: %d", (int)mode);
        return NULL;
    }
    if ((mode == DECK_FORM_MODE_ADD_SUB || mode == DECK_FORM_MODE_EDIT) &&
        (app->model->target_deck_idx < 0 ||
         app->model->target_deck_idx >= app->model->deck_count)) {
        ESP_LOGE(TAG, "Invalid target deck idx: %d", app->model->target_deck_idx);
        return NULL;
    }

    // 动态确定页面标题
    const char* page_title = "Deck Form";
    if (mode == DECK_FORM_MODE_ADD_ROOT) page_title = "Add Deck";
    else if (mode == DECK_FORM_MODE_ADD_SUB) page_title = "Add Sub Deck";
    else if (mode == DECK_FORM_MODE_EDIT) page_title = "Edit Deck";

    Page page = lv_page_create(page_title, true, deck_form_back, app);
    lv_obj_t* content_cont = page.container;
    
    DeckFormContext* ctx = malloc(sizeof(DeckFormContext));
    if (!ctx) {
        ESP_LOGE(TAG, "Failed to allocate deck form context");
        return NULL;
    }
    memset(ctx, 0, sizeof(DeckFormContext));
    ctx->mode = mode;
    ctx->target_deck_idx = app->model->target_deck_idx;
    app->view->page_nav.nav_ctx = ctx;
    
    // 背景点击处理
    lv_obj_add_flag(content_cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(content_cont, bg_click_cb, LV_EVENT_CLICKED, ctx);
    
    // --- Deck 标题输入容器 ---
    lv_obj_t* name_cont = lv_obj_create(content_cont);
    lv_obj_remove_style_all(name_cont);
    lv_obj_set_width(name_cont, LV_PCT(100));
    lv_obj_set_height(name_cont, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(name_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(name_cont, 8, 0);
    lv_obj_add_flag(name_cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(name_cont, bg_click_cb, LV_EVENT_CLICKED, ctx);
    
    lv_obj_t* name_label = lv_label_create(name_cont);
    lv_label_set_text(name_label, "Deck Name");
    lv_obj_set_style_text_font(name_label, LV_FONT_SMALL, LV_PART_MAIN);
    
    ctx->name_input = lv_textarea_create(name_cont);
    lv_textarea_set_one_line(ctx->name_input, true);
    /* one_line 已经把高度设为 LV_SIZE_CONTENT（恰好容纳一行）。不要再写死高度：
     * 一旦 content_height < font line_height，lv_textarea_scroll_to_cusor_pos()
     * 的顶部/底部判定会互相翻转，文字在两个滚动位置之间反复跳动。 */
    lv_obj_set_width(ctx->name_input, LV_PCT(100));
    lv_obj_set_style_border_width(ctx->name_input, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(ctx->name_input, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(ctx->name_input, 2, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(ctx->name_input, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_opa(ctx->name_input, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ctx->name_input, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_border_opa(ctx->name_input, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_anim_duration(ctx->name_input, 0, LV_PART_CURSOR);
    lv_obj_add_event_cb(ctx->name_input, ta_event_cb, LV_EVENT_FOCUSED, ctx);
    lv_obj_add_event_cb(ctx->name_input, ta_event_cb, LV_EVENT_CLICKED, ctx);

    // 数据回填：如果是 Edit 模式，自动填入原来的名字
    if (ctx->mode == DECK_FORM_MODE_EDIT) {
        AnkiDeck *target_deck = &app->model->decks[ctx->target_deck_idx];
        lv_textarea_set_text(ctx->name_input, target_deck->name);
    }

    // --- 按钮容器 ---
    lv_obj_t* button_cont = lv_obj_create(content_cont);
    lv_obj_remove_style_all(button_cont);
    lv_obj_set_width(button_cont, LV_PCT(100));
    lv_obj_set_height(button_cont, LV_SIZE_CONTENT);
    lv_obj_set_style_margin_top(button_cont, 30, LV_PART_MAIN);
    lv_obj_set_flex_flow(button_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button_cont, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(button_cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button_cont, bg_click_cb, LV_EVENT_CLICKED, ctx);
    
    lv_obj_t* cancel_btn = lv_btn_create(button_cont);
    ui_style_set_btn_secondary(cancel_btn);
    lv_obj_set_size(cancel_btn, 180, 60);
    lv_obj_add_event_cb(cancel_btn, deck_form_back, LV_EVENT_CLICKED, app);

    lv_obj_t* cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);

    lv_obj_t* confirm_btn = lv_btn_create(button_cont);
    ui_style_set_btn_primary(confirm_btn);
    lv_obj_set_size(confirm_btn, 180, 60);
    lv_obj_add_event_cb(confirm_btn, deck_form_confirm, LV_EVENT_CLICKED, app);
    
    lv_obj_t* confirm_label = lv_label_create(confirm_btn);
    lv_label_set_text(confirm_label, "Confirm");
    lv_obj_center(confirm_label);
    
    // --- 键盘 ---
    ctx->keyboard = lv_keyboard_create(page.screen);
    setup_custom_keyboard(ctx->keyboard);
    lv_obj_set_size(ctx->keyboard, LV_PCT(100), 250);
    lv_obj_add_flag(ctx->keyboard, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(ctx->keyboard, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_pad_all(ctx->keyboard, 5, 0);
    lv_obj_add_flag(ctx->keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(ctx->keyboard, lv_color_white(), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(ctx->keyboard, lv_color_black(), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(ctx->keyboard, lv_color_black(), LV_PART_ITEMS | LV_STATE_PRESSED);
    
    lv_obj_add_event_cb(ctx->keyboard, keyboard_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(ctx->keyboard, keyboard_event_cb, LV_EVENT_CANCEL, NULL);

    return page.screen;
}

void anki_view_deck_form_init_registry(struct AnkiApp* app) {
    PAGE_REGISTE(app, PAGE_DECK_FORM, build_deck_form_page);
}
