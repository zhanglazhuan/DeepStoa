#include <string.h>

#include "esp_log.h"
#include "flash_control.h"
#include "index_store.h"
#include "storage.h"

#define ANKI_RATING_JOURNAL_MAGIC UINT32_C(0x41525458)
#define ANKI_RATING_JOURNAL_KEY "anki_rating_txn"

static const char *TAG = "anki_rating_journal";

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t had_schedule;
    uint8_t reserved;
    CardIndex previous_card;
    AnkiScheduleState previous_schedule;
} anki_rating_journal_t;

bool anki_storage_rating_begin(const CardIndex *previous_card,
                               const AnkiScheduleState *previous_schedule,
                               bool had_schedule)
{
    if (!previous_card || !previous_schedule || previous_card->card_id == 0 ||
        previous_card->card_id != previous_schedule->card_id) return false;
    struct fdb_blob blob;
    fdb_kv_get_blob(&g_kvdb, ANKI_RATING_JOURNAL_KEY,
                    fdb_blob_make(&blob, NULL, 0));
    if (blob.saved.len != 0) return false;
    anki_rating_journal_t journal = {
        .magic = ANKI_RATING_JOURNAL_MAGIC,
        .version = 1,
        .had_schedule = had_schedule,
        .previous_card = *previous_card,
        .previous_schedule = *previous_schedule,
    };
    return fdb_kv_set_blob(&g_kvdb, ANKI_RATING_JOURNAL_KEY,
                           fdb_blob_make(&blob, &journal,
                                         sizeof(journal))) == FDB_NO_ERR;
}

bool anki_storage_rating_finish(void)
{
    struct fdb_blob blob;
    fdb_kv_get_blob(&g_kvdb, ANKI_RATING_JOURNAL_KEY,
                    fdb_blob_make(&blob, NULL, 0));
    return blob.saved.len == 0 ||
           fdb_kv_del(&g_kvdb, ANKI_RATING_JOURNAL_KEY) == FDB_NO_ERR;
}

bool anki_storage_rating_recover(void)
{
    struct fdb_blob blob;
    fdb_kv_get_blob(&g_kvdb, ANKI_RATING_JOURNAL_KEY,
                    fdb_blob_make(&blob, NULL, 0));
    if (blob.saved.len == 0) return true;
    if (blob.saved.len != sizeof(anki_rating_journal_t)) return false;
    anki_rating_journal_t journal;
    if (fdb_kv_get_blob(&g_kvdb, ANKI_RATING_JOURNAL_KEY,
                        fdb_blob_make(&blob, &journal,
                                      sizeof(journal))) != sizeof(journal) ||
        journal.magic != ANKI_RATING_JOURNAL_MAGIC ||
        journal.version != 1 || journal.had_schedule > 1 ||
        journal.previous_card.card_id == 0 ||
        journal.previous_card.card_id != journal.previous_schedule.card_id) {
        return false;
    }
    bool schedule_ok = journal.had_schedule
        ? anki_storage_save_schedule(&journal.previous_schedule)
        : anki_storage_delete_schedule(journal.previous_card.card_id);
    if (!schedule_ok || !anki_index_update_card(&journal.previous_card)) {
        return false;
    }
    ESP_LOGW(TAG, "Recovered interrupted rating for card %u",
             (unsigned)journal.previous_card.card_id);
    return anki_storage_rating_finish();
}
