/**
 * @file alarm_service.c
 * @brief 系统级闹钟与倒计时服务实现，见 alarm_service.h。
 */

#include "alarm_service.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <flashdb.h>
#include <lvgl.h>
#include "esp_log.h"
#include "esp_timer.h"

#include "app_event.h"
#include "time_service.h"
#include "vibration_control.h"

static const char *TAG = "alarm_svc";

/* FlashDB 键。闹钟表和开关位图分开存 —— 拨一下开关只写 4 字节，
 * 不用把整张 alarm_t[19]（约 700 字节）重写一遍。 */
#define KEY_COUNT    "alm_cnt"
#define KEY_LIST     "alm_list"
#define KEY_ENABLED  "alm_en"
#define KEY_PRESETS  "alm_pre"

extern struct fdb_kvdb g_kvdb;

/* ── 状态 ─────────────────────────────────────────────────────────────── */

static alarm_t  s_alarms[ALARM_MAX_COUNT];
static uint8_t  s_count;
static uint16_t s_presets[ALARM_PRESET_COUNT];

/* 幂等：记下每条闹钟最近一次触发的“年内第几分钟”。
 * 时间服务在时区变更 / 首次对时时会补发一次 tick，同一分钟内可能来两次。 */
static int32_t  s_last_fire[ALARM_MAX_COUNT];

static struct {
    uint32_t            set_seconds;
    uint32_t            remaining_ms;
    alarm_timer_state_t state;
    int64_t             start_uptime_ms;
    lv_timer_t         *tick;      /* 只在 RUNNING 期间存在 */
} s_timer;

/* ── 持久化 ───────────────────────────────────────────────────────────── */

static void save_list(void)
{
    struct fdb_blob blob;
    fdb_kv_set_blob(&g_kvdb, KEY_COUNT, fdb_blob_make(&blob, &s_count, sizeof(s_count)));
    fdb_kv_set_blob(&g_kvdb, KEY_LIST,  fdb_blob_make(&blob, s_alarms, sizeof(s_alarms)));
}

/** 开关位图：bit i = 第 i 条闹钟是否启用。 */
static void save_enabled(void)
{
    uint32_t bits = 0;
    for (uint8_t i = 0; i < s_count && i < 32; i++) {
        if (s_alarms[i].enabled) bits |= (1u << i);
    }
    struct fdb_blob blob;
    fdb_kv_set_blob(&g_kvdb, KEY_ENABLED, fdb_blob_make(&blob, &bits, sizeof(bits)));
}

static void save_presets(void)
{
    struct fdb_blob blob;
    fdb_kv_set_blob(&g_kvdb, KEY_PRESETS, fdb_blob_make(&blob, s_presets, sizeof(s_presets)));
}

bool alarm_service_factory_reset(void)
{
    memset(s_alarms, 0, sizeof(s_alarms));

    s_alarms[0].hour    = 7;
    s_alarms[0].minute  = 0;
    s_alarms[0].repeat  = ALARM_REPEAT_WEEKDAYS;
    s_alarms[0].enabled = true;
    snprintf(s_alarms[0].label, ALARM_LABEL_LEN, "%s", "Wake Up");

    s_alarms[1].hour    = 9;
    s_alarms[1].minute  = 30;
    s_alarms[1].repeat  = ALARM_REPEAT_NONE;
    s_alarms[1].enabled = false;
    snprintf(s_alarms[1].label, ALARM_LABEL_LEN, "%s", "Meeting");

    s_count = 2;

    static const uint16_t defaults[ALARM_PRESET_COUNT] = {5, 15, 25, 45};
    memcpy(s_presets, defaults, sizeof(s_presets));

    save_list();
    save_enabled();
    save_presets();

    ESP_LOGI(TAG, "factory reset done");
    return true;
}

/**
 * 净化从 flash 读回来的数据。
 *
 * 原来的 storage.c 只检查了“读到的字节数对不对”，没有校验 count 本身 ——
 * 一次位翻转把它变成 200，UI 的循环就会越过 19 个元素的数组读下去。
 * 量产设备上掉电写入和 flash 老化都会制造这种数据，所以这里逐字段夹紧。
 */
