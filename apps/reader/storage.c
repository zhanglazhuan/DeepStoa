#include "esp_log.h"
#include <string.h>
#include "flash_control.h"
#include "storage.h"
#include "model.h"

static const char *TAG = "reader_storage";

extern struct fdb_kvdb g_kvdb;

bool reader_app_factory_reset(void) {
    struct fdb_blob blob;

    // 1. 初始化并写入全局配置
    ReaderAppConfig default_cfg = {
        .font_family = 0,
        .font_size_level = 1, // 中号
        .line_spacing = 13,   // 1.3
        .sort_method = 0,
        .active_book_idx = -1,
        .mem_offset = 0,
        .last_page = 0, // 默认停留在书架页
        .layout = 1,    // 默认网格布局
        .sort_method = 0 // 默认按时间降序
    };
    fdb_kv_set_blob(&g_kvdb, CONFIG_KV_KEY, fdb_blob_make(&blob, &default_cfg, sizeof(ReaderAppConfig)));

    // 2. 初始化并写入默认书库记录
    ReaderBookInfo default_books[3];
    memset(default_books, 0, sizeof(default_books));

    strcpy(default_books[0].title, "Pride and Prejudice.txt");
    strcpy(default_books[0].filepath, "/sd/pride.txt");
    default_books[0].total_size = 418; // 匹配 mock_txt_content 的实际长度
    default_books[0].open_time = 1610000000;

    strcpy(default_books[1].title, "Zephyr RTOS Guide.txt");
    strcpy(default_books[1].filepath, "/sd/zephyr.txt");
    default_books[1].total_size = 50000;
    default_books[1].open_time = 1620000000;

    strcpy(default_books[2].title, "Good study.txt");
    strcpy(default_books[2].filepath, "/sd/pride.txt");
    default_books[2].total_size = 418; // 匹配 mock_txt_content 的实际长度
    default_books[2].open_time = 1630000000;

    uint32_t book_cnt = 3;
    fdb_kv_set_blob(&g_kvdb, "reader_book_cnt", fdb_blob_make(&blob, &book_cnt, sizeof(book_cnt)));
    fdb_kv_set_blob(&g_kvdb, "reader_books", fdb_blob_make(&blob, default_books, sizeof(default_books)));

    ESP_LOGI(TAG, "Reader factory reset done.");
    return true;
}

void reader_storage_load_config(ReaderAppConfig *cfg) {
    // 从 FlashDB 读取 Blob
    struct fdb_blob blob;
    fdb_blob_make(&blob, cfg, sizeof(ReaderAppConfig));

    size_t read_len = fdb_kv_get_blob(&g_kvdb, CONFIG_KV_KEY, &blob);
    if (read_len == sizeof(ReaderAppConfig)) {
        ESP_LOGI(TAG, "Configuration loaded from FlashDB successfully.");
    } else {
        ESP_LOGW(TAG, "No valid configuration found in FlashDB, performing factory reset.");
        reader_app_factory_reset();
        fdb_kv_get_blob(&g_kvdb, CONFIG_KV_KEY, fdb_blob_make(&blob, cfg, sizeof(ReaderAppConfig)));
    }
}

void reader_storage_load_library(ReaderModel *model) {
    struct fdb_blob blob;
    uint32_t book_cnt = 0;

    // 加载书库数量
    fdb_kv_get_blob(&g_kvdb, "reader_book_cnt", fdb_blob_make(&blob, &book_cnt, sizeof(book_cnt)));
    model->book_count = book_cnt;

    // 视情况加载具体书籍元数据
    if (book_cnt > 0) {
        fdb_kv_get_blob(&g_kvdb, "reader_books", fdb_blob_make(&blob, model->books, sizeof(ReaderBookInfo) * book_cnt));
    }
}

void reader_storage_save_config(const ReaderAppConfig *cfg) {
    struct fdb_blob blob;
    fdb_blob_make(&blob, (void *)cfg, sizeof(ReaderAppConfig));

    fdb_err_t err = fdb_kv_set_blob(&g_kvdb, CONFIG_KV_KEY, &blob);
    if (err == FDB_NO_ERR) {
        ESP_LOGD(TAG, "Configuration saved to FlashDB.");
    } else {
        ESP_LOGE(TAG, "Failed to save configuration! Error code: %d", err);
    }
}
