#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include <lvgl.h>
#include "lv_ui_style_guide.h"

#include "view_decks.h"
#include "controller_decks.h"
#include "../view.h"
#include "../app.h"
#include "../model.h"

static const char *TAG = "anki_view_decks";

// --- 定义传给 Deck Form 的 Payload ---
typedef enum {
    DECK_FORM_MODE_ADD_ROOT,
    DECK_FORM_MODE_ADD_SUB,
    DECK_FORM_MODE_EDIT
} DeckFormMode;

// --- 提前声明 ---
static void view_decks_refresh_list(AnkiApp *app);
static void view_decks_show_delete_dialog(AnkiApp *app, int deck_idx);
static void view_decks_show_empty_prompt(AnkiApp *app, int deck_idx);

// ==========================================
// Bottom Sheet 相关代码 (保持原样)
// ==========================================
static void close_sheet_wrapper_cb(lv_event_t * e) {
    lv_bottom_sheet_t * sheet = lv_event_get_user_data(e);
    lv_bottom_sheet_close(sheet);
}

// 1. Add these new explicit callbacks for the global menu buttons
static void global_menu_stats_cb(lv_event_t * e) {
    AnkiApp *app = lv_event_get_user_data(e);
    app->model->target_deck_idx = -1;
    PAGE_NAVIGATE_TO(app, PAGE_STATS, NULL);
}

static void global_menu_settings_cb(lv_event_t * e) {
    AnkiApp *app = lv_event_get_user_data(e);
    app->model->target_deck_idx = -1;
    PAGE_NAVIGATE_TO(app, PAGE_SETTINGS, NULL);
}

static void open_global_menu_sheet_event_cb(lv_event_t * e) {
    AnkiApp * app = lv_event_get_user_data(e);
    
    lv_bottom_sheet_t* sheet = lv_bottom_sheet_create(NULL);

    lv_obj_t * content = lv_bottom_sheet_get_content(sheet);
    lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    
    const char* nav_labels[] = {"Statistic", "Settings", "Cancel"};
    void (*callbacks[])(lv_event_t *) = {global_menu_stats_cb, global_menu_settings_cb, NULL};

    app->model->target_deck_idx = -1;
    
    int option_count = sizeof(nav_labels) / sizeof(nav_labels[0]);
    for(int i = 0; i < option_count; i++) {
        lv_obj_t *btn = lv_btn_create(content);
        lv_obj_set_width(btn, LV_PCT(100)); 
        lv_obj_set_height(btn, EPOS_BOTTOM_BUTTON_HEIGHT);         
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
        lv_label_set_text(lbl, nav_labels[i]);
        lv_obj_center(lbl); 
        
        if (callbacks[i] != NULL) {
            lv_obj_add_event_cb(btn, callbacks[i], LV_EVENT_CLICKED, app);
        } else {
            lv_obj_add_event_cb(btn, close_sheet_wrapper_cb, LV_EVENT_CLICKED, sheet);
        }
    }
}

// ==========================================
// 动作回调：跳转到表单页
// ==========================================
static void deck_add_root_cb(lv_event_t * e) {
    AnkiApp *app = lv_event_get_user_data(e);
    app->model->target_deck_idx = -1;
    PAGE_NAVIGATE_TO(app, PAGE_DECK_FORM, (void*)(intptr_t)DECK_FORM_MODE_ADD_ROOT);
}

// ==========================================
// More 弹窗内选项的具体动作回调
// ==========================================
static void more_edit_cb(lv_event_t * e) {
    AnkiApp *app = lv_event_get_user_data(e);
    int deck_idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    app->model->target_deck_idx = deck_idx;
    PAGE_NAVIGATE_TO(app, PAGE_DECK_FORM, (void*)(intptr_t)DECK_FORM_MODE_EDIT);
}

static void more_add_sub_cb(lv_event_t * e) {
    AnkiApp *app = lv_event_get_user_data(e);
    int deck_idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    app->model->target_deck_idx = deck_idx;
    PAGE_NAVIGATE_TO(app, PAGE_DECK_FORM, (void*)(intptr_t)DECK_FORM_MODE_ADD_SUB);
}

