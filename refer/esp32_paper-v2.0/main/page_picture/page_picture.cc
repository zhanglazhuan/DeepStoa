#include "page_picture.h"

#include "axp_prot.h"
#include "application.h"
#include "button_bsp.h"
#include "epaper_port.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "jpeg_to_image.h"
#include "pcf85063_bsp.h"
#include "sdcard_bsp.h"
#include "GUI_BMPfile.h"
#include "GUI_Paint.h"
#include "status_bar.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef LODEPNG_NO_COMPILE_CPP
#define LODEPNG_NO_COMPILE_CPP
#endif
#include "libs/lodepng/lodepng.h"

extern bool wifi_enable;
extern SemaphoreHandle_t rtc_mutex;
extern uint8_t *Image_Mono;

static const char *TAG = "page_picture";

#define PICTURE_DIR_PATH "/sdcard/picture"

#define HEADER_LINE_Y 54
#define LIST_TOP_Y 66
#define LIST_BOTTOM_Y 700
#define LIST_ITEM_HEIGHT 44
#define LIST_LEFT_X 6
#define LIST_RIGHT_X 474
#define LIST_ICON_X 14
#define LIST_TEXT_X 56
#define JPEG_GRAYSCALE_THRESHOLD 160
#define PNG_GRAYSCALE_THRESHOLD 160

static Time_data page_picture_get_time(void)
{
    Time_data time = {0};
    if (rtc_mutex != NULL) {
        xSemaphoreTake(rtc_mutex, portMAX_DELAY);
        time = PCF85063_GetTime();
        xSemaphoreGive(rtc_mutex);
    }
    return time;
}

static void truncate_string_by_width(const char *source, char *dest, int dest_size, int max_width, cFONT *font)
{
    int current_width = 0;
    int src_i = 0;
    int dest_i = 0;
    const int ellipsis_width = font->Width_EN * 3;
    const int available_width = max_width - ellipsis_width;

    while (source[src_i] != '\0' && dest_i < dest_size - 4) {
        const unsigned char ch = (unsigned char)source[src_i];
        int char_width = font->Width_EN;
        int char_bytes = 1;

        if (ch < 0x80) {
            char_width = font->Width_EN;
            char_bytes = 1;
        } else if ((ch & 0xE0) == 0xC0) {
            char_width = font->Width_CH;
            char_bytes = 2;
        } else if ((ch & 0xF0) == 0xE0) {
            char_width = font->Width_CH;
            char_bytes = 3;
        } else if ((ch & 0xF8) == 0xF0) {
            char_width = font->Width_CH;
            char_bytes = 4;
        }

        if (current_width + char_width > available_width) {
            break;
        }

        for (int j = 0; j < char_bytes && source[src_i + j] != '\0' && dest_i < dest_size - 4; ++j) {
            dest[dest_i++] = source[src_i + j];
        }

        current_width += char_width;
        src_i += char_bytes;
    }

    if (source[src_i] != '\0') {
        dest[dest_i++] = '.';
        dest[dest_i++] = '.';
        dest[dest_i++] = '.';
    }

    dest[dest_i] = '\0';
}

static bool is_valid_utf8_string(const char *str)
{
    if (str == NULL) {
        return false;
    }

    const unsigned char *p = (const unsigned char *)str;
    while (*p != '\0') {
        if (*p < 0x80) {
            ++p;
            continue;
        }

        int need = 0;
        if ((*p & 0xE0) == 0xC0) {
            need = 1;
            if (*p < 0xC2) {
                return false;
            }
        } else if ((*p & 0xF0) == 0xE0) {
            need = 2;
        } else if ((*p & 0xF8) == 0xF0) {
            need = 3;
            if (*p > 0xF4) {
                return false;
            }
        } else {
            return false;
        }

        ++p;
        for (int i = 0; i < need; ++i) {
            if ((p[i] & 0xC0) != 0x80) {
                return false;
            }
        }
        p += need;
    }

    return true;
}

static bool should_use_gbk_font(const char *filename)
{
    if (filename == NULL) {
        return false;
    }

    bool has_non_ascii = false;
    for (const unsigned char *p = (const unsigned char *)filename; *p != '\0'; ++p) {
        if (*p >= 0x80) {
            has_non_ascii = true;
            break;
        }
    }

    return has_non_ascii && !is_valid_utf8_string(filename);
}

