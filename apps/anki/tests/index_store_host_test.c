#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flash_control.h"
#include "index_store.h"
#include "blob_store.h"

#define TEST_KV_CAPACITY 512
#define TEST_SECTOR_BYTES 4096

struct fdb_kvdb g_kvdb;
static struct {
    char key[32];
    unsigned char *data;
    size_t len;
} kvs[TEST_KV_CAPACITY];
static size_t kv_count;
static int fail_after_sets = -1;
static fdb_err_t fail_error = 1;
static unsigned set_count;
static unsigned get_count;
static unsigned del_count;

fdb_blob_t fdb_blob_make(struct fdb_blob *blob, const void *buf, size_t size)
{
    *blob = (struct fdb_blob){.buf = buf, .size = size};
    return blob;
}

static int find_key(const char *key)
{
    for (size_t i = 0; i < kv_count; i++) {
        if (strcmp(kvs[i].key, key) == 0) return (int)i;
    }
    return -1;
}

size_t fdb_kv_get_blob(struct fdb_kvdb *db, const char *key, fdb_blob_t blob)
{
    (void)db;
    get_count++;
    int i = find_key(key);
    blob->saved.len = i < 0 ? 0 : kvs[i].len;
    if (i < 0 || !blob->buf) return 0;
    size_t read = kvs[i].len < blob->size ? kvs[i].len : blob->size;
    memcpy((void *)blob->buf, kvs[i].data, read);
    return read;
}

fdb_err_t fdb_kv_set_blob(struct fdb_kvdb *db, const char *key, fdb_blob_t blob)
{
    (void)db;
    if (fail_after_sets == 0) return fail_error;
    if (fail_after_sets > 0) fail_after_sets--;
    if (blob->size + strlen(key) + 64 > TEST_SECTOR_BYTES) return 1;
    int i = find_key(key);
    if (i < 0) {
        if (kv_count == TEST_KV_CAPACITY) return 1;
        i = (int)kv_count++;
        memset(&kvs[i], 0, sizeof(kvs[i]));
        strcpy(kvs[i].key, key);
    }
    unsigned char *next = malloc(blob->size);
    assert(next);
    memcpy(next, blob->buf, blob->size);
    free(kvs[i].data);
    kvs[i].data = next;
    kvs[i].len = blob->size;
    set_count++;
    return FDB_NO_ERR;
}

fdb_err_t fdb_kv_del(struct fdb_kvdb *db, const char *key)
{
    (void)db;
    del_count++;
    int i = find_key(key);
    if (i < 0) return FDB_KV_NAME_ERR;
    free(kvs[i].data);
    if ((size_t)i != kv_count - 1) kvs[i] = kvs[kv_count - 1];
    kv_count--;
    memset(&kvs[kv_count], 0, sizeof(kvs[kv_count]));
    return FDB_NO_ERR;
}

