#include "storage.h"
#include "model.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "flash_control.h"
#include "fs_control.h"

#define BOOK_DIR "/sdcard/books"
#define POSITION_DIR "/sdcard/bookmarks/reader_progress"

static const char *TAG = "reader_storage";
extern struct fdb_kvdb g_kvdb;

static bool supported_book(const char *name, bool *epub)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    *epub = strcasecmp(dot, ".epub") == 0;
    return *epub || strcasecmp(dot, ".txt") == 0;
}

bool reader_app_factory_reset(void)
{
    ReaderAppConfig cfg = {
        .font_size_level = 1,
        .line_spacing = 13,
        .active_book_idx = -1,
        .layout = 1,
        .page_index = 1,
    };
    reader_storage_save_config(&cfg);
    return true;
}

void reader_storage_load_config(ReaderAppConfig *cfg)
{
    struct fdb_blob blob;
    memset(cfg, 0, sizeof(*cfg));
    size_t n = fdb_kv_get_blob(&g_kvdb, CONFIG_KV_KEY,
                               fdb_blob_make(&blob, cfg, sizeof(*cfg)));
    if (n != sizeof(*cfg)) {
        reader_app_factory_reset();
        fdb_kv_get_blob(&g_kvdb, CONFIG_KV_KEY,
                        fdb_blob_make(&blob, cfg, sizeof(*cfg)));
    }
    if (cfg->font_size_level > 2) cfg->font_size_level = 1;
    if (cfg->layout > 1) cfg->layout = 0;
    if (cfg->sort_method > 3) cfg->sort_method = 0;
}

void reader_storage_save_config(const ReaderAppConfig *cfg)
{
    struct fdb_blob blob;
    if (fdb_kv_set_blob(&g_kvdb, CONFIG_KV_KEY,
                        fdb_blob_make(&blob, (void *)cfg, sizeof(*cfg))) != FDB_NO_ERR) {
        ESP_LOGW(TAG, "reader preferences could not be saved");
    }
}

void reader_storage_load_library(struct ReaderModel *model)
{
    model->book_count = 0;
    model->storage_ready = fs_control_is_mounted();
    model->library_available = false;
    if (!model->storage_ready) return;

    DIR *dir = opendir(BOOK_DIR);
    if (!dir) return;
    model->library_available = true;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && model->book_count < MAX_BOOKS) {
        bool epub = false;
        if (!supported_book(entry->d_name, &epub)) continue;
        ReaderBookInfo *book = &model->books[model->book_count];
        int n = snprintf(book->filepath, sizeof(book->filepath), "%s/%s",
                         BOOK_DIR, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(book->filepath) ||
            strlen(entry->d_name) >= sizeof(book->title)) continue;
        struct stat st;
        if (stat(book->filepath, &st) != 0 || !S_ISREG(st.st_mode) ||
            st.st_size < 0 || st.st_size > INT32_MAX) continue;
        snprintf(book->title, sizeof(book->title), "%s", entry->d_name);
        book->total_size = (uint32_t)st.st_size;
        book->open_time = st.st_mtime > 0 ? (uint32_t)st.st_mtime : 0;
        book->is_epub = epub;
        model->book_count++;
    }
    closedir(dir);
}

static void position_path(const char *book_path, char *out, size_t capacity)
{
    uint32_t hash = 2166136261U;
    for (const unsigned char *p = (const unsigned char *)book_path; *p; ++p) {
        hash ^= *p;
        hash *= 16777619U;
    }
    snprintf(out, capacity, POSITION_DIR "/%08lx.pos", (unsigned long)hash);
}

uint32_t reader_storage_load_position(const char *book_path)
{
    char path[128];
    position_path(book_path, path, sizeof(path));
    FILE *file = fopen(path, "r");
    if (!file) return 0;
    uint32_t page = 0;
    unsigned long value = 0;
    if (fscanf(file, "%lu", &value) == 1 && value <= UINT32_MAX)
        page = (uint32_t)value;
    fclose(file);
    return page;
}

void reader_storage_save_position(const char *book_path, uint32_t page)
{
    if (!fs_control_is_mounted()) return;
    if (mkdir("/sdcard/bookmarks", 0775) != 0 && errno != EEXIST) return;
    if (mkdir(POSITION_DIR, 0775) != 0 && errno != EEXIST) return;
    char path[128], temp[136];
    position_path(book_path, path, sizeof(path));
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    FILE *file = fopen(temp, "w");
    if (!file) return;
    bool ok = fprintf(file, "%lu\n", (unsigned long)page) > 0;
    if (fclose(file) != 0) ok = false;
    if (!ok || rename(temp, path) != 0) unlink(temp);
}
