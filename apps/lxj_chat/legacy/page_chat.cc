#include "page_chat.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "GUI_Paint.h"
#include "application.h"
#include "audio_codec.h"
#include "board.h"
#include "button_bsp.h"
#include "chat_history.h"
#include "epaper_port.h"
#include "file_browser.h"
#include "pcf85063_bsp.h"
#include "status_bar.h"
#include "axp_prot.h"

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
extern bool wifi_enable;
extern uint8_t *Image_Mono;

namespace {

std::atomic<bool> g_chat_page_active{false};
std::atomic<bool> g_chat_page_redraw_requested{false};

struct ChatPageActiveGuard {
    ChatPageActiveGuard() {
        g_chat_page_active.store(true, std::memory_order_release);
    }

    ~ChatPageActiveGuard() {
        g_chat_page_active.store(false, std::memory_order_release);
        g_chat_page_redraw_requested.store(false, std::memory_order_release);
    }
};

constexpr int kHeaderBottomY = 54;
constexpr int kContentTopY = 84;
constexpr int kContentBottomY = 780;
constexpr int kBubblePaddingX = 16;
constexpr int kBubblePaddingY = 8;
constexpr int kBubbleGapY = 16;
constexpr int kBubbleLeftX1 = 12;
constexpr int kBubbleRightX1 = 232;
constexpr int kBubbleRightLabelInset = 28;
constexpr int kRoleLabelOffsetY = 28;
constexpr size_t kCharsPerLine = 14;
constexpr int kDialogueStatusX = 292;
constexpr int kDialogueStatusY = 10;

#if CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint
constexpr int kScreenWidth = 480;
constexpr int kStatusRowY = 7;
constexpr int kStatusRowHeight = 34;
constexpr int kLvglTextWidth = 220;
constexpr int kLvglRoleLabelWidth = 64;
#endif

struct DisplayMessage {
    std::string role;
    std::string content;
};

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

#if CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint
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
        const int32_t candidate_width = lv_text_get_width(candidate.c_str(), candidate.size(), font, 0);
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
#endif

void DrawStatusIconClock(int x, int y) {
    Paint_DrawCircle(x + 10, y + 10, 9, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
    Paint_DrawLine(x + 10, y + 10, x + 10, y + 5, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 10, y + 10, x + 14, y + 12, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
}

void DrawStatusIconListening(int x, int y) {
    Paint_DrawRectangle(x + 6, y + 3, x + 14, y + 13, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
    Paint_DrawLine(x + 10, y + 13, x + 10, y + 18, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 6, y + 18, x + 14, y + 18, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 4, y + 8, x + 4, y + 12, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 16, y + 8, x + 16, y + 12, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
}

void DrawStatusIconSpeaking(int x, int y) {
    Paint_DrawRectangle(x + 2, y + 4, x + 17, y + 14, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
    Paint_DrawLine(x + 7, y + 14, x + 5, y + 18, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 11, y + 14, x + 9, y + 18, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawPoint(x + 6, y + 9, BLACK, DOT_PIXEL_1X1, DOT_FILL_AROUND);
    Paint_DrawPoint(x + 10, y + 9, BLACK, DOT_PIXEL_1X1, DOT_FILL_AROUND);
    Paint_DrawPoint(x + 14, y + 9, BLACK, DOT_PIXEL_1X1, DOT_FILL_AROUND);
}

void DrawStatusIconMuted(int x, int y) {
    Paint_DrawRectangle(x + 2, y + 7, x + 6, y + 13, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawLine(x + 6, y + 7, x + 11, y + 4, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 6, y + 13, x + 11, y + 16, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 11, y + 4, x + 11, y + 16, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 14, y + 5, x + 19, y + 15, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(x + 19, y + 5, x + 14, y + 15, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
}

void DrawDialogueStatusIcon(int x, int y) {
    auto& app = Application::GetInstance();
    auto codec = Board::GetInstance().GetAudioCodec();
    const bool muted = app.IsUiMuted();

    Paint_ClearWindows(x, y, x + 24, y + 22, WHITE);

    if (muted) {
        DrawStatusIconMuted(x, y);
        return;
    }

    switch (app.GetDeviceState()) {
        case kDeviceStateListening:
            DrawStatusIconListening(x, y);
            break;
        case kDeviceStateSpeaking:
            DrawStatusIconSpeaking(x, y);
            break;
        case kDeviceStateIdle:
        default:
            DrawStatusIconClock(x, y);
            break;
    }
}

void DrawTopBar() {
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
    DrawDialogueStatusIcon(kDialogueStatusX, kDialogueStatusY);

#if defined(CONFIG_IMG_SOURCE_EMBEDDED)
    DrawWifiStatusIcon(326, 8);
    Paint_ReadBmp(gImage_BAT, 370, 17, 32, 16);
#elif defined(CONFIG_IMG_SOURCE_TFCARD)
    DrawWifiStatusIcon(326, 8);
    GUI_ReadBmp(BMP_BAT_PATH, 370, 17);
#endif

    int battery_power = get_battery_power();
    char battery_str[16] = {0};
    snprintf(battery_str, sizeof(battery_str), "%d%%", battery_power);
    if (battery_power == -1) {
        battery_power = 20;
    } else {
        battery_power = battery_power * 20 / 100;
    }

    Paint_DrawString_EN(411, 11, battery_str, &Font16, WHITE, BLACK);
    Paint_DrawRectangle(375, 22, 395, 30, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawRectangle(375, 22, 375 + battery_power, 30, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawLine(2, kHeaderBottomY, EPD_HEIGHT - 2, kHeaderBottomY, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);

    Paint_DrawString_CN(172, 12, "对话记录", &Font16_UTF8, WHITE, BLACK);
}

std::vector<DisplayMessage> BuildDisplayMessages(const std::vector<ChatHistoryMessage>& messages) {
    std::vector<DisplayMessage> display_messages;
    display_messages.reserve(messages.size());

    for (const auto& message : messages) {
        if (message.content.empty()) {
            continue;
        }

        if (!display_messages.empty() &&
            message.role == "assistant" &&
            display_messages.back().role == "assistant") {
            display_messages.back().content += "\n";
            display_messages.back().content += message.content;
            continue;
        }

        display_messages.push_back({message.role, message.content});
    }

    return display_messages;
}

int EstimateMessageHeight(const DisplayMessage& message) {
#if CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint
    const std::vector<std::string> lines = WrapUtf8LinesByPixelWidth(message.content, &font_puhui_20_4, kLvglTextWidth);
    const int line_step_y = font_puhui_20_4.line_height + 6;
#else
    const std::vector<std::string> lines = WrapUtf8Lines(message.content, kCharsPerLine);
    const int line_step_y = Font16_UTF8.Height + 6;
#endif
    return kBubblePaddingY * 2 + static_cast<int>(lines.size()) * line_step_y + kBubbleGapY;
}

int DrawMessageBubble(const DisplayMessage& message, int y) {
    const bool is_user = (message.role == "user");
    const int x1 = is_user ? kBubbleRightX1 : kBubbleLeftX1;
    const std::vector<std::string> lines = WrapUtf8Lines(message.content, kCharsPerLine);
    const int line_step_y = Font16_UTF8.Height + 6;
    const int bubble_height = kBubblePaddingY * 2 + static_cast<int>(lines.size()) * line_step_y;
    const int bubble_bottom = y + bubble_height;

    Paint_DrawString_CN(is_user ? (EPD_HEIGHT - kRoleLabelOffsetY - kBubbleRightLabelInset) : x1,
                        y - kRoleLabelOffsetY,
                        is_user ? "我" : "小智", &Font16_UTF8, WHITE, BLACK);

    int text_y = y + kBubblePaddingY;
    for (const auto& line : lines) {
        Paint_DrawString_CN(x1 + kBubblePaddingX, text_y, line.c_str(), &Font16_UTF8, WHITE, BLACK);
        text_y += line_step_y;
    }
    return bubble_bottom + kBubbleGapY;
}

int CountVisibleMessages(const std::vector<DisplayMessage>& messages, int start_index) {
    if (messages.empty()) {
        return 0;
    }

    start_index = std::max(0, std::min(start_index, static_cast<int>(messages.size()) - 1));
    int y = kContentTopY;
    int drawn_count = 0;
    for (int i = start_index; i < static_cast<int>(messages.size()); ++i) {
        const int estimated_height = EstimateMessageHeight(messages[i]);
        if (drawn_count > 0 && y + estimated_height > kContentBottomY) {
            break;
        }
        y += estimated_height;
        ++drawn_count;
    }
    return drawn_count;
}

int FindLatestStartIndex(const std::vector<DisplayMessage>& messages) {
    if (messages.empty()) {
        return 0;
    }

    int y = kContentBottomY;
    int start_index = static_cast<int>(messages.size()) - 1;
    for (int i = static_cast<int>(messages.size()) - 1; i >= 0; --i) {
        const int estimated_height = EstimateMessageHeight(messages[i]);
        if (y - estimated_height < kContentTopY) {
            break;
        }
        y -= estimated_height;
        start_index = i;
    }
    return start_index;
}

uint32_t ComputeMessagesVersion(const std::vector<ChatHistoryMessage>& messages) {
    uint32_t hash = 2166136261u;
    const std::hash<std::string> hasher;
    for (const auto& message : messages) {
        hash ^= static_cast<uint32_t>(hasher(message.role));
        hash *= 16777619u;
        hash ^= static_cast<uint32_t>(hasher(message.content));
        hash *= 16777619u;
    }
    hash ^= static_cast<uint32_t>(messages.size());
    hash *= 16777619u;
    return hash;
}

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

void BuildLvglTopBar(lv_obj_t *screen) {
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

    lv_obj_t *title = LvLabel(screen, "对话记录", &font_puhui_20_4);
    lv_obj_set_size(title, 120, font_puhui_20_4.line_height);
    lv_obj_set_pos(title, 172, 12);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *dialogue_label = LvLabel(screen, LvDialogueIcon(), &font_awesome_20_4);
    LvAlignStatusLabel(dialogue_label, kDialogueStatusX, 32, &font_awesome_20_4, LV_TEXT_ALIGN_CENTER);

    lv_obj_t *wifi_label = LvLabel(screen, Board::GetInstance().GetNetworkStateIcon(), &font_awesome_20_4);
    LvAlignStatusLabel(wifi_label, 326, 32, &font_awesome_20_4, LV_TEXT_ALIGN_CENTER);

    int battery = get_battery_power();
    if (battery < 0) {
        battery = 20;
    } else if (battery > 100) {
        battery = 100;
    }

    lv_obj_t *battery_box = lv_obj_create(screen);
    lv_obj_set_size(battery_box, 28, 14);
    lv_obj_set_pos(battery_box, 370, kStatusRowY + (kStatusRowHeight - 14) / 2);
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
    lv_obj_set_pos(battery_cap, 398, kStatusRowY + (kStatusRowHeight - 6) / 2);
    lv_obj_set_style_radius(battery_cap, 0, 0);
    lv_obj_set_style_border_width(battery_cap, 0, 0);
    lv_obj_set_style_bg_color(battery_cap, lv_color_black(), 0);
    lv_obj_clear_flag(battery_cap, LV_OBJ_FLAG_SCROLLABLE);

    char battery_str[16] = {0};
    snprintf(battery_str, sizeof(battery_str), "%d%%", battery);
    lv_obj_t *battery_label = LvLabel(screen, battery_str, &font_puhui_20_4);
    LvAlignStatusLabel(battery_label, 411, 60, &font_puhui_20_4, LV_TEXT_ALIGN_LEFT);

    lv_obj_t *line = lv_obj_create(screen);
    lv_obj_set_size(line, kScreenWidth - 4, 2);
    lv_obj_set_pos(line, 2, kHeaderBottomY);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_bg_color(line, lv_color_black(), 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
}

void BuildLvglMessage(lv_obj_t *screen, const DisplayMessage& message, int y) {
    const bool is_user = (message.role == "user");
    const int x1 = is_user ? kBubbleRightX1 : kBubbleLeftX1;
    const std::vector<std::string> lines = WrapUtf8LinesByPixelWidth(message.content, &font_puhui_20_4, kLvglTextWidth);
    const int line_step_y = font_puhui_20_4.line_height + 6;

    const int role_x = is_user ? (kScreenWidth - kBubbleRightLabelInset - kLvglRoleLabelWidth) : x1;
    lv_obj_t *role = LvLabel(screen, is_user ? "我" : "小智", &font_puhui_20_4);
    lv_obj_set_size(role, kLvglRoleLabelWidth, font_puhui_20_4.line_height);
    lv_obj_set_pos(role, role_x,
                   y - kRoleLabelOffsetY);
    lv_obj_set_style_text_align(role, is_user ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);

    int text_y = y + kBubblePaddingY;
    for (const auto& line : lines) {
        if (text_y + font_puhui_20_4.line_height > kContentBottomY) {
            break;
        }
        lv_obj_t *text = LvLabel(screen, line.c_str(), &font_puhui_20_4);
        lv_label_set_long_mode(text, LV_LABEL_LONG_CLIP);
        lv_obj_set_size(text, kLvglTextWidth, font_puhui_20_4.line_height);
        lv_obj_set_pos(text, x1 + kBubblePaddingX, text_y);
        lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_LEFT, 0);
        text_y += line_step_y;
    }
}

void RenderChatPageLvgl(const std::vector<ChatHistoryMessage>& messages, int start_index, bool use_partial_refresh) {
    const std::vector<DisplayMessage> display_messages = BuildDisplayMessages(messages);
    epaper_lvgl_display_set_rotation(270);
    lv_obj_t *screen = epaper_lvgl_display_create_screen();
    if (screen == nullptr) {
        return;
    }

    BuildLvglTopBar(screen);

    if (display_messages.empty()) {
        lv_obj_t *empty_title = LvLabel(screen, "还没有对话内容", &font_puhui_20_4);
        lv_obj_set_size(empty_title, 260, font_puhui_20_4.line_height);
        lv_obj_set_pos(empty_title, 116, 180);
        lv_obj_set_style_text_align(empty_title, LV_TEXT_ALIGN_CENTER, 0);

        lv_obj_t *empty_hint = LvLabel(screen, "返回主页后开始和小智聊天", &font_puhui_20_4);
        lv_obj_set_size(empty_hint, 360, font_puhui_20_4.line_height);
        lv_obj_set_pos(empty_hint, 66, 240);
        lv_obj_set_style_text_align(empty_hint, LV_TEXT_ALIGN_CENTER, 0);
    } else {
        start_index = std::max(0, std::min(start_index, static_cast<int>(display_messages.size()) - 1));
        int y = kContentTopY;
        int drawn_count = 0;
        for (int i = start_index; i < static_cast<int>(display_messages.size()); ++i) {
            const auto& message = display_messages[i];
            const int estimated_height = EstimateMessageHeight(message);
            if (drawn_count > 0 && y + estimated_height > kContentBottomY) {
                break;
            }
            BuildLvglMessage(screen, message, y);
            y += estimated_height;
            ++drawn_count;
        }

        if (start_index > 0) {
            lv_obj_t *prev = LvLabel(screen, "上一页", &font_puhui_basic_16_4);
            lv_obj_set_pos(prev, 16, 720);
        }
        if (start_index + drawn_count < static_cast<int>(display_messages.size())) {
            lv_obj_t *next = LvLabel(screen, "下一页", &font_puhui_basic_16_4);
            lv_obj_set_pos(next, 380, 720);
        }
    }

    lv_obj_t *footer = LvLabel(screen, "上下滚动 长按功能键上传附件 双击功能键返回主页", &font_puhui_16_4);
    lv_obj_set_size(footer, 450, font_puhui_16_4.line_height);
    lv_obj_set_pos(footer, 20, 748);
    lv_obj_set_style_text_align(footer, LV_TEXT_ALIGN_LEFT, 0);

    if (!epaper_lvgl_display_render()) {
        return;
    }
    epaper_lvgl_display_flush(use_partial_refresh ? Partial_refresh : Global_refresh);
}

void RenderPowerOffNoticeLvgl() {
    lv_obj_t *screen = epaper_lvgl_display_create_screen();
    if (screen == nullptr) {
        return;
    }
    BuildLvglTopBar(screen);
    lv_obj_t *label = LvLabel(screen, "已关机", &font_puhui_basic_16_4);
    lv_obj_set_size(label, 120, font_puhui_basic_16_4.line_height);
    lv_obj_set_pos(label, 180, 11);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    if (epaper_lvgl_display_render()) {
        epaper_lvgl_display_flush(Partial_refresh);
    }
}
#endif

void RenderChatPage(const std::vector<ChatHistoryMessage>& messages, int start_index, bool use_partial_refresh) {
#if CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint
    RenderChatPageLvgl(messages, start_index, use_partial_refresh);
    return;
#endif

    const std::vector<DisplayMessage> display_messages = BuildDisplayMessages(messages);

    Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
    Paint_SetScale(2);
    Paint_SelectImage(Image_Mono);
    Paint_Clear(WHITE);

    DrawTopBar();

    if (display_messages.empty()) {
        Paint_DrawString_CN(116, 180, "还没有对话内容", &Font24_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(66, 240, "返回主页后开始和小智聊天", &Font16_UTF8, WHITE, BLACK);
        Paint_DrawString_CN(20, 720, "上下滚动 长按功能键上传附件 双击功能键返回主页", &Font16_UTF8, WHITE, BLACK);
        if (use_partial_refresh) {
            EPD_Display_Partial(Image_Mono, 0, 0, EPD_WIDTH, EPD_HEIGHT);
        } else {
            EPD_Display_Base(Image_Mono);
        }
        return;
    }

    start_index = std::max(0, std::min(start_index, static_cast<int>(display_messages.size()) - 1));

    int y = kContentTopY;
    int drawn_count = 0;
    for (int i = start_index; i < static_cast<int>(display_messages.size()); ++i) {
        const auto& message = display_messages[i];
        const int estimated_height = EstimateMessageHeight(message);
        if (y + estimated_height > kContentBottomY) {
            break;
        }
        y = DrawMessageBubble(message, y);
        ++drawn_count;
    }

    if (start_index > 0) {
        Paint_DrawString_CN(16, 720, "上一页", &Font16_UTF8, WHITE, BLACK);
    }
    if (start_index + drawn_count < static_cast<int>(display_messages.size())) {
        Paint_DrawString_CN(380, 720, "下一页", &Font16_UTF8, WHITE, BLACK);
    }
    Paint_DrawString_CN(20, 720, "上下滚动 长按功能键上传附件 双击功能键返回主页", &Font16_UTF8, WHITE, BLACK);

    if (use_partial_refresh) {
        EPD_Display_Partial(Image_Mono, 0, 0, EPD_WIDTH, EPD_HEIGHT);
    } else {
        EPD_Display_Base(Image_Mono);
    }
}

}  // namespace

extern "C" bool page_chat_is_active(void) {
    return g_chat_page_active.load(std::memory_order_acquire);
}

extern "C" void page_chat_request_redraw(void) {
    g_chat_page_redraw_requested.store(true, std::memory_order_release);
}

extern "C" void page_chat_show(void) {
    ChatPageActiveGuard active_guard;
    std::vector<ChatHistoryMessage> messages = ChatHistory::GetMessages();
    std::vector<DisplayMessage> display_messages = BuildDisplayMessages(messages);
    int start_index = FindLatestStartIndex(display_messages);
    uint32_t messages_version = ComputeMessagesVersion(messages);
    Time_data rtc_time = {0};
    int last_minutes = -1;
    DeviceState last_device_state = Application::GetInstance().GetDeviceState();
    bool last_muted = false;
    if (auto codec = Board::GetInstance().GetAudioCodec(); codec != nullptr) {
        last_muted = Application::GetInstance().IsUiMuted();
    }
    if (rtc_mutex != nullptr) {
        xSemaphoreTake(rtc_mutex, portMAX_DELAY);
        rtc_time = PCF85063_GetTime();
        xSemaphoreGive(rtc_mutex);
        last_minutes = rtc_time.minutes;
    }

    EPD_Init();
    RenderChatPage(messages, start_index, false);

    while (true) {
        int button = wait_key_event_and_return_code(pdMS_TO_TICKS(1000));
        const std::vector<ChatHistoryMessage> latest_messages = ChatHistory::GetMessages();
        const std::vector<DisplayMessage> latest_display_messages = BuildDisplayMessages(latest_messages);
        const uint32_t latest_version = ComputeMessagesVersion(latest_messages);
        const int previous_visible = CountVisibleMessages(display_messages, start_index);
        const bool was_at_bottom =
            display_messages.empty() ||
            (start_index + std::max(1, previous_visible) >= static_cast<int>(display_messages.size()));
        bool needs_render = false;

        if (latest_version != messages_version) {
            messages = latest_messages;
            display_messages = latest_display_messages;
            messages_version = latest_version;
            if (was_at_bottom) {
                start_index = FindLatestStartIndex(display_messages);
            } else if (!display_messages.empty()) {
                start_index = std::min(start_index, static_cast<int>(display_messages.size()) - 1);
            } else {
                start_index = 0;
            }
            needs_render = true;
        } else {
            messages = latest_messages;
            display_messages = latest_display_messages;
        }

        if (g_chat_page_redraw_requested.exchange(false, std::memory_order_acq_rel)) {
            EPD_Init();
            needs_render = true;
        }

        if (rtc_mutex != nullptr) {
            xSemaphoreTake(rtc_mutex, portMAX_DELAY);
            rtc_time = PCF85063_GetTime();
            xSemaphoreGive(rtc_mutex);
        } else {
            rtc_time = PCF85063_GetTime();
        }
        const DeviceState current_device_state = Application::GetInstance().GetDeviceState();
        bool current_muted = false;
        if (auto codec = Board::GetInstance().GetAudioCodec(); codec != nullptr) {
            current_muted = Application::GetInstance().IsUiMuted();
        }
        if (rtc_time.minutes != last_minutes ||
            current_device_state != last_device_state ||
            current_muted != last_muted) {
            last_minutes = rtc_time.minutes;
            last_device_state = current_device_state;
            last_muted = current_muted;
            needs_render = true;
        }

        const int max_start = std::max(0, static_cast<int>(display_messages.size()) - 1);

        if (button == 0) {
            const int new_start_index = std::max(0, start_index - 1);
            if (new_start_index != start_index) {
                start_index = new_start_index;
                needs_render = true;
            }
        } else if (button == 14) {
            const int new_start_index = std::min(max_start, start_index + 1);
            if (new_start_index != start_index) {
                start_index = new_start_index;
                needs_render = true;
            }
        } else if (button == 8 || button == 23) {
            return;
        } else if (button == 12) {
            file_browser_task_for_chat_upload();
            EPD_Init();
            needs_render = true;
        } else if (button == 22) {
#if CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint
            RenderPowerOffNoticeLvgl();
#else
            Paint_DrawString_CN(180, 11, " 已关机 ", &Font16_UTF8, WHITE, BLACK);
            EPD_Display_Partial(Image_Mono, 0, 0, EPD_WIDTH, EPD_HEIGHT);
#endif
            axp_pwr_off();
        }

        if (needs_render) {
            RenderChatPage(messages, start_index, true);
        }
    }
}