static void more_import_cb(lv_event_t * e) {
    AnkiApp *app = lv_event_get_user_data(e);
    int deck_idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    app->model->target_deck_idx = deck_idx;
    PAGE_NAVIGATE_TO(app, PAGE_IMPORT, NULL);
}

static void more_stats_cb(lv_event_t * e) {
    AnkiApp *app = lv_event_get_user_data(e);
    int deck_idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    app->model->target_deck_idx = deck_idx;
    PAGE_NAVIGATE_TO(app, PAGE_STATS, NULL);
}

static void more_delete_cb(lv_event_t * e) {
    AnkiApp *app = lv_event_get_user_data(e);
    lv_obj_t *button = lv_event_get_current_target(e);
    lv_obj_t *menu_content = lv_obj_get_parent(button);
    lv_bottom_sheet_t *menu_sheet = lv_obj_get_user_data(menu_content);
    int deck_idx = (int)(intptr_t)lv_obj_get_user_data(button);

    if (!app || !app->model || deck_idx < 0 || deck_idx >= app->model->deck_count) {
        ESP_LOGE(TAG, "Cannot delete invalid deck idx: %d", deck_idx);
        return;
    }

    /* Close the action menu before presenting the destructive confirmation. */
    lv_bottom_sheet_close(menu_sheet);
    view_decks_show_delete_dialog(app, deck_idx);
}

// ==========================================
// 点击 Deck 每一行的 More 弹出的 Bottom Sheet
// ==========================================
static void deck_more_sheet_cb(lv_event_t * e) {
    /* The menu is a child of the clickable row; it must never activate the row. */
    lv_event_stop_bubbling(e);

    AnkiApp *app = lv_event_get_user_data(e);
    int deck_idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));

    if (!app || !app->model || deck_idx < 0 || deck_idx >= app->model->deck_count) {
        ESP_LOGE(TAG, "Cannot open menu for invalid deck idx: %d", deck_idx);
        return;
    }

    lv_bottom_sheet_t* sheet = lv_bottom_sheet_create(NULL);
    if (!sheet) {
        ESP_LOGE(TAG, "Failed to create deck action menu");
        return;
    }

    lv_obj_t * content = lv_bottom_sheet_get_content(sheet);
    lv_obj_set_user_data(content, sheet);
    lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

    // 1. 将指针和数量声明提升到作用域外部
    char** labels;
    void (**callbacks)(lv_event_t *);
    int option_count;

    // 2. 提前定义好两种菜单的静态数组
    static char* labels_normal[] = {"Edit name", "Import cards", "Add subdeck", "Statistics", "Delete", "Cancel"};
    static void (*callbacks_normal[])(lv_event_t *) = {
        more_edit_cb, more_import_cb, more_add_sub_cb, more_stats_cb, more_delete_cb, NULL
    };

    static char* labels_deep[] = {"Edit name", "Import cards", "Statistics", "Delete", "Cancel"};
    static void (*callbacks_deep[])(lv_event_t *) = {
        more_edit_cb, more_import_cb, more_stats_cb, more_delete_cb, NULL
    };

    // 3. 根据深度动态分配指针 (已修正写反的业务逻辑)
    AnkiDeck *deck = &app->model->decks[deck_idx];
    int depth = anki_model_get_deck_depth(app->model, deck->parent_id);
    if (depth >= 3) {
        labels = labels_deep;
        callbacks = callbacks_deep;
        option_count = sizeof(labels_deep) / sizeof(labels_deep[0]);
    } else {
        labels = labels_normal;
        callbacks = callbacks_normal;
        option_count = sizeof(labels_normal) / sizeof(labels_normal[0]);
    }

    // 4. 生成列表
    for(int i = 0; i < option_count; i++) {
        lv_obj_t *btn = lv_btn_create(content);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, EPOS_BOTTOM_BUTTON_HEIGHT);
        lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
        lv_obj_set_style_text_color(btn, lv_color_black(), 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);

        // 前面的按钮加底部分割线
        if (i < option_count - 1) {
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
            lv_obj_set_style_border_color(btn, lv_color_black(), 0);
        }

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_center(lbl);

        lv_obj_set_user_data(btn, (void*)(intptr_t)deck_idx);

        if (callbacks[i]) {
            lv_obj_add_event_cb(btn, callbacks[i], LV_EVENT_CLICKED, app);
        } else {
            lv_obj_add_event_cb(btn, close_sheet_wrapper_cb, LV_EVENT_CLICKED, sheet);
        }
    }
}

