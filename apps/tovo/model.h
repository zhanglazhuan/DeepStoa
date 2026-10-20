#ifndef TOVO_MODEL_H
#define TOVO_MODEL_H

#include <stdbool.h>
#include <stddef.h>

#define TOVO_MAX 12
#define TOVO_CONTENT_SIZE 160

typedef struct {
    char content[TOVO_CONTENT_SIZE];
    bool completed;
} TovoItem;

typedef struct TovoModel {
    TovoItem items[TOVO_MAX];
    size_t count;
    bool mounted;
    bool load_error;
    bool read_only;
    bool save_error;
} TovoModel;

bool tovo_model_init(TovoModel **out);
void tovo_model_deinit(TovoModel *model);
void tovo_model_reload(TovoModel *model);
bool tovo_model_toggle(TovoModel *model, size_t index);

#endif
