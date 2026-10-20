#include "page_fiction.h"
#include "epub/rich/EpubRichReader.h"
#include "epub/rich/EpaperFictionRenderer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "button_bsp.h"
#include "sdcard_bsp.h"
#include <string.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include "dirent.h"
#include <errno.h>
#include <sys/stat.h>

#include "epaper_port.h"
#include "GUI_BMPfile.h"
#include "GUI_Paint.h"
#include "pcf85063_bsp.h"
#include "status_bar.h"
#include "axp_prot.h"
#include "application.h"
#include "display/display.h"

#include <nvs.h>
#include <nvs_flash.h>

// Add a screen size definition
#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 800
// The maximum number of characters per page
#define MAX_PAGE_CONTENT 2048

#define Default_font FONT_SIZE_18  // default font

// E-ink screen sleep time (S)
#define EPD_Sleep_Time   5
// Equipment shutdown time (minutes)
#define Unattended_Time  10

// Icon position
#define file            "/sdcard/GUI/file.bmp"
#define rests           "/sdcard/GUI/rests.bmp"
// icon size
#define GUI_WIDTH       32
#define GUI_HEIGHT      32

#define HEADER_HEIGHT 65    // The top path shows the height of the area
#define ITEM_HEIGHT   45    // The height of each file/directory entry
#define MARGIN_LEFT   10    // leftmargin
#define MARGIN_TOP    10    // top margin

extern SemaphoreHandle_t rtc_mutex;   // Protect RTC
extern bool wifi_enable;                       // Is the wifi turned on?

static const char *TAG = "page_fiction";
static TickType_t s_fiction_last_base_refresh_tick = 0;
static const uint8_t* s_fiction_last_base_refresh_buffer = nullptr;
static constexpr TickType_t kFictionBaseRefreshCooldownTicks = pdMS_TO_TICKS(1200);
static const char* kFictionNotesDir = "/sdcard/fiction";

static void fiction_display_refresh(const uint8_t* buffer, bool force_base)
{
    if (!buffer) return;

    if (force_base) {
        TickType_t now = xTaskGetTickCount();
        // Keep image quality: use normal base refresh, but avoid duplicated full refreshes
        // on the same buffer in a very short interval.
        if (s_fiction_last_base_refresh_tick != 0 &&
            (now - s_fiction_last_base_refresh_tick) < kFictionBaseRefreshCooldownTicks &&
            s_fiction_last_base_refresh_buffer == buffer) {
            EPD_Display_Partial(buffer, 0, 0, EPD_WIDTH, EPD_HEIGHT);
            return;
        }
        EPD_Display_Base(buffer);
        s_fiction_last_base_refresh_tick = now;
        s_fiction_last_base_refresh_buffer = buffer;
    } else {
        EPD_Display_Partial(buffer, 0, 0, EPD_WIDTH, EPD_HEIGHT);
    }
}

#ifndef FICTION_ENABLE_ASYNC_DIR_RESCAN
#define FICTION_ENABLE_ASYNC_DIR_RESCAN 1
#endif

typedef struct {
    SemaphoreHandle_t mutex;
    TaskHandle_t task_handle;
    file_entry_t* result_entries;
    int result_capacity;
    int request_page_index;
    int request_page_size;
    volatile bool cancel;
    volatile bool busy;
    volatile bool result_ready;
    bool result_success;
    int result_total_num;
    int result_total_pages;
    int result_page_index;
    int result_num;
} fiction_dir_rescan_state_t;

static fiction_dir_rescan_state_t s_fiction_dir_rescan = {0};

static bool fiction_dir_rescan_ensure_mutex(void)
{
    if (s_fiction_dir_rescan.mutex) return true;
    s_fiction_dir_rescan.mutex = xSemaphoreCreateMutex();
    if (!s_fiction_dir_rescan.mutex) {
        ESP_LOGE(TAG, "Failed to create fiction dir rescan mutex");
        return false;
    }
    return true;
}

static bool fiction_dir_rescan_ensure_buffer(int page_size)
{
    if (page_size <= 0) return false;
    if (s_fiction_dir_rescan.result_entries && s_fiction_dir_rescan.result_capacity == page_size) {
        return true;
    }

    if (s_fiction_dir_rescan.result_entries) {
        heap_caps_free(s_fiction_dir_rescan.result_entries);
        s_fiction_dir_rescan.result_entries = NULL;
        s_fiction_dir_rescan.result_capacity = 0;
    }

    s_fiction_dir_rescan.result_entries = (file_entry_t*)heap_caps_malloc(
        page_size * sizeof(file_entry_t), MALLOC_CAP_SPIRAM);
    if (!s_fiction_dir_rescan.result_entries) {
        s_fiction_dir_rescan.result_entries = (file_entry_t*)heap_caps_malloc(
            page_size * sizeof(file_entry_t), MALLOC_CAP_8BIT);
    }

    if (!s_fiction_dir_rescan.result_entries) {
        ESP_LOGE(TAG, "Failed to allocate async rescan buffer");
        return false;
    }
    s_fiction_dir_rescan.result_capacity = page_size;
    return true;
}

static void fiction_dir_rescan_task(void* pvParameters)
{
    int page_index = 0;
    int page_size = 0;
    file_entry_t* out_entries = NULL;

    if (!fiction_dir_rescan_ensure_mutex()) {
        vTaskDelete(NULL);
        return;
    }

    xSemaphoreTake(s_fiction_dir_rescan.mutex, portMAX_DELAY);
    page_index = s_fiction_dir_rescan.request_page_index;
    page_size = s_fiction_dir_rescan.request_page_size;
    out_entries = s_fiction_dir_rescan.result_entries;
    xSemaphoreGive(s_fiction_dir_rescan.mutex);

    bool success = false;
    int total_num = 0;
    int total_pages = 1;
    int num = 0;

    if (!s_fiction_dir_rescan.cancel && out_entries && page_size > 0) {
        total_num = get_dir_file_count("/sdcard/fiction");
        if (!s_fiction_dir_rescan.cancel) {
            total_pages = (total_num + page_size - 1) / page_size;
            if (total_pages == 0) total_pages = 1;
            if (page_index < 0) page_index = 0;
            if (page_index >= total_pages) page_index = total_pages - 1;
            num = list_dir_page("/sdcard/fiction", out_entries, page_index * page_size, page_size);
            success = !s_fiction_dir_rescan.cancel;
        }
    }

    xSemaphoreTake(s_fiction_dir_rescan.mutex, portMAX_DELAY);
    s_fiction_dir_rescan.result_success = success;
    s_fiction_dir_rescan.result_total_num = total_num;
    s_fiction_dir_rescan.result_total_pages = total_pages;
    s_fiction_dir_rescan.result_page_index = page_index;
    s_fiction_dir_rescan.result_num = num;
    s_fiction_dir_rescan.result_ready = true;
    s_fiction_dir_rescan.busy = false;
    s_fiction_dir_rescan.task_handle = NULL;
    xSemaphoreGive(s_fiction_dir_rescan.mutex);

    vTaskDelete(NULL);
}

static bool fiction_dir_rescan_start_async(int page_index, int page_size)
{
#if !FICTION_ENABLE_ASYNC_DIR_RESCAN
    (void)page_index;
    (void)page_size;
    return false;
#else
    if (!fiction_dir_rescan_ensure_mutex()) return false;

    xSemaphoreTake(s_fiction_dir_rescan.mutex, portMAX_DELAY);
    if (s_fiction_dir_rescan.busy) {
        xSemaphoreGive(s_fiction_dir_rescan.mutex);
        return false;
    }
    if (!fiction_dir_rescan_ensure_buffer(page_size)) {
        xSemaphoreGive(s_fiction_dir_rescan.mutex);
        return false;
    }

    s_fiction_dir_rescan.request_page_index = page_index;
    s_fiction_dir_rescan.request_page_size = page_size;
    s_fiction_dir_rescan.cancel = false;
    s_fiction_dir_rescan.result_ready = false;
    s_fiction_dir_rescan.result_success = false;
    s_fiction_dir_rescan.busy = true;
    xSemaphoreGive(s_fiction_dir_rescan.mutex);

    BaseType_t ok = xTaskCreate(
        fiction_dir_rescan_task, "fiction_rescan", 4096, NULL, 4, &s_fiction_dir_rescan.task_handle);
    if (ok != pdPASS) {
        xSemaphoreTake(s_fiction_dir_rescan.mutex, portMAX_DELAY);
        s_fiction_dir_rescan.busy = false;
        s_fiction_dir_rescan.task_handle = NULL;
        xSemaphoreGive(s_fiction_dir_rescan.mutex);
        ESP_LOGW(TAG, "Failed to create async dir rescan task");
        return false;
    }
    return true;
#endif
}

static bool fiction_dir_rescan_try_consume(file_entry_t* dst_entries,
                                           int dst_capacity,
                                           int* total_num,
                                           int* total_pages,
                                           int* page_index,
                                           int* num,
                                           bool* success)
{
    if (!s_fiction_dir_rescan.mutex) return false;

    xSemaphoreTake(s_fiction_dir_rescan.mutex, portMAX_DELAY);
    if (!s_fiction_dir_rescan.result_ready) {
        xSemaphoreGive(s_fiction_dir_rescan.mutex);
        return false;
    }

    int copy_num = s_fiction_dir_rescan.result_num;
    if (copy_num > dst_capacity) copy_num = dst_capacity;
    if (s_fiction_dir_rescan.result_success && dst_entries && s_fiction_dir_rescan.result_entries && copy_num > 0) {
        memcpy(dst_entries, s_fiction_dir_rescan.result_entries, copy_num * sizeof(file_entry_t));
    }

    if (total_num) *total_num = s_fiction_dir_rescan.result_total_num;
    if (total_pages) *total_pages = s_fiction_dir_rescan.result_total_pages;
    if (page_index) *page_index = s_fiction_dir_rescan.result_page_index;
    if (num) *num = copy_num;
    if (success) *success = s_fiction_dir_rescan.result_success;

    s_fiction_dir_rescan.result_ready = false;
    xSemaphoreGive(s_fiction_dir_rescan.mutex);
    return true;
}

static void fiction_dir_rescan_cancel_and_wait(void)
{
#if FICTION_ENABLE_ASYNC_DIR_RESCAN
    if (!s_fiction_dir_rescan.mutex) return;

    xSemaphoreTake(s_fiction_dir_rescan.mutex, portMAX_DELAY);
    bool busy = s_fiction_dir_rescan.busy;
    if (busy) {
        s_fiction_dir_rescan.cancel = true;
    }
    xSemaphoreGive(s_fiction_dir_rescan.mutex);

    if (busy) {
        for (int i = 0; i < 200; ++i) {
            xSemaphoreTake(s_fiction_dir_rescan.mutex, portMAX_DELAY);
            bool still_busy = s_fiction_dir_rescan.busy;
            xSemaphoreGive(s_fiction_dir_rescan.mutex);
            if (!still_busy) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
#endif
}

static void fiction_dir_rescan_release_buffer(void)
{
    if (!s_fiction_dir_rescan.mutex) return;
    xSemaphoreTake(s_fiction_dir_rescan.mutex, portMAX_DELAY);
    if (s_fiction_dir_rescan.result_entries) {
        heap_caps_free(s_fiction_dir_rescan.result_entries);
        s_fiction_dir_rescan.result_entries = NULL;
    }
    s_fiction_dir_rescan.result_capacity = 0;
    s_fiction_dir_rescan.result_ready = false;
    s_fiction_dir_rescan.result_success = false;
    xSemaphoreGive(s_fiction_dir_rescan.mutex);
}

// Add external variables and function declarations
extern uint8_t *Image_Mono;  // External image buffer
extern int wait_key_event_and_return_code(uint32_t timeout);  // Key detection function

uint8_t *Image_Fiction;


// Global context
static fiction_context_t g_fiction_ctx = {0};
static fiction_display_context_t g_display_ctx = {0};

typedef struct {
    bool active;
    bool waiting_playback;
    bool prefetch_inflight;
    int pending_prefetch_requests;
    int page_number;
    int section_number;
    size_t text_offset;
    size_t chunk_index;
    TickType_t current_chunk_started_tick;
    uint32_t current_chunk_estimated_ms;
    uint32_t prefetched_chunk_estimated_ms;
    char page_text[2048];
} fiction_ai_read_state_t;

static fiction_ai_read_state_t s_fiction_ai_read = {0};
static bool s_fiction_note_recording = false;

typedef enum {
    kFictionAiBoundaryNone = 0,
    kFictionAiBoundarySentence,
    kFictionAiBoundaryClause,
    kFictionAiBoundaryHardLimit,
} fiction_ai_boundary_type_t;

typedef struct {
    size_t end;
    fiction_ai_boundary_type_t type;
} fiction_ai_boundary_result_t;

static const char* kFictionAiSentenceTokens[] = {"\n\n", "。", "！", "？", ".", "!", "?"};
static const char* kFictionAiClauseTokens[] = {"；", ";", "：", ":", ",", "，", "、"};
static bool fiction_ai_read_advance_to_next_page();

static void fiction_trim_trailing_whitespace(std::string& text)
{
    while (!text.empty()) {
        char c = text.back();
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            text.pop_back();
        } else {
            break;
        }
    }
}

static size_t fiction_count_utf8_chars(const std::string& text)
{
    size_t count = 0;
    for (unsigned char ch : text) {
        if ((ch & 0xC0) != 0x80) {
            ++count;
        }
    }
    return count;
}

static uint32_t fiction_ai_read_estimate_duration_ms(const std::string& text)
{
    size_t char_count = fiction_count_utf8_chars(text);
    uint32_t estimated = 900 + static_cast<uint32_t>(char_count * 120);
    if (estimated < 2200) {
        estimated = 2200;
    }
    return estimated;
}

static std::string fiction_get_book_title_string()
{
    const char* path = g_fiction_ctx.source_filepath[0] != '\0' ? g_fiction_ctx.source_filepath : g_fiction_ctx.filepath;
    if (!path || path[0] == '\0') {
        return std::string();
    }

    const char* name = strrchr(path, '/');
    name = name ? name + 1 : path;
    std::string title(name);
    size_t dot = title.find_last_of('.');
    if (dot != std::string::npos) {
        title.erase(dot);
    }
    return title;
}

static std::string fiction_get_current_page_text_string()
{
    if (!g_fiction_ctx.is_open) {
        return std::string();
    }

    std::string text = g_display_ctx.page_buffers[BUFFER_CURRENT].content;
    fiction_trim_trailing_whitespace(text);
    return text;
}

static int fiction_get_current_page_number()
{
    return g_display_ctx.current_page + 1;
}

static std::string fiction_sanitize_note_filename(const std::string& title)
{
    std::string sanitized;
    sanitized.reserve(title.size());
    for (unsigned char ch : title) {
        if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' ||
            ch == '"' || ch == '<' || ch == '>' || ch == '|' || ch < 0x20) {
            sanitized.push_back('_');
            continue;
        }
        sanitized.push_back(static_cast<char>(ch));
    }
    fiction_trim_trailing_whitespace(sanitized);
    if (sanitized.empty()) {
        sanitized = "未命名书籍";
    }
    return sanitized;
}

static std::string fiction_get_current_notes_filepath_string()
{
    std::string title = fiction_get_book_title_string();
    if (title.empty()) {
        return std::string();
    }
    std::string sanitized_title = fiction_sanitize_note_filename(title);
    return std::string(kFictionNotesDir) + "/" + sanitized_title + "-读书笔记.txt";
}

static void fiction_ai_read_refresh_page_snapshot()
{
    std::string page_text = fiction_get_current_page_text_string();
    strncpy(s_fiction_ai_read.page_text, page_text.c_str(), sizeof(s_fiction_ai_read.page_text) - 1);
    s_fiction_ai_read.page_text[sizeof(s_fiction_ai_read.page_text) - 1] = '\0';
    s_fiction_ai_read.page_number = g_display_ctx.current_page;
    s_fiction_ai_read.section_number = g_display_ctx.current_section;
    s_fiction_ai_read.text_offset = 0;
    ESP_LOGI(TAG, "AI read snapshot refreshed: page=%d section=%d text_len=%u",
             s_fiction_ai_read.page_number + 1,
             s_fiction_ai_read.section_number + 1,
             (unsigned)strlen(s_fiction_ai_read.page_text));
}

static bool fiction_ai_read_match_token(const char* text, size_t pos, size_t limit, const char* token)
{
    size_t token_len = strlen(token);
    return pos + token_len <= limit && strncmp(text + pos, token, token_len) == 0;
}

static size_t fiction_ai_read_find_last_token(
    const char* text,
    size_t start,
    size_t search_end,
    const char* const* tokens,
    size_t token_count)
{
    size_t best_end = 0;
    for (size_t i = start; i < search_end; ++i) {
        for (size_t token_index = 0; token_index < token_count; ++token_index) {
            const char* token = tokens[token_index];
            if (fiction_ai_read_match_token(text, i, search_end, token)) {
                best_end = i + strlen(token);
            }
        }
    }
    return best_end;
}

static size_t fiction_ai_read_find_first_token(
    const char* text,
    size_t start,
    size_t search_end,
    const char* const* tokens,
    size_t token_count)
{
    for (size_t i = start; i < search_end; ++i) {
        for (size_t token_index = 0; token_index < token_count; ++token_index) {
            const char* token = tokens[token_index];
            if (fiction_ai_read_match_token(text, i, search_end, token)) {
                return i + strlen(token);
            }
        }
    }
    return 0;
}

static bool fiction_ai_read_has_token(
    const char* text,
    size_t start,
    size_t search_end,
    const char* const* tokens,
    size_t token_count)
{
    for (size_t i = start; i < search_end; ++i) {
        for (size_t token_index = 0; token_index < token_count; ++token_index) {
            const char* token = tokens[token_index];
            if (fiction_ai_read_match_token(text, i, search_end, token)) {
                return true;
            }
        }
    }
    return false;
}

static void fiction_ai_read_skip_leading_whitespace()
{
    size_t text_len = strlen(s_fiction_ai_read.page_text);
    while (s_fiction_ai_read.text_offset < text_len &&
           (s_fiction_ai_read.page_text[s_fiction_ai_read.text_offset] == ' ' ||
            s_fiction_ai_read.page_text[s_fiction_ai_read.text_offset] == '\n' ||
            s_fiction_ai_read.page_text[s_fiction_ai_read.text_offset] == '\r' ||
            s_fiction_ai_read.page_text[s_fiction_ai_read.text_offset] == '\t')) {
        ++s_fiction_ai_read.text_offset;
    }
}

static bool fiction_ai_read_remaining_page_has_sentence_end(const char* text, size_t start)
{
    if (!text) {
        return false;
    }
    size_t len = strlen(text);
    if (start >= len) {
        return false;
    }
    return fiction_ai_read_has_token(
        text,
        start,
        len,
        kFictionAiSentenceTokens,
        sizeof(kFictionAiSentenceTokens) / sizeof(kFictionAiSentenceTokens[0]));
}

static size_t fiction_ai_read_align_chunk_end(const char* text, size_t start, size_t candidate_end)
{
    if (!text) {
        return start;
    }

    size_t len = strlen(text);
    if (candidate_end > len) {
        candidate_end = len;
    }
    if (candidate_end <= start) {
        return start;
    }

    while (candidate_end > start) {
        unsigned char ch = static_cast<unsigned char>(text[candidate_end]);
        if ((ch & 0xC0) != 0x80) {
            break;
        }
        --candidate_end;
    }

    while (candidate_end > start) {
        char ch = text[candidate_end - 1];
        if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') {
            --candidate_end;
            continue;
        }
        break;
    }

    return candidate_end > start ? candidate_end : start;
}

static fiction_ai_boundary_result_t fiction_ai_read_find_chunk_boundary(
    const char* text,
    size_t start,
    size_t preferred_bytes,
    size_t max_bytes)
{
    fiction_ai_boundary_result_t result = {start, kFictionAiBoundaryNone};
    if (!text) {
        return result;
    }

    size_t len = strlen(text);
    if (start >= len) {
        result.end = len;
        return result;
    }

    const size_t min_sentence_bytes = 120;
    const size_t min_clause_bytes = 60;
    const size_t punctuation_lookahead_bytes = 160;
    const size_t sentence_merge_target_bytes = preferred_bytes > 180 ? preferred_bytes - 40 : preferred_bytes;
    size_t preferred_end = start + preferred_bytes;
    if (preferred_end > len) preferred_end = len;
    size_t max_end = start + max_bytes;
    if (max_end > len) max_end = len;
    size_t soft_max_end = max_end + punctuation_lookahead_bytes;
    if (soft_max_end > len) soft_max_end = len;

    size_t sentence_end = fiction_ai_read_find_last_token(
        text, start, max_end, kFictionAiSentenceTokens, sizeof(kFictionAiSentenceTokens) / sizeof(kFictionAiSentenceTokens[0]));
    size_t sentence_end_soft = sentence_end;
    if (sentence_end_soft <= start && soft_max_end > max_end) {
        sentence_end_soft = fiction_ai_read_find_last_token(
            text, preferred_end, soft_max_end, kFictionAiSentenceTokens, sizeof(kFictionAiSentenceTokens) / sizeof(kFictionAiSentenceTokens[0]));
    }
    if (sentence_end > start && sentence_end - start < sentence_merge_target_bytes) {
        size_t next_sentence_end = fiction_ai_read_find_first_token(
            text,
            sentence_end,
            soft_max_end,
            kFictionAiSentenceTokens,
            sizeof(kFictionAiSentenceTokens) / sizeof(kFictionAiSentenceTokens[0]));
        if (next_sentence_end > sentence_end) {
            sentence_end_soft = next_sentence_end;
        }
    }
    if (sentence_end > start && sentence_end - start >= min_sentence_bytes) {
        result.end = sentence_end;
        result.type = kFictionAiBoundarySentence;
        return result;
    }
    if (sentence_end >= preferred_end) {
        result.end = sentence_end;
        result.type = kFictionAiBoundarySentence;
        return result;
    }
    if (sentence_end_soft > start && sentence_end_soft <= soft_max_end) {
        result.end = sentence_end_soft;
        result.type = kFictionAiBoundarySentence;
        return result;
    }

    size_t clause_end = fiction_ai_read_find_last_token(
        text, start, max_end, kFictionAiClauseTokens, sizeof(kFictionAiClauseTokens) / sizeof(kFictionAiClauseTokens[0]));
    size_t clause_end_soft = clause_end;
    if (clause_end_soft <= start && soft_max_end > max_end) {
        clause_end_soft = fiction_ai_read_find_last_token(
            text, preferred_end, soft_max_end, kFictionAiClauseTokens, sizeof(kFictionAiClauseTokens) / sizeof(kFictionAiClauseTokens[0]));
    }
    if (clause_end > start && clause_end >= preferred_end) {
        result.end = clause_end;
        result.type = kFictionAiBoundaryClause;
        return result;
    }
    if (sentence_end > start) {
        result.end = sentence_end;
        result.type = kFictionAiBoundarySentence;
        return result;
    }
    if (clause_end > start && clause_end - start >= min_sentence_bytes) {
        result.end = clause_end;
        result.type = kFictionAiBoundaryClause;
        return result;
    }
    if (clause_end_soft > start && clause_end_soft - start >= min_clause_bytes) {
        result.end = clause_end_soft;
        result.type = kFictionAiBoundaryClause;
        return result;
    }
    if (clause_end > start) {
        result.end = clause_end;
        result.type = kFictionAiBoundaryClause;
        return result;
    }

    result.end = fiction_ai_read_align_chunk_end(text, start, max_end);
    result.type = result.end > start ? kFictionAiBoundaryHardLimit : kFictionAiBoundaryNone;
    return result;
}

