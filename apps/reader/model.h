#ifndef READER_MODEL_H
#define READER_MODEL_H

#include <stdint.h>
#include <stdbool.h>
#include "app.h"

#define MAX_BOOKS 10
#define MAX_BOOK_NAME 64
#define PAGE_BUFFER_SIZE 1024 // 单页最大读取字节数

typedef struct ReaderBookInfo{
    char title[MAX_BOOK_NAME];
    char filepath[MAX_BOOK_NAME];
    uint32_t total_size;
    uint32_t open_time;
} ReaderBookInfo;

typedef struct ReaderModel {
    ReaderBookInfo books[MAX_BOOKS];
    uint8_t book_count;

    // 书架视图偏好
    uint8_t lib_layout;  // 0: 列表 (LIST), 1: 网格 (GRID)
    uint8_t sort_method;     // 0: 按时间降序, 1: 按时间升序, 2: A-Z 升序, 3: Z-A 降序

    // 阅读缓冲
    char page_buffer[PAGE_BUFFER_SIZE];

    // 1. 阅读设置
    uint8_t font_family;     // 字体样式
    uint8_t font_size_level; // 字号 (例如 0:小, 1:中, 2:大)
    uint8_t line_spacing;    // 行距 (例如 13 表示 1.3 倍)

    // 3. 状态与进度
    int32_t active_book_idx; // 当前正在阅读的书籍索引 (-1 表示无)
    uint32_t mem_offset; // 当前书籍的阅读字节偏移量
    uint8_t last_page;       // 退出 App 时停留的页面 (0: 书架 Library, 1: 阅读页 Reading)
    uint32_t page_index;     // 给人看的页码展示
    uint32_t page_total;     // 总页数估算
} ReaderModel;

void reader_model_init(ReaderApp *app);
void reader_model_deinit(ReaderApp *app);

void reader_model_load_library(ReaderModel *model);
bool reader_model_open_book(ReaderModel *model, uint8_t idx);

// 核心阅读操作
void reader_model_read_page(ReaderModel *model);
void reader_model_set_offset_by_percent(ReaderModel *model, uint8_t percent);
void reader_model_next_page(ReaderModel *model, uint32_t bytes_read);
void reader_model_prev_page(ReaderModel *model, uint32_t bytes_read);
void reader_model_toggle_font_size(ReaderModel *model);
uint8_t reader_model_get_progress_percent(ReaderModel *model);

void reader_model_sync_config(ReaderModel *model);

#endif  // READER_MODEL_H
