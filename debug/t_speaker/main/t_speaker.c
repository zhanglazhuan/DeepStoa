/* DeepStoa V1 speaker / I2S hardware test. */

#include <math.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"

#include "deepstoa_v1.h"
#include "aw9523.h"

static const char *TAG = "t_speaker";
#define SAMPLE_RATE 44100
#define BEEP_FREQ 800
#define AMPLITUDE 8000
#define PI_F 3.14159265358979323846f

static i2s_chan_handle_t s_tx;
static i2c_master_dev_handle_t s_es8311;

static esp_err_t es8311_write(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    esp_err_t err = i2c_master_transmit(s_es8311, data, sizeof(data), 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 write 0x%02x=0x%02x failed: %s",
                 reg, value, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t es8311_read(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_es8311, &reg, 1, value, 1, 100);
}

static esp_err_t es8311_init(void)
{
    i2c_master_bus_handle_t bus =
        (i2c_master_bus_handle_t)aw9523_get_i2c_bus();
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x18,
        .scl_speed_hz = 400000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &s_es8311);
    if (err != ESP_OK) return err;

    /* ES8311 open sequence, configured as I2S slave with ESP32 as master.
     * MCLK=11.2896 MHz, LRCLK=44.1 kHz, 16-bit Philips format. */
    static const uint8_t init_regs[][2] = {
        {0x44, 0x08}, {0x44, 0x08},
        {0x01, 0x30}, {0x02, 0x00}, {0x03, 0x10},
        {0x16, 0x24}, {0x04, 0x10}, {0x05, 0x00},
        {0x0B, 0x00}, {0x0C, 0x00}, {0x10, 0x1F},
        {0x11, 0x7F}, {0x00, 0x80}, {0x01, 0x3F},
        {0x06, 0x00}, {0x13, 0x10}, {0x1B, 0x0A},
        {0x1C, 0x6A}, {0x44, 0x58},
        /* 44.1 kHz clock coefficients for MCLK=256*Fs. */
        {0x02, 0x00}, {0x03, 0x10}, {0x04, 0x10},
        {0x05, 0x00}, {0x06, 0x03}, {0x07, 0x00},
        {0x08, 0xFF},
        /* Enable DAC path and configure normal I2S, 16-bit samples. */
        {0x09, 0x0C}, {0x0E, 0x02}, {0x12, 0x00},
        {0x14, 0x1A}, {0x0D, 0x01}, {0x15, 0x40},
        {0x37, 0x08}, {0x45, 0x00},
        /* Unity DAC gain (0 dB), not the +32 dB maximum, then unmute. */
        {0x32, 0xBF}, {0x31, 0x00},
    };
    for (size_t i = 0; i < sizeof(init_regs) / sizeof(init_regs[0]); ++i) {
        err = es8311_write(init_regs[i][0], init_regs[i][1]);
        if (err != ESP_OK) return err;
    }

    uint8_t iface = 0;
    err = es8311_read(0x09, &iface);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "ES8311 ready: SDP_IN=0x%02x", iface);
    }
    return err;
}

static void es8311_dump_key_regs(void)
{
    static const uint8_t regs[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
        0x07, 0x08, 0x09, 0x0D, 0x0E, 0x12, 0x14,
        0x31, 0x32, 0x37, 0x44, 0x45, 0xFD, 0xFE,
    };
    for (size_t i = 0; i < sizeof(regs); ++i) {
        uint8_t value = 0;
        esp_err_t err = es8311_read(regs[i], &value);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ES8311 read 0x%02x failed: %s",
                     regs[i], esp_err_to_name(err));
            continue;
        }
        ESP_LOGI(TAG, "ES8311 R%02x = 0x%02x", regs[i], value);
    }
}

static void scan_audio_i2c(void)
{
    i2c_master_bus_handle_t bus =
        (i2c_master_bus_handle_t)aw9523_get_i2c_bus();
    if (!bus) {
        ESP_LOGE(TAG, "shared I2C bus unavailable");
        return;
    }

    ESP_LOGI(TAG, "scanning shared audio I2C bus...");
    int found = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
        esp_err_t err = i2c_master_probe(bus, addr, 20);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "I2C device found at 0x%02X", addr);
            found++;
        }
    }
    if (!found) ESP_LOGE(TAG, "no I2C audio device found");
}

static void i2s_init(void)
{
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(
        (i2s_port_t)DEEPV1_I2S_PORT, I2S_ROLE_MASTER);
    chan.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan, &s_tx, NULL));

    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = DEEPV1_PIN_I2S_MCLK,
            .bclk = DEEPV1_PIN_I2S_BCLK,
            .ws = DEEPV1_PIN_I2S_LRCLK,
            .dout = DEEPV1_PIN_I2S_TX,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx));
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== DeepStoa V1 speaker test ===");
    ESP_LOGI(TAG, "I2S MCLK=%d BCLK=%d LRCLK=%d DOUT=%d",
             DEEPV1_PIN_I2S_MCLK, DEEPV1_PIN_I2S_BCLK,
             DEEPV1_PIN_I2S_LRCLK, DEEPV1_PIN_I2S_TX);

    ESP_ERROR_CHECK(aw9523_init(DEEPV1_I2C_PORT, DEEPV1_PIN_I2C_SDA,
                                DEEPV1_PIN_I2C_SCL, DEEPV1_PIN_IO_RESET,
                                DEEPV1_AW9523_ADDR));
    /* AMP_EN is the J4 headphone sense input; AP_EN controls U11. */
    aw9523_set_pin(DEEPV1_AW_PIN_EN_POWER, 1);
    aw9523_set_pin(DEEPV1_AW_PIN_AP_EN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "AW9523 EN_POWER=%d AMP_EN=%d AP_EN=%d",
             aw9523_get_pin(DEEPV1_AW_PIN_EN_POWER),
             aw9523_get_pin(DEEPV1_AW_PIN_AMP_EN),
             aw9523_get_pin(DEEPV1_AW_PIN_AP_EN));
    scan_audio_i2c();

    i2s_init();
    /* ES8311 needs the MCLK generated by I2S while it is brought out of
     * reset, so start the clock before programming the codec registers. */
    ESP_ERROR_CHECK(es8311_init());
    es8311_dump_key_regs();
    aw9523_set_pin(DEEPV1_AW_PIN_AP_EN, 1);
    const int period = SAMPLE_RATE / BEEP_FREQ;
    int16_t samples[period * 2];
    for (int i = 0; i < period; ++i) {
        int16_t sample = (int16_t)(AMPLITUDE *
            sinf(2.0f * PI_F * (float)i / (float)period));
        samples[2 * i] = sample;
        samples[2 * i + 1] = sample;
    }

    ESP_LOGI(TAG, "beeping %d Hz; speaker power and amp enabled", BEEP_FREQ);
    while (1) {
        size_t written = 0;
        ESP_ERROR_CHECK(i2s_channel_write(s_tx, samples, sizeof(samples),
                                          &written, portMAX_DELAY));
        static unsigned long writes;
        if ((++writes % 1000) == 0) {
            ESP_LOGI(TAG, "I2S writes=%lu last_bytes=%u AP_EN=%d AMP_EN=%d",
                     writes, (unsigned)written,
                     aw9523_get_pin(DEEPV1_AW_PIN_AP_EN),
                     aw9523_get_pin(DEEPV1_AW_PIN_AMP_EN));
        }
    }
}
