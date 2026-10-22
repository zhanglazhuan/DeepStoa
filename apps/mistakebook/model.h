#ifndef MISTAKEBOOK_MODEL_H
#define MISTAKEBOOK_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#define MISTAKEBOOK_MAX_CARDS 32U
#define MISTAKEBOOK_FIELD_LEN 160U

typedef struct {
    char subject[MISTAKEBOOK_FIELD_LEN];
    char title[MISTAKEBOOK_FIELD_LEN];
    char question[MISTAKEBOOK_FIELD_LEN];
    char student_answer[MISTAKEBOOK_FIELD_LEN];
    char correct_answer[MISTAKEBOOK_FIELD_LEN];
    char knowledge_point[MISTAKEBOOK_FIELD_LEN];
    char error_type[MISTAKEBOOK_FIELD_LEN];
    char explanation[MISTAKEBOOK_FIELD_LEN];
    bool review_pending;
    bool legacy_format;
} MistakebookCard;

typedef struct MistakebookModel {
    MistakebookCard *cards;
    uint32_t count;
    uint32_t selected;
    bool read_only; /* Source had rows that cannot safely be rewritten. */
    bool load_error;
} MistakebookModel;

bool mistakebook_model_init(MistakebookModel **out);
void mistakebook_model_deinit(MistakebookModel *model);
void mistakebook_model_reload(MistakebookModel *model);
bool mistakebook_model_mark_reviewed(MistakebookModel *model);

#endif
