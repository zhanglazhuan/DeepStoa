// Clock main page: tab composition and navigation registration.
#include "../controller.h"
#include "../model.h"
#include "../view.h"
#include "lv_page.h"
#include "lv_tab.h"
#include "view_alarm.h"
#include "view_timer.h"
#include "view_main.h"

/* ── 页面 builder ────────────────────────────────────────────── */

static lv_obj_t *build_main_page(ClockApp *app, void *user_data)
{
    (void)user_data;

    Page page = lv_page_create("Clock", false, page_navigator_navigate_back,
                               &app->view->page_nav);

    static const char *tab_names[] = {"Alarm", "Timer"};
    lv_tab_t *tab = lv_tab_create(page.container, tab_names, 2, 40);
    lv_obj_set_style_radius(lv_tab_get_tab_bar(tab), 0, 0);  /* 直角 tabbar，不要倒角 */
    lv_obj_t *tab_root = lv_tab_get_root(tab);
    lv_obj_set_flex_grow(tab_root, 1);
    lv_tab_set_changed_cb(tab, clock_controller_on_tab_changed, app);

    lv_obj_t *alarm_tab = lv_tab_add_page(tab);
    clock_view_alarm_build(alarm_tab, app);

    lv_obj_t *timer_tab = lv_tab_add_page(tab);
    clock_view_timer_build(timer_tab, app);

    lv_tab_finalize(tab);
    lv_tab_set_active(tab, app->model->active_tab);

    /* 计时器可能在 app 关闭期间一直在跑 —— 进来先把当前状态画对，
     * 再挂上刷新定时器。 */
    clock_view_refresh_timer(app);
    clock_controller_timer_ui_attach(app);

    return page.screen;
}

void clock_view_main_init_registry(struct ClockApp *app)
{
    PAGE_REGISTE(app, PAGE_CLOCK_MAIN, build_main_page);
}
