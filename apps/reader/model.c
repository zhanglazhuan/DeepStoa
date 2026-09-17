#include <string.h>
#include <stdio.h>
#include "esp_system.h"

#include "model.h"
#include "storage.h"
#include "app.h"

// Mock: 用于演示的假数据。实际项目中这里应该替换为 POSIX fopen/fread API
static const char* mock_txt_content =
    "Chapter 1\n\nIt is a truth universally acknowledged, that a single man in possession "
    "of a good fortune, must be in want of a wife.\n\n"
    "However little known the feelings or views of such a man may be on his first entering "
    "a neighbourhood, this truth is so well fixed in the minds of the surrounding families, "
    "that he is considered the rightful property of some one or other of their daughters.\n\n"
    "(... Imagine 100KB of text here ...)";

void reader_model_init(ReaderApp *app) {
    if (!app) return;
    app->model = malloc(sizeof(ReaderModel));
    if (!app->model) return;
    memset(app->model, 0, sizeof(ReaderModel));

    // 1. 从 Flash 读取配置
    ReaderAppConfig cfg;
    reader_storage_load_config(&cfg);

    // 2. 将持久化的配置应用到 Model (需要在 model.h 中补充这几个字段)
    app->model->font_family = cfg.font_family;
    app->model->font_size_level = cfg.font_size_level;
    app->model->line_spacing = cfg.line_spacing;
    app->model->active_book_idx = cfg.active_book_idx;
    app->model->mem_offset = cfg.mem_offset;
    app->model->last_page = cfg.last_page;
    app->model->lib_layout = cfg.layout;
    app->model->sort_method = cfg.sort_method;
    app->model->page_index = cfg.page_index == 0 ? 1 : cfg.page_index; // 容错，确保页码最小为1

    reader_storage_load_library(app->model);

    // 初始化时若已有打开的书籍，计算总页数估算值
    if (app->model->active_book_idx >= 0 && app->model->active_book_idx < app->model->book_count) {
        app->model->page_total = (app->model->books[app->model->active_book_idx].total_size / 500) + 1;
    } else {
        app->model->page_total = 1;
    }
}

void reader_model_deinit(ReaderApp *app) {
    if (app->model) {
        free(app->model);
        app->model = NULL;
    }
}

// 新增：将当前 Model 的状态同步持久化到 FlashDB
void reader_model_sync_config(ReaderModel *model) {
    ReaderAppConfig cfg;
    cfg.font_family = model->font_family;
    cfg.font_size_level = model->font_size_level;
    cfg.line_spacing = model->line_spacing;
    cfg.active_book_idx = model->active_book_idx;
    cfg.mem_offset = model->mem_offset;
    cfg.last_page = model->last_page;
    cfg.layout = model->lib_layout;
    cfg.sort_method = model->sort_method;
    cfg.page_index = model->page_index;

    reader_storage_save_config(&cfg);
}

bool reader_model_open_book(ReaderModel *model, uint8_t idx) {
    if (idx >= model->book_count) return false;

    // 如果打开的是一本新书，重置进度。如果是当前正在看的书，保留当前进度
    if (model->active_book_idx != idx) {
        model->active_book_idx = idx;
        model->mem_offset = 0;
        model->page_index = 1; // 切换新书籍，重置页码
    }

    // 每次打开书时重新估算一下总页数 (初期假设固定为 500 字节一页)
    model->page_total = (model->books[idx].total_size / 500) + 1;

    model->last_page = 1; // 1 表示 Reading 页面
    reader_model_sync_config(model); // 切换书籍后保存状态
    return true;
}

void reader_model_read_page(ReaderModel *model) {
    if (model->active_book_idx < 0) return;
    ReaderBookInfo *book = &model->books[model->active_book_idx];

    // 实际项目：使用 fseek(file, model->mem_offset, SEEK_SET) 和 fread(...)
    uint32_t remain = book->total_size - model->mem_offset;
    uint32_t to_read = remain > (PAGE_BUFFER_SIZE - 1) ? (PAGE_BUFFER_SIZE - 1) : remain;

    strncpy(model->page_buffer, mock_txt_content + model->mem_offset, to_read);
    model->page_buffer[to_read] = '\0'; // 确保字符串结束
}

void reader_model_set_offset_by_percent(ReaderModel *model, uint8_t percent) {
    if (model->active_book_idx < 0) return;
    ReaderBookInfo *book = &model->books[model->active_book_idx];
    if (percent > 100) percent = 100;
    model->mem_offset = (book->total_size * percent) / 100;

    // 拖动进度条后，根据大致的单页字节数粗略重算一下当前页码
    uint32_t avg_bytes_per_page = 500;
    model->page_index = (model->mem_offset / avg_bytes_per_page) + 1;
}

uint8_t reader_model_get_progress_percent(ReaderModel *model) {
    if (model->active_book_idx < 0) return 0;
    ReaderBookInfo *book = &model->books[model->active_book_idx];
    if (book->total_size == 0) return 0;
    return (uint8_t)((model->mem_offset * 100) / book->total_size);
}

void reader_model_next_page(ReaderModel *model, uint32_t bytes_rendered) {
    ReaderBookInfo *book = &model->books[model->active_book_idx];
    if (model->mem_offset + bytes_rendered < book->total_size) {
        model->mem_offset += bytes_rendered;
        model->page_index++;
    }
}

void reader_model_prev_page(ReaderModel *model, uint32_t bytes_rendered) {
    // 这里的回退逻辑在实际排版引擎中比较复杂。最小实现：简单回退固定字节。
    if (model->mem_offset > bytes_rendered) {
        model->mem_offset -= bytes_rendered;
        if (model->page_index > 1) model->page_index--;
    } else {
        model->mem_offset = 0;
        model->page_index = 1;
    }
}

void reader_model_toggle_font_size(ReaderModel *model) {
    model->font_size_level = (model->font_size_level + 1) % 3;
    reader_model_sync_config(model); // 字体改变后保存状态
}
