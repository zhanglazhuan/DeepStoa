/*
 * view_month.c  --  Calendar month grid page
 *
 * 屏幕 480x800（3.97" ≈ 235dpi），lv_page 之后可用内容区 448x710。
 *
 * ┌────────────────────────────────────┐
 * │  Friday, 13 March 2026             │  today 横幅，24px，全程不变
 * │ ────────────────────────────────── │
 * │  ‹        March 2026  ▾        ›   │  nav_row  ← 局刷窗口上边界
 * │  Mon Tue Wed Thu Fri Sat Sun       │  表头 18px，随 CAL_WEEK_START 走
 * │                                    │
 * │            1   2   3   4   5       │
 * │    6   7   8   9  10  11  12       │  cell 60px（6.5mm），行距=列距=64
 * │   13  14 (15) 16  17  18  19       │  今天 = 3px 描边圆，全页唯一强调
 * │   20  21  22  23  24  25  26       │
 * │   27  28  29  30  31               │
 * │                                    │  固定 6 行不收缩 ← 局刷窗口下边界
 * ├────────────────────────────────────┤
 * │             [ Today ]              │  常驻，不做显隐（显隐会引起重排）
 * └────────────────────────────────────┘
 *
 * ── 墨水屏刷新策略 ──────────────────────────────────────────────────
 * 页面建好后 epd_region_begin() 打一帧基准，之后进入 PARTIAL_WIN。
 * 翻月时只有 nav_row..grid 之间会变，把面板窗口收到这块的并集上：
 *   - 推送面积 448x476 而不是整屏 480x800
 *   - 更关键的是 PARTIAL_WIN 才会走 display_control 里的逐像素差分跳过，
 *     3 月→4 月大部分数字位置不变，这些像素根本不会被驱动
 * 横幅、表头、Today 按钮都在窗口外，所以它们必须是静态的 —— 这也是
 * Today 按钮常驻而不做显隐的原因之一。
 *
 * ── 为什么没有"选中某一天" ─────────────────────────────────────────
 * 选中在日程功能落地前没有任何后续，却要付一次面板刷新，而且会和"今天"
 * 的描边圆抢同一个强调位。等日程做出来时再连详情页一起加回来。
 *
 * ── 为什么没有灰色 ─────────────────────────────────────────────────
 * 面板是 LV_COLOR_FORMAT_I1，LVGL 按亮度二值化：gray(80) 会变纯黑、
 * gray(210) 会变纯白。原来用灰色区分周末（看不出来）和画分隔线（直接
 * 消失）都是无效的。层级只能靠字号和留白。
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include <lvgl.h>
#include "lv_ui_style_guide.h"
#include "lv_epd_region.h"

#include "view_month.h"
#include "../view.h"
#include "../model.h"
#include "../controller.h"
#include "lv_page.h"
#include "lv_bottom_sheet.h"
#include "ui_utils.h"

EPOS_LV_IMG_DECLARE(arrow_left);
EPOS_LV_IMG_DECLARE(arrow_right);
EPOS_LV_IMG_DECLARE(arrow_down);

static const char *TAG = "cal_view_month";

/* ── 版面常量 ─────────────────────────────────────────────────────
 * 内容区 448 宽 / 7 列 = 64 的列距是硬上限。cell 取 60 留 4px 缝，
 * 行距同样取 4，让网格的横竖节奏一致（原来 48+24 是竖向拉伸的）。
 * 60px @235dpi ≈ 6.5mm，够手指点；再大就超过列距了。 */
#define CELL_SIZE      60
#define CELL_RADIUS    (CELL_SIZE / 2)      /* 真的画成圆，不是圆角方块 */
#define GRID_ROWS      6
#define GRID_COLS      7
#define ROW_GAP        4
#define TODAY_RING_W   3

#define NAV_BTN        64                   /* ≈6.9mm，翻月箭头的触控目标 */
#define TODAY_BTN_H    56

