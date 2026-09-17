#ifndef CLOCK_VIEW_H
#define CLOCK_VIEW_H

#include <lvgl.h>

#include "lv_page.h"
#include "page_navigator.h"
#include "lv_theme_hardcore.h"

#include "alarm_service.h"
#include "clock_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Page IDs ─────────────────────────────────────────────────── */
#define CLOCK_PAGE_ID_MAX  6

typedef enum {
    PAGE_CLOCK_NONE = 0,
    PAGE_CLOCK_MAIN,        /* tabview: Alarm + Timer */
    PAGE_ALARM_EDIT,        /* add / edit a single alarm */
    PAGE_TIMER_PRESET_EDIT, /* Timer quick settings edit page */
} ClockPageId;

/* ── Alarm tab context ───────────────────────────────────────── */
typedef struct AlarmTabCtx {
    lv_obj_t *list;
    lv_obj_t *empty_label;
    lv_obj_t *add_btn;
} AlarmTabCtx;

/* ── Timer tab context ───────────────────────────────────────── */
typedef struct TimerTabCtx {
    /* 主角：大号剩余时间 + 一行小字说明 + 进度条 */
    lv_obj_t *hero_box;       /* 三者的容器，局部刷新窗口钉在它身上 */
    lv_obj_t *hero_label;
    lv_obj_t *hero_caption;
    lv_obj_t *hero_bar;       /* 剩余占比，IDLE 时隐藏 */

    /* 时长快捷选项。一次点中，不再用 ±1 一下一下调 ——
     * 每次点击都是一次墨水屏刷新，把 5 分钟调到 25 分钟要闪二十几次。 */
    lv_obj_t *chips[ALARM_PRESET_COUNT];
    lv_obj_t *chip_lbls[ALARM_PRESET_COUNT];
    lv_obj_t *chip_more;

    /* 底部：一个随状态换文案的主按钮，外加只在计时中出现的 Reset */
    lv_obj_t *btn_primary;
    lv_obj_t *btn_primary_label;
    lv_obj_t *btn_reset;

    /* 墨水屏刷新预算：文本没变就不重设 label（lv_label_set_text 不比较旧值，
     * 一次标脏就是一次整屏推送），并记住上一次的状态用来判断要不要重建
     * 局部刷新基准帧。 */
    char                last_text[16];
    char                last_caption[24];
    int                 last_bar_pct;   /* 上一次推上屏的进度，-1 = 还没推过 */
    alarm_timer_state_t last_state;
    bool                region_on;
} TimerTabCtx;

typedef struct TimerPresetEditCtx {
    /* 一次性的任意时长：输入分钟数直接开始，不用先改快捷键 */
    lv_obj_t *ta_custom;
    /* 4 个快捷时长 */
    lv_obj_t *ta_presets[ALARM_PRESET_COUNT];
    lv_obj_t *btn_save;
} TimerPresetEditCtx;

/* ── Alarm-edit page context ─────────────────────────────────── */
typedef struct AlarmEditCtx {
    lv_obj_t *ta_hour;
    lv_obj_t *ta_min;
    lv_obj_t *repeat_btns[7];  /* Mon Tue Wed Thu Fri Sat Sun */
    lv_obj_t *label_input;     /* 目前未启用，保存时会保留原标签 */
    lv_obj_t *btn_save;
    lv_obj_t *btn_delete;
} AlarmEditCtx;

/* ── Main view ───────────────────────────────────────────────── */
typedef struct ClockView {
    page_navigator_t page_nav;
    AlarmTabCtx      alarm_ctx;
    TimerTabCtx      timer_ctx;
    AlarmEditCtx     edit_ctx;
    TimerPresetEditCtx timer_preset_ctx;
} ClockView;

/* lifecycle */
void clock_view_init(ClockApp *app);
void clock_view_deinit(ClockApp *app);

/* controller -> view */
void clock_view_refresh_alarm_list(ClockApp *app);
void clock_view_refresh_timer(ClockApp *app);

/** 退出 Timer 页时调用：解除局部刷新窗口，恢复整屏模式。 */
void clock_view_timer_leave(ClockApp *app);

#ifdef __cplusplus
}
#endif

#endif /* CLOCK_VIEW_H */
