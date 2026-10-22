#ifndef POMODORO_APP_H
#define POMODORO_APP_H

struct PomodoroModel;
struct PomodoroController;
struct PomodoroView;

typedef struct PomodoroApp {
    struct PomodoroModel *model;
    struct PomodoroController *controller;
    struct PomodoroView *view;
} PomodoroApp;

extern PomodoroApp g_pomodoro_app;
void pomodoro_init(void);

#endif
