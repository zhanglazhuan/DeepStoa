#ifndef ANKI_STORAGE_H
#define ANKI_STORAGE_H

#include "model.h"
#include "flash_control.h" 

typedef struct {
    uint32_t _id_generator;
    int active_deck_idx;
    int active_card_idx;
} anki_storage_meta_t;

bool anki_app_factory_reset(void);

// 加载所有数据 (包含 Settings 和 Decks)
void anki_storage_load_all(AnkiModel *model);

// 仅同步全局设置到 FlashDB
void anki_storage_sync_global_settings(AnkiModel *model);

// 仅同步元数据 (ID generator, active deck/card idx) 到 FlashDB
void anki_storage_sync_meta(AnkiModel *model);

// 仅同步牌组列表（含牌组专属设置）到 FlashDB
void anki_storage_sync_decks(AnkiModel *model);

#endif // ANKI_STORAGE_H