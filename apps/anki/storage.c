#include "esp_log.h"
#include "storage.h"
#include "trash.h"
#include <limits.h>

static const char *TAG = "anki_storage";

#define ANKI_META_V2_MAGIC   0x414E4B32U
#define ANKI_META_V2_VERSION 2U
#define ANKI_REVIEW_LOG_MAGIC UINT32_C(0x524C4F47)
#define ANKI_REVIEW_LOG_VERSION UINT16_C(1)
#define ANKI_REVIEW_LOG_CHUNKS 32U
#define ANKI_REVIEWS_PER_CHUNK 32U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t deck_id_generator;
    uint32_t card_id_generator;
    int active_deck_idx;
    int active_card_idx;
} anki_storage_meta_v2_t;

typedef struct {
    uint32_t card_id;
    uint32_t reviewed_at;
    uint16_t elapsed_days;
    uint16_t interval_days;
    uint8_t rating;
    uint8_t state_before;
    uint8_t state_after;
    uint8_t reserved;
} anki_review_log_entry_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t chunk_index;
    uint16_t chunk_count;
    uint16_t chunks_used;
    uint32_t total_reviews;
} anki_review_log_meta_t;

_Static_assert(sizeof(anki_review_log_entry_t) == 16,
               "Review log entries must remain compact");

_Static_assert(sizeof(CardIndex) == 24,
               "CardIndex layout must remain compatible with legacy blobs");

static void schedule_key(char *key, size_t size, uint32_t card_id)
{
    snprintf(key, size, "anki_s_%u", (unsigned)card_id);
}

static bool settings_normalize(AnkiSettings *settings)
{
    bool changed = false;
    if (settings->new_cards_daily < 0 || settings->new_cards_daily > 999) {
        settings->new_cards_daily = 20;
        changed = true;
    }
    if (settings->max_cards_daily < 0 || settings->max_cards_daily > 9999) {
        settings->max_cards_daily = 200;
        changed = true;
    }
    if (settings->study_duration_mins < 0 ||
        settings->study_duration_mins > 1440) {
        settings->study_duration_mins = 25;
        changed = true;
    }
    if (settings->desired_retention_percent < 70 ||
        settings->desired_retention_percent > 97) {
        settings->desired_retention_percent = 90;
        settings->learning_step_minutes = 10;
        settings->relearning_step_minutes = 10;
        changed = true;
    }
    if (settings->learning_step_minutes < 1 ||
        settings->learning_step_minutes > 720) {
        settings->learning_step_minutes = 10;
        changed = true;
    }
    if (settings->relearning_step_minutes < 1 ||
        settings->relearning_step_minutes > 720) {
        settings->relearning_step_minutes = 10;
        changed = true;
    }
    return changed;
}

static bool scan_card_ids(AnkiModel *model, bool migrate_legacy,
                          uint32_t *max_id)
{
    *max_id = 0;
    for (int i = 0; i < model->deck_count; i++) {
        char key[32];
        snprintf(key, sizeof(key), "anki_cards_%u",
                 (unsigned)model->decks[i].deck_id);

        struct fdb_blob blob;
        fdb_kv_get_blob(&g_kvdb, key, fdb_blob_make(&blob, NULL, 0));
        size_t len = blob.saved.len;
        if (len == 0) continue;
        if (len % sizeof(CardIndex) != 0 ||
            len > sizeof(CardIndex) * ANKI_MAX_CARDS_PER_DECK) return false;

        CardIndex *cards = malloc(len);
        if (!cards) return false;
        if (fdb_kv_get_blob(&g_kvdb, key,
                            fdb_blob_make(&blob, cards, len)) != len) {
            free(cards);
            return false;
        }
        size_t count = len / sizeof(CardIndex);
        for (size_t c = 0; c < count; c++) {
            /* Legacy card_id was uint16_t followed by two padding bytes. */
            if (migrate_legacy) cards[c].card_id &= 0xFFFFU;
            uint8_t status = cards[c].status & ANKI_CARD_STATUS_MASK;
            if (cards[c].card_id == 0 ||
                cards[c].deck_id != model->decks[i].deck_id ||
                status > ANKI_CARD_STATUS_RELEARNING) {
                ESP_LOGE(TAG, "invalid card index in deck %u at item %u",
                         (unsigned)model->decks[i].deck_id, (unsigned)c);
                free(cards);
                return false;
            }
            if (cards[c].card_id > *max_id) *max_id = cards[c].card_id;
        }
        if (migrate_legacy) {
            if (fdb_kv_set_blob(&g_kvdb, key,
                                fdb_blob_make(&blob, cards, len)) != FDB_NO_ERR) {
                free(cards);
                return false;
            }
        }
        free(cards);
    }
    return true;
}