static void fiction_ai_read_append_slice(std::string& chunk, const char* text, size_t start, size_t end)
{
    if (!text || end <= start) {
        return;
    }
    chunk.append(text + start, end - start);
}

static bool fiction_ai_read_extract_next_sentence_unit(
    std::string& sentence,
    size_t max_bytes,
    int max_borrow_pages,
    int* borrowed_pages,
    int* end_page)
{
    sentence.clear();
    if (borrowed_pages) {
        *borrowed_pages = 0;
    }
    if (end_page) {
        *end_page = g_display_ctx.current_page + 1;
    }

    while (true) {
        fiction_ai_read_skip_leading_whitespace();
        size_t text_len = strlen(s_fiction_ai_read.page_text);
        if (s_fiction_ai_read.text_offset >= text_len) {
            if (!fiction_ai_read_advance_to_next_page()) {
                break;
            }
            if (borrowed_pages) {
                ++(*borrowed_pages);
                if (*borrowed_pages > max_borrow_pages) {
                    break;
                }
            }
            if (end_page) {
                *end_page = g_display_ctx.current_page + 1;
            }
            continue;
        }

        size_t remaining_budget = sentence.size() < max_bytes ? (max_bytes - sentence.size()) : 0;
        if (remaining_budget == 0) {
            break;
        }

        size_t search_end = s_fiction_ai_read.text_offset + remaining_budget;
        if (search_end > text_len) {
            search_end = text_len;
        }

        size_t sentence_end = fiction_ai_read_find_first_token(
            s_fiction_ai_read.page_text,
            s_fiction_ai_read.text_offset,
            search_end,
            kFictionAiSentenceTokens,
            sizeof(kFictionAiSentenceTokens) / sizeof(kFictionAiSentenceTokens[0]));
        if (sentence_end > s_fiction_ai_read.text_offset) {
            fiction_ai_read_append_slice(
                sentence,
                s_fiction_ai_read.page_text,
                s_fiction_ai_read.text_offset,
                sentence_end);
            s_fiction_ai_read.text_offset = sentence_end;
            if (end_page) {
                *end_page = g_display_ctx.current_page + 1;
            }
            break;
        }

        if (search_end >= text_len) {
            fiction_ai_read_append_slice(
                sentence,
                s_fiction_ai_read.page_text,
                s_fiction_ai_read.text_offset,
                text_len);
            s_fiction_ai_read.text_offset = text_len;
            if (!fiction_ai_read_advance_to_next_page()) {
                break;
            }
            if (borrowed_pages) {
                ++(*borrowed_pages);
                if (*borrowed_pages > max_borrow_pages) {
                    break;
                }
            }
            if (end_page) {
                *end_page = g_display_ctx.current_page + 1;
            }
            continue;
        }

        size_t clause_end = fiction_ai_read_find_last_token(
            s_fiction_ai_read.page_text,
            s_fiction_ai_read.text_offset,
            search_end,
            kFictionAiClauseTokens,
            sizeof(kFictionAiClauseTokens) / sizeof(kFictionAiClauseTokens[0]));
        if (clause_end > s_fiction_ai_read.text_offset) {
            fiction_ai_read_append_slice(
                sentence,
                s_fiction_ai_read.page_text,
                s_fiction_ai_read.text_offset,
                clause_end);
            s_fiction_ai_read.text_offset = clause_end;
            if (end_page) {
                *end_page = g_display_ctx.current_page + 1;
            }
            break;
        }

        size_t hard_end = fiction_ai_read_align_chunk_end(
            s_fiction_ai_read.page_text,
            s_fiction_ai_read.text_offset,
            search_end);
        if (hard_end <= s_fiction_ai_read.text_offset) {
            break;
        }

        fiction_ai_read_append_slice(
            sentence,
            s_fiction_ai_read.page_text,
            s_fiction_ai_read.text_offset,
            hard_end);
        s_fiction_ai_read.text_offset = hard_end;
        if (end_page) {
            *end_page = g_display_ctx.current_page + 1;
        }
        break;
    }

    fiction_trim_trailing_whitespace(sentence);
    return !sentence.empty();
}

static bool fiction_ai_read_advance_to_next_page()
{
    ESP_LOGI(TAG, "AI read page finished: page=%d, turning next page",
             g_display_ctx.current_page + 1);
    if (!turn_to_next_page()) {
        ESP_LOGW(TAG, "AI read reached end or failed to turn page");
        return false;
    }
    g_fiction_ctx.current_position = g_display_ctx.current_position;
    g_fiction_ctx.current_page = g_display_ctx.current_page;
    g_fiction_ctx.current_section = g_display_ctx.current_section;
    g_fiction_ctx.current_section_pages = g_display_ctx.current_section_pages;
    fiction_save_progress(&g_fiction_ctx);
    fiction_ai_read_refresh_page_snapshot();
    ESP_LOGI(TAG, "AI read advanced to page=%d text_len=%u",
             g_display_ctx.current_page + 1,
             (unsigned)strlen(s_fiction_ai_read.page_text));
    return s_fiction_ai_read.page_text[0] != '\0';
}

static void fiction_ai_read_record_sent_chunk(const std::string& chunk, bool is_prefetch)
{
    uint32_t estimated_ms = fiction_ai_read_estimate_duration_ms(chunk);
    if (is_prefetch) {
        s_fiction_ai_read.prefetch_inflight = true;
        s_fiction_ai_read.prefetched_chunk_estimated_ms = estimated_ms;
        Application::GetInstance().SetReadingAiPrefetchPending(true);
        return;
    }

    s_fiction_ai_read.waiting_playback = true;
    s_fiction_ai_read.current_chunk_started_tick = xTaskGetTickCount();
    s_fiction_ai_read.current_chunk_estimated_ms = estimated_ms;
}

static bool fiction_ai_read_send_next_chunk();

static void fiction_ai_read_render_status()
{
    render_current_page_ui(&g_display_ctx.page_buffers[BUFFER_CURRENT]);
    fiction_display_refresh(g_display_ctx.page_buffers[BUFFER_CURRENT].buffer, false);
}

// File system/font access mutex to prevent assertions caused by simultaneous fopen of multiple tasks
static SemaphoreHandle_t fs_access_mutex = NULL;

// font table
static cFONT* utf8_font_table[FONT_SIZE_MAX] = {
    &Font12_UTF8,  // FONT_SIZE_12
    &Font16_UTF8,  // FONT_SIZE_16 
    &Font18_UTF8,  // FONT_SIZE_18
    &Font24_UTF8,  // FONT_SIZE_24
    &Font28_UTF8,  // FONT_SIZE_28
    &Font36_UTF8,  // FONT_SIZE_36
    &Font48_UTF8,  // FONT_SIZE_48
};

static cFONT* gbk_font_table[FONT_SIZE_MAX] = {
    &Font12_GBK,   // FONT_SIZE_12
    &Font16_GBK,   // FONT_SIZE_16 
    &Font18_GBK,   // FONT_SIZE_18
    &Font24_GBK,   // FONT_SIZE_24
    &Font28_GBK,   // FONT_SIZE_28
    &Font36_GBK,   // FONT_SIZE_36
    &Font48_GBK,   // FONT_SIZE_48
};

static const char* font_names[FONT_SIZE_MAX] = {
    "12号字体", "16号字体", "18号字体", "24号字体", "28号字体", "36号字体", "48号字体"
};

static const char* font_names_1[FONT_SIZE_MAX] = {
    "12号", "16号", "18号", "24号", "28号", "36号", "48号"
};


static uint8_t* bookmark_display_buffer = NULL;  // Bookmark display cache
static uint8_t* bookmark_preview_buffer = NULL;  // Bookmark preview cache
static uint8_t* page_backup_buffer = NULL;       // Page backup cache

// Novel cache area
char lines_char[25][256] = {0};
static bool lines_paragraph_start[25] = {0};
static bool lines_paragraph_end[25] = {0};
static int lines_layout_count = 0;
static constexpr int kMaxLayoutLines = sizeof(lines_char) / sizeof(lines_char[0]);
static constexpr int kLineBufBytes = sizeof(lines_char[0]);
static bool lines_char_bool = 0;

// Add a function declaration for calculating display parameters
void calculate_display_params(void);
extern bool is_chinese_filename(const char* filename);
static const char* detect_txt_encoding(const char* filepath);
static bool is_epub_filepath(const char* filepath);
static const char* fiction_identity_filepath(const fiction_context_t* ctx);
static cFONT* get_font_by_encoding(font_size_t font_size, const char* encoding);
static void fiction_epub_runtime_reset(void);
static bool fiction_epub_runtime_init(void);
static void fiction_epub_sync_display_context(void);
static void fiction_epub_sync_fiction_context(void);
static bool fiction_epub_render_current_page(bool is_current);
static bool fiction_epub_turn_next(void);
static bool fiction_epub_turn_prev(void);
static bool fiction_epub_jump_to(uint16_t section, uint16_t page);
static void fiction_epub_configure_renderer(void);
static int fiction_epub_get_toc_display_count(void);
static int fiction_epub_get_section_for_toc_display(int display_index);
static void fiction_epub_get_toc_display_title(int display_index, char* out, size_t out_len);
static int fiction_epub_get_toc_page_size(void);
static void fiction_epub_display_toc_screen(int selected_index, int top_index);
static void display_fiction_time(Time_data rtc_time);
static void reset_txt_layout_lines(void);

struct fiction_epub_runtime_t {
    bool initialized = false;
    fiction_epub_rich::RichReadState state;
    fiction_epub_rich::EpaperFictionRenderer* renderer = nullptr;
    fiction_epub_rich::EpubRichReader* reader = nullptr;
};

static fiction_epub_runtime_t g_epub_runtime;

static void reset_txt_layout_lines(void)
{
    memset(lines_char, 0, sizeof(lines_char));
    memset(lines_paragraph_start, 0, sizeof(lines_paragraph_start));
    memset(lines_paragraph_end, 0, sizeof(lines_paragraph_end));
    lines_layout_count = 0;
    lines_char_bool = false;
}

// NVS namespace and key names
#define NVS_NS_FICTION "fiction_cfg"
#define NVS_KEY_FONTIDX "font_idx"

// Save the font index to NVS (font is the font_size_t enumeration)
esp_err_t save_font_size_to_nvs(font_size_t font)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_FICTION, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE("fiction", "nvs_open(write) failed: %s", esp_err_to_name(err));
        return err;
    }
    // It uses int32 storage and has good compatibility
    int32_t v = (int32_t)font;
    err = nvs_set_i32(h, NVS_KEY_FONTIDX, v);
    if (err != ESP_OK) {
        ESP_LOGE("fiction", "nvs_set_i32 failed: %s", esp_err_to_name(err));
        nvs_close(h);
        return err;
    }
    err = nvs_commit(h);
    if (err != ESP_OK) {
        ESP_LOGE("fiction", "nvs_commit failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI("fiction", "saved font idx to NVS: %d",(int)v);
    }
    nvs_close(h);
    return err;
}

// Read the font index from NVS, successfully return ESP_OK and write the result to font_out
esp_err_t load_font_size_from_nvs(font_size_t *font_out)
{
    if (!font_out) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_FICTION, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGW("fiction", "nvs_open(read) failed: %s", esp_err_to_name(err));
        return err;
    }
    int32_t v = 0;
    err = nvs_get_i32(h, NVS_KEY_FONTIDX, &v);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI("fiction", "no saved font idx in NVS");
        return ESP_ERR_NOT_FOUND;
    } else if (err != ESP_OK) {
        ESP_LOGE("fiction", "nvs_get_i32 failed: %s", esp_err_to_name(err));
        return err;
    }
    if (v < 0 || v >= FONT_SIZE_MAX) {
        ESP_LOGW("fiction", "saved font idx out of range: %d, reset to default",(int)v);
        *font_out = FONT_SIZE_18; 
    } else {
        *font_out = (font_size_t)v;
    }
    ESP_LOGI("fiction", "loaded font idx from NVS: %d",(int)v);
    return ESP_OK;
}


// Simple GBK to UTF-8 conversion (only for common Chinese characters)
void simple_gbk_to_utf8(const char* gbk_str, char* utf8_str, int max_len)
{
    strncpy(utf8_str, gbk_str, max_len - 1);
    utf8_str[max_len - 1] = '\0';
}

// Get a preview of the content at the current location
void fiction_get_content_preview(fiction_context_t* ctx, char* preview, int max_len)
{
    if (!ctx || !preview || max_len <= 0) {
        return;
    }

    if (ctx->is_epub) {
        snprintf(preview, max_len, "章节 %d 第 %d 页", ctx->current_section + 1, ctx->current_page + 1);
        preview[max_len - 1] = '\0';
        return;
    }

    FILE* fp = fopen(ctx->filepath, "rb");
    if (!fp) {
        strncpy(preview, "无法获取内容", max_len - 1);
        preview[max_len - 1] = '\0';
        return;
    }
    fseek(fp, ctx->current_position, SEEK_SET);
    
    char buffer[256];
    char temp_preview[256] = {0};
    int total_len = 0;
    
    // Read a few lines as a preview
    for (int lines = 0; lines < 3 && total_len < sizeof(temp_preview) - 1; lines++) {
        if (fgets(buffer, sizeof(buffer), fp)) {
            char* newline = strchr(buffer, '\n');
            if (newline) *newline = '\0';
            if (strlen(buffer) == 0) {
                lines--; 
                continue;
            }
            
            int remaining = sizeof(temp_preview) - 1 - total_len;
            if (remaining > 0) {
                if (total_len > 0) {
                    strncat(temp_preview, " ", remaining);
                    total_len++;
                    remaining--;
                }
                strncat(temp_preview, buffer, remaining);
                total_len += strlen(buffer) < remaining ? strlen(buffer) : remaining;
            }
        } else {
            break;
        }
    }
    
    fclose(fp);
    
    if (strcmp(ctx->encoding, "GBK/GB2312") == 0) {
        simple_gbk_to_utf8(temp_preview, preview, max_len);
    } else {
        strncpy(preview, temp_preview, max_len - 1);
        preview[max_len - 1] = '\0';
    }
    
    if (strlen(preview) >= max_len - 4) {
        strcpy(preview + max_len - 4, "...");
    }
}

// Create a bookmark directory
void create_bookmark_directory()
{
    const char* bookmark_dir = "/sdcard/bookmarks";
    struct stat st;

    if (stat(bookmark_dir, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            ESP_LOGI(TAG, "Bookmark directory exists: %s", bookmark_dir);
            return;
        }
        ESP_LOGE(TAG, "Path exists but is not a directory: %s", bookmark_dir);
        return;
    }

    ESP_LOGI(TAG, "Creating bookmark directory: %s", bookmark_dir);
    if (mkdir(bookmark_dir, 0775) == 0 || errno == EEXIST) {
        ESP_LOGI(TAG, "Bookmark directory created: %s", bookmark_dir);
    } else {
        ESP_LOGE(TAG, "Failed to create bookmark directory (%d): %s", errno, bookmark_dir);
    }
}

// Get the path of the bookmark file
void get_bookmark_filepath(const char* txt_filepath, char* bookmark_filepath, int max_len)
{
    const char* filename = strrchr(txt_filepath, '/');
    if (filename) {
        filename++; 
    } else {
        filename = txt_filepath;
    }
    
    // Create the bookmark file path: /sdcard/bookmarks/ filene.bookmarks
    snprintf(bookmark_filepath, max_len, "/sdcard/bookmarks/%s.bookmarks", filename);
}

// Save the bookmarks to the unified directory
void fiction_save_bookmarks(const fiction_context_t* ctx)
{
    create_bookmark_directory();
    
    char bookmark_file[MAX_FILEPATH_LEN + 32];
    get_bookmark_filepath(fiction_identity_filepath(ctx), bookmark_file, sizeof(bookmark_file));
    
    FILE* fp = fopen(bookmark_file, "w");
    if (fp) {
        fprintf(fp, "# Bookmarks for: %s\n", fiction_identity_filepath(ctx));
        fprintf(fp, "%d\n", ctx->bookmark_count);
        
        for (int i = 0; i < ctx->bookmark_count; i++) {
            fprintf(fp, "%zu %d %.2f %s %s\n", 
                    ctx->bookmarks[i].position, 
                    ctx->bookmarks[i].page,
                    ctx->bookmarks[i].progress,
                    ctx->bookmarks[i].description,
                    ctx->bookmarks[i].content_preview);
        }
        fclose(fp);
        ESP_LOGI(TAG, "Bookmarks saved to: %s (%d bookmarks)", bookmark_file, ctx->bookmark_count);
    } else {
        ESP_LOGE(TAG, "Failed to save bookmarks to: %s", bookmark_file);
    }
}

// Load bookmarks from the unified directory
void fiction_load_bookmarks(fiction_context_t* ctx)
{
    char bookmark_file[MAX_FILEPATH_LEN + 32];
    get_bookmark_filepath(fiction_identity_filepath(ctx), bookmark_file, sizeof(bookmark_file));
    
    FILE* fp = fopen(bookmark_file, "r");
    if (fp) {
        char line[256];
        // Skip the comment line
        if (fgets(line, sizeof(line), fp) && line[0] == '#') {
        } else {
            fseek(fp, 0, SEEK_SET);
        }
        
        if (fscanf(fp, "%d\n", &ctx->bookmark_count) == 1) {
            if (ctx->bookmark_count > MAX_BOOKMARKS) ctx->bookmark_count = MAX_BOOKMARKS;
            
            for (int i = 0; i < ctx->bookmark_count; i++) {
                if (fscanf(fp, "%zu %d %f %63s %127[^\n]\n", 
                          &ctx->bookmarks[i].position, 
                          &ctx->bookmarks[i].page,
                          &ctx->bookmarks[i].progress,
                          ctx->bookmarks[i].description,
                          ctx->bookmarks[i].content_preview) != 5) {
                    ctx->bookmark_count = i;
                    break;
                }
            }
        }
        fclose(fp);
        ESP_LOGI(TAG, "Bookmarks loaded from: %s (%d bookmarks)", bookmark_file, ctx->bookmark_count);
    } else {
        ctx->bookmark_count = 0;
        ESP_LOGI(TAG, "No bookmark file found: %s", bookmark_file);
    }
}

// Modify progress save (Enhanced Debugging version)
void fiction_save_progress(const fiction_context_t* ctx)
{
    if (!ctx->is_open) {
        ESP_LOGW(TAG, "Cannot save progress: file not open");
        return;
    }
    
    // Make sure the directory exists
    create_bookmark_directory();
    
    // Extract the file name
    const char* id_path = fiction_identity_filepath(ctx);
    const char* filename = strrchr(id_path, '/');
    if (filename) {
        filename++;
    } else {
        filename = id_path;
    }
    
    char progress_file[MAX_FILEPATH_LEN + 32];
    snprintf(progress_file, sizeof(progress_file), "/sdcard/bookmarks/%s.progress", filename);
    
    ESP_LOGI(TAG, "Saving progress to: %s", progress_file);
    ESP_LOGI(TAG, "Progress data: pos=%zu, page=%d, section=%d",
             ctx->current_position, ctx->current_page, ctx->current_section);
    
    FILE* fp = fopen(progress_file, "w");
    if (fp) {
        fprintf(fp, "# Progress for: %s\n", id_path);
        if (ctx->is_epub) {
            fprintf(fp, "EPUB\n%d\n%d\n%d\n",
                    ctx->current_section,
                    ctx->current_page,
                    ctx->current_section_pages);
        } else {
            fprintf(fp, "TXT\n%zu\n%d\n", ctx->current_position, ctx->current_page);
        }
        fclose(fp);
        ESP_LOGI(TAG, "Progress saved successfully");
    } else {
        ESP_LOGE(TAG, "Failed to save progress file: %s", progress_file);
    }
}

// Progress loading
bool fiction_load_progress(fiction_context_t* ctx)
{
    create_bookmark_directory();
    const char* id_path = fiction_identity_filepath(ctx);
    const char* filename = strrchr(id_path, '/');
    if (filename) {
        filename++;
    } else {
        filename = id_path;
    }
    
    char progress_file[MAX_FILEPATH_LEN + 32];
    snprintf(progress_file, sizeof(progress_file), "/sdcard/bookmarks/%s.progress", filename);
    
    ESP_LOGI(TAG, "Trying to load progress from: %s", progress_file);
    
    FILE* fp = fopen(progress_file, "r");
    if (fp) {
        ESP_LOGI(TAG, "Progress file opened successfully");
        
        char line[256];
        if (fgets(line, sizeof(line), fp)) {
            ESP_LOGI(TAG, "First line: %s", line);
            if (line[0] != '#') {
                fseek(fp, 0, SEEK_SET);
            }
        }

        char mode[16] = {0};
        long marker_pos = ftell(fp);
        if (fgets(mode, sizeof(mode), fp)) {
            mode[strcspn(mode, "\r\n")] = '\0';
        } else {
            mode[0] = '\0';
        }

        if (strcmp(mode, "EPUB") == 0) {
            int section = 0;
            int page = 0;
            int pages_in_section = 0;
            if (fscanf(fp, "%d\n%d\n%d\n", &section, &page, &pages_in_section) >= 2) {
                if (section < 0) section = 0;
                if (page < 0) page = 0;
                if (pages_in_section < 0) pages_in_section = 0;

                ctx->current_section = section;
                ctx->current_page = page;
                ctx->current_section_pages = pages_in_section;
                ctx->current_position = (size_t)section;

                fclose(fp);
                ESP_LOGI(TAG, "Loaded EPUB progress: section=%d, page=%d, pages=%d",
                         section, page, pages_in_section);
                return true;
            }
            ESP_LOGW(TAG, "Failed to parse EPUB progress, fallback to legacy format");
        } else if (strcmp(mode, "TXT") == 0) {
            size_t pos = 0;
            int page = 0;
            if (fscanf(fp, "%zu\n%d\n", &pos, &page) == 2) {
                ctx->current_position = pos;
                ctx->current_page = page;
                ctx->current_section = 0;
                ctx->current_section_pages = 0;
                fclose(fp);
                ESP_LOGI(TAG, "Loaded TXT progress: pos=%zu, page=%d", pos, page);
                return true;
            }
            ESP_LOGW(TAG, "Failed to parse TXT progress, fallback to legacy format");
        } else {
            fseek(fp, marker_pos, SEEK_SET);
        }

        size_t pos = 0;
        int page = 0;
        if (fscanf(fp, "%zu\n%d\n", &pos, &page) == 2) {
            ctx->current_position = pos;
            ctx->current_page = page;
            if (ctx->is_epub) {
                ctx->current_section = (int)pos;
                ctx->current_section_pages = 0;
            } else {
                ctx->current_section = 0;
                ctx->current_section_pages = 0;
            }
            fclose(fp);
            ESP_LOGI(TAG, "Progress loaded successfully: pos=%zu, page=%d", pos, page);
            return true;
        }
        ESP_LOGE(TAG, "Failed to parse progress file");
        fclose(fp);
    } else {
        ESP_LOGI(TAG, "Progress file not found: %s", progress_file);
    }

    ctx->current_position = 0;
    ctx->current_page = 0;
    ctx->current_section = 0;
    ctx->current_section_pages = 0;
    ESP_LOGI(TAG, "Starting from beginning: pos=0, page=0");
    return false;
}

