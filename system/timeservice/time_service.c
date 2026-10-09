/*
 * DeepStoa —— 系统时间服务实现，见 time_service.h。
 */

#include "time_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "app_event.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "flash_store.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "time_svc";

/* NVS：时区和格式由本服务独占持久化，Settings 只是调用方。
 * 放 NVS 而不是 FlashDB，是因为本服务在 app 之前就要用到这两个值。 */
#define TS_NS         "settings"
#define KEY_TZ_POSIX  "tz_posix"
#define KEY_FMT24     "fmt24"

#define TIME_NS       "timesvc"
#define KEY_TIME_ANCHOR "anchor"
#define TIME_ANCHOR_MAGIC UINT32_C(0x54494D45)
#define TIME_ANCHOR_VERSION 1
#define TIME_VALID_EPOCH UINT32_C(1704067200)
#define TIME_CHECKPOINT_INTERVAL UINT32_C(900)
#define TIME_BACKWARD_TOLERANCE UINT32_C(300)

#define TZ_DEFAULT    "UTC-8"   /* 北京时间；POSIX 的符号与 UTC 偏移相反 */

/* ── 状态 ─────────────────────────────────────────────────────────────── */

static bool        s_synced = false;
static bool        s_fmt24  = true;
static char        s_tz[40] = TZ_DEFAULT;
static lv_timer_t *s_timer  = NULL;
static int         s_last_fired_minute = -1;
static bool        s_sntp_started = false;
static bool        s_time_conflict = false;
static time_service_state_t s_time_state = TIME_STATE_INVALID;
static uint32_t    s_last_trusted_epoch = 0;
static uint32_t    s_last_safe_epoch = 0;
static uint32_t    s_persisted_safe_epoch = 0;
static int64_t     s_anchor_uptime_us = 0;
static portMUX_TYPE s_time_lock = portMUX_INITIALIZER_UNLOCKED;
static time_service_rtc_backend_t s_rtc_backend;
static bool        s_rtc_backend_set = false;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t last_trusted_epoch;
    uint32_t last_safe_epoch;
} time_anchor_t;

static bool valid_epoch(int64_t epoch)
{
    return epoch >= TIME_VALID_EPOCH && (uint64_t)epoch <= UINT32_MAX;
}

static bool persist_anchor(void)
{
    time_anchor_t anchor;
    portENTER_CRITICAL(&s_time_lock);
    anchor = (time_anchor_t) {
        .magic = TIME_ANCHOR_MAGIC,
        .version = TIME_ANCHOR_VERSION,
        .last_trusted_epoch = s_last_trusted_epoch,
        .last_safe_epoch = s_last_safe_epoch,
    };
    portEXIT_CRITICAL(&s_time_lock);
    if (!flash_set_blob(TIME_NS, KEY_TIME_ANCHOR, &anchor, sizeof(anchor))) {
        ESP_LOGE(TAG, "Failed to persist time anchor");
        return false;
    }
    portENTER_CRITICAL(&s_time_lock);
    if (s_persisted_safe_epoch < anchor.last_safe_epoch) {
        s_persisted_safe_epoch = anchor.last_safe_epoch;
    }
    portEXIT_CRITICAL(&s_time_lock);
    return true;
}

static bool service_epoch(uint32_t *epoch)
{
    if (!epoch) return false;
    time_service_state_t state;
    uint32_t safe;
    int64_t anchor_uptime;
    portENTER_CRITICAL(&s_time_lock);
    state = s_time_state;
    safe = s_last_safe_epoch;
    anchor_uptime = s_anchor_uptime_us;
    portEXIT_CRITICAL(&s_time_lock);
    if (state == TIME_STATE_INVALID) return false;
    uint64_t value;
    if (state == TIME_STATE_TRUSTED) {
        time_t now = time(NULL);
        if (!valid_epoch((int64_t)now)) return false;
        value = (uint64_t)now;
    } else {
        int64_t elapsed_us = esp_timer_get_time() - anchor_uptime;
        if (elapsed_us < 0) elapsed_us = 0;
        value = (uint64_t)safe + (uint64_t)elapsed_us / 1000000U;
    }
    if (value < safe) value = safe;
    if (value > UINT32_MAX) value = UINT32_MAX;
    *epoch = (uint32_t)value;
    return true;
}
/* ── 内部 ─────────────────────────────────────────────────────────────── */

static int hour_in_display_format(int tm_hour)
{
    if (s_fmt24) return tm_hour;
    return (tm_hour % 12 == 0) ? 12 : (tm_hour % 12);
}

static void fire_tick(int hour, int minute)
{
    portENTER_CRITICAL(&s_time_lock);
    bool synced = s_synced;
    portEXIT_CRITICAL(&s_time_lock);
    app_event_clock_tick_t data = {
        .hour   = hour,
        .minute = minute,
        .synced = synced,
    };
    app_event_fire(APP_EVENT_CLOCK_TICK, &data);
}

