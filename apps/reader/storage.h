#ifndef _READER_STORAGE_H_
#define _READER_STORAGE_H_

#include <stdint.h>
#include <stdbool.h>

struct ReaderModel; // 前置声明

#define CONFIG_KV_KEY "reader_cfg"

// 定义持久化配置结构体
typedef struct {
    // 1. 阅读设置
    uint8_t font_family;     // 字体
    uint8_t font_size_level; // 字号
    uint8_t line_spacing;    // 间距 (可以存放大10倍的整数，如 13 代表 1.3)

    // 3. 状态与进度
    int32_t active_book_idx; // 当前阅读的书籍索引 (-1 表示没有)
    uint32_t mem_offset; // 当前书籍的阅读偏移量
    uint8_t last_page;       // 退出时的页面 (如 PAGE_LIBRARY = 0, PAGE_READING = 1)
    uint8_t layout;          // 书架布局 (0: 列表, 1: 网格)
    uint8_t sort_method;     // 书架排序方式 (0: 按时间, 1: 按名称等)
    uint32_t page_index;     // 当前页码
} ReaderAppConfig;

bool reader_app_factory_reset(void);

// 加载配置 (如果 Flash 中没有，会赋予默认值)
void reader_storage_load_config(ReaderAppConfig *cfg);

// 保存配置到 Flash
void reader_storage_save_config(const ReaderAppConfig *cfg);

// 从 FlashDB 加载书库列表
void reader_storage_load_library(struct ReaderModel *model);

#endif
