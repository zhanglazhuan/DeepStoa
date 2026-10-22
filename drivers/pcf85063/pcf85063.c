#include "pcf85063.h"

#include <string.h>

#define PCF85063_SECONDS_REG 0x04

static uint8_t to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4) | (value % 10U));
}

static uint8_t from_bcd(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10U) + (value & 0x0FU));
}

esp_err_t pcf85063_init(pcf85063_t *rtc, i2c_master_bus_handle_t bus)
{
    if (!rtc || !bus) return ESP_ERR_INVALID_ARG;
    memset(rtc, 0, sizeof(*rtc));
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF85063_I2C_ADDR,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(bus, &config, &rtc->device);
}

esp_err_t pcf85063_read_reg(const pcf85063_t *rtc, uint8_t reg, uint8_t *value)
{
    if (!rtc || !rtc->device || !value) return ESP_ERR_INVALID_ARG;
    return i2c_master_transmit_receive(rtc->device, &reg, 1, value, 1, 100);
}

esp_err_t pcf85063_write_reg(const pcf85063_t *rtc, uint8_t reg, uint8_t value)
{
    if (!rtc || !rtc->device) return ESP_ERR_INVALID_ARG;
    uint8_t data[2] = {reg, value};
    return i2c_master_transmit(rtc->device, data, sizeof(data), 100);
}

esp_err_t pcf85063_read_datetime(const pcf85063_t *rtc,
                                 pcf85063_datetime_t *datetime)
{
    if (!rtc || !rtc->device || !datetime) return ESP_ERR_INVALID_ARG;
    uint8_t reg = PCF85063_SECONDS_REG;
    uint8_t data[7];
    esp_err_t error = i2c_master_transmit_receive(rtc->device, &reg, 1,
                                                  data, sizeof(data), 100);
    if (error != ESP_OK) return error;
    datetime->second = from_bcd(data[0] & 0x7FU);
    datetime->minute = from_bcd(data[1] & 0x7FU);
    datetime->hour = from_bcd(data[2] & 0x3FU);
    datetime->day = from_bcd(data[3] & 0x3FU);
    datetime->weekday = data[4] & 0x07U;
    datetime->month = from_bcd(data[5] & 0x1FU);
    datetime->year = from_bcd(data[6]);
    return ESP_OK;
}

esp_err_t pcf85063_write_datetime(const pcf85063_t *rtc,
                                  const pcf85063_datetime_t *datetime)
{
    if (!rtc || !rtc->device || !datetime) return ESP_ERR_INVALID_ARG;
    uint8_t data[8] = {
        PCF85063_SECONDS_REG,
        to_bcd(datetime->second) & 0x7FU,
        to_bcd(datetime->minute) & 0x7FU,
        to_bcd(datetime->hour) & 0x3FU,
        to_bcd(datetime->day) & 0x3FU,
        (uint8_t)(datetime->weekday & 0x07U),
        to_bcd(datetime->month) & 0x1FU,
        to_bcd(datetime->year),
    };
    return i2c_master_transmit(rtc->device, data, sizeof(data), 100);
}
