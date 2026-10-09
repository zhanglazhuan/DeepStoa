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

/* Register the same Chatbot lifecycle under the independent LXJ app name.
 * Both names intentionally share one model/controller/audio owner. */
void chatbot_lxj_init(void);

#ifdef __cplusplus
}
#endif

#endif // CHATBOT_APP_H
