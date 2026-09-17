#include <string.h>
#include <stdio.h>
#include <time.h>
#include "esp_system.h"
#include "esp_log.h"

#include "model.h"
#include "app.h"
#include "storage.h"

static const char *TAG = "anki_model";


void anki_model_init(AnkiApp *app) {
    if (!app) return;
    app->model = malloc(sizeof(AnkiModel));
    if (!app->model) return;
    memset(app->model, 0, sizeof(AnkiModel));

    // 一次性全部分配 99 个牌组的内存，约 14KB
    app->model->decks = malloc(sizeof(AnkiDeck) * ANKI_MAX_DECK_CAPACITY);
    if (!app->model->decks) { free(app->model); app->model = NULL; return; }
    memset(app->model->decks, 0, sizeof(AnkiDeck) * ANKI_MAX_DECK_CAPACITY);
    
    // 复习队列也直接分配最大初始容量
    app->model->today_queue = malloc(sizeof(CardIndex) * ANKI_INITIAL_QUEUE_CAPACITY);
    if (!app->model->today_queue) { free(app->model->decks); free(app->model); app->model = NULL; return; }
    memset(app->model->today_queue, 0, sizeof(CardIndex) * ANKI_INITIAL_QUEUE_CAPACITY);

    anki_storage_load_all(app->model);
}

void anki_model_deinit(AnkiApp *app) {
    if (app->model) {
        if (app->model->decks) free(app->model->decks);
        if (app->model->today_queue) free(app->model->today_queue);
        free(app->model);
        app->model = NULL;
    }
}

// 读取 SD 卡加载 deck list
void anki_model_load_decks_catalog(AnkiModel *model) {

}

// 读取 SD 卡加载指定 deck 里的 card list
void anki_model_load_cards(AnkiModel *model, uint8_t deck_id) {

}

// 判断 child_id 是否是 root_id 本身，或者是其任意层级的子牌组
static bool is_deck_in_tree(AnkiModel *model, uint32_t child_id, uint32_t root_id) {
    if (child_id == root_id) return true; // 是当前牌组本身
    
    int current_id = (int)child_id;
    // 向上追溯，最多追溯 10 层防止死循环
    for (int depth = 0; depth < 10 && current_id != -1; depth++) {
        bool found_parent = false;
        for (int i = 0; i < model->deck_count; i++) {
            if (model->decks[i].deck_id == (uint32_t)current_id) {
                current_id = (int)model->decks[i].parent_id; // 向上找爸爸
                found_parent = true;
                break;
            }
        }
        
        if ((uint32_t)current_id == root_id) return true; // 祖先是目标根节点
        if (!found_parent) break; // 找不到父节点（孤儿数据），退出
    }
    return false;
}

// 在 model.c 中添加补水函数
static void anki_model_rebuild_today_queue(AnkiModel *model) {
    model->queue_count = 0;
    model->active_card_idx = 0; // 游标归零
    
    if (model->active_deck_idx < 0 || model->active_deck_idx >= model->deck_count) return;
    
    uint32_t target_root_id = model->decks[model->active_deck_idx].deck_id;
    time_t now = time(NULL);
    
    // 遍历所有牌组，如果它是目标牌组或其子树，则去 FlashDB 读取它的卡片列表
    for (int i = 0; i < model->deck_count; i++) {
        if (is_deck_in_tree(model, model->decks[i].deck_id, target_root_id)) {
            char key_buf[32];
            snprintf(key_buf, sizeof(key_buf), "anki_cards_%u", (unsigned int)model->decks[i].deck_id);
            
            struct fdb_blob blob;
            fdb_kv_get_blob(&g_kvdb, key_buf, fdb_blob_make(&blob, NULL, 0));
            size_t blob_len = blob.saved.len;
            if (blob_len > 0 && blob_len % sizeof(CardIndex) == 0 && blob_len <= sizeof(CardIndex) * ANKI_MAX_CARDS_PER_DECK) {
                int own_total = blob_len / sizeof(CardIndex);
                CardIndex *cards = malloc(blob_len);
                if (cards) {
                    fdb_kv_get_blob(&g_kvdb, key_buf, fdb_blob_make(&blob, cards, blob_len));
                    for (int c = 0; c < own_total; c++) {
                        // 过滤条件：新卡片(0) 或 到期卡片(1 且 due_timestamp <= now)
                        if (cards[c].status == 0 || (cards[c].status == 1 && cards[c].due_timestamp <= now)) {
                            if (model->queue_count < ANKI_INITIAL_QUEUE_CAPACITY) {
                                model->today_queue[model->queue_count++] = cards[c];
                            } else break;
                        }
                    }
                    free(cards);
                }
            }
            if (model->queue_count >= ANKI_INITIAL_QUEUE_CAPACITY) break;
        }
    }
    
    ESP_LOGI(TAG, "Queue rebuilt. Loaded %d cards for this batch.", model->queue_count);
}

