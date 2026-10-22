#include "view.h"

#include <stdlib.h>
#include "app.h"
#include "controller.h"
#include "model.h"
#include "theme/lv_theme_hardcore.h"
#include "widgets/lv_page.h"

enum { POMODORO_PAGE_MAIN = 1 };

static lv_obj_t *button(lv_obj_t *parent, const char *text, lv_event_cb_t callback)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_set_size(obj, 150, 54);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(obj);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return label;
}

void pomodoro_view_render(PomodoroApp *app)
{
    if (!app || !app->view || !app->model || !app->view->root) return;
    const char *status;
    const char *action;
    switch (app->model->state) {
    case POMODORO_RUNNING: status = "Running"; action = "Pause"; break;
    case POMODORO_PAUSED: status = "Paused"; action = "Resume"; break;
    case POMODORO_FINISHED: status = "Complete — take a break"; action = "Start"; break;
    default: status = "Ready"; action = "Start"; break;
    }
    lv_label_set_text_fmt(app->view->time, "%02u:%02u",
                          (unsigned)(app->model->remaining_s / 60),
                          (unsigned)(app->model->remaining_s % 60));
    lv_label_set_text(app->view->state, status);
    lv_label_set_text(app->view->toggle_label, action);
    lv_obj_center(app->view->toggle_label);
}

static lv_obj_t *build_main_page(void *handle, void *user_data)
{
    (void)user_data;
    PomodoroApp *app = handle;
    Page page = lv_page_create("Pomodoro", false, NULL, NULL);
    PomodoroView *view = app->view;
    view->root = lv_obj_create(page.container);
    lv_obj_set_width(view->root, LV_PCT(100));
    lv_obj_set_flex_grow(view->root, 1);
    lv_obj_set_flex_flow(view->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(view->root, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(view->root, 24, 0);

    view->time = lv_label_create(view->root);
    lv_obj_set_style_text_font(view->time, LV_FONT_LARGE, 0);
    view->state = lv_label_create(view->root);
    lv_obj_set_style_text_font(view->state, LV_FONT_NORMAL, 0);

    lv_obj_t *actions = lv_obj_create(view->root);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, 72);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(actions, 20, 0);
    view->toggle_label = button(actions, "Start", pomodoro_controller_toggle);
    button(actions, "Reset", pomodoro_controller_reset);
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
