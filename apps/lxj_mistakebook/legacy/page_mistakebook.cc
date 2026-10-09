#include "page_mistakebook.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "GUI_Paint.h"
#include "application.h"
#include "axp_prot.h"
#include "board.h"
#include "button_bsp.h"
#include "button_bsp_mcp.h"
#include "epaper_port.h"
#include "esp_log.h"
#include "cJSON.h"
#include "pcf85063_bsp.h"
#include "qmi8658_bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#if CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint
#include "boards/waveshare-s3-PhotoPainter/epaper_lvgl_display.h"
#include <font_awesome.h>
#include <lvgl.h>

LV_FONT_DECLARE(font_puhui_basic_16_4);
LV_FONT_DECLARE(font_puhui_16_4);
LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);
#endif

extern SemaphoreHandle_t rtc_mutex;
extern SemaphoreHandle_t qmi8658_mutex;
extern uint8_t *Image_Mono;
extern int home_selection;

namespace {

static const char* TAG = "mistakebook";
constexpr const char* kMistakeDir = "/sdcard/mistakebook";
constexpr const char* kMistakeFile = "/sdcard/mistakebook/cards.csv";
constexpr int kHeaderBottomY = 54;
constexpr int kContentTopY = 88;
constexpr int kCharsPerLine = 15;

struct MistakeItem {
    std::string subject;
    std::string title;
    std::string question_text;
    std::string student_answer;
    std::string correct_answer;
    std::string knowledge_point;
    std::string error_type;
    std::string explanation;
    bool review_pending = true;
};

std::vector<MistakeItem> g_items;
SemaphoreHandle_t g_mutex = nullptr;
bool g_loaded = false;

std::string SanitizeField(const char* value) {
    if (!value) {
        return "";
    }
    std::string output(value);
    for (char& ch : output) {
        if (ch == '\n' || ch == '\r' || ch == '|') {
            ch = ' ';
        }
    }
    return output;
}

void EnsureDir() {
    struct stat st;
    if (stat(kMistakeDir, &st) != 0) {
        mkdir(kMistakeDir, 0777);
    }
}

void LoadCardsLocked() {
    if (g_loaded) {
        return;
    }
    g_items.clear();
    EnsureDir();

    FILE* fp = fopen(kMistakeFile, "r");
    if (!fp) {
        g_loaded = true;
        return;
    }

    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        char* save_ptr = nullptr;
        char* token = strtok_r(line, "|", &save_ptr);
        if (!token) {
            continue;
        }

        MistakeItem item;
        item.subject = token;
        token = strtok_r(nullptr, "|", &save_ptr);
        item.title = token ? token : "";
        token = strtok_r(nullptr, "|", &save_ptr);
        std::string third = token ? token : "";
        token = strtok_r(nullptr, "|", &save_ptr);
        std::string fourth = token ? token : "";
        token = strtok_r(nullptr, "|", &save_ptr);
        std::string fifth = token ? token : "";
        token = strtok_r(nullptr, "|", &save_ptr);

        if (!token) {
            item.knowledge_point = third;
            item.error_type = fourth;
            item.review_pending = fifth.empty() ? true : atoi(fifth.c_str()) != 0;
        } else {
            item.question_text = third;
            item.student_answer = fourth;
            item.correct_answer = fifth;
            item.knowledge_point = token;
            token = strtok_r(nullptr, "|", &save_ptr);
            item.error_type = token ? token : "";
            token = strtok_r(nullptr, "|", &save_ptr);
            item.explanation = token ? token : "";
            token = strtok_r(nullptr, "|", &save_ptr);
            item.review_pending = token ? atoi(token) != 0 : true;
        }
        g_items.push_back(item);
    }
    fclose(fp);
    g_loaded = true;
}

void SaveCardsLocked() {
    EnsureDir();
    FILE* fp = fopen(kMistakeFile, "w");
    if (!fp) {
        ESP_LOGE(TAG, "Failed to open mistakebook csv for writing");
        return;
    }

    for (const auto& item : g_items) {
        fprintf(fp, "%s|%s|%s|%s|%s|%s|%s|%s|%d\n",
                item.subject.c_str(),
                item.title.c_str(),
                item.question_text.c_str(),
                item.student_answer.c_str(),
                item.correct_answer.c_str(),
                item.knowledge_point.c_str(),
                item.error_type.c_str(),
                item.explanation.c_str(),
                item.review_pending ? 1 : 0);
    }
    fclose(fp);
}

