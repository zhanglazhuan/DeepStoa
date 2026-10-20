#ifndef ANKI_SOURCE_SYNC_H
#define ANKI_SOURCE_SYNC_H

#include <stdbool.h>
#include <stdint.h>

#include "model.h"

#define ANKI_SOURCE_ROOT "/sdcard/anki"
#define ANKI_MAX_SOURCE_LINES 10000U
#define ANKI_MAX_SOURCE_BYTES (2U * 1024U * 1024U)

typedef enum {
    ANKI_SYNC_OK = 0,
    ANKI_SYNC_ERR_ARGUMENT,
    ANKI_SYNC_ERR_IO,
    ANKI_SYNC_ERR_TOO_LARGE,
    ANKI_SYNC_ERR_TOO_MANY_LINES,
    ANKI_SYNC_ERR_TOO_MANY_CARDS,
    ANKI_SYNC_ERR_FORMAT,
    ANKI_SYNC_ERR_MEMORY,
    ANKI_SYNC_ERR_STORAGE,
    ANKI_SYNC_ERR_ID_EXHAUSTED,
} AnkiSyncError;

typedef struct {
    uint16_t added;
    uint16_t updated;
    uint16_t unchanged;
    uint16_t missing;
    uint16_t purged;
    uint16_t skipped;
    AnkiSyncError error;
} AnkiSyncResult;

bool anki_source_sync_file(AnkiModel *model, const char *path, AnkiSyncResult *result);
void anki_source_sync_all(AnkiModel *model);
bool anki_source_load_payload(const AnkiModel *model, const CardIndex *card,
                              CardPayload *payload);

#endif
