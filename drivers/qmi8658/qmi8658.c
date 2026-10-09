#include "qmi8658.h"

#include <string.h>

#define QMI8658_REG_WHO_AM_I  0x00
#define QMI8658_REG_TEMP_L    0x33
#define QMI8658_REG_ACCEL_L   0x35

static esp_err_t qmi8658_read(const qmi8658_t *sensor, uint8_t reg,
                              uint8_t *data, size_t length)
{
    if (!sensor || !sensor->device || !data || length == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(sensor->device, &reg, 1, data, length,
                                       100);
}

esp_err_t qmi8658_init(qmi8658_t *sensor, i2c_master_bus_handle_t bus,
                       uint8_t address)
{
    if (!sensor || !bus || (address != QMI8658_I2C_ADDR_LOW &&
                            address != QMI8658_I2C_ADDR_HIGH)) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(sensor, 0, sizeof(*sensor));
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(bus, &config, &sensor->device);
}

esp_err_t qmi8658_read_who_am_i(const qmi8658_t *sensor, uint8_t *value)
{
    return qmi8658_read(sensor, QMI8658_REG_WHO_AM_I, value, 1);
}

esp_err_t qmi8658_read_temperature_raw(const qmi8658_t *sensor, int16_t *value)
{
    if (!value) return ESP_ERR_INVALID_ARG;
    uint8_t data[2];
    esp_err_t error = qmi8658_read(sensor, QMI8658_REG_TEMP_L, data, sizeof(data));
    if (error == ESP_OK) *value = (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
    return error;
}

esp_err_t qmi8658_read_accel_gyro_raw(const qmi8658_t *sensor,
                                      qmi8658_raw_sample_t *sample)
{
    if (!sample) return ESP_ERR_INVALID_ARG;
    uint8_t data[12];
    esp_err_t error = qmi8658_read(sensor, QMI8658_REG_ACCEL_L, data, sizeof(data));
    if (error != ESP_OK) return error;
    int16_t *values = (int16_t *)sample;
    for (size_t i = 0; i < 6; ++i) {
        values[i] = (int16_t)((uint16_t)data[i * 2] |
                              ((uint16_t)data[i * 2 + 1] << 8));
    }
    return ESP_OK;
}
