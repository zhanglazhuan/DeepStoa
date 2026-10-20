// Hardware routing: design/墨水屏-原理图V1.0.pdf, audio sheet.
// Architecture reference: xiaozhi-esp32 BoxAudioCodec / AfeAudioEngine.
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "aw9523.h"
#include "deepstoa_v1.h"

#define RATE 16000
#define RECORD_SECONDS 20
#define RECORD_SAMPLES (RATE * RECORD_SECONDS)
#define CAPTURE_STOPPED BIT0
#define RECORD_READY BIT1
#define PLAYBACK_STOPPED BIT2
static const char *TAG = "t_microphone";
static esp_codec_dev_handle_t input, output;
static const esp_afe_sr_iface_t *afe;
static esp_afe_sr_data_t *afe_data;
static int16_t *recording;
static atomic_bool stop_capture;
static EventGroupHandle_t events;

static void audio_init(void)
{
    ESP_ERROR_CHECK(aw9523_init(DEEPV1_I2C_PORT, DEEPV1_PIN_I2C_SDA,
        DEEPV1_PIN_I2C_SCL, DEEPV1_PIN_IO_RESET, DEEPV1_AW9523_ADDR));
    aw9523_set_pin(DEEPV1_AW_PIN_AP_EN, 0);
    aw9523_set_pin(DEEPV1_AW_PIN_EN_POWER, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    i2c_master_bus_handle_t bus = aw9523_get_i2c_bus();
    ESP_ERROR_CHECK(i2c_master_probe(bus, 0x18, 100));
    ESP_ERROR_CHECK(i2c_master_probe(bus, 0x40, 100));

    i2s_chan_handle_t tx, rx;
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan, &tx, &rx));
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = DEEPV1_PIN_I2S_MCLK, .bclk = DEEPV1_PIN_I2S_BCLK,
            .ws = DEEPV1_PIN_I2S_LRCLK, .dout = DEEPV1_PIN_I2S_TX, .din = I2S_GPIO_UNUSED},
    };
    i2s_tdm_config_t tdm = {
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(RATE),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO, I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
        .gpio_cfg = {.mclk = DEEPV1_PIN_I2S_MCLK, .bclk = DEEPV1_PIN_I2S_BCLK,
            .ws = DEEPV1_PIN_I2S_LRCLK, .dout = I2S_GPIO_UNUSED, .din = DEEPV1_PIN_I2S_RX},
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &std));
    ESP_ERROR_CHECK(i2s_channel_init_tdm_mode(rx, &tdm));
    ESP_ERROR_CHECK(i2s_channel_enable(tx));
    ESP_ERROR_CHECK(i2s_channel_enable(rx));

    audio_codec_i2s_cfg_t data_cfg = {.port = I2S_NUM_0, .tx_handle = tx, .rx_handle = rx};
    const audio_codec_data_if_t *data = audio_codec_new_i2s_data(&data_cfg);
    assert(data);
    // esp_codec_dev uses 8-bit addresses, unlike i2c_master_probe above.
    audio_codec_i2c_cfg_t ctrl_cfg = {.port = DEEPV1_I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = bus};
    const audio_codec_ctrl_if_t *dac_ctrl = audio_codec_new_i2c_ctrl(&ctrl_cfg);
    const audio_codec_gpio_if_t *gpio = audio_codec_new_gpio();
    assert(dac_ctrl && gpio);
    es8311_codec_cfg_t dac_cfg = {.ctrl_if = dac_ctrl, .gpio_if = gpio,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC, .pa_pin = -1, .use_mclk = true,
        .hw_gain = {.pa_voltage = 4.5, .codec_dac_voltage = 3.3}};
    const audio_codec_if_t *dac = es8311_codec_new(&dac_cfg);
    assert(dac);
    esp_codec_dev_cfg_t dev = {.dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = dac, .data_if = data};
    output = esp_codec_dev_new(&dev);
    assert(output);
    ctrl_cfg.addr = ES7210_CODEC_DEFAULT_ADDR;
    const audio_codec_ctrl_if_t *adc_ctrl = audio_codec_new_i2c_ctrl(&ctrl_cfg);
    assert(adc_ctrl);
    es7210_codec_cfg_t adc_cfg = {.ctrl_if = adc_ctrl,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4};
    const audio_codec_if_t *adc = es7210_codec_new(&adc_cfg);
    assert(adc);
    dev.dev_type = ESP_CODEC_DEV_TYPE_IN;
    dev.codec_if = adc;
    input = esp_codec_dev_new(&dev);
    assert(input);
    esp_codec_dev_sample_info_t format = {.bits_per_sample = 16, .channel = 4, .sample_rate = RATE};
    ESP_ERROR_CHECK(esp_codec_dev_open(input, &format));
    // Gain masks use physical ADC numbering, NOT serial TDM slot numbering.
    ESP_ERROR_CHECK(esp_codec_dev_set_in_channel_gain(input, 0x03, 24.0f));
    ESP_ERROR_CHECK(esp_codec_dev_set_in_channel_gain(input, 0x0c, 0.0f));
    format.channel = 1;
    ESP_ERROR_CHECK(esp_codec_dev_open(output, &format));
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(output, 55));
    ESP_ERROR_CHECK(esp_codec_dev_set_out_mute(output, false));
    ESP_LOGI(TAG, "16kHz: TX=GPIO%d RX=GPIO%d; MIC1/MIC2 gain=24dB REF MIC3=0dB",
        DEEPV1_PIN_I2S_TX, DEEPV1_PIN_I2S_RX);
}

