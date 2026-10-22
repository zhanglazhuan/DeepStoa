#include "controller.h"

#include <stdint.h>

#include "app.h"
#include "model.h"
#include "view.h"

void mistakebook_controller_select(lv_event_t *event)
{
    MistakebookApp *app = &g_mistakebook_app;
    uintptr_t encoded = (uintptr_t)lv_event_get_user_data(event);
    if (!app->model || !app->view || encoded == 0 ||
        encoded > app->model->count) return;
    app->model->selected = (uint32_t)(encoded - 1);
    PAGE_NAVIGATE_TO(app, MISTAKEBOOK_PAGE_DETAIL, NULL);
}

void mistakebook_controller_reload(lv_event_t *event)
{
    (void)event;
    MistakebookApp *app = &g_mistakebook_app;
    if (!app->model || !app->view) return;
    mistakebook_model_reload(app->model);
    mistakebook_view_refresh_list(app);
}

void mistakebook_controller_previous(lv_event_t *event)
{
    (void)event;
    MistakebookApp *app = &g_mistakebook_app;
    if (!app->model || !app->model->count) return;
    app->model->selected = app->model->selected == 0
        ? app->model->count - 1 : app->model->selected - 1;
    mistakebook_view_refresh_detail(app);
}

void mistakebook_controller_next(lv_event_t *event)
{
    (void)event;
    MistakebookApp *app = &g_mistakebook_app;
    if (!app->model || !app->model->count) return;
    app->model->selected = (app->model->selected + 1) % app->model->count;
    mistakebook_view_refresh_detail(app);
}

void mistakebook_controller_mark_reviewed(lv_event_t *event)
{
    (void)event;
    MistakebookApp *app = &g_mistakebook_app;
    if (!app->model) return;
    if (mistakebook_model_mark_reviewed(app->model)) {
        mistakebook_view_refresh_detail(app);
    } else if (app->view && app->view->detail_status) {
        lv_label_set_text(app->view->detail_status,
                          app->model->read_only ? "Source has unsupported or extra rows; review changes disabled"
                                                : "Could not save review status");
    }
}