static void truncate_gbk_by_width(const char *source, char *dest, int dest_size, int max_width, cFONT *font)
{
    int current_width = 0;
    int src_i = 0;
    int dest_i = 0;
    const int ellipsis_width = font->Width_EN * 3;
    const int available_width = max_width - ellipsis_width;

    while (source[src_i] != '\0' && dest_i < dest_size - 4) {
        unsigned char ch = (unsigned char)source[src_i];
        int char_width = font->Width_EN;
        int char_bytes = 1;

        if (ch >= 0x80) {
            char_width = font->Width_CH;
            char_bytes = (source[src_i + 1] != '\0') ? 2 : 1;
        }

        if (current_width + char_width > available_width) {
            break;
        }

        for (int j = 0; j < char_bytes && source[src_i + j] != '\0' && dest_i < dest_size - 4; ++j) {
            dest[dest_i++] = source[src_i + j];
        }

        current_width += char_width;
        src_i += char_bytes;
    }

    if (source[src_i] != '\0') {
        dest[dest_i++] = '.';
        dest[dest_i++] = '.';
        dest[dest_i++] = '.';
    }

    dest[dest_i] = '\0';
}

static void prepare_filename_for_display(const char *filename,
                                         char *display_name,
                                         int display_name_size,
                                         int max_width,
                                         cFONT **font_out)
{
    cFONT *font = &Font18_UTF8;
    if (should_use_gbk_font(filename)) {
        font = &Font18_GBK;
        truncate_gbk_by_width(filename, display_name, display_name_size, max_width, font);
    } else {
        truncate_string_by_width(filename, display_name, display_name_size, max_width, &Font18_UTF8);
    }

    if (font_out != NULL) {
        *font_out = font;
    }
}

static bool is_picture_file(const char *filename)
{
    const char *ext = strrchr(filename, '.');
    if (ext == NULL) {
        return false;
    }

    return (strcasecmp(ext, ".bmp") == 0) ||
           (strcasecmp(ext, ".jpg") == 0) ||
           (strcasecmp(ext, ".jpeg") == 0) ||
           (strcasecmp(ext, ".png") == 0) ||
           (strcasecmp(ext, ".gif") == 0) ||
           (strcasecmp(ext, ".webp") == 0);
}

static void make_picture_path(const char *filename, char *out_path, size_t out_len)
{
    snprintf(out_path, out_len, "%s/%s", PICTURE_DIR_PATH, filename);
}

