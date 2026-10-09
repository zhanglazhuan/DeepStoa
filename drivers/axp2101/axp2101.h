#ifndef DEEPSTOA_AXP2101_H
#define DEEPSTOA_AXP2101_H

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AXP2101_I2C_ADDR 0x34

typedef struct {
    i2c_master_dev_handle_t device;
} axp2101_t;

/** Attach to an existing I2C bus; does not configure power rails. */
esp_err_t axp2101_init(axp2101_t *pmic, i2c_master_bus_handle_t bus);
esp_err_t axp2101_read_reg(const axp2101_t *pmic, uint8_t reg, uint8_t *value);
esp_err_t axp2101_write_reg(const axp2101_t *pmic, uint8_t reg, uint8_t value);
esp_err_t axp2101_update_bits(const axp2101_t *pmic, uint8_t reg,
                              uint8_t mask, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif /* DEEPSTOA_AXP2101_H */
