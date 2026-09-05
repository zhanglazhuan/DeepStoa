#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"

#include "lvgl.h"

#include "view.h"
#include "controller.h"
#include "app.h"

#include "./modules/view_timer.h"
#include "./subpages/view_task_form.h"
#include "./subpages/view_settings.h"
#include "./subpages/view_home.h"
#include "./subpages/view_about.h"
#include "./subpages/view_launch.h"

// 全局应用实例
extern TodoListApp g_todolist_app;

static const char *TAG = "todolist_view";

void todolist_view_init(TodoListApp* app) {
    app->view = (TodoListView*)malloc(sizeof(TodoListView));
    if (!app->view) {
        ESP_LOGE(TAG, "View memory allocation failed");
        return;
    }

    page_navigator_page_t *registry = malloc(sizeof(page_navigator_page_t) * TODOLIST_PAGE_ID_MAX);
    memset(registry, 0, sizeof(page_navigator_page_t) * TODOLIST_PAGE_ID_MAX);
    page_navigator_init(&app->view->page_nav, registry, TODOLIST_PAGE_ID_MAX, app);

    todolist_view_timer_init(app->view);
    
    // 注册子页面
    todolist_view_launch_init_registry(app);
    todolist_view_home_init_registry(app);
    todolist_view_task_form_init_registry(app);
    todolist_view_settings_init_registry(app);
    todolist_view_about_init_registry(app);
}

void todolist_view_deinit(TodoListApp* app) {
    if (app->view != NULL) {
        /* 当前页的上下文是 builder malloc 出来的，navigator 只在「离开某一页」时
         * 释放上一页的那份 —— 最后停留的这一页没人释放。退出 App 时补上。
         * 现在页面的 DELETE 回调已经不再持有 nav_ctx，所以这里 free 是安全的。 */
        if (app->view->page_nav.nav_ctx) {
            free(app->view->page_nav.nav_ctx);
            app->view->page_nav.nav_ctx = NULL;
        }
        page_navigator_deinit(&app->view->page_nav);
        free(app->view->page_nav.registry);
        free(app->view);
        app->view = NULL;
    }
}
