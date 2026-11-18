#pragma once

#include <stddef.h>
#include <stdint.h>
#include "../clock_app.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *clock_view_timer_build(lv_obj_t *parent, ClockApp *app);
void clock_view_timer_refresh_chips(ClockApp *app);
void clock_view_timer_duration_text(uint16_t minutes, char *buf, size_t len);

#ifdef __cplusplus
}
#endif
