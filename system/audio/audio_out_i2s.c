// PCM WAV playback on the verified DeepStoa codec path. All public calls
// and callbacks run on the LVGL thread; only the worker touches streaming I/O.
#include "audio_out.h"
#include "board_audio.h"
#include "pcm_wav.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lvgl.h"

static SemaphoreHandle_t lock;
static TaskHandle_t worker;
static lv_timer_t *timer;
static FILE *file;
static bool available, playing, owns_codec;
static unsigned channels, rate;
static uint32_t data_offset, data_bytes, consumed;
static atomic_uint position_ms, duration_ms;
static uint8_t volume = 55;
static audio_err_t pending_error;
static bool pending_end;
static audio_out_eot_cb_t on_end;
static audio_out_err_cb_t on_error;
static void *callback_user;

static void close_locked(void)
{
    playing = false;
    if (file) { fclose(file); file = NULL; }
    if (owns_codec) { board_audio_close(); owns_codec = false; }
}

static void playback_task(void *arg)
{
    int16_t pcm[512];
    for (;;) {
        xSemaphoreTake(lock, portMAX_DELAY);
        if (playing && file) {
            size_t bytes = data_bytes - consumed;
            if (bytes > sizeof(pcm)) bytes = sizeof(pcm);
            if (bytes == 0) {
                // Drain the DMA tail with silence before disabling the amp.
                memset(pcm, 0, sizeof(pcm));
                for (int i = 0; i < 4; ++i) {
                    if (board_audio_write(pcm, 512) != ESP_OK) { pending_error = AUDIO_ERR_IO; break; }
                }
                pending_end = pending_error == AUDIO_OK;
                close_locked();
            } else if (fread(pcm, 1, bytes, file) != bytes) {
                pending_error = AUDIO_ERR_IO;
                close_locked();
            } else {
                // ES8311 is a mono DAC. Mix both file channels so right-only
                // content is audible, then let the codec data interface pack I2S.
                size_t frames = bytes / (channels * 2);
                if (channels == 2) {
                    for (size_t i = 0; i < frames; ++i)
                        pcm[i] = ((int32_t)pcm[2 * i] + pcm[2 * i + 1]) / 2;
                }
                if (board_audio_write(pcm, frames) != ESP_OK) {
                    pending_error = AUDIO_ERR_IO; close_locked();
                } else {
                    consumed += bytes;
                    atomic_store(&position_ms, (uint64_t)consumed * 1000 / (rate * channels * 2));
                }
            }
        }
        bool busy = playing;
        xSemaphoreGive(lock);
        if (!busy) vTaskDelay(pdMS_TO_TICKS(10));
        else taskYIELD();
    }
}

static void dispatch(lv_timer_t *t)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    audio_err_t err = pending_error;
    bool end = pending_end;
    pending_error = AUDIO_OK; pending_end = false;
    xSemaphoreGive(lock);
    if (err != AUDIO_OK && on_error) on_error(err, callback_user);
    else if (end && on_end) on_end(callback_user);
}

