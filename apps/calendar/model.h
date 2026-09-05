#ifndef CALENDAR_MODEL_H
#define CALENDAR_MODEL_H

#include <stdint.h>
#include <stdbool.h>
#include "app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 一周从哪天开始 ───────────────────────────────────────────────
 * 0 = 周日（美式）  1 = 周一（ISO / 国内习惯）
 * 设备面向国内 K-12，默认周一。改这一个常量，表头和网格会一起跟着走。
 * 等 settings 里加上"周起始日"开关时，把这里换成读配置即可。 */
#define CAL_WEEK_START  1

/* ── Date types ───────────────────────────────────────────────── */

typedef struct CalDate {
    uint16_t year;   /* e.g. 2026           */
    uint8_t  month;  /* 1-12                */
    uint8_t  day;    /* 1-31                */
} CalDate;

/* Day-of-week: 0 = Sunday … 6 = Saturday */
typedef uint8_t DayOfWeek;

/* ── Model ────────────────────────────────────────────────────────
 * 刻意没有 selected_day：选中一个日期在没有日程功能之前没有任何后续，
 * 却要付出一次面板刷新。等日程落地时再连同详情页一起加回来。 */
typedef struct CalendarModel {
    /* Today (from system time) */
    CalDate     today;
    DayOfWeek   today_wday;   /* 0=Sun … 6=Sat */

    /* Currently displayed month/year (may differ from today) */
    uint16_t    view_year;
    uint8_t     view_month;   /* 1-12 */
} CalendarModel;

/* ── Lifecycle ────────────────────────────────────────────────── */
void calendar_model_init(CalendarApp *app);
void calendar_model_deinit(CalendarApp *app);

/* ── Today ────────────────────────────────────────────────────── */
void calendar_model_refresh_today(CalendarModel *m);

/** 当前浏览的月份就是今天所在的月份 */
bool calendar_model_viewing_today_month(const CalendarModel *m);

/* ── Navigation ───────────────────────────────────────────────── */
void calendar_model_prev_month(CalendarModel *m);
void calendar_model_next_month(CalendarModel *m);
void calendar_model_go_today(CalendarModel *m);
void calendar_model_set_month(CalendarModel *m, uint16_t year, uint8_t month);

/* ── Date helpers ─────────────────────────────────────────────── */

/* Returns true if year is a leap year */
bool calendar_is_leap_year(uint16_t year);

/* Days in a given month (1-12) of a year */
uint8_t calendar_days_in_month(uint16_t year, uint8_t month);

/* Day-of-week for an arbitrary date. 0=Sun … 6=Sat.
 * Uses Tomohiko Sakamoto's algorithm (no external deps). */
DayOfWeek calendar_weekday(uint16_t year, uint8_t month, uint8_t day);

/* Day-of-week for the 1st of (year, month) */
DayOfWeek calendar_first_weekday(uint16_t year, uint8_t month);

/* ── 网格列换算（已按 CAL_WEEK_START 折算，view 不用再自己算） ──── */

/** 该月 1 号落在网格的第几列，0-6 */
uint8_t calendar_first_column(uint16_t year, uint8_t month);

/** 第 col 列（0-6）对应星期几，用来生成表头 */
DayOfWeek calendar_wday_of_column(uint8_t col);

/* Full month name, e.g. "March" */
const char *calendar_month_name(uint8_t month);

/* Short weekday name, e.g. "Mon" */
const char *calendar_wday_short(DayOfWeek wday);

/* Long weekday name, e.g. "Monday" */
const char *calendar_wday_long(DayOfWeek wday);

/* Short month name, e.g. "Mar" — 月份选择器用 */
const char *calendar_month_short(uint8_t month);

#ifdef __cplusplus
}
#endif

#endif /* CALENDAR_MODEL_H */