static void sanitize(void)
{
    if (s_count > ALARM_MAX_COUNT) {
        ESP_LOGW(TAG, "alarm count %u out of range, dropping list", (unsigned)s_count);
        s_count = 0;
        memset(s_alarms, 0, sizeof(s_alarms));
        return;
    }
    for (uint8_t i = 0; i < s_count; i++) {
        alarm_t *a = &s_alarms[i];
        if (a->hour > 23)   a->hour = 0;
        if (a->minute > 59) a->minute = 0;
        a->repeat &= ALARM_REPEAT_EVERY;
        a->label[ALARM_LABEL_LEN - 1] = '\0';
    }
    for (int i = 0; i < ALARM_PRESET_COUNT; i++) {
        if (s_presets[i] == 0 || s_presets[i] > 999) s_presets[i] = 25;
    }
}

static void load(void)
{
    struct fdb_blob blob;

    size_t n = fdb_kv_get_blob(&g_kvdb, KEY_COUNT,
                               fdb_blob_make(&blob, &s_count, sizeof(s_count)));
    if (n != sizeof(s_count)) {
        ESP_LOGW(TAG, "no saved alarms, writing defaults");
        alarm_service_factory_reset();
        return;
    }

    fdb_kv_get_blob(&g_kvdb, KEY_LIST,    fdb_blob_make(&blob, s_alarms,  sizeof(s_alarms)));
    fdb_kv_get_blob(&g_kvdb, KEY_PRESETS, fdb_blob_make(&blob, s_presets, sizeof(s_presets)));

    uint32_t bits = 0;
    if (fdb_kv_get_blob(&g_kvdb, KEY_ENABLED,
                        fdb_blob_make(&blob, &bits, sizeof(bits))) == sizeof(bits)) {
        for (uint8_t i = 0; i < ALARM_MAX_COUNT && i < 32; i++) {
            s_alarms[i].enabled = (bits & (1u << i)) != 0;
        }
    }

    sanitize();
    ESP_LOGI(TAG, "loaded %u alarms", (unsigned)s_count);
}

/* ── 触发 ─────────────────────────────────────────────────────────────── */

static void fire_alarm(uint8_t idx)
{
    alarm_t *a = &s_alarms[idx];
    ESP_LOGI(TAG, "alarm %u fired: %02u:%02u %s",
             (unsigned)idx, (unsigned)a->hour, (unsigned)a->minute, a->label);

    vibration_run_pattern(VIBRATION_PATTERN_ALARM);

    app_event_alarm_t data = { .hour = a->hour, .minute = a->minute };
    snprintf(data.label, sizeof(data.label), "%s", a->label);
    app_event_fire(APP_EVENT_ALARM_FIRED, &data);

    /* 一次性闹钟响完自动关掉 */
    if (a->repeat == ALARM_REPEAT_NONE) {
        a->enabled = false;
        save_enabled();
    }
}

static void on_clock_tick(app_event_t event, const void *data)
{
    if (event != APP_EVENT_CLOCK_TICK) return;
    const app_event_clock_tick_t *t = data;

    /* 时间没对上就别响 —— 未同步时 time() 从 1970 自由走，
     * 按它触发只会在错误的时刻吵醒用户。 */
    if (!t || !t->synced) return;

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);

    int32_t stamp = (int32_t)tm.tm_yday * 1440 + tm.tm_hour * 60 + tm.tm_min;

    for (uint8_t i = 0; i < s_count; i++) {
        alarm_t *a = &s_alarms[i];
        if (!a->enabled) continue;
        if (a->hour != tm.tm_hour || a->minute != tm.tm_min) continue;
        if (a->repeat != ALARM_REPEAT_NONE &&
            !(a->repeat & (1u << tm.tm_wday))) continue;
        if (s_last_fire[i] == stamp) continue;   /* 同一分钟只响一次 */

        s_last_fire[i] = stamp;
        fire_alarm(i);
    }
}

/* ── 倒计时 ───────────────────────────────────────────────────────────── */

static void timer_stop_tick(void)
{
    if (s_timer.tick) {
        lv_timer_del(s_timer.tick);
        s_timer.tick = NULL;
    }
}

