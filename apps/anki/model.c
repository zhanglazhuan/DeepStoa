#include <string.h>
#include <stdio.h>
#include <time.h>
#include <limits.h>
#include "esp_system.h"
#include "esp_log.h"

#include "model.h"
#include "app.h"
#include "source_sync.h"
#include "storage.h"
#include "trash.h"
#include "time_service.h"

static const char *TAG = "anki_model";

static uint32_t current_time(void)
{
    uint32_t now = 0;
    return time_service_now(&now) ? now : 0;
}

static uint32_t current_day_key(void)
{
    uint32_t epoch = current_time();
    if (!epoch) return 0;
    time_t now = (time_t)epoch;
    struct tm local;
    if (!localtime_r(&now, &local)) return 0;
    return (uint32_t)(local.tm_year + 1900) * 1000U +
           (uint32_t)local.tm_yday + 1U;
}

static bool study_duration_reached(const AnkiModel *model,
                                   const AnkiSettings *settings)
{
    if (!model || !settings || model->study_session_started_ms == 0 ||
        settings->study_duration_mins <= 0) return false;
    uint64_t now_ms = time_service_monotonic_ms();
    uint64_t limit_ms = (uint64_t)settings->study_duration_mins * 60000U;
    return now_ms >= model->study_session_started_ms &&
           now_ms - model->study_session_started_ms >= limit_ms;
}


