#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "flash_control.h"
#include "index_store.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

static const char *TAG = "anki_index";

#define INDEX_MAGIC UINT32_C(0x41494E58)
#define INDEX_VERSION 1U
#define INDEX_PENDING_MAGIC UINT32_C(0x41504E44)
#define INDEX_MAX_PARTS ((ANKI_MAX_CARDS_PER_DECK + ANKI_INDEX_PART_CARDS - 1) / ANKI_INDEX_PART_CARDS)

_Static_assert(sizeof(CardIndex) == 24 && sizeof(AnkiSourceIndex) == 24,
               "100-card records must fit a FlashDB sector");

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    uint8_t slot;
    uint8_t reserved[3];
} IndexManifest;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t target_count;
    uint16_t old_count;
    uint8_t target_slot;
    uint8_t old_slot;
    uint8_t old_exists;
    uint8_t reserved;
} PendingIndexSave;

#define INDEX_CACHE_DECKS 4U
typedef struct {
    uint32_t deck_id;
    uint32_t *ids;
    uint32_t age;
    uint16_t count;
    uint8_t slot;
} IndexCache;

static IndexCache s_cache[INDEX_CACHE_DECKS];
static uint32_t s_cache_age;

void anki_index_cache_clear(void)
{
    for (unsigned i = 0; i < INDEX_CACHE_DECKS; i++) {
        free(s_cache[i].ids);
        memset(&s_cache[i], 0, sizeof(s_cache[i]));
    }
    s_cache_age = 0;
}

static void cache_invalidate(uint32_t deck_id)
{
    for (unsigned i = 0; i < INDEX_CACHE_DECKS; i++) {
        if (s_cache[i].ids && s_cache[i].deck_id == deck_id) {
            free(s_cache[i].ids);
            memset(&s_cache[i], 0, sizeof(s_cache[i]));
        }
    }
}

static void cache_cards(uint32_t deck_id, const IndexManifest *manifest,
                        const CardIndex *cards)
{
    if (!manifest->count) return;
    IndexCache *entry = NULL;
    for (unsigned i = 0; i < INDEX_CACHE_DECKS; i++) {
        if (s_cache[i].ids && s_cache[i].deck_id == deck_id) {
            entry = &s_cache[i];
            break;
        }
    }
    if (entry && entry->count == manifest->count &&
        entry->slot == manifest->slot) {
        entry->age = ++s_cache_age;
        return;
    }
    uint32_t *ids = malloc((size_t)manifest->count * sizeof(*ids));
    if (!ids) return;
    for (uint16_t i = 0; i < manifest->count; i++) ids[i] = cards[i].card_id;
    if (!entry) {
        entry = &s_cache[0];
        for (unsigned i = 0; i < INDEX_CACHE_DECKS; i++) {
            if (!s_cache[i].ids) { entry = &s_cache[i]; break; }
            if (s_cache[i].age < entry->age) entry = &s_cache[i];
        }
    }
    free(entry->ids);
    *entry = (IndexCache){.deck_id = deck_id, .ids = ids,
                           .age = ++s_cache_age,
                           .count = manifest->count, .slot = manifest->slot};
}

static int cache_part(uint32_t deck_id, const IndexManifest *manifest,
                      uint32_t card_id)
{
    for (unsigned i = 0; i < INDEX_CACHE_DECKS; i++) {
        IndexCache *entry = &s_cache[i];
        if (!entry->ids || entry->deck_id != deck_id ||
            entry->slot != manifest->slot || entry->count != manifest->count)
            continue;
        entry->age = ++s_cache_age;
        for (uint16_t c = 0; c < entry->count; c++) {
            if (entry->ids[c] == card_id) return c / ANKI_INDEX_PART_CARDS;
        }
    }
    return -1;
}

static void manifest_key(char *key, size_t size, uint32_t deck_id)
{
    snprintf(key, size, "aM%u", (unsigned)deck_id);
}

static void pending_key(char *key, size_t size, uint32_t deck_id)
{
    snprintf(key, size, "aP%u", (unsigned)deck_id);
}

static void part_key(char *key, size_t size, uint32_t deck_id,
                     char kind, uint8_t slot, unsigned part)
{
    snprintf(key, size, "a%c%u_%u_%u", kind, (unsigned)deck_id,
             (unsigned)slot, part);
}

static void legacy_key(char *key, size_t size, uint32_t deck_id, char kind)
{
    snprintf(key, size, kind == 'C' ? "anki_cards_%u" : "anki_src_%u",
             (unsigned)deck_id);
}

static size_t blob_size(const char *key)
{
    struct fdb_blob blob;
    fdb_kv_get_blob(&g_kvdb, key, fdb_blob_make(&blob, NULL, 0));
    return blob.saved.len;
}

