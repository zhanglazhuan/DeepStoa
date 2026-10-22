#ifndef READER_MODEL_H
#define READER_MODEL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "app.h"

#define MAX_BOOKS 32
#define MAX_BOOK_NAME 256
#define READER_PAGE_BYTES 480U
#define PAGE_BUFFER_SIZE 4096

typedef struct ReaderBookInfo {
    char title[MAX_BOOK_NAME];
    char filepath[320];
    uint32_t total_size;
    uint32_t open_time;
    bool is_epub;
} ReaderBookInfo;

typedef struct ReaderModel {
    ReaderBookInfo books[MAX_BOOKS];
    uint8_t book_count;
    bool storage_ready;
    bool library_available;
    uint8_t lib_layout;
    uint8_t sort_method;
    char page_buffer[PAGE_BUFFER_SIZE];
    uint8_t font_family;
    uint8_t font_size_level;
    uint8_t line_spacing;
    int32_t active_book_idx;
    uint32_t mem_offset;
    uint8_t last_page;
    uint32_t page_index;
    uint32_t page_total;
    FILE *book_file;
    bool epub_rich;
} ReaderModel;

void reader_model_init(ReaderApp *app);
void reader_model_deinit(ReaderApp *app);
void reader_model_load_library(ReaderModel *model);
bool reader_model_open_book(ReaderModel *model, uint8_t idx);
void reader_model_read_page(ReaderModel *model);
void reader_model_set_offset_by_percent(ReaderModel *model, uint8_t percent);
void reader_model_next_page(ReaderModel *model, uint32_t bytes_read);
void reader_model_prev_page(ReaderModel *model, uint32_t bytes_read);
void reader_model_toggle_font_size(ReaderModel *model);
uint8_t reader_model_get_progress_percent(ReaderModel *model);
void reader_model_sync_config(ReaderModel *model);
bool reader_model_has_toc(const ReaderModel *model);
int reader_model_toc_count(const ReaderModel *model);
bool reader_model_toc_title(int index, char *out, size_t out_len);
bool reader_model_jump_to_toc(ReaderModel *model, int index);
bool reader_model_read_page_image(ReaderModel *model, uint8_t **data, size_t *size);
void reader_model_free_page_image(uint8_t *data);

#endif