static void timer_finish(void)
{
    s_timer.remaining_ms = 0;
    s_timer.state = ALARM_TIMER_FINISHED;
    timer_stop_tick();

    ESP_LOGI(TAG, "timer finished");
    vibration_run_pattern(VIBRATION_PATTERN_ALARM);
    app_event_fire(APP_EVENT_TIMER_FINISHED, NULL);
}

static void timer_tick_cb(lv_timer_t *t)
{
    (void)t;
    if (s_timer.state != ALARM_TIMER_RUNNING) return;

    int64_t now = esp_timer_get_time() / 1000;
    int64_t elapsed = now - s_timer.start_uptime_ms;
    if (elapsed < 0) elapsed = 0;
    s_timer.start_uptime_ms = now;

    if ((uint32_t)elapsed >= s_timer.remaining_ms) {
        timer_finish();
    } else {
        s_timer.remaining_ms -= (uint32_t)elapsed;
    }
}

void alarm_service_timer_set(uint32_t seconds)
{
    timer_stop_tick();
    s_timer.set_seconds     = seconds;
    s_timer.remaining_ms    = seconds * 1000u;
    s_timer.state           = ALARM_TIMER_IDLE;
    s_timer.start_uptime_ms = 0;
}

uint32_t alarm_service_timer_set_seconds(void) { return s_timer.set_seconds; }

void alarm_service_timer_start(void)
{
    if (s_timer.remaining_ms == 0) return;
    s_timer.state           = ALARM_TIMER_RUNNING;
    s_timer.start_uptime_ms = esp_timer_get_time() / 1000;

    /* tick 按需创建：原来那个 100 ms 的定时器不管跑没跑都常驻，
     * 每秒白白唤醒 LVGL 十次。 */
    if (!s_timer.tick) {
        s_timer.tick = lv_timer_create(timer_tick_cb, 1000, NULL);
    }
}

void alarm_service_timer_pause(void)
{
    if (s_timer.state != ALARM_TIMER_RUNNING) return;

    int64_t elapsed = (esp_timer_get_time() / 1000) - s_timer.start_uptime_ms;
    if (elapsed < 0) elapsed = 0;

    if ((uint32_t)elapsed >= s_timer.remaining_ms) {
        timer_finish();
        return;
    }
    s_timer.remaining_ms -= (uint32_t)elapsed;
    s_timer.state = ALARM_TIMER_PAUSED;
    timer_stop_tick();
}

void alarm_service_timer_reset(void)
{
    timer_stop_tick();
    s_timer.remaining_ms    = s_timer.set_seconds * 1000u;
    s_timer.state           = ALARM_TIMER_IDLE;
    s_timer.start_uptime_ms = 0;
}

alarm_timer_state_t alarm_service_timer_state(void) { return s_timer.state; }

uint32_t alarm_service_timer_remaining_s(void)
{
    uint32_t ms = s_timer.remaining_ms;

    /* RUNNING 时把“上次 tick 到现在”这段也算进去，否则 UI poll 的频率
     * 比 tick 高的时候会看到数字卡住不动。 */
    if (s_timer.state == ALARM_TIMER_RUNNING) {
        int64_t elapsed = (esp_timer_get_time() / 1000) - s_timer.start_uptime_ms;
        if (elapsed < 0) elapsed = 0;
        ms = ((uint32_t)elapsed >= ms) ? 0 : ms - (uint32_t)elapsed;
    }
    return (ms + 999) / 1000;   /* 向上取整：设 5:00 立刻显示 5:00 而不是 4:59 */
}

uint8_t alarm_service_timer_progress_pct(void)
{
    if (s_timer.set_seconds == 0) return 0;
    uint32_t total = s_timer.set_seconds;
    uint32_t left  = alarm_service_timer_remaining_s();
    if (left > total) left = total;
    return (uint8_t)(((total - left) * 100u) / total);
}

/* ── 闹钟表 ───────────────────────────────────────────────────────────── */

uint8_t alarm_service_count(void) { return s_count; }

const alarm_t *alarm_service_get(uint8_t idx)
{
    return (idx < s_count) ? &s_alarms[idx] : NULL;
}

