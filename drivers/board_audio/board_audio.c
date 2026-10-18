// DeepStoa V1 codec routing and sequencing from debug/t_microphone.
#include "board_audio.h"
#include <stdatomic.h>
#include "deepstoa_v1.h"
#include "aw9523.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"

static i2s_chan_handle_t tx, rx;
static esp_codec_dev_handle_t input, output;
static const audio_codec_data_if_t *data;
static const audio_codec_ctrl_if_t *dac_ctrl, *adc_ctrl;
static const audio_codec_gpio_if_t *gpio;
static const audio_codec_if_t *dac, *adc;
static atomic_bool opened;
static bool capturing;

void board_audio_set_amplifier(bool enabled)
{
    if (aw9523_get_i2c_bus()) aw9523_set_pin(DEEPV1_AW_PIN_AP_EN, enabled);
}

static void delete_channel(i2s_chan_handle_t *channel)
{
    if (!*channel) return;
    i2s_chan_info_t info = {0};
    if (i2s_channel_get_info(*channel, &info) == ESP_OK && info.is_enabled)
        i2s_channel_disable(*channel);
    i2s_del_channel(*channel);
    *channel = NULL;
}

void board_audio_close(void)
{
    board_audio_set_amplifier(false);
    if (input) { esp_codec_dev_close(input); esp_codec_dev_delete(input); input = NULL; }
    if (output) { esp_codec_dev_close(output); esp_codec_dev_delete(output); output = NULL; }
    if (adc) { audio_codec_delete_codec_if(adc); adc = NULL; }
    if (dac) { audio_codec_delete_codec_if(dac); dac = NULL; }
    if (adc_ctrl) { audio_codec_delete_ctrl_if(adc_ctrl); adc_ctrl = NULL; }
    if (dac_ctrl) { audio_codec_delete_ctrl_if(dac_ctrl); dac_ctrl = NULL; }
    if (gpio) { audio_codec_delete_gpio_if(gpio); gpio = NULL; }
    if (data) { audio_codec_delete_data_if(data); data = NULL; }
    delete_channel(&rx);
    delete_channel(&tx);
    capturing = false;
    atomic_store(&opened, false);
}

#define CHECK(call) do { int code = (call); if (code != 0) { \
    ESP_LOGE("board_audio", "%s failed: %d", #call, code); \
    err = ESP_FAIL; goto fail; } } while (0)
#define REQUIRE(ptr) do { if (!(ptr)) { err = ESP_ERR_NO_MEM; goto fail; } } while (0)

