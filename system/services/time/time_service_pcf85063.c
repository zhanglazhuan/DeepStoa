#include "time_service_pcf85063.h"

#include <string.h>

/* Gregorian date -> Unix days, independent of libc timegm()/TZ state. */
static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5
                       + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static bool pcf85063_read_utc(time_t *out_utc, void *user_data)
{
    if (!out_utc || !user_data) return false;

    pcf85063_datetime_t date;
    if (pcf85063_read_datetime((const pcf85063_t *)user_data, &date) != ESP_OK) {
        return false;
    }

    struct tm tm = {
        .tm_sec = date.second,
        .tm_min = date.minute,
        .tm_hour = date.hour,
        .tm_mday = date.day,
        .tm_mon = (int)date.month - 1,
        .tm_year = 100 + date.year, /* PCF85063 stores 00..99 as 2000..2099 */
        .tm_isdst = 0,
    };
    time_t utc = (time_t)(days_from_civil(tm.tm_year + 1900,
                                          (unsigned)tm.tm_mon + 1,
                                          (unsigned)tm.tm_mday) * 86400LL
                         + tm.tm_hour * 3600L
                         + tm.tm_min * 60L
                         + tm.tm_sec);
    if (utc <= 0) return false;
    *out_utc = utc;
    return true;
}

static bool pcf85063_write_utc(time_t utc, void *user_data)
{
    if (utc <= 0 || !user_data) return false;

    struct tm tm;
    if (!gmtime_r(&utc, &tm) || tm.tm_year < 100 || tm.tm_year > 199) {
        return false;
    }

    pcf85063_datetime_t date = {
        .year = (uint8_t)(tm.tm_year - 100),
        .month = (uint8_t)(tm.tm_mon + 1),
        .day = (uint8_t)tm.tm_mday,
        .weekday = (uint8_t)tm.tm_wday,
        .hour = (uint8_t)tm.tm_hour,
        .minute = (uint8_t)tm.tm_min,
        .second = (uint8_t)tm.tm_sec,
    };
    return pcf85063_write_datetime((const pcf85063_t *)user_data, &date) == ESP_OK;
}

void time_service_pcf85063_backend_init(
    pcf85063_t *rtc,
    time_service_rtc_backend_t *out_backend)
{
    if (!out_backend) return;
    memset(out_backend, 0, sizeof(*out_backend));
    if (!rtc) return;

    out_backend->read_utc = pcf85063_read_utc;
    out_backend->write_utc = pcf85063_write_utc;
    out_backend->user_data = rtc;
}
