#include "aw9523.h"
#include "sdkconfig.h"

// This assembled board has EN_POWER strapped to 3.3 V. Keep P1.7
// released even when legacy display/power callers request an output.
#if CONFIG_BOARD_TYPE_WAVESHARE_S3_PHOTOPAINT_V2
#define FIXED_POWER_PIN (1U << 15)
#else
#define FIXED_POWER_PIN 0
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"

#define AW9523_I2C_SPEED_HZ 400000
#define AW9523_TIMEOUT_MS 100

#define AW9523_REG_INPUT0  0x00
#define AW9523_REG_OUTPUT0 0x02
#define AW9523_REG_CONFIG0 0x04
#define AW9523_REG_ID      0x10
#define AW9523_REG_GCR     0x11
#define AW9523_REG_MODE0   0x12

static const char *TAG = "aw9523";
static i2c_master_dev_handle_t s_dev;
static SemaphoreHandle_t s_lock;
static uint16_t s_output = 0xffff;
static uint16_t s_direction = 0xffff;

static esp_err_t write_regs(uint8_t reg, uint16_t value) {
    uint8_t data[3] = {reg, (uint8_t)value, (uint8_t)(value >> 8)};
    return i2c_master_transmit(s_dev, data, sizeof(data), AW9523_TIMEOUT_MS);
}

static esp_err_t read_regs(uint8_t reg, uint16_t *value) {
    uint8_t data[2];
    esp_err_t ret = i2c_master_transmit_receive(s_dev, &reg, 1, data, 2,
                                                AW9523_TIMEOUT_MS);
    if (ret == ESP_OK) *value = data[0] | ((uint16_t)data[1] << 8);
    return ret;
}

bool aw9523_is_initialized(void) { return s_dev != NULL; }

esp_err_t aw9523_init(i2c_master_bus_handle_t bus, uint8_t address,
                      gpio_num_t reset_gpio) {
    if (s_dev != NULL) return ESP_OK;
    if (bus == NULL) return ESP_ERR_INVALID_ARG;
    if (s_lock == NULL) s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) return ESP_ERR_NO_MEM;

    if (reset_gpio != GPIO_NUM_NC) {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << reset_gpio,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "reset GPIO config failed");
        gpio_set_level(reset_gpio, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        gpio_set_level(reset_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = AW9523_I2C_SPEED_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev), TAG,
                        "add I2C device failed");

    uint8_t reg = AW9523_REG_ID;
    uint8_t id = 0;
    esp_err_t ret = i2c_master_transmit_receive(s_dev, &reg, 1, &id, 1,
                                                AW9523_TIMEOUT_MS);
    if (ret != ESP_OK || id != 0x23) {
        ESP_LOGE(TAG, "AW9523 ID read failed: ret=%s id=0x%02x",
                 esp_err_to_name(ret), id);
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return ret == ESP_OK ? ESP_ERR_NOT_FOUND : ret;
    }

    // Release inputs before changing the drive mode. 12H/13H: 1=GPIO,
    // 0=LED current sink. GCR bit 4: 1=P0 push-pull, 0=open-drain.
    ret = write_regs(AW9523_REG_CONFIG0, 0xffff);
    if (ret == ESP_OK) ret = write_regs(AW9523_REG_MODE0, 0xffff);
    uint8_t setup[] = {AW9523_REG_GCR, 0x10};
    if (ret == ESP_OK) ret = i2c_master_transmit(s_dev, setup, sizeof(setup), AW9523_TIMEOUT_MS);
    if (ret == ESP_OK) ret = read_regs(AW9523_REG_OUTPUT0, &s_output);
    if (ret == ESP_OK) ret = read_regs(AW9523_REG_CONFIG0, &s_direction);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AW9523 configuration failed: %s", esp_err_to_name(ret));
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return ret;
    }
    ESP_LOGI(TAG, "AW9523 initialized at 0x%02x (%d Hz, GPIO mode, P0 push-pull)",
             address, AW9523_I2C_SPEED_HZ);
    return ESP_OK;
}

esp_err_t aw9523_set_direction(uint16_t pins, bool input) {
    if (s_dev == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint16_t direction = input ? (s_direction | pins) : (s_direction & ~pins);
    direction |= FIXED_POWER_PIN;
    esp_err_t ret = write_regs(AW9523_REG_CONFIG0, direction);
    if (ret == ESP_OK) s_direction = direction;
    xSemaphoreGive(s_lock);
    return ret;
}

esp_err_t aw9523_write(uint16_t pins, bool high) {
    if (s_dev == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint16_t output = high ? (s_output | pins) : (s_output & ~pins);
    output |= FIXED_POWER_PIN;
    esp_err_t ret = write_regs(AW9523_REG_OUTPUT0, output);
    if (ret == ESP_OK) s_output = output;
    xSemaphoreGive(s_lock);
    return ret;
}

esp_err_t aw9523_read(uint16_t pins, uint16_t *levels) {
    if (s_dev == NULL || levels == NULL) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint16_t value = 0;
    esp_err_t ret = read_regs(AW9523_REG_INPUT0, &value);
    xSemaphoreGive(s_lock);
    if (ret == ESP_OK) *levels = value & pins;
    return ret;
}

esp_err_t aw9523_shift_out(uint16_t clock_pin, uint16_t data_pin,
                           const uint8_t *data, size_t length) {
    if (s_dev == NULL || (data == NULL && length != 0)) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = ESP_OK;
    for (size_t n = 0; n < length && ret == ESP_OK; ++n) {
        // Serialize one complete SPI byte, not the entire framebuffer. The
        // outer e-paper mutex still protects CS/DC and the SPI transaction.
        // Input reads and unrelated outputs can safely run between bytes.
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_output &= ~clock_pin;
        uint8_t value = data[n];
        for (uint8_t mask = 0x80; mask != 0 && ret == ESP_OK; mask >>= 1) {
            if (value & mask) s_output |= data_pin; else s_output &= ~data_pin;
            ret = write_regs(AW9523_REG_OUTPUT0, s_output);
            if (ret != ESP_OK) break;
            s_output |= clock_pin;
            ret = write_regs(AW9523_REG_OUTPUT0, s_output);
            s_output &= ~clock_pin;
            if (ret == ESP_OK) ret = write_regs(AW9523_REG_OUTPUT0, s_output);
        }
        xSemaphoreGive(s_lock);
        // Releasing alone can starve a waiter when the sender immediately
        // reacquires on the same core. Allow the key sampler to run while
        // CS remains asserted; a pause between SPI bytes preserves the data.
        if (ret == ESP_OK && (n & 15U) == 15U && n + 1 < length) {
            vTaskDelay(1);
        }
    }
    return ret;
}
