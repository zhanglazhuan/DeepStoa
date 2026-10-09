#include "shtc3.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SHTC3_CMD_READ_ID       0xEFC8
#define SHTC3_CMD_SLEEP         0xB098
#define SHTC3_CMD_WAKEUP        0x3517
#define SHTC3_CMD_MEASURE       0x7866

static uint8_t shtc3_crc(const uint8_t *data, size_t length)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80U) ? (uint8_t)((crc << 1) ^ 0x31U)
                                 : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static esp_err_t shtc3_command(const shtc3_t *sensor, uint16_t command)
{
    if (!sensor || !sensor->device) return ESP_ERR_INVALID_ARG;
    uint8_t bytes[2] = {(uint8_t)(command >> 8), (uint8_t)command};
    return i2c_master_transmit(sensor->device, bytes, sizeof(bytes), 100);
}

static esp_err_t shtc3_read(const shtc3_t *sensor, uint8_t *data, size_t length)
{
    if (!sensor || !sensor->device || !data || length == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_receive(sensor->device, data, length, 100);
}

esp_err_t shtc3_init(shtc3_t *sensor, i2c_master_bus_handle_t bus)
{
    if (!sensor || !bus) return ESP_ERR_INVALID_ARG;
    memset(sensor, 0, sizeof(*sensor));
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHTC3_I2C_ADDR,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(bus, &config, &sensor->device);
}

esp_err_t shtc3_read_id(const shtc3_t *sensor, uint16_t *id)
{
    if (!id) return ESP_ERR_INVALID_ARG;
    esp_err_t error = shtc3_command(sensor, SHTC3_CMD_READ_ID);
    if (error != ESP_OK) return error;
    uint8_t data[3];
    error = shtc3_read(sensor, data, sizeof(data));
    if (error != ESP_OK) return error;
    if (shtc3_crc(data, 2) != data[2]) return ESP_FAIL;
    *id = ((uint16_t)data[0] << 8) | data[1];
    return ESP_OK;
}

esp_err_t shtc3_measure(const shtc3_t *sensor, shtc3_measurement_t *measurement)
{
    if (!measurement) return ESP_ERR_INVALID_ARG;
    esp_err_t error = shtc3_command(sensor, SHTC3_CMD_MEASURE);
    if (error != ESP_OK) return error;
    vTaskDelay(pdMS_TO_TICKS(20));
    uint8_t data[6];
    error = shtc3_read(sensor, data, sizeof(data));
    if (error != ESP_OK) return error;
    if (shtc3_crc(data, 2) != data[2] || shtc3_crc(&data[3], 2) != data[5]) {
        return ESP_FAIL;
    }
    uint16_t raw_temperature = ((uint16_t)data[0] << 8) | data[1];
    uint16_t raw_humidity = ((uint16_t)data[3] << 8) | data[4];
    measurement->temperature_c = -45.0f + 175.0f * raw_temperature / 65536.0f;
    measurement->humidity_percent = 100.0f * raw_humidity / 65536.0f;
    return ESP_OK;
}

esp_err_t shtc3_sleep(const shtc3_t *sensor)
{
    return shtc3_command(sensor, SHTC3_CMD_SLEEP);
}

esp_err_t shtc3_wakeup(const shtc3_t *sensor)
{
    esp_err_t error = shtc3_command(sensor, SHTC3_CMD_WAKEUP);
    if (error == ESP_OK) vTaskDelay(pdMS_TO_TICKS(1));
    return error;
}