typedef struct { double sum, square; unsigned count, clipped; int peak; } meter_t;
static void meter_add(meter_t *m, int16_t value)
{
    int magnitude = abs((int)value);
    m->sum += value;
    m->square += (double)value * value;
    m->count++;
    m->clipped += magnitude >= 32760;
    if (magnitude > m->peak) m->peak = magnitude;
}
static void meter_log(const char *name, meter_t *m)
{
    if (!m->count) return;
    double dc = m->sum / m->count;
    double rms = sqrt(fmax(0, m->square / m->count - dc * dc));
    ESP_LOGI(TAG, "%s AC_RMS=%.1f dBFS=%.1f DC=%.1f peak=%d clip=%u",
        name, rms, 20 * log10(fmax(rms, 0.001) / 32768), dc, m->peak, m->clipped);
    *m = (meter_t){0};
}

static void capture_task(void *arg)
{
    int n = afe->get_feed_chunksize(afe_data);
    int16_t *raw = malloc(n * 4 * sizeof(int16_t));
    int16_t *mmr = malloc(n * 3 * sizeof(int16_t));
    assert(raw && mmr);
    meter_t meters[4] = {0};
    const char *names[] = {"MIC1(slot0)", "REF_MIC3(slot1)", "MIC2(slot2)", "NC_MIC4(slot3)"};
    unsigned samples = 0;
    while (!atomic_load(&stop_capture)) {
        ESP_ERROR_CHECK(esp_codec_dev_read(input, raw, n * 4 * sizeof(int16_t)));
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < 4; ++j) meter_add(&meters[j], raw[i * 4 + j]);
            // ES7210 serial order is MIC1, MIC3, MIC2, MIC4. AFE expects M,M,R.
            mmr[i * 3] = raw[i * 4];
            mmr[i * 3 + 1] = raw[i * 4 + 2];
            mmr[i * 3 + 2] = raw[i * 4 + 1];
        }
        afe->feed(afe_data, mmr);
        samples += n;
        if (samples >= RATE) {
            for (int j = 0; j < 4; ++j) meter_log(names[j], &meters[j]);
            samples = 0;
        }
    }
    free(raw);
    free(mmr);
    xEventGroupSetBits(events, CAPTURE_STOPPED);
    vTaskDelete(NULL);
}

// Broadband far-end stimulus gives the AEC an identifiable signal to cancel.
// The board routes ES8311's analog output back to ES7210 MIC3 (the R channel).
static void playback_task(void *arg)
{
    int16_t pcm[320];
    uint32_t noise = 0x5a17u;
    while (!atomic_load(&stop_capture)) {
        for (size_t i = 0; i < 320; ++i) {
            noise = (noise >> 1) ^ (-(noise & 1u) & 0xd0000001u);
            pcm[i] = (int16_t)((int32_t)(noise & 0x0fffu) - 2048);
        }
        ESP_ERROR_CHECK(esp_codec_dev_write(output, pcm, sizeof(pcm)));
    }
    int16_t silence[320] = {0};
    for (int i = 0; i < 20; ++i) ESP_ERROR_CHECK(esp_codec_dev_write(output, silence, sizeof(silence)));
    aw9523_set_pin(DEEPV1_AW_PIN_AP_EN, 0);
    xEventGroupSetBits(events, PLAYBACK_STOPPED);
    vTaskDelete(NULL);
}

