// apps/calendar/view.c
// Calendar view — lifecycle and page navigator setup

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include <lvgl.h>

#include "view.h"
#include "model.h"
#include "controller.h"
#include "subpages/view_month.h"

static const char *TAG = "calendar_view";

void calendar_view_init(CalendarApp *app)
{
    app->view = malloc(sizeof(CalendarView));
    if (!app->view) {
        ESP_LOGE(TAG, "Failed to alloc CalendarView");
        return;
    }
    memset(app->view, 0, sizeof(CalendarView));

    page_navigator_page_t *registry =
        malloc(sizeof(page_navigator_page_t) * CALENDAR_PAGE_ID_MAX);
    if (!registry) {
        ESP_LOGE(TAG, "Failed to alloc page registry");
        free(app->view);
        app->view = NULL;
        return;
    }
    memset(registry, 0, sizeof(page_navigator_page_t) * CALENDAR_PAGE_ID_MAX);
    page_navigator_init(&app->view->page_nav, registry,
                        CALENDAR_PAGE_ID_MAX, app);

    calendar_view_month_init_registry(app);
}

void calendar_view_deinit(CalendarApp *app)
{
    if (!app->view) return;

    page_navigator_page_t *registry = app->view->page_nav.registry;
    page_navigator_deinit(&app->view->page_nav);
    free(registry);
    free(app->view);
    app->view = NULL;
}

void calendar_view_refresh_month(CalendarApp *app)
{
    calendar_view_month_rebuild(app);
}
