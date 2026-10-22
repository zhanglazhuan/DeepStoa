#include "model.h"

#include <stdlib.h>

#include "storage.h"

bool mistakebook_model_init(MistakebookModel **out)
{
    if (!out) return false;
    *out = calloc(1, sizeof(MistakebookModel));
    if (!*out) return false;
    (*out)->cards = calloc(MISTAKEBOOK_MAX_CARDS, sizeof(MistakebookCard));
    if (!(*out)->cards) {
        free(*out);
        *out = NULL;
        return false;
    }
    mistakebook_model_reload(*out);
    return true;
}

void mistakebook_model_deinit(MistakebookModel *model)
{
    if (!model) return;
    free(model->cards);
    free(model);
}

void mistakebook_model_reload(MistakebookModel *model)
{
    if (!model) return;
    model->count = 0;
    model->selected = 0;
    model->read_only = false;
    model->load_error = false;
    mistakebook_storage_load(model);
}

bool mistakebook_model_mark_reviewed(MistakebookModel *model)
{
    if (!model || model->read_only || model->load_error ||
        model->selected >= model->count) return false;
    MistakebookCard *card = &model->cards[model->selected];
    if (!card->review_pending) return true;
    card->review_pending = false;
    if (mistakebook_storage_save(model)) return true;
    card->review_pending = true;
    return false;
}
