// system/controller/vibration_control.h
// Vibration motor control — PWM driver
// Ported from D:\Codes\EPOS\epos\drivers\epos_vibration_control.h

#ifndef VIBRATION_CONTROL_H
#define VIBRATION_CONTROL_H

#include <stdbool.h>

typedef enum {
    VIBRATION_PATTERN_CLICK,
    VIBRATION_PATTERN_NOTIFICATION,
    VIBRATION_PATTERN_ALARM,
} vibration_pattern_t;

int  vibration_run_pattern(vibration_pattern_t pattern);
int  vibration_set_enabled(bool enable);

#endif
