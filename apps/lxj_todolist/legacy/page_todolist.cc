#include "page_todolist.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <inttypes.h>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <time.h>

#include "esp_log.h"

#include "application.h"
#include "button_bsp.h"
#include "button_bsp_mcp.h"
#include "epaper_port.h"
#include "GUI_Paint.h"
#include "page_audio.h"
#include "pcf85063_bsp.h"
#include "qmi8658_bsp.h"
#include "status_bar.h"
#include "axp_prot.h"

extern SemaphoreHandle_t rtc_mutex;
extern SemaphoreHandle_t qmi8658_mutex;
extern bool wifi_enable;
extern uint8_t *Image_Mono;

// Home page selection index is defined in main.cc
extern int home_selection;

static const char *TAG = "todolist";

#define TODOLIST_DIR "/sdcard/todolist"
#define TODOLIST_FILE "/sdcard/todolist/todolist.csv"
#define COMPLETED_FILE "/sdcard/todolist/completedEvents.csv"

// E-ink screen sleep time (S)
#define EPD_Sleep_Time   5

// Max length for content when rendering (UTF-8 bytes)
#define TODO_CONTENT_MAX 120

typedef struct {
    uint16_t years;
    uint16_t months;
    uint16_t days;
    uint16_t hours;
    uint16_t minutes;
    uint16_t week;
} TodoTime;

typedef struct {
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
} TodoAlarm;

struct TodoItem {
    int index = 0;
    std::string content;
    TodoAlarm remind {0, 0, 0, 0};
    uint8_t remind_enabled = 0;  // 1=On, 0=Off
    TodoTime created {0};
    int type = 0; // 0..3
    int completed = 0; // 0/1
    TodoTime completed_time {0};
    uint32_t duration_minutes = 0; // task elapsed time in minutes
};

enum class RowKind {
    Header,
    Item,
    Placeholder
};

struct DisplayRow {
    RowKind kind;
    int type = 0;
    int item_index = -1; // index into g_items when kind == Item
    int display_number = 0; // number within the same type group
};

static std::vector<TodoItem> g_items;
static SemaphoreHandle_t g_todo_mutex = nullptr;
static TaskHandle_t g_reminder_task = nullptr;
static bool g_loaded = false;
static uint32_t g_todo_version = 0;

static const char* kTypeNames[] = {
    "重要紧急（立即去做）",
    "重要不紧急（计划去做）",
    "不重要紧急（授权去做）",
    "不重要不紧急（尽量不做）"
};

static void todolist_ensure_dir() {
    struct stat st;
    if (stat(TODOLIST_DIR, &st) == 0) {
        return;
    }
    mkdir(TODOLIST_DIR, 0777);
}

static TodoTime todolist_now() {
    Time_data rtc_time = {0};
    if (rtc_mutex) {
        xSemaphoreTake(rtc_mutex, portMAX_DELAY);
        rtc_time = PCF85063_GetTime();
        xSemaphoreGive(rtc_mutex);
    } else {
        rtc_time = PCF85063_GetTime();
    }

    TodoTime t;
    t.years = (rtc_time.years < 100) ? (rtc_time.years + 2000) : rtc_time.years;
    t.months = rtc_time.months;
    t.days = rtc_time.days;
    t.hours = rtc_time.hours;
    t.minutes = rtc_time.minutes;
    t.week = rtc_time.week;
    return t;
}

static time_t todolist_to_time_t(const TodoTime& t) {
    if (t.years == 0 || t.months == 0 || t.days == 0) {
        return 0;
    }
    struct tm tm_time = {};
    tm_time.tm_year = static_cast<int>(t.years) - 1900;
    tm_time.tm_mon = static_cast<int>(t.months) - 1;
    tm_time.tm_mday = static_cast<int>(t.days);
    tm_time.tm_hour = static_cast<int>(t.hours);
    tm_time.tm_min = static_cast<int>(t.minutes);
    tm_time.tm_sec = 0;
    tm_time.tm_isdst = -1;
    return mktime(&tm_time);
}

static uint32_t todolist_calc_duration_minutes(const TodoTime& start, const TodoTime& end) {
    time_t t_start = todolist_to_time_t(start);
    time_t t_end = todolist_to_time_t(end);
    if (t_start <= 0 || t_end <= 0 || t_end <= t_start) {
        return 0;
    }
    return static_cast<uint32_t>((t_end - t_start) / 60);
}

static int todolist_days_in_month(int month) {
    switch (month) {
        case 1: case 3: case 5: case 7: case 8: case 10: case 12:
            return 31;
        case 4: case 6: case 9: case 11:
            return 30;
        case 2:
            return 29;  // allow Feb 29 for leap years
        default:
            return 31;
    }
}

static bool todolist_reminder_matches_now(const TodoItem& item, const Time_data& rtc_time) {
    if (item.completed != 0 || item.remind_enabled == 0) {
        return false;
    }
    if (item.remind.hour != rtc_time.hours || item.remind.minute != rtc_time.minutes) {
        return false;
    }
    // Legacy compatibility: month/day unset means daily reminder at HH:MM.
    if (item.remind.month == 0 || item.remind.day == 0) {
        return true;
    }
    return item.remind.month == rtc_time.months && item.remind.day == rtc_time.days;
}