esp_err_t board_audio_open(bool capture, unsigned rate, unsigned channels)
{
    if (opened) return ESP_ERR_INVALID_STATE;
    if ((capture && (rate != 16000 || channels != 1)) ||
        (rate != 16000 && rate != 44100) || (channels != 1 && channels != 2))
        return ESP_ERR_NOT_SUPPORTED;
    i2c_master_bus_handle_t bus = aw9523_get_i2c_bus();
    if (!bus) return ESP_ERR_INVALID_STATE;
    bool expected = false;
    if (!atomic_compare_exchange_strong(&opened, &expected, true)) return ESP_ERR_INVALID_STATE;
    esp_err_t err = ESP_OK;
    board_audio_set_amplifier(false);
    aw9523_set_pin(DEEPV1_AW_PIN_EN_POWER, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    CHECK(i2c_master_probe(bus, 0x18, 100));
    if (capture) CHECK(i2c_master_probe(bus, 0x40, 100));
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.auto_clear = true;
    chan.dma_desc_num = 6;
    chan.dma_frame_num = 240;
    CHECK(i2s_new_channel(&chan, &tx, capture ? &rx : NULL));
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = DEEPV1_PIN_I2S_MCLK, .bclk = DEEPV1_PIN_I2S_BCLK,
            .ws = DEEPV1_PIN_I2S_LRCLK, .dout = DEEPV1_PIN_I2S_TX, .din = I2S_GPIO_UNUSED},
    };
    i2s_tdm_config_t tdm = {
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(rate),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO, I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
        .gpio_cfg = {.mclk = DEEPV1_PIN_I2S_MCLK, .bclk = DEEPV1_PIN_I2S_BCLK,
            .ws = DEEPV1_PIN_I2S_LRCLK, .dout = I2S_GPIO_UNUSED, .din = DEEPV1_PIN_I2S_RX},
    };
    CHECK(i2s_channel_init_std_mode(tx, &std));
    if (capture) CHECK(i2s_channel_init_tdm_mode(rx, &tdm));
    CHECK(i2s_channel_enable(tx));
    if (capture) CHECK(i2s_channel_enable(rx));

    audio_codec_i2s_cfg_t data_cfg = {.port = I2S_NUM_0, .tx_handle = tx, .rx_handle = rx};
    data = audio_codec_new_i2s_data(&data_cfg);
    REQUIRE(data);
    // esp_codec_dev uses 8-bit addresses, unlike i2c_master_probe above.
    audio_codec_i2c_cfg_t ctrl_cfg = {.port = DEEPV1_I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = bus};
    dac_ctrl = audio_codec_new_i2c_ctrl(&ctrl_cfg);
    gpio = audio_codec_new_gpio();
    REQUIRE(dac_ctrl && gpio);
    es8311_codec_cfg_t dac_cfg = {.ctrl_if = dac_ctrl, .gpio_if = gpio,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC, .pa_pin = -1, .use_mclk = true,
        .hw_gain = {.pa_voltage = 4.5, .codec_dac_voltage = 3.3}};
    dac = es8311_codec_new(&dac_cfg);
    REQUIRE(dac);
    esp_codec_dev_cfg_t dev = {.dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = dac, .data_if = data};
    output = esp_codec_dev_new(&dev);
    REQUIRE(output);
    if (capture) {
        ctrl_cfg.addr = ES7210_CODEC_DEFAULT_ADDR;
        adc_ctrl = audio_codec_new_i2c_ctrl(&ctrl_cfg);
        REQUIRE(adc_ctrl);
        es7210_codec_cfg_t adc_cfg = {.ctrl_if = adc_ctrl,
            .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4};
        adc = es7210_codec_new(&adc_cfg);
        REQUIRE(adc);
        dev.dev_type = ESP_CODEC_DEV_TYPE_IN;
        dev.codec_if = adc;
        input = esp_codec_dev_new(&dev);
        REQUIRE(input);
        esp_codec_dev_sample_info_t format = {.bits_per_sample = 16, .channel = 4, .sample_rate = rate};
        CHECK(esp_codec_dev_open(input, &format));
        // Gain masks use physical ADC numbering, NOT serial TDM slot numbering.
        CHECK(esp_codec_dev_set_in_channel_gain(input, 0x03, 24.0f));
        CHECK(esp_codec_dev_set_in_channel_gain(input, 0x0c, 0.0f));
    }
    esp_codec_dev_sample_info_t format = {.bits_per_sample = 16, .channel = channels, .sample_rate = rate};
    CHECK(esp_codec_dev_open(output, &format));
    CHECK(esp_codec_dev_set_out_vol(output, 55));
    CHECK(esp_codec_dev_set_out_mute(output, false));

    opened = true;
    capturing = capture;
    return ESP_OK;
fail:
    ESP_LOGE("board_audio", "Codec initialization failed: %s", esp_err_to_name(err));
    board_audio_close();
    return err;
}

esp_err_t board_audio_read(int16_t *raw, size_t frames)
{
    if (!opened || !capturing) return ESP_ERR_INVALID_STATE;
    if (!raw || !frames || frames > 4096) return ESP_ERR_INVALID_ARG;
    return esp_codec_dev_read(input, raw, frames * 4 * sizeof(int16_t)) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t board_audio_write(int16_t *pcm, size_t samples)
{
    if (!opened || capturing) return ESP_ERR_INVALID_STATE;
    if (!pcm || !samples || samples > 8192) return ESP_ERR_INVALID_ARG;
    return esp_codec_dev_write(output, pcm, samples * sizeof(int16_t)) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t board_audio_volume(unsigned volume)
{
    if (!opened) return ESP_ERR_INVALID_STATE;
    return esp_codec_dev_set_out_vol(output, volume > 100 ? 100 : volume) == 0 ? ESP_OK : ESP_FAIL;
}

void board_audio_to_mmr(const int16_t *raw, int16_t *mmr, size_t frames)
{
    for (size_t i = 0; i < frames; ++i) {
        mmr[i * 3] = raw[i * 4];
        mmr[i * 3 + 1] = raw[i * 4 + 2];
        mmr[i * 3 + 2] = raw[i * 4 + 1];
    }
}
