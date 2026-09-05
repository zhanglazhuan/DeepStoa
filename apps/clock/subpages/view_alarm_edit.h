#ifndef CLOCK_VIEW_ALARM_EDIT_H
#define CLOCK_VIEW_ALARM_EDIT_H

#include "../clock_app.h"

#ifdef __cplusplus
extern "C" {
#endif

void clock_view_alarm_edit_init_registry(struct ClockApp *app);
void show_delete_confirm_dialog(lv_event_t *e);

#ifdef __cplusplus
}
#endif

#endif /* CLOCK_VIEW_ALARM_EDIT_H */