#define PICKER_YEAR_MIN 2020
#define PICKER_YEAR_MAX 2040
#define PICKER_COLS     4
#define PICKER_CELL_W   100
#define PICKER_CELL_H   56

/* ── 共享样式 ─────────────────────────────────────────────────────
 * 42 个格子共用同一份样式对象，而不是每格调十几次 lv_obj_set_style_*：
 * 省掉 42 份 per-object style 数组，重画时也只是加/减一个样式引用。 */
static lv_style_t s_cell;
static lv_style_t s_cell_today;
static lv_style_t s_wday;
static lv_style_t s_hairline;
static bool       s_styles_init;

static void init_styles(void)
{
    if (s_styles_init) return;
    s_styles_init = true;

    /* 每格就是一个定尺 label。文字靠 pad_top 压到垂直居中 —— 省掉
     * "容器 + 居中子 label" 那一层，42 个对象直接砍掉。 */
    int32_t lh  = lv_font_get_line_height(CAL_FONT_MONTH);
    int32_t top = (CELL_SIZE - lh) / 2;
    if (top < 0) top = 0;

    lv_style_init(&s_cell);
    lv_style_set_width(&s_cell, CELL_SIZE);
    lv_style_set_height(&s_cell, CELL_SIZE);
    lv_style_set_pad_top(&s_cell, top);
    lv_style_set_pad_left(&s_cell, 0);
    lv_style_set_pad_right(&s_cell, 0);
    lv_style_set_text_font(&s_cell, CAL_FONT_MONTH);
    lv_style_set_text_align(&s_cell, LV_TEXT_ALIGN_CENTER);
    lv_style_set_text_color(&s_cell, lv_color_black());
    lv_style_set_radius(&s_cell, CELL_RADIUS);
    lv_style_set_border_width(&s_cell, 0);
    lv_style_set_bg_opa(&s_cell, LV_OPA_TRANSP);

    /* 今天：描边圆。全页唯一的强调，进页面和按 Today 都是这一种画法。 */
    lv_style_init(&s_cell_today);
    lv_style_set_border_width(&s_cell_today, TODAY_RING_W);
    lv_style_set_border_color(&s_cell_today, lv_color_black());
    lv_style_set_border_opa(&s_cell_today, LV_OPA_COVER);

    lv_style_init(&s_wday);
    lv_style_set_width(&s_wday, CELL_SIZE);
    lv_style_set_text_font(&s_wday, CAL_FONT_WDAY);
    lv_style_set_text_align(&s_wday, LV_TEXT_ALIGN_CENTER);
    lv_style_set_text_color(&s_wday, lv_color_black());

    lv_style_init(&s_hairline);
    lv_style_set_height(&s_hairline, 1);
    lv_style_set_bg_color(&s_hairline, lv_color_black());
    lv_style_set_bg_opa(&s_hairline, LV_OPA_COVER);
    lv_style_set_border_width(&s_hairline, 0);
    lv_style_set_radius(&s_hairline, 0);
}

/* ── 小工具 ───────────────────────────────────────────────────── */

static lv_obj_t *make_row(lv_obj_t *parent, int32_t h)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), h);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
}

/** 图标按钮：热区就是绘制区，不用 translate 去挪图标（那会让两者错开）。 */
static lv_obj_t *make_icon_button(lv_obj_t *parent, const void *img_src,
                                  lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, NAV_BTN, NAV_BTN);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *ic = lv_image_create(btn);
    lv_image_set_src(ic, img_src);
    lv_obj_center(ic);
    return btn;
}

