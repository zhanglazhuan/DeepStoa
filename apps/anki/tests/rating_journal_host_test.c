#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "flash_control.h"
#include "rating_journal.c"

struct fdb_kvdb g_kvdb;
static unsigned char saved_journal[256];
static size_t saved_len;
static CardIndex stored_card;
static AnkiScheduleState stored_schedule;
static bool schedule_exists;
static bool fail_card_write;

fdb_blob_t fdb_blob_make(struct fdb_blob *blob, const void *buf, size_t size)
{
    *blob = (struct fdb_blob){.buf = buf, .size = size};
    return blob;
}

size_t fdb_kv_get_blob(struct fdb_kvdb *db, const char *key, fdb_blob_t blob)
{
    (void)db;
    assert(strcmp(key, "anki_rating_txn") == 0);
    blob->saved.len = saved_len;
    if (!blob->buf || !saved_len) return 0;
    size_t len = saved_len < blob->size ? saved_len : blob->size;
    memcpy((void *)blob->buf, saved_journal, len);
    return len;
}

fdb_err_t fdb_kv_set_blob(struct fdb_kvdb *db, const char *key, fdb_blob_t blob)
{
    (void)db;
    assert(strcmp(key, "anki_rating_txn") == 0);
    assert(blob->size <= sizeof(saved_journal));
    memcpy(saved_journal, blob->buf, blob->size);
    saved_len = blob->size;
    return FDB_NO_ERR;
}

fdb_err_t fdb_kv_del(struct fdb_kvdb *db, const char *key)
{
    (void)db;
    assert(strcmp(key, "anki_rating_txn") == 0);
    saved_len = 0;
    return FDB_NO_ERR;
}

bool anki_storage_save_schedule(const AnkiScheduleState *schedule)
{
    stored_schedule = *schedule;
    schedule_exists = true;
    return true;
}

bool anki_storage_delete_schedule(uint32_t card_id)
{
    assert(card_id == stored_card.card_id);
    schedule_exists = false;
    return true;
}

bool anki_index_update_card(const CardIndex *card)
{
    if (fail_card_write) return false;
    stored_card = *card;
    return true;
}

int main(void)
{
    CardIndex before = {.card_id = 42, .deck_id = 2,
                        .status = ANKI_CARD_STATUS_NEW};
    AnkiScheduleState old_schedule = {.card_id = 42};
    stored_card = before;
    stored_schedule = old_schedule;
    schedule_exists = true;
    assert(anki_storage_rating_begin(&before, &old_schedule, true));
    assert(!anki_storage_rating_begin(&before, &old_schedule, true));
    stored_card.status = ANKI_CARD_STATUS_REVIEW;
    stored_schedule.reps = 1;
    fail_card_write = true;
    assert(!anki_storage_rating_recover());
    assert(saved_len != 0);
    fail_card_write = false;
    assert(anki_storage_rating_recover());
    assert(saved_len == 0 && stored_card.status == ANKI_CARD_STATUS_NEW);
    assert(schedule_exists && stored_schedule.reps == 0);

    schedule_exists = false;
    assert(anki_storage_rating_begin(&before, &old_schedule, false));
    schedule_exists = true;
    stored_card.status = ANKI_CARD_STATUS_REVIEW;
    assert(anki_storage_rating_recover());
    assert(!schedule_exists && stored_card.status == ANKI_CARD_STATUS_NEW);

    assert(anki_storage_rating_begin(&before, &old_schedule, false));
    assert(anki_storage_rating_finish());
    assert(saved_len == 0);
    puts("rating_journal_host_test: PASS");
    return 0;
}
