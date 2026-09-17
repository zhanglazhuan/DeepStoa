#include <lvgl.h>
#include "esp_log.h"
#include <stdlib.h> // 引入 qsort

#include "view_library.h"
#include "../view.h"
#include "../controller.h"
#include "../model.h"

static const char *TAG = "reader_view_library";

// --- 状态与枚举定义 ---
// 【修改1】把 LAYOUT_CARD 改名为 LAYOUT_GRID
typedef enum { LAYOUT_LIST, LAYOUT_GRID } lib_layout_t;
typedef enum { SORT_TIME_DESC, SORT_TIME_ASC, SORT_AZ_ASC, SORT_AZ_DESC } lib_sort_t;

// 唯一外部访问的容器对象
static lv_obj_t *book_container = NULL;

// --- 排序回调函数 ---
static int cmp_time_desc(const void *a, const void *b) {
    return ((ReaderBookInfo*)b)->open_time - ((ReaderBookInfo*)a)->open_time;
}
static int cmp_time_asc(const void *a, const void *b) {
    return ((ReaderBookInfo*)a)->open_time - ((ReaderBookInfo*)b)->open_time;
}
static int cmp_az_asc(const void *a, const void *b) {
    return strcmp(((ReaderBookInfo*)a)->title, ((ReaderBookInfo*)b)->title);
}
static int cmp_az_desc(const void *a, const void *b) {
    return strcmp(((ReaderBookInfo*)b)->title, ((ReaderBookInfo*)a)->title);
}

// 包装函数：处理书籍点击
static void book_clicked_wrapper(lv_event_t *e) {
    lv_obj_t *target = lv_event_get_current_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(target);
    reader_controller_on_book_clicked(e);
}

