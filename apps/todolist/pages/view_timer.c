#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "lv_ui_style_guide.h"
#include "lv_epd_region.h"

#include "lv_tab.h"
#include "view_timer.h"
#include "controller_timer.h"
#include "../view.h"
#include "../controller.h"
#include "../app.h"

__attribute__((unused)) static const char *TAG = "todolist_view_timer";

// 全局应用实例
extern TodoListApp g_todolist_app;

#define TIMER_PROGRESS_MAX 10000

/* Keep sub-percent progress so the bar can advance with the seconds display. */
static uint32_t progress_value(uint32_t elapsed_s, uint32_t total_s)
{
    if (total_s == 0) return 0;
    if (elapsed_s >= total_s) return TIMER_PROGRESS_MAX;
    return (uint32_t)((uint64_t)elapsed_s * TIMER_PROGRESS_MAX / total_s);
}

/* ── 按秒显示 MM:SS ─────────────────────────────────────────────────── */

uint32_t todolist_view_timer_display_key(uint32_t rem_s)
{
    return rem_s;
}

static void format_remaining(char *buf, size_t len, uint32_t rem_s)
{
    snprintf(buf, len, "%02u:%02u", (unsigned)(rem_s / 60), (unsigned)(rem_s % 60));
}

/* 当前 Timer 页的上下文；不在 Home 页 / Timer tab 没建起来时返回 NULL。
 * nav_ctx 是每个页面各自的上下文，只有 Home 页里放的才是 TodoListViewHomeCtx ——
 * 后台计时结束时可能停在表单页上，不挡住就会把别的结构体当 HomeCtx 解引用。 */
static TodoListViewTimerCtx * timer_ctx_get(void)
{
    if (!g_todolist_app.view) return NULL;
    if (g_todolist_app.view->page_nav.current_page != PAGE_HOME) return NULL;

    TodoListViewHomeCtx *home_ctx = (TodoListViewHomeCtx*)g_todolist_app.view->page_nav.nav_ctx;
    if (!home_ctx || !home_ctx->timer_ctx) return NULL;
    return home_ctx->timer_ctx;
}

/* ── 构建 ─────────────────────────────────────────────────────────────── */