static void fetch_task(void *arg)
{
    meter_t meter = {0};
    size_t recorded = 0;
    unsigned reported_seconds = 0;
    while (true) {
        afe_fetch_result_t *r = afe->fetch_with_delay(afe_data, pdMS_TO_TICKS(2000));
        if (!r || r->ret_value == ESP_FAIL || !r->data) {
            if (atomic_load(&stop_capture) &&
                (xEventGroupGetBits(events) & CAPTURE_STOPPED)) break;
            ESP_LOGE(TAG, "AFE fetch failed/timeout; check capture task and clocks");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (recorded < RECORD_SAMPLES) {
            size_t count = r->data_size / sizeof(int16_t);
            if (count > RECORD_SAMPLES - recorded) count = RECORD_SAMPLES - recorded;
            memcpy(recording + recorded, r->data, count * sizeof(int16_t));
            recorded += count;
            unsigned seconds = recorded / RATE;
            if (seconds != reported_seconds) {
                ESP_LOGI(TAG, "Recording %u/%d seconds", seconds, RECORD_SECONDS);
                reported_seconds = seconds;
            }
            if (recorded == RECORD_SAMPLES) atomic_store(&stop_capture, true);
        }
        for (int i = 0; i < r->data_size / sizeof(int16_t); ++i) meter_add(&meter, r->data[i]);
        if (meter.count >= RATE) meter_log("AEC_OUT", &meter);
        // Drain remaining AFE output until the producer has stopped. This avoids
        // leaving the producer blocked in feed() when the recording is full.
        if (atomic_load(&stop_capture) &&
            (xEventGroupGetBits(events) & CAPTURE_STOPPED)) break;
    }
    xEventGroupSetBits(events, RECORD_READY);
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== DeepStoa microphone + hardware-reference AEC test ===");
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < 1024 * 1024) {
        ESP_LOGE(TAG, "AEC requires working PSRAM; run debug/t_psram first");
        return;
    }
    afe_config_t *cfg = afe_config_init("MMR", NULL, AFE_TYPE_FD, AFE_MODE_LOW_COST);
    assert(cfg);
    cfg->aec_init = true;
    cfg->aec_mode = AEC_MODE_FD_LOW_COST;
    cfg->aec_nlp_level = AEC_NLP_LEVEL_VERYAGGR;
    cfg->ns_init = false;
    cfg->vad_init = false;
    cfg->wakenet_init = false;
    cfg->agc_init = false;
    cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    afe = esp_afe_handle_from_config(cfg);
    assert(afe);
    afe_data = afe->create_from_config(cfg);
    afe_config_free(cfg);
    assert(afe_data);
    assert(afe->get_feed_channel_num(afe_data) == 3);
    afe->print_pipeline(afe_data);
    audio_init();
    recording = heap_caps_malloc(RECORD_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(recording);
    events = xEventGroupCreate();
    assert(events);
    ESP_LOGI(TAG, "feed=%d fetch=%d; AEC_OUT is diagnostic level, not an ERLE measurement",
        afe->get_feed_chunksize(afe_data), afe->get_fetch_chunksize(afe_data));
    ESP_LOGI(TAG, "Get ready: simultaneous playback and capture start in 3 seconds. Speak into the microphones.");
    vTaskDelay(pdMS_TO_TICKS(3000));
    ESP_LOGI(TAG, "DUPLEX START: %d seconds; broadband playback on, speak into the microphones", RECORD_SECONDS);
    aw9523_set_pin(DEEPV1_AW_PIN_AP_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    assert(xTaskCreate(fetch_task, "aec_fetch", 6144, NULL, 5, NULL) == pdPASS);
    assert(xTaskCreate(capture_task, "mic_feed", 6144, NULL, 5, NULL) == pdPASS);
    assert(xTaskCreate(playback_task, "far_end_play", 4096, NULL, 5, NULL) == pdPASS);
    EventBits_t done = xEventGroupWaitBits(events, RECORD_READY | CAPTURE_STOPPED | PLAYBACK_STOPPED,
        pdFALSE, pdTRUE, pdMS_TO_TICKS(35000));
    if ((done & (RECORD_READY | CAPTURE_STOPPED | PLAYBACK_STOPPED)) !=
        (RECORD_READY | CAPTURE_STOPPED | PLAYBACK_STOPPED)) {
        ESP_LOGE(TAG, "Duplex test timed out; reset and check capture/AEC/playback logs.");
        atomic_store(&stop_capture, true);
        aw9523_set_pin(DEEPV1_AW_PIN_AP_EN, 0);
        return;
    }
    ESP_ERROR_CHECK(esp_codec_dev_close(input));
    ESP_LOGI(TAG, "DUPLEX DONE: captured %d samples of AEC output during far-end playback", RECORD_SAMPLES);
    ESP_LOGI(TAG, "PLAY CLEANED CAPTURE: AEC output from the duplex interval");
    aw9523_set_pin(DEEPV1_AW_PIN_AP_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    for (size_t offset = 0; offset < RECORD_SAMPLES; offset += 320) {
        ESP_ERROR_CHECK(esp_codec_dev_write(output, recording + offset, 320 * sizeof(int16_t)));
    }
    int16_t silence[320] = {0};
    for (int i = 0; i < 20; ++i) ESP_ERROR_CHECK(esp_codec_dev_write(output, silence, sizeof(silence)));
    aw9523_set_pin(DEEPV1_AW_PIN_AP_EN, 0);
    ESP_ERROR_CHECK(esp_codec_dev_close(output));
    afe->destroy(afe_data);
    free(recording);
    vEventGroupDelete(events);
    ESP_LOGI(TAG, "TEST DONE. Press RESET to record again.");
}
