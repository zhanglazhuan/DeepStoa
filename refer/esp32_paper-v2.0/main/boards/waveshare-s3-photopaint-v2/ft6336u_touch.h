#ifndef FT6336U_TOUCH_H
#define FT6336U_TOUCH_H

#include <driver/i2c_master.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FT6336U_DEFAULT_ADDR 0x2E

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t pressure;
    uint8_t id;
    bool active;
} ft6336u_touch_point_t;

typedef struct {
    i2c_master_dev_handle_t dev_handle;
    ft6336u_touch_point_t points[2];
    uint8_t num_points;
} ft6336u_handle_t;

// Initialize FT6336U touch controller on the given I2C bus
bool ft6336u_init(i2c_master_bus_handle_t bus_handle, uint8_t i2c_addr, ft6336u_handle_t *handle);
void ft6336u_deinit(ft6336u_handle_t *handle);

// Read touch data (call periodically)
bool ft6336u_read(ft6336u_handle_t *handle);

// Get number of touch points
uint8_t ft6336u_get_num_points(const ft6336u_handle_t *handle);

// Get touch point data
const ft6336u_touch_point_t *ft6336u_get_point(const ft6336u_handle_t *handle, int index);

#ifdef __cplusplus
}
#endif

#endif
