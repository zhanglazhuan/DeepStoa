#ifndef DEEPSTOA_PCF85063_H
#define DEEPSTOA_PCF85063_H

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCF85063_I2C_ADDR 0x51

typedef struct {
    uint8_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} pcf85063_datetime_t;

typedef struct {
    i2c_master_dev_handle_t device;
} pcf85063_t;

/** Attach to an existing I2C bus; does not configure RTC policy or GPIOs. */
esp_err_t pcf85063_init(pcf85063_t *rtc, i2c_master_bus_handle_t bus);
esp_err_t pcf85063_read_datetime(const pcf85063_t *rtc,
                                 pcf85063_datetime_t *datetime);
esp_err_t pcf85063_write_datetime(const pcf85063_t *rtc,
                                  const pcf85063_datetime_t *datetime);
esp_err_t pcf85063_read_reg(const pcf85063_t *rtc, uint8_t reg, uint8_t *value);
esp_err_t pcf85063_write_reg(const pcf85063_t *rtc, uint8_t reg, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif /* DEEPSTOA_PCF85063_H */
