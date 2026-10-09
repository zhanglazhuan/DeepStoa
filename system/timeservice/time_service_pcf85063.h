#ifndef DEEPSTOA_TIME_SERVICE_PCF85063_H
#define DEEPSTOA_TIME_SERVICE_PCF85063_H

#include "pcf85063.h"
#include "time_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a time_service RTC backend for an already attached PCF85063.
 *
 * The caller owns the I2C bus and the pcf85063_t instance. This helper only
 * fills callback pointers; it does not probe, configure, or claim hardware.
 */
void time_service_pcf85063_backend_init(
    pcf85063_t *rtc,
    time_service_rtc_backend_t *out_backend);

#ifdef __cplusplus
}
#endif

#endif /* DEEPSTOA_TIME_SERVICE_PCF85063_H */
