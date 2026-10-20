#include "controller.h"

#include <stdlib.h>
#include "app.h"
#include "app_manager.h"
#include "model.h"
#include "view.h"
#include "pomodoro_service.h"

static void sync_view(PomodoroApp *app)
{
    if (app && app->model && app->view && pomodoro_model_sync(app->model))
        pomodoro_view_render(app);
}

static void poll(lv_timer_t *timer)
{
    sync_view(lv_timer_get_user_data(timer));
}

bool pomodoro_controller_init(PomodoroApp *app)
{
    if (!app) return false;
    app->controller = calloc(1, sizeof(*app->controller));
    if (!app->controller) return false;
    app->controller->poll_timer = lv_timer_create(poll, 500, app);
    if (!app->controller->poll_timer) {
        free(app->controller);
        app->controller = NULL;
        return false;
    }
    return true;
}

void pomodoro_controller_deinit(PomodoroApp *app)
{
    if (!app || !app->controller) return;
    if (app->controller->poll_timer) lv_timer_delete(app->controller->poll_timer);
    free(app->controller);
    app->controller = NULL;
}

void pomodoro_controller_toggle(lv_event_t *event)
{
    (void)event;
    pomodoro_service_toggle();
    sync_view(&g_pomodoro_app);
}

void pomodoro_controller_reset(lv_event_t *event)
{
    (void)event;
    pomodoro_service_reset();
    sync_view(&g_pomodoro_app);
}

void pomodoro_controller_close(lv_event_t *event)
{
    (void)event;
    app_manager_exit_app();
}