static bool delete_if_present(const char *key)
{
    fdb_err_t err = fdb_kv_del(&g_kvdb, key);
    return err == FDB_NO_ERR || err == FDB_KV_NAME_ERR;
}

static bool read_manifest(uint32_t deck_id, IndexManifest *manifest,
                          bool *exists)
{
    char key[32];
    manifest_key(key, sizeof(key), deck_id);
    size_t len = blob_size(key);
    *exists = len != 0;
    if (!*exists) return true;
    if (len != sizeof(*manifest)) return false;
    struct fdb_blob blob;
    if (fdb_kv_get_blob(&g_kvdb, key,
                        fdb_blob_make(&blob, manifest, sizeof(*manifest))) != len) {
        return false;
    }
    return manifest->magic == INDEX_MAGIC &&
           manifest->version == INDEX_VERSION &&
           manifest->count <= ANKI_MAX_CARDS_PER_DECK &&
           manifest->slot <= 1;
}

static bool read_pending(uint32_t deck_id, PendingIndexSave *pending,
                         bool *exists)
{
    char key[32];
    pending_key(key, sizeof(key), deck_id);
    size_t len = blob_size(key);
    *exists = len != 0;
    if (!*exists) return true;
    if (len != sizeof(*pending)) return false;
    struct fdb_blob blob;
    if (fdb_kv_get_blob(&g_kvdb, key,
                        fdb_blob_make(&blob, pending, len)) != len) return false;
    return pending->magic == INDEX_PENDING_MAGIC &&
           pending->version == 1 &&
           pending->target_count <= ANKI_MAX_CARDS_PER_DECK &&
           pending->old_count <= ANKI_MAX_CARDS_PER_DECK &&
           pending->target_slot <= 1 && pending->old_slot <= 1 &&
           pending->old_exists <= 1;
}

static bool load_legacy(uint32_t deck_id, char kind, size_t item_size,
                        void **items, uint16_t *count)
{
    char key[32];
    legacy_key(key, sizeof(key), deck_id, kind);
    size_t len = blob_size(key);
    if (len == 0) return true;
    if (len % item_size || len > item_size * ANKI_MAX_CARDS_PER_DECK) {
        ESP_LOGE(TAG, "invalid legacy index %s: %u bytes", key, (unsigned)len);
        return false;
    }
    void *data = malloc(len);
    if (!data) return false;
    struct fdb_blob blob;
    if (fdb_kv_get_blob(&g_kvdb, key, fdb_blob_make(&blob, data, len)) != len) {
        free(data);
        return false;
    }
    *items = data;
    *count = (uint16_t)(len / item_size);
    return true;
}

static bool load_index(uint32_t deck_id, char kind, size_t item_size,
                       void **items, uint16_t *count)
{
    *items = NULL;
    *count = 0;
    IndexManifest manifest;
    bool exists;
    if (!read_manifest(deck_id, &manifest, &exists)) {
        ESP_LOGE(TAG, "invalid manifest for deck %u", (unsigned)deck_id);
        return false;
    }
    if (!exists) return load_legacy(deck_id, kind, item_size, items, count);
    if (!manifest.count) return true;

    size_t len = (size_t)manifest.count * item_size;
    uint8_t *data = malloc(len);
    if (!data) {
        ESP_LOGE(TAG, "out of memory loading %u index bytes", (unsigned)len);
        return false;
    }
    unsigned parts = (manifest.count + ANKI_INDEX_PART_CARDS - 1) /
                     ANKI_INDEX_PART_CARDS;
    for (unsigned part = 0; part < parts; part++) {
        char key[32];
        part_key(key, sizeof(key), deck_id, kind, manifest.slot, part);
        unsigned offset = part * ANKI_INDEX_PART_CARDS;
        unsigned entries = manifest.count - offset;
        if (entries > ANKI_INDEX_PART_CARDS) entries = ANKI_INDEX_PART_CARDS;
        size_t bytes = entries * item_size;
        struct fdb_blob blob;
        if (blob_size(key) != bytes ||
            fdb_kv_get_blob(&g_kvdb, key,
                            fdb_blob_make(&blob, data + offset * item_size,
                                          bytes)) != bytes) {
            ESP_LOGE(TAG, "missing/corrupt index part %s", key);
            free(data);
            return false;
        }
    }
    *items = data;
    *count = manifest.count;
    if (kind == 'C') cache_cards(deck_id, &manifest, (const CardIndex *)data);
    return true;
}

bool anki_index_load_cards(uint32_t deck_id, CardIndex **cards,
                           uint16_t *count)
{
    if (!cards || !count) return false;
    return load_index(deck_id, 'C', sizeof(**cards), (void **)cards, count);
}

