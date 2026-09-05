#include <stdio.h>
#include <stdlib.h>
#include <lvgl.h>
#include "lv_ui_style_guide.h"

#include "view_settings.h"
#include "controller_settings.h"
#include "../view.h"

// 弹窗上下文与业务逻辑 ---
typedef struct {
    AnkiApp *app;
    lv_obj_t *sheet;
    lv_obj_t *num_input;
    lv_obj_t *target_label;
    const char *format_str;
} NumInputCtx;

// 在 view_settings.c 顶部，或者 view_settings.h 中定义
typedef struct {
    const char *title;         // 弹窗标题
    const char *desc;          // 解释文本 (可选，可传 NULL)
    int init_val;              // 初始值
    int step;                  // 步进值
    int min_val;               // 最小值 (顺便扩展一下，让代码更健壮)
    int max_val;               // 最大值 (顺便扩展)
    const char *format_str;    // 格式化字符串
    lv_obj_t *target_label;    // 需要实时更新的 View 层 Label
    lv_event_cb_t ctrl_cb;     // Controller 层回调
} NumInputSheetCfg;

// 弹窗销毁时的统一内存释放点 (避免点击遮罩关闭时漏释放)
static void sheet_delete_cb(lv_event_t *e) {
    NumInputCtx *ctx = lv_event_get_user_data(e);
    if (ctx) {
        lv_free(ctx);
    }
}

// View 层专属：仅负责实时更新父页面的 Label 文本
static void view_num_input_changed_cb(lv_event_t *e) {
    NumInputCtx *ctx = lv_event_get_user_data(e);
    // 直接从 param 取出值
    int val = (int)(intptr_t)lv_event_get_param(e);

    char buf[32];
    snprintf(buf, sizeof(buf), ctx->format_str, val);
    lv_label_set_text(ctx->target_label, buf);
}

