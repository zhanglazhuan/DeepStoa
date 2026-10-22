#include "controller.h"

#include "app.h"
#include "model.h"
#include "view.h"

static void render_after_event(void *user_data)
{
    tovo_view_render(user_data);
}

void tovo_controller_refresh(lv_event_t *event)
{
    (void)event;
    TovoApp *app = &g_tovo_app;
    if (!app->model || !app->view) return;
    tovo_model_reload(app->model);
    tovo_view_render(app);
}

void tovo_controller_toggle(size_t index, void *user_data)
{
    TovoApp *app = user_data;
    if (!app || !app->model || !app->view) return;
    tovo_model_toggle(app->model, index);
    lv_async_call(render_after_event, app);
}