static std::string csv_escape(const std::string& input) {
    bool need_quote = false;
    for (char c : input) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            need_quote = true;
            break;
        }
    }
    if (!need_quote) {
        return input;
    }

    std::string out;
    out.push_back('"');
    for (char c : input) {
        if (c == '"') {
            out.append("\"\"");
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

static std::string utf8_truncate(const std::string& input, size_t max_bytes) {
    if (input.size() <= max_bytes) {
        return input;
    }
    size_t pos = 0;
    while (pos < max_bytes) {
        unsigned char c = static_cast<unsigned char>(input[pos]);
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

        if (pos + char_len > max_bytes) {
            break;
        }
        pos += char_len;
    }
    std::string out = input.substr(0, pos);
    if (pos < input.size()) {
        out += "...";
    }
    return out;
}

static std::vector<std::string> csv_split(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool in_quotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    field.push_back('"');
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                field.push_back(c);
            }
        } else {
            if (c == '"') {
                in_quotes = true;
            } else if (c == ',') {
                fields.push_back(field);
                field.clear();
            } else if (c != '\r' && c != '\n') {
                field.push_back(c);
            }
        }
    }
    fields.push_back(field);
    return fields;
}

static bool csv_to_int(const std::string& value, int* out) {
    if (!out) return false;
    if (value.empty()) return false;
    char* end_ptr = nullptr;
    long v = strtol(value.c_str(), &end_ptr, 10);
    if (end_ptr == value.c_str()) return false;
    *out = static_cast<int>(v);
    return true;
}

static void todolist_reindex_locked() {
    for (size_t i = 0; i < g_items.size(); ++i) {
        g_items[i].index = static_cast<int>(i) + 1;
    }
}

static void todolist_bump_version_locked() {
    g_todo_version++;
}

static bool todolist_load_csv_locked() {
    if (g_loaded) {
        return true;
    }

    g_items.clear();
    FILE* fp = fopen(TODOLIST_FILE, "r");
    if (!fp) {
        g_loaded = true;
        return false;
    }

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        std::string line_str(line);
        auto fields = csv_split(line_str);
        if (fields.size() < 13) {
            continue;
        }

        int idx = 0;
        if (!csv_to_int(fields[0], &idx)) {
            // Skip headers or invalid lines
            continue;
        }

        TodoItem item;
        item.index = idx;
        item.content = fields[1];

        int tmp = 0;
        if (fields.size() >= 22) {
            // New format:
            // idx,content,remind_month,remind_day,remind_hour,remind_minute,remind_enabled,
            // created(6),type,completed,completed_time(6),duration
            if (csv_to_int(fields[2], &tmp)) item.remind.month = static_cast<uint8_t>(tmp);
            if (csv_to_int(fields[3], &tmp)) item.remind.day = static_cast<uint8_t>(tmp);
            if (csv_to_int(fields[4], &tmp)) item.remind.hour = static_cast<uint8_t>(tmp);
            if (csv_to_int(fields[5], &tmp)) item.remind.minute = static_cast<uint8_t>(tmp);
            if (csv_to_int(fields[6], &tmp)) item.remind_enabled = static_cast<uint8_t>(tmp);

            csv_to_int(fields[7], &tmp); item.created.years = static_cast<uint16_t>(tmp);
            csv_to_int(fields[8], &tmp); item.created.months = static_cast<uint16_t>(tmp);
            csv_to_int(fields[9], &tmp); item.created.days = static_cast<uint16_t>(tmp);
            csv_to_int(fields[10], &tmp); item.created.hours = static_cast<uint16_t>(tmp);
            csv_to_int(fields[11], &tmp); item.created.minutes = static_cast<uint16_t>(tmp);
            csv_to_int(fields[12], &tmp); item.created.week = static_cast<uint16_t>(tmp);

            csv_to_int(fields[13], &tmp); item.type = tmp;
            csv_to_int(fields[14], &tmp); item.completed = tmp;

            csv_to_int(fields[15], &tmp); item.completed_time.years = static_cast<uint16_t>(tmp);
            csv_to_int(fields[16], &tmp); item.completed_time.months = static_cast<uint16_t>(tmp);
            csv_to_int(fields[17], &tmp); item.completed_time.days = static_cast<uint16_t>(tmp);
            csv_to_int(fields[18], &tmp); item.completed_time.hours = static_cast<uint16_t>(tmp);
            csv_to_int(fields[19], &tmp); item.completed_time.minutes = static_cast<uint16_t>(tmp);
            csv_to_int(fields[20], &tmp); item.completed_time.week = static_cast<uint16_t>(tmp);

            csv_to_int(fields[21], &tmp); item.duration_minutes = static_cast<uint32_t>(tmp);
        } else {
            // Legacy format compatibility:
            // idx,content,remind_hour,remind_minute,remind_enabled,created(6),type,completed,completed_time(6),duration
            if (fields.size() >= 5) {
                if (csv_to_int(fields[2], &tmp)) item.remind.hour = static_cast<uint8_t>(tmp);
                if (csv_to_int(fields[3], &tmp)) item.remind.minute = static_cast<uint8_t>(tmp);
                if (csv_to_int(fields[4], &tmp)) item.remind_enabled = static_cast<uint8_t>(tmp);
            }

            if (fields.size() >= 11) {
                csv_to_int(fields[5], &tmp); item.created.years = static_cast<uint16_t>(tmp);
                csv_to_int(fields[6], &tmp); item.created.months = static_cast<uint16_t>(tmp);
                csv_to_int(fields[7], &tmp); item.created.days = static_cast<uint16_t>(tmp);
                csv_to_int(fields[8], &tmp); item.created.hours = static_cast<uint16_t>(tmp);
                csv_to_int(fields[9], &tmp); item.created.minutes = static_cast<uint16_t>(tmp);
                csv_to_int(fields[10], &tmp); item.created.week = static_cast<uint16_t>(tmp);
            }

            if (fields.size() >= 13) {
                csv_to_int(fields[11], &tmp); item.type = tmp;
                csv_to_int(fields[12], &tmp); item.completed = tmp;
            }

            if (fields.size() >= 19) {
                csv_to_int(fields[13], &tmp); item.completed_time.years = static_cast<uint16_t>(tmp);
                csv_to_int(fields[14], &tmp); item.completed_time.months = static_cast<uint16_t>(tmp);
                csv_to_int(fields[15], &tmp); item.completed_time.days = static_cast<uint16_t>(tmp);
                csv_to_int(fields[16], &tmp); item.completed_time.hours = static_cast<uint16_t>(tmp);
                csv_to_int(fields[17], &tmp); item.completed_time.minutes = static_cast<uint16_t>(tmp);
                csv_to_int(fields[18], &tmp); item.completed_time.week = static_cast<uint16_t>(tmp);
            }

            if (fields.size() >= 20) {
                csv_to_int(fields[19], &tmp); item.duration_minutes = static_cast<uint32_t>(tmp);
            }
        }

        g_items.push_back(std::move(item));
    }

    fclose(fp);
    todolist_reindex_locked();
    g_loaded = true;
    return true;
}

