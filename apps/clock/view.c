// Clock view: navigator lifecycle and page registration.

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "pages/view_alarm_edit.h"
#include "pages/view_main.h"
#include "pages/view_alarm.h"
#include "pages/view_timer_preset.h"
#include "view.h"

static const char *TAG = "clock_view";

void clock_view_init(ClockApp *app)
{
    app->view = malloc(sizeof(ClockView));
    if (!app->view) {
        ESP_LOGE(TAG, "Failed to alloc ClockView");
        return;
    }
    memset(app->view, 0, sizeof(ClockView));

    page_navigator_page_t *registry = malloc(sizeof(page_navigator_page_t) * CLOCK_PAGE_ID_MAX);
    memset(registry, 0, sizeof(page_navigator_page_t) * CLOCK_PAGE_ID_MAX);
    page_navigator_init(&app->view->page_nav, registry, CLOCK_PAGE_ID_MAX, app);

    clock_view_main_init_registry(app);
    clock_view_alarm_edit_init_registry(app);
    clock_view_timer_preset_init_registry(app);
}

void clock_view_deinit(ClockApp *app)
{
    if (app->view) {
        page_navigator_deinit(&app->view->page_nav);
        free(app->view->page_nav.registry);
        free(app->view);
        app->view = NULL;
    }
}

/* ── Alarm list refresh ──────────────────────────────────────── */

void clock_view_refresh_alarm_list(ClockApp *app)
{
    clock_view_alarm_rebuild_list(app);
}