static void fire_immediate_tick(void)
{
    uint32_t epoch;
    if (!service_epoch(&epoch)) {
        fire_tick(0, 0);
        return;
    }
    time_t now = (time_t)epoch;
    struct tm tm;
    localtime_r(&now, &tm);
    fire_tick(hour_in_display_format(tm.tm_hour), tm.tm_min);
}

static void apply_timezone(void)
{
    setenv("TZ", s_tz, 1);
    tzset();
}

static void try_restore_from_rtc(void)
{
    if (!s_rtc_backend_set || !s_rtc_backend.read_utc) return;

    time_t utc = 0;
    if (!s_rtc_backend.read_utc(&utc, s_rtc_backend.user_data) || utc <= 0) {
        ESP_LOGW(TAG, "RTC backend has no valid time; keeping system clock");
        return;
    }

    struct timeval tv = { .tv_sec = utc, .tv_usec = 0 };
    if (settimeofday(&tv, NULL) != 0) {
        ESP_LOGW(TAG, "RTC time could not be applied to system clock");
        return;
    }
    s_synced = true;
    ESP_LOGI(TAG, "Restored time from RTC (epoch=%lld)", (long long)utc);
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
    if (!tv || !valid_epoch(tv->tv_sec)) {
        ESP_LOGW(TAG, "Ignoring invalid SNTP time");
        return;
    }
    uint32_t candidate = (uint32_t)tv->tv_sec;
    portENTER_CRITICAL(&s_time_lock);
    uint32_t safe = s_last_safe_epoch;
    bool conflict = candidate < safe &&
        safe - candidate > TIME_BACKWARD_TOLERANCE;
    if (conflict) {
        s_synced = false;
        s_time_conflict = true;
        s_time_state = safe
            ? TIME_STATE_ESTIMATED : TIME_STATE_INVALID;
        portEXIT_CRITICAL(&s_time_lock);
        ESP_LOGE(TAG, "SNTP time is behind persisted application time");
        fire_immediate_tick();
        return;
    }
    s_synced = true;
    s_time_conflict = false;
    s_time_state = TIME_STATE_TRUSTED;
    s_last_trusted_epoch = candidate;
    if (s_last_safe_epoch < s_last_trusted_epoch) {
        s_last_safe_epoch = s_last_trusted_epoch;
    }
    s_anchor_uptime_us = esp_timer_get_time();
    portEXIT_CRITICAL(&s_time_lock);
    persist_anchor();
    ESP_LOGI(TAG, "SNTP synced — epoch=%lld", (long long)tv->tv_sec);

    /* SNTP is the authoritative network source.  Persist the result in the
     * optional RTC as well, so the next offline boot can restore a useful
     * wall clock.  A failing/absent RTC must not affect the already-successful
     * SNTP path. */
    if (s_rtc_backend_set && s_rtc_backend.write_utc &&
        !s_rtc_backend.write_utc(tv->tv_sec, s_rtc_backend.user_data)) {
        ESP_LOGW(TAG, "SNTP synced, but RTC write-back failed");
    }

    /* 第一次对上时，时间可能跳很远 —— 立刻广播一次，让状态栏和闹钟服务
     * 不用等到下一个整分才发现时间变了。 */
    fire_immediate_tick();
}

/* ── 1 秒定时器：按分钟发 tick ────────────────────────────────────────── */

static void tick_cb(lv_timer_t *t)
{
    (void)t;

    uint32_t epoch;
    if (!service_epoch(&epoch)) {
        if (s_last_fired_minute != -2) {
            s_last_fired_minute = -2;
            fire_tick(0, 0);
        }
        return;
    }
    time_t now = (time_t)epoch;
    struct tm tm;
    localtime_r(&now, &tm);

    if (tm.tm_min == s_last_fired_minute) return;   /* 同一分钟内不重复发 */
    s_last_fired_minute = tm.tm_min;

    fire_tick(hour_in_display_format(tm.tm_hour), tm.tm_min);
}

/* ── 公开 API ─────────────────────────────────────────────────────────── */

void time_service_set_rtc_backend(const time_service_rtc_backend_t *backend)
{
    if (!backend) {
        memset(&s_rtc_backend, 0, sizeof(s_rtc_backend));
        s_rtc_backend_set = false;
        return;
    }

    s_rtc_backend = *backend;
    s_rtc_backend_set = s_rtc_backend.read_utc != NULL;
}