bool anki_model_init(AnkiApp *app) {
    if (!app) return false;
    app->model = malloc(sizeof(AnkiModel));
    if (!app->model) return false;
    memset(app->model, 0, sizeof(AnkiModel));

    // 一次性全部分配 99 个牌组的内存，约 14KB
    app->model->decks = malloc(sizeof(AnkiDeck) * ANKI_MAX_DECK_CAPACITY);
    if (!app->model->decks) { free(app->model); app->model = NULL; return false; }
    memset(app->model->decks, 0, sizeof(AnkiDeck) * ANKI_MAX_DECK_CAPACITY);
    
    // 复习队列也直接分配最大初始容量
    app->model->today_queue = malloc(sizeof(CardIndex) * ANKI_INITIAL_QUEUE_CAPACITY);
    if (!app->model->today_queue) { free(app->model->decks); free(app->model); app->model = NULL; return false; }
    memset(app->model->today_queue, 0, sizeof(CardIndex) * ANKI_INITIAL_QUEUE_CAPACITY);

    anki_storage_load_all(app->model);
    if (!app->model->storage_fault) {
        anki_trash_reconcile_external_restores(app->model);
        if (!app->model->storage_fault) {
            anki_trash_cleanup_expired(app->model);
            anki_source_sync_all(app->model);
        }
    }
    return true;
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

static int queue_priority(const CardIndex *card)
{
    uint8_t status = card->status & ANKI_CARD_STATUS_MASK;
    if (status == ANKI_CARD_STATUS_LEARNING ||
        status == ANKI_CARD_STATUS_RELEARNING) return 0;
    if (status == ANKI_CARD_STATUS_REVIEW) return 1;
    return 2;
}

static int compare_queue_cards(const CardIndex *left, const CardIndex *right)
{
    int lp = queue_priority(left);
    int rp = queue_priority(right);
    if (lp != rp) return lp - rp;
    if (lp < 2 && left->due_timestamp != right->due_timestamp) {
        return left->due_timestamp < right->due_timestamp ? -1 : 1;
    }
    uint32_t lh = left->card_id * UINT32_C(2654435761);
    uint32_t rh = right->card_id * UINT32_C(2654435761);
    return lh < rh ? -1 : lh > rh ? 1 : 0;
}

static bool insert_queue_card(AnkiModel *model, const CardIndex *card,
                              uint16_t capacity)
{
    if (capacity == 0) return false;
    uint16_t position = 0;
    while (position < model->queue_count &&
           compare_queue_cards(&model->today_queue[position], card) <= 0) {
        position++;
    }
    if (model->queue_count >= capacity && position >= capacity) return false;

    uint16_t count = model->queue_count < capacity
        ? model->queue_count + 1 : model->queue_count;
    if (position < count) {
        uint16_t move = count - position - 1;
        if (move > 0) {
            memmove(&model->today_queue[position + 1],
                    &model->today_queue[position], move * sizeof(CardIndex));
        }
        model->today_queue[position] = *card;
    }
    model->queue_count = count;
    return true;
}

static AnkiSettings *settings_for_deck_index(AnkiModel *model, int deck_idx)
{
    if (deck_idx >= 0 && deck_idx < model->deck_count &&
        model->decks[deck_idx].has_custom_settings) {
        return &model->decks[deck_idx].custom_settings;
    }
    return &model->global_settings;
}

static void collect_queue_pass(AnkiModel *model, uint32_t root_id,
                               uint32_t now, int kind,
                               uint16_t capacity, bool allow_ahead)
{
    uint32_t deadline = allow_ahead && now <= UINT32_MAX - 86400U
        ? now + 86400U : now;
    for (int i = 0; i < model->deck_count; i++) {
        if (!is_deck_in_tree(model, model->decks[i].deck_id, root_id)) continue;
        char key[32];
        snprintf(key, sizeof(key), "anki_cards_%u",
                 (unsigned)model->decks[i].deck_id);
        struct fdb_blob blob;
        fdb_kv_get_blob(&g_kvdb, key, fdb_blob_make(&blob, NULL, 0));
        size_t len = blob.saved.len;
        if (len == 0 || len % sizeof(CardIndex) != 0 ||
            len > sizeof(CardIndex) * ANKI_MAX_CARDS_PER_DECK) continue;

        CardIndex *cards = malloc(len);
        if (!cards) continue;
        if (fdb_kv_get_blob(&g_kvdb, key,
                            fdb_blob_make(&blob, cards, len)) != len) {
            free(cards);
            continue;
        }
        size_t count = len / sizeof(*cards);
        for (size_t c = 0; c < count; c++) {
            if (cards[c].status & ANKI_CARD_FLAG_MISSING) continue;
            uint8_t status = cards[c].status & ANKI_CARD_STATUS_MASK;
            bool eligible = kind == 2
                ? status == ANKI_CARD_STATUS_NEW
                : kind == 1
                    ? status == ANKI_CARD_STATUS_REVIEW &&
                      cards[c].due_timestamp <= deadline
                    : (status == ANKI_CARD_STATUS_LEARNING ||
                       status == ANKI_CARD_STATUS_RELEARNING) &&
                      cards[c].due_timestamp <= now;
            if (eligible) insert_queue_card(model, &cards[c], capacity);
        }
        free(cards);
    }
}

static void anki_model_rebuild_today_queue(AnkiModel *model) {
    model->queue_count = 0;
    model->active_card_idx = 0; // 游标归零
    model->study_stop_reason = ANKI_STUDY_STOP_COMPLETE;
    
    if (model->active_deck_idx < 0 || model->active_deck_idx >= model->deck_count) return;
    
    uint32_t now = current_time();
    if (now == 0) {
        ESP_LOGW(TAG, "RTC is not ready; review queue is unavailable");
        model->study_stop_reason = ANKI_STUDY_STOP_CLOCK;
        return;
    }
    AnkiSettings *settings = settings_for_deck_index(model,
                                                     model->active_deck_idx);
    if (study_duration_reached(model, settings)) {
        ESP_LOGI(TAG, "Study duration reached");
        model->study_stop_reason = ANKI_STUDY_STOP_DURATION;
        return;
    }

    uint16_t new_done = 0;
    uint16_t review_done = 0;
    anki_model_get_daily_counts(model, model->active_deck_idx,
                                &new_done, &review_done);
    uint16_t review_remaining = settings->max_cards_daily > review_done
        ? (uint16_t)(settings->max_cards_daily - review_done) : 0;
    uint16_t new_remaining = settings->new_cards_daily > new_done
        ? (uint16_t)(settings->new_cards_daily - new_done) : 0;
    uint32_t root_id = model->decks[model->active_deck_idx].deck_id;

    collect_queue_pass(model, root_id, now, 0, ANKI_INITIAL_QUEUE_CAPACITY,
                       false);
    uint16_t review_room = ANKI_INITIAL_QUEUE_CAPACITY - model->queue_count;
    uint16_t add_reviews = review_remaining < review_room
        ? review_remaining : review_room;
    collect_queue_pass(model, root_id, now, 1,
                       model->queue_count + add_reviews,
                       settings->allow_study_ahead);

    uint16_t room = ANKI_INITIAL_QUEUE_CAPACITY - model->queue_count;
    uint16_t add_new = new_remaining < room ? new_remaining : room;
    collect_queue_pass(model, root_id, now, 2,
                       model->queue_count + add_new, false);
    if (model->queue_count > 0) {
        model->study_stop_reason = ANKI_STUDY_STOP_COMPLETE;
    }
    
}

void anki_model_begin_study(AnkiModel *model)
{
    if (model) {
        uint64_t started = time_service_monotonic_ms();
        model->study_session_started_ms = started ? started : 1;
        model->rating_in_progress = false;
    }
}

void anki_model_get_daily_counts(AnkiModel *model, int root_deck_idx,
                                 uint16_t *new_cards,
                                 uint16_t *review_cards)
{
    if (new_cards) *new_cards = 0;
    if (review_cards) *review_cards = 0;
    if (!model || root_deck_idx < 0 || root_deck_idx >= model->deck_count) return;
    uint32_t new_total = 0;
    uint32_t review_total = 0;
    uint32_t day = current_day_key();
    uint32_t root_id = model->decks[root_deck_idx].deck_id;
    for (uint8_t i = 0; i < model->daily_progress_count; i++) {
        AnkiDailyProgress *progress = &model->daily_progress[i];
        if (progress->day_key != day ||
            !is_deck_in_tree(model, progress->deck_id, root_id)) continue;
        new_total += progress->new_cards;
        review_total += progress->review_cards;
    }
    if (new_cards) *new_cards = new_total > UINT16_MAX
        ? UINT16_MAX : (uint16_t)new_total;
    if (review_cards) *review_cards = review_total > UINT16_MAX
        ? UINT16_MAX : (uint16_t)review_total;
}

void anki_model_record_answer(AnkiModel *model, uint32_t deck_id,
                              bool was_new, bool was_review)
{
    if (!model || (!was_new && !was_review)) return;
    uint32_t day = current_day_key();
    if (day == 0) return;
    AnkiDailyProgress *target = NULL;
    for (uint8_t i = 0; i < model->daily_progress_count; i++) {
        if (model->daily_progress[i].deck_id == deck_id &&
            model->daily_progress[i].day_key == day) {
            target = &model->daily_progress[i];
            break;
        }
    }
    if (!target) {
        for (uint8_t i = 0; i < model->daily_progress_count; i++) {
            if (model->daily_progress[i].day_key != day) {
                target = &model->daily_progress[i];
                break;
            }
        }
    }
    if (!target && model->daily_progress_count < ANKI_MAX_DECK_CAPACITY) {
        target = &model->daily_progress[model->daily_progress_count++];
    }
    if (!target) return;
    if (target->deck_id != deck_id || target->day_key != day) {
        memset(target, 0, sizeof(*target));
        target->deck_id = deck_id;
        target->day_key = day;
    }
    if (was_new && target->new_cards < UINT16_MAX) target->new_cards++;
    if (was_review && target->review_cards < UINT16_MAX) target->review_cards++;
    anki_storage_sync_daily_progress(model);
}

// 在 anki_model_load_next_card 的上方，实现这个函数：
bool anki_model_load_card_payload(AnkiModel *model, const CardIndex *card) {
    if (!model || !card) return false;
    if (anki_source_load_payload(model, card, &model->current_card_payload)) {
        model->current_card_available = true;
        return true;
    }

    struct fdb_blob blob;
    char key_buf[32];
    
    // 组装我们在 app_factory_reset 时约定的 key (例如 "anki_payload_1")
    snprintf(key_buf, sizeof(key_buf), "anki_payload_%u", (unsigned)card->card_id);
    
    // 直接将文本数据懒加载覆盖到 model 现有的 current_card_payload 中
    size_t read_len = fdb_kv_get_blob(&g_kvdb, key_buf, fdb_blob_make(&blob, &model->current_card_payload, sizeof(CardPayload)));
    if (read_len != sizeof(CardPayload)) {
        ESP_LOGW(TAG, "Failed to load payload for card %u, len: %u",
                 (unsigned)card->card_id, (unsigned)read_len);
        // 如果读取失败，放入防错占位符，防止渲染乱码
        memset(&model->current_card_payload, 0, sizeof(CardPayload));
        strncpy(model->current_card_payload.front, "Load Error", 10);
        strncpy(model->current_card_payload.back,
                "Card source is unavailable. Reinsert the SD card and rescan.",
                sizeof(model->current_card_payload.back) - 1);
        model->current_card_available = false;
        return false;
    }
    model->current_card_available = true;
    return true;
}

// 修改 model.c 中的原有函数
bool anki_model_load_next_card(AnkiModel *model) {
    if (!model || model->active_deck_idx < 0 ||
        model->active_deck_idx >= model->deck_count) return false;
    if (model->active_card_idx < 0) model->active_card_idx = 0;
    uint32_t now = current_time();
    AnkiSettings *settings = settings_for_deck_index(
        model, model->active_deck_idx);
    if (now && study_duration_reached(model, settings)) {
        ESP_LOGI(TAG, "Study duration reached");
        model->study_stop_reason = ANKI_STUDY_STOP_DURATION;
        return false;
    }

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
    anki_model_load_card_payload(model, idx);

    // 4. 状态重置
    model->is_showing_answer = false;
    model->rating_in_progress = false;
    
    // 【重点注意】：这里绝对不要写 model->active_card_idx++
    // active_card_idx 停在当前卡片上，等待用户选择 Again/Hard/Good/Easy。
    // Controller 收到评分计算完下一次到期时间后，由 Controller 负责推进 active_card_idx++ 并再次调用本函数！
    
    return true;
}

// 1. 新增 Deck
bool anki_model_add_deck(AnkiModel *model, const char *name, int parent_idx) {
    if (!model || !model->decks || !name || name[0] == '\0' ||
        model->storage_fault ||
        model->deck_count >= ANKI_MAX_DECK_CAPACITY) {
        ESP_LOGE(TAG, "Max deck capacity reached!");
        return false;
    }
    if (model->_id_generator == 0 || model->_id_generator > INT32_MAX) {
        ESP_LOGE(TAG, "Deck ID space exhausted");
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
    if (!model || !card) return false;
    char key_buf[32];
    snprintf(key_buf, sizeof(key_buf), "anki_cards_%u", (unsigned int)card->deck_id);

    struct fdb_blob blob;
    fdb_kv_get_blob(&g_kvdb, key_buf, fdb_blob_make(&blob, NULL, 0));
    size_t blob_len = blob.saved.len;
            if (blob_len > 0 && blob_len % sizeof(CardIndex) == 0 && blob_len <= sizeof(CardIndex) * ANKI_MAX_CARDS_PER_DECK) {
        int own_total = blob_len / sizeof(CardIndex);
        CardIndex *cards = malloc(blob_len);
        if (cards) {
            if (fdb_kv_get_blob(&g_kvdb, key_buf,
                                fdb_blob_make(&blob, cards, blob_len)) != blob_len) {
                free(cards);
                return false;
            }
            bool found = false;
            for (int c = 0; c < own_total; c++) {
                if (cards[c].card_id == card->card_id) {
                    cards[c] = *card; // override
                    found = true;
                    break;
                }
            }
            bool saved = found && fdb_kv_set_blob(
                &g_kvdb, key_buf,
                fdb_blob_make(&blob, cards, blob_len)) == FDB_NO_ERR;
            if (found && !saved) {
                ESP_LOGE(TAG, "Failed to persist card %u",
                         (unsigned)card->card_id);
            }
            free(cards);
            return saved;
        }
    }
    return false;
}