static void draw_top_status(void)
{
    char time_str[16] = {0};
    char battery_str[16] = {0};
    int battery_level = get_battery_power();

    Time_data rtc_time = page_picture_get_time();
    snprintf(time_str, sizeof(time_str), "%02d:%02d", rtc_time.hours, rtc_time.minutes);
    Paint_DrawString_EN(20, 11, time_str, &Font16, WHITE, BLACK);

#if defined(CONFIG_IMG_SOURCE_EMBEDDED)
    DrawWifiStatusIcon(326, 8);
    Paint_ReadBmp(gImage_BAT, 370, 17, 32, 16);
#elif defined(CONFIG_IMG_SOURCE_TFCARD)
    DrawWifiStatusIcon(326, 8);
    GUI_ReadBmp(BMP_BAT_PATH, 370, 17);
#endif

    snprintf(battery_str, sizeof(battery_str), "%d%%", battery_level);
    Paint_DrawString_EN(411, 11, battery_str, &Font16, WHITE, BLACK);
    if (battery_level < 0) {
        battery_level = 20;
    } else {
        battery_level = battery_level * 20 / 100;
    }
    Paint_DrawRectangle(375, 22, 395, 30, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawRectangle(375, 22, 375 + battery_level, 30, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawLine(2, HEADER_LINE_Y, EPD_HEIGHT - 2, HEADER_LINE_Y, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
}

static void draw_page_header(const char *title, const char *subtitle)
{
    draw_top_status();
    if (title != NULL) {
        uint16_t title_x = reassignCoordinates_CH(240, title, &Font18_UTF8);
        Paint_DrawString_CN(title_x, 66, title, &Font18_UTF8, BLACK, WHITE);
    }
    if (subtitle != NULL) {
        Paint_DrawString_CN(10, 88, subtitle, &Font16_UTF8, WHITE, BLACK);
    }
}

static int get_list_page_size(void)
{
    int page_size = (LIST_BOTTOM_Y - LIST_TOP_Y) / LIST_ITEM_HEIGHT;
    if (page_size < 1) {
        page_size = 1;
    }
    return page_size;
}

static esp_err_t ensure_picture_directory(void)
{
    struct stat st = {0};
    if (stat(PICTURE_DIR_PATH, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return ESP_OK;
        }
        ESP_LOGE(TAG, "%s exists but is not a directory", PICTURE_DIR_PATH);
        return ESP_FAIL;
    }

    if (mkdir(PICTURE_DIR_PATH, 0755) == 0 || errno == EEXIST) {
        ESP_LOGI(TAG, "Created picture directory: %s", PICTURE_DIR_PATH);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to create %s: errno=%d (%s)", PICTURE_DIR_PATH, errno, strerror(errno));
    return ESP_FAIL;
}

static int load_picture_entries(file_entry_t *entries, int max_num)
{
    if (entries == NULL || max_num <= 0) {
        return 0;
    }

    int total = list_dir_once(PICTURE_DIR_PATH, entries, max_num);
    int write_index = 0;

    for (int i = 0; i < total; ++i) {
        if (entries[i].is_dir) {
            continue;
        }
        if (!is_picture_file(entries[i].name)) {
            continue;
        }
        if (write_index != i) {
            entries[write_index] = entries[i];
        }
        ++write_index;
    }

    return write_index;
}

static void display_picture_list(const file_entry_t *entries, int total_entries, int selected_index, bool full_refresh)
{
    const int page_size = get_list_page_size();
    const int total_pages = (total_entries <= 0) ? 1 : ((total_entries + page_size - 1) / page_size);
    const int page_index = (total_entries <= 0) ? 0 : (selected_index / page_size);
    const int start_index = page_index * page_size;
    const int end_index = (start_index + page_size > total_entries) ? total_entries : (start_index + page_size);

    Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_SetScale(2);
    Paint_SelectImage(Image_Mono);
    Paint_Clear(WHITE);

    draw_top_status();

    if (total_entries <= 0) {
        uint16_t msg_x = reassignCoordinates_CH(240, " 当前目录没有图片文件 ", &Font18_UTF8);
        Paint_DrawString_CN(msg_x, 320, " 当前目录没有图片文件 ", &Font18_UTF8, BLACK, WHITE);
        Paint_DrawString_CN(82, 364, " 支持: bmp/jpg/jpeg/png/gif/webp ", &Font16_UTF8, WHITE, BLACK);
    } else {
        for (int idx = start_index; idx < end_index; ++idx) {
            const int row = idx - start_index;
            const int y = LIST_TOP_Y + row * LIST_ITEM_HEIGHT;

#if defined(CONFIG_IMG_SOURCE_EMBEDDED)
            Paint_ReadBmp(gImage_picture, LIST_ICON_X, y + 6, 32, 32);
#elif defined(CONFIG_IMG_SOURCE_TFCARD)
            GUI_ReadBmp(BMP_RESTS_PATH, LIST_ICON_X, y + 6);
#endif

            char display_name[128] = {0};
            cFONT *name_font = &Font18_UTF8;
            prepare_filename_for_display(entries[idx].name, display_name, sizeof(display_name), 400, &name_font);
            Paint_DrawString_CN(LIST_TEXT_X, y + 10, display_name, name_font, WHITE, BLACK);

            if (idx == selected_index) {
                Paint_DrawRectangle(LIST_LEFT_X, y + 2, LIST_RIGHT_X, y + LIST_ITEM_HEIGHT - 2,
                                    BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
            }
        }
    }

    Paint_DrawLine(10, 718, 470, 718, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    char page_info[64] = {0};
    snprintf(page_info, sizeof(page_info), "第%d/%d页  共%d张", page_index + 1, total_pages, total_entries);
    Paint_DrawString_CN(10, 726, page_info, &Font12_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(10, 760, "↑↓选择, 单击确认:查看, 长按功能键:菜单, 双击确认:返回", &Font12_UTF8, WHITE, BLACK);

    if (full_refresh) {
        EPD_Display_Base(Image_Mono);
    } else {
        EPD_Display_Partial(Image_Mono, 0, 0, EPD_WIDTH, EPD_HEIGHT);
    }
}

static void display_message_page(const char *title, const char *line1, const char *line2, bool full_refresh)
{
    Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_SetScale(2);
    Paint_SelectImage(Image_Mono);
    Paint_Clear(WHITE);

    draw_page_header(title, NULL);

    uint16_t x1 = reassignCoordinates_CH(240, line1, &Font18_UTF8);
    Paint_DrawString_CN(x1, 300, line1, &Font18_UTF8, WHITE, BLACK);
    if (line2 != NULL) {
        uint16_t x2 = reassignCoordinates_CH(240, line2, &Font16_UTF8);
        Paint_DrawString_CN(x2, 340, line2, &Font16_UTF8, WHITE, BLACK);
    }

    Paint_DrawString_CN(90, 760, "单击确认或双击确认返回", &Font16_UTF8, WHITE, BLACK);

    if (full_refresh) {
        EPD_Display_Base(Image_Mono);
    } else {
        EPD_Display_Partial(Image_Mono, 0, 0, EPD_WIDTH, EPD_HEIGHT);
    }
}

static void wait_back_key_or_confirm(void)
{
    while (1) {
        int button = wait_key_event_and_return_code(pdMS_TO_TICKS(1000));
        if (button == 7 || button == 8 || button == 22 || button == 12) {
            return;
        }
    }
}

static void display_picture_manage_page(const char *filename, int option_index, bool full_refresh)
{
    static const char *menu_options[] = {
        "查看",
        "文件信息",
        "上传到服务端",
        "删除文件",
        "返回"
    };
    const int option_count = sizeof(menu_options) / sizeof(menu_options[0]);

    Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_SetScale(2);
    Paint_SelectImage(Image_Mono);
    Paint_Clear(WHITE);

    draw_page_header(" 图片文件菜单 ", NULL);

    char display_name[96] = {0};
    cFONT *name_font = &Font18_UTF8;
    prepare_filename_for_display(filename, display_name, sizeof(display_name), 340, &name_font);
    Paint_DrawString_CN(12, 120, "文件:", &Font18_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(88, 120, display_name, name_font, WHITE, BLACK);

    const int x1 = 70;
    const int x2 = 410;
    const int y_start = 220;
    const int y_step = 88;
    const int y_height = 58;

    for (int i = 0; i < option_count; ++i) {
        const int y1 = y_start + i * y_step;
        const int y2 = y1 + y_height;
        const bool selected = (i == option_index);
        uint16_t text_x = reassignCoordinates_CH((x1 + x2) / 2, menu_options[i], &Font24_UTF8);

        Paint_DrawRectangle(x1, y1, x2, y2, BLACK, DOT_PIXEL_2X2,
                            selected ? DRAW_FILL_FULL : DRAW_FILL_EMPTY);
        Paint_DrawString_CN(text_x, y1 + 11, menu_options[i], &Font24_UTF8,
                            selected ? BLACK : WHITE,
                            selected ? WHITE : BLACK);
    }

    Paint_DrawString_CN(64, 760, "上下切换, 单击确认执行, 双击确认返回", &Font16_UTF8, WHITE, BLACK);

    if (full_refresh) {
        EPD_Display_Base(Image_Mono);
    } else {
        EPD_Display_Partial(Image_Mono, 0, 0, EPD_WIDTH, EPD_HEIGHT);
    }
}

static uint8_t rgb565_to_gray(uint16_t rgb565)
{
    uint8_t r5 = (rgb565 >> 11) & 0x1F;
    uint8_t g6 = (rgb565 >> 5) & 0x3F;
    uint8_t b5 = rgb565 & 0x1F;

    uint8_t r = (r5 << 3) | (r5 >> 2);
    uint8_t g = (g6 << 2) | (g6 >> 4);
    uint8_t b = (b5 << 3) | (b5 >> 2);
    return (uint8_t)((r * 38 + g * 75 + b * 15) >> 7);
}

static uint8_t rgb888_to_gray(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint8_t)((r * 38 + g * 75 + b * 15) >> 7);
}

static void blend_with_white(uint8_t *r, uint8_t *g, uint8_t *b, uint8_t a)
{
    if (a == 255) {
        return;
    }
    if (a == 0) {
        *r = 255;
        *g = 255;
        *b = 255;
        return;
    }

    uint16_t inv_a = (uint16_t)(255 - a);
    *r = (uint8_t)(((uint16_t)(*r) * a + 255U * inv_a) / 255U);
    *g = (uint8_t)(((uint16_t)(*g) * a + 255U * inv_a) / 255U);
    *b = (uint8_t)(((uint16_t)(*b) * a + 255U * inv_a) / 255U);
}

static bool read_file_to_memory(const char *file_path, uint8_t **buffer, size_t *buffer_size)
{
    if (buffer == NULL || buffer_size == NULL) {
        return false;
    }

    *buffer = NULL;
    *buffer_size = 0;

    struct stat st = {0};
    if (stat(file_path, &st) != 0 || st.st_size <= 0) {
        return false;
    }

    FILE *fp = fopen(file_path, "rb");
    if (fp == NULL) {
        return false;
    }

    uint8_t *data = (uint8_t *)heap_caps_malloc((size_t)st.st_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (data == NULL) {
        data = (uint8_t *)heap_caps_malloc((size_t)st.st_size, MALLOC_CAP_8BIT);
    }
    if (data == NULL) {
        fclose(fp);
        return false;
    }

    size_t read_size = fread(data, 1, (size_t)st.st_size, fp);
    fclose(fp);
    if (read_size != (size_t)st.st_size) {
        heap_caps_free(data);
        return false;
    }

    *buffer = data;
    *buffer_size = read_size;
    return true;
}

static void compute_fit_rect(int src_w, int src_h, int *dst_x, int *dst_y, int *dst_w, int *dst_h)
{
    if (src_w <= 0 || src_h <= 0 || dst_x == NULL || dst_y == NULL || dst_w == NULL || dst_h == NULL) {
        return;
    }

    int target_w = Paint.Width;
    int target_h = Paint.Height;
    if (target_w <= 0 || target_h <= 0) {
        target_w = EPD_WIDTH;
        target_h = EPD_HEIGHT;
    }

    if ((int64_t)target_w * src_h <= (int64_t)target_h * src_w) {
        *dst_w = target_w;
        *dst_h = (int)(((int64_t)target_w * src_h) / src_w);
    } else {
        *dst_h = target_h;
        *dst_w = (int)(((int64_t)target_h * src_w) / src_h);
    }

    if (*dst_w < 1) {
        *dst_w = 1;
    }
    if (*dst_h < 1) {
        *dst_h = 1;
    }

    *dst_x = (target_w - *dst_w) / 2;
    *dst_y = (target_h - *dst_h) / 2;
}

static void render_rgb565_preview(const uint8_t *rgb565_data,
                                  size_t stride,
                                  int src_w,
                                  int src_h,
                                  uint8_t threshold)
{
    int dst_x = 0;
    int dst_y = 0;
    int dst_w = 0;
    int dst_h = 0;
    compute_fit_rect(src_w, src_h, &dst_x, &dst_y, &dst_w, &dst_h);

    for (int dy = 0; dy < dst_h; ++dy) {
        int sy = (int)(((int64_t)dy * src_h) / dst_h);
        const uint8_t *src_row = rgb565_data + (size_t)sy * stride;
        for (int dx = 0; dx < dst_w; ++dx) {
            int sx = (int)(((int64_t)dx * src_w) / dst_w);
            const uint8_t *pixel = src_row + (size_t)sx * 2U;
            uint16_t rgb565 = (uint16_t)(pixel[0] | (pixel[1] << 8));
            uint8_t gray = rgb565_to_gray(rgb565);
            Paint_SetPixel((UWORD)(dst_x + dx), (UWORD)(dst_y + dy), gray < threshold ? BLACK : WHITE);
        }
        if ((dy & 0x0F) == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

static void render_rgba8888_preview(const unsigned char *rgba_data,
                                    unsigned src_w,
                                    unsigned src_h,
                                    uint8_t threshold)
{
    int dst_x = 0;
    int dst_y = 0;
    int dst_w = 0;
    int dst_h = 0;
    compute_fit_rect((int)src_w, (int)src_h, &dst_x, &dst_y, &dst_w, &dst_h);

    for (int dy = 0; dy < dst_h; ++dy) {
        unsigned sy = (unsigned)(((uint64_t)dy * src_h) / (unsigned)dst_h);
        const unsigned char *src_row = rgba_data + (size_t)sy * src_w * 4U;
        for (int dx = 0; dx < dst_w; ++dx) {
            unsigned sx = (unsigned)(((uint64_t)dx * src_w) / (unsigned)dst_w);
            const unsigned char *pixel = src_row + (size_t)sx * 4U;
            uint8_t r = pixel[0];
            uint8_t g = pixel[1];
            uint8_t b = pixel[2];
            blend_with_white(&r, &g, &b, pixel[3]);
            uint8_t gray = rgb888_to_gray(r, g, b);
            Paint_SetPixel((UWORD)(dst_x + dx), (UWORD)(dst_y + dy), gray < threshold ? BLACK : WHITE);
        }
        if ((dy & 0x0F) == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

static bool view_jpeg_file(const char *file_path)
{
    uint8_t *file_data = NULL;
    size_t file_size = 0;
    if (!read_file_to_memory(file_path, &file_data, &file_size)) {
        return false;
    }

    uint8_t *rgb565_data = NULL;
    size_t out_len = 0;
    size_t width = 0;
    size_t height = 0;
    size_t stride = 0;
    esp_err_t ret = jpeg_to_image(file_data, file_size, &rgb565_data, &out_len, &width, &height, &stride);
    heap_caps_free(file_data);
    if (ret != ESP_OK || rgb565_data == NULL || width == 0 || height == 0) {
        if (rgb565_data != NULL) {
            heap_caps_free(rgb565_data);
        }
        return false;
    }

    Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_SetScale(2);
    Paint_SelectImage(Image_Mono);
    Paint_Clear(WHITE);
    Paint_SetMirroring(MIRROR_NONE);
    render_rgb565_preview(rgb565_data, stride, (int)width, (int)height, JPEG_GRAYSCALE_THRESHOLD);
    EPD_Display_Base(Image_Mono);

    heap_caps_free(rgb565_data);
    return true;
}

static bool view_png_file(const char *file_path)
{
    uint8_t *file_data = NULL;
    size_t file_size = 0;
    if (!read_file_to_memory(file_path, &file_data, &file_size)) {
        return false;
    }

    unsigned src_w = 0;
    unsigned src_h = 0;
    unsigned char *rgba = NULL;
    unsigned error = lodepng_decode32(&rgba, &src_w, &src_h, file_data, file_size);
    heap_caps_free(file_data);
    if (error != 0 || rgba == NULL || src_w == 0 || src_h == 0) {
        if (rgba != NULL) {
            free(rgba);
        }
        return false;
    }

    Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_SetScale(2);
    Paint_SelectImage(Image_Mono);
    Paint_Clear(WHITE);
    Paint_SetMirroring(MIRROR_NONE);
    render_rgba8888_preview(rgba, src_w, src_h, PNG_GRAYSCALE_THRESHOLD);
    EPD_Display_Base(Image_Mono);

    free(rgba);
    return true;
}

static void view_picture_file(const char *filename)
{
    char file_path[320] = {0};
    make_picture_path(filename, file_path, sizeof(file_path));

    const char *ext = strrchr(filename, '.');
    if (ext == NULL) {
        display_message_page(" 查看图片 ", "无法识别图片格式", "请检查文件扩展名", true);
        wait_back_key_or_confirm();
        return;
    }

    if (strcasecmp(ext, ".bmp") == 0) {
        FILE *fp = fopen(file_path, "rb");
        if (fp == NULL) {
            ESP_LOGE(TAG, "Failed to open picture: %s", file_path);
            display_message_page(" 查看图片 ", "文件打开失败", "请检查文件是否存在", true);
            wait_back_key_or_confirm();
            return;
        }
        fclose(fp);

        Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
        Paint_SetScale(2);
        Paint_SelectImage(Image_Mono);
        Paint_Clear(WHITE);
        Paint_SetMirroring(MIRROR_ORIGIN);
        GUI_ReadBmp(file_path, 0, 0);
        Paint_SetMirroring(MIRROR_NONE);

        EPD_Display_Base(Image_Mono);
    } else if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
        if (!view_jpeg_file(file_path)) {
            display_message_page(" 查看图片 ", "JPG 解码失败", "请检查图片是否为基线 JPEG", true);
            wait_back_key_or_confirm();
            return;
        }
    } else if (strcasecmp(ext, ".png") == 0) {
        if (!view_png_file(file_path)) {
            display_message_page(" 查看图片 ", "PNG 解码失败", "请检查图片文件是否完整", true);
            wait_back_key_or_confirm();
            return;
        }
    } else {
        display_message_page(" 查看图片 ", "当前暂不支持此格式预览", "支持: BMP/JPG/PNG", true);
        wait_back_key_or_confirm();
        return;
    }

    while (1) {
        int button = wait_key_event_and_return_code(pdMS_TO_TICKS(1000));
        if (button == 7 || button == 8 || button == 22 || button == 12) {
            return;
        }
    }
}

static void show_picture_file_info(const char *filename)
{
    char file_path[320] = {0};
    make_picture_path(filename, file_path, sizeof(file_path));

    struct stat file_stat = {0};
    if (stat(file_path, &file_stat) != 0) {
        display_message_page(" 文件信息 ", "无法获取文件信息", "请检查文件是否存在", true);
        wait_back_key_or_confirm();
        return;
    }

    Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_SetScale(2);
    Paint_SelectImage(Image_Mono);
    Paint_Clear(WHITE);

    draw_page_header(" 文件信息 ", NULL);

    char line[192] = {0};
    char display_name[96] = {0};
    cFONT *name_font = &Font18_UTF8;
    prepare_filename_for_display(filename, display_name, sizeof(display_name), 360, &name_font);
    Paint_DrawString_CN(14, 120, "文件名:", &Font18_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(102, 120, display_name, name_font, WHITE, BLACK);

    const double size_kb = (double)file_stat.st_size / 1024.0;
    const double size_mb = size_kb / 1024.0;
    snprintf(line, sizeof(line), "大小: %.2f KB (%.2f MB)", size_kb, size_mb);
    Paint_DrawString_CN(14, 164, line, &Font18_UTF8, WHITE, BLACK);

    struct tm *timeinfo = localtime(&file_stat.st_mtime);
    char time_str[64] = {0};
    if (timeinfo != NULL) {
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", timeinfo);
    } else {
        snprintf(time_str, sizeof(time_str), "未知");
    }
    snprintf(line, sizeof(line), "修改时间: %s", time_str);
    Paint_DrawString_CN(14, 208, line, &Font18_UTF8, WHITE, BLACK);

    const char *ext = strrchr(filename, '.');
    if (ext != NULL && strcasecmp(ext, ".bmp") == 0) {
        FILE *fp = fopen(file_path, "rb");
        if (fp != NULL) {
            BMPFILEHEADER file_header = {0};
            BMPINFOHEADER info_header = {0};
            size_t r1 = fread(&file_header, 1, sizeof(file_header), fp);
            size_t r2 = fread(&info_header, 1, sizeof(info_header), fp);
            fclose(fp);

            if (r1 == sizeof(file_header) && r2 == sizeof(info_header)) {
                snprintf(line, sizeof(line), "分辨率: %lu x %lu",
                         (unsigned long)info_header.biWidth,
                         (unsigned long)info_header.biHeight);
                Paint_DrawString_CN(14, 252, line, &Font18_UTF8, WHITE, BLACK);

                snprintf(line, sizeof(line), "BMP位深: %u", info_header.biBitCount);
                Paint_DrawString_CN(14, 296, line, &Font18_UTF8, WHITE, BLACK);
            }
        }
    }

    // Paint_DrawLine(10, 718, 470, 718, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    // Paint_DrawString_CN(80, 760, "单击确认/双击确认: 返回", &Font16_UTF8, WHITE, BLACK);
    EPD_Display_Base(Image_Mono);

    wait_back_key_or_confirm();
}

static bool delete_picture_file(const char *filename)
{
    char file_path[320] = {0};
    make_picture_path(filename, file_path, sizeof(file_path));

    bool need_redraw = true;
    while (1) {
        if (need_redraw) {
            Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
            Paint_SetScale(2);
            Paint_SelectImage(Image_Mono);
            Paint_Clear(WHITE);

            draw_page_header(" 删除图片文件 ", NULL);
            Paint_DrawString_CN(14, 240, "确认删除以下文件吗？", &Font24_UTF8, WHITE, BLACK);

            char display_name[96] = {0};
            cFONT *name_font = &Font18_UTF8;
            prepare_filename_for_display(filename, display_name, sizeof(display_name), 420, &name_font);
            Paint_DrawString_CN(14, 294, display_name, name_font, WHITE, BLACK);
            Paint_DrawString_CN(14, 360, "单击确认: 删除文件", &Font18_UTF8, WHITE, BLACK);
            Paint_DrawString_CN(14, 398, "双击确认: 取消删除", &Font18_UTF8, WHITE, BLACK);

            EPD_Display_Base(Image_Mono);
            need_redraw = false;
        }

        int button = wait_key_event_and_return_code(pdMS_TO_TICKS(1000));
        if (button == 7) {
            int ret = unlink(file_path);
            if (ret == 0) {
                display_message_page(" 删除图片文件 ", "文件删除成功", NULL, true);
                vTaskDelay(pdMS_TO_TICKS(700));
                return true;
            }

            ESP_LOGE(TAG, "Delete failed: %s errno=%d (%s)", file_path, errno, strerror(errno));
            display_message_page(" 删除图片文件 ", "文件删除失败", strerror(errno), true);
            wait_back_key_or_confirm();
            return false;
        }
        if (button == 8 || button == 22) {
            return false;
        }
        if (button == 12) {
            EPD_Init();
            need_redraw = true;
        }
    }
}

static void upload_picture_file(const char *filename)
{
    char file_path[320] = {0};
    make_picture_path(filename, file_path, sizeof(file_path));

    struct stat file_stat = {0};
    if (stat(file_path, &file_stat) != 0 || !S_ISREG(file_stat.st_mode)) {
        display_message_page(" 上传图片 ", "无法访问图片文件", "请检查文件是否存在", true);
        wait_back_key_or_confirm();
        return;
    }

    Application::GetInstance().UploadAttachmentAndSendDialogueText(
        "请提取这张图片里的错题并整理到错题本",
        file_path,
        "image");

    display_message_page(" 上传图片 ", "已提交上传请求", "稍后查看服务端返回结果", true);
    wait_back_key_or_confirm();
}

static bool page_picture_file_management(const char *filename)
{
    int option_index = 0;
    bool need_redraw = true;
    bool full_refresh = true;

    while (1) {
        if (need_redraw) {
            display_picture_manage_page(filename, option_index, full_refresh);
            need_redraw = false;
            full_refresh = false;
        }

        int button = wait_key_event_and_return_code(pdMS_TO_TICKS(1000));
        if (button == 14) {
            option_index = (option_index + 1) % 5;
            need_redraw = true;
        } else if (button == 0) {
            option_index = (option_index + 4) % 5;
            need_redraw = true;
        } else if (button == 7) {
            if (option_index == 0) {
                view_picture_file(filename);
            } else if (option_index == 1) {
                show_picture_file_info(filename);
            } else if (option_index == 2) {
                upload_picture_file(filename);
            } else if (option_index == 3) {
                if (delete_picture_file(filename)) {
                    return true;
                }
            } else {
                return false;
            }
            EPD_Init();
            full_refresh = true;
            need_redraw = true;
        } else if (button == 8 || button == 22) {
            return false;
        } else if (button == 12) {
            EPD_Init();
            full_refresh = true;
            need_redraw = true;
        }
    }
}

void page_picture_show(void)
{
    if (ensure_picture_directory() != ESP_OK) {
        display_message_page(" 图片 ", "无法访问 /sdcard/picture", "请检查 SD 卡状态", true);
        wait_back_key_or_confirm();
        return;
    }

    file_entry_t *entries = (file_entry_t *)heap_caps_malloc(MAX_ENTRY_NUM * sizeof(file_entry_t),
                                                             MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (entries == NULL) {
        entries = (file_entry_t *)heap_caps_malloc(MAX_ENTRY_NUM * sizeof(file_entry_t), MALLOC_CAP_8BIT);
    }
    if (entries == NULL) {
        ESP_LOGE(TAG, "Picture entries allocation failed");
        display_message_page(" 图片 ", "内存不足，无法读取图片列表", NULL, true);
        wait_back_key_or_confirm();
        return;
    }

    int total_entries = load_picture_entries(entries, MAX_ENTRY_NUM);
    int selected_index = 0;

    Time_data rtc_time = page_picture_get_time();
    int last_minutes = rtc_time.minutes;
    bool need_redraw = true;
    bool full_refresh = true;

    while (1) {
        if (total_entries > 0 && selected_index >= total_entries) {
            selected_index = total_entries - 1;
        }
        if (total_entries <= 0) {
            selected_index = 0;
        }

        if (need_redraw) {
            display_picture_list(entries, total_entries, selected_index, full_refresh);
            need_redraw = false;
            full_refresh = false;
        }

        int button = wait_key_event_and_return_code(pdMS_TO_TICKS(1000));
        if (button == 14) {
            if (total_entries > 0) {
                selected_index = (selected_index + 1) % total_entries;
                need_redraw = true;
            }
        } else if (button == 0) {
            if (total_entries > 0) {
                selected_index = (selected_index - 1 + total_entries) % total_entries;
                need_redraw = true;
            }
        } else if (button == 7) {
            if (total_entries > 0) {
                view_picture_file(entries[selected_index].name);
                EPD_Init();
                full_refresh = true;
                need_redraw = true;
            }
        } else if (button == 12 || button == 23) {
            if (total_entries > 0) {
                bool deleted = page_picture_file_management(entries[selected_index].name);
                if (deleted) {
                    total_entries = load_picture_entries(entries, MAX_ENTRY_NUM);
                    if (selected_index >= total_entries) {
                        selected_index = (total_entries > 0) ? (total_entries - 1) : 0;
                    }
                }
                EPD_Init();
                full_refresh = true;
                need_redraw = true;
            }
        } else if (button == 8 || button == 22) {
            break;
        } else if (button == 21) {
            EPD_Init();
            full_refresh = true;
            need_redraw = true;
        }

        rtc_time = page_picture_get_time();
        if (rtc_time.minutes != last_minutes) {
            last_minutes = rtc_time.minutes;
            need_redraw = true;
        }
    }

    heap_caps_free(entries);
}
