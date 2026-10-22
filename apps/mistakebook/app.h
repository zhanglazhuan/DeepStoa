#ifndef MISTAKEBOOK_APP_H
#define MISTAKEBOOK_APP_H

struct MistakebookModel;
struct MistakebookView;

typedef struct MistakebookApp {
    struct MistakebookModel *model;
    struct MistakebookView *view;
} MistakebookApp;

extern MistakebookApp g_mistakebook_app;

void mistakebook_init(void);

#endif
