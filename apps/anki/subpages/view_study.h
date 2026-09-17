#ifndef ANKI_VIEW_STUDY_H
#define ANKI_VIEW_STUDY_H

#include "../app.h"

#ifdef __cplusplus
extern "C" {
#endif

void anki_view_study_init_registry(struct AnkiApp* app);
void anki_view_study_card_flip(AnkiApp* app);
void anki_view_study_refresh_card(AnkiApp *app);
void anki_view_study_show_finished_msgbox(AnkiApp *app);

#ifdef __cplusplus
}
#endif

#endif // ANKI_VIEW_STUDY_H