// ==========================================
// 动作回调：点击牌组进入学习
// ==========================================
static void deck_row_click_cb(lv_event_t * e) {
    AnkiApp *app = lv_event_get_user_data(e);
    lv_obj_t *target = lv_event_get_current_target(e);
    int deck_idx = (int)(intptr_t)lv_obj_get_user_data(target);

    if (!app || !app->model || deck_idx < 0 || deck_idx >= app->model->deck_count) {
        ESP_LOGE(TAG, "Cannot open invalid deck idx: %d", deck_idx);
        return;
    }

    app->model->active_deck_idx = deck_idx;
    app->model->active_card_idx = 0;
    app->model->is_showing_answer = false;

    // 获取子树聚合卡片总数，若 > 0 则允许进入学习；否则弹提示
    int tree_cards = app->model->decks[deck_idx].total_cards_count;
    if (tree_cards > 0) {
        PAGE_NAVIGATE_TO(app, PAGE_STUDY, NULL);
    } else {
        ESP_LOGW(TAG, "Deck tree is empty, cannot study!");
        view_decks_show_empty_prompt(app, deck_idx);
    }
}

/*
 * A horizontal swipe on a vertically scrollable list has no LVGL scroll object,
 * so LVGL would otherwise emit CLICKED when the pointer is released.  Swipes no
 * longer have a product action; consume the remainder of that touch sequence so
 * it cannot accidentally open the deck.
 */
static void deck_row_cancel_gesture_cb(lv_event_t * e) {
    lv_indev_t *indev = lv_event_get_param(e);
    if (!indev) indev = lv_indev_active();
    if (indev) lv_indev_wait_release(indev);
}

// ==========================================
// 删除底部弹窗逻辑
// ==========================================
static void delete_deck_confirm_cb(lv_event_t * e) {
    lv_bottom_sheet_t * sheet = (lv_bottom_sheet_t *)lv_event_get_user_data(e);
    AnkiApp *app = &g_anki_app;

    if(sheet && app) {
        int deck_idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));

        anki_model_delete_deck(app->model, deck_idx);
        ESP_LOGI(TAG, "Confirmed deleting deck idx: %d", deck_idx);

        lv_bottom_sheet_close(sheet);
        // 删除后局部刷新列表
        lv_async_call((lv_async_cb_t)view_decks_refresh_list, app);
    }
}

