/*
 * DeepStoa —— 系统时间服务实现，见 time_service.h。
 */

#include "time_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_event.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "flash_store.h"

static const char *TAG = "time_svc";

/* NVS：时区和格式由本服务独占持久化，Settings 只是调用方。
 * 放 NVS 而不是 FlashDB，是因为本服务在 app 之前就要用到这两个值。 */
#define TS_NS         "settings"
#define KEY_TZ_POSIX  "tz_posix"
#define KEY_FMT24     "fmt24"

#define TZ_DEFAULT    "UTC-8"   /* 北京时间；POSIX 的符号与 UTC 偏移相反 */

/* ── 状态 ─────────────────────────────────────────────────────────────── */

static bool        s_synced = false;
static bool        s_fmt24  = true;
static char        s_tz[40] = TZ_DEFAULT;
static lv_timer_t *s_timer  = NULL;
static int         s_last_fired_minute = -1;
static bool        s_sntp_started = false;

/* ── 内部 ─────────────────────────────────────────────────────────────── */

static int hour_in_display_format(int tm_hour)
{
    if (s_fmt24) return tm_hour;
    return (tm_hour % 12 == 0) ? 12 : (tm_hour % 12);
}

static void fire_tick(int hour, int minute)
{
    app_event_clock_tick_t data = {
        .hour   = hour,
        .minute = minute,
        .synced = s_synced,
    };
    app_event_fire(APP_EVENT_CLOCK_TICK, &data);
}

static void fire_immediate_tick(void)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    fire_tick(hour_in_display_format(tm.tm_hour), tm.tm_min);
}

static void apply_timezone(void)
{
    setenv("TZ", s_tz, 1);
    tzset();
}

/* ── WiFi 连上 → 触发对时 ─────────────────────────────────────────────── */

static void on_app_event(app_event_t event, const void *data)
{
    (void)data;
    if (event == APP_EVENT_WIFI_CONNECTED && !s_sntp_started) {
        s_sntp_started = true;
        time_service_sync_sntp();
    }
}

static void on_sntp_sync(struct timeval *tv)
{
    bool first = !s_synced;
    s_synced = true;
    ESP_LOGI(TAG, "SNTP synced — epoch=%lld", (long long)tv->tv_sec);
    /* 第一次对上时，时间可能跳很远 —— 立刻广播一次，让状态栏和闹钟服务
     * 不用等到下一个整分才发现时间变了。 */
    if (first) fire_immediate_tick();
}

/* ── 1 秒定时器：按分钟发 tick ────────────────────────────────────────── */

static void tick_cb(lv_timer_t *t)
{
    (void)t;

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);

    if (tm.tm_min == s_last_fired_minute) return;   /* 同一分钟内不重复发 */
    s_last_fired_minute = tm.tm_min;

    fire_tick(hour_in_display_format(tm.tm_hour), tm.tm_min);
}

/* ── 公开 API ─────────────────────────────────────────────────────────── */

void time_service_init(void)
{
    flash_store_init();   /* 幂等 */

    /* 先恢复设置再建定时器 —— 保证第一个 tick 就是对的时区和格式 */
    flash_get_str(TS_NS, KEY_TZ_POSIX, s_tz, sizeof(s_tz), TZ_DEFAULT);
    if (s_tz[0] == '\0') snprintf(s_tz, sizeof(s_tz), "%s", TZ_DEFAULT);
    s_fmt24 = flash_get_bool(TS_NS, KEY_FMT24, true);
    apply_timezone();

    app_event_register(on_app_event);

    s_timer = lv_timer_create(tick_cb, 1000, NULL);
    lv_timer_set_repeat_count(s_timer, -1);

    ESP_LOGI(TAG, "Initialized (tz=%s, fmt=%s, synced=%d)",
             s_tz, s_fmt24 ? "24h" : "12h", (int)s_synced);
}

void time_service_set_timezone_posix(const char *posix_tz)
{
    if (!posix_tz || !posix_tz[0]) return;
    snprintf(s_tz, sizeof(s_tz), "%s", posix_tz);
    apply_timezone();
    flash_set_str(TS_NS, KEY_TZ_POSIX, s_tz);
    s_last_fired_minute = -1;      /* 时区变了，分钟数可能没变但显示要刷新 */
    fire_immediate_tick();
    ESP_LOGI(TAG, "Timezone -> %s", s_tz);
}

void time_service_set_format_24h(bool fmt24)
{
    s_fmt24 = fmt24;
    flash_set_bool(TS_NS, KEY_FMT24, fmt24);
    s_last_fired_minute = -1;
    fire_immediate_tick();
    ESP_LOGI(TAG, "Format -> %s", fmt24 ? "24h" : "12h");
}

bool time_service_get_format_24h(void)
{
    return s_fmt24;
}

int time_service_get_hour(void)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    return hour_in_display_format(tm.tm_hour);
}

int time_service_get_minute(void)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    return tm.tm_min;
}

int time_service_get_second(void)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    return tm.tm_sec;
}

bool time_service_is_synced(void)
{
    return s_synced;
}

void time_service_sync_sntp(void)
{
    ESP_LOGI(TAG, "Starting SNTP sync...");
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    sntp_set_time_sync_notification_cb(on_sntp_sync);
    esp_sntp_init();
}
