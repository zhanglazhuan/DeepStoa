#include <stdio.h>
#include <string.h>
#include "../theme/lv_theme_hardcore.h"
#include "../utils/ui_fonts.h"
#include "../utils/ui_utils.h"
#include "lv_status_bar.h"
#include "audio_service.h"
#include "time_service.h"
#include "app_event.h"

// 使用工程专属的宏来声明图片（兼容内置和外部Flash）
EPOS_LV_IMG_DECLARE(alarm_on);
EPOS_LV_IMG_DECLARE(battery_0);
EPOS_LV_IMG_DECLARE(battery_1);
EPOS_LV_IMG_DECLARE(battery_2);
EPOS_LV_IMG_DECLARE(battery_3);
EPOS_LV_IMG_DECLARE(battery_4);
EPOS_LV_IMG_DECLARE(battery_5);
EPOS_LV_IMG_DECLARE(battery_6);
EPOS_LV_IMG_DECLARE(battery_bolt);
EPOS_LV_IMG_DECLARE(battery_full_bolt);
EPOS_LV_IMG_DECLARE(battery_full);
EPOS_LV_IMG_DECLARE(wifi_on);

// 内部持有全局句柄
typedef struct {
    lv_obj_t * status_bar;
    lv_obj_t * time_lbl;

    // 使用容器统一管理右侧组件
    lv_obj_t * right_cont;
    lv_obj_t * wifi_img;
    lv_obj_t * alarm_img;
    lv_obj_t * bat_lbl;    // 电量百分比数字
    lv_obj_t * bat_img;    // 电池图片
    lv_obj_t * play_lbl;   // 后台播放指示

    lv_font_t  merged_right_font;
} status_bar_t;

static status_bar_t g_status_bar = {0};

/* ---------------- 数据源 ----------------
 * 时间来自 system/timeservice（RTC + SNTP）。没对上时显示 --:--，
 * 宁可让用户看到"还不知道几点"，也不要显示一个看起来像真的假时间。 */
static void get_time_text(char * buf, size_t len) {
    if (!time_service_is_synced()) {
        snprintf(buf, len, "--:--");
        return;
    }
    snprintf(buf, len, "%02d:%02d",
             time_service_get_hour(), time_service_get_minute());
}

// 注意：如果你的真实业务逻辑返回了 false，这里图标就会隐藏
static bool mock_is_wifi_on(void) {
    return true;
}

static bool mock_is_alarm_on(void) {
    return true;
}

static int mock_get_battery_level(void) {
    return 85;
}

static bool mock_is_charging(void) {
    return false;
}

/* ---------------- 后台播放指示 ----------------
 * 播放会话是系统级的（system/audio），退出播放器 app 音乐还在放。
 * 那就必须有个全局可见的提示，否则用户在别的界面里完全不知道在放什么。
 * 这里只做提示，不做控制 —— 从状态栏拉起 app 需要把 launcher 的
 * close_cb/root/group 透传进来，为一个图标做这种跨层耦合不划算。
 *
 * 状态栏是常驻对象，永远不会被销毁，所以订阅之后不需要 unsubscribe。 */
