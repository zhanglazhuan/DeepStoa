#ifndef TOVO_STORAGE_H
#define TOVO_STORAGE_H

#include <stdbool.h>
struct TovoModel;

void tovo_storage_load(struct TovoModel *model);
bool tovo_storage_save(const struct TovoModel *model);

#endif
