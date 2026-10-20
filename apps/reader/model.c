#include "model.h"
#include "storage.h"
#include "epub/fiction_epub_adapter.h"
#include "epub/fiction_epub_rich_adapter.h"

#include <stdlib.h>
#include <string.h>

static void close_book(ReaderModel *model)
{
    if (model->book_file) fclose(model->book_file);
    model->book_file = NULL;
    fiction_epub_rich_close();
    model->epub_rich = false;
}

void reader_model_init(ReaderApp *app)
{
    app->model = calloc(1, sizeof(ReaderModel));
    if (!app->model) return;
    ReaderModel *model = app->model;
    ReaderAppConfig cfg;
    reader_storage_load_config(&cfg);
    model->font_family = cfg.font_family;
    model->font_size_level = cfg.font_size_level;
    model->line_spacing = cfg.line_spacing;
    model->lib_layout = cfg.layout;
    model->sort_method = cfg.sort_method;
    model->active_book_idx = -1;
    model->page_index = 1;
    model->page_total = 1;
    reader_storage_load_library(model);
}

void reader_model_deinit(ReaderApp *app)
{
    if (!app->model) return;
    close_book(app->model);
    free(app->model);
    app->model = NULL;
}

void reader_model_load_library(ReaderModel *model)
{
    if (model) reader_storage_load_library(model);
}

bool reader_model_open_book(ReaderModel *model, uint8_t idx)
{
    if (!model || idx >= model->book_count) return false;
    close_book(model);
    model->active_book_idx = -1;
    ReaderBookInfo *book = &model->books[idx];
    uint32_t text_size = book->total_size;
    if (book->is_epub) {
        if (fiction_epub_rich_open(book->filepath)) {
            model->epub_rich = true;
            model->page_total = 0;
            model->page_index = 1;
        } else {
            char cache[320], error[128];
            if (!fiction_epub_prepare_cache(book->filepath, cache, sizeof(cache),
                                            error, sizeof(error))) return false;
            model->book_file = fopen(cache, "rb");
            if (!model->book_file) return false;
            if (fseek(model->book_file, 0, SEEK_END) != 0) goto fail;
            long size = ftell(model->book_file);
            if (size < 0 || size > INT32_MAX) goto fail;
            text_size = (uint32_t)size;
        }
    } else {
        model->book_file = fopen(book->filepath, "rb");
        if (!model->book_file) return false;
        if (fseek(model->book_file, 0, SEEK_END) != 0) goto fail;
        long size = ftell(model->book_file);
        if (size < 0 || size > INT32_MAX) goto fail;
        text_size = (uint32_t)size;
    }
    if (!model->epub_rich) {
        model->page_total = text_size ? (text_size - 1U) / READER_PAGE_BYTES + 1U : 1U;
        uint32_t saved = reader_storage_load_position(book->filepath);
        model->page_index = saved >= 1 && saved <= model->page_total ? saved : 1U;
        model->mem_offset = (model->page_index - 1U) * READER_PAGE_BYTES;
    }
    model->active_book_idx = idx;
    model->last_page = 1;
    return true;
fail:
    close_book(model);
    return false;
}

static long utf8_boundary(FILE *file, long offset, long size)
{
    if (offset >= size) return size;
    if (fseek(file, offset, SEEK_SET) != 0) return -1;
    for (int i = 0; i < 4 && offset < size; ++i, ++offset) {
        int c = fgetc(file);
        if (c == EOF) return -1;
        if ((c & 0xc0) != 0x80) return offset;
    }
    return offset;
}

