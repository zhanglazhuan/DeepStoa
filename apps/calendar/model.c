// apps/calendar/model.c
// Calendar model — date logic using POSIX time()

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "esp_log.h"

#include "model.h"
#include "app.h"

static const char *TAG = "calendar_model";

/* ── Static tables ───────────────────────────────────────────── */

static const char *s_month_names[13] = {
    "", /* index 0 unused */
    "January", "February", "March",    "April",
    "May",     "June",     "July",     "August",
    "September","October", "November", "December",
};

static const char *s_month_short[13] = {
    "", /* index 0 unused */
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

static const char *s_wday_short[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
};

static const char *s_wday_long[7] = {
    "Sunday",   "Monday",  "Tuesday", "Wednesday",
    "Thursday", "Friday",  "Saturday",
};

static const uint8_t s_days_in_month[13] = {
    0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
};

/* ── Helper implementations ──────────────────────────────────── */

bool calendar_is_leap_year(uint16_t year)
{
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

uint8_t calendar_days_in_month(uint16_t year, uint8_t month)
{
    if (month < 1 || month > 12) return 0;
    if (month == 2 && calendar_is_leap_year(year)) return 29;
    return s_days_in_month[month];
}

/*
 * Tomohiko Sakamoto's day-of-week algorithm.
 * Returns 0=Sunday, 1=Monday … 6=Saturday.
 */
DayOfWeek calendar_weekday(uint16_t year, uint8_t month, uint8_t day)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    uint16_t y = year;
    if (month < 3) y--;
    int w = (y + y/4 - y/100 + y/400 + t[month-1] + day) % 7;
    if (w < 0) w += 7;
    return (DayOfWeek)w;
}

DayOfWeek calendar_first_weekday(uint16_t year, uint8_t month)
{
    return calendar_weekday(year, month, 1);
}

uint8_t calendar_first_column(uint16_t year, uint8_t month)
{
    /* +7 保证 CAL_WEEK_START 大于星期号时不会先算成负数 */
    return (uint8_t)((calendar_first_weekday(year, month) + 7 - CAL_WEEK_START) % 7);
}

DayOfWeek calendar_wday_of_column(uint8_t col)
{
    return (DayOfWeek)((col + CAL_WEEK_START) % 7);
}

const char *calendar_month_name(uint8_t month)
{
    if (month < 1 || month > 12) return "";
    return s_month_names[month];
}

const char *calendar_month_short(uint8_t month)
{
    if (month < 1 || month > 12) return "";
    return s_month_short[month];
}

const char *calendar_wday_short(DayOfWeek wday)
{
    return s_wday_short[wday % 7];
}

const char *calendar_wday_long(DayOfWeek wday)
{
    return s_wday_long[wday % 7];
}

/* ── Read today from system time (or fallback) ───────────────── */

static void read_today(CalDate *out_date, DayOfWeek *out_wday)
{
    time_t now_ts = time(NULL);
    struct tm *now = localtime(&now_ts);

    if (now != NULL && now->tm_year >= 100) {
        out_date->year  = (uint16_t)(now->tm_year + 1900);
        out_date->month = (uint8_t)(now->tm_mon + 1);  /* tm_mon 0-based */
        out_date->day   = (uint8_t)now->tm_mday;
        *out_wday       = (DayOfWeek)(now->tm_wday);
        return;
    }

    ESP_LOGW(TAG, "System time not available, using fallback date");

    /* Fallback: hard-coded date for when system time is not set. */
    out_date->year  = 2026;
    out_date->month = 3;
    out_date->day   = 13;
    *out_wday = calendar_weekday(2026, 3, 13);
}

/* ── Lifecycle ────────────────────────────────────────────────── */

void calendar_model_init(CalendarApp *app)
{
    app->model = malloc(sizeof(CalendarModel));
    if (!app->model) {
        ESP_LOGE(TAG, "Failed to alloc CalendarModel");
        return;
    }
    memset(app->model, 0, sizeof(CalendarModel));

    calendar_model_refresh_today(app->model);

    /* Start the view on the current month */
    app->model->view_year  = app->model->today.year;
    app->model->view_month = app->model->today.month;
}

void calendar_model_deinit(CalendarApp *app)
{
    if (app->model) {
        free(app->model);
        app->model = NULL;
    }
}

void calendar_model_refresh_today(CalendarModel *m)
{
    CalDate   prev = m->today;
    read_today(&m->today, &m->today_wday);

    if (prev.year != m->today.year || prev.month != m->today.month ||
        prev.day != m->today.day) {
        ESP_LOGI(TAG, "Today: %04d-%02d-%02d (%s)",
                 m->today.year, m->today.month, m->today.day,
                 calendar_wday_long(m->today_wday));
    }
}

bool calendar_model_viewing_today_month(const CalendarModel *m)
{
    return m->view_year == m->today.year && m->view_month == m->today.month;
}

/* ── Month navigation ─────────────────────────────────────────── */

void calendar_model_prev_month(CalendarModel *m)
{
    if (m->view_month == 1) {
        m->view_month = 12;
        m->view_year--;
    } else {
        m->view_month--;
    }
}

void calendar_model_next_month(CalendarModel *m)
{
    if (m->view_month == 12) {
        m->view_month = 1;
        m->view_year++;
    } else {
        m->view_month++;
    }
}

void calendar_model_go_today(CalendarModel *m)
{
    calendar_model_refresh_today(m);
    m->view_year  = m->today.year;
    m->view_month = m->today.month;
}

void calendar_model_set_month(CalendarModel *m, uint16_t year, uint8_t month)
{
    if (month < 1 || month > 12) return;
    m->view_year  = year;
    m->view_month = month;
}
