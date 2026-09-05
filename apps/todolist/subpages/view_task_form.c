#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <lvgl.h>
#include "lv_ui_style_guide.h"
#include "esp_system.h"
#include "esp_log.h"

#include "lv_epd_region.h"

#include "lv_toast.h"
#include "view_task_form.h"
#include "../utils.h"
#include "../app.h"
#include "../model.h"
#include "../view.h"
#include "../controller.h"

static const char *TAG = "todolist_view_task_form";

typedef struct {
    lv_obj_t* title_input;
    lv_obj_t* estimate_input;
    lv_obj_t* keyboard;
    /* 存 id 不存 todolist_task_t*：表单要跨若干帧存活，期间后台计时器完成一个任务
     * 就会 memmove 压缩 todo 数组，指针会指向另一条任务，保存时写错条目。
     * 0 = 新建模式。 */
    uint32_t  edit_task_id;
} TaskFormContext;

static void task_form_confirm(lv_event_t* e);
static void task_form_back(lv_event_t* e);
static void task_form_delete(lv_event_t* e);

static void task_form_pop_async(void *arg) {
    TodoListApp* app = arg;
    page_navigator_navigate_pop(&app->view->page_nav, app);
}
static void ta_event_cb(lv_event_t* e);
static void ta_bind_keyboard(lv_obj_t* kb, lv_obj_t* ta);
static void keyboard_event_cb(lv_event_t* e);

