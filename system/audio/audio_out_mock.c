/**
 * @file audio_out_mock.c
 * @brief audio_out.h 的 mock 实现 —— 板子上没有喇叭 / codec 时用
 *
 * 用 esp_timer 的墙钟时间模拟播放位置推进，因此 UI 的进度条、快进、
 * 暂停恢复、播完自动切歌都能真实地跑起来。
 * 硬件到位后新增 audio_out_i2s.c，CMakeLists 换一行即可。
 */

#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <lvgl.h>
#include "esp_log.h"
#include "esp_timer.h"

#include "audio_out.h"
#include "audio_meta.h"

static const char *TAG = "audio_out_mock";

static bool     s_available = true;
static bool     s_playing;
static bool     s_opened;
static uint32_t s_duration_ms;
static uint32_t s_base_ms;      /* 已累计播放的毫秒 */
static int64_t  s_resume_us;    /* 本次 resume 的时间戳 */
static uint8_t  s_volume = 60;

static audio_err_t s_injected = AUDIO_OK;

static audio_out_eot_cb_t s_on_eot;
static audio_out_err_cb_t s_on_err;
static void                 *s_user_data;

static lv_timer_t *s_eot_timer;   /* 播放到头时触发 EOT */

static bool file_exists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0;
}

/* 本 mock 实现支持的格式 */
#define MOCK_SUPPORTED_EXT ".mp3,.wav"

bool audio_out_ext_supported(const char *name_or_path)
{
    if (!name_or_path) return false;
    const char *dot = strrchr(name_or_path, '.');
    if (!dot) return false;

    const char *p = MOCK_SUPPORTED_EXT;
    while (*p) {
        while (*p == ',' || *p == ' ') p++;
        const char *end = p;
        while (*end && *end != ',') end++;
        size_t len = (size_t)(end - p);
        if (len > 0 && strlen(dot) == len && strncasecmp(dot, p, len) == 0) return true;
        p = end;
    }
    return false;
}

/* 时长优先从文件头真实解析 —— 这不需要 codec，见 audio_meta.h。
 * 只有文件读不到 / 解析不出来时才退回哈希编一个，好歹让 UI 有东西显示。 */
static uint32_t duration_for(const char *path)
{
    audio_meta_t m;
    if (audio_meta_read(path, &m) && m.duration_ms > 0) return m.duration_ms;

    uint32_t h = 0;
    for (const char *p = path; *p; p++) h = h * 31u + (uint8_t)*p;
    return 45000u + (h % 180000u);   /* 45s ~ 225s */
}

static void cancel_eot_timer(void)
{
    if (s_eot_timer) { lv_timer_delete(s_eot_timer); s_eot_timer = NULL; }
}

static void eot_timer_cb(lv_timer_t *t)
{
    (void)t;
    cancel_eot_timer();

    s_playing = false;
    s_base_ms = s_duration_ms;
    ESP_LOGI(TAG, "end of track");

    if (s_on_eot) s_on_eot(s_user_data);   /* lv_timer 回调天然在 LVGL 线程 */
}

static void arm_eot_timer(void)
{
    cancel_eot_timer();
    if (!s_playing) return;

    uint32_t pos = audio_out_position_ms();
    uint32_t left = (s_duration_ms > pos) ? (s_duration_ms - pos) : 0;
    s_eot_timer = lv_timer_create(eot_timer_cb, left ? left : 1, NULL);
    lv_timer_set_repeat_count(s_eot_timer, 1);
}

esp_err_t audio_out_init(void)
{
    s_available = true;
    s_playing = s_opened = false;
    s_duration_ms = s_base_ms = 0;
    s_injected = AUDIO_OK;
    ESP_LOGI(TAG, "mock audio out ready (no real codec)");
    return ESP_OK;
}

void audio_out_deinit(void)
{
    cancel_eot_timer();
    s_playing = s_opened = false;
    s_on_eot = NULL;
    s_on_err = NULL;
}

bool audio_out_is_available(void) { return s_available; }

