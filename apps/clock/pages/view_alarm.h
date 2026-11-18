#pragma once

#include "../clock_app.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *clock_view_alarm_build(lv_obj_t *parent, ClockApp *app);
void clock_view_alarm_rebuild_list(ClockApp *app);
void clock_view_alarm_set_row_enabled(lv_obj_t *sw, bool enabled);
void clock_view_alarm_show_max_warning(void);

#ifdef __cplusplus
}
#endif