int alarm_service_add(const alarm_t *a)
{
    if (!a || s_count >= ALARM_MAX_COUNT) return -1;
    s_alarms[s_count] = *a;
    s_alarms[s_count].label[ALARM_LABEL_LEN - 1] = '\0';
    s_last_fire[s_count] = -1;
    uint8_t idx = s_count++;
    save_list();
    save_enabled();
    return idx;
}

bool alarm_service_update(uint8_t idx, const alarm_t *a)
{
    if (idx >= s_count || !a) return false;
    alarm_t *dst = &s_alarms[idx];
    dst->hour   = a->hour;
    dst->minute = a->minute;
    dst->repeat = a->repeat & ALARM_REPEAT_EVERY;
    snprintf(dst->label, ALARM_LABEL_LEN, "%s", a->label);
    s_last_fire[idx] = -1;
    save_list();
    return true;
}

bool alarm_service_remove(uint8_t idx)
{
    if (idx >= s_count) return false;
    for (uint8_t i = idx; i + 1 < s_count; i++) {
        s_alarms[i]    = s_alarms[i + 1];
        s_last_fire[i] = s_last_fire[i + 1];
    }
    s_count--;
    memset(&s_alarms[s_count], 0, sizeof(s_alarms[s_count]));
    save_list();
    save_enabled();
    return true;
}

bool alarm_service_toggle(uint8_t idx)
{
    if (idx >= s_count) return false;
    s_alarms[idx].enabled = !s_alarms[idx].enabled;
    s_last_fire[idx] = -1;
    save_enabled();          /* 只写 4 字节，不重写整张表 */
    /* 返回"操作是否成功"，和 update/remove 一致。
     * 千万不要改成 return s_alarms[idx].enabled —— 那样关闭操作会返回 false，
     * 调用方按失败处理，开关被拨回去，结果就是"能开不能关"。 */
    return true;
}

bool alarm_service_next(uint8_t *out_idx, uint32_t *out_minutes)
{
    if (!time_service_is_synced()) return false;

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    int cur = tm.tm_hour * 60 + tm.tm_min;

    uint32_t best = UINT32_MAX;
    uint8_t  best_idx = 0;

    for (uint8_t i = 0; i < s_count; i++) {
        const alarm_t *a = &s_alarms[i];
        if (!a->enabled) continue;
        int at = a->hour * 60 + a->minute;

        uint32_t delta;
        if (a->repeat == ALARM_REPEAT_NONE) {
            delta = (at > cur) ? (uint32_t)(at - cur)
                               : (uint32_t)(at + 24 * 60 - cur);
        } else {
            /* 往后找 7 天里第一个命中的星期 */
            delta = UINT32_MAX;
            for (int d = 0; d < 8; d++) {
                int wday = (tm.tm_wday + d) % 7;
                if (!(a->repeat & (1u << wday))) continue;
                int32_t cand = d * 24 * 60 + at - cur;
                if (cand <= 0) continue;
                delta = (uint32_t)cand;
                break;
            }
        }
        if (delta < best) { best = delta; best_idx = i; }
    }

    if (best == UINT32_MAX) return false;
    if (out_idx)     *out_idx = best_idx;
    if (out_minutes) *out_minutes = best;
    return true;
}

/* ── 预设 ─────────────────────────────────────────────────────────────── */

uint16_t alarm_service_preset(uint8_t i)
{
    return (i < ALARM_PRESET_COUNT) ? s_presets[i] : 0;
}

void alarm_service_set_presets(const uint16_t *minutes)
{
    if (!minutes) return;
    for (int i = 0; i < ALARM_PRESET_COUNT; i++) {
        s_presets[i] = (minutes[i] == 0 || minutes[i] > 999) ? 25 : minutes[i];
    }
    save_presets();
}

/* ── 生命周期 ─────────────────────────────────────────────────────────── */

void alarm_service_init(void)
{
    memset(&s_timer, 0, sizeof(s_timer));
    for (int i = 0; i < ALARM_MAX_COUNT; i++) s_last_fire[i] = -1;

    load();

    /* 默认 25 分钟，和番茄钟对齐 */
    alarm_service_timer_set(25 * 60);

    app_event_register(on_clock_tick);

    ESP_LOGI(TAG, "ready (%u alarms)", (unsigned)s_count);
}
