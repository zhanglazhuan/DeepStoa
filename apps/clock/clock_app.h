#ifndef CLOCK_APP_H
#define CLOCK_APP_H

#include <lvgl.h>
#include "page_navigator.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ClockModel;
struct ClockView;
struct ClockController;

typedef struct ClockApp {
    struct ClockModel      *model;
    struct ClockView       *view;
    struct ClockController *controller;
} ClockApp;

extern ClockApp g_clock_app;

// Register with app_manager. Call once at boot.
// 名字带 _app_ 是有意的：system/timeservice 那边是 time_service_init()，
// 两个 init 曾经都叫 clock_init()，撞过重复符号。
void clock_app_init(void);

#ifdef __cplusplus
}
#endif

#endif /* CLOCK_APP_H */