int main(void)
{
    const uint32_t deck_id = 42;
    CardIndex legacy_cards[2] = {{.card_id = 1, .deck_id = deck_id},
                                  {.card_id = 2, .deck_id = deck_id}};
    AnkiSourceIndex legacy_sources[2] = {{.card_id = 1}, {.card_id = 2}};
    struct fdb_blob blob;
    assert(fdb_kv_set_blob(&g_kvdb, "anki_cards_42",
           fdb_blob_make(&blob, legacy_cards, sizeof(legacy_cards))) == FDB_NO_ERR);
    assert(fdb_kv_set_blob(&g_kvdb, "anki_src_42",
           fdb_blob_make(&blob, legacy_sources, sizeof(legacy_sources))) == FDB_NO_ERR);
    CardIndex *read_cards = NULL;
    uint16_t count = 0;
    assert(anki_index_load_cards(deck_id, &read_cards, &count));
    assert(count == 2 && read_cards[1].card_id == 2);
    free(read_cards);
    AnkiSourceIndex *read_sources = NULL;
    assert(anki_index_load_sources(deck_id, &read_sources, &count));
    assert(count == 2 && read_sources[1].card_id == 2);
    free(read_sources);

    CardIndex small_card = {.card_id = 1001, .deck_id = 99};
    AnkiSourceIndex small_source = {.card_id = 1001};
    unsigned reads_before_small_save = get_count + del_count;
    assert(anki_index_save(99, &small_card, &small_source, 1));
    assert(get_count + del_count - reads_before_small_save < 20);
    reads_before_small_save = get_count + del_count;
    assert(anki_index_save(99, &small_card, &small_source, 1));
    assert(get_count + del_count - reads_before_small_save < 20);

    CardIndex *cards = calloc(ANKI_MAX_CARDS_PER_DECK, sizeof(*cards));
    AnkiSourceIndex *sources = calloc(ANKI_MAX_CARDS_PER_DECK,
                                     sizeof(*sources));
    assert(cards && sources);
    for (unsigned i = 0; i < ANKI_MAX_CARDS_PER_DECK; i++) {
        cards[i].card_id = i + 1;
        cards[i].deck_id = deck_id;
        sources[i].card_id = i + 1;
    }
    assert(anki_index_save(deck_id, cards, sources, ANKI_MAX_CARDS_PER_DECK));
    assert(!anki_index_save(deck_id, cards, sources,
                            ANKI_MAX_CARDS_PER_DECK + 1));
    assert(anki_index_is_chunked(deck_id));
    assert(anki_index_load_cards(deck_id, &read_cards, &count));
    assert(count == 9999 && read_cards[9998].card_id == 9999);
    free(read_cards);
    assert(anki_index_load_sources(deck_id, &read_sources, &count));
    assert(count == 9999 && read_sources[9998].card_id == 9999);
    free(read_sources);

    cards[505].status = ANKI_CARD_STATUS_REVIEW;
    unsigned writes_before = set_count;
    unsigned reads_before = get_count;
    assert(anki_index_update_card(&cards[505]));
    assert(set_count == writes_before + 1);
    assert(get_count - reads_before < 10);
    assert(anki_index_load_cards(deck_id, &read_cards, &count));
    assert(read_cards[505].status == ANKI_CARD_STATUS_REVIEW);
    free(read_cards);

    cards[0].status = ANKI_CARD_STATUS_REVIEW;
    fail_after_sets = 5;
    fail_error = FDB_SAVED_FULL;
    assert(!anki_index_save(deck_id, cards, sources, ANKI_MAX_CARDS_PER_DECK));
    assert(anki_index_last_write_full());
    assert(find_key("aP42") >= 0);
    fail_after_sets = -1;
    fail_error = 1;
    assert(anki_index_load_cards(deck_id, &read_cards, &count));
    assert(read_cards[0].status == ANKI_CARD_STATUS_NEW);
    assert(read_cards[505].status == ANKI_CARD_STATUS_REVIEW);
    free(read_cards);

    assert(anki_index_save(deck_id, cards, sources, ANKI_MAX_CARDS_PER_DECK));
    assert(find_key("aP42") < 0);
    assert(anki_index_load_cards(deck_id, &read_cards, &count));
    assert(read_cards[0].status == ANKI_CARD_STATUS_REVIEW);
    free(read_cards);
    assert(anki_index_delete(deck_id));
    assert(anki_index_load_cards(deck_id, &read_cards, &count));
    assert(count == 0 && read_cards == NULL);
    free(cards);
    free(sources);
    enum { DECK_BYTES = 172, TRASH_BYTES = 328, ITEMS = 99 };
    unsigned char *records = calloc(ITEMS, TRASH_BYTES);
    unsigned char *loaded = calloc(ITEMS, TRASH_BYTES);
    assert(records && loaded);
    for (unsigned i = 0; i < ITEMS; i++) records[i * TRASH_BYTES] = (unsigned char)i;
    assert(anki_blob_save("anki_trash", "at", TRASH_BYTES, ITEMS,
                          records, ITEMS));
    assert(anki_blob_load("anki_trash", "at", TRASH_BYTES, ITEMS,
                          loaded, &count));
    assert(count == ITEMS && memcmp(records, loaded, ITEMS * TRASH_BYTES) == 0);
    records[0] = 200;
    fail_after_sets = 3;
    assert(!anki_blob_save("anki_trash", "at", TRASH_BYTES, ITEMS,
                           records, ITEMS));
    fail_after_sets = -1;
    assert(anki_blob_load("anki_trash", "at", TRASH_BYTES, ITEMS,
                          loaded, &count));
    assert(count == ITEMS && loaded[0] == 0);
    assert(anki_blob_save("anki_trash", "at", TRASH_BYTES, ITEMS,
                          records, ITEMS));
    assert(anki_blob_load("anki_trash", "at", TRASH_BYTES, ITEMS,
                          loaded, &count));
    assert(count == ITEMS && loaded[0] == 200);
    assert(anki_blob_delete("anki_trash", "at", TRASH_BYTES, ITEMS));

    memset(records, 0x5a, 3 * DECK_BYTES);
    assert(fdb_kv_set_blob(&g_kvdb, "anki_decks",
           fdb_blob_make(&blob, records, 3 * DECK_BYTES)) == FDB_NO_ERR);
    assert(anki_blob_load("anki_decks", "ad", DECK_BYTES, ITEMS,
                          loaded, &count));
    assert(count == 3 && memcmp(records, loaded, 3 * DECK_BYTES) == 0);
    assert(anki_blob_save("anki_decks", "ad", DECK_BYTES, ITEMS,
                          records, ITEMS));
    assert(anki_blob_load("anki_decks", "ad", DECK_BYTES, ITEMS,
                          loaded, &count));
    assert(count == ITEMS && memcmp(records, loaded, ITEMS * DECK_BYTES) == 0);
    free(records);
    free(loaded);
    puts("index_store_host_test: PASS");
    return 0;
}