esp_err_t audio_out_init(void)
{
    if (worker) return ESP_OK;
    lock = xSemaphoreCreateMutex();
    if (!lock) return ESP_ERR_NO_MEM;
    if (xTaskCreate(playback_task, "audio_play", 4096, NULL, 4, &worker) != pdPASS) {
        vSemaphoreDelete(lock); lock = NULL; return ESP_ERR_NO_MEM;
    }
    timer = lv_timer_create(dispatch, 50, NULL);
    if (!timer) { vTaskDelete(worker); worker = NULL; vSemaphoreDelete(lock); lock = NULL; return ESP_ERR_NO_MEM; }
    // Probe the actual DAC at startup; no fake availability or playback clock.
    esp_err_t err = board_audio_open(false, 44100, 2);
    available = err == ESP_OK;
    if (available) board_audio_close();
    return err;
}
void audio_out_stop(void)
{
    if (!lock) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    close_locked(); pending_error = AUDIO_OK; pending_end = false;
    atomic_store(&position_ms, 0); atomic_store(&duration_ms, 0);
    xSemaphoreGive(lock);
}
void audio_out_deinit(void)
{
    audio_out_stop();
    if (timer) { lv_timer_delete(timer); timer = NULL; }
    if (worker) { vTaskDelete(worker); worker = NULL; }
    if (lock) { vSemaphoreDelete(lock); lock = NULL; }
    available = false; on_end = NULL; on_error = NULL;
}
bool audio_out_is_available(void) { return available; }
void audio_out_set_callbacks(audio_out_eot_cb_t end, audio_out_err_cb_t err, void *user)
{ on_end = end; on_error = err; callback_user = user; }
bool audio_out_ext_supported(const char *path)
{ const char *dot = path ? strrchr(path, '.') : NULL; return dot && !strcasecmp(dot, ".wav"); }
audio_err_t audio_out_open(const char *path, uint32_t *duration)
{
    if (!available) return AUDIO_ERR_INIT;
    if (!path) return AUDIO_ERR_NOT_FOUND;
    if (!audio_out_ext_supported(path)) return AUDIO_ERR_FORMAT;
    FILE *next = fopen(path, "rb");
    if (!next) return AUDIO_ERR_NOT_FOUND;
    unsigned hz = 0, ch = 0; uint32_t off = 0, bytes = 0;
    if (!pcm_wav_parse(next, &hz, &ch, &off, &bytes)) { fclose(next); return AUDIO_ERR_FORMAT; }
    audio_out_stop();
    xSemaphoreTake(lock, portMAX_DELAY);
    if (board_audio_open(false, hz, 1) != ESP_OK) {
        xSemaphoreGive(lock); fclose(next); return AUDIO_ERR_INIT;
    }
    owns_codec = true;
    if (board_audio_volume(volume) != ESP_OK) {
        close_locked(); xSemaphoreGive(lock); fclose(next); return AUDIO_ERR_INIT;
    }
    file = next; rate = hz; channels = ch; data_offset = off; data_bytes = bytes; consumed = 0;
    atomic_store(&duration_ms, (uint64_t)bytes * 1000 / (hz * ch * 2));
    if (duration) *duration = atomic_load(&duration_ms);
    board_audio_set_amplifier(true); playing = true;
    xSemaphoreGive(lock);
    return AUDIO_OK;
}
void audio_out_pause(void)
{
    if (!lock) return;
    xSemaphoreTake(lock, portMAX_DELAY); playing = false;
    if (owns_codec) { board_audio_close(); owns_codec = false; }
    xSemaphoreGive(lock);
}
void audio_out_resume(void)
{
    if (!lock) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (file && !playing) {
        if (!owns_codec) {
            owns_codec = board_audio_open(false, rate, 1) == ESP_OK;
        }
        if (owns_codec && board_audio_volume(volume) == ESP_OK) {
            board_audio_set_amplifier(true); playing = true;
        } else {
            pending_error = AUDIO_ERR_INIT; close_locked();
        }
    }
    xSemaphoreGive(lock);
}
void audio_out_seek(uint32_t ms)
{
    if (!lock) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (file) {
        uint64_t frame = (uint64_t)ms * rate / 1000;
        uint64_t bytes = frame * channels * 2;
        if (bytes > data_bytes) bytes = data_bytes;
        if (!fseek(file, data_offset + (long)bytes, SEEK_SET)) {
            consumed = bytes;
            atomic_store(&position_ms, (uint64_t)consumed * 1000 / (rate * channels * 2));
        } else { pending_error = AUDIO_ERR_IO; close_locked(); }
    }
    xSemaphoreGive(lock);
}
uint32_t audio_out_position_ms(void) { return atomic_load(&position_ms); }
uint32_t audio_out_duration_ms(void) { return atomic_load(&duration_ms); }
bool audio_out_is_playing(void)
{
    if (!lock) return false;
    xSemaphoreTake(lock, portMAX_DELAY); bool result = playing; xSemaphoreGive(lock); return result;
}
void audio_out_set_volume(uint8_t v)
{
    volume = v > 100 ? 100 : v;
    if (!lock) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (owns_codec) board_audio_volume(volume);
    xSemaphoreGive(lock);
}
uint8_t audio_out_volume(void) { return volume; }
void audio_out_mock_inject(audio_err_t err) { (void)err; }
void audio_out_mock_set_available(bool value) { (void)value; }