static bool todolist_save_csv_locked() {
    todolist_ensure_dir();
    FILE* fp = fopen(TODOLIST_FILE, "w");
    if (!fp) {
        ESP_LOGE(TAG, "Failed to open todolist file for write");
        return false;
    }

    todolist_reindex_locked();
    for (const auto& item : g_items) {
        std::string content = csv_escape(item.content);
        fprintf(fp, "%d,%s,%d,%d,%d,%d,%d,",
                item.index,
                content.c_str(),
                item.remind.month,
                item.remind.day,
                item.remind.hour,
                item.remind.minute,
                item.remind_enabled);
        fprintf(fp, "%d,%d,%d,%d,%d,%d,",
                item.created.years,
                item.created.months,
                item.created.days,
                item.created.hours,
                item.created.minutes,
                item.created.week);
        fprintf(fp, "%d,%d,",
                item.type,
                item.completed);
        fprintf(fp, "%d,%d,%d,%d,%d,%d,",
                item.completed_time.years,
                item.completed_time.months,
                item.completed_time.days,
                item.completed_time.hours,
                item.completed_time.minutes,
                item.completed_time.week);
        fprintf(fp, "%" PRIu32 "\n", item.duration_minutes);
    }

    fclose(fp);
    todolist_bump_version_locked();
    return true;
}

static bool todolist_append_completed_event_locked(const TodoItem& item, const TodoTime& delete_time) {
    todolist_ensure_dir();
    FILE* fp = fopen(COMPLETED_FILE, "a");
    if (!fp) {
        ESP_LOGE(TAG, "Failed to open completed events file");
        return false;
    }

    std::string content = csv_escape(item.content);
    fprintf(fp, "%d,%s,%d,%d,%d,%d,%d,",
            item.index,
            content.c_str(),
            item.remind.month,
            item.remind.day,
            item.remind.hour,
            item.remind.minute,
            item.remind_enabled);
    fprintf(fp, "%d,%d,%d,%d,%d,%d,",
            item.created.years,
            item.created.months,
            item.created.days,
            item.created.hours,
            item.created.minutes,
            item.created.week);
    fprintf(fp, "%d,%d,",
            item.type,
            item.completed);
    fprintf(fp, "%d,%d,%d,%d,%d,%d,",
            item.completed_time.years,
            item.completed_time.months,
            item.completed_time.days,
            item.completed_time.hours,
            item.completed_time.minutes,
            item.completed_time.week);
    fprintf(fp, "%" PRIu32 ",", item.duration_minutes);
    fprintf(fp, "%d,%d,%d,%d,%d,%d\n",
            delete_time.years,
            delete_time.months,
            delete_time.days,
            delete_time.hours,
            delete_time.minutes,
            delete_time.week);

    fclose(fp);
    return true;
}

