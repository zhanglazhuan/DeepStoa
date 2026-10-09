#include "es8311.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define ES8311_REG_RESET 0x00
#define ES8311_RESET_CMD 0x1F

esp_err_t es8311_init(es8311_t *codec, i2c_master_bus_handle_t bus,
                      uint8_t address)
{
    if (!codec || !bus || (address != ES8311_I2C_ADDR_LOW &&
                           address != ES8311_I2C_ADDR_HIGH)) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(codec, 0, sizeof(*codec));
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(bus, &config, &codec->device);
}

esp_err_t es8311_read_reg(const es8311_t *codec, uint8_t reg, uint8_t *value)
{
    if (!codec || !codec->device || !value) return ESP_ERR_INVALID_ARG;
    return i2c_master_transmit_receive(codec->device, &reg, 1, value, 1, 100);
}

esp_err_t es8311_write_reg(const es8311_t *codec, uint8_t reg, uint8_t value)
{
    if (!codec || !codec->device) return ESP_ERR_INVALID_ARG;
    uint8_t data[2] = {reg, value};
    return i2c_master_transmit(codec->device, data, sizeof(data), 100);
}

esp_err_t es8311_soft_reset(const es8311_t *codec)
{
    esp_err_t error = es8311_write_reg(codec, ES8311_REG_RESET, ES8311_RESET_CMD);
    if (error == ESP_OK) vTaskDelay(pdMS_TO_TICKS(10));
    return error;
}
