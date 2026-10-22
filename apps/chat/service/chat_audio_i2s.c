// LVGL-thread adapter for the system microphone/AEC service.
#include "chat_audio.h"
#include "audio_capture.h"
#include "audio_service.h"
#include "lvgl.h"
static lv_timer_t *timer;
static bool available, active, cancelled;
static chat_audio_done_cb_t pending;
static void *pending_user;

static void poll(lv_timer_t *t)
{
    if (!active || !audio_capture_done()) return;
    // Keep a naturally completed clip until stop() supplies its callback.
    if (!cancelled && !pending) return;
    chat_audio_done_cb_t cb = pending;
    void *user = pending_user;
    pending = NULL;
    size_t samples = 0;
    const int16_t *pcm = audio_capture_pcm(&samples);
    chat_audio_clip_t clip = {.pcm = pcm, .len = samples * sizeof(int16_t),
        .duration_ms = audio_capture_elapsed_ms(), .peak_level = audio_capture_level()};
    chat_audio_err_t err = audio_capture_result() == ESP_OK ? CHAT_AUDIO_OK : CHAT_AUDIO_ERR_INIT;
    if (err == CHAT_AUDIO_OK && clip.duration_ms < CHAT_REC_MIN_MS) err = CHAT_AUDIO_ERR_TOO_SHORT;
    if (!cancelled && cb) cb(&clip, err, user);
    audio_capture_release(); active = false;
}
esp_err_t chat_audio_init(void)
{
    if (!timer) timer = lv_timer_create(poll, 50, NULL);
    available = timer != NULL;
    return available ? ESP_OK : ESP_ERR_NO_MEM;
}
void chat_audio_cancel(void)
{
    pending = NULL; cancelled = true;
    if (active) audio_capture_stop();
}
void chat_audio_deinit(void)
{
    chat_audio_cancel(); available = false;
    // Keep the timer until workers have drained; no worker ever calls LVGL.
}
bool chat_audio_is_available(void) { return available; }
esp_err_t chat_audio_start(void)
{
    if (!available || active) return ESP_ERR_INVALID_STATE;
    if (audio_service_state() == AUDIO_STATE_PLAYING) audio_service_toggle();
    esp_err_t err = audio_capture_start();
    if (err == ESP_OK) { active = true; cancelled = false; pending = NULL; }
    return err;
}
esp_err_t chat_audio_stop(chat_audio_done_cb_t cb, void *user)
{
    if (!cb) return ESP_ERR_INVALID_ARG;
    if (!active) {
        chat_audio_clip_t clip = {0};
        cb(&clip, CHAT_AUDIO_ERR_TOO_SHORT, user);
        return ESP_OK;
    }
    pending = cb; pending_user = user; audio_capture_stop();
    return ESP_OK;
}
uint32_t chat_audio_elapsed_ms(void) { return active ? audio_capture_elapsed_ms() : 0; }
uint8_t chat_audio_level(void) { return active ? audio_capture_level() : 0; }
void chat_audio_mock_inject(chat_audio_err_t err) { (void)err; }
void chat_audio_mock_set_available(bool value) { (void)value; }
