#ifndef CHAT_APP_H
#define CHAT_APP_H

#include <lvgl.h>
#include "page_navigator.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ChatModel;
struct ChatView;
struct ChatController;

typedef struct ChatApp {
    struct ChatModel      *model;
    struct ChatView       *view;
    struct ChatController *controller;
} ChatApp;

extern ChatApp g_chat_app;

void chat_init(void);

#ifdef __cplusplus
}
#endif

#endif // CHAT_APP_H