static void view_decks_show_delete_dialog(AnkiApp *app, int deck_idx) {
    lv_bottom_sheet_t * sheet = lv_bottom_sheet_create(NULL);
    lv_bottom_sheet_add_header(sheet, "Delete deck");

    lv_obj_t * content = lv_bottom_sheet_get_content(sheet);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(content, 24, 0);

    // 提示文案：与之前 msgbox 一致
    AnkiDeck *deck = &app->model->decks[deck_idx];
    char msg_buf[128];
    snprintf(msg_buf, sizeof(msg_buf), "Delete '%s' and all its cards?", deck->name);

    lv_obj_t * msg = lv_label_create(content);
    lv_label_set_text(msg, msg_buf);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg, LV_PCT(100));

    // 按钮行：左右布局（参考 ExitApp）
    lv_obj_t *btn_row = lv_obj_create(content);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_width(btn_row, LV_PCT(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Cancel（secondary）
    lv_obj_t *btn_cancel = lv_button_create(btn_row);
    lv_obj_set_size(btn_cancel, 120, 48);
    lv_obj_add_event_cb(btn_cancel, close_sheet_wrapper_cb, LV_EVENT_CLICKED, sheet);
    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Cancel");
    lv_obj_center(lbl_cancel);
    ui_style_set_btn_secondary(btn_cancel);

    // Delete（primary，携带 deck_idx 供确认回调读取）
    lv_obj_t *btn_del = lv_button_create(btn_row);
    lv_obj_set_size(btn_del, 120, 48);
    lv_obj_add_event_cb(btn_del, delete_deck_confirm_cb, LV_EVENT_CLICKED, sheet);
    lv_obj_t *lbl_del = lv_label_create(btn_del);
    lv_label_set_text(lbl_del, "Delete");
    lv_obj_center(lbl_del);
    ui_style_set_btn_primary(btn_del);
    lv_obj_set_user_data(btn_del, (void *)(intptr_t)deck_idx);
}

// ==========================================
// 空牌组提示底部弹窗（卡片为 0，无法进入学习）
// ==========================================
static void view_decks_show_empty_prompt(AnkiApp *app, int deck_idx) {
    lv_bottom_sheet_t * sheet = lv_bottom_sheet_create(NULL);
    lv_bottom_sheet_add_header(sheet, "No cards");

    lv_obj_t * content = lv_bottom_sheet_get_content(sheet);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(content, 24, 0);

    AnkiDeck *deck = &app->model->decks[deck_idx];
    char msg_buf[128];
    snprintf(msg_buf, sizeof(msg_buf), "Deck '%s' has no cards yet.\nAdd some cards before studying.", deck->name);

    lv_obj_t * msg = lv_label_create(content);
    lv_label_set_text(msg, msg_buf);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg, LV_PCT(100));

    // OK 按钮：单个、居中，参考 ExitApp 按钮样式（无需分割线）
    lv_obj_t *btn_row = lv_obj_create(content);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_width(btn_row, LV_PCT(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *btn_ok = lv_button_create(btn_row);
    lv_obj_set_size(btn_ok, 120, 48);
    lv_obj_add_flag(btn_ok, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn_ok, close_sheet_wrapper_cb, LV_EVENT_CLICKED, sheet);
    lv_obj_t *lbl_ok = lv_label_create(btn_ok);
    lv_label_set_text(lbl_ok, "OK");
    lv_obj_center(lbl_ok);
    ui_style_set_btn_primary(btn_ok);
}

// ==========================================
// 核心：构建 Deck 的自定义单行
// ==========================================
static lv_obj_t * view_create_deck_row(lv_obj_t *parent, AnkiDeck *deck, int deck_idx, AnkiApp* app) {
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));     
    lv_obj_set_height(row, LV_SIZE_CONTENT); 
    lv_obj_set_style_pad_top(row, 8, 0);    
    lv_obj_set_style_pad_bottom(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // 【修改点 1：纯净对齐方案】完全依靠 padding-left 进行层级缩进
    int depth = anki_model_get_deck_depth(app->model, deck->parent_id);
    int pad_left = 12 + (depth * 24);
    lv_obj_set_style_pad_left(row, pad_left, 0);
    lv_obj_set_style_pad_right(row, 12, 0);
    lv_obj_set_style_pad_column(row, 12, 0); 

    // 整行只有一个主动作：点击进入学习。破坏性操作统一放到右侧菜单。
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(row, deck_row_cancel_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(row, deck_row_click_cb, LV_EVENT_CLICKED, app);
    lv_obj_set_user_data(row, (void*)(intptr_t)deck_idx);

    // 1. 图标 
    lv_obj_t * icon = lv_label_create(row);
    lv_label_set_text(icon, deck->parent_id == -1 ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE);

    // 2. 标题 
    lv_obj_t * label = lv_label_create(row);
    lv_obj_set_width(label, 0);
    lv_obj_set_flex_grow(label, 1); 
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP); 
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(label, deck->name); // 请确保此时 Model 里的 name 是没有任何空格前缀的纯净字符串
    
    // 卡片数量信息：x/y (包含所有子牌组的聚合统计)
    char cards_info[16];
    uint16_t unlearned = deck->new_cards_count + deck->due_cards_count;
    snprintf(cards_info, sizeof(cards_info), "%d/%d", unlearned, deck->total_cards_count);
    lv_obj_t * cards_label = lv_label_create(row);
    lv_label_set_text(cards_label, cards_info);
    lv_obj_set_style_text_align(cards_label, LV_TEXT_ALIGN_RIGHT, 0);
    
    // More 按钮
    lv_obj_t * more_icon = lv_label_create(row);
    lv_label_set_text(more_icon, LV_SYMBOL_BARS); // 标准齿轮图标，你也可以换成 LV_SYMBOL_LIST 等
    lv_obj_set_ext_click_area(more_icon, 15);
    lv_obj_add_flag(more_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(more_icon, deck_more_sheet_cb, LV_EVENT_CLICKED, app);
    lv_obj_set_user_data(more_icon, (void*)(intptr_t)deck_idx);

    return row;
}

// 渲染列表页
static void view_decks_refresh_list(AnkiApp *app) {
    if (!app || !app->view || !app->view->page_nav.nav_ctx) return;
    AnkiViewDecksCtx *ctx = (AnkiViewDecksCtx*)app->view->page_nav.nav_ctx;
    
    // 增加一层有效性保护
    if (ctx->deck_list_cont && lv_obj_is_valid(ctx->deck_list_cont)) {
        lv_obj_clean(ctx->deck_list_cont);
        for (int i = 0; i < app->model->deck_count; i++) {
            AnkiDeck *deck = &app->model->decks[i];
            view_create_deck_row(ctx->deck_list_cont, deck, i, app);
        }
    }
}

// ==========================================
// 页面构建主入口
// ==========================================
static lv_obj_t* build_decks_page(AnkiApp* app, void* user_data) {
    Page page = lv_page_create("eAnki", false, NULL, &app->view->page_nav);

    // 申请上下文
    if (app->view->page_nav.nav_ctx != NULL) {
        free(app->view->page_nav.nav_ctx);
    }
    AnkiViewDecksCtx* decks_ctx = malloc(sizeof(AnkiViewDecksCtx));
    memset(decks_ctx, 0, sizeof(AnkiViewDecksCtx));
    app->view->page_nav.nav_ctx = decks_ctx;

    // 1. Header 菜单图标
    lv_obj_t *right_slot = lv_obj_get_child(page.header, 2);
    lv_obj_t *menu_btn = lv_btn_create(right_slot);
    lv_obj_set_size(menu_btn, LV_PCT(100), LV_PCT(100)); // 让按钮完全填满 40x40 的右侧预留槽位
    lv_obj_set_ext_click_area(menu_btn, 15);
    lv_obj_set_style_bg_color(menu_btn, lv_color_white(), 0);
    lv_obj_set_style_text_color(menu_btn, lv_color_black(), 0);
    lv_obj_set_style_border_width(menu_btn, 0, 0);
    lv_obj_set_style_pad_all(menu_btn, 0, 0);
    lv_obj_center(menu_btn);
    
    lv_obj_t *menu_icon = lv_label_create(menu_btn);
    lv_label_set_text(menu_icon, LV_SYMBOL_SETTINGS); 
    lv_obj_add_event_cb(menu_btn, open_global_menu_sheet_event_cb, LV_EVENT_CLICKED, app);

    // 2. 卡片列表容器 (使用 Flex Column 替代原先的 lv_list)
    lv_obj_t *list_cont = lv_obj_create(page.container);
    lv_obj_remove_style_all(list_cont);
    lv_obj_set_size(list_cont, LV_PCT(100), LV_PCT(100)); 
    lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list_cont, LV_DIR_VER);
    lv_obj_set_style_pad_row(list_cont, 12, 0); // 行间距
    decks_ctx->deck_list_cont = list_cont;

    // 填充数据
    view_decks_refresh_list(app);

    // 3. 悬浮的 Add Root Deck 按钮
    lv_obj_t * add_btn = lv_button_create(page.container);
    ui_style_set_btn_primary(add_btn);
    lv_obj_set_size(add_btn, 80, 80);
    lv_obj_set_ext_click_area(add_btn, 10);
    lv_obj_add_flag(add_btn, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
    lv_obj_align(add_btn, LV_ALIGN_BOTTOM_RIGHT, -30, -30);
    lv_obj_set_style_radius(add_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(add_btn, 0, LV_PART_MAIN);

    lv_obj_t * add_lbl = lv_label_create(add_btn);
    lv_label_set_text(add_lbl, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(add_lbl, LV_FONT_NORMAL, 0); 
    lv_obj_set_style_text_color(add_lbl, lv_color_white(), 0); 
    lv_obj_center(add_lbl);
    
    lv_obj_add_event_cb(add_btn, deck_add_root_cb, LV_EVENT_CLICKED, app);

    return page.screen;
}

void anki_view_decks_init_registry(struct AnkiApp* app) {
    PAGE_REGISTE(app, PAGE_DECKS, build_decks_page);
}
