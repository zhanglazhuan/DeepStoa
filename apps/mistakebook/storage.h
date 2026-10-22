#ifndef MISTAKEBOOK_STORAGE_H
#define MISTAKEBOOK_STORAGE_H

#include <stdbool.h>
#include "model.h"

void mistakebook_storage_load(MistakebookModel *model);
bool mistakebook_storage_save(const MistakebookModel *model);

#endif