void EnsureInitialized() {
    if (!g_mutex) {
        g_mutex = xSemaphoreCreateMutex();
    }
    if (g_mutex) {
        xSemaphoreTake(g_mutex, portMAX_DELAY);
    }
    LoadCardsLocked();
    if (g_mutex) {
        xSemaphoreGive(g_mutex);
    }
}

#if !CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint
std::vector<std::string> WrapUtf8Lines(const std::string& text, size_t max_chars_per_line) {
    std::vector<std::string> lines;
    std::string current_line;
    size_t current_chars = 0;

    for (size_t i = 0; i < text.size();) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        size_t char_len = 1;
        if (c < 0x80) {
            char_len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            char_len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            char_len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            char_len = 4;
        }

        std::string ch = text.substr(i, char_len);
        i += char_len;
        if (ch == "\n") {
            if (!current_line.empty()) {
                lines.push_back(current_line);
                current_line.clear();
                current_chars = 0;
            }
            continue;
        }

        current_line += ch;
        ++current_chars;
        if (current_chars >= max_chars_per_line) {
            lines.push_back(current_line);
            current_line.clear();
            current_chars = 0;
        }
    }

    if (!current_line.empty()) {
        lines.push_back(current_line);
    }
    if (lines.empty()) {
        lines.push_back("");
    }
    return lines;
}
#endif

#if CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint
lv_obj_t *LvLabel(lv_obj_t *parent, const char *text, const lv_font_t *font) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text == nullptr ? "" : text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    return label;
}

void LvAlignStatusLabel(lv_obj_t *label, int x, int width, const lv_font_t *font, lv_text_align_t align) {
    constexpr int kStatusRowY = 7;
    constexpr int kStatusRowHeight = 34;
    const int line_height = font != nullptr ? font->line_height : kStatusRowHeight;
    lv_obj_set_size(label, width, line_height);
    lv_obj_set_pos(label, x, kStatusRowY + (kStatusRowHeight - line_height) / 2);
    lv_obj_set_style_text_align(label, align, 0);
}

const char *LvDialogueIcon() {
    auto &app = Application::GetInstance();
    if (app.IsUiMuted()) {
        return FONT_AWESOME_VOLUME_XMARK;
    }

    switch (app.GetDeviceState()) {
        case kDeviceStateListening:
        case kDeviceStateSpeaking:
            return FONT_AWESOME_MICROPHONE;
        case kDeviceStateConnecting:
            return FONT_AWESOME_ARROWS_ROTATE;
        case kDeviceStateIdle:
        default:
            return FONT_AWESOME_COMMENT;
    }
}