// --- 核心渲染函数 ---
static void draw_books(ReaderApp *app) {
    if (!book_container) return;

    // 1. 清空旧的书籍节点
    lv_obj_clean(book_container);

    // 2. 排序
    if (app->model->book_count > 0 && app->model->sort_method >= SORT_TIME_DESC && app->model->sort_method <= SORT_AZ_DESC) {
        int (*cmp_func)(const void*, const void*) = cmp_time_desc;
        switch((lib_sort_t)app->model->sort_method) {
            case SORT_TIME_DESC: cmp_func = cmp_time_desc; break;
            case SORT_TIME_ASC:  cmp_func = cmp_time_asc;  break;
            case SORT_AZ_ASC:    cmp_func = cmp_az_asc;    break;
            case SORT_AZ_DESC:   cmp_func = cmp_az_desc;   break;
        }
        qsort(app->model->books, app->model->book_count, sizeof(ReaderBookInfo), cmp_func); // 使用 app->model->books
    }

    // 3. 生成节点
    if (app->model->lib_layout == LAYOUT_LIST) {
        lv_obj_set_flex_flow(book_container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(book_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

        for (int i = 0; i < app->model->book_count; i++) {
            lv_obj_t *btn = lv_btn_create(book_container);
            lv_obj_set_size(btn, LV_PCT(100), 60);
            lv_obj_set_style_pad_all(btn, 15, 0);

            lv_obj_t *label = lv_label_create(btn);
            lv_obj_set_width(label, LV_PCT(100));
            lv_obj_set_height(label, lv_font_get_line_height(lv_obj_get_style_text_font(label, 0)));
            lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
            lv_label_set_text_fmt(label, "%s  %s", LV_SYMBOL_FILE, app->model->books[i].title);
            lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

            lv_obj_set_user_data(btn, (void*)(intptr_t)i);
            lv_obj_add_event_cb(btn, book_clicked_wrapper, LV_EVENT_CLICKED, app);
        }
    } else if (app->model->lib_layout == LAYOUT_GRID) { // 检查是否为网格布局
        // 网格布局 (Grid)
        lv_obj_set_flex_flow(book_container, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(book_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_column(book_container, 10, 0);
        lv_obj_set_style_pad_top(book_container, 10, 0);
        lv_obj_set_style_pad_bottom(book_container, 20, 0);
        lv_obj_set_style_pad_row(book_container, 20, 0);

        for (int i = 0; i < app->model->book_count; i++) {
            lv_obj_t *btn = lv_btn_create(book_container);
            lv_obj_set_size(btn, 136, 184);
            lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t *icon = lv_label_create(btn);
            lv_label_set_text(icon, LV_SYMBOL_FILE);

            lv_obj_t *label = lv_label_create(btn);
            lv_label_set_text(label, app->model->books[i].title);
            lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
            lv_obj_set_width(label, LV_PCT(100));
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

            lv_obj_set_user_data(btn, (void*)(intptr_t)i);
            lv_obj_add_event_cb(btn, book_clicked_wrapper, LV_EVENT_CLICKED, app);
        }
    }
}

// --- 事件回调 ---
static void layout_btn_event_cb(lv_event_t * e) {
    ReaderApp *app = lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_current_target(e);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    // 更新 model 中的布局
    app->model->lib_layout = (app->model->lib_layout == LAYOUT_LIST) ? LAYOUT_GRID : LAYOUT_LIST;
    // 更新按钮显示
    lv_label_set_text(label, app->model->lib_layout == LAYOUT_LIST ? LV_SYMBOL_LIST: MY_SYMBOL_GRID);
    // 同步到 flash
    reader_model_sync_config(app->model);

    draw_books(app);
}

// 【修改3】处理 Dropdown 的值改变事件
static void sort_dropdown_event_cb(lv_event_t * e) {
    ReaderApp *app = lv_event_get_user_data(e);
    lv_obj_t *dropdown = lv_event_get_current_target(e);

    // 获取下拉框当前选中的索引，正好对应我们 lib_sort_t 的枚举值 (0~3)
    app->model->sort_method = lv_dropdown_get_selected(dropdown);
    // 同步到 flash
    reader_model_sync_config(app->model);

    draw_books(app);
}

// --- 书库页面构建 ---
static lv_obj_t* build_library_page(ReaderApp* app, void* user_data) {
    Page page = lv_page_create("Library", false, page_navigator_navigate_back, &app->view->page_nav);

    lv_obj_set_flex_flow(page.container, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(page.container, LV_OBJ_FLAG_SCROLLABLE);

    // 1. 创建顶部控制栏
    lv_obj_t *top_bar = lv_obj_create(page.container);
    lv_obj_set_width(top_bar, LV_PCT(100));
    lv_obj_set_height(top_bar, 50);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_flex_flow(top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(top_bar, 10, 0);
    lv_obj_set_style_pad_gap(top_bar, 10, 0);

    // 1.1 排序下拉菜单 (替代之前的 btn_sort)
    lv_obj_t *dd_sort = lv_dropdown_create(top_bar);
    // 选项顺序必须与 lib_sort_t 枚举严格对应 (0: Time-, 1: Time+, 2: A-Z+, 3: Z-A-)
    lv_dropdown_set_options(dd_sort,
                            "Time-\n"
                            "Time+\n"
                            "A-Z+\n"
                            "Z-A-"); // 选项顺序必须与 lib_sort_t 枚举严格对应
    lv_dropdown_set_selected(dd_sort, app->model->sort_method); // 设置默认选中项
    lv_obj_set_width(dd_sort, 140); // 稍微限制下宽度防止挤占空间
    // 监听下拉选项改变的事件
    lv_obj_add_event_cb(dd_sort, sort_dropdown_event_cb, LV_EVENT_VALUE_CHANGED, app);

    // 1.2 布局切换按钮
    lv_obj_t *btn_layout = lv_btn_create(top_bar);
    lv_obj_set_width(btn_layout, 50); // 固定宽度
    lv_obj_add_event_cb(btn_layout, layout_btn_event_cb, LV_EVENT_CLICKED, app);
    lv_obj_t *label_layout = lv_label_create(btn_layout); // 修正：label_layout 定义
    lv_obj_set_style_text_font(label_layout, &custom_font_normal, 0);
    lv_label_set_text(label_layout, app->model->lib_layout == LAYOUT_LIST ? LV_SYMBOL_LIST: MY_SYMBOL_GRID);
    lv_obj_center(label_layout); // icon 居中

    // 2. 创建承载书籍的容器
    book_container = lv_obj_create(page.container);
    lv_obj_set_width(book_container, LV_PCT(100));
    lv_obj_set_flex_grow(book_container, 1);
    lv_obj_set_style_bg_opa(book_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(book_container, 0, 0);
    lv_obj_add_flag(book_container, LV_OBJ_FLAG_SCROLLABLE);

    // 3. 初始渲染
    draw_books(app);

    return page.screen;
}

void reader_view_library_init_registry(struct ReaderApp* app) {
    PAGE_REGISTE(app, PAGE_LIBRARY, build_library_page);
}