void time_service_init(void)
{
    flash_store_init();   /* 幂等 */
    s_synced = false;

    /* 先恢复设置再建定时器 —— 保证第一个 tick 就是对的时区和格式 */
    flash_get_str(TS_NS, KEY_TZ_POSIX, s_tz, sizeof(s_tz), TZ_DEFAULT);
    if (s_tz[0] == '\0') snprintf(s_tz, sizeof(s_tz), "%s", TZ_DEFAULT);
    s_fmt24 = flash_get_bool(TS_NS, KEY_FMT24, true);
    apply_timezone();
    try_restore_from_rtc();

    time_anchor_t anchor = {0};
    if (flash_get_blob(TIME_NS, KEY_TIME_ANCHOR, &anchor, sizeof(anchor)) &&
        anchor.magic == TIME_ANCHOR_MAGIC &&
        anchor.version == TIME_ANCHOR_VERSION &&
        valid_epoch(anchor.last_safe_epoch) &&
        (!anchor.last_trusted_epoch ||
         (valid_epoch(anchor.last_trusted_epoch) &&
          anchor.last_trusted_epoch <= anchor.last_safe_epoch))) {
        s_last_trusted_epoch = anchor.last_trusted_epoch;
        s_last_safe_epoch = anchor.last_safe_epoch;
        s_persisted_safe_epoch = anchor.last_safe_epoch;
        s_anchor_uptime_us = esp_timer_get_time();
        s_time_state = TIME_STATE_ESTIMATED;
        s_time_conflict = false;
    } else {
        s_time_state = TIME_STATE_INVALID;
        s_time_conflict = false;
        s_last_trusted_epoch = 0;
        s_last_safe_epoch = 0;
        s_persisted_safe_epoch = 0;
        s_anchor_uptime_us = esp_timer_get_time();
    }

    app_event_register(on_app_event);

    s_timer = lv_timer_create(tick_cb, 1000, NULL);
    lv_timer_set_repeat_count(s_timer, -1);

    ESP_LOGI(TAG, "Initialized (tz=%s, fmt=%s, time_state=%d)",
             s_tz, s_fmt24 ? "24h" : "12h", (int)s_time_state);
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
    uint32_t epoch;
    if (!service_epoch(&epoch)) return 0;
    time_t now = (time_t)epoch;
    struct tm tm;
    localtime_r(&now, &tm);
    return hour_in_display_format(tm.tm_hour);
}

int time_service_get_minute(void)
{
    uint32_t epoch;
    if (!service_epoch(&epoch)) return 0;
    time_t now = (time_t)epoch;
    struct tm tm;
    localtime_r(&now, &tm);
    return tm.tm_min;
}

int time_service_get_second(void)
{
    uint32_t epoch;
    if (!service_epoch(&epoch)) return 0;
    time_t now = (time_t)epoch;
    struct tm tm;
    localtime_r(&now, &tm);
    return tm.tm_sec;
}

bool time_service_is_synced(void)
{
    return time_service_get_state() == TIME_STATE_TRUSTED;
}

time_service_state_t time_service_get_state(void)
{
    portENTER_CRITICAL(&s_time_lock);
    time_service_state_t state = s_time_state;
    portEXIT_CRITICAL(&s_time_lock);
    return state;
}

bool time_service_has_conflict(void)
{
    portENTER_CRITICAL(&s_time_lock);
    bool conflict = s_time_conflict;
    portEXIT_CRITICAL(&s_time_lock);
    return conflict;
}

bool time_service_now(uint32_t *epoch)
{
    return service_epoch(epoch);
}

uint64_t time_service_monotonic_ms(void)
{
    int64_t uptime = esp_timer_get_time();
    return uptime > 0 ? (uint64_t)uptime / 1000U : 0;
}

void time_service_checkpoint(uint32_t epoch)
{
    if (!valid_epoch(epoch)) return;
    bool should_persist = false;
    portENTER_CRITICAL(&s_time_lock);
    if (epoch <= s_last_safe_epoch) {
        portEXIT_CRITICAL(&s_time_lock);
        return;
    }
    s_last_safe_epoch = epoch;
    s_anchor_uptime_us = esp_timer_get_time();
    should_persist = s_persisted_safe_epoch == 0 ||
        epoch - s_persisted_safe_epoch >= TIME_CHECKPOINT_INTERVAL;
    portEXIT_CRITICAL(&s_time_lock);
    if (should_persist) persist_anchor();
}

bool time_service_set_manual(uint32_t epoch)
{
    if (!valid_epoch(epoch)) return false;
    portENTER_CRITICAL(&s_time_lock);
    uint32_t safe = s_last_safe_epoch;
    portEXIT_CRITICAL(&s_time_lock);
    if (safe && epoch < safe &&
        safe - epoch > TIME_BACKWARD_TOLERANCE) return false;
    struct timeval value = { .tv_sec = (time_t)epoch, .tv_usec = 0 };
    if (settimeofday(&value, NULL) != 0) return false;
    portENTER_CRITICAL(&s_time_lock);
    s_synced = true;
    s_time_conflict = false;
    s_time_state = TIME_STATE_TRUSTED;
    s_last_trusted_epoch = epoch;
    if (s_last_safe_epoch < epoch) s_last_safe_epoch = epoch;
    s_anchor_uptime_us = esp_timer_get_time();
    portEXIT_CRITICAL(&s_time_lock);
    bool persisted = persist_anchor();
    s_last_fired_minute = -1;
    fire_immediate_tick();
    ESP_LOGI(TAG, "Time set manually — epoch=%u", (unsigned)epoch);
    return persisted;
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
