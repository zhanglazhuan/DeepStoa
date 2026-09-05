#ifndef CHATBOT_APP_H
#define CHATBOT_APP_H

#include <lvgl.h>
#include "page_navigator.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ChatbotModel;
struct ChatbotView;
struct ChatbotController;

typedef struct ChatbotApp {
    struct ChatbotModel      *model;
    struct ChatbotView       *view;
    struct ChatbotController *controller;
} ChatbotApp;

extern ChatbotApp g_chatbot_app;

void chatbot_init(void);

#ifdef __cplusplus
}
#endif

#endif // CHATBOT_APP_H
