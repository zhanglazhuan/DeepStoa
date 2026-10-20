#include "app.h"

#include <string.h>
#include "app_manager.h"
#include "esp_log.h"
#include "model.h"
#include "view.h"

static const char *TAG = "tovo_app";
TovoApp g_tovo_app;

static void start(lv_obj_t *root, lv_group_t *group)
{
    (void)group;
    TovoApp *app = &g_tovo_app;
    if (!tovo_model_init(&app->model) || !tovo_view_init(app)) {
        ESP_LOGE(TAG, "cannot start TodoList");
        tovo_view_deinit(app);
        tovo_model_deinit(app->model);
        memset(app, 0, sizeof(*app));
        if (root) {
            lv_obj_t *label = lv_label_create(root);
            lv_label_set_text(label, "Unable to open TodoList");
            lv_obj_center(label);
        }
        return;
    }
    page_navigator_navigate_to(&app->view->page_nav, app, 1, NULL);
}

static void stop(void)
{
    TovoApp *app = &g_tovo_app;
    tovo_view_deinit(app);
    tovo_model_deinit(app->model);
    memset(app, 0, sizeof(*app));
}

static bool back(void)
{
    return g_tovo_app.view &&
        page_navigator_navigate_pop(&g_tovo_app.view->page_nav,
                                     &g_tovo_app);
}

static application_t s_application = {
    .name = "Tovo",
    .icon = NULL,
    .start_func = start,
    .stop_func = stop,
    .back_func = back,
    .category = APP_CATEGORY_SYSTEM,
};

void tovo_init(void)
{
    app_manager_add_application(&s_application);
    ESP_LOGI(TAG, "Tovo app registered");
}