void view_build_timer_tab(TodoListView* v) {
    TodoListViewHomeCtx *home_ctx = (TodoListViewHomeCtx*)v->page_nav.nav_ctx;
    lv_obj_t * tab = home_ctx->tab_timer;
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tab, 12, 0);
    lv_obj_set_scroll_dir(tab, LV_DIR_VER);

    TodoListViewTimerCtx* timer_ctx = &home_ctx->timer_ctx_storage;
    memset(timer_ctx, 0, sizeof(TodoListViewTimerCtx));
    home_ctx->timer_ctx = timer_ctx;

    lv_obj_t * timer_task_lbl = lv_label_create(tab);
    lv_label_set_text(timer_task_lbl, "Active Task");
    lv_obj_set_style_text_font(timer_task_lbl, LV_FONT_SMALL, 0);

    lv_obj_t * timer_task_name = lv_label_create(tab);
    lv_obj_set_width(timer_task_name, LV_PCT(100));
    int32_t line_h = lv_font_get_line_height(LV_FONT_NORMAL);
    lv_obj_set_height(timer_task_name, line_h * 2);
    lv_label_set_long_mode(timer_task_name, LV_LABEL_LONG_DOT);
    lv_label_set_text(timer_task_name, "None");
    lv_obj_set_style_text_font(timer_task_name, LV_FONT_NORMAL, 0);
    timer_ctx->task_name = timer_task_name;

    lv_obj_t * timer_estimate_lbl = lv_label_create(tab);
    lv_label_set_text(timer_estimate_lbl, "Estimate (min)");
    lv_obj_set_style_text_font(timer_estimate_lbl, LV_FONT_SMALL, 0);
    lv_obj_set_style_margin_top(timer_estimate_lbl, 10, 0);

    lv_obj_t * timer_estimate = lv_label_create(tab);
    lv_label_set_text(timer_estimate, "--");
    lv_obj_set_style_text_font(timer_estimate, LV_FONT_NORMAL, 0);
    timer_ctx->estimate = timer_estimate;

    /* 外层负责占位和居中，不纳入每秒刷新的窗口。 */
    lv_obj_t * mid_cont = lv_obj_create(tab);
    lv_obj_remove_style_all(mid_cont); // 移除默认的背景和内边距，使其完全透明
    lv_obj_set_width(mid_cont, LV_PCT(100));
    lv_obj_set_flex_grow(mid_cont, 1);
    lv_obj_set_flex_flow(mid_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(mid_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(mid_cont, LV_OBJ_FLAG_SCROLLABLE);

    /* 紧凑的快刷区域，只包含 MM:SS 和进度条。固定整宽，避免数字变化
     * 引发布局变化；高度随内容计算，不刷新外层的空白占位。 */
    lv_obj_t *countdown_zone = lv_obj_create(mid_cont);
    lv_obj_remove_style_all(countdown_zone);
    lv_obj_set_size(countdown_zone, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(countdown_zone, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(countdown_zone, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(countdown_zone, LV_OBJ_FLAG_SCROLLABLE);
    timer_ctx->zone_countdown = countdown_zone;

    lv_obj_t * timer_countdown = lv_label_create(countdown_zone);
    lv_obj_set_width(timer_countdown, LV_PCT(100));
    lv_obj_set_style_text_align(timer_countdown, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_margin_top(timer_countdown, 20, 0);
    lv_obj_set_style_text_font(timer_countdown, LV_FONT_LARGE, 0);
    lv_obj_set_style_border_width(timer_countdown, 2, 0);
    lv_obj_set_style_border_color(timer_countdown, lv_color_black(), 0);
    lv_obj_set_style_border_side(timer_countdown, LV_BORDER_SIDE_FULL, 0);
    lv_obj_set_style_radius(timer_countdown, 2, 0);
    lv_obj_set_style_pad_all(timer_countdown, 10, 0);
    lv_label_set_text(timer_countdown, "--");

    lv_obj_clear_flag(timer_countdown, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(timer_countdown, LV_OBJ_FLAG_SCROLLABLE);
    timer_ctx->countdown = timer_countdown;

    lv_obj_t * timer_bar = lv_bar_create(countdown_zone);
    lv_obj_set_size(timer_bar, LV_PCT(100), 12);
    lv_bar_set_range(timer_bar, 0, TIMER_PROGRESS_MAX);
    lv_bar_set_value(timer_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_margin_top(timer_bar, 10, 0);
    timer_ctx->progress_bar = timer_bar;

    // 一排 4 个调整按钮：' 表示分钟。
    lv_obj_t * btn_cont = lv_obj_create(tab);
    lv_obj_remove_style_all(btn_cont);
    lv_obj_set_width(btn_cont, LV_PCT(100));
    lv_obj_set_height(btn_cont, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_cont, 12, 0);

    /* 方角描边而不是纯黑圆形：主题的语言是 radius=2 的硬边，
     * 而且大块黑面积在墨水屏上翻转更慢、残影更重，圆形在 1bpp 下边缘就是锯齿。 */
    static const int adjust_min[] = { -10, -5, 5, 10 };
    static const char * const adjust_txt[] = { "-10'", "-5'", "+5'", "+10'" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t * btn = lv_btn_create(btn_cont);
        ui_style_set_btn_secondary(btn);
        lv_obj_set_size(btn, 88, 64);
        lv_obj_set_style_radius(btn, 2, 0);
        lv_obj_t * lbl = lv_label_create(btn);
        lv_label_set_text(lbl, adjust_txt[i]);
        lv_obj_set_style_text_font(lbl, LV_FONT_SMALL, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, controller_on_timer_adjust_estimate, LV_EVENT_CLICKED,
                            (void*)(intptr_t)adjust_min[i]);
    }

    // 底部按钮网格：完成-左，开始-中(宽度 2 倍)，跳过-右
    lv_obj_t * btn_grid = lv_obj_create(tab);
    lv_obj_remove_style_all(btn_grid);
    lv_obj_set_size(btn_grid, LV_PCT(100), 60);
    lv_obj_set_style_margin_top(btn_grid, 20, LV_PART_MAIN);
    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(2), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(btn_grid, col_dsc, row_dsc);

    // Finish: outline button
    lv_obj_t * timer_finish_btn = lv_btn_create(btn_grid);
    ui_style_set_btn_secondary(timer_finish_btn);
    lv_obj_set_width(timer_finish_btn, LV_PCT(30));
    lv_obj_set_grid_cell(timer_finish_btn, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_t * fn_lbl = lv_label_create(timer_finish_btn);
    lv_label_set_text(fn_lbl, "Finish");
    lv_obj_center(fn_lbl);
    lv_obj_set_style_text_color(fn_lbl, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_opa(fn_lbl, LV_OPA_COVER, LV_PART_MAIN); // 确保可见
    lv_obj_add_flag(fn_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(timer_finish_btn, controller_on_timer_finish, LV_EVENT_CLICKED, NULL);
    timer_ctx->finish_btn = timer_finish_btn;

    // Start: Primary button (middle column, double width)
    lv_obj_t * timer_start_pause_btn = lv_btn_create(btn_grid);
    ui_style_set_btn_primary(timer_start_pause_btn);
    lv_obj_set_width(timer_start_pause_btn, LV_PCT(35));
    lv_obj_set_grid_cell(timer_start_pause_btn, LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_add_event_cb(timer_start_pause_btn, controller_on_timer_start_pause, LV_EVENT_CLICKED, NULL);
    lv_obj_t * sp_lbl = lv_label_create(timer_start_pause_btn);
    lv_label_set_text(sp_lbl, "Start");
    lv_obj_center(sp_lbl);
    timer_ctx->start_pause_btn = timer_start_pause_btn;

    // Skip: Outline button
    lv_obj_t * timer_skip_btn = lv_btn_create(btn_grid);
    ui_style_set_btn_secondary(timer_skip_btn);
    lv_obj_set_width(timer_skip_btn, LV_PCT(30));
    lv_obj_set_grid_cell(timer_skip_btn, LV_GRID_ALIGN_END, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_add_event_cb(timer_skip_btn, controller_on_timer_skip, LV_EVENT_CLICKED, NULL);
    lv_obj_t * sk_lbl = lv_label_create(timer_skip_btn);
    lv_label_set_text(sk_lbl, "Skip");
    lv_obj_center(sk_lbl);
    lv_obj_set_style_text_color(sk_lbl, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_opa(sk_lbl, LV_OPA_COVER, LV_PART_MAIN); // 确保可见
    lv_obj_add_flag(sk_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    timer_ctx->skip_btn = timer_skip_btn;

    /* 页面刚建好，必须从 model 把状态水合回来。
     * 否则直接点 Timer 标签进来永远是 "None / -- / --"，
     * 计时中切走再切回来还会把按钮显示成 Start —— 而后台其实还在跑。 */
    bool running = todolist_model_timer_is_running(g_todolist_app.model);
    todolist_view_timer_sync(todolist_model_get_active_task(g_todolist_app.model), running);

    if (running && g_todolist_app.controller) {
        controller_timer_resume_polling(g_todolist_app.controller);
    }
}

/* ── 刷新 ─────────────────────────────────────────────────────────────── */

void todolist_view_timer_sync(const todolist_task_t *t, bool running) {
    TodoListViewTimerCtx *timer_ctx = timer_ctx_get();
    if (!timer_ctx) return;

    if(!t) {
        lv_label_set_text(timer_ctx->task_name, "None");
        lv_label_set_text(timer_ctx->estimate, "--");
        lv_label_set_text(timer_ctx->countdown, "--");
        lv_bar_set_value(timer_ctx->progress_bar, 0, LV_ANIM_OFF);
    } else {
        lv_label_set_text_fmt(timer_ctx->task_name, "%s", t->title);
        lv_label_set_text_fmt(timer_ctx->estimate, "%u", (unsigned int)(t->estimate_s / 60));

        uint32_t elapsed = todolist_model_timer_elapsed_s(g_todolist_app.model, t);
        uint32_t total_s = t->estimate_s;
        uint32_t rem_s = (elapsed >= total_s) ? 0 : (total_s - elapsed);

        char buf[16];
        format_remaining(buf, sizeof(buf), rem_s);
        lv_label_set_text(timer_ctx->countdown, buf);

        lv_bar_set_value(timer_ctx->progress_bar, progress_value(elapsed, total_s), LV_ANIM_OFF);
    }

    lv_obj_t * lbl = lv_obj_get_child(timer_ctx->start_pause_btn, 0);
    if(lbl) lv_label_set_text(lbl, running ? "Pause" : "Start");

    /* 整页都变了：打一帧全屏基准，然后把窗口重新钉回倒计时区。
     * helper 内部走 lv_async_call，可以安全地在页面构建/事件回调里调用。 */
    epd_region_begin_focus(lv_screen_active(), timer_ctx->zone_countdown);
}

void todolist_view_timer_update(todolist_task_t *t) {
    todolist_view_timer_sync(t, false);
}

// 局部刷新：只更新倒计时文本和进度条 (由 Controller 的变化检测驱动)
void todolist_view_timer_refresh_progress(const todolist_task_t *t) {
    if (!t) return;

    TodoListViewTimerCtx *timer_ctx = timer_ctx_get();
    if (!timer_ctx) return;

    TodoListViewHomeCtx *home_ctx = (TodoListViewHomeCtx*)g_todolist_app.view->page_nav.nav_ctx;
    if (!home_ctx->tab || lv_tab_get_active(home_ctx->tab) != 1) return;

    uint32_t elapsed = todolist_model_timer_elapsed_s(g_todolist_app.model, t);
    uint32_t total_s = t->estimate_s;

    uint32_t progress = progress_value(elapsed, total_s);

    uint32_t rem_s = (elapsed >= total_s) ? 0 : (total_s - elapsed);
    char buf[16];
    format_remaining(buf, sizeof(buf), rem_s);
    bool text_changed = strcmp(lv_label_get_text(timer_ctx->countdown), buf) != 0;
    bool bar_changed = lv_bar_get_value(timer_ctx->progress_bar) != (int32_t)progress;
    if (!text_changed && !bar_changed) return;
    if (bar_changed) lv_bar_set_value(timer_ctx->progress_bar, progress, LV_ANIM_OFF);
    if (text_changed) lv_label_set_text(timer_ctx->countdown, buf);

    /* 只把这一块推到面板，不整屏刷 */
    epd_region_flush_obj(timer_ctx->zone_countdown);
}

void todolist_view_timer_set_running(bool running) {
    TodoListViewTimerCtx *timer_ctx = timer_ctx_get();
    if (!timer_ctx || !timer_ctx->start_pause_btn) return;

    lv_obj_t * lbl = lv_obj_get_child(timer_ctx->start_pause_btn, 0);
    if (!lbl) return;
    const char *text = running ? "Pause" : "Start";
    if (strcmp(lv_label_get_text(lbl), text) == 0) return;
    lv_label_set_text(lbl, text);

    /* The display whitelist is frame-wide, not per object. Include the final
     * countdown and the button together; the next running tick narrows it
     * back to the countdown. Paused values produce no further invalidation. */
    lv_obj_update_layout(timer_ctx->zone_countdown);
    lv_area_t area, button;
    lv_obj_get_coords(timer_ctx->zone_countdown, &area);
    lv_obj_get_coords(timer_ctx->start_pause_btn, &button);
    area.x1 = LV_MIN(area.x1, button.x1);
    area.y1 = LV_MIN(area.y1, button.y1);
    area.x2 = LV_MAX(area.x2, button.x2);
    area.y2 = LV_MAX(area.y2, button.y2);
    epd_region_flush_area(&area);
    lv_obj_invalidate(timer_ctx->start_pause_btn);
}

void todolist_view_timer_init(TodoListView* v) {
    (void)v;
}

void todolist_view_timer_deinit(TodoListView* v) {
    (void)v;
}