// 呼出数字输入弹窗的通用封装
static void open_num_input_sheet(AnkiApp *app, const NumInputSheetCfg *cfg) {
    if (!cfg) return;

    NumInputCtx *ctx = lv_malloc(sizeof(NumInputCtx));
    ctx->app = app;
    ctx->target_label = cfg->target_label;
    ctx->format_str = cfg->format_str;

    lv_bottom_sheet_t *sheet = lv_bottom_sheet_create(lv_screen_active());
    ctx->sheet = (lv_obj_t *)sheet;
    lv_obj_add_event_cb((lv_obj_t *)sheet, sheet_delete_cb, LV_EVENT_DELETE, ctx);
    lv_bottom_sheet_add_header(sheet, cfg->title);

    lv_obj_t *content = lv_bottom_sheet_get_content(sheet);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (cfg->desc != NULL) {
        lv_obj_t *desc_label = lv_label_create(content);
        lv_label_set_text(desc_label, cfg->desc);
        lv_obj_set_width(desc_label, LV_PCT(100));
        lv_label_set_long_mode(desc_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(desc_label, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_pad_bottom(desc_label, 15, 0);
    }

    // 创建数字输入框，并使用 config 里的步长
    ctx->num_input = lv_number_input_create(content, NULL, cfg->init_val, cfg->step, NULL);
    
    // 如果你用的 lv_number_input 支持设置极值，可以在这里设置
    // lv_number_input_set_range(ctx->num_input, cfg->min_val, cfg->max_val);

    lv_obj_add_event_cb(ctx->num_input, view_num_input_changed_cb, LV_EVENT_VALUE_CHANGED, ctx);
    
    if (cfg->ctrl_cb) {
        lv_obj_add_event_cb(ctx->num_input, cfg->ctrl_cb, LV_EVENT_VALUE_CHANGED, app);
    }
}

// 1. 修改原本的 task_plan_cb 变为 new_cards_cb
static void new_cards_cb(lv_event_t *e) {
    AnkiApp *app = lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *btn_label = lv_obj_get_child(btn, 0); 
    
    int current_val = atoi(lv_label_get_text(btn_label));
    if(current_val <= 0) current_val = 20;
    
    open_num_input_sheet(app, &(NumInputSheetCfg){
        .title = "New Cards Daily",
        .desc = "How many new cards you want to learn daily?",
        .init_val = current_val,
        .step = 5,
        .min_val = 0,           
        .max_val = 100,
        .target_label = btn_label,
        .format_str = "%d",
        .ctrl_cb = anki_controller_settings_new_cards_changed // 绑定新回调
    });
}

// 2. 新增 max_cards_cb
static void max_cards_cb(lv_event_t *e) {
    AnkiApp *app = lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *btn_label = lv_obj_get_child(btn, 0); 
    
    int current_val = atoi(lv_label_get_text(btn_label));
    if(current_val <= 0) current_val = 200;
    
    open_num_input_sheet(app, &(NumInputSheetCfg){
        .title = "Max Cards Daily",
        .desc = "Today's due Review cards + today's New card quota", // 按照你的要求设置描述
        .init_val = current_val,
        .step = 5,
        .min_val = 10,           
        .max_val = 999,
        .target_label = btn_label,
        .format_str = "%d",
        .ctrl_cb = anki_controller_settings_max_cards_changed // 绑定新回调
    });
}

static void study_duration_cb(lv_event_t *e) {
    AnkiApp *app = lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *btn_label = lv_obj_get_child(btn, 0);
    
    int current_val = atoi(lv_label_get_text(btn_label));
    if(current_val <= 0) current_val = 30;
    
    open_num_input_sheet(app, &(NumInputSheetCfg){
        .title = "Study Duration",
        .desc = "How many minutes you want to study daily?",        // 如果不需要描述，甚至可以直接不写这行，C99 会自动初始化为 0/NULL
        .init_val = current_val,
        .step = 5,
        .target_label = btn_label,
        .format_str = "%d",
        .ctrl_cb = anki_controller_settings_study_duration_changed
    });
}

// ==========================================
// 打开复习间隔设置弹窗 (纯净无 Context 版)
// ==========================================
static void open_schedule_sheet(AnkiApp *app) {
    // 1. 创建底层 sheet 面板 (关闭时 LVGL 自动回收其所有内存)
    lv_bottom_sheet_t *sheet = lv_bottom_sheet_create(lv_screen_active());

    // 2. 添加带有 X 按钮的 Header
    lv_bottom_sheet_add_header(sheet, "Review Schedule");

    // 3. 获取内容区并设置布局
    lv_obj_t *content = lv_bottom_sheet_get_content(sheet);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 20, 0);

    lv_obj_t *desc_label = lv_label_create(content);
    lv_label_set_text(desc_label, "Review again after how many days?");
    lv_obj_set_width(desc_label, LV_PCT(100));
    lv_label_set_long_mode(desc_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(desc_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_pad_bottom(desc_label, 15, 0);

    AnkiSettings *settings = anki_controller_settings_get_config(app);
    
    // 4. Hard 间隔设置 - 直接绑定 Controller，传入 app 即可
    lv_obj_t * hard_input = lv_number_input_create(content, "Hard:", settings->hard_interval, 1, NULL);
    lv_obj_set_size(hard_input, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_add_event_cb(hard_input, anki_controller_settings_hard_interval_changed, LV_EVENT_VALUE_CHANGED, app);

    // 5. Good 间隔设置
    lv_obj_t * good_input = lv_number_input_create(content, "Good:", settings->good_interval, 1, NULL);
    lv_obj_set_size(good_input, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_add_event_cb(good_input, anki_controller_settings_good_interval_changed, LV_EVENT_VALUE_CHANGED, app);

    // 6. Easy 间隔设置
    lv_obj_t * easy_input = lv_number_input_create(content, "Easy:", settings->easy_interval, 1, NULL);
    lv_obj_set_size(easy_input, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_add_event_cb(easy_input, anki_controller_settings_easy_interval_changed, LV_EVENT_VALUE_CHANGED, app);
}

// 复习间隔设置回调
static void schedule_cb(lv_event_t *e) {
    AnkiApp *app = lv_event_get_user_data(e);
    open_schedule_sheet(app);
}

// 构建设置页面
static lv_obj_t* build_settings_page(AnkiApp* app, void* user_data) {
    int deck_idx = app->model->target_deck_idx;
    const char* title = (deck_idx == -1) ? "Global Settings" : "Deck Settings";
    Page page = lv_page_create(title, true, page_navigator_navigate_back, &app->view->page_nav);

    AnkiSettings *settings = anki_controller_settings_get_config(app);
    char text_buf[32];

    lv_obj_t *new_cards_cont = lv_obj_create(page.container);
    lv_obj_set_size(new_cards_cont, LV_PCT(100), 60); 
    lv_obj_align(new_cards_cont, LV_ALIGN_TOP_MID, 0, 10); // Y: 10
    lv_obj_set_flex_flow(new_cards_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(new_cards_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    lv_obj_t *new_cards_label = lv_label_create(new_cards_cont);
    lv_label_set_text(new_cards_label, "New Cards");
    
    lv_obj_t *new_cards_btn = lv_btn_create(new_cards_cont);
    ui_style_set_btn_secondary(new_cards_btn);
    lv_obj_set_size(new_cards_btn, 160, 40);
    lv_obj_add_event_cb(new_cards_btn, new_cards_cb, LV_EVENT_CLICKED, app);
    
    lv_obj_t *new_cards_btn_label = lv_label_create(new_cards_btn);
    snprintf(text_buf, sizeof(text_buf), "%d", settings->new_cards_daily);
    lv_label_set_text(new_cards_btn_label, text_buf);
    lv_obj_center(new_cards_btn_label);

    // 【2】新增：Max Cards 设置
    lv_obj_t *max_cards_cont = lv_obj_create(page.container);
    lv_obj_set_size(max_cards_cont, LV_PCT(100), 60); 
    lv_obj_align(max_cards_cont, LV_ALIGN_TOP_MID, 0, 80); // Y: 80
    lv_obj_set_flex_flow(max_cards_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(max_cards_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    lv_obj_t *max_cards_label = lv_label_create(max_cards_cont);
    lv_label_set_text(max_cards_label, "Max Cards");
    
    lv_obj_t *max_cards_btn = lv_btn_create(max_cards_cont);
    ui_style_set_btn_secondary(max_cards_btn);
    lv_obj_set_size(max_cards_btn, 160, 40);
    lv_obj_add_event_cb(max_cards_btn, max_cards_cb, LV_EVENT_CLICKED, app);
    
    lv_obj_t *max_cards_btn_label = lv_label_create(max_cards_btn);
    snprintf(text_buf, sizeof(text_buf), "%d", settings->max_cards_daily);
    lv_label_set_text(max_cards_btn_label, text_buf);
    lv_obj_center(max_cards_btn_label);

    // 学习时长设置
    lv_obj_t *study_duration_cont = lv_obj_create(page.container);
    lv_obj_set_size(study_duration_cont, LV_PCT(100), 60);
    lv_obj_align(study_duration_cont, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_flex_flow(study_duration_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(study_duration_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    lv_obj_t *study_duration_label = lv_label_create(study_duration_cont);
    lv_label_set_text(study_duration_label, "Duration(min)");
    
    lv_obj_t *study_duration_btn = lv_btn_create(study_duration_cont);
    ui_style_set_btn_secondary(study_duration_btn);
    lv_obj_set_size(study_duration_btn, 160, 40);
    lv_obj_add_event_cb(study_duration_btn, study_duration_cb, LV_EVENT_CLICKED, app);
    
    lv_obj_t *study_duration_btn_label = lv_label_create(study_duration_btn);
    snprintf(text_buf, sizeof(text_buf), "%d", settings->study_duration_mins);
    lv_label_set_text(study_duration_btn_label, text_buf);
    lv_obj_center(study_duration_btn_label);

    // 复习间隔设置
    lv_obj_t *schedule_cont = lv_obj_create(page.container);
    lv_obj_set_size(schedule_cont, LV_PCT(100), 60);
    lv_obj_align(schedule_cont, LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_set_flex_flow(schedule_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(schedule_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    lv_obj_t *schedule_label = lv_label_create(schedule_cont);
    lv_label_set_text(schedule_label, "Schedule");
    
    lv_obj_t *schedule_btn = lv_btn_create(schedule_cont);
    ui_style_set_btn_secondary(schedule_btn);
    lv_obj_set_size(schedule_btn, 160, 40);
    lv_obj_add_event_cb(schedule_btn, schedule_cb, LV_EVENT_CLICKED, app);
    
    lv_obj_t *schedule_btn_label = lv_label_create(schedule_btn);
    lv_label_set_text(schedule_btn_label, "Edit");
    lv_obj_center(schedule_btn_label);

    // 允许提前学习设置
    lv_obj_t *advance_study_cont = lv_obj_create(page.container);
    lv_obj_set_size(advance_study_cont, LV_PCT(100), 60);
    lv_obj_align(advance_study_cont, LV_ALIGN_TOP_MID, 0, 220);
    lv_obj_set_flex_flow(advance_study_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(advance_study_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    lv_obj_t *advance_study_label = lv_label_create(advance_study_cont);
    lv_label_set_text(advance_study_label, "Allow Early Study");
    
    lv_obj_t *advance_study_switch = lv_switch_create(advance_study_cont);
    ui_style_set_switch(advance_study_switch);
    if (settings->allow_study_ahead) {
        lv_obj_add_state(advance_study_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(advance_study_switch, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(advance_study_switch, anki_controller_settings_advance_study_changed, LV_EVENT_VALUE_CHANGED, app);

    return page.screen;
}

void anki_view_settings_init_registry(struct AnkiApp* app) {
    PAGE_REGISTE(app, PAGE_SETTINGS, build_settings_page);
}