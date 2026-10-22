#ifndef TOVO_CONTROLLER_H
#define TOVO_CONTROLLER_H

#include <stddef.h>
#include "lvgl.h"

void tovo_controller_refresh(lv_event_t *event);
void tovo_controller_toggle(size_t index, void *user_data);

#endif
