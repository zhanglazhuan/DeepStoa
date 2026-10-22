#include <string.h>

#include "app_manager.h"
#include "esp_log.h"
#include "app.h"
#include "model.h"
#include "view.h"
#include "ui_utils.h"

static const char *TAG = "mistakebook";
EPOS_LV_IMG_DECLARE(app_misktakebook_logo);
MistakebookApp g_mistakebook_app;

static void mistakebook_app_start(lv_obj_t *root, lv_group_t *group)
{
    (void)group;
    if (!mistakebook_model_init(&g_mistakebook_app.model) ||
        !mistakebook_view_init(&g_mistakebook_app)) {
        ESP_LOGE(TAG, "out of memory while starting");
        mistakebook_view_deinit(&g_mistakebook_app);
        mistakebook_model_deinit(g_mistakebook_app.model);
        g_mistakebook_app.model = NULL;
        if (root) {
            lv_obj_t *label = lv_label_create(root);
            lv_label_set_text(label, "Unable to open Mistakebook");
            lv_obj_center(label);
        }
        return;
    }
    page_navigator_navigate_to(&g_mistakebook_app.view->page_nav,
                               &g_mistakebook_app, MISTAKEBOOK_PAGE_LIST, NULL);
}

static void mistakebook_app_stop(void)
{
    mistakebook_view_deinit(&g_mistakebook_app);
    mistakebook_model_deinit(g_mistakebook_app.model);
    memset(&g_mistakebook_app, 0, sizeof(g_mistakebook_app));
}

static bool mistakebook_app_back(void)
{
    if (!g_mistakebook_app.view) return false;
    return page_navigator_navigate_pop(&g_mistakebook_app.view->page_nav,
                                       &g_mistakebook_app);
}

static application_t s_application = {
    .name = "Mistakebook",
    .icon = EPOS_LV_IMG_USE(app_misktakebook_logo),
    .start_func = mistakebook_app_start,
    .stop_func = mistakebook_app_stop,
    .back_func = mistakebook_app_back,
    .category = APP_CATEGORY_TOOLS,
};

void mistakebook_init(void)
{
    app_manager_add_application(&s_application);
    ESP_LOGI(TAG, "Mistakebook app registered");
}
