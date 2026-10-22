#ifndef DEEPSTOA_SHTC3_H
#define DEEPSTOA_SHTC3_H

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHTC3_I2C_ADDR 0x70

typedef struct {
    i2c_master_dev_handle_t device;
} shtc3_t;

typedef struct {
    float temperature_c;
    float humidity_percent;
} shtc3_measurement_t;

/** Attach to an already-owned I2C bus. */
esp_err_t shtc3_init(shtc3_t *sensor, i2c_master_bus_handle_t bus);
esp_err_t shtc3_read_id(const shtc3_t *sensor, uint16_t *id);

/**
 * Read one high-precision temperature/humidity sample.
 * The standard SHTC3 measurement requires a short conversion delay.
 */
esp_err_t shtc3_measure(const shtc3_t *sensor, shtc3_measurement_t *measurement);
esp_err_t shtc3_sleep(const shtc3_t *sensor);
esp_err_t shtc3_wakeup(const shtc3_t *sensor);

#ifdef __cplusplus
}
#endif

#endif /* DEEPSTOA_SHTC3_H */
