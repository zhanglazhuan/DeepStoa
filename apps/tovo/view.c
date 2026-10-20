#include "view.h"

#include <stdio.h>
#include <stdlib.h>

#include "app.h"
#include "controller.h"
#include "model.h"
#include "widgets/lv_page.h"

enum { TOVO_PAGE_MAIN = 1 };

void tovo_view_render(TovoApp *app)
{
    if (!app || !app->view || !app->model || !app->view->panel.root) return;
    TovoModel *model = app->model;
    lv_todo_row_t rows[TOVO_MAX];
    for (size_t i = 0; i < model->count; ++i) {
        rows[i].content = model->items[i].content;
        rows[i].completed = model->items[i].completed;
    }
    char status[96];
    if (!model->mounted) snprintf(status, sizeof(status), "Storage is not mounted");
    else if (model->load_error) snprintf(status, sizeof(status), "Could not read todo CSV");
    else if (model->read_only) snprintf(status, sizeof(status), "CSV has extra or invalid rows; editing disabled");
    else if (model->save_error) snprintf(status, sizeof(status), "Could not save task; change reverted");
    else if (!model->count) snprintf(status, sizeof(status), "No legacy todo CSV found");
    else snprintf(status, sizeof(status), "%u task(s), click to complete", (unsigned)model->count);
    lv_todo_list_panel_render(&app->view->panel, rows, model->count,
                              status, tovo_controller_toggle, app);
}

static lv_obj_t *build_main_page(void *handle, void *user_data)
{
    (void)user_data;
    TovoApp *app = handle;
    Page page = lv_page_create("Tovo", false, NULL, NULL);
    lv_obj_t *close = lv_button_create(page.header_right);
    lv_obj_set_size(close, 40, 40);
    lv_obj_add_event_cb(close, tovo_controller_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(close);
    lv_label_set_text(label, "X");
    lv_obj_center(label);
    lv_todo_list_panel_create(page.container, &app->view->panel,
                               tovo_controller_refresh, NULL);
    tovo_view_render(app);
    return page.screen;
}

bool tovo_view_init(TovoApp *app)
{
    if (!app) return false;
    app->view = calloc(1, sizeof(*app->view));
    if (!app->view) return false;
    page_navigator_page_t *registry = calloc(1, sizeof(*registry));
    if (!registry) { free(app->view); app->view = NULL; return false; }
    page_navigator_init(&app->view->page_nav, registry, 1, app);
    page_navigator_register_page(&app->view->page_nav,
                                  TOVO_PAGE_MAIN, build_main_page);
    return true;
}

void tovo_view_deinit(TovoApp *app)
{
    if (!app || !app->view) return;
    free(app->view->page_nav.registry);
    page_navigator_deinit(&app->view->page_nav);
    free(app->view);
    app->view = NULL;
}
