#ifndef ANKI_CONTROLLER_H
#define ANKI_CONTROLLER_H

#include <lvgl.h>
#include "model.h"
#include "view.h"

struct AnkiApp;
typedef struct AnkiController {
    AnkiModel *model;
    AnkiView *view;
} AnkiController;

void anki_controller_init(struct AnkiApp *app);
void anki_controller_deinit(struct AnkiApp *app);
void anki_controller_sync(struct AnkiApp *app);

#endif // ANKI_CONTROLLER_H