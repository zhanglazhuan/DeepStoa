#ifndef NEWS_APP_H
#define NEWS_APP_H

#include <lvgl.h>
#include "page_navigator.h"

#ifdef __cplusplus
extern "C" {
#endif

struct NewsModel;
struct NewsView;
struct NewsController;

typedef struct NewsApp {
    struct NewsModel    *model;
    struct NewsView     *view;
    struct NewsController *controller;
} NewsApp;

extern NewsApp g_news_app;

#ifdef __cplusplus
}
#endif

#endif // NEWS_APP_H
