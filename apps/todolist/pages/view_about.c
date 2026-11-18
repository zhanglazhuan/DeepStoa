#include <stdio.h>
#include "esp_app_desc.h"

#include "view_about.h"
#include "../view.h"
#include "../app.h"

extern TodoListApp g_todolist_app;

void view_open_about(void) {
    TodoListApp* app = &g_todolist_app;
    PAGE_NAVIGATE_TO(app, PAGE_ABOUT, NULL);
}

// 辅助函数：创建小节标题
static lv_obj_t* create_section_title(lv_obj_t* parent, const char* title_text) {
    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, title_text);
    /* 1bpp 墨水屏上没有颜色可用：小节标题靠字号和留白区分，不要用蓝色 —— 
     * 它要么被抖动成噪点，要么直接变成纯黑，两种结果都比不上加粗留白。 */
    lv_obj_set_style_text_font(title, LV_FONT_NORMAL, 0);
    lv_obj_set_style_pad_top(title, 16, 0);
    lv_obj_set_style_pad_bottom(title, 8, 0);
    return title;
}

// 辅助函数：创建正文信息
static lv_obj_t* create_info_label(lv_obj_t* parent, const char* info_text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, info_text);
    lv_obj_set_style_text_font(label, LV_FONT_SMALL, 0); // 正文字体
    lv_obj_set_style_pad_bottom(label, 6, 0);
    // 让长文本能够自动换行
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(100)); 
    return label;
}

static lv_obj_t* build_about_page(TodoListApp* app) {
    Page page = lv_page_create("About", true, page_navigator_navigate_back, &app->view->page_nav);
    lv_obj_t* content_cont = page.container;
    lv_obj_set_style_pad_all(content_cont, 16, 0);
    lv_obj_set_flex_flow(content_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    
    // ==========================================
    // 章节 1：How to use 教程
    // ==========================================
    create_section_title(content_cont, "How to use");
    /* 方向必须与 controller_todo.c 的实现一致：LV_DIR_LEFT 走删除，LV_DIR_RIGHT 走计时 */
    create_info_label(content_cont, "- Click '+' button to add a new task.");
    create_info_label(content_cont, "- Tap a task, or swipe RIGHT, to start its timer.");
    create_info_label(content_cont, "- Swipe LEFT on a task to delete it.");
    create_info_label(content_cont, "- Click the checkbox to mark it as Done.");

    // 添加一条灰色的分割线 (用一个高度1px的矩形代替)
    lv_obj_t* divider = lv_obj_create(content_cont);
    lv_obj_set_size(divider, LV_PCT(30), 1);
    lv_obj_set_style_bg_color(divider, lv_color_black(), 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_margin_top(divider, 16, 0);

    // ==========================================
    // 章节 2：About todolist 信息
    // ==========================================
    create_section_title(content_cont, "About TodoList");

    /* 版本号取固件自己的信息，不要硬编码 —— 手写的版本号在量产机上一定会和实际固件对不上 */
    const esp_app_desc_t *desc = esp_app_get_description();
    char buf[96];
    snprintf(buf, sizeof(buf), "Version: %s", desc ? desc->version : "unknown");
    create_info_label(content_cont, buf);
    snprintf(buf, sizeof(buf), "Built: %s %s", desc ? desc->date : "?", desc ? desc->time : "");
    create_info_label(content_cont, buf);
    
    return page.screen;
}

void todolist_view_about_init_registry(TodoListApp* app) {
    PAGE_REGISTE(app, PAGE_ABOUT, build_about_page);
}
