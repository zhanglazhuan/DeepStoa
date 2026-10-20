#ifndef ANKI_CONTROLLER_STUDY_H
#define ANKI_CONTROLLER_STUDY_H

#include <lvgl.h>
#include "../app.h"

#ifdef __cplusplus
extern "C" {
#endif

void anki_controller_on_show_answer(lv_event_t *e);
void anki_controller_on_rate_card(lv_event_t *e);
void anki_controller_study_format_labels(AnkiApp *app,
                                         char labels[4][24]);

#ifdef __cplusplus
}
#endif

#endif // ANKI_CONTROLLER_STUDY_H
