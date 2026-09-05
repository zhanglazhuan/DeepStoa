#include <lvgl.h>
#include "lv_ui_style_guide.h"
#include "view_stats.h"
#include "../view.h"

// 时间范围切换回调
static void time_range_cb(lv_event_t *e) {
    AnkiApp *app = lv_event_get_user_data(e);
    // 实现时间范围切换逻辑
}

// 构建统计页面
static lv_obj_t* build_stats_page(AnkiApp* app, void* user_data) {
    Page page = lv_page_create("Stats", true, page_navigator_navigate_back, &app->view->page_nav);

    // 时间范围选择器
    lv_obj_t *range_cont = lv_obj_create(page.container);
    lv_obj_set_size(range_cont, LV_PCT(100), 60);
    lv_obj_align(range_cont, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_flex_flow(range_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(range_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    const char* range_labels[] = {"1 Month", "3 Months", "1 Year", "All"};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *btn = lv_btn_create(range_cont);
        ui_style_set_btn_secondary(btn);
        lv_obj_set_size(btn, 80, 40);
        lv_obj_add_event_cb(btn, time_range_cb, LV_EVENT_CLICKED, app);
        
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, range_labels[i]);
        lv_obj_center(label);
    }

    // 学习量统计图表区域
    lv_obj_t *chart_cont = lv_obj_create(page.container);
    lv_obj_set_size(chart_cont, LV_PCT(96), LV_PCT(70));
    lv_obj_align(chart_cont, LV_ALIGN_TOP_MID, 0, 80);
    
    // Simulated chart (real chart should be drawn based on data in actual application)
    lv_obj_t *chart_label = lv_label_create(chart_cont);
    lv_label_set_text(chart_label, "Daily Study Statistics Chart");
    lv_obj_set_style_text_align(chart_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(chart_label, LV_ALIGN_CENTER, 0, 0);

    // 统计摘要
    lv_obj_t *summary_cont = lv_obj_create(page.container);
    lv_obj_set_size(summary_cont, LV_PCT(100), 80);
    lv_obj_align(summary_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(summary_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(summary_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    lv_obj_t *total_cards_label = lv_label_create(summary_cont);
    lv_label_set_text(total_cards_label, "Total: 1234 cards");
    
    lv_obj_t *avg_cards_label = lv_label_create(summary_cont);
    lv_label_set_text(avg_cards_label, "Daily Avg: 12 cards");

    return page.screen;
}

void anki_view_stats_init_registry(struct AnkiApp* app) {
    PAGE_REGISTE(app, PAGE_STATS, build_stats_page);
}