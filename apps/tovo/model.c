#include "model.h"

#include <stdlib.h>
#include "fs_control.h"
#include "storage.h"

bool tovo_model_init(TovoModel **out)
{
    if (!out) return false;
    *out = calloc(1, sizeof(**out));
    if (!*out) return false;
    tovo_model_reload(*out);
    return true;
}

void tovo_model_deinit(TovoModel *model)
{
    free(model);
}

void tovo_model_reload(TovoModel *model)
{
    if (!model) return;
    model->count = 0;
    model->mounted = fs_control_is_mounted();
    model->load_error = false;
    model->read_only = false;
    model->save_error = false;
    if (model->mounted) tovo_storage_load(model);
}

bool tovo_model_toggle(TovoModel *model, size_t index)
{
    if (!model || !model->mounted || model->load_error ||
        model->read_only || index >= model->count) return false;
    model->items[index].completed = !model->items[index].completed;
    if (!tovo_storage_save(model)) {
        model->items[index].completed = !model->items[index].completed;
        model->save_error = true;
        return false;
    }
    model->save_error = false;
    return true;
}
