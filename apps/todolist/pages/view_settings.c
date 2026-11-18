#include <stdio.h>
#include "view_settings.h"

/* 步进不能是 1：25 -> 50 分钟要点 25 下，每一下都是一帧墨水屏刷新 */
#define TODOLIST_SETTINGS_STEP_MIN       5
#define TODOLIST_SETTINGS_ALERT_STEP_MIN 1

#include "controller_settings.h"

#include "../view.h"
#include "../controller.h"
#include "../model.h"
#include "../app.h"

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
    lv_obj_set_width(task_lbl, LV_PCT(90)); // 占满整行，文本自然左对齐
    
    lv_obj_t * task_time_item = lv_number_input_create(body, NULL, config->task_duration_min, TODOLIST_SETTINGS_STEP_MIN, NULL);
    // 将其内部的 flex 对齐方式从 SPACE_BETWEEN 覆盖为 CENTER，使其居中
    lv_obj_set_flex_align(task_time_item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(task_time_item, task_duration_change_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // --- Rest Time 模块 ---
    lv_obj_t * rest_lbl = lv_label_create(body);
    lv_label_set_text(rest_lbl, "Rest Time (min)");
    lv_obj_set_width(rest_lbl, LV_PCT(90)); // 占满整行，文本自然左对齐
    lv_obj_set_style_margin_top(rest_lbl, 10, LV_PART_MAIN);
    
    lv_obj_t * rest_time_item = lv_number_input_create(body, NULL, config->rest_duration_min, TODOLIST_SETTINGS_STEP_MIN, NULL);
    lv_obj_set_flex_align(rest_time_item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(rest_time_item, rest_duration_change_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // --- Pre-alert 模块 ---
    lv_obj_t * pre_alert_lbl = lv_label_create(body);
    lv_label_set_text(pre_alert_lbl, "Pre-alert (min)");
    lv_obj_set_width(pre_alert_lbl, LV_PCT(90)); // 占满整行，文本自然左对齐
    lv_obj_set_style_margin_top(pre_alert_lbl, 10, LV_PART_MAIN);
    
    lv_obj_t * pre_alert_item = lv_number_input_create(body, NULL, config->pre_alert_min, TODOLIST_SETTINGS_ALERT_STEP_MIN, NULL);
    lv_obj_set_flex_align(pre_alert_item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(pre_alert_item, pre_alert_change_cb, LV_EVENT_VALUE_CHANGED, NULL);

    return page.screen;
}

void todolist_view_settings_init_registry(struct TodoListApp* app) {
    PAGE_REGISTE(app, PAGE_SETTINGS, build_settings_page);
}
