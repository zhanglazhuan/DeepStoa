#include "view.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "app.h"
#include "controller.h"
#include "model.h"
#include "lv_floating_button.h"
#include "widgets/lv_page.h"

LV_IMAGE_DECLARE(refresh);

enum { TOVO_PAGE_MAIN = 1 };

static void row_clicked(lv_event_t *event)
{
    lv_obj_t *row = lv_event_get_current_target(event);
    TovoApp *app = lv_obj_get_user_data(row);
    tovo_controller_toggle((size_t)(uintptr_t)lv_event_get_user_data(event), app);
}

void tovo_view_render(TovoApp *app)
{
    if (!app || !app->view || !app->model || !app->view->root) return;
    TovoView *view = app->view;
    TovoModel *model = app->model;
    char status[96];
    if (!model->mounted) snprintf(status, sizeof(status), "Storage is not mounted");
    else if (model->load_error) snprintf(status, sizeof(status), "Could not read todo CSV");
    else if (model->read_only) snprintf(status, sizeof(status), "CSV has extra or invalid rows; editing disabled");
    else if (model->save_error) snprintf(status, sizeof(status), "Could not save task; change reverted");
    else if (!model->count) snprintf(status, sizeof(status), "No legacy todo CSV found");
    else snprintf(status, sizeof(status), "%u task(s), click to complete", (unsigned)model->count);
    lv_label_set_text(view->status, status);
    lv_obj_clean(view->list);
    for (size_t i = 0; i < model->count; ++i) {
        lv_obj_t *row = lv_button_create(view->list);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 48);
        lv_obj_set_user_data(row, app);
        lv_obj_add_event_cb(row, row_clicked, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
        lv_obj_t *label = lv_label_create(row);
        lv_obj_set_width(label, LV_PCT(96));
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_label_set_text_fmt(label, "%s%s",
                              model->items[i].completed ? "[x] " : "[ ] ",
                              model->items[i].content);
        lv_obj_center(label);
    }
}

static lv_obj_t *build_main_page(void *handle, void *user_data)
{
    (void)user_data;
    TovoApp *app = handle;
    Page page = lv_page_create("Tovo", false, NULL, NULL);
    TovoView *view = app->view;
    view->root = lv_obj_create(page.container);
    lv_obj_set_width(view->root, LV_PCT(100));
    lv_obj_set_flex_grow(view->root, 1);
    lv_obj_set_flex_flow(view->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(view->root, 8, 0);

    view->status = lv_label_create(view->root);
    lv_obj_set_width(view->status, LV_PCT(100));
    lv_label_set_long_mode(view->status, LV_LABEL_LONG_WRAP);
    view->list = lv_obj_create(view->root);
    lv_obj_set_width(view->list, LV_PCT(100));
    lv_obj_set_flex_grow(view->list, 1);
    lv_obj_set_flex_flow(view->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(view->list, 4, 0);
    lv_obj_set_style_pad_bottom(view->list,
                                LV_FLOATING_BUTTON_SIZE + LV_FLOATING_BUTTON_MARGIN, 0);

    lv_floating_button_create_image(view->root, &refresh,
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
