
#include <lvgl.h>
#include <stdio.h>
#include "esp_log.h"

#include "lv_page.h"
#include "lv_bottom_sheet.h"

#include "view_reading.h"
#include "../view.h"
#include "../controller.h"
#include "../model.h"

static const char *TAG = "reader_view_reading";

// --- 菜单呼出/隐藏回调函数 ---
static void toggle_menu_cb(lv_event_t * e) {
    lv_obj_t * bottom_bar = (lv_obj_t *)lv_event_get_user_data(e);

    // 安全查找 header：遍历 screen 的子节点，不是 cont 的那个就是 header
    lv_obj_t * header = g_reader_app.view->ctx.header;
    if (lv_obj_has_flag(bottom_bar, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(header, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(bottom_bar, LV_OBJ_FLAG_HIDDEN);
        lv_status_bar_set_visible(true);  // 显示状态栏
    } else {
        lv_obj_add_flag(header, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(bottom_bar, LV_OBJ_FLAG_HIDDEN);
        lv_status_bar_set_visible(false);  // 隐藏状态栏
    }
}

// --- 手势翻页回调函数 (支持上下左右滑动) ---
static void reading_gesture_cb(lv_event_t * e) {
    ReaderApp *app = lv_event_get_user_data(e);

    // 【新增拦截】：如果菜单栏 (bottom_bar) 处于显示状态，说明在设置状态，直接忽略手势
    if (app->view->ctx.bottom_bar && !lv_obj_has_flag(app->view->ctx.bottom_bar, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());

    if (dir == LV_DIR_LEFT || dir == LV_DIR_TOP) {
        // 向左划或向上划 -> 下一页
        reader_controller_on_read_next_page(e);
    } else if (dir == LV_DIR_RIGHT || dir == LV_DIR_BOTTOM) {
        // 向右划或向下划 -> 上一页
        reader_controller_on_read_prev_page(e);
    }
}


// --- 字体大小滑块回调 ---
static void font_size_slider_cb(lv_event_t * e) {
    lv_obj_t * slider = lv_event_get_target(e);
    lv_obj_t * label = lv_event_get_user_data(e);

    int val = lv_slider_get_value(slider);
    int font_sizes[] = {16, 20, 24, 28, 32}; // 5个选项

    // 更新上方 Label 的显示
    lv_label_set_text_fmt(label, "Font Size: %d", font_sizes[val]);

    // TODO: 在这里调用 controller 层更新 app->model 的字体大小，并刷新阅读视图
    // ReaderApp *app = (ReaderApp *)lv_obj_get_user_data(slider); // 需要的话可以在创建时绑定
}

// --- 行距滑块回调 ---
static void line_space_slider_cb(lv_event_t * e) {
    lv_obj_t * slider = lv_event_get_target(e);
    lv_obj_t * label = lv_event_get_user_data(e);

    int val = lv_slider_get_value(slider);
    int spaces[] = {11, 13, 15}; // 对应 1.1, 1.3, 1.5

    // 更新上方 Label 的显示 (通过整除和取余实现小数显示)
    lv_label_set_text_fmt(label, "Line Spacing: %d.%d", spaces[val] / 10, spaces[val] % 10);

    // TODO: 在这里调用 controller 层更新 app->model 的行距，并刷新阅读视图
}

// --- 点击字体按钮呼出 Bottom Sheet ---
static void font_settings_btn_cb(lv_event_t * e) {
    ReaderApp * app = lv_event_get_user_data(e);

    // 1. 接收正确的类型：lv_bottom_sheet_t *
    lv_bottom_sheet_t * bs = lv_bottom_sheet_create(lv_screen_active());
    lv_bottom_sheet_add_header(bs, "Font Settings"); // 传入 bs

    // 2. 偏移时，必须操作 bs->sheet，而不是 bs 结构体本身
    lv_obj_align(bs->sheet, LV_ALIGN_BOTTOM_MID, 0, -50);

    // 3. 获取 content
    lv_obj_t * content = lv_bottom_sheet_get_content(bs); // 传入 bs
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 15, LV_PART_MAIN);

    // --- 字体大小设置区域 ---
    lv_obj_t * font_label = lv_label_create(content);
    lv_label_set_text(font_label, "Font Size: 24");

    lv_obj_t * font_slider = lv_slider_create(content);
    lv_obj_set_width(font_slider, LV_PCT(90));
    lv_slider_set_range(font_slider, 0, 4);
    lv_slider_set_value(font_slider, 2, LV_ANIM_OFF);
    lv_obj_clear_flag(font_slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(font_slider, font_size_slider_cb, LV_EVENT_VALUE_CHANGED, font_label);

    // --- 行间距设置区域 ---
    lv_obj_t * space_label = lv_label_create(content);
    lv_label_set_text(space_label, "Line Spacing: 1.3");
    lv_obj_set_style_pad_top(space_label, 15, LV_PART_MAIN);

    lv_obj_t * space_slider = lv_slider_create(content);
    lv_obj_set_width(space_slider, LV_PCT(90));
    lv_slider_set_range(space_slider, 0, 2);
    lv_slider_set_value(space_slider, 1, LV_ANIM_OFF);
    lv_obj_clear_flag(space_slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(space_slider, line_space_slider_cb, LV_EVENT_VALUE_CHANGED, space_label);
}

static void prev_zone_click_cb(lv_event_t * e) {
    ReaderApp *app = lv_event_get_user_data(e);
    if (app->view->ctx.bottom_bar && !lv_obj_has_flag(app->view->ctx.bottom_bar, LV_OBJ_FLAG_HIDDEN)) return;

    reader_controller_on_read_prev_page(e);
}

static void next_zone_click_cb(lv_event_t * e) {
    ReaderApp *app = lv_event_get_user_data(e);
    if (app->view->ctx.bottom_bar && !lv_obj_has_flag(app->view->ctx.bottom_bar, LV_OBJ_FLAG_HIDDEN)) return;

    reader_controller_on_read_next_page(e);
}

// --- 阅读页面构建 ---
static lv_obj_t* build_reading_page(ReaderApp* app, void* user_data) {
    Page page = lv_page_create("Reading", true, reader_controller_on_back_to_library, &app->view->page_nav);

    // 🌟 1. 处理 Header：脱离布局、置于顶层、加白底黑线边框
    app->view->ctx.header = page.header;
    lv_obj_add_flag(page.header, LV_OBJ_FLAG_IGNORE_LAYOUT);     // 关键：脱离 Flex，不挤压文本层
    lv_obj_move_foreground(page.header);                         // 关键：移到 Z 轴最前端
    lv_obj_set_style_bg_color(page.header, lv_color_white(), 0); // 设为白底
    lv_obj_set_style_bg_opa(page.header, LV_OPA_COVER, 0);       // 不透明，遮挡下方文字
    lv_obj_set_style_border_width(page.header, 1, 0);            // 加一条下划线区分层次
    lv_obj_set_style_border_side(page.header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_add_flag(page.header, LV_OBJ_FLAG_HIDDEN);            // 默认隐藏

    // 2. 阅读主区域 (让其填满整个屏幕)
    lv_obj_t* cont = page.container;
    lv_obj_t *text_container = lv_obj_create(cont);
    lv_obj_set_size(text_container, LV_PCT(98), LV_PCT(100)); // 高度 100% 满屏
    lv_obj_align(text_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(text_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(text_container, 0, 0);
    lv_obj_set_style_pad_bottom(text_container, 30, 0); // 为底部页码预留空间避免遮挡

    lv_obj_t *content_label = lv_label_create(text_container);
    lv_obj_set_width(content_label, LV_PCT(100));
    lv_label_set_long_mode(content_label, LV_LABEL_LONG_WRAP);
    app->view->ctx.content_label = content_label;

    // 3. 全局手势支持 (绑定给 cont)
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE); // 禁用默认的页面滚动，防止出现竖向滚动条
    lv_obj_add_event_cb(cont, reading_gesture_cb, LV_EVENT_GESTURE, app);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // 4. 隐形点击热区：左侧 30% (上一页)
    lv_obj_t *prev_zone = lv_obj_create(cont);
    lv_obj_add_flag(prev_zone, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(prev_zone, LV_PCT(30), LV_PCT(100));
    lv_obj_align(prev_zone, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_opa(prev_zone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(prev_zone, 0, 0);
    lv_obj_clear_flag(prev_zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(prev_zone, prev_zone_click_cb, LV_EVENT_CLICKED, app);

    // 5. 隐形点击热区：右侧 30% (下一页)
    lv_obj_t *next_zone = lv_obj_create(cont);
    lv_obj_add_flag(next_zone, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(next_zone, LV_PCT(30), LV_PCT(100));
    lv_obj_align(next_zone, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_opa(next_zone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(next_zone, 0, 0);
    lv_obj_clear_flag(next_zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(next_zone, next_zone_click_cb, LV_EVENT_CLICKED, app);

    // 5.5 页面底部的页码展示 (默认显示在阅读页面底部居中)
    lv_obj_t *page_index_label = lv_label_create(cont);
    lv_obj_add_flag(page_index_label, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING); // 脱离布局排版，悬浮显示
    lv_obj_align(page_index_label, LV_ALIGN_BOTTOM_MID, 0, 0); // 贴近底部
    lv_obj_set_style_text_font(page_index_label, LV_FONT_SMALL, 0); // 使用小字体
    app->view->ctx.page_index_label = page_index_label;

    // 7. 隐形点击热区：中间 40% (点击呼出/隐藏菜单)
    lv_obj_t *center_zone = lv_obj_create(cont);
    lv_obj_add_flag(center_zone, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(center_zone, LV_PCT(40), LV_PCT(100));
    lv_obj_align(center_zone, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_opa(center_zone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(center_zone, 0, 0);
    lv_obj_clear_flag(center_zone, LV_OBJ_FLAG_SCROLLABLE);

    // 6. 底部控制栏：置于顶层，并设置不透明白底
    lv_obj_t *bottom_bar = lv_obj_create(cont);
    lv_obj_add_flag(bottom_bar, LV_OBJ_FLAG_IGNORE_LAYOUT); // 脱离 Flex
    lv_obj_set_style_bg_color(bottom_bar, lv_color_white(), 0); // 设为白底
    lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_COVER, 0);       // 不透明，遮挡文本
    lv_obj_set_style_border_width(bottom_bar, 1, 0);            // 加一条上划线
    lv_obj_set_style_border_side(bottom_bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_size(bottom_bar, LV_PCT(100), 60);
    lv_obj_align(bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(bottom_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_top(bottom_bar, 20, 0); // 清除默认内边距，确保子组件能完美垂直居中
    lv_obj_add_flag(bottom_bar, LV_OBJ_FLAG_HIDDEN); // 默认隐藏
    lv_status_bar_set_visible(false);  // 隐藏状态栏
    app->view->ctx.bottom_bar = bottom_bar;

    // 字体大小按钮
    lv_obj_t *font_btn = lv_btn_create(bottom_bar);
    lv_obj_set_size(font_btn, 40, 30); // 设定固定尺寸避免随内容变形
    lv_obj_align(font_btn, LV_ALIGN_LEFT_MID, 30, 0); // 靠左居中对齐，并预留 15px 边距
    app->view->ctx.font_btn_label = lv_label_create(font_btn);
    lv_label_set_text(app->view->ctx.font_btn_label, "A");
    lv_obj_center(app->view->ctx.font_btn_label); // 字母 A 居中于按钮内
    lv_obj_add_event_cb(font_btn, font_settings_btn_cb, LV_EVENT_CLICKED, app);

    // 进度条 (Slider)
    lv_obj_t *slider = lv_slider_create(bottom_bar);
    lv_obj_set_size(slider, LV_PCT(50), 10);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, 0);
    lv_slider_set_range(slider, 0, 100);
    lv_obj_clear_flag(slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(slider, reader_controller_on_read_slider_changed, LV_EVENT_VALUE_CHANGED, app);
    app->view->ctx.progress_slider = slider;

    // 进度百分比文本
    app->view->ctx.progress_label = lv_label_create(bottom_bar);
    lv_label_set_text(app->view->ctx.progress_label, "0%");
    lv_obj_align(app->view->ctx.progress_label, LV_ALIGN_RIGHT_MID, -15, 0); // 靠右居中对齐

    // 将 center_zone 的事件绑定移至此处，确保 bottom_bar 已经创建
    lv_obj_add_event_cb(center_zone, toggle_menu_cb, LV_EVENT_CLICKED, bottom_bar);

    // 初始加载内容
    reader_view_refresh_read_page(app);

    return page.screen;
}

void reader_view_refresh_read_page(ReaderApp *app) {
    if (!app->view->ctx.content_label) return;

    ReaderModel *m = app->model;

    // 1. 设置字体
    const lv_font_t *font;
    char *font_text;
    switch (m->font_size_level) {
        case 0: font = LV_FONT_SMALL; font_text = "A-"; break; // 小
        case 2: font = LV_FONT_LARGE; font_text = "A+"; break; // 大
        case 1:
        default: font = LV_FONT_NORMAL; font_text = "A"; break; // 中
    }
    lv_obj_set_style_text_font(app->view->ctx.content_label, font, 0);
    lv_label_set_text(app->view->ctx.font_btn_label, font_text);

    // 2. 加载文字
    reader_model_read_page(m);
    lv_label_set_text(app->view->ctx.content_label, m->page_buffer);

    // 3. 计算本页实际渲染了多少字符 (用于精确翻页)
    // 注意：LVGL 没有直接提供获取 CLIP 模式下截断位置的 API。
    // 在工程中，一般通过估算 (容器高度 / 行高 * 每行字符数)，或者强制使用定制的文本断行引擎。
    // 这里做基础估算，防止死循环。
    app->view->ctx.last_rendered_bytes = strlen(m->page_buffer);
    // 假设一页最多显示 500 字节，如果超了说明被 Clip 了
    if (app->view->ctx.last_rendered_bytes > 500) {
        app->view->ctx.last_rendered_bytes = 500;
    }

    // 4. 更新进度条和文本
    uint8_t pct = reader_model_get_progress_percent(m);
    lv_slider_set_value(app->view->ctx.progress_slider, pct, LV_ANIM_OFF);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    lv_label_set_text(app->view->ctx.progress_label, buf);

    // 5. 更新页码展示
    if (app->view->ctx.page_index_label) {
        snprintf(buf, sizeof(buf), "%lu/%lu", (unsigned long)m->page_index, (unsigned long)m->page_total);
        lv_label_set_text(app->view->ctx.page_index_label, buf);
    }
}

uint32_t reader_view_get_rendered_bytes(ReaderView *view) {
    return view->ctx.last_rendered_bytes;
}

void reader_view_reading_init_registry(struct ReaderApp* app) {
    PAGE_REGISTE(app, PAGE_READING, build_reading_page);
}
