#include "view.h"

#include <stdlib.h>
#include "app.h"
#include "controller.h"
#include "model.h"
#include "widgets/lv_page.h"

enum { POMODORO_PAGE_MAIN = 1 };

void pomodoro_view_render(PomodoroApp *app)
{
    if (!app || !app->view || !app->model || !app->view->panel.root) return;
    const char *status;
    const char *action;
    switch (app->model->state) {
    case POMODORO_RUNNING: status = "Running"; action = "Pause"; break;
    case POMODORO_PAUSED: status = "Paused"; action = "Resume"; break;
    case POMODORO_FINISHED: status = "Complete — take a break"; action = "Start"; break;
    default: status = "Ready"; action = "Start"; break;
    }
    lv_pomodoro_panel_render(&app->view->panel, app->model->remaining_s,
                              status, action);
}

static lv_obj_t *build_main_page(void *handle, void *user_data)
{
    (void)user_data;
    PomodoroApp *app = handle;
    Page page = lv_page_create("Pomodoro", false, NULL, NULL);
    lv_obj_t *close = lv_button_create(page.header_right);
    lv_obj_set_size(close, 40, 40);
    lv_obj_add_event_cb(close, pomodoro_controller_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(close);
    lv_label_set_text(label, "X");
    lv_obj_center(label);
    lv_pomodoro_panel_create(page.container, &app->view->panel,
                              pomodoro_controller_toggle, pomodoro_controller_reset);
    pomodoro_view_render(app);
    return page.screen;
}

bool pomodoro_view_init(PomodoroApp *app)
{
    if (!app) return false;
    app->view = calloc(1, sizeof(*app->view));
    if (!app->view) return false;
    page_navigator_page_t *registry = calloc(1, sizeof(*registry));
    if (!registry) { free(app->view); app->view = NULL; return false; }
    page_navigator_init(&app->view->page_nav, registry, 1, app);
    page_navigator_register_page(&app->view->page_nav,
                                  POMODORO_PAGE_MAIN, build_main_page);
    return true;
}

void pomodoro_view_deinit(PomodoroApp *app)
{
    if (!app || !app->view) return;
    free(app->view->page_nav.registry);
    page_navigator_deinit(&app->view->page_nav);
    free(app->view);
    app->view = NULL;
}
