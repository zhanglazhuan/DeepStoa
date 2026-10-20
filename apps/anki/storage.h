#ifndef ANKI_STORAGE_H
#define ANKI_STORAGE_H

#include "model.h"
#include "scheduler.h"
#include "flash_control.h" 

typedef struct {
    uint32_t _id_generator;
    int active_deck_idx;
    int active_card_idx;
} anki_storage_meta_v1_t;

bool anki_app_factory_reset(void);

// 加载所有数据 (包含 Settings 和 Decks)
void anki_storage_load_all(AnkiModel *model);

// 仅同步全局设置到 FlashDB
bool anki_storage_sync_global_settings(AnkiModel *model);

// 仅同步元数据 (ID generator, active deck/card idx) 到 FlashDB
bool anki_storage_sync_meta(AnkiModel *model);

// 仅同步牌组列表（含牌组专属设置）到 FlashDB
bool anki_storage_sync_decks(AnkiModel *model);
bool anki_storage_sync_daily_progress(AnkiModel *model);

bool anki_storage_load_schedule(const CardIndex *card,
                                AnkiScheduleState *schedule);
bool anki_storage_save_schedule(const AnkiScheduleState *schedule);
bool anki_storage_delete_schedule(uint32_t card_id);
bool anki_storage_append_review(uint32_t card_id, uint32_t reviewed_at,
                                uint16_t elapsed_days,
                                uint16_t interval_days, uint8_t rating,
                                uint8_t state_before, uint8_t state_after);

#endif // ANKI_STORAGE_H
