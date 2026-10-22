#include "app.h"

#include <string.h>
#include "app_manager.h"
#include "esp_log.h"
#include "controller.h"
#include "model.h"
#include "view.h"
#include "ui_utils.h"

static const char *TAG = "pomodoro";
EPOS_LV_IMG_DECLARE(app_pomodoro_logo);
PomodoroApp g_pomodoro_app;

static void start(lv_obj_t *root, lv_group_t *group)
{
    (void)group;
    PomodoroApp *app = &g_pomodoro_app;
    if (!pomodoro_model_init(&app->model) ||
        !pomodoro_controller_init(app) ||
        !pomodoro_view_init(app)) {
        ESP_LOGE(TAG, "cannot start Pomodoro");
        pomodoro_controller_deinit(app);
        pomodoro_view_deinit(app);
        pomodoro_model_deinit(app->model);
        memset(app, 0, sizeof(*app));
        if (root) {
            lv_obj_t *label = lv_label_create(root);
            lv_label_set_text(label, "Unable to open Pomodoro");
            lv_obj_center(label);
        }
        return;
    }
    page_navigator_navigate_to(&app->view->page_nav, app, 1, NULL);
}

static void stop(void)
{
    PomodoroApp *app = &g_pomodoro_app;
    pomodoro_controller_deinit(app);
    pomodoro_view_deinit(app);
    pomodoro_model_deinit(app->model);
    memset(app, 0, sizeof(*app));
}

static bool back(void)
{
    return g_pomodoro_app.view &&
        page_navigator_navigate_pop(&g_pomodoro_app.view->page_nav,
                                     &g_pomodoro_app);
}

static application_t s_application = {
    .name = "Pomodoro",
    .icon = EPOS_LV_IMG_USE(app_pomodoro_logo),
    .start_func = start,
    .stop_func = stop,
    .back_func = back,
    .category = APP_CATEGORY_TOOLS,
};

void pomodoro_init(void)
{
    app_manager_add_application(&s_application);
    ESP_LOGI(TAG, "Pomodoro app registered");
}