// 新增：处理背景空白区域的点击
static void bg_click_cb(lv_event_t* e) {
    lv_obj_t* target = lv_event_get_target(e);
    lv_obj_t* current_target = lv_event_get_current_target(e);
    
    // 核心判定：确保只有直接点击绑定的容器时才触发，防止点击子元素（如输入框）冒泡导致误杀
    if (target != current_target) return;
    
    TaskFormContext* ctx = lv_event_get_user_data(e);
    if (!ctx || !ctx->keyboard) return;
    
    // 隐藏键盘并清除所有输入框的焦点
    lv_obj_add_flag(ctx->keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(ctx->keyboard, NULL);
    if (ctx->title_input) lv_obj_clear_state(ctx->title_input, LV_STATE_FOCUSED);
    if (ctx->estimate_input) lv_obj_clear_state(ctx->estimate_input, LV_STATE_FOCUSED);

    epd_region_end();
}

static void task_form_confirm(lv_event_t* e) {
    TodoListApp* app = lv_event_get_user_data(e);
    if (!app) return;
    
    TaskFormContext* ctx = (TaskFormContext*)app->view->page_nav.nav_ctx;
    if (!ctx) return;
    
    // 隐藏键盘逻辑保持不变...
    if (ctx->keyboard) {
        lv_obj_add_flag(ctx->keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(ctx->keyboard, NULL);
        lv_obj_clear_state(ctx->title_input, LV_STATE_FOCUSED);
        lv_obj_clear_state(ctx->estimate_input, LV_STATE_FOCUSED);
    }

    epd_region_end();
    
    const char* title = lv_textarea_get_text(ctx->title_input);
    const char* estimate_str = lv_textarea_get_text(ctx->estimate_input);
    
    if (strlen(title) == 0) return;
    
    uint32_t estimate_s = atoi(estimate_str) * 60;
    if (estimate_s == 0) {
        estimate_s = 30 * 60;
    }
    
    // 核心差异：根据模式执行不同操作
    if (ctx->edit_task_id != 0) {
        // Edit 模式：更新现有任务
        if (!todolist_model_update_todo_task(app->model, ctx->edit_task_id, title, estimate_s)) {
            lv_toast_show("Task no longer exists", 2000);
        }
    } else {
        // Add 模式：创建新任务。列表满了要说一声，不能静默丢掉用户刚输入的内容
        if (todolist_model_add_todo_task(app->model, title, estimate_s) == 0) {
            lv_toast_show("Task list is full", 2000);
            return;
        }
    }
    
    lv_textarea_set_text(ctx->title_input, "");
    lv_textarea_set_text(ctx->estimate_input, "");
    
    // 恢复 EPD 方向为竖屏
    // epd_display_set_orientation(EPD_ORIENT_PORTRAIT);
    
    page_navigator_navigate_pop(&app->view->page_nav, app);
}

static void task_form_back(lv_event_t* e) {
    TodoListApp* app = lv_event_get_user_data(e);
    if (!app) return;
    
    TaskFormContext* ctx = (TaskFormContext*)app->view->page_nav.nav_ctx;
    if (ctx) {
        // 需求2：点击 Cancel 同样收起键盘（虽然页面也会销毁，但加双保险防止闪烁）
        if (ctx->keyboard) {
            lv_obj_add_flag(ctx->keyboard, LV_OBJ_FLAG_HIDDEN);
            lv_keyboard_set_textarea(ctx->keyboard, NULL);
        }

        epd_region_end();
        
        lv_textarea_set_text(ctx->title_input, "");
        lv_textarea_set_text(ctx->estimate_input, "");
    }
    
    // epd_display_set_orientation(EPD_ORIENT_PORTRAIT);

    page_navigator_navigate_pop(&app->view->page_nav, app);
}

/* 删除确认通过后：真正删掉并退回列表页。
 * 用 async 退页，是因为这时还在弹窗按钮的事件回调里，弹窗自己也正在被删。 */
static void task_form_delete_confirmed(void *user_data) {
    uint32_t task_id = (uint32_t)(uintptr_t)user_data;
    if (task_id == 0) return;

    todolist_model_delete_todo_task(g_todolist_app.model, task_id);
    lv_async_call(task_form_pop_async, &g_todolist_app);
}

static void task_form_delete(lv_event_t* e) {
    TodoListApp* app = lv_event_get_user_data(e);
    if (!app) return;

    TaskFormContext* ctx = (TaskFormContext*)app->view->page_nav.nav_ctx;
    if (!ctx || ctx->edit_task_id == 0) return;

    const todolist_task_t* task = todolist_model_get_todo_task(app->model, ctx->edit_task_id);
    if (!task) return;

    char msg[192];
    snprintf(msg, sizeof(msg), "Delete \"%.120s\"?", task->title);
    todolist_confirm_dialog("Confirm Delete", msg, "Delete",
                            task_form_delete_confirmed, (void *)(uintptr_t)ctx->edit_task_id);
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
    TaskFormContext* ctx = lv_event_get_user_data(e);

    if (!ctx || !ctx->keyboard) return;

    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        /* 标题用自定义键盘，时长用数字键盘 */
        if (ta == ctx->title_input)
            lv_keyboard_set_mode(ctx->keyboard, LV_KEYBOARD_MODE_USER_1);
        else
            lv_keyboard_set_mode(ctx->keyboard, LV_KEYBOARD_MODE_NUMBER);

        if (lv_obj_has_flag(ctx->keyboard, LV_OBJ_FLAG_HIDDEN)) {
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
            /* 键盘已经开着，只是换了个输入框：把窗口挪过去即可 */
            ta_bind_keyboard(ctx->keyboard, ta);
            epd_region_flush_obj(ta);
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

static lv_obj_t* build_task_form_page(struct TodoListApp* app, void* user_data) {
    uint32_t current_task_id = (uint32_t)(uintptr_t)user_data;   /* 0 = 新建 */
    const todolist_task_t* current_task = todolist_model_get_todo_task(app->model, current_task_id);
    bool is_edit_mode = (current_task != NULL);
    
    // 设置 EPD 方向为横屏
    // epd_display_set_orientation(EPD_ORIENT_LANDSCAPE);

    const char* page_title = is_edit_mode ? "Edit Task" : "Add Task";
    Page page = lv_page_create(page_title, true, task_form_back, app);
    lv_obj_t* content_cont = page.container;

    /* 不要在这里 free(nav_ctx)：page_navigator 进 builder 前已经把它置 NULL，
     * 旧上下文由它在旧 screen 的 DELETE 回调里释放。这里再 free 一次就是二次释放。 */
    TaskFormContext* ctx = malloc(sizeof(TaskFormContext));
    if (!ctx) {
        ESP_LOGE(TAG, "task form ctx alloc failed");
        return page.screen;
    }
    memset(ctx, 0, sizeof(TaskFormContext));
    ctx->edit_task_id = is_edit_mode ? current_task_id : 0;
    app->view->page_nav.nav_ctx = ctx;
    
    // 为最外层背景容器绑定点击事件
    lv_obj_add_flag(content_cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(content_cont, bg_click_cb, LV_EVENT_CLICKED, ctx);
    
    // --- 任务标题输入 ---
    lv_obj_t* title_cont = lv_obj_create(content_cont);
    lv_obj_remove_style_all(title_cont);
    lv_obj_set_width(title_cont, LV_PCT(100));
    lv_obj_set_height(title_cont, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(title_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(title_cont, 4, 0);  /* label 与输入框之间的间隙 */
    lv_obj_add_flag(title_cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(title_cont, bg_click_cb, LV_EVENT_CLICKED, ctx);
    
    lv_obj_t* title_label = lv_label_create(title_cont);
    lv_label_set_text(title_label, "Task Title");
    lv_obj_set_style_text_font(title_label, LV_FONT_SMALL, LV_PART_MAIN);
    
    ctx->title_input = lv_textarea_create(title_cont);
    lv_textarea_set_one_line(ctx->title_input, true);
    /* 不要写死高度：one_line 已是 LV_SIZE_CONTENT（恰好容纳一行）。写死高度一旦让
     * content_height < line_height，lv_textarea_scroll_to_cusor_pos() 的顶部/底部判定
     * 会互相翻转，文字在两个滚动位置之间反复跳动。 */
    lv_obj_set_width(ctx->title_input, LV_PCT(100));
    lv_obj_set_style_border_width(ctx->title_input, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(ctx->title_input, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(ctx->title_input, 2, LV_PART_MAIN);
    lv_obj_set_style_opa(ctx->title_input, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ctx->title_input, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_border_opa(ctx->title_input, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_anim_duration(ctx->title_input, 0, LV_PART_CURSOR);
    lv_obj_set_scrollbar_mode(ctx->title_input, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(ctx->title_input, ta_event_cb, LV_EVENT_FOCUSED, ctx);
    lv_obj_add_event_cb(ctx->title_input, ta_event_cb, LV_EVENT_CLICKED, ctx);

    // --- 预计时长输入 ---
    lv_obj_t* estimate_cont = lv_obj_create(content_cont);
    lv_obj_remove_style_all(estimate_cont);
    lv_obj_set_width(estimate_cont, LV_PCT(100));
    lv_obj_set_height(estimate_cont, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(estimate_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(estimate_cont, 4, 0);  /* label 与输入框之间的间隙 */
    lv_obj_set_style_margin_top(estimate_cont, 20, LV_PART_MAIN);
    lv_obj_add_flag(estimate_cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(estimate_cont, bg_click_cb, LV_EVENT_CLICKED, ctx);
    
    lv_obj_t* estimate_label = lv_label_create(estimate_cont);
    lv_label_set_text(estimate_label, "Estimate Time (minutes)");
    lv_obj_set_style_text_font(estimate_label, LV_FONT_SMALL, LV_PART_MAIN);
    
    ctx->estimate_input = lv_textarea_create(estimate_cont);
    lv_textarea_set_one_line(ctx->estimate_input, true);
    /* 不要写死高度：one_line 已是 LV_SIZE_CONTENT（恰好容纳一行）。写死高度一旦让
     * content_height < line_height，lv_textarea_scroll_to_cusor_pos() 的顶部/底部判定
     * 会互相翻转，文字在两个滚动位置之间反复跳动。 */
    lv_obj_set_width(ctx->estimate_input, LV_PCT(100));
    lv_obj_set_style_border_width(ctx->estimate_input, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(ctx->estimate_input, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(ctx->estimate_input, 2, LV_PART_MAIN);
    lv_obj_set_style_opa(ctx->estimate_input, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ctx->estimate_input, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_border_opa(ctx->estimate_input, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_anim_duration(ctx->estimate_input, 0, LV_PART_CURSOR);
    lv_obj_set_scrollbar_mode(ctx->estimate_input, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(ctx->estimate_input, ta_event_cb, LV_EVENT_FOCUSED, ctx);
    lv_obj_add_event_cb(ctx->estimate_input, ta_event_cb, LV_EVENT_CLICKED, ctx);

    // 2. 数据回填 (Data Pre-fill)
    if (is_edit_mode) {
        // 假设你的 task 结构体有 title 和 estimate_min 字段
        lv_textarea_set_text(ctx->title_input, current_task->title);
        
        char est_str[16];
        snprintf(est_str, sizeof(est_str), "%lu", (unsigned long)(current_task->estimate_s / 60));
        lv_textarea_set_text(ctx->estimate_input, est_str);
    } else {
        todolist_config_t *config = todolist_model_config_get();
        char est_str[16];
        snprintf(est_str, sizeof(est_str), "%lu", (unsigned long)config->task_duration_min);
        lv_textarea_set_text(ctx->estimate_input, est_str); // Add 模式默认值
    }

    // --- 按钮容器 ---
    lv_obj_t* button_cont = lv_obj_create(content_cont);
    lv_obj_remove_style_all(button_cont);
    lv_obj_set_width(button_cont, LV_PCT(100));
    lv_obj_set_height(button_cont, LV_SIZE_CONTENT);
    lv_obj_set_style_margin_top(button_cont, 20, LV_PART_MAIN);
    lv_obj_set_flex_flow(button_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button_cont, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(button_cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button_cont, bg_click_cb, LV_EVENT_CLICKED, ctx);
    
    lv_obj_t* cancel_btn = lv_btn_create(button_cont);
    ui_style_set_btn_secondary(cancel_btn);
    lv_obj_set_size(cancel_btn, 180, 60);
    lv_obj_add_event_cb(cancel_btn, task_form_back, LV_EVENT_CLICKED, app);
    
    lv_obj_t* cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);
    
    lv_obj_t* confirm_btn = lv_btn_create(button_cont);
    ui_style_set_btn_primary(confirm_btn);
    lv_obj_set_size(confirm_btn, 180, 60);
    lv_obj_add_event_cb(confirm_btn, task_form_confirm, LV_EVENT_CLICKED, app);
    
    lv_obj_t* confirm_label = lv_label_create(confirm_btn);
    lv_label_set_text(confirm_label, "Confirm");
    lv_obj_center(confirm_label);

    /* 编辑模式下给一个明确的删除入口。
     * 左滑删除仍然保留（有二次确认），但盲滑在墨水屏上既不可发现也不好瞄准，
     * 不该是删除任务的唯一途径。 */
    if (is_edit_mode) {
        lv_obj_t* delete_btn = lv_btn_create(content_cont);
        ui_style_set_btn_secondary(delete_btn);
        lv_obj_set_width(delete_btn, LV_PCT(100));
        lv_obj_set_height(delete_btn, 60);
        lv_obj_set_style_margin_top(delete_btn, 16, LV_PART_MAIN);
        lv_obj_add_event_cb(delete_btn, task_form_delete, LV_EVENT_CLICKED, app);

        lv_obj_t* delete_label = lv_label_create(delete_btn);
        lv_label_set_text(delete_label, LV_SYMBOL_TRASH "  Delete Task");
        lv_obj_center(delete_label);
    }


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

void todolist_view_task_form_init_registry(struct TodoListApp* app) {
    PAGE_REGISTE(app, PAGE_TASK_FORM, build_task_form_page);
}
