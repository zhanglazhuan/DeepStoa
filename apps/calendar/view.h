#ifndef CALENDAR_VIEW_H
#define CALENDAR_VIEW_H

#include <lvgl.h>
#include "app.h"
#include "model.h"
#include "page_navigator.h"
#include "lv_theme_hardcore.h"
#include "lv_bottom_sheet.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Page IDs ─────────────────────────────────────────────────── */
#define CALENDAR_PAGE_ID_MAX  4

typedef enum {
    PAGE_CALENDAR_NONE = 0,
    PAGE_CALENDAR_MONTH,
} CalendarPageId;

/* ── 字号分级 ─────────────────────────────────────────────────────
 * 屏 480x800 ≈ 235dpi。1bpp 面板没有灰度可用（LV_COLOR_FORMAT_I1 会把
 * 颜色按亮度二值化），层级只能靠字号和留白拉开，不能靠深浅。 */
#define CAL_FONT_MONTH  LV_FONT_NORMAL   /* montserrat 32 — 月份标题、日期数字 */
#define CAL_FONT_TODAY  LV_FONT_SMALL    /* montserrat 24 — 今天横幅、按钮 */
#define CAL_FONT_WDAY   LV_FONT_TINY     /* montserrat 18 — 星期表头 */

/* ── Month page context ───────────────────────────────────────────
 * 只留真正会被复用的引用。nav_row / grid 是局部刷新窗口的上下边界，
 * 月份切换时只有这两块之间的像素会变。 */
typedef struct MonthPageCtx {
    lv_obj_t *today_label;      /* "Friday, 13 March 2026"，整个 app 生命周期基本不变 */
    lv_obj_t *nav_row;          /* 刷新窗口上边界 */
    lv_obj_t *month_label;      /* "March 2026" */
    lv_obj_t *grid;             /* 刷新窗口下边界 */

    /* 6x7 日期格，每格就是一个 label（不是 button + label） */
    lv_obj_t *day_cells[6][7];

    /* 月份选择器。挂在 view 里，不再单独 malloc 塞进 page_nav.nav_ctx —— */
    /* 那块内存没人释放（page_navigator 只在换页时释放，而本 app 只有一页）。 */
    lv_bottom_sheet_t *picker_sheet;
    lv_obj_t          *picker_year_label;
    lv_obj_t          *picker_months;   /* 12 个月份格的父容器，子序号 0..11 = 1..12 月 */
    uint16_t           picker_year;
} MonthPageCtx;

/* ── View ─────────────────────────────────────────────────────── */
typedef struct CalendarView {
    page_navigator_t page_nav;
    MonthPageCtx     month_ctx;
} CalendarView;

/* Lifecycle */
void calendar_view_init(CalendarApp *app);
void calendar_view_deinit(CalendarApp *app);

/** 重画月份标题 + 网格，并把面板刷新限制在 nav_row..grid 这一块。 */
void calendar_view_refresh_month(CalendarApp *app);

/* 月份选择器（bottom sheet） */
void calendar_view_show_month_picker(CalendarApp *app);
void calendar_view_close_month_picker(CalendarApp *app);
void calendar_view_picker_step_year(CalendarApp *app, int delta);

#ifdef __cplusplus
}
#endif

#endif /* CALENDAR_VIEW_H */
