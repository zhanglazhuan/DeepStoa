#include <stdio.h>
#include "view_settings.h"
#include "lv_num_input.h"
#include "lv_ui_style_guide.h"

/* 步进不能是 1：25 -> 50 分钟要点 25 下，每一下都是一帧墨水屏刷新 */
#define TODOLIST_SETTINGS_STEP_MIN       5
#define TODOLIST_SETTINGS_ALERT_STEP_MIN 1

#include "controller_settings.h"

#include "../view.h"
#include "../controller.h"
#include "../model.h"
#include "../app.h"

static lv_obj_t *create_settings_number_input(lv_obj_t *parent, int value,
                                             int step, lv_event_cb_t changed_cb) {
    lv_obj_t *input = lv_number_input_create(parent, NULL, value, step, NULL);
    if (!input) return NULL;
    lv_obj_set_width(input, LV_PCT(80));

    /* With no title/unit, the widget contains one row: minus, value, plus. */
    lv_obj_t *row = lv_obj_get_child(input, 0);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_t *minus = lv_obj_get_child(row, 0);
    lv_obj_t *field = lv_obj_get_child(row, 1);
    lv_obj_t *plus = lv_obj_get_child(row, 2);
    ui_style_set_btn_secondary(minus);
    ui_style_set_btn_secondary(plus);
    lv_obj_set_style_radius(minus, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(plus, 4, LV_PART_MAIN);
    lv_obj_set_width(minus, 64);
    lv_obj_set_width(plus, 64);
    lv_obj_set_width(field, 0);
    lv_obj_set_flex_grow(field, 1);
    lv_obj_set_style_border_width(field, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(field, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_side(field, LV_BORDER_SIDE_FULL, LV_PART_MAIN);
    lv_obj_set_style_radius(field, 4, LV_PART_MAIN);
    lv_num_input_set_height(input, 56);
    lv_obj_add_event_cb(input, changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    return input;
}

static lv_obj_t* build_settings_page(TodoListApp* app) {
    Page page = lv_page_create("Settings", true, page_navigator_navigate_back, &app->view->page_nav);
    lv_obj_t * body = page.container;
    
    // 设置页面垂直 Flex 布局 (保持主容器居中策略)
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 页面销毁时把配置一次性落盘 —— 不管用户是点返回、还是被系统关掉，都会走到这里 */
    settings_mark_config_clean();
    lv_obj_add_event_cb(page.screen, settings_flush_config, LV_EVENT_DELETE, NULL);

    todolist_config_t* config = todolist_model_config_get();

    // --- Task Time 模块 ---
    lv_obj_t * task_lbl = lv_label_create(body);
    lv_label_set_text(task_lbl, "Task Time (min)");
    lv_obj_set_width(task_lbl, LV_PCT(80));
    
    create_settings_number_input(body, config->task_duration_min,
                                 TODOLIST_SETTINGS_STEP_MIN, task_duration_change_cb);

    // --- Rest Time 模块 ---
    lv_obj_t * rest_lbl = lv_label_create(body);
    lv_label_set_text(rest_lbl, "Rest Time (min)");
    lv_obj_set_width(rest_lbl, LV_PCT(80));
    lv_obj_set_style_margin_top(rest_lbl, 10, LV_PART_MAIN);
    
    create_settings_number_input(body, config->rest_duration_min,
                                 TODOLIST_SETTINGS_STEP_MIN, rest_duration_change_cb);

    // --- Pre-alert 模块 ---
    lv_obj_t * pre_alert_lbl = lv_label_create(body);
    lv_label_set_text(pre_alert_lbl, "Pre-alert (min)");
    lv_obj_set_width(pre_alert_lbl, LV_PCT(80));
    lv_obj_set_style_margin_top(pre_alert_lbl, 10, LV_PART_MAIN);
    
    create_settings_number_input(body, config->pre_alert_min,
                                 TODOLIST_SETTINGS_ALERT_STEP_MIN, pre_alert_change_cb);

    return page.screen;
}

void todolist_view_settings_init_registry(struct TodoListApp* app) {
    PAGE_REGISTE(app, PAGE_SETTINGS, build_settings_page);
}