bool anki_index_load_sources(uint32_t deck_id, AnkiSourceIndex **sources,
                             uint16_t *count)
{
    if (!sources || !count) return false;
    return load_index(deck_id, 'S', sizeof(**sources), (void **)sources, count);
}

bool anki_index_is_chunked(uint32_t deck_id)
{
    IndexManifest manifest;
    bool exists;
    return read_manifest(deck_id, &manifest, &exists) && exists;
}

static bool delete_slot(uint32_t deck_id, uint8_t slot, unsigned parts)
{
    bool ok = true;
    for (unsigned part = 0; part < parts; part++) {
#ifdef ESP_PLATFORM
        if ((part & 3U) == 0) vTaskDelay(1);
#endif
        char key[32];
        part_key(key, sizeof(key), deck_id, 'C', slot, part);
        if (!delete_if_present(key)) ok = false;
        part_key(key, sizeof(key), deck_id, 'S', slot, part);
        if (!delete_if_present(key)) ok = false;
    }
    return ok;
}

static bool recover_pending(uint32_t deck_id,
                            const IndexManifest *active, bool active_exists)
{
    PendingIndexSave pending;
    bool exists;
    if (!read_pending(deck_id, &pending, &exists)) return false;
    if (!exists) return true;
    bool committed = active_exists && active->slot == pending.target_slot &&
                     active->count == pending.target_count;
    unsigned count = committed ? pending.old_count : pending.target_count;
    uint8_t slot = committed ? pending.old_slot : pending.target_slot;
    if (count > 0 && !delete_slot(deck_id, slot,
        (count + ANKI_INDEX_PART_CARDS - 1) / ANKI_INDEX_PART_CARDS)) {
        return false;
    }
    char key[32];
    pending_key(key, sizeof(key), deck_id);
    return delete_if_present(key);
}

static bool s_last_write_full;

bool anki_index_last_write_full(void)
{
    return s_last_write_full;
}

bool anki_index_save(uint32_t deck_id, const CardIndex *cards,
                     const AnkiSourceIndex *sources, uint16_t count)
{
    s_last_write_full = false;
    if (count > ANKI_MAX_CARDS_PER_DECK || (count && (!cards || !sources))) {
        return false;
    }
    IndexManifest previous;
    bool exists;
    if (!read_manifest(deck_id, &previous, &exists)) return false;
    if (!recover_pending(deck_id, &previous, exists)) return false;
    uint8_t slot = exists ? (uint8_t)(1 - previous.slot) : 0;
    PendingIndexSave pending = {
        .magic = INDEX_PENDING_MAGIC,
        .version = 1,
        .target_count = count,
        .old_count = exists ? previous.count : 0,
        .target_slot = slot,
        .old_slot = exists ? previous.slot : 0,
        .old_exists = exists,
    };
    char key[32];
    pending_key(key, sizeof(key), deck_id);
    struct fdb_blob blob;
    fdb_err_t err = fdb_kv_set_blob(&g_kvdb, key,
                                   fdb_blob_make(&blob, &pending,
                                                 sizeof(pending)));
    if (err != FDB_NO_ERR) {
        s_last_write_full = err == FDB_SAVED_FULL;
        return false;
    }
    unsigned parts = (count + ANKI_INDEX_PART_CARDS - 1) /
                     ANKI_INDEX_PART_CARDS;
    for (unsigned part = 0; part < parts; part++) {
#ifdef ESP_PLATFORM
        if ((part & 3U) == 0) vTaskDelay(1);
#endif
        unsigned offset = part * ANKI_INDEX_PART_CARDS;
        unsigned entries = count - offset;
        if (entries > ANKI_INDEX_PART_CARDS) entries = ANKI_INDEX_PART_CARDS;
        part_key(key, sizeof(key), deck_id, 'S', slot, part);
        err = fdb_kv_set_blob(&g_kvdb, key,
                fdb_blob_make(&blob, sources + offset,
                              entries * sizeof(*sources)));
        if (err != FDB_NO_ERR) {
            s_last_write_full = err == FDB_SAVED_FULL;
            return false;
        }
        part_key(key, sizeof(key), deck_id, 'C', slot, part);
        err = fdb_kv_set_blob(&g_kvdb, key,
                fdb_blob_make(&blob, cards + offset,
                              entries * sizeof(*cards)));
        if (err != FDB_NO_ERR) {
            s_last_write_full = err == FDB_SAVED_FULL;
            return false;
        }
    }

    IndexManifest next = {
        .magic = INDEX_MAGIC,
        .version = INDEX_VERSION,
        .count = count,
        .slot = slot,
    };
    manifest_key(key, sizeof(key), deck_id);
    err = fdb_kv_set_blob(&g_kvdb, key,
                                   fdb_blob_make(&blob, &next, sizeof(next)));
    if (err != FDB_NO_ERR) {
        s_last_write_full = err == FDB_SAVED_FULL;
        return false;
    }

    /* The manifest is the commit point. Cleanup failures leave only reclaimable
     * records; a retry can still read the committed generation. */
    cache_invalidate(deck_id);
    legacy_key(key, sizeof(key), deck_id, 'C');
    delete_if_present(key);
    legacy_key(key, sizeof(key), deck_id, 'S');
    delete_if_present(key);
    if (exists) {
        unsigned old_parts = (previous.count + ANKI_INDEX_PART_CARDS - 1) /
                             ANKI_INDEX_PART_CARDS;
        if (delete_slot(deck_id, previous.slot, old_parts)) {
            pending_key(key, sizeof(key), deck_id);
            delete_if_present(key);
        }
    } else {
        pending_key(key, sizeof(key), deck_id);
        delete_if_present(key);
    }
    return true;
}