static bool delete_existing_schedule_states(void)
{
    AnkiDeck *decks = calloc(ANKI_MAX_DECK_CAPACITY, sizeof(*decks));
    if (!decks) return false;
    struct fdb_blob blob;
    size_t deck_len = fdb_kv_get_blob(&g_kvdb, "anki_decks",
        fdb_blob_make(&blob, decks,
                      sizeof(*decks) * ANKI_MAX_DECK_CAPACITY));
    if (deck_len % sizeof(*decks) != 0 ||
        deck_len > sizeof(*decks) * ANKI_MAX_DECK_CAPACITY) {
        free(decks);
        return false;
    }
    size_t deck_count = deck_len / sizeof(*decks);
    for (size_t i = 0; i < deck_count; i++) {
        char key[32];
        snprintf(key, sizeof(key), "anki_cards_%u", (unsigned)decks[i].deck_id);
        fdb_kv_get_blob(&g_kvdb, key, fdb_blob_make(&blob, NULL, 0));
        size_t card_len = blob.saved.len;
        if (card_len == 0 || card_len % sizeof(CardIndex) != 0 ||
            card_len > sizeof(CardIndex) * ANKI_MAX_CARDS_PER_DECK) continue;
        CardIndex *cards = malloc(card_len);
        if (!cards) {
            free(decks);
            return false;
        }
        if (fdb_kv_get_blob(&g_kvdb, key,
                            fdb_blob_make(&blob, cards, card_len)) != card_len) {
            free(cards);
            free(decks);
            return false;
        }
        size_t count = card_len / sizeof(*cards);
        for (size_t c = 0; c < count; c++) {
            if (!anki_storage_delete_schedule(cards[c].card_id)) {
                free(cards);
                free(decks);
                return false;
            }
            char payload_key[32];
            snprintf(payload_key, sizeof(payload_key), "anki_payload_%u",
                     (unsigned)cards[c].card_id);
            fdb_kv_del(&g_kvdb, payload_key);
        }
        free(cards);
        fdb_kv_del(&g_kvdb, key);
        snprintf(key, sizeof(key), "anki_src_%u", (unsigned)decks[i].deck_id);
        fdb_kv_del(&g_kvdb, key);
    }
    free(decks);
    fdb_kv_del(&g_kvdb, "anki_daily_v1");
    fdb_kv_del(&g_kvdb, "anki_rlog_meta");
    for (unsigned i = 0; i < ANKI_REVIEW_LOG_CHUNKS; i++) {
        char key[24];
        snprintf(key, sizeof(key), "anki_rlog_%u", i);
        fdb_kv_del(&g_kvdb, key);
    }
    return true;
}


