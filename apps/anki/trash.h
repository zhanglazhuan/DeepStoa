#ifndef ANKI_TRASH_H
#define ANKI_TRASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "model.h"

#define ANKI_TRASH_ROOT "/sdcard/anki/.trash"
#define ANKI_TRASH_RETENTION_DAYS 30U

typedef struct {
    uint32_t group_id;
    uint32_t deleted_at;
    AnkiDeck deck;
    char trash_path[128];
    uint64_t source_hash;
    uint32_t source_size;
} AnkiTrashEntry;

void anki_trash_reconcile_external_restores(AnkiModel *model);
void anki_trash_cleanup_expired(AnkiModel *model);
bool anki_trash_factory_reset(void);
bool anki_trash_recycle_deck(AnkiModel *model, int deck_idx);
bool anki_trash_delete_active_permanently(AnkiModel *model, int deck_idx);

size_t anki_trash_group_count(void);
bool anki_trash_get_group(size_t index, AnkiTrashEntry *entry);
bool anki_trash_restore_group(AnkiModel *model, uint32_t group_id);
bool anki_trash_restore_group_as_copy(AnkiModel *model, uint32_t group_id);
bool anki_trash_delete_group_permanently(uint32_t group_id);

#endif
