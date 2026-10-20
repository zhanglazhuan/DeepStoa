// Verified ES7210 -> MMR -> AFE hardware-reference AEC pipeline.
#include "audio_capture.h"
#include "board_audio.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"

#define RATE 16000
#define MAX_SAMPLES (RATE * 60)
static const esp_afe_sr_iface_t *afe;
static esp_afe_sr_data_t *context;
static int16_t *recording, *raw, *mmr;
static atomic_bool stop_requested, producer_done, consumer_done;
static atomic_uint sample_count, peak;
static atomic_int result;
static bool active;

static void feed_task(void *arg)
{
    int frames = afe->get_feed_chunksize(context);
    while (!atomic_load(&stop_requested)) {
        if (board_audio_read(raw, frames) != ESP_OK) {
            atomic_store(&result, ESP_FAIL); atomic_store(&stop_requested, true); break;
        }
        board_audio_to_mmr(raw, mmr, frames);
        afe->feed(context, mmr);
    }
    atomic_store(&producer_done, true);
    vTaskDelete(NULL);
}
static void fetch_task(void *arg)
{
    while (true) {
        afe_fetch_result_t *r = afe->fetch_with_delay(context, pdMS_TO_TICKS(2000));
        if (r && r->ret_value != ESP_FAIL && r->data && r->data_size > 0) {
            size_t have = atomic_load(&sample_count);
            size_t n = r->data_size / sizeof(int16_t);
            if (n > MAX_SAMPLES - have) n = MAX_SAMPLES - have;
            memcpy(recording + have, r->data, n * sizeof(int16_t));
            unsigned level = 0;
            for (size_t i = 0; i < n; ++i) {
                unsigned value = abs((int)r->data[i]);
                if (value > level) level = value;
            }
            atomic_store(&peak, level * 100 / 32768);
            atomic_store(&sample_count, have + n);
            if (have + n == MAX_SAMPLES) atomic_store(&stop_requested, true);
        } else if (!atomic_load(&stop_requested)) {
            atomic_store(&result, ESP_FAIL);
            atomic_store(&stop_requested, true);
        }
        // Keep draining while feed can be blocked in AFE's ring buffer.
        if (atomic_load(&producer_done)) break;
    }
    atomic_store(&consumer_done, true);
    vTaskDelete(NULL);
}

esp_err_t audio_capture_start(void)
{
    if (active) return ESP_ERR_INVALID_STATE;
    esp_err_t err = board_audio_open(true, RATE, 1);
    if (err != ESP_OK) return err; // Another playback/capture owner keeps its session.
    active = true;
    atomic_store(&stop_requested, false); atomic_store(&producer_done, false);
    atomic_store(&consumer_done, false); atomic_store(&sample_count, 0);
    atomic_store(&peak, 0); atomic_store(&result, ESP_OK);
    afe_config_t *cfg = afe_config_init("MMR", NULL, AFE_TYPE_FD, AFE_MODE_LOW_COST);
    if (!cfg) { err = ESP_ERR_NO_MEM; goto fail; }
    cfg->aec_init = true;
    cfg->aec_mode = AEC_MODE_FD_LOW_COST;
    cfg->aec_nlp_level = AEC_NLP_LEVEL_VERYAGGR;
    cfg->ns_init = false; cfg->vad_init = false; cfg->wakenet_init = false; cfg->agc_init = false;
    cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    afe = esp_afe_handle_from_config(cfg);
    context = afe ? afe->create_from_config(cfg) : NULL;
    afe_config_free(cfg);
    if (!context) { err = ESP_ERR_NO_MEM; goto fail; }
    if (afe->get_feed_channel_num(context) != 3) { err = ESP_ERR_INVALID_STATE; goto fail; }
    size_t frames = afe->get_feed_chunksize(context);
    recording = heap_caps_malloc(MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    raw = malloc(frames * 4 * sizeof(int16_t));
    mmr = malloc(frames * 3 * sizeof(int16_t));
    if (!recording || !raw || !mmr) { err = ESP_ERR_NO_MEM; goto fail; }
    // Start the consumer first, as in t_microphone. It must never be deleted
    // while the producer can still be in feed().
    if (xTaskCreate(fetch_task, "aec_fetch", 6144, NULL, 5, NULL) != pdPASS) {
        err = ESP_ERR_NO_MEM; goto fail;
    }
    if (xTaskCreate(feed_task, "mic_feed", 6144, NULL, 5, NULL) != pdPASS) {
        atomic_store(&result, ESP_ERR_NO_MEM);
        atomic_store(&stop_requested, true); atomic_store(&producer_done, true);
        // Consumer finishes asynchronously; the UI will deliver the error.
    }
    return ESP_OK;
fail:
    atomic_store(&producer_done, true); atomic_store(&consumer_done, true);
    audio_capture_release();
    return err;
}
void audio_capture_stop(void) { atomic_store(&stop_requested, true); }
bool audio_capture_done(void)
{ return active && atomic_load(&producer_done) && atomic_load(&consumer_done); }
const int16_t *audio_capture_pcm(size_t *samples)
{ if (samples) *samples = atomic_load(&sample_count); return audio_capture_done() ? recording : NULL; }
uint32_t audio_capture_elapsed_ms(void) { return atomic_load(&sample_count) * 1000 / RATE; }
uint8_t audio_capture_level(void) { return atomic_load(&peak); }
esp_err_t audio_capture_result(void) { return atomic_load(&result); }
void audio_capture_release(void)
{
    if (!audio_capture_done()) return;
    if (context) { afe->destroy(context); context = NULL; }
    free(recording); recording = NULL; free(raw); raw = NULL; free(mmr); mmr = NULL;
    board_audio_close(); active = false;
}