bool anki_app_factory_reset(void) {
    struct fdb_blob blob;
    if (!delete_existing_schedule_states()) return false;
    if (!anki_trash_factory_reset()) {
        ESP_LOGE(TAG, "Failed to clear recycle-bin state");
        return false;
    }

    // 1. 初始化并写入全局配置
    AnkiSettings default_cfg = {
        .new_cards_daily = 20,
        .max_cards_daily = 200,
        .study_duration_mins = 25,
        .allow_study_ahead = false,
        .desired_retention_percent = 90,
        .learning_step_minutes = 10,
        .relearning_step_minutes = 10,
    };
    if (fdb_kv_set_blob(&g_kvdb, "anki_g_cfg",
                        fdb_blob_make(&blob, &default_cfg,
                                      sizeof(default_cfg))) != FDB_NO_ERR) {
        return false;
    }

    // 2. 同步写入初始元数据
    anki_storage_meta_v1_t initial_meta = {
        ._id_generator = 4 ,
        .active_deck_idx = -1, // 重置为未选中任何牌组
        .active_card_idx = 0
    };
    if (fdb_kv_set_blob(&g_kvdb, "anki_meta",
                        fdb_blob_make(&blob, &initial_meta,
                                      sizeof(initial_meta))) != FDB_NO_ERR) {
        return false;
    }

    anki_storage_meta_v2_t initial_meta_v2 = {
        .magic = ANKI_META_V2_MAGIC,
        .version = ANKI_META_V2_VERSION,
        .deck_id_generator = 4,
        .card_id_generator = 4,
        .active_deck_idx = -1,
        .active_card_idx = 0,
    };
    if (fdb_kv_set_blob(&g_kvdb, "anki_meta_v2",
                        fdb_blob_make(&blob, &initial_meta_v2,
                                      sizeof(initial_meta_v2))) != FDB_NO_ERR) {
        return false;
    }

    // 3. 构建教程牌组 (Decks)
    AnkiDeck tutorial_decks[3];
    memset(tutorial_decks, 0, sizeof(tutorial_decks));

    // Root Deck: Tutorial
    tutorial_decks[0].deck_id = 1;
    tutorial_decks[0].parent_id = -1;
    strncpy(tutorial_decks[0].name, "Tutorial", sizeof(tutorial_decks[0].name) - 1);
    tutorial_decks[0].total_cards_count = 3;
    
    // Sub Deck 01: Basic Operations (挂载了 3 张教程卡片)
    tutorial_decks[1].deck_id = 2;
    tutorial_decks[1].parent_id = 1;
    strncpy(tutorial_decks[1].name, "Basic Operations", sizeof(tutorial_decks[1].name) - 1);
    tutorial_decks[1].total_cards_count = 3;
    tutorial_decks[1].new_cards_count = 3;
    tutorial_decks[1].due_cards_count = 0;

    // Sub Deck 02: Advanced Usage
    tutorial_decks[2].deck_id = 3;
    tutorial_decks[2].parent_id = 1;
    strncpy(tutorial_decks[2].name, "Advanced Usage", sizeof(tutorial_decks[2].name) - 1);
    tutorial_decks[2].total_cards_count = 0;
    tutorial_decks[2].new_cards_count = 0;
    tutorial_decks[2].due_cards_count = 0;

    if (fdb_kv_set_blob(&g_kvdb, "anki_decks",
                        fdb_blob_make(&blob, tutorial_decks,
                                      sizeof(tutorial_decks))) != FDB_NO_ERR) {
        return false;
    }

    // 3. 构建教程卡片 (Cards) 并挂载到 subdeck01 (deck_id = 2) 之下
    CardIndex tutorial_indices[3];
    memset(tutorial_indices, 0, sizeof(tutorial_indices));

    tutorial_indices[0].card_id = 1;
    tutorial_indices[0].deck_id = 2;
    tutorial_indices[0].status = 0; // 新卡片

    tutorial_indices[1].card_id = 2;
    tutorial_indices[1].deck_id = 2;
    tutorial_indices[1].status = 0;

    tutorial_indices[2].card_id = 3;
    tutorial_indices[2].deck_id = 2;
    tutorial_indices[2].status = 0;

    // 存储 Index 数组到 deck_id = 2 之下
    char key_buf[32];
    snprintf(key_buf, sizeof(key_buf), "anki_cards_%d", 2);
    if (fdb_kv_set_blob(&g_kvdb, key_buf,
                        fdb_blob_make(&blob, tutorial_indices,
                                      sizeof(tutorial_indices))) != FDB_NO_ERR) {
        return false;
    }

    // 5. 循环构建教程卡片的文本负载 (CardPayload)
    CardPayload payload;

    // Card 1 Payload
    memset(&payload, 0, sizeof(CardPayload));
    strncpy(payload.front, "How to start studying a deck?", sizeof(payload.front) - 1);
    strncpy(payload.back, "Swipe right on the deck.", sizeof(payload.back) - 1);
    snprintf(key_buf, sizeof(key_buf), "anki_payload_%d", 1); // 以 card_id 为 key
    if (fdb_kv_set_blob(&g_kvdb, key_buf,
                        fdb_blob_make(&blob, &payload,
                                      sizeof(payload))) != FDB_NO_ERR) {
        return false;
    }

    // Card 2 Payload
    memset(&payload, 0, sizeof(CardPayload));
    strncpy(payload.front, "How to add cards to this deck?", sizeof(payload.front) - 1);
    strncpy(payload.back, "Tap the menu icon on the right, then select Import.", sizeof(payload.back) - 1);
    snprintf(key_buf, sizeof(key_buf), "anki_payload_%d", 2);
    if (fdb_kv_set_blob(&g_kvdb, key_buf,
                        fdb_blob_make(&blob, &payload,
                                      sizeof(payload))) != FDB_NO_ERR) {
        return false;
    }

    // Card 3 Payload
    memset(&payload, 0, sizeof(CardPayload));
    strncpy(payload.front, "How to create cards for a deck?", sizeof(payload.front) - 1);
    strncpy(payload.back,
            "Put .txt files in /sdcard/anki. Use one card per line, with front and back separated by '|'.",
            sizeof(payload.back) - 1);
    snprintf(key_buf, sizeof(key_buf), "anki_payload_%d", 3);
    if (fdb_kv_set_blob(&g_kvdb, key_buf,
                        fdb_blob_make(&blob, &payload,
                                      sizeof(payload))) != FDB_NO_ERR) {
        return false;
    }

    ESP_LOGI(TAG, "Anki factory reset done.");
    return true;
}