static void update_play_indicator(void) {
    if (!g_status_bar.play_lbl) return;

    audio_state_t st = audio_service_state();
    if (st == AUDIO_STATE_PLAYING) {
        lv_label_set_text(g_status_bar.play_lbl, LV_SYMBOL_PLAY);
        lv_obj_remove_flag(g_status_bar.play_lbl, LV_OBJ_FLAG_HIDDEN);
    } else if (st == AUDIO_STATE_PAUSED) {
        lv_label_set_text(g_status_bar.play_lbl, LV_SYMBOL_PAUSE);
        lv_obj_remove_flag(g_status_bar.play_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_status_bar.play_lbl, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_audio_event(audio_event_t ev, audio_err_t err, void *user_data) {
    (void)err; (void)user_data;
    if (ev == AUDIO_EV_STATE_CHANGED || ev == AUDIO_EV_TRACK_CHANGED) {
        update_play_indicator();
    }
}

/* ---------------- 状态更新逻辑 ---------------- */
void lv_status_bar_update(void) {
    if (!g_status_bar.right_cont) return;

    // 1. 更新 WIFI 显示状态
    if (mock_is_wifi_on()) {
        lv_obj_clear_flag(g_status_bar.wifi_img, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_status_bar.wifi_img, LV_OBJ_FLAG_HIDDEN);
    }

    // 2. 更新闹钟显示状态
    if (mock_is_alarm_on()) {
        lv_obj_clear_flag(g_status_bar.alarm_img, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_status_bar.alarm_img, LV_OBJ_FLAG_HIDDEN);
    }

    update_play_indicator();

    // 3. 更新电池文字和图标状态 (使用 fmt 接口更安全)
    int bat_level = mock_get_battery_level();
    bool is_charging = mock_is_charging();
    lv_label_set_text_fmt(g_status_bar.bat_lbl, "%d%%", bat_level);

    // 根据电量和充电状态选择合适的电池图标
    const void * bat_img_src;
    if (is_charging) {
        if (bat_level >= 95) {
            bat_img_src = EPOS_LV_IMG_USE(battery_full_bolt);
        } else if (bat_level >= 80) {
            bat_img_src = EPOS_LV_IMG_USE(battery_6);
        } else if (bat_level >= 65) {
            bat_img_src = EPOS_LV_IMG_USE(battery_5);
        } else if (bat_level >= 50) {
            bat_img_src = EPOS_LV_IMG_USE(battery_4);
        } else if (bat_level >= 35) {
            bat_img_src = EPOS_LV_IMG_USE(battery_3);
        } else if (bat_level >= 20) {
            bat_img_src = EPOS_LV_IMG_USE(battery_2);
        } else if (bat_level >= 10) {
            bat_img_src = EPOS_LV_IMG_USE(battery_1);
        } else {
            bat_img_src = EPOS_LV_IMG_USE(battery_bolt);
        }
    } else {
        if (bat_level >= 95) {
            bat_img_src = EPOS_LV_IMG_USE(battery_full);
        } else if (bat_level >= 80) {
            bat_img_src = EPOS_LV_IMG_USE(battery_6);
        } else if (bat_level >= 65) {
            bat_img_src = EPOS_LV_IMG_USE(battery_5);
        } else if (bat_level >= 50) {
            bat_img_src = EPOS_LV_IMG_USE(battery_4);
        } else if (bat_level >= 35) {
            bat_img_src = EPOS_LV_IMG_USE(battery_3);
        } else if (bat_level >= 20) {
            bat_img_src = EPOS_LV_IMG_USE(battery_2);
        } else if (bat_level >= 10) {
            bat_img_src = EPOS_LV_IMG_USE(battery_1);
        } else {
            bat_img_src = EPOS_LV_IMG_USE(battery_0);
        }
    }

    // 挂载电池图片，使用宏兼容外部存储
    lv_image_set_src(g_status_bar.bat_img, bat_img_src);
}

/* 只在文本真的变了的时候才重设 label。
 * lv_label_set_text() 不比较旧值，每次都会 realloc + 标脏，而状态栏在
 * lv_layer_top 上 —— 一次标脏就是一次整屏 PARTIAL_ALL 推送。分钟没变
 * 却照样刷，等于全天候每分钟白闪一次屏。 */
static void refresh_time_label(void) {
    if (!g_status_bar.time_lbl) return;

    char time_str[10];
    get_time_text(time_str, sizeof(time_str));

    const char * cur = lv_label_get_text(g_status_bar.time_lbl);
    if (cur && strcmp(cur, time_str) == 0) return;

    lv_label_set_text(g_status_bar.time_lbl, time_str);
}

/* 时间服务每分钟广播一次 tick，状态栏跟着它走，不再自己轮询。 */
static void on_clock_event(app_event_t event, const void * data) {
    (void)data;
    if (event == APP_EVENT_CLOCK_TICK) refresh_time_label();
}

/* ---------------- UI 初始化逻辑 ---------------- */
void lv_status_bar_init(void) {
    if (g_status_bar.status_bar) return; // 防止重复初始化

    lv_obj_t * sys_layer = lv_layer_top();

    g_status_bar.status_bar = lv_obj_create(sys_layer);
    lv_obj_remove_style_all(g_status_bar.status_bar);
    lv_obj_set_width(g_status_bar.status_bar, LV_PCT(100));
    lv_obj_set_height(g_status_bar.status_bar, 40);
    lv_obj_set_style_text_font(g_status_bar.status_bar, LV_FONT_SMALL, 0);
    lv_obj_align(g_status_bar.status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_border_width(g_status_bar.status_bar, 1, 0);
    lv_obj_set_style_border_side(g_status_bar.status_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(g_status_bar.status_bar, lv_color_black(), 0);
    lv_obj_set_style_pad_top(g_status_bar.status_bar, 8, 0);
    lv_obj_set_style_pad_bottom(g_status_bar.status_bar, 2, 0);
    lv_obj_set_style_pad_left(g_status_bar.status_bar, 10, 0);
    lv_obj_set_style_pad_right(g_status_bar.status_bar, 10, 0);

    // --- 左侧：时间 ---
    g_status_bar.time_lbl = lv_label_create(g_status_bar.status_bar);
    lv_obj_align(g_status_bar.time_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    // --- 右侧：状态区域 (修复截断和覆盖的核心) ---
    g_status_bar.right_cont = lv_obj_create(g_status_bar.status_bar);
    lv_obj_remove_style_all(g_status_bar.right_cont);

    // 【修改点】：给定固定的 60% 宽度，放弃 LV_SIZE_CONTENT 以防止文本变长时组件被挤出屏幕
    lv_obj_set_size(g_status_bar.right_cont, LV_PCT(60), LV_PCT(100));
    lv_obj_align(g_status_bar.right_cont, LV_ALIGN_RIGHT_MID, 0, 0);

    // 设置 Flex 布局 (横向排列，子元素在固定容器内往右对靠)
    lv_obj_set_flex_flow(g_status_bar.right_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_status_bar.right_cont, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(g_status_bar.right_cont, 8, 0); // 各个图标间隙 8px

    // --- 依次添加右侧元素 ---

    // 0. 后台播放指示（默认隐藏）
    g_status_bar.play_lbl = lv_label_create(g_status_bar.right_cont);
    lv_label_set_text(g_status_bar.play_lbl, LV_SYMBOL_PLAY);
    lv_obj_add_flag(g_status_bar.play_lbl, LV_OBJ_FLAG_HIDDEN);

    // 1. WiFi 图标
    g_status_bar.wifi_img = lv_image_create(g_status_bar.right_cont);
    lv_image_set_src(g_status_bar.wifi_img, EPOS_LV_IMG_USE(wifi_on));

    // 2. 闹钟图标
    g_status_bar.alarm_img = lv_image_create(g_status_bar.right_cont);
    lv_image_set_src(g_status_bar.alarm_img, EPOS_LV_IMG_USE(alarm_on));

    // 3. 电池百分比文字
    g_status_bar.bat_lbl = lv_label_create(g_status_bar.right_cont);

    const lv_font_t * base_font = lv_obj_get_style_text_font(g_status_bar.status_bar, 0);
    g_status_bar.merged_right_font = *base_font;
    g_status_bar.merged_right_font.fallback = custom_font_normal.fallback;
    lv_obj_set_style_text_font(g_status_bar.bat_lbl, &g_status_bar.merged_right_font, 0);

    // 4. 电池图标
    g_status_bar.bat_img = lv_image_create(g_status_bar.right_cont);
    lv_image_set_src(g_status_bar.bat_img, EPOS_LV_IMG_USE(battery_0));

    // 初始刷一次数据
    refresh_time_label();
    audio_service_subscribe(on_audio_event, NULL);
    lv_status_bar_update();

    /* 时间由 APP_EVENT_CLOCK_TICK 驱动（每分钟一次），不再起本地定时器。
     * 状态栏是常驻对象，不会被销毁，所以订阅之后不需要注销。 */
    app_event_register(on_clock_event);
}

// 隐藏或显示状态栏
void lv_status_bar_set_visible(bool visible) {
    if (!g_status_bar.status_bar) return;

    if (visible) {
        lv_obj_clear_flag(g_status_bar.status_bar, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_status_bar.status_bar, LV_OBJ_FLAG_HIDDEN);
    }
}

// 可选：获取状态栏高度
lv_coord_t lv_status_bar_get_height(void) {
    if (!g_status_bar.status_bar || lv_obj_has_flag(g_status_bar.status_bar, LV_OBJ_FLAG_HIDDEN)) {
        return 0;
    }
    return lv_obj_get_height(g_status_bar.status_bar);
}
