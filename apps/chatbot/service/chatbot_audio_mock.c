/**
 * @file chatbot_audio_mock.c
 * @brief chatbot_audio.h 的 mock 实现 —— 没有麦克风电路时用
 *
 * 它模拟真实驱动的时序：按住多久就报多久，短按/超长按都会走真实的错误分支。
 * 阶段 2 板子到位后新增 chatbot_audio_i2s.c，CMakeLists 换一行即可，
 * controller 和 view 不用改。
 */

#include <string.h>
#include <lvgl.h>
#include "esp_log.h"
#include "esp_timer.h"

#include "chatbot_audio.h"

static const char *TAG = "chatbot_audio_mock";

static bool     s_available = true;
static bool     s_recording = false;
static int64_t  s_start_us  = 0;
static chatbot_audio_err_t s_injected = CHATBOT_AUDIO_OK;

/* 回调要弹回 LVGL 线程 —— mock 本来就在 LVGL 线程，
 * 但仍然走 lv_async_call，好让时序和真实驱动一致（回调不在 stop() 栈里发生）。 */
typedef struct {
    chatbot_audio_done_cb_t cb;
    void                   *user_data;
    chatbot_audio_clip_t    clip;
    chatbot_audio_err_t     err;
} pending_done_t;

static pending_done_t s_pending;

static void deliver_done_cb(void *arg)
{
    (void)arg;
    if (!s_pending.cb) return;
    chatbot_audio_done_cb_t cb = s_pending.cb;
    s_pending.cb = NULL;
    cb(&s_pending.clip, s_pending.err, s_pending.user_data);
}

esp_err_t chatbot_audio_init(void)
{
    s_available = true;
    s_recording = false;
    s_injected  = CHATBOT_AUDIO_OK;
    memset(&s_pending, 0, sizeof(s_pending));
    ESP_LOGI(TAG, "mock audio ready (no real microphone)");
    return ESP_OK;
}

void chatbot_audio_deinit(void)
{
    s_recording = false;
    s_pending.cb = NULL;
}

bool chatbot_audio_is_available(void) { return s_available; }

esp_err_t chatbot_audio_start(void)
{
    if (!s_available) return ESP_ERR_INVALID_STATE;
    s_recording = true;
    s_start_us  = esp_timer_get_time();
    ESP_LOGI(TAG, "recording started");
    return ESP_OK;
}

uint32_t chatbot_audio_elapsed_ms(void)
{
    if (!s_recording) return 0;
    return (uint32_t)((esp_timer_get_time() - s_start_us) / 1000);
}

uint8_t chatbot_audio_level(void)
{
    if (!s_recording) return 0;
    /* 伪造一个随时间起伏的电平，让 UI 的音量格动起来 */
    uint32_t t = chatbot_audio_elapsed_ms() / 250;
    static const uint8_t pattern[] = {35, 62, 48, 80, 55, 25, 70, 42};
    return pattern[t % (sizeof(pattern) / sizeof(pattern[0]))];
}

void chatbot_audio_cancel(void)
{
    s_recording  = false;
    s_pending.cb = NULL;
    ESP_LOGI(TAG, "recording cancelled");
}

esp_err_t chatbot_audio_stop(chatbot_audio_done_cb_t cb, void *user_data)
{
    if (!cb) return ESP_ERR_INVALID_ARG;

    uint32_t dur = chatbot_audio_elapsed_ms();
    s_recording = false;

    chatbot_audio_err_t err = CHATBOT_AUDIO_OK;

    /* 注入的故障优先，用来逐条验证错误 UI */
    if (s_injected != CHATBOT_AUDIO_OK) {
        err = s_injected;
        s_injected = CHATBOT_AUDIO_OK;   /* 一次性 */
    } else if (dur < CHATBOT_REC_MIN_MS) {
        err = CHATBOT_AUDIO_ERR_TOO_SHORT;
    }

    memset(&s_pending, 0, sizeof(s_pending));
    s_pending.cb               = cb;
    s_pending.user_data        = user_data;
    s_pending.err              = err;
    s_pending.clip.pcm         = NULL;          /* mock 没有真实 PCM */
    s_pending.clip.len         = 0;
    s_pending.clip.duration_ms = dur;
    s_pending.clip.peak_level  = 60;

    ESP_LOGI(TAG, "recording stopped: %lums err=%d", (unsigned long)dur, (int)err);

    /* 契约 2：stop 之后必定回调一次，且不在 stop 的调用栈里 */
    lv_async_call(deliver_done_cb, NULL);
    return ESP_OK;
}

/* ── 故障注入 ─────────────────────────────────────────────────────── */

void chatbot_audio_mock_inject(chatbot_audio_err_t err)
{
    s_injected = err;
    ESP_LOGW(TAG, "next stop() will report err=%d", (int)err);
}

void chatbot_audio_mock_set_available(bool available)
{
    s_available = available;
    ESP_LOGW(TAG, "mock availability -> %d", (int)available);
}