void anki_storage_load_all(AnkiModel *model) {
    if (!model) return;

    struct fdb_blob blob;
    size_t read_len;

    // 1. 加载全局设置
    read_len = fdb_kv_get_blob(&g_kvdb, "anki_g_cfg", fdb_blob_make(&blob, &model->global_settings, sizeof(AnkiSettings)));
    if (read_len != sizeof(AnkiSettings)) {
        ESP_LOGI(TAG, "No global settings found, using defaults");
        // 初始化默认全局配置
        model->global_settings.new_cards_daily = 20;   // 默认20张新卡
        model->global_settings.max_cards_daily = 200;  // 默认最大复习200张
        model->global_settings.study_duration_mins = 25;
        model->global_settings.allow_study_ahead = false;
        model->global_settings.desired_retention_percent = 90;
        model->global_settings.learning_step_minutes = 10;
        model->global_settings.relearning_step_minutes = 10;
    }
    bool settings_changed = settings_normalize(&model->global_settings);

    // 2. Load metadata. Keep the v1 key readable by older firmware.
    bool legacy_meta = false;
    anki_storage_meta_v2_t meta_v2;
    read_len = fdb_kv_get_blob(&g_kvdb, "anki_meta_v2",
                               fdb_blob_make(&blob, &meta_v2, sizeof(meta_v2)));
    if (read_len == sizeof(meta_v2) && meta_v2.magic == ANKI_META_V2_MAGIC &&
        meta_v2.version == ANKI_META_V2_VERSION) {
        model->_id_generator = meta_v2.deck_id_generator;
        model->_cards_id_generator = meta_v2.card_id_generator;
        model->active_deck_idx = meta_v2.active_deck_idx;
        model->active_card_idx = meta_v2.active_card_idx;
    } else {
        legacy_meta = true;
        anki_storage_meta_v1_t meta_v1;
        read_len = fdb_kv_get_blob(&g_kvdb, "anki_meta",
                                   fdb_blob_make(&blob, &meta_v1, sizeof(meta_v1)));
        if (read_len == sizeof(meta_v1)) {
            model->_id_generator = meta_v1._id_generator;
            model->active_deck_idx = meta_v1.active_deck_idx;
            model->active_card_idx = meta_v1.active_card_idx;
        } else {
            model->_id_generator = 1;
            model->active_deck_idx = -1;
            model->active_card_idx = 0;
        }
        model->_cards_id_generator = 1;
    }

    // 3. 加载牌组列表 (Decks 结构体里已经包含了 custom_settings)
    read_len = fdb_kv_get_blob(&g_kvdb, "anki_decks", fdb_blob_make(&blob, model->decks, sizeof(AnkiDeck) * ANKI_MAX_DECK_CAPACITY));
    if (read_len > 0 && read_len <= sizeof(AnkiDeck) * ANKI_MAX_DECK_CAPACITY &&
        read_len % sizeof(AnkiDeck) == 0) {
        model->deck_count = read_len / sizeof(AnkiDeck);
        bool valid_catalog = true;
        for (int i = 0; i < model->deck_count; i++) {
            AnkiDeck *deck = &model->decks[i];
            deck->name[sizeof(deck->name) - 1] = '\0';
            deck->sd_file_path[sizeof(deck->sd_file_path) - 1] = '\0';
            if (deck->deck_id == 0 || deck->deck_id > INT32_MAX) {
                valid_catalog = false;
            }
            for (int j = 0; j < i; j++) {
                if (model->decks[j].deck_id == deck->deck_id) {
                    valid_catalog = false;
                }
            }
        }
        if (!valid_catalog) {
            ESP_LOGE(TAG, "Invalid deck catalog; refusing to load it");
            model->deck_count = 0;
            model->storage_fault = true;
        }
        ESP_LOGI(TAG, "Loaded %d decks from FlashDB", model->deck_count);
    } else {
        if (read_len > 0) ESP_LOGW(TAG, "Invalid deck blob length: %u", (unsigned)read_len);
        if (read_len > 0) model->storage_fault = true;
        model->deck_count = 0;
    }

    if (model->active_deck_idx < -1 ||
        model->active_deck_idx >= model->deck_count) {
        model->active_deck_idx = -1;
    }
    if (model->active_card_idx < 0 ||
        model->active_card_idx > ANKI_INITIAL_QUEUE_CAPACITY) {
        model->active_card_idx = 0;
    }

    for (int i = 0; i < model->deck_count; i++) {
        if (model->decks[i].has_custom_settings &&
            settings_normalize(&model->decks[i].custom_settings)) {
            settings_changed = true;
        }
    }
    if (settings_changed) {
        anki_storage_sync_global_settings(model);
        anki_storage_sync_decks(model);
    }

    uint32_t max_deck_id = 0;
    for (int i = 0; i < model->deck_count; i++) {
        if (model->decks[i].deck_id > max_deck_id) {
            max_deck_id = model->decks[i].deck_id;
        }
    }
    if (max_deck_id >= INT32_MAX) model->_id_generator = 0;
    else if (model->_id_generator == 0 ||
             model->_id_generator <= max_deck_id ||
             model->_id_generator > INT32_MAX) {
        model->_id_generator = max_deck_id + 1;
    }

    uint32_t max_card_id = 0;
    bool card_ids_valid = scan_card_ids(model, legacy_meta, &max_card_id);
    if (!card_ids_valid || max_card_id == UINT32_MAX) {
        ESP_LOGE(TAG, "Card indexes are unavailable; disabling card creation");
        model->_cards_id_generator = 0;
        model->storage_fault = true;
    } else if (model->_cards_id_generator == 0 ||
               model->_cards_id_generator <= max_card_id) {
        model->_cards_id_generator = max_card_id + 1;
    }
    if (legacy_meta) anki_storage_sync_meta(model);

    read_len = fdb_kv_get_blob(&g_kvdb, "anki_daily_v1",
        fdb_blob_make(&blob, model->daily_progress,
                      sizeof(model->daily_progress)));
    if (read_len <= sizeof(model->daily_progress) &&
        read_len % sizeof(AnkiDailyProgress) == 0) {
        model->daily_progress_count =
            (uint8_t)(read_len / sizeof(AnkiDailyProgress));
    } else {
        model->daily_progress_count = 0;
    }
}