// 在 anki_model_load_next_card 的上方，实现这个函数：
void anki_model_load_card_payload(AnkiModel *model, uint16_t card_id) {
    struct fdb_blob blob;
    char key_buf[32];
    
    // 组装我们在 app_factory_reset 时约定的 key (例如 "anki_payload_1")
    snprintf(key_buf, sizeof(key_buf), "anki_payload_%d", card_id);
    
    // 直接将文本数据懒加载覆盖到 model 现有的 current_card_payload 中
    size_t read_len = fdb_kv_get_blob(&g_kvdb, key_buf, fdb_blob_make(&blob, &model->current_card_payload, sizeof(CardPayload)));
    if (read_len != sizeof(CardPayload)) {
        ESP_LOGW(TAG, "Failed to load payload for card %d, len: %d", card_id, read_len);
        // 如果读取失败，放入防错占位符，防止渲染乱码
        memset(&model->current_card_payload, 0, sizeof(CardPayload));
        strncpy(model->current_card_payload.front, "Load Error", 10);
        strncpy(model->current_card_payload.back, "Payload not found in FlashDB.", 29);
    }
}

// 修改 model.c 中的原有函数
bool anki_model_load_next_card(AnkiModel *model) {
    // 1. 如果当前队列已经背完（或者刚点击牌组进入学习，队列还是空的）
    if (model->active_card_idx >= model->queue_count) {
        
        // 触发懒加载补水，去 FlashDB 捞下一批卡片
        anki_model_rebuild_today_queue(model);
        
        // 如果捞取后依然是 0，说明当前牌组及其所有子牌组，今天真的没有卡片需要背了
        if (model->queue_count == 0) {
            ESP_LOGI(TAG, "All cards in this deck tree have been reviewed!");
            return false; 
        }
    }

    // 2. 从滑动窗口队列中取出当前要背的卡片索引
    CardIndex *idx = &model->today_queue[model->active_card_idx];
    
    // 3. 将真实的长文本从 SD 卡 / Flash 懒加载到 RAM 的 current_card_payload 中
    anki_model_load_card_payload(model, idx->card_id);

    // 4. 状态重置
    model->is_showing_answer = false;
    
    // 【重点注意】：这里绝对不要写 model->active_card_idx++
    // 因为 active_card_idx 必须停留在这张卡片上，等待用户按下 Hard/Good/Easy 进行评分。
    // Controller 收到评分计算完下一次到期时间后，由 Controller 负责推进 active_card_idx++ 并再次调用本函数！
    
    return true;
}

// 1. 新增 Deck
bool anki_model_add_deck(AnkiModel *model, const char *name, int parent_idx) {
    if (!model || !model->decks || !name || name[0] == '\0' ||
        model->deck_count >= ANKI_MAX_DECK_CAPACITY) {
        ESP_LOGE(TAG, "Max deck capacity reached!");
        return false;
    }

    // 解析 parent_id
    int parent_id = -1;
    if (parent_idx >= 0 && parent_idx < model->deck_count) {
        parent_id = model->decks[parent_idx].deck_id;
    }

    // 初始化新牌组并挂载到数组末尾
    AnkiDeck *new_deck = &model->decks[model->deck_count];
    memset(new_deck, 0, sizeof(AnkiDeck));
    new_deck->deck_id = model->_id_generator++;
    new_deck->parent_id = parent_id;
    strncpy(new_deck->name, name, sizeof(new_deck->name) - 1);
    new_deck->name[sizeof(new_deck->name) - 1] = '\0'; // 确保安全截断
    
    model->deck_count++;
    return true;
}