void BuildLvglStatusBar(lv_obj_t *screen, int width) {
    Time_data rtc_time = {0};
    if (rtc_mutex != nullptr) {
        xSemaphoreTake(rtc_mutex, portMAX_DELAY);
        rtc_time = PCF85063_GetTime();
        xSemaphoreGive(rtc_mutex);
    } else {
        rtc_time = PCF85063_GetTime();
    }

    char time_str[16] = {0};
    snprintf(time_str, sizeof(time_str), "%02d:%02d", rtc_time.hours, rtc_time.minutes);
    lv_obj_t *time_label = LvLabel(screen, time_str, &font_puhui_20_4);
    LvAlignStatusLabel(time_label, 20, 80, &font_puhui_20_4, LV_TEXT_ALIGN_LEFT);

    lv_obj_t *dialogue_label = LvLabel(screen, LvDialogueIcon(), &font_awesome_20_4);
    LvAlignStatusLabel(dialogue_label, width - 188, 32, &font_awesome_20_4, LV_TEXT_ALIGN_CENTER);

    lv_obj_t *wifi_label = LvLabel(screen, Board::GetInstance().GetNetworkStateIcon(), &font_awesome_20_4);
    LvAlignStatusLabel(wifi_label, width - 154, 32, &font_awesome_20_4, LV_TEXT_ALIGN_CENTER);

    int battery = get_battery_power();
    if (battery < 0) {
        battery = 20;
    } else if (battery > 100) {
        battery = 100;
    }

    lv_obj_t *battery_box = lv_obj_create(screen);
    lv_obj_set_size(battery_box, 28, 14);
    lv_obj_set_pos(battery_box, width - 110, 17);
    lv_obj_set_style_radius(battery_box, 0, 0);
    lv_obj_set_style_border_width(battery_box, 1, 0);
    lv_obj_set_style_border_color(battery_box, lv_color_black(), 0);
    lv_obj_set_style_bg_color(battery_box, lv_color_white(), 0);
    lv_obj_set_style_pad_all(battery_box, 0, 0);
    lv_obj_clear_flag(battery_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *battery_fill = lv_obj_create(battery_box);
    lv_obj_set_size(battery_fill, (battery * 24) / 100, 10);
    lv_obj_set_pos(battery_fill, 1, 1);
    lv_obj_set_style_radius(battery_fill, 0, 0);
    lv_obj_set_style_border_width(battery_fill, 0, 0);
    lv_obj_set_style_bg_color(battery_fill, lv_color_black(), 0);
    lv_obj_clear_flag(battery_fill, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *battery_cap = lv_obj_create(screen);
    lv_obj_set_size(battery_cap, 3, 6);
    lv_obj_set_pos(battery_cap, width - 82, 21);
    lv_obj_set_style_radius(battery_cap, 0, 0);
    lv_obj_set_style_border_width(battery_cap, 0, 0);
    lv_obj_set_style_bg_color(battery_cap, lv_color_black(), 0);
    lv_obj_clear_flag(battery_cap, LV_OBJ_FLAG_SCROLLABLE);

    char battery_str[16] = {0};
    snprintf(battery_str, sizeof(battery_str), "%d%%", battery);
    lv_obj_t *battery_label = LvLabel(screen, battery_str, &font_puhui_20_4);
    LvAlignStatusLabel(battery_label, width - 69, 60, &font_puhui_20_4, LV_TEXT_ALIGN_LEFT);

    lv_obj_t *line = lv_obj_create(screen);
    lv_obj_set_size(line, width - 4, 2);
    lv_obj_set_pos(line, 2, kHeaderBottomY);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_bg_color(line, lv_color_black(), 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
}

std::vector<std::string> WrapUtf8LinesByPixelWidth(const std::string& text, const lv_font_t *font, int max_width) {
    std::vector<std::string> lines;
    std::string current_line;

    for (size_t i = 0; i < text.size();) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        size_t char_len = 1;
        if (c < 0x80) {
            char_len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            char_len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            char_len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            char_len = 4;
        }
        if (i + char_len > text.size()) {
            char_len = 1;
        }

        std::string ch = text.substr(i, char_len);
        i += char_len;
        if (ch == "\n") {
            if (!current_line.empty()) {
                lines.push_back(current_line);
                current_line.clear();
            }
            continue;
        }

        std::string candidate = current_line + ch;
        int32_t candidate_width = lv_text_get_width(candidate.c_str(), candidate.size(), font, 0);
        if (!current_line.empty() && candidate_width > max_width) {
            lines.push_back(current_line);
            current_line = ch;
        } else {
            current_line = candidate;
        }
    }

    if (!current_line.empty()) {
        lines.push_back(current_line);
    }
    if (lines.empty()) {
        lines.push_back("");
    }
    return lines;
}

void AddLvglText(lv_obj_t *screen, int x, int y, int width, const std::string& text, const lv_font_t *font) {
    lv_obj_t *label = LvLabel(screen, text.c_str(), font);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(label, width, font->line_height);
    lv_obj_set_pos(label, x, y);
}

void AddLvglBoldText(lv_obj_t *screen, int x, int y, int width, const std::string& text, const lv_font_t *font) {
    AddLvglText(screen, x, y, width, text, font);
    AddLvglText(screen, x + 1, y, width, text, font);
}

int AddLvglSection(lv_obj_t *screen, const char *label, const std::string& content, int y, int width, int bottom) {
    if (y + font_puhui_20_4.line_height > bottom) {
        return y;
    }
    AddLvglBoldText(screen, 20, y, width - 40, label, &font_puhui_20_4);
    y += font_puhui_20_4.line_height + 8;

    const std::string value = content.empty() ? "暂无" : content;
    const auto lines = WrapUtf8LinesByPixelWidth(value, &font_puhui_20_4, width - 56);
    for (const auto& line : lines) {
        if (y + font_puhui_20_4.line_height > bottom) {
            return y;
        }
        AddLvglText(screen, 28, y, width - 56, line, &font_puhui_20_4);
        y += font_puhui_20_4.line_height + 5;
    }
    return y + 12;
}

int ReadMistakebookRotation() {
    float acc[3] = {0};
    float gyro[3] = {0};
    static int last_rotation = 270;
    static int last_candidate = 270;
    static int stable_samples = 0;
    constexpr float kSwitchThreshold = 400.0f;
    constexpr int kStableSamples = 2;

    if (qmi8658_mutex != nullptr) {
        xSemaphoreTake(qmi8658_mutex, portMAX_DELAY);
        QMI8658_read_xyz(acc, gyro, nullptr);
        xSemaphoreGive(qmi8658_mutex);
    } else {
        QMI8658_read_xyz(acc, gyro, nullptr);
    }

    const float ax = fabsf(acc[0]);
    const float ay = fabsf(acc[1]);
    int candidate = last_rotation;
    if (ax > ay + kSwitchThreshold) {
        candidate = acc[0] >= 0 ? 270 : 90;
    } else if (ay > ax + kSwitchThreshold) {
        candidate = acc[1] >= 0 ? 0 : 180;
    } else {
        stable_samples = 0;
        last_candidate = candidate;
        return last_rotation;
    }

    if (candidate == last_candidate) {
        ++stable_samples;
    } else {
        last_candidate = candidate;
        stable_samples = 1;
    }

    if (stable_samples >= kStableSamples) {
        last_rotation = candidate;
        stable_samples = 0;
    }
    return last_rotation;
}

void RenderPageLvgl(const std::vector<MistakeItem>& items, int current_index, int rotation, int refresh_mode) {
    epaper_lvgl_display_set_rotation(rotation);
    lv_obj_t *screen = epaper_lvgl_display_create_screen();
    if (screen == nullptr) {
        return;
    }

    const int width = epaper_lvgl_display_get_width();
    const int height = epaper_lvgl_display_get_height();
    const int footer_y = height - 36;
    BuildLvglStatusBar(screen, width);

    lv_obj_t *title = LvLabel(screen, "错题本", &font_puhui_20_4);
    lv_obj_set_size(title, 120, font_puhui_20_4.line_height);
    lv_obj_set_pos(title, 20, 66);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, 0);

    char page_str[24] = {0};
    snprintf(page_str, sizeof(page_str), "%d/%d", items.empty() ? 0 : current_index + 1, static_cast<int>(items.size()));
    lv_obj_t *page_label = LvLabel(screen, page_str, &font_puhui_20_4);
    lv_obj_set_size(page_label, 90, font_puhui_20_4.line_height);
    lv_obj_set_pos(page_label, width - 110, 66);
    lv_obj_set_style_text_align(page_label, LV_TEXT_ALIGN_RIGHT, 0);

    if (items.empty()) {
        lv_obj_t *empty_title = LvLabel(screen, "还没有错题卡片", &font_puhui_20_4);
        lv_obj_set_size(empty_title, width - 40, font_puhui_20_4.line_height);
        lv_obj_set_pos(empty_title, 20, 180);
        lv_obj_set_style_text_align(empty_title, LV_TEXT_ALIGN_CENTER, 0);

        lv_obj_t *empty_hint = LvLabel(screen, "可由服务端同步或通过 MCP 添加到本地缓存", &font_puhui_16_4);
        lv_obj_set_size(empty_hint, width - 40, font_puhui_16_4.line_height);
        lv_obj_set_pos(empty_hint, 20, 230);
        lv_obj_set_style_text_align(empty_hint, LV_TEXT_ALIGN_CENTER, 0);
    } else {
        current_index = std::max(0, std::min(current_index, static_cast<int>(items.size()) - 1));
        const MistakeItem& item = items[current_index];
        int y = 108;

        AddLvglText(screen, 20, y, width - 40, item.subject.empty() ? "未分类" : item.subject, &font_puhui_20_4);
        lv_obj_t *state = LvLabel(screen, item.review_pending ? "待复习" : "已复习", &font_puhui_20_4);
        lv_obj_set_size(state, 80, font_puhui_20_4.line_height);
        lv_obj_set_pos(state, width - 120, y + 2);
        lv_obj_set_style_text_align(state, LV_TEXT_ALIGN_RIGHT, 0);
        y += 44;

        y = AddLvglSection(screen, "1.题目", item.title, y, width, footer_y - 8);
        y = AddLvglSection(screen, "2.题干", item.question_text, y, width, footer_y - 8);
        y = AddLvglSection(screen, "3.学生答案", item.student_answer, y, width, footer_y - 8);
        y = AddLvglSection(screen, "4.正确答案", item.correct_answer, y, width, footer_y - 8);
        y = AddLvglSection(screen, "5.知识点", item.knowledge_point, y, width, footer_y - 8);
        y = AddLvglSection(screen, "6.错因", item.error_type, y, width, footer_y - 8);
        AddLvglSection(screen, "7.讲解", item.explanation, y, width, footer_y - 8);
    }

    lv_obj_t *footer = LvLabel(screen, "上下切换  双击功能键/Boot返回主页", &font_puhui_16_4);
    lv_obj_set_size(footer, width - 40, font_puhui_16_4.line_height);
    lv_obj_set_pos(footer, 20, footer_y);
    lv_obj_set_style_text_align(footer, LV_TEXT_ALIGN_CENTER, 0);

    epaper_lvgl_display_present(refresh_mode);
}
#endif

#if !CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint
void DrawHeader(int current_index, int total_count) {
    Time_data rtc_time = {0};
    if (rtc_mutex != nullptr) {
        xSemaphoreTake(rtc_mutex, portMAX_DELAY);
        rtc_time = PCF85063_GetTime();
        xSemaphoreGive(rtc_mutex);
    } else {
        rtc_time = PCF85063_GetTime();
    }

    char time_str[16] = {0};
    snprintf(time_str, sizeof(time_str), "%02d:%02d", rtc_time.hours, rtc_time.minutes);
    Paint_DrawString_EN(20, 11, time_str, &Font16, WHITE, BLACK);
    Paint_DrawString_CN(170, 12, "错题本", &Font16_UTF8, WHITE, BLACK);

    char page_str[24] = {0};
    snprintf(page_str, sizeof(page_str), "%d/%d", total_count == 0 ? 0 : (current_index + 1), total_count);
    Paint_DrawString_EN(390, 11, page_str, &Font16, WHITE, BLACK);
    Paint_DrawLine(2, kHeaderBottomY, EPD_HEIGHT - 2, kHeaderBottomY, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
}

void DrawItemCard(const MistakeItem& item) {
    Paint_DrawString_CN(20, kContentTopY, item.subject.c_str(), &Font24_UTF8, WHITE, BLACK);
    Paint_DrawString_CN(20, kContentTopY + 44, item.review_pending ? "待复习" : "已复习", &Font16_UTF8, WHITE, BLACK);

    int text_y = kContentTopY + 92;
    auto draw_section = [&](const char* label, const std::string& content) {
        Paint_DrawString_CN(20, text_y, label, &Font16_UTF8, WHITE, BLACK);
        text_y += 28;
        for (const auto& line : WrapUtf8Lines(content.empty() ? "暂无" : content, kCharsPerLine)) {
            Paint_DrawString_CN(28, text_y, line.c_str(), &Font16_UTF8, WHITE, BLACK);
            text_y += 24;
            if (text_y > 690) {
                break;
            }
        }
        text_y += 12;
    };

    draw_section("题目", item.title);
    if (text_y <= 690) {
        draw_section("题干", item.question_text);
    }
    if (text_y <= 690) {
        draw_section("学生答案", item.student_answer);
    }
    if (text_y <= 690) {
        draw_section("正确答案", item.correct_answer);
    }
    if (text_y <= 690) {
        draw_section("知识点", item.knowledge_point);
    }
    if (text_y <= 690) {
        draw_section("错因", item.error_type);
    }
    if (text_y <= 690) {
        draw_section("讲解", item.explanation);
    }

    Paint_DrawString_CN(70, 730, "上下切换  双击功能键返回主页", &Font16_UTF8, WHITE, BLACK);
}
#endif

void RenderPage(int current_index, bool partial_refresh, int rotation) {
    std::vector<MistakeItem> items;
    if (g_mutex) {
        xSemaphoreTake(g_mutex, portMAX_DELAY);
    }
    items = g_items;
    if (g_mutex) {
        xSemaphoreGive(g_mutex);
    }

#if CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint
    RenderPageLvgl(items, current_index, rotation, partial_refresh ? Partial_refresh : Global_refresh);
    return;
#else
    (void)rotation;
    Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, 0, WHITE);
    Paint_SelectImage(Image_Mono);
    Paint_Clear(WHITE);

    DrawHeader(current_index, static_cast<int>(items.size()));

    if (items.empty()) {
        Paint_DrawString_CN(130, 210, "还没有错题卡片", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(52, 280, "可由服务端同步或通过 MCP 添加到本地缓存", &Font16_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(76, 730, "双击功能键返回主页", &Font16_UTF8, WHITE, BLACK);
    } else {
        current_index = std::max(0, std::min(current_index, static_cast<int>(items.size()) - 1));
        DrawItemCard(items[current_index]);
    }

    if (partial_refresh) {
        EPD_Display_Partial(Image_Mono, 0, 0, EPD_WIDTH, EPD_HEIGHT);
    } else {
        EPD_Display_Base(Image_Mono);
    }
#endif
}

}  // namespace

extern "C" bool mistakebook_add_item_from_mcp(const char* subject,
                                               const char* title,
                                               const char* question_text,
                                               const char* student_answer,
                                               const char* correct_answer,
                                               const char* knowledge_point,
                                               const char* error_type,
                                               const char* explanation,
                                               int review_pending) {
    EnsureInitialized();
    if (g_mutex) {
        xSemaphoreTake(g_mutex, portMAX_DELAY);
    }
    g_items.push_back({
        SanitizeField(subject),
        SanitizeField(title),
        SanitizeField(question_text),
        SanitizeField(student_answer),
        SanitizeField(correct_answer),
        SanitizeField(knowledge_point),
        SanitizeField(error_type),
        SanitizeField(explanation),
        review_pending != 0,
    });
    SaveCardsLocked();
    if (g_mutex) {
        xSemaphoreGive(g_mutex);
    }
    return true;
}

extern "C" void mistakebook_open_from_mcp(void) {
    home_selection = 10;
    button_mcp_set_event_code(BUTTON_BSP_ID_FUNCTION, BUTTON_BSP_EVENT_SINGLE_CLICK);
}

std::string mistakebook_get_items_json(bool review_pending_only) {
    EnsureInitialized();

    cJSON* root = cJSON_CreateArray();
    if (g_mutex) {
        xSemaphoreTake(g_mutex, portMAX_DELAY);
    }

    for (const auto& item : g_items) {
        if (review_pending_only && !item.review_pending) {
            continue;
        }

        cJSON* entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "subject", item.subject.c_str());
        cJSON_AddStringToObject(entry, "title", item.title.c_str());
        cJSON_AddStringToObject(entry, "question_text", item.question_text.c_str());
        cJSON_AddStringToObject(entry, "student_answer", item.student_answer.c_str());
        cJSON_AddStringToObject(entry, "correct_answer", item.correct_answer.c_str());
        cJSON_AddStringToObject(entry, "knowledge_point", item.knowledge_point.c_str());
        cJSON_AddStringToObject(entry, "error_type", item.error_type.c_str());
        cJSON_AddStringToObject(entry, "explanation", item.explanation.c_str());
        cJSON_AddBoolToObject(entry, "review_pending", item.review_pending);
        cJSON_AddItemToArray(root, entry);
    }

    if (g_mutex) {
        xSemaphoreGive(g_mutex);
    }

    char* json = cJSON_PrintUnformatted(root);
    std::string result = json ? json : "[]";
    if (json) {
        cJSON_free(json);
    }
    cJSON_Delete(root);
    return result;
}

extern "C" void page_mistakebook_show(void) {
    EnsureInitialized();

    int current_index = 0;
    if (g_mutex) {
        xSemaphoreTake(g_mutex, portMAX_DELAY);
    }
    if (!g_items.empty()) {
        current_index = static_cast<int>(g_items.size()) - 1;
    }
    if (g_mutex) {
        xSemaphoreGive(g_mutex);
    }

    EPD_Init();
#if CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint
    int current_rotation = ReadMistakebookRotation();
#else
    int current_rotation = 0;
#endif
    RenderPage(current_index, false, current_rotation);

    while (true) {
        int button = wait_key_event_and_return_code(pdMS_TO_TICKS(1000));
        bool needs_render = false;

        int total = 0;
        if (g_mutex) {
            xSemaphoreTake(g_mutex, portMAX_DELAY);
        }
        total = static_cast<int>(g_items.size());
        if (g_mutex) {
            xSemaphoreGive(g_mutex);
        }

        if (button == 0 && total > 0) {
            int new_index = std::max(0, current_index - 1);
            if (new_index != current_index) {
                current_index = new_index;
                needs_render = true;
            }
        } else if (button == 14 && total > 0) {
            int new_index = std::min(total - 1, current_index + 1);
            if (new_index != current_index) {
                current_index = new_index;
                needs_render = true;
            }
        } else if (button == 8 || button == 22 || button == 23) {
            return;
        } else if (button == 12) {
            EPD_Init();
            needs_render = true;
        }

#if CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint
        const int latest_rotation = ReadMistakebookRotation();
        if (latest_rotation != current_rotation) {
            current_rotation = latest_rotation;
            EPD_Init();
            needs_render = true;
        }
#endif

        if (needs_render) {
            RenderPage(current_index, true, current_rotation);
        }
    }
}