bool anki_storage_sync_global_settings(AnkiModel *model) {
    if (!model) return false;
    struct fdb_blob blob;
    fdb_err_t err = fdb_kv_set_blob(&g_kvdb, "anki_g_cfg", fdb_blob_make(&blob, &model->global_settings, sizeof(AnkiSettings)));
    if (err != FDB_NO_ERR) ESP_LOGE(TAG, "save global settings failed: %d", err);
    return err == FDB_NO_ERR;
}

bool anki_storage_sync_meta(AnkiModel *model) {
    if (!model) return false;
    struct fdb_blob blob;
    
    // 将分散在 model 中的状态打包写入
    anki_storage_meta_v1_t meta_v1 = {
        ._id_generator = model->_id_generator,
        .active_deck_idx = model->active_deck_idx,
        .active_card_idx = model->active_card_idx
    };
    fdb_err_t err_v1 = fdb_kv_set_blob(&g_kvdb, "anki_meta",
                                    fdb_blob_make(&blob, &meta_v1, sizeof(meta_v1)));
    if (err_v1 != FDB_NO_ERR) ESP_LOGE(TAG, "save legacy metadata failed: %d", err_v1);

    anki_storage_meta_v2_t meta_v2 = {
        .magic = ANKI_META_V2_MAGIC,
        .version = ANKI_META_V2_VERSION,
        .deck_id_generator = model->_id_generator,
        .card_id_generator = model->_cards_id_generator,
        .active_deck_idx = model->active_deck_idx,
        .active_card_idx = model->active_card_idx,
    };
    fdb_err_t err_v2 = fdb_kv_set_blob(&g_kvdb, "anki_meta_v2",
                                       fdb_blob_make(&blob, &meta_v2,
                                                     sizeof(meta_v2)));
    if (err_v2 != FDB_NO_ERR) ESP_LOGE(TAG, "save metadata v2 failed: %d", err_v2);
    return err_v1 == FDB_NO_ERR && err_v2 == FDB_NO_ERR;
}

