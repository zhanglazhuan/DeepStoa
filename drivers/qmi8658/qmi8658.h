#ifndef DEEPSTOA_QMI8658_H
#define DEEPSTOA_QMI8658_H

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QMI8658_I2C_ADDR_LOW  0x6A
#define QMI8658_I2C_ADDR_HIGH 0x6B
#define QMI8658_WHO_AM_I      0x05

typedef struct {
    i2c_master_dev_handle_t device;
} qmi8658_t;

typedef struct {
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
} qmi8658_raw_sample_t;

/** Attach to an already-owned I2C bus. This function does not create a bus. */
esp_err_t qmi8658_init(qmi8658_t *sensor, i2c_master_bus_handle_t bus,
                       uint8_t address);

esp_err_t qmi8658_read_who_am_i(const qmi8658_t *sensor, uint8_t *value);
esp_err_t qmi8658_read_temperature_raw(const qmi8658_t *sensor, int16_t *value);
esp_err_t qmi8658_read_accel_gyro_raw(const qmi8658_t *sensor,
                                      qmi8658_raw_sample_t *sample);

#ifdef __cplusplus
}
#endif

#endif /* DEEPSTOA_QMI8658_H */
