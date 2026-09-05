#include "esp_log.h"
#include <lvgl.h>
#include <string.h>
#include <stdlib.h>

#include "app.h"
#include "view.h"
#include "subpages/view_main.h"
#include "subpages/view_history.h"

static const char *TAG = "chatbot_view";

void chatbot_view_init(struct ChatbotApp *app) {
    app->view = malloc(sizeof(ChatbotView));
    if (!app->view) {
        ESP_LOGE(TAG, "Failed to allocate memory for ChatbotView");
        return;
    }

    memset(app->view, 0, sizeof(ChatbotView));

    ESP_LOGI(TAG, "chatbot_view_init");

    page_navigator_page_t *page_builders = malloc(sizeof(page_navigator_page_t) * CHATBOT_PAGE_ID_MAX);
    memset(page_builders, 0, sizeof(page_navigator_page_t) * CHATBOT_PAGE_ID_MAX);
    page_navigator_init(&app->view->page_nav, page_builders, CHATBOT_PAGE_ID_MAX, app);

    chatbot_view_main_init_registry(app);
    chatbot_view_history_init_registry(app);

    app->view->chat_container = NULL;
    app->view->btn_record = NULL;
    app->view->lbl_record = NULL;
}

void chatbot_view_deinit(struct ChatbotApp *app) {
    ESP_LOGI(TAG, "chatbot_view_deinit");

    page_navigator_deinit(&app->view->page_nav);

    app->view->chat_container = NULL;
    app->view->btn_record = NULL;
    app->view->lbl_record = NULL;

    if (app->view != NULL) {
        free(app->view->page_nav.registry);
        free(app->view);
        app->view = NULL;
    }
}
