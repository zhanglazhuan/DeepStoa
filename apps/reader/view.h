#ifndef READER_VIEW_H
#define READER_VIEW_H

#include "app.h"
#include "lv_theme_hardcore.h"
#include "lv_page.h"
#include "lv_bottom_sheet.h"
#include "ui_fonts.h"

#define READER_PAGE_ID_MAX 4

// 页面定义
enum reader_page_id_t {
    PAGE_NONE = 0,
    PAGE_LAUNCH,
    PAGE_LIBRARY, // 书库页面
    PAGE_READING  // 阅读页面
};

typedef struct ReaderViewCtx {
    // 阅读页上下文引用，用于 controller 触发刷新
    lv_obj_t *header;
    lv_obj_t *content_label;
    lv_obj_t *progress_slider;
    lv_obj_t *progress_label;
    lv_obj_t *font_btn_label;
    lv_obj_t *bottom_bar;
    lv_obj_t *page_index_label;
    uint32_t last_rendered_bytes;
} ReaderViewCtx;

typedef struct ReaderView {
    page_navigator_t page_nav;
    ReaderViewCtx ctx;
} ReaderView;

void reader_view_init(ReaderApp *app);
void reader_view_deinit(ReaderApp *app);

// 对外暴露给 Controller 调用的刷新方法
void reader_view_refresh_read_page(ReaderApp *app);
uint32_t reader_view_get_rendered_bytes(ReaderView *view);

#endif  // READER_VIEW_H