// add bookmark
void fiction_add_bookmark(fiction_context_t* ctx)
{
    if (ctx->bookmark_count >= MAX_BOOKMARKS) {
        ESP_LOGW(TAG, "Maximum bookmarks reached, replacing oldest");
        // Delete the oldest bookmark and add a new one
        for (int i = 0; i < MAX_BOOKMARKS - 1; i++) {
            ctx->bookmarks[i] = ctx->bookmarks[i + 1];
        }
        ctx->bookmark_count = MAX_BOOKMARKS - 1;
    }
    
    // Percentage of calculation progress
    float progress = 0.0f;
    if (ctx->is_epub) {
        progress = 0.0f;
    } else if (ctx->file_size > 0) {
        progress = (float)ctx->current_position * 100.0f / ctx->file_size;
    }
    
    // Get content preview
    char preview[128];
    fiction_get_content_preview(ctx, preview, sizeof(preview));
    
    // Add the current position as a bookmark
    ctx->bookmarks[ctx->bookmark_count].position = ctx->is_epub ? (size_t)ctx->current_section : ctx->current_position;
    ctx->bookmarks[ctx->bookmark_count].page = ctx->current_page;
    ctx->bookmarks[ctx->bookmark_count].progress = progress;
    if (ctx->is_epub) {
        snprintf(ctx->bookmarks[ctx->bookmark_count].description, 64,
                 "章%d 页%d", ctx->current_section + 1, ctx->current_page + 1);
    } else {
        snprintf(ctx->bookmarks[ctx->bookmark_count].description, 64, "第%d页", ctx->current_page + 1);
    }
    strncpy(ctx->bookmarks[ctx->bookmark_count].content_preview, preview, 127);
    ctx->bookmarks[ctx->bookmark_count].content_preview[127] = '\0';
    
    ctx->bookmark_count++;
    fiction_save_bookmarks(ctx);
    ESP_LOGI(TAG, "Bookmark added at page %d (%.1f%%): %s",  ctx->current_page + 1, progress, preview);
}

// Display the bookmark operation options
void fiction_show_bookmark_options(fiction_context_t* ctx, int bookmark_index, int option_selection)
{
    ESP_LOGI(TAG, "=== Bookmark operation ===");
    ESP_LOGI(TAG, "Select the bookmark: Bookmark %d (%.1f%%, page %d)", bookmark_index + 1, ctx->bookmarks[bookmark_index].progress, ctx->bookmarks[bookmark_index].page + 1);
    
    ESP_LOGI(TAG, "%s 1. Jump to this sign", (option_selection == 0) ? ">" : " ");
    ESP_LOGI(TAG, "%s 2. Delete this signature", (option_selection == 1) ? ">" : " ");
    ESP_LOGI(TAG, "%s 3. Return to the bookmark list", (option_selection == 2) ? ">" : " ");
    ESP_LOGI(TAG, "Use the up/down key to select, the confirm key to execute, and the cancel key to return");
}

// Display bookmark list
void fiction_show_bookmarks(fiction_context_t* ctx, int selected_index)
{
    if (ctx->bookmark_count == 0) {
        ESP_LOGI(TAG, "No bookmarks for now");
        return;
    }
    
    ESP_LOGI(TAG, "=== Bookmark list (%d) ===", ctx->bookmark_count);
    for (int i = 0; i < ctx->bookmark_count; i++) {
        const char* marker = (i == selected_index) ? ">" : " ";
        
        char safe_preview[32];
        strncpy(safe_preview, ctx->bookmarks[i].content_preview, sizeof(safe_preview) - 1);
        safe_preview[sizeof(safe_preview) - 1] = '\0';
        
        ESP_LOGI(TAG, "%s %d: Bookmark %d (%.1f%%, page %d)", marker, i + 1, i + 1, ctx->bookmarks[i].progress, ctx->bookmarks[i].page + 1);
    }
    ESP_LOGI(TAG, "Use the up/down keys to select, the confirm key to operate, and the cancel key to exit");
}


// Delete bookmarks
void fiction_delete_bookmark(fiction_context_t* ctx, int bookmark_index)
{
    if (bookmark_index < 0 || bookmark_index >= ctx->bookmark_count) {
        ESP_LOGE(TAG, "Invalid bookmark index: %d", bookmark_index);
        return;
    }
    
    ESP_LOGI(TAG, "Delete bookmark: %s (page %d)", ctx->bookmarks[bookmark_index].content_preview, ctx->bookmarks[bookmark_index].page + 1);
    
    // Move the subsequent bookmarks forward to cover them
    for (int i = bookmark_index; i < ctx->bookmark_count - 1; i++) {
        ctx->bookmarks[i] = ctx->bookmarks[i + 1];
    }
    
    ctx->bookmark_count--;
    fiction_save_bookmarks(ctx);
    ESP_LOGI(TAG, "The bookmarks have been deleted. There are still %d bookmarks left", ctx->bookmark_count);
}

// Jump to Bookmarks
bool fiction_jump_to_bookmark(fiction_context_t* ctx, int bookmark_index)
{
    if (bookmark_index < 0 || bookmark_index >= ctx->bookmark_count) {
        ESP_LOGE(TAG, "Invalid bookmark index: %d", bookmark_index);
        return false;
    }
    
    if (ctx->is_epub) {
        ctx->current_section = (int)ctx->bookmarks[bookmark_index].position;
        ctx->current_page = ctx->bookmarks[bookmark_index].page;
        ctx->current_position = (size_t)ctx->current_section;
    } else {
        ctx->current_position = ctx->bookmarks[bookmark_index].position;
        ctx->current_page = ctx->bookmarks[bookmark_index].page;
    }
    
    ESP_LOGI(TAG, "Jump to bookmark %d: %.1f%%, page %d", bookmark_index + 1, ctx->bookmarks[bookmark_index].progress, ctx->current_page + 1);
    ESP_LOGI(TAG, "Content: %s", ctx->bookmarks[bookmark_index].content_preview);
    return true;
}

// Read a page of content
bool fiction_read_page(fiction_context_t* ctx, char lines[][MAX_LINE_LENGTH], int* line_count)
{
    if (!ctx->is_open) return false;
    
    FILE* fp = fopen(ctx->filepath, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "Failed to open file: %s", ctx->filepath);
        return false;
    }
    
    // Locate to the current position
    fseek(fp, ctx->current_position, SEEK_SET);
    
    *line_count = 0;
    char buffer[MAX_LINE_LENGTH * 2];
    
    while (*line_count < LINES_PER_PAGE && fgets(buffer, sizeof(buffer), fp)) {
        if (strcmp(ctx->encoding, "GBK/GB2312") == 0) {
            strncpy(lines[*line_count], buffer, MAX_LINE_LENGTH - 1);
        } else if (strstr(ctx->encoding, "UTF-8")) {
            strncpy(lines[*line_count], buffer, MAX_LINE_LENGTH - 1);
        } else {
            strncpy(lines[*line_count], buffer, MAX_LINE_LENGTH - 1);
        }
        
        lines[*line_count][MAX_LINE_LENGTH - 1] = '\0';
        
        char* newline = strchr(lines[*line_count], '\n');
        if (newline) *newline = '\0';
        
        if (strlen(lines[*line_count]) > 0) {
            (*line_count)++;
        }
    }
    ctx->current_position = ftell(fp);
    fclose(fp);
    
    return (*line_count > 0);
}

// closed file
void fiction_close_file(fiction_context_t* ctx)
{
    fiction_ai_read_stop(true);
    if (ctx->is_open) {
        if (ctx->is_epub) {
            fiction_epub_sync_fiction_context();
        }
        fiction_save_progress(ctx);
        ctx->is_open = false;
        if (ctx->is_epub) {
            fiction_epub_runtime_reset();
        }
        ESP_LOGI(TAG, "Fiction file closed");
    }
}

// Open the novel file
void page_fiction_open_file(const char* filepath, const char* encoding)
{
    memset(&s_fiction_ai_read, 0, sizeof(s_fiction_ai_read));
    if (!filepath || filepath[0] == '\0') {
        ESP_LOGE(TAG, "Invalid file path");
        return;
    }

    bool is_epub = is_epub_filepath(filepath);
    char readable_path[MAX_FILEPATH_LEN] = {0};
    const char* detected_encoding = nullptr;

    if (is_epub) {
        strncpy(readable_path, filepath, sizeof(readable_path) - 1);
        detected_encoding = "UTF-8";
    } else {
        strncpy(readable_path, filepath, sizeof(readable_path) - 1);
        detected_encoding = detect_txt_encoding(filepath);
        if (!detected_encoding || strstr(detected_encoding, "unknown") != NULL) {
            if (encoding && encoding[0] != '\0' && strstr(encoding, "unknown") == NULL) {
                detected_encoding = encoding;
            } else {
                detected_encoding = "UTF-8";
            }
        }
    }

    if (g_fiction_ctx.is_open) {
        fiction_close_file(&g_fiction_ctx);
    }
    
    // Initialize the context
    memset(&g_fiction_ctx, 0, sizeof(g_fiction_ctx));
    strncpy(g_fiction_ctx.filepath, readable_path, MAX_FILEPATH_LEN - 1);
    strncpy(g_fiction_ctx.source_filepath, filepath, MAX_FILEPATH_LEN - 1);
    g_fiction_ctx.is_epub = is_epub;
    strncpy(g_fiction_ctx.encoding, detected_encoding, sizeof(g_fiction_ctx.encoding) - 1);
    ESP_LOGI(TAG, "File encoding set to: %s", g_fiction_ctx.encoding);
    
    // Get the file size
    FILE* fp = fopen(readable_path, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "Failed to open readable file: %s", readable_path);
        return;
    }
    fseek(fp, 0, SEEK_END);
    g_fiction_ctx.file_size = ftell(fp);
    fclose(fp);
    
    g_fiction_ctx.is_open = true;
    
    // Load the reading progress
    fiction_load_progress(&g_fiction_ctx);
    if (g_fiction_ctx.is_epub) {
        g_fiction_ctx.current_position = (size_t)g_fiction_ctx.current_section;
    }
    fiction_load_bookmarks(&g_fiction_ctx);
    
    ESP_LOGI(TAG, "Fiction opened: src=%s read=%s, encoding=%s, size=%zu bytes, is_epub=%d",
             filepath, readable_path, g_fiction_ctx.encoding, g_fiction_ctx.file_size, (int)is_epub);
    
    // Start the novel reading task
    page_fiction_task();
}

static void fiction_epub_runtime_reset(void)
{
    if (g_epub_runtime.reader) {
        delete g_epub_runtime.reader;
        g_epub_runtime.reader = nullptr;
    }
    if (g_epub_runtime.renderer) {
        delete g_epub_runtime.renderer;
        g_epub_runtime.renderer = nullptr;
    }
    g_epub_runtime.initialized = false;
    g_epub_runtime.state = fiction_epub_rich::RichReadState{};
}

static void fiction_epub_configure_renderer(void)
{
    if (!g_epub_runtime.renderer) {
        return;
    }

    const int content_x = 10;
    const int content_y = 70;
    const int content_width = SCREEN_WIDTH - 20;
    const int content_height = SCREEN_HEIGHT - content_y - 30;
    cFONT* font = get_font_by_encoding(g_display_ctx.current_font_size, "UTF-8");
    int line_height = font->Height + 6;

    g_epub_runtime.renderer->configure(
        font, content_x, content_y, content_width, content_height, line_height);
}

static bool fiction_epub_runtime_init(void)
{
    if (!g_fiction_ctx.is_epub) {
        return false;
    }

    if (g_epub_runtime.initialized && g_epub_runtime.reader && g_epub_runtime.renderer) {
        return true;
    }

    fiction_epub_runtime_reset();

    g_epub_runtime.renderer = new fiction_epub_rich::EpaperFictionRenderer();
    if (!g_epub_runtime.renderer) {
        ESP_LOGE(TAG, "Failed to create epub renderer");
        return false;
    }

    fiction_epub_configure_renderer();

    const char* source = fiction_identity_filepath(&g_fiction_ctx);
    g_epub_runtime.state.path = source ? source : "";
    if (g_epub_runtime.state.path.empty()) {
        g_epub_runtime.state.path = g_fiction_ctx.filepath;
    }
    g_epub_runtime.state.current_section = g_fiction_ctx.current_section < 0
                                               ? 0
                                               : static_cast<uint16_t>(g_fiction_ctx.current_section);
    g_epub_runtime.state.current_page =
        g_fiction_ctx.current_page < 0 ? 0 : static_cast<uint16_t>(g_fiction_ctx.current_page);
    g_epub_runtime.state.pages_in_current_section =
        g_fiction_ctx.current_section_pages < 0
            ? 0
            : static_cast<uint16_t>(g_fiction_ctx.current_section_pages);

    g_epub_runtime.reader =
        new fiction_epub_rich::EpubRichReader(g_epub_runtime.state, g_epub_runtime.renderer);
    if (!g_epub_runtime.reader) {
        ESP_LOGE(TAG, "Failed to create epub reader");
        fiction_epub_runtime_reset();
        return false;
    }

    if (!g_epub_runtime.reader->load() ||
        !g_epub_runtime.reader->jump_to(g_epub_runtime.state.current_section,
                                        g_epub_runtime.state.current_page)) {
        ESP_LOGE(TAG, "Failed to initialize rich epub reader for %s", g_epub_runtime.state.path.c_str());
        fiction_epub_runtime_reset();
        return false;
    }

    g_epub_runtime.initialized = true;
    return true;
}

static void fiction_epub_sync_display_context(void)
{
    if (!g_epub_runtime.initialized || !g_epub_runtime.reader) {
        return;
    }

    const auto& state = g_epub_runtime.reader->state();
    g_display_ctx.current_section = state.current_section;
    g_display_ctx.current_page = state.current_page;
    g_display_ctx.current_section_pages = state.pages_in_current_section;
    g_display_ctx.current_position = state.current_section;
}

static void fiction_epub_sync_fiction_context(void)
{
    if (!g_epub_runtime.initialized || !g_epub_runtime.reader) {
        return;
    }

    const auto& state = g_epub_runtime.reader->state();
    g_fiction_ctx.current_section = state.current_section;
    g_fiction_ctx.current_page = state.current_page;
    g_fiction_ctx.current_section_pages = state.pages_in_current_section;
    g_fiction_ctx.current_position = state.current_section;
}

