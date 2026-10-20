#ifndef MISTAKEBOOK_CONTROLLER_H
#define MISTAKEBOOK_CONTROLLER_H

#include <lvgl.h>

void mistakebook_controller_select(lv_event_t *event);
void mistakebook_controller_reload(lv_event_t *event);
void mistakebook_controller_previous(lv_event_t *event);
void mistakebook_controller_next(lv_event_t *event);
void mistakebook_controller_mark_reviewed(lv_event_t *event);
void mistakebook_controller_close(lv_event_t *event);

#endif
