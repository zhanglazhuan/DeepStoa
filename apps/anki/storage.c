#include "esp_log.h"
#include "storage.h"

static const char *TAG = "anki_storage";


bool anki_app_factory_reset(void) {
    struct fdb_blob blob;

    // 1. 初始化并写入全局配置
    AnkiSettings default_cfg = {
        .new_cards_daily = 25,
        .max_cards_daily = 100,
        .study_duration_mins = 25,
        .allow_study_ahead = false,
        .hard_interval = 1,
        .good_interval = 7,
        .easy_interval = 21,
    };
    fdb_kv_set_blob(&g_kvdb, "anki_g_cfg", fdb_blob_make(&blob, &default_cfg, sizeof(AnkiSettings)));

    // 2. 同步写入初始元数据
    anki_storage_meta_t initial_meta = {
        ._id_generator = 4 ,
        .active_deck_idx = -1, // 重置为未选中任何牌组
        .active_card_idx = 0
    };
    fdb_kv_set_blob(&g_kvdb, "anki_meta", fdb_blob_make(&blob, &initial_meta, sizeof(initial_meta)));

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

    fdb_kv_set_blob(&g_kvdb, "anki_decks", fdb_blob_make(&blob, tutorial_decks, sizeof(tutorial_decks)));

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
    fdb_kv_set_blob(&g_kvdb, key_buf, fdb_blob_make(&blob, tutorial_indices, sizeof(tutorial_indices)));

    // 5. 循环构建教程卡片的文本负载 (CardPayload)
    CardPayload payload;

    // Card 1 Payload
    memset(&payload, 0, sizeof(CardPayload));
    strncpy(payload.front, "How to start studying a deck?", sizeof(payload.front) - 1);
    strncpy(payload.back, "Swipe right on the deck.", sizeof(payload.back) - 1);
    snprintf(key_buf, sizeof(key_buf), "anki_payload_%d", 1); // 以 card_id 为 key
    fdb_kv_set_blob(&g_kvdb, key_buf, fdb_blob_make(&blob, &payload, sizeof(CardPayload)));

    // Card 2 Payload
    memset(&payload, 0, sizeof(CardPayload));
    strncpy(payload.front, "How to add cards to this deck?", sizeof(payload.front) - 1);
    strncpy(payload.back, "Tap the menu icon on the right, then select Import.", sizeof(payload.back) - 1);
    snprintf(key_buf, sizeof(key_buf), "anki_payload_%d", 2);
    fdb_kv_set_blob(&g_kvdb, key_buf, fdb_blob_make(&blob, &payload, sizeof(CardPayload)));

    // Card 3 Payload
    memset(&payload, 0, sizeof(CardPayload));
    strncpy(payload.front, "How to create cards for a deck?", sizeof(payload.front) - 1);
    strncpy(payload.back, "TXT mode: one card per line, front and back separated by '|'.\nCSV mode: 1st column is front, 2nd is back.", sizeof(payload.back) - 1);
    snprintf(key_buf, sizeof(key_buf), "anki_payload_%d", 3);
    fdb_kv_set_blob(&g_kvdb, key_buf, fdb_blob_make(&blob, &payload, sizeof(CardPayload)));

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
        model->global_settings.hard_interval = 1;
        model->global_settings.good_interval = 5;
        model->global_settings.easy_interval = 21;
    }

    // 2. 加载元数据 (Meta)
    anki_storage_meta_t meta_data;
    read_len = fdb_kv_get_blob(&g_kvdb, "anki_meta", fdb_blob_make(&blob, &meta_data, sizeof(meta_data)));
    if (read_len == sizeof(meta_data)) {
        model->_id_generator = meta_data._id_generator;
        model->active_deck_idx = meta_data.active_deck_idx;
        model->active_card_idx = meta_data.active_card_idx;
    } else {
        model->_id_generator = 1;
        model->active_deck_idx = -1;
        model->active_card_idx = 0;
    }

    // 3. 加载牌组列表 (Decks 结构体里已经包含了 custom_settings)
    read_len = fdb_kv_get_blob(&g_kvdb, "anki_decks", fdb_blob_make(&blob, model->decks, sizeof(AnkiDeck) * ANKI_MAX_DECK_CAPACITY));
    if (read_len > 0 && read_len <= sizeof(AnkiDeck) * ANKI_MAX_DECK_CAPACITY &&
        read_len % sizeof(AnkiDeck) == 0) {
        model->deck_count = read_len / sizeof(AnkiDeck);
        ESP_LOGI(TAG, "Loaded %d decks from FlashDB", model->deck_count);
    } else {
        if (read_len > 0) ESP_LOGW(TAG, "Invalid deck blob length: %u", (unsigned)read_len);
        model->deck_count = 0;
    }
}

void anki_storage_sync_global_settings(AnkiModel *model) {
    if (!model) return;
    struct fdb_blob blob;
    fdb_err_t err = fdb_kv_set_blob(&g_kvdb, "anki_g_cfg", fdb_blob_make(&blob, &model->global_settings, sizeof(AnkiSettings)));
    if (err != FDB_NO_ERR) ESP_LOGE(TAG, "save global settings failed: %d", err);
}

void anki_storage_sync_meta(AnkiModel *model) {
    if (!model) return;
    struct fdb_blob blob;
    
    // 将分散在 model 中的状态打包写入
    anki_storage_meta_t meta_data = {
        ._id_generator = model->_id_generator,
        .active_deck_idx = model->active_deck_idx,
        .active_card_idx = model->active_card_idx
    };
    
    fdb_err_t err = fdb_kv_set_blob(&g_kvdb, "anki_meta", fdb_blob_make(&blob, &meta_data, sizeof(meta_data)));
    if (err != FDB_NO_ERR) ESP_LOGE(TAG, "save metadata failed: %d", err);
}

void anki_storage_sync_decks(AnkiModel *model) {
    if (!model) return;
    size_t len = model->deck_count * sizeof(AnkiDeck);
    if (len > 0) {
        struct fdb_blob blob;
        fdb_err_t err = fdb_kv_set_blob(&g_kvdb, "anki_decks", fdb_blob_make(&blob, model->decks, len));
        if (err != FDB_NO_ERR) ESP_LOGE(TAG, "save decks failed: %d", err);
    } else {
        fdb_kv_del(&g_kvdb, "anki_decks"); 
    }
}
