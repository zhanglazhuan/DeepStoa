#ifndef TODOLIST_VIEW_H
#define TODOLIST_VIEW_H

#include <lvgl.h>

#include "theme/lv_theme_hardcore.h"
#include "framework/page_navigator.h"
#include "widgets/lv_page.h"
#include "widgets/lv_bottom_sheet.h"
#include "widgets/lv_num_input.h"
#include "widgets/lv_keyboard.h"
#include "utils/ui_fonts.h"

#include "model.h"
#include "app.h"

#ifdef __cplusplus
extern "C" {
#endif

struct TodoListApp;

#define TODOLIST_PAGE_ID_MAX 6

enum todolist_page_id_t{
    PAGE_NONE = 0,
    PAGE_LAUNCH,
    PAGE_HOME,
    PAGE_TASK_FORM,
    PAGE_SETTINGS,
    PAGE_ABOUT
};

typedef struct TodoListViewTimerCtx {
    lv_obj_t * task_name;
    lv_obj_t * estimate;
    lv_obj_t * countdown;
    lv_obj_t * progress_bar;
    lv_obj_t * zone_countdown;   /* 倒计时 + 进度条所在的容器，墨水屏局部刷新窗口就钉在它上面 */
    lv_obj_t * start_pause_btn;
    lv_obj_t * skip_btn;
    lv_obj_t * finish_btn;
} TodoListViewTimerCtx;

#define DRAG_THRESHOLD_PX 6

typedef struct {
    lv_obj_t * row;
    int32_t    from_index;
    lv_coord_t start_y;
} TodoListDragCtx;

typedef struct TodoListViewHomeCtx {
    struct _lv_tab_t *tab;  /* lv_tab_t — custom tab widget */

    lv_obj_t * tab_todo;
    lv_obj_t * tab_timer;
    lv_obj_t * tab_done;

    /* Timer tab 的控件引用内嵌在这里，不再单独 malloc。
     * 原来它是堆分配的，而 page_navigator 只会 free nav_ctx 本体（也就是本结构体），
     * 嵌套的那块没人管 —— 每进一次 Timer 页漏一次。内嵌之后没有东西需要单独释放，
     * 顺带也消除了「页面销毁回调和退出流程都想 free 它」的二次释放风险。
     * timer_ctx 只是个「Timer tab 当前建起来了没有」的标记。 */
    TodoListViewTimerCtx   timer_ctx_storage;
    TodoListViewTimerCtx * timer_ctx;

    lv_obj_t * todo_list;
    lv_obj_t * done_list;

    TodoListDragCtx drag_ctx;
} TodoListViewHomeCtx;

typedef struct TodoListView {
    page_navigator_t page_nav;
} TodoListView;

void todolist_view_init(TodoListApp* app);
void todolist_view_deinit(TodoListApp* app);

// 兼容原函数声明
void todolist_view_build_main(TodoListApp* app, TodoListView* v);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TODOLIST_VIEW_H */