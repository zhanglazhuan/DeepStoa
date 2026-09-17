#ifndef PLAYER_APP_H
#define PLAYER_APP_H

#include <lvgl.h>
#include "page_navigator.h"

#ifdef __cplusplus
extern "C" {
#endif

struct PlayerModel;
struct PlayerView;
struct PlayerController;

typedef struct PlayerApp {
    struct PlayerModel      *model;
    struct PlayerView       *view;
    struct PlayerController *controller;
} PlayerApp;

extern PlayerApp g_player_app;

#ifdef __cplusplus
}
#endif

#endif // PLAYER_APP_H
