#include "photo_painter_home_lvgl.h"

#include "application.h"
#include "axp_prot.h"
#include "board.h"
#include "epaper_lvgl_display.h"
#include "epaper_port.h"

#include <font_awesome.h>
#include <lvgl.h>

LV_FONT_DECLARE(font_puhui_basic_16_4);
LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);
LV_FONT_DECLARE(font_awesome_30_4);

namespace {
constexpr int kHomePageSize = 12;
constexpr int kHomeItemCount = 12;
constexpr int kScreenWidth = 480;
constexpr int kBoxWidth = 140;
constexpr int kBoxHeight = 160;
constexpr int kTitleWidth = 140;
constexpr int kTitleHeight = 34;
constexpr int kStatusRowY = 7;
constexpr int kStatusRowHeight = 34;

int s_last_selection = 0;

const char *kHomeItems[kHomeItemCount] = {
    "阅读", "对话", "待办", "番茄钟",
    "音频", "日历", "时钟", "图片",
    "闹钟", "天气", "文件", "设置",
};

const int kBoxX[kHomePageSize] = {10, 170, 330, 10, 170, 330, 10, 170, 330, 10, 170, 330};
const int kBoxY[kHomePageSize] = {57, 57, 57, 243, 243, 243, 429, 429, 429, 615, 615, 615};

const char *kHomeIcons[kHomeItemCount] = {
    FONT_AWESOME_GLASSES,
    FONT_AWESOME_COMMENT,
    FONT_AWESOME_CIRCLE_CHECK,
    FONT_AWESOME_WATCH,
    FONT_AWESOME_MUSIC,
    FONT_AWESOME_CALENDAR,
    FONT_AWESOME_CLOCK,
    FONT_AWESOME_IMAGE,
    FONT_AWESOME_ALARM_CLOCK,
    FONT_AWESOME_CLOUD_SUN,
    FONT_AWESOME_SD_CARD,
    FONT_AWESOME_GEAR,
};

static lv_obj_t *Label(lv_obj_t *parent, const char *text, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text == nullptr ? "" : text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    return label;
}

static void AlignStatusLabel(lv_obj_t *label, int x, int width, const lv_font_t *font, lv_text_align_t align)
{
    const int line_height = font != nullptr ? font->line_height : kStatusRowHeight;
    lv_obj_set_size(label, width, line_height);
    lv_obj_set_pos(label, x, kStatusRowY + (kStatusRowHeight - line_height) / 2);
    lv_obj_set_style_text_align(label, align, 0);
}

static void BuildHomeIcon(lv_obj_t *box, int item_index)
{
    lv_obj_t *icon = Label(box, kHomeIcons[item_index],
                           &font_awesome_30_4);
    lv_obj_set_size(icon, kBoxWidth, 72);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
}

static const char *DialogueIcon()
{
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
        default:
            return FONT_AWESOME_COMMENT;
    }
}

