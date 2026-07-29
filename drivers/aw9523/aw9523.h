// drivers/aw9523/aw9523.h
// AW9523BTQR I2C GPIO Expander Driver

#ifndef AW9523_H
#define AW9523_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hal/gpio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize AW9523 IO expander.
 *
 * @param i2c_port   I2C port number (0 or 1)
 * @param sda_pin    I2C SDA GPIO pin
 * @param scl_pin    I2C SCL GPIO pin
 * @param rst_pin    Hardware reset GPIO pin (low active), or GPIO_NUM_NC
 * @param i2c_addr   7-bit I2C address (default 0x58)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t aw9523_init(uint8_t i2c_port, gpio_num_t sda_pin, gpio_num_t scl_pin,
                      gpio_num_t rst_pin, uint8_t i2c_addr);

/**
 * @brief Software reset the AW9523.
 */
void aw9523_reset(void);

/**
 * @brief Read the chip ID register (0x10). Expected value: 0x23.
 * @return uint8_t Chip ID value
 */
uint8_t aw9523_get_chip_id(void);

/**
 * @brief Write value to a full port (P0 or P1).
 * @param port   0 for P0, 1 for P1
 * @param value  8-bit value to write
 */
void aw9523_write_port(uint8_t port, uint8_t value);

/**
 * @brief Read a full port (P0 or P1).
 * @param port   0 for P0, 1 for P1
 * @return uint8_t 8-bit port input value
 */
uint8_t aw9523_read_port(uint8_t port);

/**
 * @brief Set a single pin output level.
 * @param pin    Encoded pin (use AW9523_PIN macro from board header)
 * @param level  0 = low, 1 = high
 */
void aw9523_set_pin(uint8_t pin, uint8_t level);

/**
 * @brief Read a single pin input level.
 * @param pin    Encoded pin (use AW9523_PIN macro from board header)
 * @return uint8_t 0 = low, 1 = high
 */
uint8_t aw9523_get_pin(uint8_t pin);

/**
 * @brief Configure pin direction for a full port.
 * @param port  0 for P0, 1 for P1
 * @param dir   8-bit direction mask: 0 = output, 1 = input
 */
void aw9523_set_port_dir(uint8_t port, uint8_t dir);

#ifdef __cplusplus
}
#endif

#endif // AW9523_H
