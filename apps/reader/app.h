#ifndef READER_APP_H
#define READER_APP_H

#include <lvgl.h>
#include "page_navigator.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ReaderModel;
struct ReaderView;
struct ReaderController;

typedef struct ReaderApp {
    struct ReaderModel *model;
    struct ReaderView *view;
    struct ReaderController *controller;
} ReaderApp;

extern ReaderApp g_reader_app;

#ifdef __cplusplus
}
#endif

#endif // READER_APP_H