bool anki_storage_sync_decks(AnkiModel *model) {
    if (!model) return false;
    size_t len = model->deck_count * sizeof(AnkiDeck);
    if (len > 0) {
        struct fdb_blob blob;
        fdb_err_t err = fdb_kv_set_blob(&g_kvdb, "anki_decks", fdb_blob_make(&blob, model->decks, len));
        if (err != FDB_NO_ERR) ESP_LOGE(TAG, "save decks failed: %d", err);
        return err == FDB_NO_ERR;
    } else {
        struct fdb_blob blob;
        fdb_kv_get_blob(&g_kvdb, "anki_decks", fdb_blob_make(&blob, NULL, 0));
        return blob.saved.len == 0 ||
               fdb_kv_del(&g_kvdb, "anki_decks") == FDB_NO_ERR;
    }
}

bool anki_storage_sync_daily_progress(AnkiModel *model)
{
    if (!model) return false;
    if (model->daily_progress_count == 0) {
        struct fdb_blob blob;
        fdb_kv_get_blob(&g_kvdb, "anki_daily_v1",
                        fdb_blob_make(&blob, NULL, 0));
        return blob.saved.len == 0 ||
               fdb_kv_del(&g_kvdb, "anki_daily_v1") == FDB_NO_ERR;
    }
    struct fdb_blob blob;
    size_t len = model->daily_progress_count * sizeof(AnkiDailyProgress);
    fdb_err_t err = fdb_kv_set_blob(&g_kvdb, "anki_daily_v1",
        fdb_blob_make(&blob, model->daily_progress, len));
    if (err != FDB_NO_ERR) ESP_LOGE(TAG, "save daily progress failed: %d", err);
    return err == FDB_NO_ERR;
}

bool anki_storage_load_schedule(const CardIndex *card,
                                AnkiScheduleState *schedule)
{
    if (!card || !schedule || card->card_id == 0) return false;
    memset(schedule, 0, sizeof(*schedule));

    char key[32];
    schedule_key(key, sizeof(key), card->card_id);
    struct fdb_blob blob;
    size_t len = fdb_kv_get_blob(&g_kvdb, key,
        fdb_blob_make(&blob, schedule, sizeof(*schedule)));
    if (len == sizeof(*schedule) &&
        schedule->magic == ANKI_SCHEDULE_MAGIC &&
        schedule->version == ANKI_SCHEDULE_VERSION &&
        schedule->size == sizeof(*schedule) &&
        schedule->card_id == card->card_id) {
        return true;
    }

    memset(schedule, 0, sizeof(*schedule));
    schedule->magic = ANKI_SCHEDULE_MAGIC;
    schedule->version = ANKI_SCHEDULE_VERSION;
    schedule->size = sizeof(*schedule);
    schedule->card_id = card->card_id;
    uint8_t status = card->status & ANKI_CARD_STATUS_MASK;
    if (status != ANKI_CARD_STATUS_NEW) {
        schedule->interval_days = card->interval ? card->interval : 1;
        schedule->stability = (float)schedule->interval_days;
        schedule->difficulty = 5.0f;
        schedule->reps = 1;
        uint64_t interval_seconds = (uint64_t)schedule->interval_days * 86400U;
        if (card->due_timestamp > interval_seconds) {
            schedule->last_review_at =
                card->due_timestamp - (uint32_t)interval_seconds;
        }
    }
    return false;
}

