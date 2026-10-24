#ifndef ANKI_INDEX_STORE_H
#define ANKI_INDEX_STORE_H

#include <stdbool.h>
#include <stdint.h>
#include "model.h"

typedef struct {
    uint32_t card_id;
    uint64_t line_hash;
    uint64_t front_hash;
} AnkiSourceIndex;

/* 100 cards (2400 bytes) fit within one current FlashDB 4 KiB sector. */
#define ANKI_INDEX_PART_CARDS 100U

bool anki_index_load_cards(uint32_t deck_id, CardIndex **cards,
                           uint16_t *count);
bool anki_index_load_sources(uint32_t deck_id, AnkiSourceIndex **sources,
                             uint16_t *count);
bool anki_index_is_chunked(uint32_t deck_id);
bool anki_index_save(uint32_t deck_id, const CardIndex *cards,
                     const AnkiSourceIndex *sources, uint16_t count);
bool anki_index_update_card(const CardIndex *card);
bool anki_index_delete(uint32_t deck_id);
bool anki_index_last_write_full(void);
void anki_index_cache_clear(void);

#endif