static bool fiction_epub_render_current_page(bool is_current)
{
    (void)is_current;
    if (!fiction_epub_runtime_init()) {
        return false;
    }

    page_cache_t* cache = &g_display_ctx.page_buffers[BUFFER_CURRENT];
    if (!cache || !cache->buffer) {
        return false;
    }

    fiction_epub_configure_renderer();
    g_epub_runtime.renderer->begin(cache->buffer);
    if (!g_epub_runtime.reader->render()) {
        ESP_LOGE(TAG, "Failed to render epub current page");
        return false;
    }

    fiction_epub_sync_display_context();
    fiction_epub_sync_fiction_context();

    Paint_SelectImage(cache->buffer);

    Paint_DrawRectangle(0, 0, SCREEN_WIDTH, 70, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    xSemaphoreTake(rtc_mutex, portMAX_DELAY);
    Time_data rtc_time = PCF85063_GetTime();
    xSemaphoreGive(rtc_mutex);
    display_fiction_time(rtc_time);

    int pages_in_section = g_display_ctx.current_section_pages;
    if (pages_in_section <= 0) pages_in_section = 1;

    Paint_DrawRectangle(0, SCREEN_HEIGHT - 42, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    if (s_fiction_ai_read.active) {
        Paint_DrawString_CN(20, SCREEN_HEIGHT - 42, "AI朗读中 功双击停止 ↑↓/Boot返回会中断",
                            &Font12_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(20, SCREEN_HEIGHT - 24, "功单书签 功按住笔记 ↑↑目录/字",
                            &Font12_UTF8, WHITE, BLACK);
    } else {
        Paint_DrawString_CN(20, SCREEN_HEIGHT - 42, "↑↓翻页 ↑↑目录/字 ↓↓字体 功单书签 功双朗读",
                            &Font12_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(20, SCREEN_HEIGHT - 24, "功按住笔记 Boot双击返回",
                            &Font12_UTF8, WHITE, BLACK);
    }
    char page_no[32];
    snprintf(page_no, sizeof(page_no), "页 %d/%d", g_display_ctx.current_page + 1, pages_in_section);
    Paint_DrawString_CN(SCREEN_WIDTH - 100, SCREEN_HEIGHT - 24, page_no, &Font12_UTF8, WHITE, BLACK);

    std::string page_text = g_epub_runtime.reader->get_current_page_text(static_cast<int>(sizeof(cache->content) - 1));
    fiction_trim_trailing_whitespace(page_text);
    strncpy(cache->content, page_text.c_str(), sizeof(cache->content) - 1);
    cache->content[sizeof(cache->content) - 1] = '\0';

    cache->file_position = g_display_ctx.current_position;
    cache->page_number = g_display_ctx.current_page;
    cache->is_valid = true;
    cache->is_rendering = false;
    cache->lines_count = 0;
    return true;
}

static bool fiction_epub_turn_next(void)
{
    if (!fiction_epub_runtime_init()) {
        return false;
    }
    if (!g_epub_runtime.reader->next()) {
        ESP_LOGI(TAG, "Reached end of EPUB");
        return false;
    }
    if (!fiction_epub_render_current_page(true)) {
        return false;
    }
    fiction_display_refresh(g_display_ctx.page_buffers[BUFFER_CURRENT].buffer, false);
    return true;
}

static bool fiction_epub_turn_prev(void)
{
    if (!fiction_epub_runtime_init()) {
        return false;
    }
    if (!g_epub_runtime.reader->prev()) {
        ESP_LOGI(TAG, "Already at beginning of EPUB");
        return false;
    }
    if (!fiction_epub_render_current_page(true)) {
        return false;
    }
    fiction_display_refresh(g_display_ctx.page_buffers[BUFFER_CURRENT].buffer, false);
    return true;
}

static bool fiction_epub_jump_to(uint16_t section, uint16_t page)
{
    if (!fiction_epub_runtime_init()) {
        return false;
    }
    if (!g_epub_runtime.reader->jump_to(section, page)) {
        ESP_LOGW(TAG, "Failed to jump to epub section=%u page=%u",
                 (unsigned)section, (unsigned)page);
        return false;
    }
    if (!fiction_epub_render_current_page(true)) {
        return false;
    }
    fiction_display_refresh(g_display_ctx.page_buffers[BUFFER_CURRENT].buffer, false);
    return true;
}

static int fiction_epub_get_toc_display_count(void)
{
    if (!fiction_epub_runtime_init() || !g_epub_runtime.reader) {
        return 0;
    }

    int toc_count = g_epub_runtime.reader->get_toc_count();
    if (toc_count > 0) {
        return toc_count;
    }

    int spine_count = g_epub_runtime.reader->get_spine_count();
    return spine_count > 0 ? spine_count : 0;
}

static int fiction_epub_get_section_for_toc_display(int display_index)
{
    if (!fiction_epub_runtime_init() || !g_epub_runtime.reader) {
        return -1;
    }

    int toc_count = g_epub_runtime.reader->get_toc_count();
    if (toc_count > 0) {
        int section = g_epub_runtime.reader->get_section_for_toc(display_index);
        if (section >= 0) {
            return section;
        }
    }

    int spine_count = g_epub_runtime.reader->get_spine_count();
    if (display_index < 0 || display_index >= spine_count) {
        return -1;
    }
    return display_index;
}

static void fiction_epub_get_toc_display_title(int display_index, char* out, size_t out_len)
{
    if (!out || out_len == 0) return;
    out[0] = '\0';

    if (!fiction_epub_runtime_init() || !g_epub_runtime.reader) {
        snprintf(out, out_len, "目录项 %d", display_index + 1);
        return;
    }

    int toc_count = g_epub_runtime.reader->get_toc_count();
    if (toc_count > 0) {
        std::string title = g_epub_runtime.reader->get_toc_title(display_index);
        if (!title.empty()) {
            snprintf(out, out_len, "%s", title.c_str());
            return;
        }
    }

    int section = fiction_epub_get_section_for_toc_display(display_index);
    if (section >= 0) {
        snprintf(out, out_len, "章节 %d", section + 1);
    } else {
        snprintf(out, out_len, "目录项 %d", display_index + 1);
    }
}

static int fiction_epub_get_toc_page_size(void)
{
    int top = 70;
    int bottom = SCREEN_HEIGHT - 30;
    int line_height = 28;
    int size = (bottom - top) / line_height;
    if (size < 3) size = 3;
    return size;
}

static void fiction_epub_display_toc_screen(int selected_index, int top_index)
{
    if (!bookmark_display_buffer) return;

    int total = fiction_epub_get_toc_display_count();
    if (total <= 0) {
        display_loading_fiction("目录为空", Partial_refresh);
        display_current_page();
        return;
    }

    if (selected_index < 0) selected_index = 0;
    if (selected_index >= total) selected_index = total - 1;

    int page_size = fiction_epub_get_toc_page_size();
    if (top_index > selected_index) top_index = selected_index;
    if (top_index < 0) top_index = 0;
    if (selected_index >= top_index + page_size) {
        top_index = selected_index - page_size + 1;
    }

    Paint_NewImage(bookmark_display_buffer, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_SelectImage(bookmark_display_buffer);
    Paint_Clear(WHITE);

    Paint_DrawString_CN(20, 10, "EPUB目录", &Font24_UTF8, WHITE, BLACK);
    char info[80];
    snprintf(info, sizeof(info), "共 %d 项，当前 %d", total, selected_index + 1);
    Paint_DrawString_CN(20, 40, info, &Font12_UTF8, WHITE, BLACK);
    Paint_DrawLine(20, 62, SCREEN_WIDTH - 20, 62, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    int line_height = 28;
    int y = 70;
    for (int row = 0; row < page_size; ++row) {
        int idx = top_index + row;
        if (idx >= total) break;
        char title[128];
        fiction_epub_get_toc_display_title(idx, title, sizeof(title));
        char line[168];
        snprintf(line, sizeof(line), "%s %d. %s", (idx == selected_index) ? ">" : " ", idx + 1, title);
        Paint_DrawString_CN(20, y, line, &Font16_UTF8, WHITE, BLACK);
        y += line_height;
    }

    Paint_DrawString_CN(20, SCREEN_HEIGHT - 25, "↑↓选择,确认跳转,返回退出", &Font12_UTF8, WHITE, BLACK);
    display_time_bet_fiction(bookmark_display_buffer);
    fiction_display_refresh(bookmark_display_buffer, false);
}


// Obtain the corresponding font based on the code
static cFONT* get_font_by_encoding(font_size_t font_size, const char* encoding) {
    if (font_size >= FONT_SIZE_MAX) return &Font16_UTF8;
    
    if (strstr(encoding, "GBK") || strstr(encoding, "GB2312")) {
        ESP_LOGI(TAG, "Using GBK font: %s", font_names[font_size]);
        return gbk_font_table[font_size];
    } else {
        ESP_LOGI(TAG, "Using UTF-8 font: %s", font_names[font_size]);
        return utf8_font_table[font_size];
    }
}

void Forced_refresh_fiction(const uint8_t* button)
{
    fiction_display_refresh(button, true);
}
void Forced_Refresh_page_fiction(const uint8_t* button)
{
    fiction_display_refresh(button, false);
}
static int Sleep_wake_fiction(const bool font_menu_mode,
                              const bool bookmark_mode,
                              const bool bookmark_action_mode,
                              const bool epub_toc_mode)
{
    int button = 0;
    int sleep_js = 0;
    Time_data rtc_time = {0};
    int last_minutes = -1;
    ESP_LOGI("home", "EPD_Sleep");
    EPD_Sleep();
    while(1)
    {
        button = wait_key_event_and_return_code(pdMS_TO_TICKS(1000));
        if (button == 12){
            ESP_LOGI("home", "EPD_Init");
            EPD_Init();
            break;
        } else if (button == 8 || button == 22 || button == 14 || button == 0 || button == 7 || button == 15 || button == 1){
            // 初始化
            ESP_LOGI("home", "EPD_Init");
            EPD_Init();
            if (!font_menu_mode && !bookmark_mode && !bookmark_action_mode && !epub_toc_mode) {
                Forced_Refresh_page_fiction(g_display_ctx.page_buffers[BUFFER_CURRENT].buffer);
                ESP_LOGI(TAG, "Time/battery updated in reading mode");
            } else if (epub_toc_mode) {
                Forced_Refresh_page_fiction(bookmark_display_buffer);
                ESP_LOGI(TAG, "Time/battery updated in epub toc mode");
            } else if (bookmark_mode && !bookmark_action_mode) {
                // display_time_bet_fiction(bookmark_display_buffer);
                Forced_Refresh_page_fiction(bookmark_display_buffer);
                ESP_LOGI(TAG, "Time/battery updated in bookmark mode");
            } else if (bookmark_action_mode) {
                // display_time_bet_fiction(bookmark_preview_buffer);
                Forced_Refresh_page_fiction(bookmark_preview_buffer);
                ESP_LOGI(TAG, "Time/battery updated in bookmark action mode");
            }
            break;
        } 
        xSemaphoreTake(rtc_mutex, portMAX_DELAY);
        rtc_time = PCF85063_GetTime();
        xSemaphoreGive(rtc_mutex);
        if(rtc_time.minutes != last_minutes) {
            last_minutes = rtc_time.minutes;
            ESP_LOGI("home", "EPD_Init");
            EPD_Init();
            if (!font_menu_mode && !bookmark_mode && !bookmark_action_mode && !epub_toc_mode) {
                Paint_SelectImage(g_display_ctx.page_buffers[BUFFER_CURRENT].buffer);
                display_fiction_time(rtc_time);
                Forced_Refresh_page_fiction(g_display_ctx.page_buffers[BUFFER_CURRENT].buffer);
                ESP_LOGI(TAG, "Time/battery updated in reading mode");
            } else if (epub_toc_mode) {
                Paint_SelectImage(bookmark_display_buffer);
                display_time_bet_fiction(bookmark_display_buffer);
                Forced_Refresh_page_fiction(bookmark_display_buffer);
                ESP_LOGI(TAG, "Time/battery updated in epub toc mode");
            } else if (bookmark_mode && !bookmark_action_mode) {
                Paint_SelectImage(bookmark_display_buffer);
                display_time_bet_fiction(bookmark_display_buffer);
                Forced_Refresh_page_fiction(bookmark_display_buffer);
                ESP_LOGI(TAG, "Time/battery updated in bookmark mode");
            } else if (bookmark_action_mode) {
                Paint_SelectImage(bookmark_preview_buffer);
                display_time_bet_fiction(bookmark_preview_buffer);
                Forced_Refresh_page_fiction(bookmark_preview_buffer);
                ESP_LOGI(TAG, "Time/battery updated in bookmark action mode");
            }
            ESP_LOGI("home", "EPD_Sleep");
            EPD_Sleep();
            // sleep_js++;
            // if(sleep_js > Unattended_Time){
            //     ESP_LOGI("home", "pwr_off");
            //     axp_pwr_off();
            // }
        }
    }
    return button;
}


// The main task is to read novels
void page_fiction_task(void)
{
    if (!g_fiction_ctx.is_open) {
        ESP_LOGE(TAG, "No fiction file opened");
        return;
    }

    // Initialize the display system
    init_fiction_display_buffers();
    init_bookmark_display_buffers();

    display_loading_fiction("小说加载中...",Partial_refresh);
    
    // Set the display context
    strncpy(g_display_ctx.filepath, g_fiction_ctx.filepath, sizeof(g_display_ctx.filepath) - 1);
    strncpy(g_display_ctx.encoding, g_fiction_ctx.encoding, sizeof(g_display_ctx.encoding) - 1);
    g_display_ctx.file_size = g_fiction_ctx.file_size;
    g_display_ctx.current_position = g_fiction_ctx.current_position;
    g_display_ctx.current_page = g_fiction_ctx.current_page;
    g_display_ctx.current_section = g_fiction_ctx.current_section;
    g_display_ctx.current_section_pages = g_fiction_ctx.current_section_pages;
    g_display_ctx.is_open = true;
    // init_fiction_display_buffers() runs before encoding is copied; recompute now.
    calculate_display_params();
    if (g_fiction_ctx.is_epub) {
        fiction_epub_runtime_reset();
        if (!fiction_epub_runtime_init()) {
            free_fiction_display_buffers();
            free_bookmark_display_buffers();
            g_fiction_ctx.is_open = false;
            ESP_LOGE(TAG, "EPUB rich runtime init failed");
            return;
        }
    }
    
    int button = -1;
    bool font_menu_mode = false;
    font_size_t font_menu_selection = g_display_ctx.current_font_size;
    bool bookmark_mode = false;
    bool bookmark_action_mode = false;
    int bookmark_selection = 0;
    int option_selection = 0;
    bool epub_toc_mode = false;
    int epub_toc_selection = 0;
    int epub_toc_top_index = 0;

    int time_count = 0;
    Time_data rtc_time = {0};
    int last_minutes = -1;
    
    // Display the first page
    display_current_page();
    s_fiction_note_recording = false;
    
    ESP_LOGI(TAG, "Fiction display started with bookmark support");

    rtc_time = PCF85063_GetTime();
    last_minutes = rtc_time.minutes;
    
    while (1) {
        fiction_ai_read_pump();
        bool reading_note_mode_active = Application::GetInstance().IsReadingNoteMode();
        uint32_t wait_timeout = s_fiction_ai_read.active ? pdMS_TO_TICKS(200) : pdMS_TO_TICKS(1000);
        button = wait_key_event_and_return_code(wait_timeout);
        if(button == -1 && !s_fiction_ai_read.active && !reading_note_mode_active && !s_fiction_note_recording) time_count++;
        if(time_count >= EPD_Sleep_Time) {
            button = Sleep_wake_fiction(font_menu_mode, bookmark_mode, bookmark_action_mode, epub_toc_mode);
        }
        if (font_menu_mode) {
            // Font selection mode
            switch (button) {
                case 14: // The next font
                    {
                        int temp = (int)font_menu_selection + 1;
                        if (temp >= FONT_SIZE_MAX) {
                            temp = 0;
                        }
                        font_menu_selection = (font_size_t)temp;
                    }
                    display_font_menu(font_menu_selection);
                    time_count = 0;
                    break;
                    
                case 0: // The previous font
                    {
                        int temp = (int)font_menu_selection - 1;
                        if (temp < 0) {
                            temp = FONT_SIZE_MAX - 1;
                        }
                        font_menu_selection = (font_size_t)temp;
                    }
                    display_font_menu(font_menu_selection);
                    time_count = 0;
                    break;
                    
                case 7: // Apply style font size
                    switch_font_size(font_menu_selection);
                    font_menu_mode = false;
                    display_current_page();
                    ESP_LOGI(TAG, "Font applied: %s", font_names[font_menu_selection]);
                    time_count = 0;
                    break;
                    
                case 8: // 取消
                case 22:
                    font_menu_mode = false;
                    display_current_page();
                    time_count = 0;
                    break;
            }
        } else if (epub_toc_mode) {
            int toc_total = fiction_epub_get_toc_display_count();
            int toc_page_size = fiction_epub_get_toc_page_size();
            switch (button) {
                case 14:
                    if (toc_total > 0) {
                        epub_toc_selection++;
                        if (epub_toc_selection >= toc_total) epub_toc_selection = 0;
                        if (epub_toc_selection >= epub_toc_top_index + toc_page_size) {
                            epub_toc_top_index = epub_toc_selection - toc_page_size + 1;
                        } else if (epub_toc_selection < epub_toc_top_index) {
                            epub_toc_top_index = epub_toc_selection;
                        }
                        fiction_epub_display_toc_screen(epub_toc_selection, epub_toc_top_index);
                    }
                    time_count = 0;
                    break;
                case 0:
                    if (toc_total > 0) {
                        epub_toc_selection--;
                        if (epub_toc_selection < 0) epub_toc_selection = toc_total - 1;
                        if (epub_toc_selection < epub_toc_top_index) {
                            epub_toc_top_index = epub_toc_selection;
                        } else if (epub_toc_selection >= epub_toc_top_index + toc_page_size) {
                            epub_toc_top_index = epub_toc_selection - toc_page_size + 1;
                        }
                        fiction_epub_display_toc_screen(epub_toc_selection, epub_toc_top_index);
                    }
                    time_count = 0;
                    break;
                case 7: {
                    int target_section = fiction_epub_get_section_for_toc_display(epub_toc_selection);
                    if (target_section >= 0 && fiction_epub_jump_to((uint16_t)target_section, 0)) {
                        g_fiction_ctx.current_position = g_display_ctx.current_position;
                        g_fiction_ctx.current_page = g_display_ctx.current_page;
                        g_fiction_ctx.current_section = g_display_ctx.current_section;
                        g_fiction_ctx.current_section_pages = g_display_ctx.current_section_pages;
                        fiction_save_progress(&g_fiction_ctx);
                    }
                    epub_toc_mode = false;
                    time_count = 0;
                    break;
                }
                case 8:
                case 22:
                    epub_toc_mode = false;
                    display_current_page();
                    time_count = 0;
                    break;
                case 12:
                    Forced_refresh_fiction(bookmark_display_buffer);
                    time_count = 0;
                    break;
                default:
                    break;
            }
        } else if (bookmark_action_mode) {
            // Bookmark operation mode
            switch (button) {
                case 14: // The next option
                    option_selection++;
                    if (option_selection > 2) option_selection = 0;
                    display_bookmark_action_menu_on_screen_Down(option_selection, Partial_refresh);
                    time_count = 0;
                    break;
                    
                case 0: // Previous option
                    option_selection--;
                    if (option_selection < 0) option_selection = 2;
                    display_bookmark_action_menu_on_screen_Up(option_selection, Partial_refresh);
                    time_count = 0;
                    break;
                    
                case 7: // Perform the selection operation
                    switch (option_selection) {
                        case 0: // Jump to Bookmarks
                            if (fiction_jump_to_bookmark(&g_fiction_ctx, bookmark_selection)) {
                                g_display_ctx.current_position = g_fiction_ctx.current_position;
                                g_display_ctx.current_page = g_fiction_ctx.current_page;
                                g_display_ctx.current_section = g_fiction_ctx.current_section;
                                for (int i = 0; i < 3; i++) {
                                    g_display_ctx.page_buffers[i].is_valid = false;
                                }
                                if (g_fiction_ctx.is_epub) {
                                    fiction_epub_jump_to((uint16_t)g_fiction_ctx.current_section,
                                                         (uint16_t)g_fiction_ctx.current_page);
                                } else {
                                    display_current_page();
                                }
                            }
                            bookmark_action_mode = false;
                            bookmark_mode = false;
                            break;
                            
                        case 1: // Delete bookmarks
                            fiction_delete_bookmark(&g_fiction_ctx, bookmark_selection);
                            if (g_fiction_ctx.bookmark_count == 0) {
                                restore_current_page();
                                bookmark_action_mode = false;
                                bookmark_mode = false;
                            } else {
                                if (bookmark_selection >= g_fiction_ctx.bookmark_count) {
                                    bookmark_selection = g_fiction_ctx.bookmark_count - 1;
                                }
                                display_loading_fiction("书签加载中...",Partial_refresh);
                                display_bookmark_list_on_screen(&g_fiction_ctx, bookmark_selection);
                                bookmark_action_mode = false;
                            }
                            break;
                            
                        case 2: // Return to the bookmark list
                            restore_bookmark_page();
                            bookmark_action_mode = false;
                            break;
                    }
                    time_count = 0;
                    break;
                    
                case 8: // cancel
                case 22:
                    restore_bookmark_page();
                    bookmark_action_mode = false;
                    time_count = 0;
                    break;

                case 12:
                    Forced_refresh_fiction(bookmark_preview_buffer);
                    time_count = 0;
                    break;
            }
        } else if (bookmark_mode) {
            // Bookmark selection mode
            switch (button) {
                case 14:
                    if (g_fiction_ctx.bookmark_count > 0) {
                        bookmark_selection++;
                        if (bookmark_selection >= g_fiction_ctx.bookmark_count) {
                            bookmark_selection = 0;
                        }
                        display_bookmark_list_on_screen_Down(bookmark_selection, Partial_refresh);
                    }
                    time_count = 0;
                    break;
                    
                case 0:
                    if (g_fiction_ctx.bookmark_count > 0) {
                        bookmark_selection--;
                        if (bookmark_selection < 0) {
                            bookmark_selection = g_fiction_ctx.bookmark_count - 1;
                        }
                        display_bookmark_list_on_screen_Up(bookmark_selection, Partial_refresh);
                    }
                    time_count = 0;
                    break;
                    
                case 7:
                    if (g_fiction_ctx.bookmark_count > 0) {
                        option_selection = 0;
                        display_loading_fiction("书签预览加载中...",Partial_refresh);
                        display_bookmark_action_menu_on_screen(&g_fiction_ctx, bookmark_selection, option_selection);
                        bookmark_action_mode = true;
                    }
                    time_count = 0;
                    break;
                    
                case 8: // Add a bookmark (in bookmark mode)
                    g_fiction_ctx.current_position = g_display_ctx.current_position;
                    g_fiction_ctx.current_page = g_display_ctx.current_page;
                    g_fiction_ctx.current_section = g_display_ctx.current_section;
                    g_fiction_ctx.current_section_pages = g_display_ctx.current_section_pages;
                    fiction_add_bookmark(&g_fiction_ctx);

                    bookmark_selection = g_fiction_ctx.bookmark_count - 1;
                    display_loading_fiction("书签加载中...",Partial_refresh);
                    display_bookmark_list_on_screen(&g_fiction_ctx, bookmark_selection);
                    time_count = 0;
                    break;
                    
                case 22: // Exit bookmark mode
                    restore_current_page();
                    bookmark_mode = false;
                    time_count = 0;
                    break;

                case 12:
                    Forced_refresh_fiction(bookmark_display_buffer);
                    time_count = 0;
                    break;
            }
        } else {
            // Normal reading mode
            Application& app = Application::GetInstance();
            if (reading_note_mode_active) {
                if (button == 10 && s_fiction_note_recording) {
                    app.StopListening();
                    s_fiction_note_recording = false;
                    auto display = Board::GetInstance().GetDisplay();
                    display->ShowNotification("读书笔记识别中");
                    ESP_LOGI(TAG, "Reading note recording stopped, waiting for STT");
                    time_count = 0;
                    continue;
                }
                if (button == 9 && !s_fiction_note_recording) {
                    time_count = 0;
                    continue;
                }
                if (button != -1) {
                    auto display = Board::GetInstance().GetDisplay();
                    display->ShowNotification(s_fiction_note_recording ? "读书笔记录音中" : "读书笔记识别中");
                    ESP_LOGI(TAG, "Ignore button %d while reading note mode is active", button);
                    time_count = 0;
                }
                continue;
            }

            switch (button) {
                case 14: // next page
                    if (s_fiction_ai_read.active) {
                        fiction_ai_read_stop(false);
                    }
                    if (turn_to_next_page()) {
                        g_fiction_ctx.current_position = g_display_ctx.current_position;
                        g_fiction_ctx.current_page = g_display_ctx.current_page;
                        g_fiction_ctx.current_section = g_display_ctx.current_section;
                        g_fiction_ctx.current_section_pages = g_display_ctx.current_section_pages;
                        fiction_save_progress(&g_fiction_ctx);
                    }
                    time_count = 0;
                    break;
                    
                case 0: // previous page
                    if (s_fiction_ai_read.active) {
                        fiction_ai_read_stop(false);
                    }
                    if (turn_to_previous_page()) {
                        g_fiction_ctx.current_position = g_display_ctx.current_position;
                        g_fiction_ctx.current_page = g_display_ctx.current_page;
                        g_fiction_ctx.current_section = g_display_ctx.current_section;
                        g_fiction_ctx.current_section_pages = g_display_ctx.current_section_pages;
                        fiction_save_progress(&g_fiction_ctx);
                    }
                    time_count = 0;
                    break;
                    
                case 15: // Double-click DOWN - Font Settings
                    if (s_fiction_ai_read.active) {
                        fiction_ai_read_stop(false);
                    }
                    font_menu_selection = g_display_ctx.current_font_size;
                    display_font_menu(font_menu_selection);
                    font_menu_mode = true;
                    time_count = 0;
                    break;
                    
                case 1: // Double-click on - Font Settings (Standby)
                    if (s_fiction_ai_read.active) {
                        fiction_ai_read_stop(false);
                    }
                    if (g_fiction_ctx.is_epub) {
                        int toc_total = fiction_epub_get_toc_display_count();
                        if (toc_total > 0) {
                            epub_toc_selection = g_display_ctx.current_section;
                            if (epub_toc_selection < 0) epub_toc_selection = 0;
                            if (epub_toc_selection >= toc_total) epub_toc_selection = toc_total - 1;
                            int toc_page_size = fiction_epub_get_toc_page_size();
                            epub_toc_top_index = (epub_toc_selection / toc_page_size) * toc_page_size;
                            fiction_epub_display_toc_screen(epub_toc_selection, epub_toc_top_index);
                            epub_toc_mode = true;
                        }
                    } else {
                        font_menu_selection = g_display_ctx.current_font_size;
                        display_font_menu(font_menu_selection);
                        font_menu_mode = true;
                    }
                    time_count = 0;
                    break;
                    
                case 7: // Bookmark function
                    if (s_fiction_ai_read.active) {
                        fiction_ai_read_stop(false);
                    }
                    if (g_fiction_ctx.bookmark_count > 0) {
                        bookmark_selection = 0;
                        display_loading_fiction("书签加载中...",Partial_refresh);
                        display_bookmark_list_on_screen(&g_fiction_ctx, bookmark_selection);
                        bookmark_mode = true;
                    } else {
                        display_loading_fiction("书签加载中...",Partial_refresh);
                        display_bookmark_list_on_screen(&g_fiction_ctx, 0);
                        bookmark_mode = true;
                    }
                    time_count = 0;
                    break;

                case 22: // quit
                    fiction_ai_read_stop(true);
                    g_fiction_ctx.current_position = g_display_ctx.current_position;
                    g_fiction_ctx.current_page = g_display_ctx.current_page;
                    g_fiction_ctx.current_section = g_display_ctx.current_section;
                    g_fiction_ctx.current_section_pages = g_display_ctx.current_section_pages;
                    fiction_close_file(&g_fiction_ctx);
                    free_fiction_display_buffers();
                    free_bookmark_display_buffers();
                    ESP_LOGI(TAG, "Fiction reading closed");
                    time_count = 0;
                    return;
                
                case 8:
                    if (!fiction_ai_read_toggle()) {
                        ESP_LOGW(TAG, "Failed to toggle AI reading");
                    }
                    time_count = 0;
                    break;

                case 9: // Hold FUNCTION to start reading note recording
                    if (s_fiction_ai_read.active) {
                        fiction_ai_read_stop(false);
                    }
                    app.EnableReadingNoteMode();
                    app.StartListening();
                    s_fiction_note_recording = true;
                    Board::GetInstance().GetDisplay()->ShowNotification("读书笔记录音中");
                    ESP_LOGI(TAG, "Reading note recording started on page=%d", fiction_get_current_page_number());
                    time_count = 0;
                    break;
                    
                default:
                    break;
            }
        }

        rtc_time = PCF85063_GetTime();
        if ((rtc_time.minutes != last_minutes) && (time_count < EPD_Sleep_Time)) {
            last_minutes = rtc_time.minutes;
            if (!font_menu_mode && !bookmark_mode && !bookmark_action_mode && !epub_toc_mode) {
                Paint_SelectImage(g_display_ctx.page_buffers[BUFFER_CURRENT].buffer);
                display_fiction_time(rtc_time);
                Forced_Refresh_page_fiction(g_display_ctx.page_buffers[BUFFER_CURRENT].buffer);
                ESP_LOGI(TAG, "Time/battery updated in reading mode");
            } else if (epub_toc_mode) {
                Paint_SelectImage(bookmark_display_buffer);
                display_time_bet_fiction(bookmark_display_buffer);
                Forced_Refresh_page_fiction(bookmark_display_buffer);
                ESP_LOGI(TAG, "Time/battery updated in epub toc mode");
            } else if (bookmark_mode && !bookmark_action_mode) {
                Paint_SelectImage(bookmark_display_buffer);
                display_time_bet_fiction(bookmark_display_buffer);
                Forced_Refresh_page_fiction(bookmark_display_buffer);
                ESP_LOGI(TAG, "Time/battery updated in bookmark mode");
            } else if (bookmark_action_mode) {
                Paint_SelectImage(bookmark_preview_buffer);
                display_time_bet_fiction(bookmark_preview_buffer);
                Forced_Refresh_page_fiction(bookmark_preview_buffer);
                ESP_LOGI(TAG, "Time/battery updated in bookmark action mode");
            }
        }
    }
    
    free_fiction_display_buffers();
    free_bookmark_display_buffers();
    fiction_close_file(&g_fiction_ctx);
}

// Chinese character detection function
bool is_chinese_filename(const char* filename) {
    if (!filename) return false;
    
    for (int i = 0; filename[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)filename[i];
        if (ch >= 0x80) {
            return true;
        }
    }
    return false;
}

// Release the display cache
void free_fiction_display_buffers(void) {
    fiction_epub_runtime_reset();

    // Release the mutex
    if (fs_access_mutex) {
        vSemaphoreDelete(fs_access_mutex);
        fs_access_mutex = NULL;
        ESP_LOGI(TAG, "fs_access_mutex deleted");
    }

    heap_caps_free(page_backup_buffer);
    page_backup_buffer = NULL;
    for (int i = 0; i < 3; i++) {
        if (g_display_ctx.page_buffers[i].buffer) {
            heap_caps_free(g_display_ctx.page_buffers[i].buffer);
            g_display_ctx.page_buffers[i].buffer = NULL;
            g_display_ctx.page_buffers[i].is_valid = false;
            g_display_ctx.page_buffers[i].is_rendering = false;
        }
    }
    ESP_LOGI(TAG, "Fiction display buffers freed");
}

// Initialize the display cache area
void init_fiction_display_buffers(void) {
    size_t page_buffer_size = (SCREEN_WIDTH * SCREEN_HEIGHT) / 8;
    page_backup_buffer = (uint8_t*)heap_caps_malloc(page_buffer_size, MALLOC_CAP_SPIRAM);
    if (!page_backup_buffer) {
        ESP_LOGE(TAG, "Failed to allocate page buffer ");
        return;
    }

    for (int i = 0; i < 3; i++) {
        g_display_ctx.page_buffers[i].buffer = (uint8_t*)heap_caps_malloc(page_buffer_size, MALLOC_CAP_SPIRAM);
        if (!g_display_ctx.page_buffers[i].buffer) {
            ESP_LOGE(TAG, "Failed to allocate page buffer %d", i);
            return;
        }
        memset(g_display_ctx.page_buffers[i].buffer, 0xFF, page_buffer_size);
        g_display_ctx.page_buffers[i].is_valid = false;
        g_display_ctx.page_buffers[i].is_rendering = false;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE("fiction", "NVS init failed: %s", esp_err_to_name(err));
    }

    // Load the saved font index (if any)
    font_size_t saved_font = Default_font;
    if (load_font_size_from_nvs(&saved_font) == ESP_OK) {
        ESP_LOGI("fiction", "apply saved font size %d", saved_font);
        switch_font_size(saved_font);
    } else {
        ESP_LOGI("fiction", "no saved font, keep default");
    }
        
    // Set the default font
    g_display_ctx.current_font_size = saved_font;
    g_display_ctx.current_font = get_font_by_encoding(saved_font, g_display_ctx.encoding);
    g_display_ctx.current_buffer_index = BUFFER_CURRENT;
    
    calculate_display_params();
    
    ESP_LOGI(TAG, "Fiction display buffers initialized: %d bytes per page", (int)page_buffer_size);
}

// Count the number of characters (distinguish between Chinese and English)
int count_characters(const char* text, const char* encoding) {
    int char_count = 0;
    const char* p = text;
    
    while (*p != '\0') {
        int char_len = 1;
        
        if (strstr(encoding, "UTF-8") || strstr(encoding, "utf-8") ||
            strstr(encoding, "UTF8")  || strstr(encoding, "utf8")) {
            char_len = Get_UTF8_Char_Length((unsigned char)*p);
        } else {
            char_len = (*p < 0x80) ? 1 : 2;
        }
        
        char_count++;
        p += char_len;
    }
    
    return char_count;
}

// Calculate the display parameter function
void calculate_display_params(void) {
    g_display_ctx.current_font = get_font_by_encoding(g_display_ctx.current_font_size, g_display_ctx.encoding);
    cFONT* font = g_display_ctx.current_font;
    
    int start_y = 70;
    int footer_space = 30;
    int line_height = font->Height + 6;
    int content_height = SCREEN_HEIGHT - start_y - footer_space;
    
    g_display_ctx.lines_per_page = content_height / line_height;
    if (g_display_ctx.lines_per_page > kMaxLayoutLines) {
        ESP_LOGW(TAG, "lines_per_page=%d exceeds layout buffer=%d, clamped",
                 g_display_ctx.lines_per_page, (int)kMaxLayoutLines);
        g_display_ctx.lines_per_page = kMaxLayoutLines;
    }
    
    int left_margin = 20;
    int right_margin = 10;
    int available_width = SCREEN_WIDTH - left_margin - right_margin;
    
    int ch_char_width = font->Width_CH;
    int max_chars_by_width = available_width / ch_char_width;
    
    if (strstr(g_display_ctx.encoding, "GBK") || strstr(g_display_ctx.encoding, "GB2312")) {
        g_display_ctx.chars_per_line = max_chars_by_width + 2;
    } else {
        g_display_ctx.chars_per_line = max_chars_by_width + 1;
    }
    
    int absolute_max = (available_width * 110) / (ch_char_width * 100);
    if (g_display_ctx.chars_per_line > absolute_max) {
        g_display_ctx.chars_per_line = absolute_max;
    }
    
    ESP_LOGI(TAG, "Display params optimized:");
    ESP_LOGI(TAG, "  Available width: %d px, char width: %d px", available_width, ch_char_width);
    ESP_LOGI(TAG, "  Result: %d lines × %d chars (max_by_width=%d, absolute_max=%d)", g_display_ctx.lines_per_page, g_display_ctx.chars_per_line, max_chars_by_width, absolute_max);
}

// Switch font size
void switch_font_size(font_size_t font_size) {
    if (font_size >= FONT_SIZE_MAX) return;
    
    g_display_ctx.current_font_size = font_size;
    // Select the corresponding font based on the current encoding
    g_display_ctx.current_font = get_font_by_encoding(font_size, g_display_ctx.encoding);
    
    // Recalculate the display parameters
    calculate_display_params();

    for (int i = 0; i < 3; i++) {
        g_display_ctx.page_buffers[i].is_valid = false;
    }
    if (g_fiction_ctx.is_epub && g_epub_runtime.initialized && g_epub_runtime.reader) {
        fiction_epub_configure_renderer();
        g_epub_runtime.reader->invalidate_layout();
    }
    
    // Save the selection to NVS
    esp_err_t err = save_font_size_to_nvs(font_size);
    if (err != ESP_OK) {
        ESP_LOGW("fiction", "save font idx failed: %s", esp_err_to_name(err));
    }
    
    ESP_LOGI(TAG, "Font switched to: %s (%s encoding)", font_names[font_size], g_display_ctx.encoding);
}

// Calculate the byte length of the character
int get_char_byte_length(const char* str, int pos, const char* encoding) {
    if (strstr(encoding, "GBK") || strstr(encoding, "GB2312")) {
        unsigned char ch = (unsigned char)str[pos];
        if (ch >= 0xA1 && ch <= 0xFE) {
            return 2; 
        } else {
            return 1;
        }
    } else {
        unsigned char ch = (unsigned char)str[pos];
        if (ch < 0x80) {
            return 1;
        } else if ((ch & 0xE0) == 0xC0) {
            return 2; 
        } else if ((ch & 0xF0) == 0xE0) {
            return 3; 
        } else if ((ch & 0xF8) == 0xF0) {
            return 4; 
        }
        return 1;
    }
}

// Extract the string by the number of characters
int copy_chars_by_count(const char* source, char* dest, int target_chars, const char* encoding) {
    int copied_chars = 0;
    int bytes_copied = 0;
    const char* p = source;
    
    while (*p != '\0' && copied_chars < target_chars && bytes_copied < 510) { 
        int char_len = 1;
        
        if (strstr(encoding, "UTF-8") || strstr(encoding, "utf-8") ||
            strstr(encoding, "UTF8")  || strstr(encoding, "utf8")) {
            char_len = Get_UTF8_Char_Length((unsigned char)*p);
        } else {
            char_len = (*p < 0x80) ? 1 : 2;
        }
        
        if (bytes_copied + char_len >= 510) {
            ESP_LOGW(TAG, "Buffer boundary reached at char %d", copied_chars);
            break;
        }
        
        for (int i = 0; i < char_len && *(p + i) != '\0'; i++) {
            dest[bytes_copied++] = *(p + i);
        }
        
        copied_chars++;
        p += char_len;
        
        ESP_LOGV(TAG, "Copied char %d: len=%d bytes, total_chars=%d, total_bytes=%d", copied_chars, char_len, copied_chars, bytes_copied);
    }
    
    dest[bytes_copied] = '\0';
    ESP_LOGI(TAG, "copy_chars_by_count: target=%d, actual_chars=%d, bytes=%d", target_chars, copied_chars, bytes_copied);
    return bytes_copied;
}


// File reading
bool read_page_from_file(size_t start_position, char* content, int max_len, size_t* end_position) {
    FILE* fp = fopen(g_display_ctx.filepath, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "Failed to open file: %s", g_display_ctx.filepath);
        return false;
    }
    
    fseek(fp, start_position, SEEK_SET);
    
    int content_pos = 0;
    int line_count = 0;

    reset_txt_layout_lines();
    content[0] = '\0';
    int target_lines = g_display_ctx.lines_per_page;
    if (target_lines > kMaxLayoutLines) target_lines = kMaxLayoutLines;
    if (target_lines <= 0) {
        fclose(fp);
        return false;
    }
    
    ESP_LOGI(TAG, "Reading page: target %d lines, max_len=%d bytes", target_lines, max_len);

    int columns_count = 0;
    int line_count_char_len = 0; 
    const bool is_utf8 = strstr(g_display_ctx.encoding, "UTF") != NULL;
    bool paragraph_pending = true;
    bool line_is_paragraph_start = false;

    if (start_position > 0) {
        if (fseek(fp, (long)(start_position - 1), SEEK_SET) == 0) {
            int prev_ch = fgetc(fp);
            paragraph_pending = (prev_ch == '\n');
        } else {
            paragraph_pending = false;
        }
        fseek(fp, start_position, SEEK_SET);
    }

    auto finalize_line = [&](bool paragraph_end) {
        if (line_count >= target_lines) return;
        if (columns_count < kLineBufBytes) lines_char[line_count][columns_count] = '\0';
        else lines_char[line_count][kLineBufBytes - 1] = '\0';
        lines_paragraph_start[line_count] = line_is_paragraph_start;
        lines_paragraph_end[line_count] = paragraph_end;
        content_pos += columns_count;
        columns_count = 0;
        line_count_char_len = 0;
        line_is_paragraph_start = false;
        line_count++;
    };

    auto apply_indent_if_needed = [&](unsigned char first_byte, const unsigned char* seq, int seq_len) {
        if (columns_count != 0 || !paragraph_pending) return;
        bool has_leading_blank = (first_byte == ' ' || first_byte == '\t');
        if (!has_leading_blank && is_utf8 && seq_len == 3 &&
            seq[0] == 0xE3 && seq[1] == 0x80 && seq[2] == 0x80) {
            has_leading_blank = true;
        }
        line_is_paragraph_start = true;
        paragraph_pending = false;
        if (has_leading_blank) return;

        if (is_utf8) {
            static const unsigned char kIndentUtf8[] = {0xE3, 0x80, 0x80, 0xE3, 0x80, 0x80};
            const int indent_px = g_display_ctx.current_font->Width_CH * 2;
            if (6 < kLineBufBytes - 1 && line_count_char_len + indent_px <= (SCREEN_WIDTH - 20)) {
                memcpy(&lines_char[line_count][columns_count], kIndentUtf8, sizeof(kIndentUtf8));
                columns_count += (int)sizeof(kIndentUtf8);
                line_count_char_len += indent_px;
            }
        } else {
            const int indent_px = g_display_ctx.current_font->Width_EN * 2;
            if (2 < kLineBufBytes - 1 && line_count_char_len + indent_px <= (SCREEN_WIDTH - 20)) {
                lines_char[line_count][columns_count++] = ' ';
                lines_char[line_count][columns_count++] = ' ';
                line_count_char_len += indent_px;
            }
        }
    };

    int ch;

    while (line_count < target_lines) {
        ch = fgetc(fp);
        if (ch == EOF) break;
        unsigned char uc = (unsigned char)ch;
        if (uc == '\r') continue;
        if (uc == '\n') {
            finalize_line(true);
            paragraph_pending = true;
            continue;
        }

        int char_len = 1;
        int char_pixel = g_display_ctx.current_font->Width_EN;
        unsigned char tmp[4];
        tmp[0] = uc;

        if (is_utf8) {
            if (uc < 0x80) { char_len = 1; char_pixel = g_display_ctx.current_font->Width_EN; }
            else if ((uc & 0xE0) == 0xC0) { char_len = 2; char_pixel = g_display_ctx.current_font->Width_CH; }
            else if ((uc & 0xF0) == 0xE0) { char_len = 3; char_pixel = g_display_ctx.current_font->Width_CH; }
            else if ((uc & 0xF8) == 0xF0) { char_len = 4; char_pixel = g_display_ctx.current_font->Width_CH; }
            for (int i = 1; i < char_len; i++) {
                int nb = fgetc(fp);
                if (nb == EOF) { char_len = i; break; }
                tmp[i] = (unsigned char)nb;
            }
        } else {
            if (uc < 0x80) { char_len = 1; char_pixel = g_display_ctx.current_font->Width_EN; }
            else {
                char_len = 2; char_pixel = g_display_ctx.current_font->Width_CH;
                int nb = fgetc(fp);
                if (nb == EOF) { char_len = 1; }
                else tmp[1] = (unsigned char)nb;
            }
        }

        apply_indent_if_needed(uc, tmp, char_len);

        if (columns_count + char_len >= kLineBufBytes - 1) {
            for (int i = char_len - 1; i >= 0; i--) ungetc(tmp[i], fp);
            finalize_line(false);
            continue;
        }

        if (line_count_char_len + char_pixel > (SCREEN_WIDTH - 20)) {
            for (int i = char_len - 1; i >= 0; i--) ungetc(tmp[i], fp);
            finalize_line(false);
            continue;
        }

        for (int i = 0; i < char_len; i++) {
            lines_char[line_count][columns_count++] = tmp[i];
        }
        line_count_char_len += char_pixel;
    }

    if (columns_count > 0 && line_count < target_lines) {
        finalize_line(false);
    }
    lines_layout_count = line_count;

    long ft = ftell(fp);
    if (ft < 0) {
        ESP_LOGW(TAG, "ftell failed, using start_position as end_position");
        *end_position = start_position;
    } else {
        *end_position = (size_t)ft;
    }
    fclose(fp);
    
    float fill_rate = (float)line_count / target_lines * 100.0f;
    ESP_LOGI(TAG, "Page read result: %d/%d lines (%.1f%%), %d bytes", line_count, target_lines, fill_rate, content_pos);

    return (line_count > 0);
}

// Render canvas
void render_page_to_buffer(page_cache_t* cache, const char* content, bool is_current) {
    if (!cache || !cache->buffer || cache->is_rendering) return;
    
    cache->is_rendering = true;
    Paint_NewImage(cache->buffer, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_SelectImage(cache->buffer);
    Paint_Clear(WHITE);
    
    cFONT* font = get_font_by_encoding(g_display_ctx.current_font_size, g_display_ctx.encoding);
    
    if (is_current) {
        Paint_DrawRectangle(0, 0, SCREEN_WIDTH, 70, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        xSemaphoreTake(rtc_mutex, portMAX_DELAY);
        Time_data rtc_time = PCF85063_GetTime();
        xSemaphoreGive(rtc_mutex);
        display_fiction_time(rtc_time);
    }

    int start_y = 70;
    int line_height = font->Height + 6;
    int footer_space = 30;
    int content_height = SCREEN_HEIGHT - start_y - footer_space;
    int max_lines = content_height / line_height;
    int max_content_y = start_y + max_lines * line_height;
    
    ESP_LOGI(TAG, "Layout: start_y=%d, line_height=%d, footer_space=%d, max_lines=%d, max_content_y=%d", start_y, line_height, footer_space, max_lines, max_content_y);
    
    const char* p = content;
    const char* line_start = content;

    int target_lines = g_display_ctx.lines_per_page;
    if (target_lines > kMaxLayoutLines) target_lines = kMaxLayoutLines;
    int rendered_lines = lines_layout_count;
    if (rendered_lines < 0) rendered_lines = 0;
    if (rendered_lines > target_lines) rendered_lines = target_lines;

    for (int i = 0; i < rendered_lines; i++)
    {
        Paint_DrawString_CN(10, start_y + i*line_height, lines_char[i], font, WHITE, BLACK);
    }
    
    if (is_current) {
        char footer[100];
        snprintf(footer, sizeof(footer), "↑↓翻页 双击↑目录 双击↓字体");
        Paint_DrawString_CN(20, SCREEN_HEIGHT - 25, footer, &Font12_UTF8, WHITE, BLACK);
        char page_no[32];
        snprintf(page_no, sizeof(page_no), "页 %d", cache->page_number + 1);
        Paint_DrawString_CN(SCREEN_WIDTH - 90, SCREEN_HEIGHT - 25, page_no, &Font12_UTF8, WHITE, BLACK);
    }
    
    cache->lines_count = rendered_lines;
    cache->is_valid = true;
    cache->is_rendering = false;
    
    // ESP_LOGI(TAG, "Page rendered successfully: %d lines displayed", rendered_lines);
}

// Display time and battery level
void display_time_bet_fiction(uint8_t* buffer)
{
    Paint_SelectImage(buffer);
    char Time_str[16]={0};
    int BAT_Power;

    xSemaphoreTake(rtc_mutex, portMAX_DELAY);
    Time_data rtc_time = PCF85063_GetTime();
    xSemaphoreGive(rtc_mutex);
    snprintf(Time_str, sizeof(Time_str), "%02d:%02d", rtc_time.hours, rtc_time.minutes);
    Paint_DrawString_EN(SCREEN_WIDTH - 120, SCREEN_HEIGHT - 25, Time_str, &Font12, WHITE, BLACK);
#if defined(CONFIG_IMG_SOURCE_EMBEDDED)
    Paint_ReadBmp(gImage_BAT,SCREEN_WIDTH - 60, SCREEN_HEIGHT - 23,32,16);
#elif defined(CONFIG_IMG_SOURCE_TFCARD)
    GUI_ReadBmp(BMP_BAT_PATH,SCREEN_WIDTH - 60, SCREEN_HEIGHT - 23);
#endif
    
    BAT_Power = get_battery_power();
    ESP_LOGI("BAT_Power", "BAT_Power = %d%%",BAT_Power);
    if(BAT_Power == -1) BAT_Power = 20;
    else BAT_Power = BAT_Power * 20 / 100;
    Paint_DrawRectangle(SCREEN_WIDTH - 55, SCREEN_HEIGHT - 18, SCREEN_WIDTH - 75, SCREEN_HEIGHT - 10, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawRectangle(SCREEN_WIDTH - 55, SCREEN_HEIGHT - 18, SCREEN_WIDTH - 55 + BAT_Power, SCREEN_HEIGHT - 10, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
}

// Preload the next page
void preload_next_page(void) {
    if (g_fiction_ctx.is_epub) {
        return;
    }
    // if (fs_access_mutex) xSemaphoreTake(fs_access_mutex, pdMS_TO_TICKS(5000));
    if (fs_access_mutex) xSemaphoreTake(fs_access_mutex, portMAX_DELAY);

    page_cache_t* next_cache = &g_display_ctx.page_buffers[BUFFER_NEXT];
    page_cache_t* current_cache = &g_display_ctx.page_buffers[BUFFER_CURRENT];

    if (next_cache->is_valid || next_cache->is_rendering) {
        if (fs_access_mutex) xSemaphoreGive(fs_access_mutex);
        return;
    }

    size_t start_pos = current_cache->file_position;
    size_t end_pos;

    ESP_LOGI(TAG, "Preloading next page from position %zu", start_pos);

    if (read_page_from_file(start_pos, next_cache->content, sizeof(next_cache->content), &end_pos)) {
        next_cache->file_position = end_pos;
        next_cache->page_number = current_cache->page_number + 1;
        render_page_to_buffer(next_cache, next_cache->content, false);
        ESP_LOGI(TAG, "Next page preloaded successfully: page %d, pos %zu->%zu", next_cache->page_number, start_pos, end_pos);
    } else {
        ESP_LOGW(TAG, "Failed to preload next page (possibly end of file)");
    }

    if (fs_access_mutex) xSemaphoreGive(fs_access_mutex);
}

// Preload the previous page
void preload_previous_page(void) {
    if (g_fiction_ctx.is_epub) {
        return;
    }
    page_cache_t* prev_cache = &g_display_ctx.page_buffers[BUFFER_PREVIOUS];
    page_cache_t* current_cache = &g_display_ctx.page_buffers[BUFFER_CURRENT];
    
    if (prev_cache->is_valid || prev_cache->is_rendering || current_cache->page_number <= 0) {
        return;
    }
    
    size_t estimated_page_size = g_display_ctx.lines_per_page * g_display_ctx.chars_per_line;
    size_t start_pos = (current_cache->file_position > estimated_page_size) ? 
                       current_cache->file_position - estimated_page_size : 0;
    size_t end_pos;
    
    ESP_LOGI(TAG, "Preloading previous page from estimated position %zu", start_pos);
    
    if (read_page_from_file(start_pos, prev_cache->content, sizeof(prev_cache->content), &end_pos)) {
        prev_cache->file_position = start_pos;
        prev_cache->page_number = current_cache->page_number - 1;
        
        render_page_to_buffer(prev_cache, prev_cache->content, false);
        
        ESP_LOGI(TAG, "Previous page preloaded successfully: page %d", prev_cache->page_number);
    } else {
        ESP_LOGW(TAG, "Failed to preload previous page");
    }
}

// Display the current page
void display_current_page(void) {
    if (g_fiction_ctx.is_epub) {
        if (fiction_epub_render_current_page(true)) {
            fiction_display_refresh(g_display_ctx.page_buffers[BUFFER_CURRENT].buffer, false);
        }
        return;
    }
    // if (fs_access_mutex) xSemaphoreTake(fs_access_mutex, pdMS_TO_TICKS(5000));
    if (fs_access_mutex) xSemaphoreTake(fs_access_mutex, portMAX_DELAY);

    page_cache_t* current_cache = &g_display_ctx.page_buffers[BUFFER_CURRENT];
    if (!current_cache->is_valid) {
        size_t end_pos;
        if (read_page_from_file(g_display_ctx.current_position, current_cache->content, sizeof(current_cache->content), &end_pos)) {
            current_cache->file_position = end_pos;
            current_cache->page_number = g_display_ctx.current_page;
            render_page_to_buffer(current_cache, current_cache->content, true);
        }
    } else {
        render_page_to_buffer(current_cache, current_cache->content, true);
    }

    fiction_display_refresh(current_cache->buffer, false);

    if (fs_access_mutex) xSemaphoreGive(fs_access_mutex);

    preload_next_page();
    preload_previous_page();
}

// Turn to the next page
bool turn_to_next_page(void) {
    if (g_fiction_ctx.is_epub) {
        return fiction_epub_turn_next();
    }
    // if (fs_access_mutex) xSemaphoreTake(fs_access_mutex, pdMS_TO_TICKS(5000));
    if (fs_access_mutex) xSemaphoreTake(fs_access_mutex, portMAX_DELAY);

    page_cache_t* next_cache = &g_display_ctx.page_buffers[BUFFER_NEXT];
    if (!next_cache->is_valid) {
        ESP_LOGW(TAG, "Next page not preloaded, loading now...");
        size_t start_pos = g_display_ctx.page_buffers[BUFFER_CURRENT].file_position;
        size_t end_pos;
        if (!read_page_from_file(start_pos, next_cache->content, sizeof(next_cache->content), &end_pos)) {
            ESP_LOGE(TAG, "Failed to load next page");
            if (fs_access_mutex) xSemaphoreGive(fs_access_mutex);
            return false;
        }
        next_cache->file_position = end_pos;
        next_cache->page_number = g_display_ctx.current_page + 1;
        render_page_to_buffer(next_cache, next_cache->content, false);
    } else {
        ESP_LOGI(TAG, "Using preloaded next page (fast switch)");
    }

    // Cache exchange
    uint8_t* temp_buffer = g_display_ctx.page_buffers[BUFFER_PREVIOUS].buffer;
    char temp_content[sizeof(g_display_ctx.page_buffers[0].content)];
    strcpy(temp_content, g_display_ctx.page_buffers[BUFFER_PREVIOUS].content);
    g_display_ctx.page_buffers[BUFFER_PREVIOUS] = g_display_ctx.page_buffers[BUFFER_CURRENT];
    g_display_ctx.page_buffers[BUFFER_CURRENT] = g_display_ctx.page_buffers[BUFFER_NEXT];
    g_display_ctx.page_buffers[BUFFER_NEXT].buffer = temp_buffer;
    strcpy(g_display_ctx.page_buffers[BUFFER_NEXT].content, temp_content);
    g_display_ctx.page_buffers[BUFFER_NEXT].is_valid = false;
    g_display_ctx.page_buffers[BUFFER_NEXT].is_rendering = false;
    g_display_ctx.current_page = g_display_ctx.page_buffers[BUFFER_CURRENT].page_number;
    g_display_ctx.current_position = g_display_ctx.page_buffers[BUFFER_CURRENT].file_position;

    // UI + Display
    render_current_page_ui(&g_display_ctx.page_buffers[BUFFER_CURRENT]);
    fiction_display_refresh(g_display_ctx.page_buffers[BUFFER_CURRENT].buffer, false);

    if (fs_access_mutex) xSemaphoreGive(fs_access_mutex);

    // Preload the next page in the background
    xTaskCreate(preload_next_page_task, "preload_next", 10*1024, NULL, 5, NULL);

    ESP_LOGI(TAG, "Fast page turn to: %d", g_display_ctx.current_page);
    return true;
}

// Turn to the previous page
bool turn_to_previous_page(void) {
    if (g_fiction_ctx.is_epub) {
        return fiction_epub_turn_prev();
    }
    if (g_display_ctx.current_page <= 0) {
        ESP_LOGW(TAG, "Already at first page");
        return false;
    }
    
    page_cache_t* prev_cache = &g_display_ctx.page_buffers[BUFFER_PREVIOUS];
    
    if (!prev_cache->is_valid) {
        ESP_LOGW(TAG, "Previous page not preloaded, loading now...");
        size_t estimated_page_size = g_display_ctx.lines_per_page * g_display_ctx.chars_per_line;
        size_t start_pos = (g_display_ctx.current_position > estimated_page_size) ? g_display_ctx.current_position - estimated_page_size : 0;
        size_t end_pos;
        
        if (!read_page_from_file(start_pos, prev_cache->content, sizeof(prev_cache->content), &end_pos)) {
            ESP_LOGE(TAG, "Failed to load previous page");
            return false;
        }
        
        prev_cache->file_position = start_pos;
        prev_cache->page_number = g_display_ctx.current_page - 1;
        render_page_to_buffer(prev_cache, prev_cache->content, false);
    } else {
        ESP_LOGI(TAG, "Using preloaded previous page (fast switch)");
    }
    
    uint8_t* temp_buffer = g_display_ctx.page_buffers[BUFFER_NEXT].buffer;
    char temp_content[sizeof(g_display_ctx.page_buffers[0].content)];
    strcpy(temp_content, g_display_ctx.page_buffers[BUFFER_NEXT].content);
    
    // Backward scrolling cache
    g_display_ctx.page_buffers[BUFFER_NEXT] = g_display_ctx.page_buffers[BUFFER_CURRENT];
    g_display_ctx.page_buffers[BUFFER_CURRENT] = g_display_ctx.page_buffers[BUFFER_PREVIOUS];
    
    // Previous page cache
    g_display_ctx.page_buffers[BUFFER_PREVIOUS].buffer = temp_buffer;
    strcpy(g_display_ctx.page_buffers[BUFFER_PREVIOUS].content, temp_content);
    g_display_ctx.page_buffers[BUFFER_PREVIOUS].is_valid = false;
    g_display_ctx.page_buffers[BUFFER_PREVIOUS].is_rendering = false;
    
    // Update the context
    g_display_ctx.current_page = g_display_ctx.page_buffers[BUFFER_CURRENT].page_number;
    g_display_ctx.current_position = g_display_ctx.page_buffers[BUFFER_CURRENT].file_position;
    
    // Only add UI elements
    render_current_page_ui(&g_display_ctx.page_buffers[BUFFER_CURRENT]);
    
    // Display immediately
    fiction_display_refresh(g_display_ctx.page_buffers[BUFFER_CURRENT].buffer, false);
    
    // The previous page was preloaded in the background
    xTaskCreate(preload_previous_page_task, "preload_prev", 10*1024, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "Fast page turn to: %d", g_display_ctx.current_page);
    return true;
}

// UI rendering
void render_current_page_ui(page_cache_t* cache) {
    if (!cache || !cache->buffer) return;
    
    Paint_SelectImage(cache->buffer);
    Paint_DrawRectangle(0, 0, SCREEN_WIDTH, 70, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);

    xSemaphoreTake(rtc_mutex, portMAX_DELAY);
    Time_data rtc_time = PCF85063_GetTime();
    xSemaphoreGive(rtc_mutex);
    display_fiction_time(rtc_time);
    
    // Clear and draw the footer
    Paint_DrawRectangle(0, SCREEN_HEIGHT - 42, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    if (s_fiction_ai_read.active) {
        Paint_DrawString_CN(20, SCREEN_HEIGHT - 42, "AI朗读中 功双击停止 ↑↓/Boot返回会中断", &Font12_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(20, SCREEN_HEIGHT - 24, "功单书签 功按住笔记 ↑↑目录/字", &Font12_UTF8, WHITE, BLACK);
    } else {
        Paint_DrawString_CN(20, SCREEN_HEIGHT - 42, "↑↓翻页 ↑↑目录/字 ↓↓字体 功单书签 功双朗读", &Font12_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(20, SCREEN_HEIGHT - 24, "功按住笔记 Boot双击返回", &Font12_UTF8, WHITE, BLACK);
    }
    char page_no[32];
    snprintf(page_no, sizeof(page_no), "页 %d", cache->page_number + 1);
    Paint_DrawString_CN(SCREEN_WIDTH - 90, SCREEN_HEIGHT - 24, page_no, &Font12_UTF8, WHITE, BLACK);
}

bool fiction_ai_read_is_active(void)
{
    return s_fiction_ai_read.active;
}

void fiction_ai_read_stop(bool close_audio_channel)
{
    if (!s_fiction_ai_read.active && !s_fiction_ai_read.waiting_playback) {
        return;
    }

    s_fiction_ai_read.active = false;
    s_fiction_ai_read.waiting_playback = false;
    s_fiction_ai_read.prefetch_inflight = false;
    s_fiction_ai_read.pending_prefetch_requests = 0;
    s_fiction_ai_read.text_offset = 0;
    s_fiction_ai_read.chunk_index = 0;
    s_fiction_ai_read.current_chunk_started_tick = 0;
    s_fiction_ai_read.current_chunk_estimated_ms = 0;
    s_fiction_ai_read.prefetched_chunk_estimated_ms = 0;
    s_fiction_ai_read.page_text[0] = '\0';
    Application::GetInstance().SetReadingAiPrefetchPending(false);
    Application::GetInstance().DisableReadingAiMode(close_audio_channel);
    fiction_ai_read_render_status();
}

static bool fiction_ai_read_send_next_chunk()
{
    static constexpr size_t kAiReadPreferredBytes = 300;
    static constexpr size_t kAiReadMaxBytes = 460;
    static constexpr size_t kAiReadCrossPageMaxBytes = 720;
    static constexpr int kAiReadMaxBorrowPages = 1;

    if (!s_fiction_ai_read.active || !g_fiction_ctx.is_open) {
        ESP_LOGW(TAG, "AI read send skipped: active=%d open=%d",
                 (int)s_fiction_ai_read.active, (int)g_fiction_ctx.is_open);
        return false;
    }

    if (s_fiction_ai_read.page_number != g_display_ctx.current_page ||
        s_fiction_ai_read.section_number != g_display_ctx.current_section ||
        s_fiction_ai_read.page_text[0] == '\0') {
        fiction_ai_read_refresh_page_snapshot();
    }

    fiction_ai_read_skip_leading_whitespace();

    int chunk_start_page = g_display_ctx.current_page + 1;
    int chunk_end_page = chunk_start_page;
    int borrowed_pages = 0;
    std::string chunk;

    while (chunk.size() < kAiReadPreferredBytes && chunk.size() < kAiReadCrossPageMaxBytes) {
        size_t remaining_budget = kAiReadCrossPageMaxBytes - chunk.size();
        size_t sentence_budget = remaining_budget < (kAiReadMaxBytes - chunk.size())
            ? remaining_budget
            : (kAiReadMaxBytes - chunk.size());
        if (sentence_budget == 0) {
            break;
        }

        std::string sentence;
        int sentence_borrowed_pages = 0;
        int sentence_end_page = chunk_end_page;
        if (!fiction_ai_read_extract_next_sentence_unit(
                sentence,
                sentence_budget,
                kAiReadMaxBorrowPages - borrowed_pages,
                &sentence_borrowed_pages,
                &sentence_end_page)) {
            break;
        }

        fiction_ai_read_append_slice(chunk, sentence.c_str(), 0, sentence.size());
        borrowed_pages += sentence_borrowed_pages;
        chunk_end_page = sentence_end_page;

        if (chunk.size() >= kAiReadPreferredBytes || borrowed_pages >= kAiReadMaxBorrowPages) {
            break;
        }
    }

    fiction_trim_trailing_whitespace(chunk);
    if (chunk.empty()) {
        ESP_LOGW(TAG, "AI read prepared empty chunk");
        fiction_ai_read_stop(true);
        return false;
    }

    char book_title[128] = {0};
    fiction_get_current_book_title(book_title, sizeof(book_title));

    char metadata_json[512] = {0};
    snprintf(metadata_json,
             sizeof(metadata_json),
             "{\"book\":\"%s\",\"page\":%d,\"page_end\":%d,\"section\":%d,\"chunk_index\":%u}",
             book_title,
             chunk_start_page,
             chunk_end_page,
             g_display_ctx.current_section + 1,
             (unsigned)(++s_fiction_ai_read.chunk_index));

    Application::GetInstance().EnableReadingAiMode();
    Application::GetInstance().SendTtsText(chunk, metadata_json);
    fiction_ai_read_record_sent_chunk(chunk, false);
    ESP_LOGI(TAG, "AI read sent chunk: page=%d-%d chunk=%u bytes=%u borrowed_pages=%d",
             chunk_start_page,
             chunk_end_page,
             (unsigned)s_fiction_ai_read.chunk_index,
             (unsigned)chunk.size(),
             borrowed_pages);
    fiction_ai_read_render_status();
    return true;
}

static bool fiction_ai_read_try_prefetch_chunk(bool allow_cross_page)
{
    if (!s_fiction_ai_read.active ||
        !s_fiction_ai_read.waiting_playback ||
        s_fiction_ai_read.prefetch_inflight ||
        s_fiction_ai_read.page_text[0] == '\0') {
        ESP_LOGI(TAG,
                 "AI read prefetch not ready: active=%d waiting=%d inflight=%d text_ready=%d",
                 (int)s_fiction_ai_read.active,
                 (int)s_fiction_ai_read.waiting_playback,
                 (int)s_fiction_ai_read.prefetch_inflight,
                 s_fiction_ai_read.page_text[0] != '\0');
        return false;
    }

    size_t text_len = strlen(s_fiction_ai_read.page_text);
    fiction_ai_read_skip_leading_whitespace();
    if (!allow_cross_page && s_fiction_ai_read.text_offset >= text_len) {
        ESP_LOGI(TAG, "AI read prefetch skipped: page tail reached");
        return false;
    }

    static constexpr size_t kAiReadPreferredBytes = 300;
    static constexpr size_t kAiReadMaxBytes = 460;
    static constexpr size_t kAiReadCrossPageMaxBytes = 720;
    static constexpr int kAiReadMaxBorrowPages = 1;

    int chunk_start_page = g_display_ctx.current_page + 1;
    int chunk_end_page = chunk_start_page;
    int borrowed_pages = 0;
    std::string chunk;

    while (chunk.size() < kAiReadPreferredBytes && chunk.size() < kAiReadCrossPageMaxBytes) {
        size_t remaining_budget = kAiReadCrossPageMaxBytes - chunk.size();
        size_t sentence_budget = remaining_budget < (kAiReadMaxBytes - chunk.size())
            ? remaining_budget
            : (kAiReadMaxBytes - chunk.size());
        if (sentence_budget == 0) {
            break;
        }

        std::string sentence;
        int sentence_borrowed_pages = 0;
        int sentence_end_page = chunk_end_page;
        if (!fiction_ai_read_extract_next_sentence_unit(
                sentence,
                sentence_budget,
                kAiReadMaxBorrowPages - borrowed_pages,
                &sentence_borrowed_pages,
                &sentence_end_page)) {
            break;
        }

        fiction_ai_read_append_slice(chunk, sentence.c_str(), 0, sentence.size());
        borrowed_pages += sentence_borrowed_pages;
        chunk_end_page = sentence_end_page;

        if (chunk.size() >= kAiReadPreferredBytes || borrowed_pages >= kAiReadMaxBorrowPages) {
            break;
        }
    }

    fiction_trim_trailing_whitespace(chunk);
    if (chunk.empty()) {
        return false;
    }

    if (!allow_cross_page && (borrowed_pages > 0 || chunk_end_page != chunk_start_page)) {
        ESP_LOGI(TAG,
                 "AI read prefetch skipped: cross-page chunk start=%d end=%d borrowed=%d",
                 chunk_start_page,
                 chunk_end_page,
                 borrowed_pages);
        return false;
    }

    char book_title[128] = {0};
    fiction_get_current_book_title(book_title, sizeof(book_title));

    char metadata_json[512] = {0};
    snprintf(metadata_json,
             sizeof(metadata_json),
             "{\"book\":\"%s\",\"page\":%d,\"page_end\":%d,\"section\":%d,\"chunk_index\":%u,\"prefetch\":true}",
             book_title,
             chunk_start_page,
             chunk_end_page,
             g_display_ctx.current_section + 1,
             (unsigned)(s_fiction_ai_read.chunk_index + 1));

    Application::GetInstance().EnableReadingAiMode();
    Application::GetInstance().SendTtsText(chunk, metadata_json);
    fiction_ai_read_record_sent_chunk(chunk, true);
    ESP_LOGI(TAG, "AI read prefetched chunk: page=%d-%d next_chunk=%u bytes=%u borrowed_pages=%d",
             chunk_start_page,
             chunk_end_page,
             (unsigned)(s_fiction_ai_read.chunk_index + 1),
             (unsigned)chunk.size(),
             borrowed_pages);
    return true;
}

bool fiction_ai_read_toggle(void)
{
    if (s_fiction_ai_read.active) {
        ESP_LOGI(TAG, "AI read toggled off by user");
        fiction_ai_read_stop(false);
        return false;
    }

    fiction_ai_read_refresh_page_snapshot();
    if (s_fiction_ai_read.page_text[0] == '\0') {
        ESP_LOGW(TAG, "AI read toggle failed: current page text is empty");
        return false;
    }

    s_fiction_ai_read.active = true;
    s_fiction_ai_read.waiting_playback = false;
    if (!fiction_ai_read_send_next_chunk()) {
        s_fiction_ai_read.active = false;
        ESP_LOGW(TAG, "AI read toggle failed: unable to send first chunk");
        return false;
    }
    ESP_LOGI(TAG, "AI read toggled on");
    return true;
}

bool fiction_ai_read_pump(void)
{
    if (!s_fiction_ai_read.active || !s_fiction_ai_read.waiting_playback) {
        return false;
    }

    int server_requested_chunks = Application::GetInstance().ConsumeReadingAiNextChunkRequests();
    if (server_requested_chunks > 0) {
        s_fiction_ai_read.pending_prefetch_requests += server_requested_chunks;
        ESP_LOGI(TAG,
                 "AI read queued prefetch requests: pending=%d newly_received=%d",
                 s_fiction_ai_read.pending_prefetch_requests,
                 server_requested_chunks);
    }

    while (s_fiction_ai_read.pending_prefetch_requests > 0 &&
           !s_fiction_ai_read.prefetch_inflight) {
        ESP_LOGI(TAG, "AI read prefetch requested by server");
        if (!fiction_ai_read_try_prefetch_chunk(true)) {
            break;
        }
        --s_fiction_ai_read.pending_prefetch_requests;
    }

    if (!Application::GetInstance().ConsumeReadingAiPlaybackFinished()) {
        return false;
    }

    if (s_fiction_ai_read.prefetch_inflight) {
        s_fiction_ai_read.prefetch_inflight = false;
        s_fiction_ai_read.chunk_index++;
        s_fiction_ai_read.current_chunk_started_tick = xTaskGetTickCount();
        s_fiction_ai_read.current_chunk_estimated_ms = s_fiction_ai_read.prefetched_chunk_estimated_ms;
        s_fiction_ai_read.prefetched_chunk_estimated_ms = 0;
        Application::GetInstance().SetReadingAiPrefetchPending(false);
        ESP_LOGI(TAG, "AI read promoted prefetched chunk: chunk=%u",
                 (unsigned)s_fiction_ai_read.chunk_index);
        return true;
    }

    s_fiction_ai_read.waiting_playback = false;
    s_fiction_ai_read.current_chunk_started_tick = 0;
    s_fiction_ai_read.current_chunk_estimated_ms = 0;
    return fiction_ai_read_send_next_chunk();
}

// Background preload task
void preload_next_page_task(void* pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(100));
    preload_next_page();
    vTaskDelete(NULL);
}

void preload_previous_page_task(void* pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(100));
    preload_previous_page();
    vTaskDelete(NULL);
}

// Font menu display
void display_font_menu(font_size_t selected_font) {
    Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_SelectImage(Image_Mono);
    Paint_Clear(WHITE);
    
    Paint_DrawString_CN(20, 20, "字体设置", &Font24_UTF8, BLACK, WHITE);
    Paint_DrawLine(20, 60, SCREEN_WIDTH - 20, 60, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    
    char encoding_info[50];
    snprintf(encoding_info, sizeof(encoding_info), "编码: %s", g_display_ctx.encoding);
    Paint_DrawString_CN(20, 70, encoding_info, &Font16_UTF8, BLACK, WHITE);
    
    int start_y = 120;
    int item_height = 50;
    int y_pos = start_y;
    
    for (int i = 0; i < FONT_SIZE_MAX; i++) {
        cFONT* demo_font = get_font_by_encoding((font_size_t)i, "UTF");
        Paint_DrawString_CN(60, y_pos, font_names_1[i], demo_font, BLACK, WHITE);
        
        // Sample text
        Paint_DrawString_CN(200, y_pos, "示例Abc123", demo_font, WHITE, BLACK);
        
        if (i == (int)selected_font) {
            Paint_DrawRectangle(15, y_pos - 5, SCREEN_WIDTH - 15, y_pos + demo_font->Height + 5, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
            Paint_DrawString_CN(25, y_pos, ">", demo_font, BLACK, WHITE);
        }

        y_pos = y_pos + demo_font->Height + 10;
        if (y_pos > SCREEN_HEIGHT - 80) break;
    }
    
    Paint_DrawString_CN(20, SCREEN_HEIGHT - 40, "↑↓选择,单击确认:应用,双击确认:返回", &Font12_UTF8, WHITE, BLACK);
    
    fiction_display_refresh(Image_Mono, false);
}






// Bookmark e-paper display
// Initialize the bookmark display
void init_bookmark_display_buffers(void) {
    size_t buffer_size = (SCREEN_WIDTH * SCREEN_HEIGHT) / 8;
    
    // Allocate bookmark display cache
    if (!bookmark_display_buffer) {
        bookmark_display_buffer = (uint8_t*)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
        if (!bookmark_display_buffer) {
            ESP_LOGE(TAG, "Failed to allocate bookmark display buffer");
            return;
        }
    }

    // Allocate the bookmark preview cache
    if (!bookmark_preview_buffer) {
        bookmark_preview_buffer = (uint8_t*)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
        if (!bookmark_preview_buffer) {
            ESP_LOGE(TAG, "Failed to allocate bookmark display buffer");
            return;
        }
    }    
    ESP_LOGI(TAG, "Bookmark display buffers initialized: %d bytes each", (int)buffer_size);
}

// Release the bookmark display cache
void free_bookmark_display_buffers(void) {
    if (bookmark_display_buffer) {
        heap_caps_free(bookmark_display_buffer);
        bookmark_display_buffer = NULL;
    }

    if (bookmark_preview_buffer) {
        heap_caps_free(bookmark_preview_buffer);
        bookmark_preview_buffer = NULL;
    }
    ESP_LOGI(TAG, "Bookmark display buffers freed");
}

// Restore the bookmark display page
void restore_bookmark_page(void) {
    // Display the content of the backup directly
    fiction_display_refresh(bookmark_display_buffer, false);
    ESP_LOGI(TAG, "Current page restored from backup");
}

// Restore the current page content
void restore_current_page(void) {
    // Display the content of the backup directly
    fiction_display_refresh(g_display_ctx.page_buffers[BUFFER_CURRENT].buffer, false);
    ESP_LOGI(TAG, "Current page restored from backup");
}

// Extract the string for preview display
void safe_truncate_for_preview(const char* src, char* dst, int max_chars, const char* encoding) {
    int src_pos = 0;
    int dst_pos = 0;
    int char_count = 0;
    int src_len = strlen(src);
    
    const int max_dst_size = 79 - 3;
    
    while (src_pos < src_len && char_count < max_chars && dst_pos < max_dst_size) {
        int char_bytes = get_char_byte_length(src, src_pos, encoding);
        if (dst_pos + char_bytes > max_dst_size) {
            ESP_LOGI(TAG, "Preview truncated at char %d due to buffer limit", char_count);
            break;
        }
        if (src_pos + char_bytes > src_len) {
            ESP_LOGW(TAG, "Source string boundary exceeded, stopping at char %d", char_count);
            break;
        }
        for (int i = 0; i < char_bytes; i++) {
            dst[dst_pos++] = src[src_pos++];
        }
        char_count++;
    }
    
    if (src_pos < src_len && dst_pos <= max_dst_size) {
        strcpy(dst + dst_pos, "...");
        dst_pos += 3;
    }
    
    dst[dst_pos] = '\0';
    
    ESP_LOGI(TAG, "Preview: %d chars, %d bytes from %d source bytes (encoding: %s)", 
             char_count, dst_pos, src_len, encoding);
}

// Preview processing of the content in the bookmark list display
void display_bookmark_list_on_screen(fiction_context_t* ctx, int selected_index) {
    if (!bookmark_display_buffer) {
        ESP_LOGE(TAG, "Bookmark display buffer not initialized");
        return;
    }
    
    // Set the bookmark display cache
    Paint_NewImage(bookmark_display_buffer, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_SelectImage(bookmark_display_buffer);
    Paint_Clear(WHITE);
    
    // title bar
    Paint_DrawString_CN(20, 20, "书签管理", &Font24_UTF8, WHITE, BLACK);
    Paint_DrawLine(20, 60, SCREEN_WIDTH - 20, 60, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    
    // Display bookmark statistics information
    char stats_info[100];
    snprintf(stats_info, sizeof(stats_info), "共有 %d 个书签", ctx->bookmark_count);
    Paint_DrawString_CN(20, 70, stats_info, &Font16_UTF8, WHITE, BLACK);
    
    if (ctx->bookmark_count == 0) {
        // A prompt when there are no bookmarks
        int center_y = SCREEN_HEIGHT / 2;
        Paint_DrawString_CN(60, center_y - 60, "还没有添加任何书签", &Font18_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(60, center_y - 20, "在阅读时按功能键进入书签列表", &Font16_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(60, center_y + 20, "书签可以帮助您快速回到感兴趣的位置", &Font16_UTF8, WHITE, BLACK);
    } else {
        // Display the bookmark list
        int list_start_y = 100; 
        int item_height = 100;
        int available_height = SCREEN_HEIGHT - list_start_y - 80;
        int visible_items = available_height / item_height;
        
        ESP_LOGI(TAG, "Bookmark layout: start_y=%d, item_height=%d, visible_items=%d, total=%d", list_start_y, item_height, visible_items, ctx->bookmark_count);
        
        // Calculate the rolling offset
        int scroll_offset = 0;
        if (selected_index >= visible_items) {
            scroll_offset = selected_index - visible_items + 1;
        }
        
        // Display bookmark items
        for (int i = 0; i < ctx->bookmark_count && i < visible_items; i++) {
            int actual_index = i + scroll_offset;
            if (actual_index >= ctx->bookmark_count) break;
            
            int item_y = list_start_y + i * item_height;
            
            // Selected item background
            if (actual_index == selected_index) {
                Paint_DrawRectangle(15, item_y+2, SCREEN_WIDTH - 15, item_y + item_height + 2, BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
                Paint_DrawString_CN(25, item_y + 5, ">", &Font18_UTF8, WHITE, BLACK);
            }
            
            // Bookmark serial number and basic information
            char bookmark_title[100];
            snprintf(bookmark_title, sizeof(bookmark_title), "书签 %d: 第 %d 页", actual_index + 1, ctx->bookmarks[actual_index].page + 1);
            Paint_DrawString_CN(55, item_y + 5, bookmark_title, &Font18_UTF8, WHITE, BLACK);
            
            // Progress information
            char progress_info[50];
            snprintf(progress_info, sizeof(progress_info), "阅读进度: %.1f%%", 
                     ctx->bookmarks[actual_index].progress);
            Paint_DrawString_CN(55, item_y + 8 + Font18_UTF8.Height, progress_info, &Font16_UTF8, WHITE, BLACK);
            
            // Content Preview
            char preview[80];
            int max_preview_chars;
            if (strstr(g_display_ctx.encoding, "GBK") || strstr(g_display_ctx.encoding, "GB2312")) {
                max_preview_chars = 15;
            } else {
                max_preview_chars = 12;
            }
            
            safe_truncate_for_preview(ctx->bookmarks[actual_index].content_preview, preview, max_preview_chars, g_display_ctx.encoding);
            if (strstr(g_display_ctx.encoding, "GBK") || strstr(g_display_ctx.encoding, "GB2312")) {
                Paint_DrawString_CN(55, item_y + 11 + Font18_GBK.Height + Font16_GBK.Height, preview, &Font12_GBK, WHITE, BLACK);
            } else {
                Paint_DrawString_CN(55, item_y + 11 + Font18_UTF8.Height + Font16_UTF8.Height, preview, &Font12_UTF8, WHITE, BLACK);
            }
            
            ESP_LOGI(TAG, "Bookmark %d: y=%d, preview=[%s]", actual_index + 1, item_y, preview);
            if (i < visible_items - 1 && actual_index < ctx->bookmark_count - 1) {
                Paint_DrawLine(30, item_y + item_height - 5, SCREEN_WIDTH - 30, item_y + item_height - 5, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
            }
        }
        
        if (ctx->bookmark_count > visible_items) {
            char scroll_info[30];
            snprintf(scroll_info, sizeof(scroll_info), "%d/%d", selected_index + 1, ctx->bookmark_count);
            Paint_DrawString_CN(SCREEN_WIDTH - 80, SCREEN_HEIGHT - 60, scroll_info, &Font16_UTF8, WHITE, BLACK);
        }
    }
    
    // OI (operating instructions)
    Paint_DrawLine(20, SCREEN_HEIGHT - Font16_UTF8.Height * 2 - 25, SCREEN_WIDTH - 20, SCREEN_HEIGHT - Font16_UTF8.Height * 2 - 25, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    
    if (ctx->bookmark_count > 0) {
        Paint_DrawString_CN(20, SCREEN_HEIGHT - Font16_UTF8.Height * 2 - 20, "↑↓选择书签,确认:操作菜单", &Font16_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(20, SCREEN_HEIGHT - Font16_UTF8.Height * 1 - 15, "双击确认:添加书签,双击Boot:返回", &Font12_UTF8, WHITE, BLACK);
    } else {
        Paint_DrawString_CN(20, SCREEN_HEIGHT - Font16_UTF8.Height * 2 - 20, "双击确认:添加书签", &Font16_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(20, SCREEN_HEIGHT - Font16_UTF8.Height * 1 - 15, "双击Boot:返回阅读", &Font16_UTF8, WHITE, BLACK);
    }

    // Time and battery power
    display_time_bet_fiction(bookmark_display_buffer);
    
    // Display on the screen
    fiction_display_refresh(bookmark_display_buffer, false);
    ESP_LOGI(TAG, "Full-screen bookmark list displayed: %d bookmarks, selected: %d", ctx->bookmark_count, selected_index);
}

// Select up and down from the bookmark list
void display_bookmark_list_on_screen_Down(int selected_index, int Refresh_mode)
{
    if (!bookmark_display_buffer) {
        ESP_LOGE(TAG, "Bookmark display buffer not initialized");
        return;
    }
    
    Paint_SelectImage(bookmark_display_buffer);
    
    int list_start_y = 100; 
    int item_height = 100; 
    
    int selection_old = selected_index - 1;
    if (selection_old < 0) {
        selection_old = g_fiction_ctx.bookmark_count - 1;
    }

    int y_pos_old = list_start_y + selection_old * item_height;
    int y_pos_new = list_start_y + selected_index * item_height;

    // Clear the old selected mark
    Paint_DrawRectangle(15, y_pos_old + 2, SCREEN_WIDTH - 15, y_pos_old + item_height + 2, WHITE, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
    Paint_DrawString_CN(25, y_pos_old + 5, "  ", &Font18_UTF8, WHITE, BLACK);

    // Draw a new selected mark
    Paint_DrawRectangle(15, y_pos_new + 2, SCREEN_WIDTH - 15, y_pos_new + item_height + 2, BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
    Paint_DrawString_CN(25, y_pos_new + 5, ">", &Font18_UTF8, WHITE, BLACK);

    if (Refresh_mode == Global_refresh) {
        fiction_display_refresh(bookmark_display_buffer, true);
    } else if(Refresh_mode == Partial_refresh){
        fiction_display_refresh(bookmark_display_buffer, false);
    }
    
    ESP_LOGI(TAG, "Bookmark selection moved down: %d -> %d", selection_old, selected_index);
}

void display_bookmark_list_on_screen_Up(int selected_index, int Refresh_mode)
{
    if (!bookmark_display_buffer) {
        ESP_LOGE(TAG, "Bookmark display buffer not initialized");
        return;
    }
    Paint_SelectImage(bookmark_display_buffer);
    
    int list_start_y = 100; 
    int item_height = 100;
    
    int selection_old = selected_index + 1;
    if (selection_old >= g_fiction_ctx.bookmark_count) {
        selection_old = 0;
    }

    int y_pos_old = list_start_y + selection_old * item_height;
    int y_pos_new = list_start_y + selected_index * item_height;

    // Clear the old selected mark
    Paint_DrawRectangle(15, y_pos_old + 2, SCREEN_WIDTH - 15, y_pos_old + item_height + 2, WHITE, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
    Paint_DrawString_CN(25, y_pos_old + 5, "  ", &Font18_UTF8, WHITE, BLACK);

    // Draw a new selected mark
    Paint_DrawRectangle(15, y_pos_new + 2, SCREEN_WIDTH - 15, y_pos_new + item_height + 2, BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
    Paint_DrawString_CN(25, y_pos_new + 5, ">", &Font18_UTF8, WHITE, BLACK);

    if (Refresh_mode == Global_refresh) {
        fiction_display_refresh(bookmark_display_buffer, true);
    } else if(Refresh_mode == Partial_refresh){
        fiction_display_refresh(bookmark_display_buffer, false);
    }
    
    ESP_LOGI(TAG, "Bookmark selection moved up: %d -> %d", selection_old, selected_index);
}


// The bookmark operation menu is displayed
void display_bookmark_action_menu_on_screen(fiction_context_t* ctx, int bookmark_index, int option_selection) {
    if (!bookmark_preview_buffer) {
        ESP_LOGE(TAG, "Bookmark display buffer not initialized");
        return;
    }
    
    Paint_NewImage(bookmark_preview_buffer, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_SelectImage(bookmark_preview_buffer);
    Paint_Clear(WHITE);
    
    Paint_DrawString_CN(10, 20, "书签操作", &Font24_UTF8, WHITE, BLACK);
    Paint_DrawLine(10, 20 + Font24_UTF8.Height+5, SCREEN_WIDTH - 10, 20 + Font24_UTF8.Height+5, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    
    int info_start_y = 80;
    
    char bookmark_title[100];
    snprintf(bookmark_title, sizeof(bookmark_title), "书签 %d", bookmark_index + 1);
    Paint_DrawString_CN(10, info_start_y, bookmark_title, &Font18_UTF8, WHITE, BLACK);
    
    char page_info[50];
    snprintf(page_info, sizeof(page_info), "位置: 第 %d 页", ctx->bookmarks[bookmark_index].page + 1);
    Paint_DrawString_CN(10, info_start_y + Font18_UTF8.Height + 5, page_info, &Font18_UTF8, WHITE, BLACK);
    
    char progress_info[50];
    snprintf(progress_info, sizeof(progress_info), "进度: %.1f%%", ctx->bookmarks[bookmark_index].progress);
    Paint_DrawString_CN(10, info_start_y + Font18_UTF8.Height * 2 + 10, progress_info, &Font18_UTF8, WHITE, BLACK);
    
    // Content Preview
    Paint_DrawString_CN(10, info_start_y + Font18_UTF8.Height * 3 + 15, "内容预览:", &Font16_UTF8, WHITE, BLACK);

    char preview[128];
    strncpy(preview, ctx->bookmarks[bookmark_index].content_preview, sizeof(preview) - 1);
    preview[sizeof(preview) - 1] = '\0';
    
    int chars_per_line;
    if (strstr(g_display_ctx.encoding, "GBK") || strstr(g_display_ctx.encoding, "GB2312")) {
        chars_per_line = (SCREEN_WIDTH-20) / Font16_GBK.Width_CH;  
    } else {
        chars_per_line = (SCREEN_WIDTH-20) / Font16_UTF8.Width_CH;
    }
    
    int preview_y = info_start_y + Font18_UTF8.Height * 3 + 20 + Font16_UTF8.Height;
    int preview_len = strlen(preview);
    int pos = 0;
    int line_count = 0;
    
    // Display preview content
    while (pos < preview_len && line_count < 3 && preview_y < SCREEN_HEIGHT - 250) {
        char line[60];
        int chars_copied = copy_chars_by_count(preview + pos, line, chars_per_line, g_display_ctx.encoding);
        if (chars_copied > 0) {
            if (strstr(g_display_ctx.encoding, "GBK") || strstr(g_display_ctx.encoding, "GB2312")) {
                Paint_DrawString_CN(10, preview_y, line, &Font16_GBK, WHITE, BLACK);
            } else {
                Paint_DrawString_CN(10, preview_y, line, &Font16_UTF8, WHITE, BLACK);
            }
            preview_y += Font16_UTF8.Height + 5;
            pos += chars_copied;
            line_count++;
        } else {
            break;
        }
    }
    
    // Operation options
    int options_start_y = SCREEN_HEIGHT - 240;
    Paint_DrawLine(20, options_start_y - 20, SCREEN_WIDTH - 20, options_start_y - 20, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    
    Paint_DrawString_CN(20, options_start_y - 15, "选择操作:", &Font18_UTF8, WHITE, BLACK);
    
    const char* options[] = {"跳转到此书签", "删除此书签", "返回书签列表"};
    int option_height = Font18_UTF8.Height+10; 
    
    for (int i = 0; i < 3; i++) {
        int option_y = options_start_y + Font18_UTF8.Height + i * option_height-10;

        char option_text[50];
        snprintf(option_text, sizeof(option_text), "%d. %s", i + 1, options[i]);
        Paint_DrawString_CN(55, option_y+5, option_text, &Font18_UTF8, WHITE, BLACK);
    }
    // Selected item mark
    Paint_DrawRectangle(15, (options_start_y + Font18_UTF8.Height + option_selection * option_height-10), SCREEN_WIDTH - 15, (options_start_y + Font18_UTF8.Height + option_selection * option_height-10) + option_height, BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
    Paint_DrawString_CN(25, (options_start_y + Font18_UTF8.Height + option_selection * option_height-10)+5, ">", &Font18_UTF8, BLACK, WHITE);
    
    Paint_DrawLine(10, SCREEN_HEIGHT - 70, SCREEN_WIDTH - 10, SCREEN_HEIGHT - 70, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawString_CN(10, SCREEN_HEIGHT - 65, "↑↓选择,确认:执行", &Font16_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(10, SCREEN_HEIGHT - 32, "双击确认/Boot:返回书签列表", &Font16_UTF8, WHITE, BLACK);

    display_time_bet_fiction(bookmark_preview_buffer);

    fiction_display_refresh(bookmark_preview_buffer, false);
    ESP_LOGI(TAG, "Full-screen bookmark action menu displayed: bookmark %d, option %d", bookmark_index, option_selection);
}


// Select functions up and down in the bookmark operation menu
void display_bookmark_action_menu_on_screen_Down(int option_selection, int Refresh_mode)
{
    if (!bookmark_preview_buffer) {
        ESP_LOGE(TAG, "Bookmark preview buffer not initialized");
        return;
    }
    
    Paint_SelectImage(bookmark_preview_buffer);
    
    int options_start_y = SCREEN_HEIGHT - 240;
    int option_height = Font18_UTF8.Height + 10;
    
    int selection_old = option_selection - 1;
    if (selection_old < 0) {
        selection_old = 2;
    }
    
    int y_pos_old = options_start_y + Font18_UTF8.Height + selection_old * option_height - 10;
    int y_pos_new = options_start_y + Font18_UTF8.Height + option_selection * option_height - 10;

    Paint_DrawRectangle(15, y_pos_old, SCREEN_WIDTH - 15, y_pos_old + option_height, WHITE, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
    Paint_DrawString_CN(25, y_pos_old + 5, "  ", &Font18_UTF8, WHITE, BLACK);

    Paint_DrawRectangle(15, y_pos_new, SCREEN_WIDTH - 15, y_pos_new + option_height, BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
    Paint_DrawString_CN(25, y_pos_new + 5, ">", &Font18_UTF8, BLACK, WHITE);

    if (Refresh_mode == Global_refresh) {
        fiction_display_refresh(bookmark_preview_buffer, true);
    } else if(Refresh_mode == Partial_refresh){
        fiction_display_refresh(bookmark_preview_buffer, false);
    }
    
    ESP_LOGI(TAG, "Bookmark action selection moved down: %d -> %d", selection_old, option_selection);
}

void display_bookmark_action_menu_on_screen_Up(int option_selection, int Refresh_mode)
{
    if (!bookmark_preview_buffer) {
        ESP_LOGE(TAG, "Bookmark preview buffer not initialized");
        return;
    }
    
    Paint_SelectImage(bookmark_preview_buffer);
    
    int options_start_y = SCREEN_HEIGHT - 240;
    int option_height = Font18_UTF8.Height + 10;
    
    int selection_old = option_selection + 1;
    if (selection_old > 2) {
        selection_old = 0;
    }
    
    int y_pos_old = options_start_y + Font18_UTF8.Height + selection_old * option_height - 10;
    int y_pos_new = options_start_y + Font18_UTF8.Height + option_selection * option_height - 10;

    Paint_DrawRectangle(15, y_pos_old, SCREEN_WIDTH - 15, y_pos_old + option_height, WHITE, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
    Paint_DrawString_CN(25, y_pos_old + 5, "  ", &Font18_UTF8, WHITE, BLACK);

    Paint_DrawRectangle(15, y_pos_new, SCREEN_WIDTH - 15, y_pos_new + option_height, BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
    Paint_DrawString_CN(25, y_pos_new + 5, ">", &Font18_UTF8, BLACK, WHITE);

    if (Refresh_mode == Global_refresh) {
        fiction_display_refresh(bookmark_preview_buffer, true);
    } else if(Refresh_mode == Partial_refresh){
        fiction_display_refresh(bookmark_preview_buffer, false);
    }
    
    ESP_LOGI(TAG, "Bookmark action selection moved up: %d -> %d", selection_old, option_selection);
}

// Display the loading interface
void display_loading_fiction(const char* message, int Refresh_mode)
{
    // Check the input parameters
    if (!message) {
        ESP_LOGE(TAG, "Loading message is NULL");
        return;
    }
    
    if (!page_backup_buffer) {
        ESP_LOGE(TAG, "page_backup_buffer buffer is NULL");
        return;
    }

    Paint_NewImage(page_backup_buffer, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_SetScale(2);
    Paint_SelectImage(page_backup_buffer);
    Paint_Clear(WHITE);
    
    cFONT* font = &Font24_UTF8;
    
    int message_len = strlen(message);
    if (message_len > 100) {
        ESP_LOGW(TAG, "Loading message too long: %d chars", message_len);
        message_len = 100;
    }

    int estimated_width = message_len * (font->Width_CH / 2);
    int text_x = (SCREEN_WIDTH - estimated_width) / 2;
    int text_y = (SCREEN_HEIGHT - font->Height) / 2;
    
    if (text_x < 10) text_x = 10;
    if (text_x > SCREEN_WIDTH - 50) text_x = SCREEN_WIDTH - 50;
    if (text_y < 10) text_y = 10;
    if (text_y > SCREEN_HEIGHT - font->Height - 10) text_y = SCREEN_HEIGHT - font->Height - 10;

    char safe_message[101];
    strncpy(safe_message, message, 100);
    safe_message[100] = '\0';
    
    Paint_DrawString_CN(text_x, text_y, safe_message, font, BLACK, WHITE);
    
    if (Refresh_mode == Global_refresh) {
        fiction_display_refresh(page_backup_buffer, true);
    } else if(Refresh_mode == Partial_refresh){
        fiction_display_refresh(page_backup_buffer, false);
    }
}



static void Forced_refresh_fiction(uint8_t *EDP_buffer)
{
    fiction_display_refresh(EDP_buffer, true);
}
static void Refresh_page_fiction(uint8_t *EDP_buffer)
{
    fiction_display_refresh(EDP_buffer, false);
}
static void display_fiction_time_last(Time_data rtc_time)
{
    char Time_str[20]={0};
    int hours = rtc_time.hours;
    int minutes = rtc_time.minutes-1;

    if(minutes < 0){
        minutes = 59;
        hours = rtc_time.hours - 1;
        if(hours < 0 )
            hours = 23;
    }

    snprintf(Time_str, sizeof(Time_str), "%02d:%02d", hours, minutes);
    Paint_DrawString_EN(20, 11, Time_str, &Font16, WHITE, BLACK);

    // EPD_Display_Partial(Image_Fiction,0,0,EPD_WIDTH,EPD_HEIGHT);
}
static void display_fiction_time(Time_data rtc_time)
{
    char Time_str[16]={0};
    int BAT_Power;
    char BAT_Power_str[16]={0};

    snprintf(Time_str, sizeof(Time_str), "%02d:%02d", rtc_time.hours, rtc_time.minutes);
    Paint_DrawString_EN(20, 11, Time_str, &Font16, WHITE, BLACK);
#if defined(CONFIG_IMG_SOURCE_EMBEDDED)
    DrawWifiStatusIcon(326, 8);
    Paint_ReadBmp(gImage_BAT, 370, 17, 32, 16);
#elif defined(CONFIG_IMG_SOURCE_TFCARD)
    DrawWifiStatusIcon(326, 8);
    GUI_ReadBmp(BMP_BAT_PATH, 370, 17);
#endif
    BAT_Power = get_battery_power();
    ESP_LOGI("BAT_Power", "BAT_Power = %d%%",BAT_Power);
    snprintf(BAT_Power_str, sizeof(BAT_Power_str), "%d%%", BAT_Power);
    if(BAT_Power == -1) BAT_Power = 20;
    else BAT_Power = BAT_Power * 20 / 100;
    Paint_DrawString_EN(411, 11, BAT_Power_str, &Font16, WHITE, BLACK);
    Paint_DrawRectangle(375, 22, 395, 30, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawRectangle(375, 22, 375+BAT_Power, 30, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawLine(2, 54, EPD_HEIGHT-2, 54, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
    // EPD_Display_Partial(Image_Fiction,0,0,EPD_WIDTH,EPD_HEIGHT);
}

// calculate display width
static int calculate_display_width(const char* str)
{
    int width = 0;
    const char* p = str;
    
    while (*p) {
        if ((*p & 0x80) == 0) {
            width += 9;
            p++;
        } else {
            width += 18;
            if ((*p & 0xE0) == 0xC0) p += 2;
            else if ((*p & 0xF0) == 0xE0) p += 3;
            else if ((*p & 0xF8) == 0xF0) p += 4;
            else p++;
        }
    }
    return width;
}
// Truncate the file name to fit the display width
static void truncate_filename_for_display(const char* filename, char* display_name, int max_len, int max_width)
{
    int filename_width = calculate_display_width(filename);
    
    if (filename_width <= max_width) {
        strncpy(display_name, filename, max_len - 1);
        display_name[max_len - 1] = '\0';
    } else {
        const char* ext = strrchr(filename, '.');
        int ext_len = ext ? strlen(ext) : 0;
        int available_len = max_len - ext_len - 4; 
        
        strncpy(display_name, filename, available_len);
        display_name[available_len] = '\0';
        strcat(display_name, "...");
        if (ext) strcat(display_name, ext);
    }
}
// Update the selected items (only partially refresh the selected box)
static void update_fiction_selection(int new_selected)
{
    static int last_selected = 0;
    
    if (last_selected == new_selected) return;
    
    int y_start = HEADER_HEIGHT;
    int item_height = Font18_UTF8.Height + 10;
    
    if (last_selected >= 0) {
        int old_y = y_start + last_selected * item_height;
        Paint_DrawRectangle(5, old_y - 5, 475, old_y + Font18_UTF8.Height + 5, WHITE, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
    }
    
    int new_y = y_start + new_selected * item_height;
    Paint_DrawRectangle(5, new_y - 5, 475, new_y + Font18_UTF8.Height + 5, BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
    
    last_selected = new_selected;
    
    // 局部刷新
    Refresh_page_fiction(Image_Fiction);
}

// Truncate the string to the specified display width
static void truncate_string_by_width(const char* source, char* dest, int dest_size, int max_width, cFONT *Font) {
    int current_width = 0;
    int i = 0;
    int dest_pos = 0;
    
    int ellipsis_width = Font->Width_EN * 3;
    int available_width = max_width - ellipsis_width;
    
    while (source[i] != '\0' && dest_pos < dest_size - 4) {
        unsigned char ch = (unsigned char)source[i];
        int char_width = 0;
        int char_bytes = 1;
        
        if (ch < 0x80) {
            char_width = Font->Width_EN;
            char_bytes = 1;
        } else if ((ch & 0xE0) == 0xC0) {
            char_width = Font->Width_CH;
            char_bytes = 2;
        } else if ((ch & 0xF0) == 0xE0) {
            char_width = Font->Width_CH;
            char_bytes = 3;
        } else if ((ch & 0xF8) == 0xF0) {
            char_width = Font->Width_CH;
            char_bytes = 4;
        } else {
            char_width = Font->Width_EN;
            char_bytes = 1;
        }
        if (current_width + char_width > available_width) {
            break;
        }

        for (int j = 0; j < char_bytes && source[i + j] != '\0' && dest_pos < dest_size - 4; j++) {
            dest[dest_pos++] = source[i + j];
        }
        
        current_width += char_width;
        i += char_bytes;
    }
    
    if (source[i] != '\0') {
        dest[dest_pos++] = '.';
        dest[dest_pos++] = '.';
        dest[dest_pos++] = '.';
    }
    
    dest[dest_pos] = '\0';
}
// Calculate the number of files displayed per page in the file list
static int get_fiction_page_size(void)
{
    int header_height = HEADER_HEIGHT;    
    int status_height = Font16.Height * 2; 
    int available_height = EPD_WIDTH - header_height - status_height-10;
    
    int item_height = Font18_UTF8.Height + 10;
    
    int page_size = available_height / item_height;
    
    return page_size;
}

// Determine the file type
static const char* get_file_type(const char* filename)
{
    const char* ext = strrchr(filename, '.');
    if (!ext) return "unknown";
    if (strcasecmp(ext, ".txt") == 0 || strcasecmp(ext, ".md") == 0 || strcasecmp(ext, ".epub") == 0) return "text";
    if (strcasecmp(ext, ".wav") == 0 || strcasecmp(ext, ".mp3") == 0) return "audio";
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".png") == 0 || strcasecmp(ext, ".bmp") == 0) return "image";
    return "unknown";
}

// Display file list
static void display_fiction_file_list(file_entry_t* entries, int num_entries, int current_selection, int page_index, int total_pages, int refresh_mode)
{
    Paint_DrawRectangle(5, 58, 475, 789, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    int start_y = HEADER_HEIGHT;
    int max_display = get_fiction_page_size();

    int icon_width = GUI_WIDTH + 5;
    int available_width = 480 - 10 - icon_width - 20;

    for (int i = 0; i < num_entries && i < max_display; i++) {
        int y_pos = start_y + i * (Font18_UTF8.Height + 10 );
        
        const char* file_type = get_file_type(entries[i].name);
        if (strcmp(file_type, "text") == 0) {
            #if defined(CONFIG_IMG_SOURCE_EMBEDDED)
                Paint_ReadBmp(gImage_text,10,y_pos-1,32,32);
            #elif defined(CONFIG_IMG_SOURCE_TFCARD)
                GUI_ReadBmp(BMP_TEXT_PATH,10,y_pos-1);
            #endif
        } else {
            #if defined(CONFIG_IMG_SOURCE_EMBEDDED)
                Paint_ReadBmp(gImage_rests,10,y_pos-1,32,32);
            #elif defined(CONFIG_IMG_SOURCE_TFCARD)
                GUI_ReadBmp(BMP_RESTS_PATH,10,y_pos-1);
            #endif
        }
        
        char display_name[100]; 
        truncate_string_by_width(entries[i].name, display_name, sizeof(display_name), available_width, &Font18_UTF8);
        
        if (is_chinese_filename(entries[i].name)) {
            Paint_DrawString_CN(10 + icon_width, y_pos, display_name, &Font18_UTF8, WHITE, BLACK);
        } else {
            Paint_DrawString_EN(10 + icon_width, y_pos, display_name, &Font16, WHITE, BLACK);
        }

        if (i == current_selection) {
            Paint_DrawRectangle(5, y_pos - 5, 475, y_pos + Font18_UTF8.Height + 5, BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
        }
    }
    
    int status_y = EPD_WIDTH - (Font12_UTF8.Height*2+15);
    Paint_DrawLine(10, status_y - 5, EPD_HEIGHT - 10, status_y - 5, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    char page_info[50];
    snprintf(page_info, sizeof(page_info), "第%d/%d页 共%d个文件", page_index + 1, total_pages, num_entries);
    Paint_DrawString_CN(10, EPD_WIDTH-(Font12_UTF8.Height*2 + 10), page_info, &Font12_UTF8, WHITE, BLACK);
    
    Paint_DrawString_CN(10, EPD_WIDTH - (Font12_UTF8.Height + 5), "↑↓选择,单击确认:打开,双击确认:返回上级", &Font12_UTF8, WHITE, BLACK);

    if (refresh_mode == Global_refresh) {
        fiction_display_refresh(Image_Fiction, true);
    } else {
        fiction_display_refresh(Image_Fiction, false);
    }
}



static bool is_valid_utf8_sample(const uint8_t* data, size_t len, int* utf8_chars)
{
    size_t i = 0;
    int chars = 0;
    while (i < len) {
        uint8_t c = data[i];
        if (c < 0x80) {
            i++;
            continue;
        }

        int cont = 0;
        if (c >= 0xC2 && c <= 0xDF) cont = 1;
        else if (c >= 0xE0 && c <= 0xEF) cont = 2;
        else if (c >= 0xF0 && c <= 0xF4) cont = 3;
        else return false;

        if (i + (size_t)cont >= len) return false;
        for (int j = 1; j <= cont; ++j) {
            if ((data[i + (size_t)j] & 0xC0) != 0x80) return false;
        }

        // Reject invalid UTF-8 ranges (overlong/surrogate/out of range).
        if (c == 0xE0 && data[i + 1] < 0xA0) return false;
        if (c == 0xED && data[i + 1] >= 0xA0) return false;
        if (c == 0xF0 && data[i + 1] < 0x90) return false;
        if (c == 0xF4 && data[i + 1] > 0x8F) return false;

        chars++;
        i += (size_t)cont + 1;
    }
    if (utf8_chars) *utf8_chars = chars;
    return true;
}

static int count_gbk_pairs(const uint8_t* data, size_t len)
{
    int pairs = 0;
    size_t i = 0;
    while (i + 1 < len) {
        uint8_t c1 = data[i];
        uint8_t c2 = data[i + 1];
        if (c1 >= 0x81 && c1 <= 0xFE && c2 >= 0x40 && c2 <= 0xFE && c2 != 0x7F) {
            pairs++;
            i += 2;
        } else {
            i++;
        }
    }
    return pairs;
}

// Determine the encoding format of the txt file
static const char* detect_txt_encoding(const char* filepath)
{
    FILE* fp = fopen(filepath, "rb");
    if (!fp) return "unknown";

    uint8_t bom[4] = {0};
    size_t bom_n = fread(bom, 1, sizeof(bom), fp);
    if (bom_n >= 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF) {
        fclose(fp);
        return "UTF-8";
    }
    if (bom_n >= 2 && bom[0] == 0xFF && bom[1] == 0xFE) {
        fclose(fp);
        return "UTF-16 LE BOM";
    }
    if (bom_n >= 2 && bom[0] == 0xFE && bom[1] == 0xFF) {
        fclose(fp);
        return "UTF-16 BE BOM";
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return "unknown";
    }

    uint8_t sample[4096];
    size_t n = fread(sample, 1, sizeof(sample), fp);
    fclose(fp);
    if (n == 0) return "ASCII/ANSI";

    bool ascii_only = true;
    int high_bytes = 0;
    for (size_t i = 0; i < n; ++i) {
        if (sample[i] & 0x80) {
            ascii_only = false;
            high_bytes++;
        }
    }
    if (ascii_only) return "ASCII/ANSI";

    int utf8_chars = 0;
    bool utf8_valid = is_valid_utf8_sample(sample, n, &utf8_chars);
    int gbk_pairs = count_gbk_pairs(sample, n);

    if (utf8_valid && utf8_chars > 0) return "UTF-8";

    // If most high bytes can be grouped into GBK pairs, prefer GBK.
    if (gbk_pairs > 0 && (gbk_pairs * 2) >= (high_bytes * 7 / 10)) return "GBK/GB2312";
    if (gbk_pairs > 0) return "GBK/GB2312";

    return "unknown";
}

static bool is_epub_filepath(const char* filepath)
{
    if (!filepath) {
        return false;
    }
    const char* ext = strrchr(filepath, '.');
    if (!ext) {
        return false;
    }
    return strcasecmp(ext, ".epub") == 0;
}

static const char* fiction_identity_filepath(const fiction_context_t* ctx)
{
    if (!ctx) {
        return "";
    }
    if (ctx->is_epub && ctx->source_filepath[0] != '\0') {
        return ctx->source_filepath;
    }
    if (ctx->filepath[0] != '\0') {
        return ctx->filepath;
    }
    return "";
}

const char* fiction_get_notes_filepath(void)
{
    static char path[320];
    std::string notes_path = fiction_get_current_notes_filepath_string();
    if (notes_path.empty()) {
        path[0] = '\0';
        return path;
    }
    strncpy(path, notes_path.c_str(), sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    return path;
}

bool fiction_get_current_book_title(char* out, size_t out_len)
{
    if (!out || out_len == 0) {
        return false;
    }
    out[0] = '\0';

    std::string title = fiction_get_book_title_string();
    if (title.empty()) {
        return false;
    }

    strncpy(out, title.c_str(), out_len - 1);
    out[out_len - 1] = '\0';
    return true;
}

bool fiction_get_current_page_text(char* out, size_t out_len)
{
    if (!out || out_len == 0) {
        return false;
    }
    out[0] = '\0';

    std::string page_text = fiction_get_current_page_text_string();
    if (page_text.empty()) {
        return false;
    }

    strncpy(out, page_text.c_str(), out_len - 1);
    out[out_len - 1] = '\0';
    return true;
}

bool fiction_append_reading_note(const char* thoughts, char* out_message, size_t out_message_len)
{
    auto set_message = [out_message, out_message_len](const char* message) {
        if (!out_message || out_message_len == 0) {
            return;
        }
        strncpy(out_message, message, out_message_len - 1);
        out_message[out_message_len - 1] = '\0';
    };

    set_message("");

    if (!g_fiction_ctx.is_open) {
        set_message("当前没有打开书籍");
        return false;
    }

    std::string book_title = fiction_get_book_title_string();
    if (book_title.empty()) {
        set_message("无法获取当前书名");
        return false;
    }

    std::string notes_path = fiction_get_current_notes_filepath_string();
    if (notes_path.empty()) {
        set_message("无法生成读书笔记文件名");
        return false;
    }

    std::string page_text = fiction_get_current_page_text_string();
    if (page_text.empty()) {
        set_message("无法获取当前页内容");
        return false;
    }

    std::string note_text = thoughts ? thoughts : "";
    fiction_trim_trailing_whitespace(note_text);
    if (note_text.empty()) {
        set_message("感想内容不能为空");
        return false;
    }

    const int page_number = fiction_get_current_page_number();
    FILE* fp = fopen(notes_path.c_str(), "a");
    if (!fp) {
        ESP_LOGE(TAG, "Failed to open reading notes file: %s errno=%d", notes_path.c_str(), errno);
        set_message("打开读书笔记文件失败");
        return false;
    }

    int written = fprintf(fp,
                          "原文页码：P%d\n\n原文：\n%s\n\n读书感想：\n%s\n\n--------------------------------\n\n",
                          page_number,
                          page_text.c_str(),
                          note_text.c_str());
    fclose(fp);

    if (written < 0) {
        set_message("写入读书笔记失败");
        return false;
    }

    ESP_LOGI(TAG, "Reading note saved: file=%s book=%s page=%d page_text_len=%u thoughts_len=%u written=%d",
             notes_path.c_str(),
             book_title.c_str(),
             page_number,
             static_cast<unsigned>(page_text.size()),
             static_cast<unsigned>(note_text.size()),
             written);

    std::string ok = std::string("已追加到 ") + notes_path;
    set_message(ok.c_str());
    return true;
}

// The file selection menu displayed in pagination
int page_fiction_file(void)
{
    fiction_dir_rescan_cancel_and_wait();
    fiction_dir_rescan_release_buffer();

    if((Image_Fiction = (UBYTE *)heap_caps_malloc(EPD_SIZE_MONO,MALLOC_CAP_SPIRAM)) == NULL) 
    {
        ESP_LOGE(TAG,"Failed to apply for black memory...");
        // return ESP_FAIL;
    }
    Paint_NewImage(Image_Fiction, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_SetScale(2);
    Paint_SelectImage(Image_Fiction);
    Paint_Clear(WHITE);

    // Calculate pagination parameters
    int page_size = get_fiction_page_size(); 
    if (page_size < 1) page_size = 5;

    // Allocate a large array with PSRAM to store all the files and directories in the current directory
    file_entry_t* entries = (file_entry_t*)heap_caps_malloc(page_size * sizeof(file_entry_t), MALLOC_CAP_SPIRAM);
    if (!entries) {
        ESP_LOGE(TAG, "The memory allocation for the file list failed");
        if (Image_Fiction) {
            heap_caps_free(Image_Fiction);
            Image_Fiction = NULL;
        }
        return -1;
    }
    int total_num = 0; 
    int num = 0; 
    int num_selection = 0;
    int page_index = 0;
    bool first_display = true; 

    Paint_DrawRectangle(5, 58, 475, 789, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    uint16_t x_or = reassignCoordinates_CH(240, " 正在读取文件目录... ", &Font24_UTF8);
    Paint_DrawString_CN(x_or, 410, " 正在读取文件目录... ", &Font24_UTF8, BLACK, WHITE);
    Refresh_page_fiction(Image_Fiction);
    Paint_DrawRectangle(5, 58, 475, 789, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);

    total_num = get_dir_file_count("/sdcard/fiction");
    int total_pages = (total_num + page_size - 1) / page_size;
    if (total_pages == 0) total_pages = 1;

    num = list_dir_page("/sdcard/fiction", entries, page_index * page_size, page_size);

    
    int button;
    int time_count = 0;
    bool async_rescan_pending = false;
    int async_expected_page_index = -1;
    xSemaphoreTake(rtc_mutex, portMAX_DELAY);
    Time_data rtc_time = PCF85063_GetTime();
    xSemaphoreGive(rtc_mutex);
    int last_minutes = -1;
    last_minutes = rtc_time.minutes;
    display_fiction_time(rtc_time);

    display_fiction_file_list(entries, num, num_selection, page_index, total_pages, Partial_refresh);


    while (1) {
        button = wait_key_event_and_return_code(pdMS_TO_TICKS(1000)); 
        if(button == -1) time_count++;

        if((time_count >= EPD_Sleep_Time)) {
            ESP_LOGI("home", "EPD_Sleep");
            EPD_Sleep();
            int sleep_js = 0;
            while(1)
            {
                button = wait_key_event_and_return_code(pdMS_TO_TICKS(1000));
                if (button == 12){
                    ESP_LOGI("home", "EPD_Init");
                    EPD_Init();
                    // Forced_refresh_fiction(Image_Fiction);
                    time_count = 0;
                    break;
                } else if (button == 8 || button == 22 || button == 14 || button == 0 || button == 7){
                    ESP_LOGI("home", "EPD_Init");
                    EPD_Init();
                    Refresh_page_fiction(Image_Fiction);
                    time_count = 0;
                    break;
                }
                xSemaphoreTake(rtc_mutex, portMAX_DELAY);
                rtc_time = PCF85063_GetTime();
                xSemaphoreGive(rtc_mutex);
                if(rtc_time.minutes != last_minutes) {
                    last_minutes = rtc_time.minutes;
                    ESP_LOGI("home", "EPD_Init");
                    EPD_Init();
                    // display_fiction_time_last(rtc_time);
                    display_fiction_time(rtc_time);
                    Refresh_page_fiction(Image_Fiction);
                    ESP_LOGI("home", "EPD_Sleep");
                    EPD_Sleep();
                    // sleep_js++;
                    // if(sleep_js > Unattended_Time){
                    //     ESP_LOGI("home", "pwr_off");
                    //     axp_pwr_off();
                    // }
                }
            }
        }

        bool async_success = false;
        int async_total_num = 0;
        int async_total_pages = 1;
        int async_page_index = 0;
        int async_num = 0;
        if (fiction_dir_rescan_try_consume(entries, page_size, &async_total_num, &async_total_pages,
                                           &async_page_index, &async_num, &async_success)) {
            if (async_success && async_rescan_pending &&
                page_index == async_expected_page_index &&
                async_page_index == async_expected_page_index) {
                total_num = async_total_num;
                total_pages = async_total_pages;
                num = async_num;
                if (num <= 0) {
                    num_selection = 0;
                } else if (num_selection >= num) {
                    num_selection = num - 1;
                } else if (num_selection < 0) {
                    num_selection = 0;
                }
                display_fiction_file_list(entries, num, num_selection, page_index, total_pages, Partial_refresh);
                ESP_LOGI(TAG, "Async directory rescan applied: total=%d, page=%d/%d, num=%d",
                         total_num, page_index + 1, total_pages, num);
            }
            async_rescan_pending = false;
            async_expected_page_index = -1;
        }

        if (button == 14) {
            time_count = 0;
            async_rescan_pending = false;
            async_expected_page_index = -1;
            num_selection++;          
            if (num_selection >= page_size || num_selection >= total_num) {
                ESP_LOGI("num", "Turn to the next page");
                page_index++;
                if(page_index * page_size >= total_num)  page_index = 0;
                num = list_dir_page("/sdcard/fiction", entries, page_index * page_size, page_size);
                num_selection = 0;
                display_fiction_file_list(entries, num, num_selection, page_index, total_pages, Partial_refresh);    
            } else {
                update_fiction_selection(num_selection);
            }
            
        } else if (button == 0) {
            time_count = 0;
            async_rescan_pending = false;
            async_expected_page_index = -1;
            num_selection--;
            if (num_selection < 0) {
                ESP_LOGI("num", "Turn to the previous page");
                page_index--;
                if(page_index < 0)  page_index = total_pages -1;
                num = list_dir_page("/sdcard/fiction", entries, page_index * page_size, page_size);
                num_selection = num-1;
                display_fiction_file_list(entries, num, num_selection, page_index, total_pages, Partial_refresh);
            } else {
                update_fiction_selection(num_selection);
            }
            
        } else if (button == 7) { 
            if (async_rescan_pending) {
                fiction_dir_rescan_cancel_and_wait();
                async_rescan_pending = false;
                async_expected_page_index = -1;
            }

            ESP_LOGI("num", "The selected file is %s", entries[num_selection].name);
            if (strcasecmp(get_file_type(entries[num_selection].name), "text") == 0) {
                char filepath[300];
                snprintf(filepath, sizeof(filepath), "/sdcard/fiction/%s", entries[num_selection].name);
                ESP_LOGI("file", "try open: %s", filepath);
                const char* encoding = is_epub_filepath(filepath) ? "UTF-8" : detect_txt_encoding(filepath);
                ESP_LOGI("file", "open-file: %s, encoding: %s", entries[num_selection].name, encoding);
                page_fiction_open_file(filepath, encoding);
            } 
            ESP_LOGI("home", "EPD_Init");
            EPD_Init();

            // Render cached directory immediately for fast return.
            Paint_NewImage(Image_Fiction, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
            Paint_SetScale(2);
            Paint_SelectImage(Image_Fiction);
            Paint_Clear(WHITE);

            xSemaphoreTake(rtc_mutex, portMAX_DELAY);
            rtc_time = PCF85063_GetTime();
            xSemaphoreGive(rtc_mutex);
            last_minutes = rtc_time.minutes;
            display_fiction_time(rtc_time);
            display_fiction_file_list(entries, num, num_selection, page_index, total_pages, Global_refresh);

            if (fiction_dir_rescan_start_async(page_index, page_size)) {
                async_rescan_pending = true;
                async_expected_page_index = page_index;
                ESP_LOGI(TAG, "Async directory rescan scheduled for page %d", page_index + 1);
            } else {
                // Fallback to synchronous rescan when async is disabled/unavailable.
                total_num = get_dir_file_count("/sdcard/fiction");
                total_pages = (total_num + page_size - 1) / page_size;
                if (total_pages == 0) total_pages = 1;
                if (page_index < 0) page_index = 0;
                if (page_index >= total_pages) page_index = total_pages - 1;

                num = list_dir_page("/sdcard/fiction", entries, page_index * page_size, page_size);
                if (num <= 0) {
                    num_selection = 0;
                } else if (num_selection >= num) {
                    num_selection = num - 1;
                } else if (num_selection < 0) {
                    num_selection = 0;
                }
                display_fiction_file_list(entries, num, num_selection, page_index, total_pages, Partial_refresh);
            }
            time_count = 0;
        } else if (button == 8 || button == 22) {
            fiction_dir_rescan_cancel_and_wait();
            fiction_dir_rescan_release_buffer();
            heap_caps_free(entries);
            heap_caps_free(Image_Fiction);
            Image_Fiction = NULL;
            return -1;
        } else if (button == 12) {
            ESP_LOGI("home", "EPD_Init");
            EPD_Init();
            Forced_refresh_fiction(Image_Fiction);
            time_count = 0;
        }

        xSemaphoreTake(rtc_mutex, portMAX_DELAY);
        rtc_time = PCF85063_GetTime();
        xSemaphoreGive(rtc_mutex);
        if(rtc_time.minutes != last_minutes) {
            last_minutes = rtc_time.minutes;
            display_fiction_time(rtc_time);
            Refresh_page_fiction(Image_Fiction);
        }
    }
    fiction_dir_rescan_cancel_and_wait();
    fiction_dir_rescan_release_buffer();
    heap_caps_free(entries);
    heap_caps_free(Image_Fiction);
    Image_Fiction = NULL;
}