bool anki_storage_save_schedule(const AnkiScheduleState *schedule)
{
    if (!schedule || schedule->card_id == 0 ||
        schedule->magic != ANKI_SCHEDULE_MAGIC ||
        schedule->version != ANKI_SCHEDULE_VERSION ||
        schedule->size != sizeof(*schedule)) return false;
    char key[32];
    schedule_key(key, sizeof(key), schedule->card_id);
    struct fdb_blob blob;
    fdb_err_t err = fdb_kv_set_blob(&g_kvdb, key,
        fdb_blob_make(&blob, schedule, sizeof(*schedule)));
    if (err != FDB_NO_ERR) {
        ESP_LOGE(TAG, "save schedule %u failed: %d",
                 (unsigned)schedule->card_id, err);
        return false;
    }
    return true;
}

bool anki_storage_delete_schedule(uint32_t card_id)
{
    if (card_id == 0) return false;
    char key[32];
    schedule_key(key, sizeof(key), card_id);
    struct fdb_blob blob;
    fdb_kv_get_blob(&g_kvdb, key, fdb_blob_make(&blob, NULL, 0));
    return blob.saved.len == 0 || fdb_kv_del(&g_kvdb, key) == FDB_NO_ERR;
}

bool anki_storage_append_review(uint32_t card_id, uint32_t reviewed_at,
                                uint16_t elapsed_days,
                                uint16_t interval_days, uint8_t rating,
                                uint8_t state_before, uint8_t state_after)
{
    if (card_id == 0 || reviewed_at == 0 || rating < 1 || rating > 4) {
        return false;
    }

    struct fdb_blob blob;
    anki_review_log_meta_t meta = {0};
    size_t meta_len = fdb_kv_get_blob(&g_kvdb, "anki_rlog_meta",
        fdb_blob_make(&blob, &meta, sizeof(meta)));
    if (meta_len != sizeof(meta) || meta.magic != ANKI_REVIEW_LOG_MAGIC ||
        meta.version != ANKI_REVIEW_LOG_VERSION ||
        meta.chunk_index >= ANKI_REVIEW_LOG_CHUNKS ||
        meta.chunk_count > ANKI_REVIEWS_PER_CHUNK) {
        memset(&meta, 0, sizeof(meta));
        meta.magic = ANKI_REVIEW_LOG_MAGIC;
        meta.version = ANKI_REVIEW_LOG_VERSION;
    }

    if (meta.chunk_count == ANKI_REVIEWS_PER_CHUNK) {
        meta.chunk_index = (meta.chunk_index + 1U) % ANKI_REVIEW_LOG_CHUNKS;
        meta.chunk_count = 0;
        if (meta.chunks_used < ANKI_REVIEW_LOG_CHUNKS) meta.chunks_used++;
    }
    if (meta.chunks_used == 0) meta.chunks_used = 1;

    anki_review_log_entry_t entries[ANKI_REVIEWS_PER_CHUNK] = {0};
    char key[24];
    snprintf(key, sizeof(key), "anki_rlog_%u", (unsigned)meta.chunk_index);
    if (meta.chunk_count > 0) {
        size_t expected = meta.chunk_count * sizeof(entries[0]);
        size_t len = fdb_kv_get_blob(&g_kvdb, key,
            fdb_blob_make(&blob, entries, expected));
        if (len != expected) {
            ESP_LOGW(TAG, "reset corrupt review-log chunk %u",
                     (unsigned)meta.chunk_index);
            memset(entries, 0, sizeof(entries));
            meta.chunk_count = 0;
        }
    }

    entries[meta.chunk_count++] = (anki_review_log_entry_t) {
        .card_id = card_id,
        .reviewed_at = reviewed_at,
        .elapsed_days = elapsed_days,
        .interval_days = interval_days,
        .rating = rating,
        .state_before = state_before,
        .state_after = state_after,
    };
    size_t chunk_len = meta.chunk_count * sizeof(entries[0]);
    if (fdb_kv_set_blob(&g_kvdb, key,
        fdb_blob_make(&blob, entries, chunk_len)) != FDB_NO_ERR) return false;
    if (meta.total_reviews < UINT32_MAX) meta.total_reviews++;
    return fdb_kv_set_blob(&g_kvdb, "anki_rlog_meta",
        fdb_blob_make(&blob, &meta, sizeof(meta))) == FDB_NO_ERR;
}
