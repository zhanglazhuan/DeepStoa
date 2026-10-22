#include "axp2101.h"

#include <string.h>

esp_err_t axp2101_init(axp2101_t *pmic, i2c_master_bus_handle_t bus)
{
    if (!pmic || !bus) return ESP_ERR_INVALID_ARG;
    memset(pmic, 0, sizeof(*pmic));
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_I2C_ADDR,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(bus, &config, &pmic->device);
}

esp_err_t axp2101_read_reg(const axp2101_t *pmic, uint8_t reg, uint8_t *value)
{
    if (!pmic || !pmic->device || !value) return ESP_ERR_INVALID_ARG;
    return i2c_master_transmit_receive(pmic->device, &reg, 1, value, 1, 100);
}

esp_err_t axp2101_write_reg(const axp2101_t *pmic, uint8_t reg, uint8_t value)
{
    if (!pmic || !pmic->device) return ESP_ERR_INVALID_ARG;
    uint8_t data[2] = {reg, value};
    return i2c_master_transmit(pmic->device, data, sizeof(data), 100);
}

esp_err_t axp2101_update_bits(const axp2101_t *pmic, uint8_t reg,
                              uint8_t mask, uint8_t value)
{
    uint8_t current;
    esp_err_t error = axp2101_read_reg(pmic, reg, &current);
    if (error != ESP_OK) return error;
    current = (uint8_t)((current & (uint8_t)~mask) | (value & mask));
    return axp2101_write_reg(pmic, reg, current);
}