/** 把面板刷新窗口收到 nav_row..grid 的并集上（两者之间只隔一个静态表头）。 */
static void flush_month_region(CalendarApp *app)
{
    MonthPageCtx *ctx = &app->view->month_ctx;

    /* 基准帧还没打完就先别设窗口 —— 让它走普通整屏刷新，画面才是对的 */
    if (!epd_region_is_active()) return;
    if (!ctx->nav_row || !ctx->grid) return;

    lv_obj_update_layout(ctx->grid);

    lv_area_t a, b, u;
    lv_obj_get_coords(ctx->nav_row, &a);
    lv_obj_get_coords(ctx->grid, &b);
    u.x1 = LV_MIN(a.x1, b.x1);
    u.y1 = LV_MIN(a.y1, b.y1);
    u.x2 = LV_MAX(a.x2, b.x2);
    u.y2 = LV_MAX(a.y2, b.y2);

    epd_region_flush_area(&u);
    lv_obj_invalidate(ctx->nav_row);
    lv_obj_invalidate(ctx->grid);
}

/* ── 重画：月份标题 + 网格 ────────────────────────────────────── */

void calendar_view_month_rebuild(CalendarApp *app)
{
    CalendarModel *m   = app->model;
    MonthPageCtx  *ctx = &app->view->month_ctx;

    if (!ctx->month_label) return;   /* 页面还没建好 */

    /* 顺手重读一次系统时间。这样跨天之后第一次交互，今天的圈就自己挪对了，
     * 不需要一个 24 小时周期、而且 app 一关就被销毁的"午夜定时器"。 */
    calendar_model_refresh_today(m);

    if (ctx->today_label) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s, %d %s %d",
                 calendar_wday_long(m->today_wday),
                 m->today.day,
                 calendar_month_name(m->today.month),
                 m->today.year);
        lv_label_set_text(ctx->today_label, buf);
    }

    /* "March 2026" 比 "2026 / 03" 好读，横向空间也够 */
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s %d",
                 calendar_month_name(m->view_month), m->view_year);
        lv_label_set_text(ctx->month_label, buf);
    }

    uint8_t dim       = calendar_days_in_month(m->view_year, m->view_month);
    uint8_t first_col = calendar_first_column(m->view_year, m->view_month);
    bool    show_today = calendar_model_viewing_today_month(m);

    for (int row = 0; row < GRID_ROWS; row++) {
        for (int col = 0; col < GRID_COLS; col++) {
            lv_obj_t *cell = ctx->day_cells[row][col];
            if (!cell) continue;

            int day_slot = row * GRID_COLS + col - (int)first_col;

            if (day_slot < 0 || day_slot >= dim) {
                lv_label_set_text(cell, "");
                lv_obj_remove_style(cell, &s_cell_today, LV_PART_MAIN);
                continue;
            }

            uint8_t d = (uint8_t)(day_slot + 1);
            lv_label_set_text_fmt(cell, "%d", d);

            if (show_today && d == m->today.day) {
                lv_obj_add_style(cell, &s_cell_today, LV_PART_MAIN);
            } else {
                lv_obj_remove_style(cell, &s_cell_today, LV_PART_MAIN);
            }
        }
    }

    flush_month_region(app);
}

/* ── 页面构建 ─────────────────────────────────────────────────── */

