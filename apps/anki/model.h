#ifndef ANKI_MODEL_H
#define ANKI_MODEL_H

#include <stdint.h>
#include <stdbool.h>

#define ANKI_MAX_DECK_DEPTH 3
#define ANKI_MAX_DECK_CAPACITY 99
#define ANKI_MAX_CARDS_PER_DECK 999
#define ANKI_INITIAL_QUEUE_CAPACITY 50

typedef struct {
    uint16_t card_id;
    uint32_t deck_id;         // 新增：记录该卡片所属的牌组ID
    uint32_t file_offset;     // 这行卡片文本在 SD 卡 CSV 文件中的绝对字节偏移量
    uint16_t file_len;
    uint32_t due_timestamp;
    uint8_t interval;
    uint8_t  ease_factor;
    uint8_t status;     // 新卡片/学习中/毕业等
} CardIndex;

typedef struct {
    char front[512];
    char back[512];
} CardPayload;

// 1. 提取统一的设置结构体
typedef struct {
    int new_cards_daily;          // 每日新卡片额度
    int max_cards_daily;          // 最大每日计划卡片数
    int study_duration_mins;  // 学习时长
    bool allow_study_ahead;   // 允许提前学习
    int hard_interval;        // Hard 间隔
    int good_interval;        // Good 间隔
    int easy_interval;        // Easy 间隔
} AnkiSettings;

typedef struct {
    uint32_t deck_id;
    uint32_t parent_id; // 嵌套支持：-1表示根目录, 最大128
    char name[64];
    char sd_file_path[64];
    uint16_t new_cards_count;  // 待学习的卡片数
    uint16_t due_cards_count;  // 待复习的卡片数
    uint16_t total_cards_count;  // 总卡片数

    // --- 新增设置相关 ---
    bool has_custom_settings;      // 是否开启了专属设置
    AnkiSettings custom_settings;  // 该牌组的专属设置参数
} AnkiDeck;

typedef struct AnkiModel {
    AnkiDeck * decks;
    uint8_t deck_count;

    CardIndex *today_queue; 
    uint16_t queue_count;

    CardPayload current_card_payload;
    
    // 当前在学习的牌组索引
    int active_deck_idx;
    int active_card_idx;
    bool is_showing_answer;

    // 当前在操作的牌组索引，用于添加/删除/编辑牌组
    int target_deck_idx;

    // --- 新增：全局设置 ---
    AnkiSettings global_settings;
    
    // 统计项 (过去 30 天的简单学习量)
    uint16_t daily_stats[30]; 

    uint32_t _id_generator;
    uint32_t _cards_id_generator;
} AnkiModel;

struct AnkiApp;

void anki_model_init(struct AnkiApp *app);
void anki_model_deinit(struct AnkiApp *app);

void anki_model_load_decks_catalog(AnkiModel *model);
void anki_model_load_cards(AnkiModel *model, uint8_t deck_id);

bool anki_model_load_next_card(AnkiModel *model);

// 增加牌组 (parent_idx 为 -1 时表示添加根牌组，否则表示添加子牌组)
bool anki_model_add_deck(AnkiModel *model, const char *name, int parent_idx);

// 更新牌组名称
bool anki_model_update_deck(AnkiModel *model, int deck_idx, const char *name);

// 删除牌组 (会级联删除直接子牌组)
bool anki_model_delete_deck(AnkiModel *model, int deck_idx);
int anki_model_get_deck_depth(AnkiModel *model, int parent_id);

// 获取整个牌组树（包含自己及所有层级子牌组）的卡片聚合统计
int anki_model_get_tree_total_cards(AnkiModel *model, int deck_idx);
int anki_model_get_tree_unlearned_cards(AnkiModel *model, int deck_idx);

// 更新某个具体的卡片状态到 FlashDB 中持久化
bool anki_model_update_card(AnkiModel *model, CardIndex *card);

#endif // ANKI_MODEL_H