static void BuildStatusBar(lv_obj_t *screen, Time_data rtc_time)
{
    char time_str[16] = {0};
    snprintf(time_str, sizeof(time_str), "%02d:%02d", rtc_time.hours, rtc_time.minutes);

    lv_obj_t *time_label = Label(screen, time_str, &font_puhui_20_4);
    AlignStatusLabel(time_label, 20, 80, &font_puhui_20_4, LV_TEXT_ALIGN_LEFT);

    lv_obj_t *dialogue_label = Label(screen, DialogueIcon(), &font_awesome_20_4);
    AlignStatusLabel(dialogue_label, 292, 32, &font_awesome_20_4, LV_TEXT_ALIGN_CENTER);

    lv_obj_t *wifi_label = Label(screen, Board::GetInstance().GetNetworkStateIcon(), &font_awesome_20_4);
    AlignStatusLabel(wifi_label, 326, 32, &font_awesome_20_4, LV_TEXT_ALIGN_CENTER);

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

    lv_obj_t *battery_cap = lv_obj_create(screen);
    lv_obj_set_size(battery_cap, 3, 6);
    lv_obj_set_pos(battery_cap, 398, kStatusRowY + (kStatusRowHeight - 6) / 2);
    lv_obj_set_style_radius(battery_cap, 0, 0);
    lv_obj_set_style_border_width(battery_cap, 0, 0);
    lv_obj_set_style_bg_color(battery_cap, lv_color_black(), 0);
    lv_obj_clear_flag(battery_cap, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *battery_fill = lv_obj_create(battery_box);
    lv_obj_set_size(battery_fill, (battery * 24) / 100, 10);
    lv_obj_set_pos(battery_fill, 1, 1);
    lv_obj_set_style_radius(battery_fill, 0, 0);
    lv_obj_set_style_border_width(battery_fill, 0, 0);
    lv_obj_set_style_bg_color(battery_fill, lv_color_black(), 0);
    lv_obj_clear_flag(battery_fill, LV_OBJ_FLAG_SCROLLABLE);

    char battery_str[16] = {0};
    snprintf(battery_str, sizeof(battery_str), "%d%%", battery);
    lv_obj_t *battery_label = Label(screen, battery_str, &font_puhui_20_4);
    AlignStatusLabel(battery_label, 411, 60, &font_puhui_20_4, LV_TEXT_ALIGN_LEFT);

    lv_obj_t *line = lv_obj_create(screen);
    lv_obj_set_size(line, kScreenWidth - 4, 2);
    lv_obj_set_pos(line, 2, 54);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_bg_color(line, lv_color_black(), 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
}

static bool BuildHome(Time_data rtc_time, int selection)
{
    epaper_lvgl_display_set_rotation(270);
    lv_obj_t *screen = epaper_lvgl_display_create_screen();
    if (screen == nullptr) {
        return false;
    }

    BuildStatusBar(screen, rtc_time);

    const int page_index = selection / kHomePageSize;
    const int selected_slot = selection % kHomePageSize;
    for (int slot = 0; slot < kHomePageSize; ++slot) {
        int item_index = page_index * kHomePageSize + slot;
        if (item_index >= kHomeItemCount) {
            break;
        }

        lv_obj_t *box = lv_obj_create(screen);
        lv_obj_set_size(box, kBoxWidth, kBoxHeight);
        lv_obj_set_pos(box, kBoxX[slot], kBoxY[slot]);
        lv_obj_set_style_radius(box, 0, 0);
        lv_obj_set_style_border_width(box, slot == selected_slot ? 2 : 0, 0);
        lv_obj_set_style_border_color(box, lv_color_black(), 0);
        lv_obj_set_style_bg_color(box, lv_color_white(), 0);
        lv_obj_set_style_pad_all(box, 0, 0);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

        BuildHomeIcon(box, item_index);

        lv_obj_t *title = Label(box, kHomeItems[item_index], &font_puhui_20_4);
        lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
        lv_obj_set_size(title, kTitleWidth, kTitleHeight);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 104);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    }

    return true;
}
} // namespace

extern "C" bool photo_painter_lvgl_home_show(int selection, int refresh_mode)
{
    Time_data rtc_time = PCF85063_GetTime();
    s_last_selection = selection;
    if (!BuildHome(rtc_time, selection)) {
        return false;
    }
    if (!epaper_lvgl_display_render()) {
        return false;
    }
    epaper_lvgl_display_flush(refresh_mode);
    return true;
}

extern "C" bool photo_painter_lvgl_home_move_selection(int selection, int refresh_mode)
{
    return photo_painter_lvgl_home_show(selection, refresh_mode);
}

extern "C" bool photo_painter_lvgl_home_update_status(Time_data rtc_time)
{
    if (!BuildHome(rtc_time, s_last_selection)) {
        return false;
    }
    if (!epaper_lvgl_display_render()) {
        return false;
    }
    epaper_lvgl_display_flush(Partial_refresh);
    return true;
}
