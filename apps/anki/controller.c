#include <time.h>
#include "esp_system.h"
#include "controller.h"
#include "model.h"
#include "view.h"
#include "app.h"
#include "storage.h"
#include "time_service.h"

#include "esp_log.h"

static const char *TAG = "anki_controller";

static uint16_t saturate_u16(uint32_t value)
{
    return value > UINT16_MAX ? UINT16_MAX : (uint16_t)value;
}

bool anki_controller_init(AnkiApp *app) {
    if (!app || !app->model || !app->view) return false;
    app->controller = malloc(sizeof(AnkiController));
    if (!app->controller) return false;
    app->controller->model = app->model;
    app->controller->view = app->view;

    anki_controller_sync(app);
    return true;
}

void anki_controller_deinit(AnkiApp *app) {
    if (app->controller) {
        free(app->controller);
        app->controller = NULL;
    }
}

void anki_controller_sync(AnkiApp *app) {
    if (!app || !app->model) return;
    AnkiModel *model = app->model;
    uint32_t new_counts[ANKI_MAX_DECK_CAPACITY] = {0};
    uint32_t due_counts[ANKI_MAX_DECK_CAPACITY] = {0};
    uint32_t total_counts[ANKI_MAX_DECK_CAPACITY] = {0};

    uint32_t now = 0;
    bool has_time = time_service_now(&now);
    for (int i = 0; i < model->deck_count; i++) {
        uint32_t deck_id = model->decks[i].deck_id;
        char key_buf[32];
        snprintf(key_buf, sizeof(key_buf), "anki_cards_%u", (unsigned int)deck_id);
        
        struct fdb_blob blob;
        fdb_kv_get_blob(&g_kvdb, key_buf, fdb_blob_make(&blob, NULL, 0));
        size_t blob_len = blob.saved.len; // 正确获取 KV 数据真实大小的方法
        if (blob_len == 0) continue;
        if (blob_len % sizeof(CardIndex) != 0 ||
            blob_len > sizeof(CardIndex) * ANKI_MAX_CARDS_PER_DECK) {
            ESP_LOGE(TAG, "Invalid card index length for deck %u: %u",
                     (unsigned)deck_id, (unsigned)blob_len);
            return;
        }
            int own_total = blob_len / sizeof(CardIndex);
            uint32_t own_active = 0;
            uint32_t own_new = 0;
            uint32_t own_due = 0;
            
            CardIndex *cards = malloc(blob_len);
            if (!cards) return;
            if (fdb_kv_get_blob(&g_kvdb, key_buf,
                                fdb_blob_make(&blob, cards, blob_len)) != blob_len) {
                free(cards);
                ESP_LOGE(TAG, "Failed to read card index for deck %u",
                         (unsigned)deck_id);
                return;
            }
                for (int c = 0; c < own_total; c++) {
                    if (cards[c].status & ANKI_CARD_FLAG_MISSING) continue;
                    own_active++;
                    uint8_t status = cards[c].status & ANKI_CARD_STATUS_MASK;
                    if (status == ANKI_CARD_STATUS_NEW) own_new++;
                    else if ((status == ANKI_CARD_STATUS_REVIEW ||
                              status == ANKI_CARD_STATUS_LEARNING ||
                              status == ANKI_CARD_STATUS_RELEARNING) &&
                             has_time && cards[c].due_timestamp <= now) own_due++;
                }
                free(cards);

            uint32_t current_id = deck_id;
            for (int depth = 0;
                 depth < ANKI_MAX_DECK_CAPACITY && current_id != UINT32_MAX;
                 depth++) {
                bool found = false;
                for (int j = 0; j < model->deck_count; j++) {
                    if (model->decks[j].deck_id == current_id) {
                        total_counts[j] += own_active;
                        new_counts[j] += own_new;
                        due_counts[j] += own_due;
                        current_id = model->decks[j].parent_id;
                        found = true;
                        break;
                    }
                }
                if (!found) break;
            }
    }

    bool changed = false;
    for (int i = 0; i < model->deck_count; i++) {
        uint16_t next_new = saturate_u16(new_counts[i]);
        uint16_t next_due = saturate_u16(due_counts[i]);
        uint16_t next_total = saturate_u16(total_counts[i]);
        changed |= model->decks[i].new_cards_count != next_new ||
                   model->decks[i].due_cards_count != next_due ||
                   model->decks[i].total_cards_count != next_total;
        model->decks[i].new_cards_count = next_new;
        model->decks[i].due_cards_count = next_due;
        model->decks[i].total_cards_count = next_total;
    }

    if (changed) {
        anki_storage_sync_decks(model);
    }
}