void audio_out_set_callbacks(audio_out_eot_cb_t on_eot,
                                audio_out_err_cb_t on_err, void *user_data)
{
    s_on_eot    = on_eot;
    s_on_err    = on_err;
    s_user_data = user_data;
}

audio_err_t audio_out_open(const char *path, uint32_t *out_duration_ms)
{
    if (!path) return AUDIO_ERR_NOT_FOUND;
    if (!s_available) return AUDIO_ERR_INIT;

    /* 注入的故障优先 */
    if (s_injected != AUDIO_OK) {
        audio_err_t e = s_injected;
        s_injected = AUDIO_OK;
        ESP_LOGW(TAG, "open %s -> injected err %d", path, (int)e);
        return e;
    }

    /* 契约 3：先校验格式再解码，UI 靠这个立刻弹窗 */
    if (!audio_out_ext_supported(path)) {
        ESP_LOGW(TAG, "unsupported format: %s", path);
        return AUDIO_ERR_FORMAT;
    }

    /* 文件不存在要如实报，否则"坏文件自动跳过"那条路走不通 */
    audio_meta_t meta;
    bool have_meta = audio_meta_read(path, &meta);
    if (!have_meta && !file_exists(path)) {
        ESP_LOGW(TAG, "not found: %s", path);
        return AUDIO_ERR_NOT_FOUND;
    }

    cancel_eot_timer();
    s_duration_ms = (have_meta && meta.duration_ms > 0) ? meta.duration_ms
                                                        : duration_for(path);
    s_base_ms     = 0;
    s_resume_us   = esp_timer_get_time();
    s_playing     = true;
    s_opened      = true;

    if (out_duration_ms) *out_duration_ms = s_duration_ms;
    ESP_LOGI(TAG, "open %s (%lums)", path, (unsigned long)s_duration_ms);

    arm_eot_timer();
    return AUDIO_OK;
}

uint32_t audio_out_position_ms(void)
{
    if (!s_opened) return 0;
    uint32_t pos = s_base_ms;
    if (s_playing) {
        pos += (uint32_t)((esp_timer_get_time() - s_resume_us) / 1000);
    }
    return pos > s_duration_ms ? s_duration_ms : pos;
}

uint32_t audio_out_duration_ms(void) { return s_duration_ms; }
bool     audio_out_is_playing(void)  { return s_playing; }

void audio_out_pause(void)
{
    if (!s_opened || !s_playing) return;
    s_base_ms = audio_out_position_ms();
    s_playing = false;
    cancel_eot_timer();
    ESP_LOGI(TAG, "pause @%lums", (unsigned long)s_base_ms);
}

void audio_out_resume(void)
{
    if (!s_opened || s_playing) return;
    s_resume_us = esp_timer_get_time();
    s_playing = true;
    arm_eot_timer();
    ESP_LOGI(TAG, "resume @%lums", (unsigned long)s_base_ms);
}

void audio_out_stop(void)
{
    cancel_eot_timer();
    s_playing = false;
    s_opened  = false;
    s_base_ms = 0;
    s_duration_ms = 0;
}

void audio_out_seek(uint32_t pos_ms)
{
    if (!s_opened) return;
    if (pos_ms > s_duration_ms) pos_ms = s_duration_ms;

    s_base_ms   = pos_ms;
    s_resume_us = esp_timer_get_time();
    arm_eot_timer();
    ESP_LOGI(TAG, "seek -> %lums", (unsigned long)pos_ms);
}

void    audio_out_set_volume(uint8_t vol) { s_volume = vol > 100 ? 100 : vol; }
uint8_t audio_out_volume(void)            { return s_volume; }

/* ── 故障注入 ─────────────────────────────────────────────────────── */

void audio_out_mock_inject(audio_err_t err)
{
    s_injected = err;
    ESP_LOGW(TAG, "next open() will report err=%d", (int)err);
}

void audio_out_mock_set_available(bool available)
{
    s_available = available;
    if (!available) { cancel_eot_timer(); s_playing = false; }
    ESP_LOGW(TAG, "mock availability -> %d", (int)available);
}
