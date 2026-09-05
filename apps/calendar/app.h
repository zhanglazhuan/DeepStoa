#ifndef CALENDAR_APP_H
#define CALENDAR_APP_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CalendarModel;
struct CalendarView;
struct CalendarController;

typedef struct CalendarApp {
    struct CalendarModel      *model;
    struct CalendarView       *view;
    struct CalendarController *controller;
} CalendarApp;

extern CalendarApp g_calendar_app;

// Register with app_manager. Call once at boot.
void calendar_init(void);

#ifdef __cplusplus
}
#endif

#endif /* CALENDAR_APP_H */