void reader_model_read_page(ReaderModel *model)
{
    if (!model || model->active_book_idx < 0) return;
    model->page_buffer[0] = '\0';
    if (model->epub_rich) {
        if (!fiction_epub_rich_read_page(model->page_buffer,
                                         sizeof(model->page_buffer)))
            snprintf(model->page_buffer, sizeof(model->page_buffer),
                     "Unable to read this EPUB page");
        return;
    }
    FILE *file = model->book_file;
    if (!file || fseek(file, 0, SEEK_END) != 0) return;
    long size = ftell(file);
    if (size < 0) return;
    long base = (long)((model->page_index - 1U) * READER_PAGE_BYTES);
    long limit = base + READER_PAGE_BYTES;
    if (limit > size) limit = size;
    long start = utf8_boundary(file, base, size);
    long end = utf8_boundary(file, limit, size);
    if (start < 0 || end < start || (size_t)(end - start) >= sizeof(model->page_buffer) ||
        fseek(file, start, SEEK_SET) != 0) return;
    size_t wanted = (size_t)(end - start);
    size_t read = fread(model->page_buffer, 1, wanted, file);
    model->page_buffer[read] = '\0';
    model->mem_offset = (uint32_t)start;
    if (read == 0) snprintf(model->page_buffer, sizeof(model->page_buffer),
                            size == 0 ? "Empty book" : "Unable to read page");
}

void reader_model_set_offset_by_percent(ReaderModel *model, uint8_t percent)
{
    if (!model || model->active_book_idx < 0 || model->epub_rich) return;
    if (percent > 100) percent = 100;
    model->page_index = (uint32_t)(((uint64_t)(model->page_total - 1U) * percent) / 100U) + 1U;
    model->mem_offset = (model->page_index - 1U) * READER_PAGE_BYTES;
    reader_storage_save_position(model->books[model->active_book_idx].filepath,
                                 model->page_index);
}

void reader_model_next_page(ReaderModel *model, uint32_t unused)
{
    (void)unused;
    if (!model || model->active_book_idx < 0) return;
    if (model->epub_rich) {
        if (fiction_epub_rich_next()) model->page_index++;
        return;
    }
    if (model->page_index < model->page_total) model->page_index++;
    reader_storage_save_position(model->books[model->active_book_idx].filepath,
                                 model->page_index);
}

void reader_model_prev_page(ReaderModel *model, uint32_t unused)
{
    (void)unused;
    if (!model || model->active_book_idx < 0) return;
    if (model->epub_rich) {
        if (fiction_epub_rich_prev() && model->page_index > 1) model->page_index--;
        return;
    }
    if (model->page_index > 1) model->page_index--;
    reader_storage_save_position(model->books[model->active_book_idx].filepath,
                                 model->page_index);
}

uint8_t reader_model_get_progress_percent(ReaderModel *model)
{
    if (!model || model->active_book_idx < 0) return 0;
    if (model->epub_rich) return fiction_epub_rich_progress_percent();
    if (model->page_total <= 1) return 100;
    return (uint8_t)(((uint64_t)(model->page_index - 1U) * 100U) /
                     (model->page_total - 1U));
}

void reader_model_toggle_font_size(ReaderModel *model)
{
    model->font_size_level = (model->font_size_level + 1U) % 3U;
    reader_model_sync_config(model);
}

void reader_model_sync_config(ReaderModel *model)
{
    ReaderAppConfig cfg = {
        .font_family = model->font_family,
        .font_size_level = model->font_size_level,
        .line_spacing = model->line_spacing,
        .active_book_idx = -1,
        .last_page = 0,
        .layout = model->lib_layout,
        .sort_method = model->sort_method,
        .page_index = 1,
    };
    reader_storage_save_config(&cfg);
}

bool reader_model_has_toc(const ReaderModel *model)
{
    return model && model->epub_rich && fiction_epub_rich_toc_count() > 0;
}

int reader_model_toc_count(const ReaderModel *model)
{
    return model && model->epub_rich ? fiction_epub_rich_toc_count() : 0;
}

bool reader_model_toc_title(int index, char *out, size_t out_len)
{
    return fiction_epub_rich_toc_title(index, out, out_len);
}

bool reader_model_jump_to_toc(ReaderModel *model, int index)
{
    if (!model || !model->epub_rich || !fiction_epub_rich_jump_to_toc(index)) return false;
    model->page_index = 1;
    return true;
}

bool reader_model_read_page_image(ReaderModel *model, uint8_t **data, size_t *size)
{
    return model && model->epub_rich && fiction_epub_rich_read_page_image(data, size);
}

void reader_model_free_page_image(uint8_t *data)
{
    fiction_epub_rich_free_page_image(data);
}