bool anki_index_update_card(const CardIndex *card)
{
    if (!card) return false;
    IndexManifest manifest;
    bool exists;
    if (!read_manifest(card->deck_id, &manifest, &exists)) return false;
    if (!exists) {
        CardIndex *cards = NULL;
        uint16_t count;
        if (!anki_index_load_cards(card->deck_id, &cards, &count)) return false;
        bool found = false;
        for (uint16_t i = 0; i < count; i++) {
            if (cards[i].card_id == card->card_id) {
                cards[i] = *card;
                found = true;
                break;
            }
        }
        char key[32];
        legacy_key(key, sizeof(key), card->deck_id, 'C');
        struct fdb_blob blob;
        bool ok = found && fdb_kv_set_blob(&g_kvdb, key,
            fdb_blob_make(&blob, cards, count * sizeof(*cards))) == FDB_NO_ERR;
        free(cards);
        return ok;
    }

    CardIndex *part_cards = malloc(ANKI_INDEX_PART_CARDS * sizeof(*part_cards));
    if (!part_cards) return false;
    unsigned parts = (manifest.count + ANKI_INDEX_PART_CARDS - 1) /
                     ANKI_INDEX_PART_CARDS;
    int preferred = cache_part(card->deck_id, &manifest, card->card_id);
    for (unsigned attempt = 0; attempt < parts; attempt++) {
#ifdef ESP_PLATFORM
        if ((attempt & 3U) == 0) vTaskDelay(1);
#endif
        unsigned part = attempt;
        if (preferred >= 0) {
            if (attempt == 0) part = (unsigned)preferred;
            else if (attempt <= (unsigned)preferred) part = attempt - 1;
        }
        char key[32];
        part_key(key, sizeof(key), card->deck_id, 'C', manifest.slot, part);
        unsigned entries = manifest.count - part * ANKI_INDEX_PART_CARDS;
        if (entries > ANKI_INDEX_PART_CARDS) entries = ANKI_INDEX_PART_CARDS;
        size_t len = entries * sizeof(*part_cards);
        struct fdb_blob blob;
        if (fdb_kv_get_blob(&g_kvdb, key,
                            fdb_blob_make(&blob, part_cards, len)) != len) {
            free(part_cards);
            return false;
        }
        for (unsigned i = 0; i < entries; i++) {
            if (part_cards[i].card_id != card->card_id) continue;
            part_cards[i] = *card;
            bool saved = fdb_kv_set_blob(&g_kvdb, key,
                fdb_blob_make(&blob, part_cards, len)) == FDB_NO_ERR;
            free(part_cards);
            return saved;
        }
    }
    free(part_cards);
    return false;
}

bool anki_index_delete(uint32_t deck_id)
{
    cache_invalidate(deck_id);
    char key[32];
    manifest_key(key, sizeof(key), deck_id);
    if (!delete_if_present(key)) return false;
    bool ok = delete_slot(deck_id, 0, INDEX_MAX_PARTS);
    if (!delete_slot(deck_id, 1, INDEX_MAX_PARTS)) ok = false;
    legacy_key(key, sizeof(key), deck_id, 'C');
    if (!delete_if_present(key)) ok = false;
    legacy_key(key, sizeof(key), deck_id, 'S');
    if (!delete_if_present(key)) ok = false;
    pending_key(key, sizeof(key), deck_id);
    if (!delete_if_present(key)) ok = false;
    snprintf(key, sizeof(key), "anki_sig_%u", (unsigned)deck_id);
    if (!delete_if_present(key)) ok = false;
    return ok;
}
