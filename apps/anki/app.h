#ifndef ANKI_APP_H
#define ANKI_APP_H

#include <lvgl.h>
#include "page_navigator.h"

#ifdef __cplusplus
extern "C" {
#endif

struct AnkiModel;
struct AnkiView;
struct AnkiController;

typedef struct AnkiApp {
    struct AnkiModel      *model;
    struct AnkiView       *view;
    struct AnkiController *controller;
} AnkiApp;

extern AnkiApp g_anki_app;

#ifdef __cplusplus
}
#endif

#endif // ANKI_APP_H