static bool todolist_add_item_locked(const char* content,
                                     int type,
                                     int remind_enabled,
                                     int remind_month,
                                     int remind_day,
                                     int remind_hour,
                                     int remind_minute) {
    if (!content || content[0] == '\0') {
        return false;
    }

    TodoItem item;
    item.content = content;
    item.type = std::max(0, std::min(type, 3));
    item.completed = 0;
    item.created = todolist_now();
    item.remind_enabled = (remind_enabled != 0) ? 1 : 0;
    if (item.remind_enabled) {
        int month = remind_month;
        int day = remind_day;
        if (month <= 0 || day <= 0) {
            month = item.created.months;
            day = item.created.days;
        }
        month = std::max(1, std::min(month, 12));
        day = std::max(1, std::min(day, todolist_days_in_month(month)));
        item.remind.month = static_cast<uint8_t>(month);
        item.remind.day = static_cast<uint8_t>(day);
        item.remind.hour = static_cast<uint8_t>(std::max(0, std::min(remind_hour, 23)));
        item.remind.minute = static_cast<uint8_t>(std::max(0, std::min(remind_minute, 59)));
    }

    g_items.push_back(std::move(item));
    return todolist_save_csv_locked();
}

static bool todolist_mark_completed_locked(int item_index) {
    if (item_index < 0 || item_index >= static_cast<int>(g_items.size())) {
        return false;
    }
    TodoItem& item = g_items[item_index];
    if (item.completed) {
        return true;
    }
    item.completed = 1;
    item.completed_time = todolist_now();
    item.duration_minutes = todolist_calc_duration_minutes(item.created, item.completed_time);
    return todolist_save_csv_locked();
}

static bool todolist_toggle_completed_locked(int item_index) {
    if (item_index < 0 || item_index >= static_cast<int>(g_items.size())) {
        return false;
    }
    TodoItem& item = g_items[item_index];
    if (item.completed) {
        item.completed = 0;
        item.completed_time = {};
        item.duration_minutes = 0;
        return todolist_save_csv_locked();
    }
    return todolist_mark_completed_locked(item_index);
}

static bool todolist_delete_item_locked(int item_index) {
    if (item_index < 0 || item_index >= static_cast<int>(g_items.size())) {
        return false;
    }

    TodoItem item = g_items[item_index];
    TodoTime delete_time = todolist_now();
    if (item.completed == 0) {
        item.completed = 1;
        item.completed_time = delete_time;
        item.duration_minutes = todolist_calc_duration_minutes(item.created, item.completed_time);
    }

    todolist_append_completed_event_locked(item, delete_time);
    g_items.erase(g_items.begin() + item_index);
    return todolist_save_csv_locked();
}

static bool todolist_is_portrait() {
    float acc[3] = {0};
    float gyro[3] = {0};
    static bool last_portrait = true;
    static bool last_candidate = true;
    static int stable_samples = 0;
    const float kSwitchThreshold = 400.0f; // Reduce sensitivity to small jitters
    const int kStableSamples = 1;
    if (qmi8658_mutex) {
        xSemaphoreTake(qmi8658_mutex, portMAX_DELAY);
        QMI8658_read_xyz(acc, gyro, NULL);
        xSemaphoreGive(qmi8658_mutex);
    } else {
        QMI8658_read_xyz(acc, gyro, NULL);
    }

    float ax = fabsf(acc[0]);
    float ay = fabsf(acc[1]);
    bool candidate = last_portrait;

    if (ax > ay + kSwitchThreshold) {
        candidate = true;
    } else if (ay > ax + kSwitchThreshold) {
        candidate = false;
    } else {
        stable_samples = 0;
        last_candidate = candidate;
        return last_portrait;
    }

    if (candidate == last_candidate) {
        stable_samples++;
    } else {
        last_candidate = candidate;
        stable_samples = 1;
    }

    if (stable_samples >= kStableSamples) {
        last_portrait = candidate;
        stable_samples = 0;
    }

    return last_portrait;
}

static void todolist_build_rows(std::vector<DisplayRow>& rows) {
    rows.clear();
    for (int type = 0; type < 4; ++type) {
        rows.push_back({RowKind::Header, type, -1});
        bool has_item = false;
        int display_counter = 0;
        for (size_t i = 0; i < g_items.size(); ++i) {
            if (g_items[i].type == type) {
                display_counter++;
                rows.push_back({RowKind::Item, type, static_cast<int>(i), display_counter});
                has_item = true;
            }
        }
        if (!has_item) {
            rows.push_back({RowKind::Placeholder, type, -1});
        }
    }
}

static int todolist_find_selected_row(const std::vector<DisplayRow>& rows, int selected_item) {
    if (selected_item < 0) return -1;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].kind == RowKind::Item && rows[i].item_index == selected_item) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

static int todolist_find_next_item(const std::vector<DisplayRow>& rows, int current_item, bool forward) {
    int start_row = todolist_find_selected_row(rows, current_item);
    if (start_row < 0) {
        for (const auto& row : rows) {
            if (row.kind == RowKind::Item) {
                return row.item_index;
            }
        }
        return -1;
    }

    if (forward) {
        for (size_t i = static_cast<size_t>(start_row + 1); i < rows.size(); ++i) {
            if (rows[i].kind == RowKind::Item) {
                return rows[i].item_index;
            }
        }
    } else {
        for (int i = start_row - 1; i >= 0; --i) {
            if (rows[i].kind == RowKind::Item) {
                return rows[i].item_index;
            }
        }
    }
    return current_item;
}

