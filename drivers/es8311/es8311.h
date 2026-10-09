#ifndef DEEPSTOA_ES8311_H
#define DEEPSTOA_ES8311_H

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ES8311_I2C_ADDR_LOW  0x18
#define ES8311_I2C_ADDR_HIGH 0x19

typedef struct {
    i2c_master_dev_handle_t device;
} es8311_t;

/** Attach to an existing I2C bus; I2S and PA ownership stays in audio system. */
esp_err_t es8311_init(es8311_t *codec, i2c_master_bus_handle_t bus,
                      uint8_t address);
esp_err_t es8311_read_reg(const es8311_t *codec, uint8_t reg, uint8_t *value);
esp_err_t es8311_write_reg(const es8311_t *codec, uint8_t reg, uint8_t value);
esp_err_t es8311_soft_reset(const es8311_t *codec);

#ifdef __cplusplus
}
#endif

#endif /* DEEPSTOA_ES8311_H */
