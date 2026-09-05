#include <stdlib.h>
#include <string.h>
#include <lvgl.h>
#include "esp_log.h"

#include "app.h"
#include "view.h"

static const char *TAG = "player_view";

// 外部声明页面注册函数 (通常在子页面的头文件声明，如 view_player_list.h)
extern void player_view_list_init_registry(struct PlayerApp* app);
extern void player_view_play_init_registry(struct PlayerApp* app);

void player_view_init(struct PlayerApp *app) {
    app->view = malloc(sizeof(PlayerView));
    if (!app->view) {
        ESP_LOGE(TAG, "Failed to allocate memory for PlayerView");
        return;
    }

    memset(app->view, 0, sizeof(PlayerView));
    ESP_LOGI(TAG, "player_view_init");

    // 初始化页面导航器
    page_navigator_page_t *registry = malloc(sizeof(page_navigator_page_t) * PAGE_PLAYER_ID_MAX);
    memset(registry, 0, sizeof(page_navigator_page_t) * PAGE_PLAYER_ID_MAX);
    page_navigator_init(&app->view->page_nav, registry, PAGE_PLAYER_ID_MAX, app);

    // 注册子页面
    player_view_list_init_registry(app);
    player_view_play_init_registry(app);
}

void player_view_deinit(struct PlayerApp *app) {
    ESP_LOGI(TAG, "player_view_deinit");

    if (app->view) {
        page_navigator_deinit(&app->view->page_nav);
        free(app->view->page_nav.registry);
        free(app->view);
        app->view = NULL;
    }
}