static void todolist_draw_checkbox(int x, int y, bool checked) {
    Paint_DrawRectangle(x, y + 2, x + 16, y + 18, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
    if (checked) {
        Paint_DrawLine(x + 3, y + 10, x + 7, y + 14, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        Paint_DrawLine(x + 7, y + 14, x + 13, y + 5, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    }
}

static void todolist_draw_page(int selected_item, int page_index, bool portrait, int refresh_mode) {
    UWORD rotate = portrait ? ROTATE_270 : ROTATE_0;
    const UWORD draw_width = (rotate == ROTATE_0 || rotate == ROTATE_180) ? EPD_WIDTH : EPD_HEIGHT;
    const UWORD draw_height = (rotate == ROTATE_0 || rotate == ROTATE_180) ? EPD_HEIGHT : EPD_WIDTH;

    Paint_NewImage(Image_Mono, EPD_WIDTH, EPD_HEIGHT, rotate, WHITE);
    Paint_SetScale(2);
    Paint_SelectImage(Image_Mono);
    Paint_Clear(WHITE);

    Time_data rtc_time = {0};
    if (rtc_mutex) {
        xSemaphoreTake(rtc_mutex, portMAX_DELAY);
        rtc_time = PCF85063_GetTime();
        xSemaphoreGive(rtc_mutex);
    }

    char time_str[16] = {0};
    char battery_str[16] = {0};
    int battery = get_battery_power();
    int battery_bar = battery;
    const int wifi_x = static_cast<int>(draw_width) - 154;
    const int bat_icon_x = static_cast<int>(draw_width) - 110;
    const int bat_text_x = static_cast<int>(draw_width) - 69;
    const int bat_bar_x0 = static_cast<int>(draw_width) - 105;
    const int bat_bar_x1 = static_cast<int>(draw_width) - 85;

    snprintf(time_str, sizeof(time_str), "%02d:%02d", rtc_time.hours, rtc_time.minutes);
    Paint_DrawString_EN(20, 11, time_str, &Font16, WHITE, BLACK);
#if defined(CONFIG_IMG_SOURCE_EMBEDDED)
    DrawWifiStatusIcon(wifi_x, 8);
    Paint_ReadBmp(gImage_BAT, bat_icon_x, 17, 32, 16);
#elif defined(CONFIG_IMG_SOURCE_TFCARD)
    DrawWifiStatusIcon(wifi_x, 8);
    GUI_ReadBmp(BMP_BAT_PATH, bat_icon_x, 17);
#endif
    snprintf(battery_str, sizeof(battery_str), "%d%%", battery);
    if (battery_bar == -1) battery_bar = 20;
    else battery_bar = battery_bar * 20 / 100;
    Paint_DrawString_EN(bat_text_x, 11, battery_str, &Font16, WHITE, BLACK);
    Paint_DrawRectangle(bat_bar_x0, 22, bat_bar_x1, 30, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawRectangle(bat_bar_x0, 22, bat_bar_x0 + battery_bar, 30, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);

    Paint_DrawLine(2, 54, draw_width - 2, 54, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);

    std::vector<DisplayRow> rows;
    todolist_build_rows(rows);

    const int top = 58;
    const int line_height = Font18_UTF8.Height + 12;
    const int bottom = 40;
    int rows_per_page = (draw_height - top - bottom) / line_height;
    if (rows_per_page < 3) rows_per_page = 3;

    int total_pages = (rows.size() + rows_per_page - 1) / rows_per_page;
    if (total_pages <= 0) total_pages = 1;
    if (page_index < 0) page_index = 0;
    if (page_index >= total_pages) page_index = total_pages - 1;

    int start_row = page_index * rows_per_page;
    int end_row = std::min(static_cast<int>(rows.size()), start_row + rows_per_page);

    for (int i = start_row; i < end_row; ++i) {
        int row_y = top + (i - start_row) * line_height;
        const DisplayRow& row = rows[i];

        if (row.kind == RowKind::Header) {
            Paint_DrawString_CN(10, row_y, kTypeNames[row.type], &Font18_UTF8, WHITE, BLACK);
            Paint_DrawLine(10, row_y + Font18_UTF8.Height + 2, draw_width - 10, row_y + Font18_UTF8.Height + 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
        } else if (row.kind == RowKind::Placeholder) {
            Paint_DrawString_CN(20, row_y, "（暂无）", &Font16_UTF8, WHITE, BLACK);
        } else if (row.kind == RowKind::Item && row.item_index >= 0 && row.item_index < static_cast<int>(g_items.size())) {
            const TodoItem& item = g_items[row.item_index];
            bool selected = (row.item_index == selected_item);

            if (selected) {
                Paint_DrawRectangle(4, row_y - 3, draw_width - 4, row_y + line_height - 5, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
            }

            int x = 12;
            todolist_draw_checkbox(x, row_y + 6, item.completed != 0);
            x += 24;

            char prefix[16] = {0};
            int display_no = row.display_number > 0 ? row.display_number : item.index;
            snprintf(prefix, sizeof(prefix), "%d.", display_no);
            Paint_DrawString_EN(x, row_y, prefix, &Font18, WHITE, BLACK);
            x += (Font18.Width * static_cast<int>(strlen(prefix)) + 4);

            std::string content = utf8_truncate(item.content, TODO_CONTENT_MAX);
            Paint_DrawString_CN(x, row_y, content.c_str(), &Font18_UTF8, WHITE, BLACK);

            ESP_LOGI(TAG, "Draw item idx=%d type=%d completed=%d remind_enabled=%d remind=%d-%d %02d:%02d content=%s",
                     item.index, item.type, item.completed, item.remind_enabled,
                     item.remind.month, item.remind.day, item.remind.hour, item.remind.minute,
                     item.content.c_str());
            if (item.remind_enabled) {
                char remind_str[20] = {0};
                if (item.remind.month > 0 && item.remind.day > 0) {
                    snprintf(remind_str, sizeof(remind_str), "%02d/%02d %02d:%02d",
                             item.remind.month, item.remind.day, item.remind.hour, item.remind.minute);
                } else {
                    snprintf(remind_str, sizeof(remind_str), "--/-- %02d:%02d",
                             item.remind.hour, item.remind.minute);
                }
                int remind_width = Font18.Width * static_cast<int>(strlen(remind_str));
                int remind_x = static_cast<int>(draw_width) - remind_width - 12;  // keep right margin
                if (remind_x < x + 24) {
                    remind_x = x + 24;
                }
                Paint_DrawString_EN(remind_x, row_y, remind_str, &Font18, WHITE, BLACK);
            }

            if (item.completed) {
                int line_y = row_y + Font18_UTF8.Height / 2;
                Paint_DrawLine(x - 3, line_y + 3, draw_width - 10, line_y + 3, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
            }
        }
    }

    char footer[128] = {0};
    snprintf(footer, sizeof(footer), "页%d/%d 单击:完成 长按:删除 双击:返回", page_index + 1, total_pages);
    Paint_DrawString_CN(10, draw_height - 28, footer, &Font16_UTF8, WHITE, BLACK);

    if (refresh_mode == Global_refresh) {
        EPD_Display_Base(Image_Mono);
    } else {
        EPD_Display_Partial(Image_Mono, 0, 0, EPD_WIDTH, EPD_HEIGHT);
    }
}

static bool todolist_play_server_tts(const TodoItem& item) {
    if (item.content.empty()) {
        return false;
    }

    auto& app = Application::GetInstance();
    char spoken_text[TODO_CONTENT_MAX + 32] = {0};
    snprintf(spoken_text, sizeof(spoken_text), "待办提醒：%s", item.content.c_str());

    static constexpr char kReminderMetadata[] =
        "{\"source\":\"todo_reminder\",\"chunk_index\":1,"
        "\"prefetch\":false,\"single_shot\":true,\"suppress_prefetch\":true}";

    app.EnableReadingAiMode();
    app.SendTtsText(spoken_text, kReminderMetadata);

    bool playback_started = false;
    bool playback_finished = false;
    int idle_polls_after_start = 0;
    int start_wait_polls = 0;

    for (int i = 0; i < 300; ++i) {
        if (app.ConsumeReadingAiPlaybackFinished()) {
            playback_finished = true;
            break;
        }

        auto state = app.GetDeviceState();
        if (state == kDeviceStateSpeaking) {
            playback_started = true;
            idle_polls_after_start = 0;
        } else if (playback_started && state == kDeviceStateIdle) {
            idle_polls_after_start++;
            if (idle_polls_after_start >= 5) {
                break;
            }
        } else if (!playback_started) {
            start_wait_polls++;
            if (start_wait_polls >= 50) {
                ESP_LOGW(TAG, "Reminder TTS start timeout, fallback to ringtone: %s",
                         item.content.c_str());
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    app.DisableReadingAiMode(true);

    if (!playback_started) {
        ESP_LOGW(TAG, "Reminder TTS did not start, fallback to ringtone only: %s",
                 item.content.c_str());
        return false;
    }

    if (!playback_finished) {
        ESP_LOGW(TAG, "Reminder TTS did not report playback completion, continue with ringtone: %s",
                 item.content.c_str());
    }

    return true;
}

static void todolist_preempt_conversation_for_reminder() {
    auto& app = Application::GetInstance();
    auto state = app.GetDeviceState();
    if (state != kDeviceStateConnecting &&
        state != kDeviceStateListening &&
        state != kDeviceStateSpeaking) {
        return;
    }

    ESP_LOGI(TAG, "Preempt conversation for todo reminder");
    app.InterruptConversationForLocalAudio();

    for (int i = 0; i < 50; ++i) {
        auto current_state = app.GetDeviceState();
        if (current_state != kDeviceStateConnecting &&
            current_state != kDeviceStateListening &&
            current_state != kDeviceStateSpeaking) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void todolist_reminder_notify(const TodoItem& item) {
    auto& app = Application::GetInstance();
    todolist_preempt_conversation_for_reminder();
    todolist_play_server_tts(item);
    page_audio_play_memory();
    app.RestoreWakeWordDetectionIfIdle();
}

static void todolist_reminder_task(void* arg) {
    int last_minute = -1;
    int last_hour = -1;
    int last_day = -1;
    int last_month = -1;
    int last_year = -1;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        auto& app = Application::GetInstance();
        Time_data rtc_time = {0};
        if (rtc_mutex) {
            xSemaphoreTake(rtc_mutex, portMAX_DELAY);
            rtc_time = PCF85063_GetTime();
            xSemaphoreGive(rtc_mutex);
        }

        if (rtc_time.minutes == last_minute &&
            rtc_time.hours == last_hour &&
            rtc_time.days == last_day &&
            rtc_time.months == last_month &&
            rtc_time.years == last_year) {
            continue;
        }

        last_minute = rtc_time.minutes;
        last_hour = rtc_time.hours;
        last_day = rtc_time.days;
        last_month = rtc_time.months;
        last_year = rtc_time.years;

        std::vector<TodoItem> due_items;
        if (g_todo_mutex) {
            xSemaphoreTake(g_todo_mutex, portMAX_DELAY);
        }
        todolist_load_csv_locked();
        for (const auto& item : g_items) {
            if (todolist_reminder_matches_now(item, rtc_time)) {
                due_items.push_back(item);
            }
        }
        if (g_todo_mutex) {
            xSemaphoreGive(g_todo_mutex);
        }

        if (!due_items.empty()) {
            for (const auto& item : due_items) {
                ESP_LOGI(TAG, "Reminder: %s", item.content.c_str());
                todolist_reminder_notify(item);
            }
        }
    }
}

static void todolist_ensure_initialized() {
    if (!g_todo_mutex) {
        g_todo_mutex = xSemaphoreCreateMutex();
    }

    if (g_todo_mutex) {
        xSemaphoreTake(g_todo_mutex, portMAX_DELAY);
    }
    todolist_load_csv_locked();
    if (g_todo_mutex) {
        xSemaphoreGive(g_todo_mutex);
    }

    if (!g_reminder_task) {
        xTaskCreate(todolist_reminder_task, "todolist_reminder", 4 * 1024, nullptr, 4, &g_reminder_task);
    }
}

void page_todolist_init(void) {
    todolist_ensure_initialized();
}

bool todolist_add_item_from_mcp(const char* content,
                               int type,
                               int remind_enabled,
                               int remind_month,
                               int remind_day,
                               int remind_hour,
                               int remind_minute) {
    todolist_ensure_initialized();
    bool ok = false;
    if (g_todo_mutex) {
        xSemaphoreTake(g_todo_mutex, portMAX_DELAY);
    }
    ok = todolist_add_item_locked(content, type, remind_enabled, remind_month, remind_day, remind_hour, remind_minute);
    if (g_todo_mutex) {
        xSemaphoreGive(g_todo_mutex);
    }
    return ok;
}

void todolist_open_from_mcp(void) {
    home_selection = 2; // todolist entry
    button_mcp_set_event_code(BUTTON_BSP_ID_FUNCTION, BUTTON_BSP_EVENT_SINGLE_CLICK);
}

void page_todolist_show(void) {
    todolist_ensure_initialized();

    auto read_rtc_time = []() -> Time_data {
        Time_data rtc_time = {0};
        if (rtc_mutex) {
            xSemaphoreTake(rtc_mutex, portMAX_DELAY);
            rtc_time = PCF85063_GetTime();
            xSemaphoreGive(rtc_mutex);
        } else {
            rtc_time = PCF85063_GetTime();
        }
        return rtc_time;
    };

    int selected_item = -1;
    int page_index = 0;
    int time_count = 0;
    int last_clock_hour = -1;
    int last_clock_minute = -1;
    bool portrait = true;
    uint32_t last_version = 0;

    if (g_todo_mutex) {
        xSemaphoreTake(g_todo_mutex, portMAX_DELAY);
    }
    todolist_load_csv_locked();
    last_version = g_todo_version;
    if (!g_items.empty()) {
        selected_item = 0;
    }
    if (g_todo_mutex) {
        xSemaphoreGive(g_todo_mutex);
    }

    Time_data init_time = read_rtc_time();
    last_clock_hour = init_time.hours;
    last_clock_minute = init_time.minutes;

    portrait = todolist_is_portrait();
    if (g_todo_mutex) {
        xSemaphoreTake(g_todo_mutex, portMAX_DELAY);
    }
    todolist_draw_page(selected_item, page_index, portrait, Global_refresh);
    if (g_todo_mutex) {
        xSemaphoreGive(g_todo_mutex);
    }

    while (1) {
        int button = wait_key_event_and_return_code(pdMS_TO_TICKS(1000));
        if (button == -1) {
            time_count++;
        } else {
            time_count = 0;
        }

        // if (time_count >= EPD_Sleep_Time) {
        //     EPD_Sleep();
        //     // Wake on any key
        //     wait_key_event_and_return_code(portMAX_DELAY);
        //     EPD_Init();
        //     time_count = 0;
        //     portrait = todolist_is_portrait();
        //     if (g_todo_mutex) {
        //         xSemaphoreTake(g_todo_mutex, portMAX_DELAY);
        //     }
        //     todolist_draw_page(selected_item, page_index, portrait, Global_refresh);
        //     if (g_todo_mutex) {
        //         xSemaphoreGive(g_todo_mutex);
        //     }
        //     continue;
        // }

        uint32_t current_version = last_version;
        std::vector<DisplayRow> rows;
        if (g_todo_mutex) {
            xSemaphoreTake(g_todo_mutex, portMAX_DELAY);
        }
        todolist_load_csv_locked();
        todolist_build_rows(rows);
        current_version = g_todo_version;
        if (g_todo_mutex) {
            xSemaphoreGive(g_todo_mutex);
        }

        bool force_refresh = (current_version != last_version);
        Time_data now_time = read_rtc_time();
        if (now_time.hours != last_clock_hour || now_time.minutes != last_clock_minute) {
            last_clock_hour = now_time.hours;
            last_clock_minute = now_time.minutes;
            force_refresh = true;
        }
        if (force_refresh) {
            last_version = current_version;
        }

        int selected_row = todolist_find_selected_row(rows, selected_item);
        if (selected_row < 0) {
            for (const auto& row : rows) {
                if (row.kind == RowKind::Item) {
                    selected_item = row.item_index;
                    selected_row = todolist_find_selected_row(rows, selected_item);
                    break;
                }
            }
        }
        int line_height = Font18_UTF8.Height + 12;
        int top = 58;
        bool current_portrait = todolist_is_portrait();
        UWORD rotate = current_portrait ? ROTATE_270 : ROTATE_0;
        const UWORD draw_height = (rotate == ROTATE_0 || rotate == ROTATE_180) ? EPD_HEIGHT : EPD_WIDTH;
        int rows_per_page = (draw_height - top - 40) / line_height;
        if (rows_per_page < 3) rows_per_page = 3;
        int total_pages = (rows.size() + rows_per_page - 1) / rows_per_page;
        if (total_pages <= 0) total_pages = 1;

        bool list_changed = false;

        if (button == 14) {
            int next_item = todolist_find_next_item(rows, selected_item, true);
            if (next_item != selected_item) {
                selected_item = next_item;
            }
        } else if (button == 0) {
            int prev_item = todolist_find_next_item(rows, selected_item, false);
            if (prev_item != selected_item) {
                selected_item = prev_item;
            }
        } else if (button == 7) {
            if (g_todo_mutex) {
                xSemaphoreTake(g_todo_mutex, portMAX_DELAY);
            }
            todolist_toggle_completed_locked(selected_item);
            if (g_todo_mutex) {
                xSemaphoreGive(g_todo_mutex);
            }
            list_changed = true;
        } else if (button == 12) {
            int new_size = 0;
            if (g_todo_mutex) {
                xSemaphoreTake(g_todo_mutex, portMAX_DELAY);
            }
            todolist_delete_item_locked(selected_item);
            new_size = static_cast<int>(g_items.size());
            if (g_todo_mutex) {
                xSemaphoreGive(g_todo_mutex);
            }
            selected_item = (new_size <= 0 ? -1 : std::min(selected_item, new_size - 1));
            list_changed = true;
        } else if (button == 8 || button == 22) {
            return; // back to home
        }

        if (list_changed) {
            if (g_todo_mutex) {
                xSemaphoreTake(g_todo_mutex, portMAX_DELAY);
            }
            todolist_build_rows(rows);
            if (g_todo_mutex) {
                xSemaphoreGive(g_todo_mutex);
            }
            total_pages = (rows.size() + rows_per_page - 1) / rows_per_page;
            if (total_pages <= 0) total_pages = 1;
        }

        selected_row = todolist_find_selected_row(rows, selected_item);
        if (selected_row >= 0) {
            page_index = selected_row / rows_per_page;
        } else {
            page_index = 0;
        }

        if (page_index >= total_pages) page_index = total_pages - 1;
        if (page_index < 0) page_index = 0;

        if (current_portrait != portrait) {
            portrait = current_portrait;
            if (g_todo_mutex) {
                xSemaphoreTake(g_todo_mutex, portMAX_DELAY);
            }
            todolist_draw_page(selected_item, page_index, portrait, Global_refresh);
            if (g_todo_mutex) {
                xSemaphoreGive(g_todo_mutex);
            }
        } else if (button != -1 || force_refresh) {
            if (g_todo_mutex) {
                xSemaphoreTake(g_todo_mutex, portMAX_DELAY);
            }
            todolist_draw_page(selected_item, page_index, portrait, Partial_refresh);
            if (g_todo_mutex) {
                xSemaphoreGive(g_todo_mutex);
            }
        }
    }
}