static lv_obj_t *build_month_page(CalendarApp *app, void *user_data)
{
    (void)user_data;
    init_styles();

    MonthPageCtx *ctx = &app->view->month_ctx;
    memset(ctx, 0, sizeof(*ctx));

    Page page = lv_page_create("Calendar", false, NULL, NULL);
    lv_obj_t *cont = page.container;

    /* 纵向节奏自己定，不吃主题 card 样式带来的 pad_gap */
    lv_obj_set_style_pad_row(cont, 0, LV_PART_MAIN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    /* 1. 今天横幅 —— 离开当前月之后，它是屏幕上唯一还告诉你"今天"的东西 */
    lv_obj_t *today_lbl = lv_label_create(cont);
    lv_obj_set_width(today_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(today_lbl, CAL_FONT_TODAY, 0);
    lv_obj_set_style_text_align(today_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_bottom(today_lbl, 6, 0);
    lv_label_set_long_mode(today_lbl, LV_LABEL_LONG_DOT);
    lv_label_set_text(today_lbl, "");
    ctx->today_label = today_lbl;

    lv_obj_t *hr = lv_obj_create(cont);
    lv_obj_remove_style_all(hr);
    lv_obj_add_style(hr, &s_hairline, LV_PART_MAIN);
    lv_obj_set_width(hr, LV_PCT(100));

    /* 2. 月份导航 ‹ March 2026 ▾ › */
    lv_obj_t *nav_row = make_row(cont, NAV_BTN);
    lv_obj_set_style_margin_top(nav_row, 6, 0);
    ctx->nav_row = nav_row;

    make_icon_button(nav_row, EPOS_LV_IMG_USE(arrow_left),
                     calendar_controller_on_prev_month, app);

    lv_obj_t *month_btn = lv_obj_create(nav_row);
    lv_obj_remove_style_all(month_btn);
    lv_obj_set_size(month_btn, LV_SIZE_CONTENT, NAV_BTN);
    lv_obj_set_flex_flow(month_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(month_btn, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(month_btn, 6, 0);
    lv_obj_add_flag(month_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(month_btn, calendar_controller_on_month_title_clicked,
                        LV_EVENT_CLICKED, app);

    lv_obj_t *month_lbl = lv_label_create(month_btn);
    lv_obj_set_style_text_font(month_lbl, CAL_FONT_MONTH, 0);
    lv_label_set_text(month_lbl, "");
    ctx->month_label = month_lbl;

    lv_obj_t *drop_icon = lv_image_create(month_btn);
    lv_image_set_src(drop_icon, EPOS_LV_IMG_USE(arrow_down));

    make_icon_button(nav_row, EPOS_LV_IMG_USE(arrow_right),
                     calendar_controller_on_next_month, app);

    /* 3. 星期表头 —— 列名由 model 按 CAL_WEEK_START 算，和网格保证同步 */
    lv_obj_t *wday_row = make_row(cont, 32);
    lv_obj_set_style_margin_top(wday_row, 10, 0);
    for (int col = 0; col < GRID_COLS; col++) {
        lv_obj_t *wl = lv_label_create(wday_row);
        lv_obj_add_style(wl, &s_wday, LV_PART_MAIN);
        lv_label_set_text(wl, calendar_wday_short(calendar_wday_of_column(col)));
    }

    /* 4. 日期网格：42 格预分配，翻月只改文字和"今天"那个样式引用 */
    lv_obj_t *grid = lv_obj_create(cont);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_margin_top(grid, 6, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(grid, ROW_GAP, LV_PART_MAIN);
    ctx->grid = grid;

    for (int row = 0; row < GRID_ROWS; row++) {
        lv_obj_t *row_obj = make_row(grid, CELL_SIZE);
        for (int col = 0; col < GRID_COLS; col++) {
            lv_obj_t *cell = lv_label_create(row_obj);
            lv_obj_add_style(cell, &s_cell, LV_PART_MAIN);
            lv_label_set_long_mode(cell, LV_LABEL_LONG_CLIP);
            lv_label_set_text(cell, "");
            ctx->day_cells[row][col] = cell;
        }
    }

    /* 5. 弹性留白，把 Today 顶到底部 */
    lv_obj_t *spacer = lv_obj_create(cont);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_width(spacer, LV_PCT(100));
    lv_obj_set_flex_grow(spacer, 1);

    /* 6. Today —— 常驻。做显隐会让版面在翻月时上下跳，而且它在局刷窗口
     *    之外，显隐了也推不上面板。已经在当前月时点它就是空操作。 */
    lv_obj_t *today_btn = lv_button_create(cont);
    ui_style_set_btn_secondary(today_btn);
    lv_obj_set_size(today_btn, LV_SIZE_CONTENT, TODAY_BTN_H);
    lv_obj_set_style_pad_hor(today_btn, 28, 0);
    lv_obj_set_style_shadow_width(today_btn, 0, 0);
    lv_obj_set_style_margin_bottom(today_btn, 24, 0); /* 距底部留 24px 空隙 */
    lv_obj_add_event_cb(today_btn, calendar_controller_on_today,
                        LV_EVENT_CLICKED, app);
    lv_obj_t *tl = lv_label_create(today_btn);
    lv_obj_set_style_text_font(tl, CAL_FONT_TODAY, 0);
    lv_label_set_text(tl, "Today");
    lv_obj_center(tl);

    calendar_view_month_rebuild(app);

    /* 先打一帧基准（整屏，仅此一次），之后翻月就只推 nav_row..grid 那块 */
    epd_region_begin(page.screen);

    return page.screen;
}

/* ── 月份选择器 ───────────────────────────────────────────────────
 * 原来用的是 lv_roller。roller 是为 60fps LCD 的惯性滚动设计的：拖动
 * 期间每帧一次整屏推送，松手还有主题给的 300ms 吸附动画，在墨水屏上
 * 会糊成一片再猛跳一下。这里换成"一次点击 = 一次刷新"的确定性控件：
 * 年份左右步进 + 12 个月份宫格，点中即生效，连确认按钮都不需要。
 */

static void picker_deleted_cb(lv_event_t *e)
{
    CalendarApp *app = lv_event_get_user_data(e);

    /* app 已经停了（view 被释放）就什么都别做 —— 此时活动屏幕已经不是
     * 日历页了，在它上面 begin() 会把别人的页面切进窗口模式。 */
    if (!app || !app->view) return;

    app->view->month_ctx.picker_sheet = NULL;
    epd_region_begin(lv_screen_active());   /* 弹层没了，重新进局刷模式 */
}

/** 给"正在看的那个月"描边。年份步进之后必须重算，否则边框会留在旧年份上。 */
static void picker_mark_current_month(CalendarApp *app)
{
    MonthPageCtx *ctx = &app->view->month_ctx;
    if (!ctx->picker_months) return;

    bool same_year = (ctx->picker_year == app->model->view_year);

    for (uint8_t mon = 1; mon <= 12; mon++) {
        lv_obj_t *cell = lv_obj_get_child(ctx->picker_months, mon - 1);
        if (!cell) continue;

        bool current = same_year && mon == app->model->view_month;
        lv_obj_set_style_border_width(cell, current ? 2 : 0, 0);
        if (current) {
            lv_obj_set_style_border_color(cell, lv_color_black(), 0);
        }
    }
}

static void picker_month_clicked_cb(lv_event_t *e)
{
    CalendarApp *app  = lv_event_get_user_data(e);
    lv_obj_t    *cell = lv_event_get_current_target(e);
    uint8_t      mon  = (uint8_t)(uintptr_t)lv_obj_get_user_data(cell);

    calendar_controller_on_month_picked(app, mon);
}

void calendar_view_show_month_picker(CalendarApp *app)
{
    MonthPageCtx *ctx = &app->view->month_ctx;
    if (ctx->picker_sheet) return;      /* 连点两下不要开出两个 */

    /* 弹层铺满全屏，会落在局刷窗口之外 —— 不先退出窗口模式，
     * 窗口外的像素根本推不上面板，弹层会"看不见"。
     *
     * 这里不加 is_active() 判断：begin() 是异步的，可能已经排队但基准帧还
     * 没打（is_active 仍是 false）。end() 会把 s_screen 清掉，让那个待执行
     * 的 begin_async_cb 自己判 "screen changed" 后跳过 —— 顺手把竞态也堵了。 */
    epd_region_end();

    lv_bottom_sheet_t *bs = lv_bottom_sheet_create(lv_screen_active());
    if (!bs) return;
    ctx->picker_sheet = bs;
    ctx->picker_year  = app->model->view_year;

    lv_bottom_sheet_add_header(bs, "Jump to month");

    /* 无论从哪条路径消失（选中 / 关闭按钮 / 点遮罩），都在这里统一收尾。
     * 这样 picker_sheet 不会变成野指针，局刷模式也一定会被重新拉起。 */
    lv_obj_add_event_cb(bs->overlay, picker_deleted_cb, LV_EVENT_DELETE, app);

    lv_obj_t *content = lv_bottom_sheet_get_content(bs);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 12, 0);

    /* 年份步进 ‹ 2026 › */
    lv_obj_t *year_row = lv_obj_create(content);
    lv_obj_remove_style_all(year_row);
    lv_obj_set_size(year_row, LV_PCT(100), NAV_BTN);
    lv_obj_set_flex_flow(year_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(year_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(year_row, 24, 0);

    make_icon_button(year_row, EPOS_LV_IMG_USE(arrow_left),
                     calendar_controller_on_picker_year_prev, app);

    lv_obj_t *year_lbl = lv_label_create(year_row);
    lv_obj_set_style_text_font(year_lbl, CAL_FONT_MONTH, 0);
    lv_label_set_text_fmt(year_lbl, "%d", ctx->picker_year);
    ctx->picker_year_label = year_lbl;

    make_icon_button(year_row, EPOS_LV_IMG_USE(arrow_right),
                     calendar_controller_on_picker_year_next, app);

    /* 12 个月份，4 列 x 3 行，点中即生效 */
    lv_obj_t *months = lv_obj_create(content);
    lv_obj_remove_style_all(months);
    lv_obj_set_size(months, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(months, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(months, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(months, 8, 0);
    ctx->picker_months = months;

    int32_t lh  = lv_font_get_line_height(CAL_FONT_TODAY);
    int32_t top = (PICKER_CELL_H - lh) / 2;
    if (top < 0) top = 0;

    for (uint8_t mon = 1; mon <= 12; mon++) {
        lv_obj_t *cell = lv_label_create(months);
        lv_obj_set_size(cell, PICKER_CELL_W, PICKER_CELL_H);
        lv_obj_set_style_pad_top(cell, top, 0);
        lv_obj_set_style_text_font(cell, CAL_FONT_TODAY, 0);
        lv_obj_set_style_text_align(cell, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_radius(cell, 4, 0);
        lv_label_set_long_mode(cell, LV_LABEL_LONG_CLIP);
        lv_label_set_text(cell, calendar_month_short(mon));

        lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(cell, (void *)(uintptr_t)mon);
        lv_obj_add_event_cb(cell, picker_month_clicked_cb, LV_EVENT_CLICKED, app);
    }

    /* 当前正在看的月份描个边，给个位置感 */
    picker_mark_current_month(app);

    ESP_LOGI(TAG, "month picker opened (year %d)", ctx->picker_year);
}

void calendar_view_close_month_picker(CalendarApp *app)
{
    MonthPageCtx *ctx = &app->view->month_ctx;
    if (!ctx->picker_sheet) return;

    /* 指针清理和重进局刷都在 picker_deleted_cb 里做，这里只负责关 */
    lv_bottom_sheet_close(ctx->picker_sheet);
}

void calendar_view_picker_step_year(CalendarApp *app, int delta)
{
    MonthPageCtx *ctx = &app->view->month_ctx;
    if (!ctx->picker_sheet || !ctx->picker_year_label) return;

    int y = (int)ctx->picker_year + delta;
    if (y < PICKER_YEAR_MIN) y = PICKER_YEAR_MIN;
    if (y > PICKER_YEAR_MAX) y = PICKER_YEAR_MAX;
    if (y == (int)ctx->picker_year) return;      /* 到头了就别白刷一次屏 */

    ctx->picker_year = (uint16_t)y;
    lv_label_set_text_fmt(ctx->picker_year_label, "%d", ctx->picker_year);
    picker_mark_current_month(app);   /* 换年之后边框要跟着走，不能留在旧年份上 */
}

void calendar_view_month_init_registry(CalendarApp *app)
{
    PAGE_REGISTE(app, PAGE_CALENDAR_MONTH, build_month_page);
}
