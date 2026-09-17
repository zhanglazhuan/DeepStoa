#ifndef CALENDAR_VIEW_MONTH_H
#define CALENDAR_VIEW_MONTH_H

#include "../app.h"

#ifdef __cplusplus
extern "C" {
#endif

void calendar_view_month_init_registry(struct CalendarApp *app);
void calendar_view_month_rebuild(struct CalendarApp *app);

#ifdef __cplusplus
}
#endif

#endif /* CALENDAR_VIEW_MONTH_H */
