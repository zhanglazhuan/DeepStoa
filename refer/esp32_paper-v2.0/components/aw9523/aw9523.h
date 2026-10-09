#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AW9523_I2C_ADDR 0x58

esp_err_t aw9523_init(i2c_master_bus_handle_t bus, uint8_t address,
                      gpio_num_t reset_gpio);
bool aw9523_is_initialized(void);
esp_err_t aw9523_set_direction(uint16_t pins, bool input);
esp_err_t aw9523_write(uint16_t pins, bool high);
esp_err_t aw9523_read(uint16_t pins, uint16_t *levels);
esp_err_t aw9523_shift_out(uint16_t clock_pin, uint16_t data_pin,
                           const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif
