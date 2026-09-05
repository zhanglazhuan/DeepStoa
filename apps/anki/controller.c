#include <time.h>
#include "esp_system.h"
#include "controller.h"
#include "model.h"
#include "view.h"
#include "app.h"
#include "storage.h"

void anki_controller_init(AnkiApp *app) {
    app->controller = malloc(sizeof(AnkiController));
    app->controller->model = app->model;
    app->controller->view = app->view;

    anki_controller_sync(app);
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
    bool changed = false;

    uint16_t old_new[ANKI_MAX_DECK_CAPACITY];
    uint16_t old_due[ANKI_MAX_DECK_CAPACITY];
    uint16_t old_total[ANKI_MAX_DECK_CAPACITY];

    // 1. 备份原值并清空，准备从底层数据重建聚合状态 (防止自己加自己)
    for (int i = 0; i < model->deck_count; i++) {
        old_new[i] = model->decks[i].new_cards_count;
        old_due[i] = model->decks[i].due_cards_count;
        old_total[i] = model->decks[i].total_cards_count;
        
        model->decks[i].new_cards_count = 0;
        model->decks[i].due_cards_count = 0;
        model->decks[i].total_cards_count = 0;
    }

    // 2. 遍历 FlashDB 中的真实卡片记录，向上级联汇总
    time_t now = time(NULL);
    for (int i = 0; i < model->deck_count; i++) {
        uint32_t deck_id = model->decks[i].deck_id;
        char key_buf[32];
        snprintf(key_buf, sizeof(key_buf), "anki_cards_%u", (unsigned int)deck_id);
        
        struct fdb_blob blob;
        fdb_kv_get_blob(&g_kvdb, key_buf, fdb_blob_make(&blob, NULL, 0));
        size_t blob_len = blob.saved.len; // 正确获取 KV 数据真实大小的方法
        if (blob_len > 0) {
            int own_total = blob_len / sizeof(CardIndex);
            int own_new = 0;
            int own_due = 0;
            
            CardIndex *cards = malloc(blob_len);
            if (cards) {
                fdb_kv_get_blob(&g_kvdb, key_buf, fdb_blob_make(&blob, cards, blob_len));
                for (int c = 0; c < own_total; c++) {
                    if (cards[c].status == 0) own_new++;
                    else if (cards[c].status == 1 && cards[c].due_timestamp <= now) own_due++;
                }
                free(cards);
            }
            
            // 向上累加到自己及所有祖先节点
            uint32_t current_id = (uint32_t)deck_id;
            for (int depth = 0; depth < 10 && current_id != -1; depth++) {
                for (int j = 0; j < model->deck_count; j++) {
                    if (model->decks[j].deck_id == (uint32_t)current_id) {
                        model->decks[j].total_cards_count += own_total;
                        model->decks[j].new_cards_count += own_new;
                        model->decks[j].due_cards_count += own_due;
                        current_id = (uint32_t)model->decks[j].parent_id; // 找父节点继续
                        break;
                    }
                }
            }
        }
    }

    // 3. 对比变更并持久化
    for (int i = 0; i < model->deck_count; i++) {
        if (model->decks[i].new_cards_count != old_new[i] || 
            model->decks[i].due_cards_count != old_due[i] ||
            model->decks[i].total_cards_count != old_total[i]) {
            changed = true;
            break;
        }
    }

    if (changed) {
        anki_storage_sync_decks(model);
    }
}