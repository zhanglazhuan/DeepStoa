#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"

#include "controller.h"
#include "view.h"
#include "model.h"
#include "lv_tab.h"
#include "lv_toast.h"
#include "lv_epd_region.h"
#include "app.h"
#include "utils.h"
#include "storage.h"

#include "./modules/controller_timer.h"
#include "./modules/view_todo.h"
#include "./modules/view_timer.h"
#include "./modules/view_done.h"

// 全局应用实例
extern TodoListApp g_todolist_app;

static const char *TAG = "todolist_controller";

void todolist_controller_init(TodoListApp* app) {
    app->controller = (TodoListController*)malloc(sizeof(TodoListController));
    if (!app->controller) {
        ESP_LOGE(TAG, "controller alloc failed");
        return;
    }
    memset(app->controller, 0, sizeof(TodoListController));

    // 动态分配 Timer 相关的上下文
    app->controller->timer_ctx = (TodoListTimerController*)malloc(sizeof(TodoListTimerController));
    if (!app->controller->timer_ctx) {
        ESP_LOGE(TAG, "timer controller alloc failed");
        free(app->controller);
        app->controller = NULL;
        return;
    }
    memset(app->controller->timer_ctx, 0, sizeof(TodoListTimerController)); // 归零初始化

    controller_timer_init(app->controller); // 初始化 Timer 模块
}

void todolist_controller_deinit(TodoListApp* app) {
    if (app->controller != NULL) {
        controller_timer_deinit(app->controller); // 释放 Timer 模块

        free(app->controller);
        app->controller = NULL;
    }
    
}

void todolist_controller_active_tab(uint16_t tab_idx, bool with_anim) {
    TodoListView *v = g_todolist_app.view;
    TodoListViewHomeCtx *home_ctx = (TodoListViewHomeCtx*)v->page_nav.nav_ctx;

    if (with_anim) {
        lv_tab_set_active(home_ctx->tab, tab_idx);
    }

    /* 离开 Timer 页：停掉轮询，并退出局部刷新模式 —— 窗口是钉在倒计时那块矩形上的，
     * 不解除的话新 tab 的内容根本推不到面板上。 */
    controller_timer_pause_polling(g_todolist_app.controller);
    if (epd_region_is_active()) epd_region_end();

    /* 内嵌存储，置空即可（见 view.h 里 timer_ctx_storage 的说明） */
    home_ctx->timer_ctx = NULL;

    // 核心优化：在渲染新内容前，清空所有 tab 容器以释放内存
    lv_obj_clean(home_ctx->tab_todo);
    lv_obj_clean(home_ctx->tab_timer);
    lv_obj_clean(home_ctx->tab_done);

    // 根据当前激活的 tab 重新构建对应的内容
    switch (tab_idx) {
        case 0:
            view_build_todo_tab(v);
            break;
        case 1:
            view_build_timer_tab(v);
            break;
        case 2:
            view_build_done_tab(v);
            break;
        default:
            break;
    }
}

void todolist_controller_on_tab_changed(lv_tab_t *tab, uint32_t idx, void *user_data)
{
    TodoListApp* app = (TodoListApp*)user_data;

    TodoListViewHomeCtx *home_ctx = (TodoListViewHomeCtx*)app->view->page_nav.nav_ctx;
    if (!home_ctx || !home_ctx->tab) return;

    ESP_LOGI(TAG, "todolist_controller_on_tab_changed: %lu", (unsigned long)idx);
    todolist_controller_active_tab((uint16_t)idx, false);
}

void todolist_controller_open_settings(void) {
    TodoListApp* app = &g_todolist_app;
    PAGE_NAVIGATE_TO(app, PAGE_SETTINGS, NULL);
}

static void clear_done_confirmed(void *user_data) {
    (void)user_data;
    todolist_model_clear_all_done_tasks(g_todolist_app.model);

    if (g_todolist_app.view->page_nav.current_page == PAGE_HOME) {
        if (g_todolist_app.view->page_nav.nav_ctx) {
            TodoListViewHomeCtx *home_ctx = (TodoListViewHomeCtx*)g_todolist_app.view->page_nav.nav_ctx;
            if (home_ctx->tab && lv_tab_get_active(home_ctx->tab) == 2) {
                view_refresh_done_list();
            }
        }
    }
    ESP_LOGI(TAG, "done list cleared");
}

/* 清空已完成是不可撤销的批量删除，必须和单条删除一样给二次确认 */
void settings_clear_done_cb(lv_event_t * e) {
    (void)e;
    uint8_t cnt = g_todolist_app.model ? g_todolist_app.model->done_task_count : 0;
    if (cnt == 0) {
        lv_toast_show("Done list is already empty", 1500);
        return;
    }

    char msg[96];
    snprintf(msg, sizeof(msg), "Delete all %u finished tasks?\nThis cannot be undone.", cnt);
    todolist_confirm_dialog("Clear Done List", msg, "Clear", clear_done_confirmed, NULL);
}