// 2. 更新 Deck
bool anki_model_update_deck(AnkiModel *model, int deck_idx, const char *name) {
    if (!model || !name || deck_idx < 0 || deck_idx >= model->deck_count) return false;
    
    strncpy(model->decks[deck_idx].name, name, sizeof(model->decks[deck_idx].name) - 1);
    model->decks[deck_idx].name[sizeof(model->decks[deck_idx].name) - 1] = '\0';
    
    return true;
}

// 3. 删除 Deck (级联清理)
bool anki_model_delete_deck(AnkiModel *model, int deck_idx) {
    if (!model || deck_idx < 0 || deck_idx >= model->deck_count) return false;

    bool remove[ANKI_MAX_DECK_CAPACITY] = { false };
    remove[deck_idx] = true;
    bool changed;
    do {
        changed = false;
        for (int i = 0; i < model->deck_count; ++i) {
            if (remove[i]) continue;
            for (int p = 0; p < model->deck_count; ++p) {
                if (remove[p] && model->decks[i].parent_id == model->decks[p].deck_id) {
                    remove[i] = true; changed = true; break;
                }
            }
        }
    } while (changed);
    int write = 0;
    for (int i = 0; i < model->deck_count; ++i)
        if (!remove[i]) model->decks[write++] = model->decks[i];
    model->deck_count = (uint8_t)write;

    ESP_LOGI(TAG, "after Deleted deck cnt: %d", model->deck_count);

    return true;
}

// ==========================================
// 辅助函数：计算牌组在树状结构中的绝对深度
// 根节点 depth = 0，子节点 depth = 1，孙子节点 depth = 2...
// ==========================================
int anki_model_get_deck_depth(AnkiModel *model, int parent_id) {
    int depth = 0;
    int current_id = parent_id;
    
    // 向上追溯直到根节点 (parent_id == -1)
    while (current_id != -1 && depth < ANKI_MAX_DECK_DEPTH) { 
        depth++;
        bool found = false;
        // 在数组中查找父节点
        for (int i = 0; i < model->deck_count; i++) {
            if (model->decks[i].deck_id == current_id) {
                current_id = model->decks[i].parent_id;
                found = true;
                break;
            }
        }
        if (!found) break; // 如果找不到父节点（孤儿节点），安全退出
    }
    return depth;
}

int anki_model_get_tree_total_cards(AnkiModel *model, int deck_idx) {
    if (deck_idx < 0 || deck_idx >= model->deck_count) return 0;
    return model->decks[deck_idx].total_cards_count;
}

int anki_model_get_tree_unlearned_cards(AnkiModel *model, int deck_idx) {
    if (deck_idx < 0 || deck_idx >= model->deck_count) return 0;
    return model->decks[deck_idx].new_cards_count + model->decks[deck_idx].due_cards_count;
}

bool anki_model_update_card(AnkiModel *model, CardIndex *card) {
    char key_buf[32];
    snprintf(key_buf, sizeof(key_buf), "anki_cards_%u", (unsigned int)card->deck_id);

    struct fdb_blob blob;
    fdb_kv_get_blob(&g_kvdb, key_buf, fdb_blob_make(&blob, NULL, 0));
    size_t blob_len = blob.saved.len;
            if (blob_len > 0 && blob_len % sizeof(CardIndex) == 0 && blob_len <= sizeof(CardIndex) * ANKI_MAX_CARDS_PER_DECK) {
        int own_total = blob_len / sizeof(CardIndex);
        CardIndex *cards = malloc(blob_len);
        if (cards) {
            fdb_kv_get_blob(&g_kvdb, key_buf, fdb_blob_make(&blob, cards, blob_len));
            bool found = false;
            for (int c = 0; c < own_total; c++) {
                if (cards[c].card_id == card->card_id) {
                    cards[c] = *card; // override
                    found = true;
                    break;
                }
            }
            if (found) {
                fdb_kv_set_blob(&g_kvdb, key_buf, fdb_blob_make(&blob, cards, blob_len));
            }
            free(cards);
            return found;
        }
    }
    return false;
}
