#ifndef TOVO_APP_H
#define TOVO_APP_H

struct TovoModel;
struct TovoView;

typedef struct TovoApp {
    struct TovoModel *model;
    struct TovoView *view;
} TovoApp;

extern TovoApp g_tovo_app;
void tovo_init(void);

#endif
