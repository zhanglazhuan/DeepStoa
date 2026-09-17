// drivers/ft6336/ft6336.h
// FT6336U I2C capacitive touch controller driver
//
// Reference: LVGL-WT32-SC01 / FocalTech FT6x36 datasheet
// I2C address: 0x38 (7-bit)
// Touch points: up to 2 simultaneous

#ifndef FT6336_H
#define FT6336_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hal/gpio_types.h"
#include "hal/i2c_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ─── Data structures ───────────────────────────────────────────────────

typedef enum {
    FT6336_EVENT_DOWN    = 0,  // press down
    FT6336_EVENT_UP      = 1,  // lift up
    FT6336_EVENT_CONTACT = 2,  // ongoing contact
    FT6336_EVENT_NONE    = 3,  // no event / reserved
} ft6336_event_t;

typedef enum {
    FT6336_GESTURE_NONE          = 0x00,
    FT6336_GESTURE_SWIPE_UP      = 0x01,
    FT6336_GESTURE_SWIPE_DOWN    = 0x02,
    FT6336_GESTURE_SWIPE_LEFT    = 0x03,
    FT6336_GESTURE_SWIPE_RIGHT   = 0x04,
    FT6336_GESTURE_ZOOM_IN       = 0x05,
    FT6336_GESTURE_ZOOM_OUT      = 0x06,
    FT6336_GESTURE_PRESS         = 0x0B,
} ft6336_gesture_t;

typedef struct {
    uint16_t x;          // X coordinate (12-bit, display-dependent mapping)
    uint16_t y;          // Y coordinate (12-bit)
    uint8_t  weight;     // touch weight / pressure
    uint8_t  area;       // touch area
    ft6336_event_t event;
} ft6336_touch_point_t;

typedef struct {
    uint8_t  count;              // number of active touch points (0–2)
    uint8_t  gesture_id;         // gesture ID from reg 0x01
    ft6336_touch_point_t points[2];
} ft6336_touch_data_t;

// ─── API ───────────────────────────────────────────────────────────────

/**
 * @brief Initialize FT6336 touch controller on its own I2C bus.
 *        Use this when FT6336 is the only device on the bus.
 *
 * @param i2c_port  I2C port number (e.g. I2C_NUM_0)
 * @param sda_pin   I2C SDA GPIO pin
 * @param scl_pin   I2C SCL GPIO pin
 * @param rst_pin   Hardware reset GPIO pin (low active), or GPIO_NUM_NC
 * @param i2c_addr  7-bit I2C address (default 0x38)
 * @return esp_err_t
 */
esp_err_t ft6336_init(i2c_port_t i2c_port, gpio_num_t sda_pin, gpio_num_t scl_pin,
                      gpio_num_t rst_pin, uint8_t i2c_addr);

/**
 * @brief Initialize FT6336 on an existing I2C bus (shared with other devices).
 *        Use this when AW9523 or other I2C chips share the same bus.
 *
 * @param bus       Existing i2c_master_bus_handle_t (pass as void*)
 * @param rst_pin   Hardware reset GPIO pin (low active), or GPIO_NUM_NC
 * @param i2c_addr  7-bit I2C address (default 0x38)
 * @return esp_err_t
 */
esp_err_t ft6336_init_shared(void *bus, gpio_num_t rst_pin, uint8_t i2c_addr);

/**
 * @brief Read chip ID register (0x00).
 *        Expected value: 0x02 for FT6336U.
 *
 * @param chip_id  [out] chip ID value
 * @return esp_err_t
 */
esp_err_t ft6336_get_chip_id(uint8_t *chip_id);

/**
 * @brief Poll touch data. Reads registers 0x01–0x0E via I2C.
 *
 * @param data  [out] parsed touch point data
 * @return esp_err_t  ESP_OK on success, ESP_ERR_TIMEOUT if no touch present
 */
esp_err_t ft6336_read(ft6336_touch_data_t *data);

/**
 * @brief Put the controller into sleep / wake it.
 *
 * @param enable  true = enter sleep, false = wake
 * @return esp_err_t
 */
esp_err_t ft6336_sleep(bool enable);

#ifdef __cplusplus
}
#endif

#endif // FT6336_H
