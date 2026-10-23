#include <string.h>
#include <time.h>
#include <lvgl.h>
#include "esp_log.h"
#include "lv_num_input.h"
#include "lv_page.h"
#include "page_navigator.h"
#include "settings_app.h"
#include "settings_view.h"
#include "settings_controller.h"
#include "time_service.h"
#include "view_manual_datetime.h"

static const char *TAG = "settings_manual_time";

typedef struct {
    lv_obj_t *year;
    lv_obj_t *month;
    lv_obj_t *day;
    lv_obj_t *hour;
    lv_obj_t *minute;
    lv_timer_t *save_timer;
    bool dirty;
} manual_time_ctx_t;

static void save_pending(manual_time_ctx_t *ctx)
{
    if (!ctx->dirty) return;
    if (!settings_controller_set_manual_datetime(
            lv_num_input_get_value(ctx->year),
            lv_num_input_get_value(ctx->month),
            lv_num_input_get_value(ctx->day),
            lv_num_input_get_value(ctx->hour),
            lv_num_input_get_value(ctx->minute))) {
        ESP_LOGW(TAG, "automatic time save rejected; waiting for valid date/time");
        return;
    }
    ctx->dirty = false;
}

static void save_timer_cb(lv_timer_t *timer)
{
    manual_time_ctx_t *ctx = lv_timer_get_user_data(timer);
    lv_timer_pause(timer);
    save_pending(ctx);
}

static void input_changed_cb(lv_event_t *e)
{
    manual_time_ctx_t *ctx = lv_event_get_user_data(e);
    ctx->dirty = true;
    if (ctx->save_timer) {
        lv_timer_reset(ctx->save_timer);
        lv_timer_resume(ctx->save_timer);
    } else {
        save_pending(ctx);
    }
}

static void page_delete_cb(lv_event_t *e)
{
    manual_time_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx->save_timer) lv_timer_delete(ctx->save_timer);
    save_pending(ctx);
    lv_free(ctx);
}

static lv_obj_t *manual_time_input(lv_obj_t *parent, const char *title,
                                   int value, int minimum, int maximum,
                                   manual_time_ctx_t *ctx)
{
    lv_obj_t *input = lv_number_input_create(parent, title, value, 1, NULL);
    if (input) {
        lv_num_input_set_range(input, minimum, maximum);
        lv_num_input_set_field_width(input, 120);
        lv_obj_set_width(input, LV_PCT(100));
        lv_obj_add_event_cb(input, input_changed_cb, LV_EVENT_VALUE_CHANGED, ctx);
    }
    return input;
}

static lv_obj_t *build_manual_datetime_page(app_handle_t handler, void *user_data)
{
    (void)user_data;
    SettingsApp *app = handler;
    Page page = lv_page_create("Manual Date & Time", true,
                               page_navigator_navigate_back, &app->view->page_nav);

    manual_time_ctx_t *ctx = lv_malloc(sizeof(*ctx));
    if (!ctx) return page.screen;
    memset(ctx, 0, sizeof(*ctx));
    lv_obj_add_event_cb(page.screen, page_delete_cb, LV_EVENT_DELETE, ctx);

    struct tm local = {
        .tm_year = 124,
        .tm_mon = 0,
        .tm_mday = 1,
    };
    time_t current = time(NULL);
    struct tm clock_local;
    if (localtime_r(&current, &clock_local) &&
        clock_local.tm_year + 1900 >= 2024 &&
        clock_local.tm_year + 1900 <= 2105) {
        local = clock_local;
    } else {
        uint32_t estimated_epoch;
        if (time_service_now(&estimated_epoch)) {
            current = (time_t)estimated_epoch;
            localtime_r(&current, &local);
        }
    }

    lv_obj_t *cont = page.container;
    ctx->year = manual_time_input(cont, "Year", local.tm_year + 1900,
                                  2024, 2105, ctx);
    ctx->month = manual_time_input(cont, "Month", local.tm_mon + 1, 1, 12, ctx);
    ctx->day = manual_time_input(cont, "Day", local.tm_mday, 1, 31, ctx);
    ctx->hour = manual_time_input(cont, "Hour", local.tm_hour, 0, 23, ctx);
    ctx->minute = manual_time_input(cont, "Minute", local.tm_min, 0, 59, ctx);

    ctx->save_timer = lv_timer_create(save_timer_cb, 1200, ctx);
    if (ctx->save_timer) lv_timer_pause(ctx->save_timer);
    return page.screen;
}

void settings_view_manual_datetime_init_registry(SettingsApp *app)
{
    page_navigator_register_page(&app->view->page_nav,
                                 PAGE_GENERAL_MANUAL_DATETIME,
                                 build_manual_datetime_page);
}
