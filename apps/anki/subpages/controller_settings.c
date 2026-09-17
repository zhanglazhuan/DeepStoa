#include "esp_log.h"
#include "controller_settings.h"
#include "../app.h"
#include "../model.h"
#include "../storage.h"

static const char *TAG = "anki_controller_settings";

// 修改原有的 task_plan 回调
void anki_controller_settings_new_cards_changed(lv_event_t *e) {
    AnkiApp *app = lv_event_get_user_data(e);
    int val = (int)(intptr_t)lv_event_get_param(e);
    
    if (app && app->model) {
        int deck_idx = app->model->target_deck_idx;
        if (deck_idx == -1) {
            app->model->global_settings.new_cards_daily = val;
            anki_storage_sync_global_settings(app->model);
            ESP_LOGI(TAG, "Global New cards updated to: %d", val);
        } else {
            app->model->decks[deck_idx].has_custom_settings = true;
            app->model->decks[deck_idx].custom_settings.new_cards_daily = val;
            anki_storage_sync_decks(app->model); 
            ESP_LOGI(TAG, "Deck [%d] New cards updated to: %d", deck_idx, val);
        }
    }
}

// 新增 max_cards 回调
void anki_controller_settings_max_cards_changed(lv_event_t *e) {
    AnkiApp *app = lv_event_get_user_data(e);
    int val = (int)(intptr_t)lv_event_get_param(e);
    
    if (app && app->model) {
        int deck_idx = app->model->target_deck_idx;
        if (deck_idx == -1) {
            app->model->global_settings.max_cards_daily = val;
            anki_storage_sync_global_settings(app->model);
            ESP_LOGI(TAG, "Global Max cards updated to: %d", val);
        } else {
            app->model->decks[deck_idx].has_custom_settings = true;
            app->model->decks[deck_idx].custom_settings.max_cards_daily = val;
            anki_storage_sync_decks(app->model);
            ESP_LOGI(TAG, "Deck [%d] Max cards updated to: %d", deck_idx, val);
        }
    }
}

void anki_controller_settings_study_duration_changed(lv_event_t *e) {
    AnkiApp *app = lv_event_get_user_data(e);
    int val = (int)(intptr_t)lv_event_get_param(e);
    
    if (app && app->model) {
        int deck_idx = app->model->target_deck_idx;
        if (deck_idx == -1) {
            app->model->global_settings.study_duration_mins = val;
            anki_storage_sync_global_settings(app->model);
        } else {
            app->model->decks[deck_idx].has_custom_settings = true;
            app->model->decks[deck_idx].custom_settings.study_duration_mins = val;
            anki_storage_sync_decks(app->model);
        }
    }
}

void anki_controller_settings_advance_study_changed(lv_event_t *e) {
    AnkiApp *app = lv_event_get_user_data(e);
    bool is_on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    
    if (app && app->model) {
        int deck_idx = app->model->target_deck_idx;
        if (deck_idx == -1) {
            app->model->global_settings.allow_study_ahead = is_on;
            anki_storage_sync_global_settings(app->model);
        } else {
            app->model->decks[deck_idx].has_custom_settings = true;
            app->model->decks[deck_idx].custom_settings.allow_study_ahead = is_on;
            anki_storage_sync_decks(app->model);
        }
    }
}

void anki_controller_settings_hard_interval_changed(lv_event_t *e) {
    AnkiApp *app = lv_event_get_user_data(e);
    int val = (int)(intptr_t)lv_event_get_param(e);
    
    if (app && app->model) {
        int deck_idx = app->model->target_deck_idx;
        if (deck_idx == -1) {
            app->model->global_settings.hard_interval = val;
            anki_storage_sync_global_settings(app->model);
        } else {
            app->model->decks[deck_idx].has_custom_settings = true;
            app->model->decks[deck_idx].custom_settings.hard_interval = val;
            anki_storage_sync_decks(app->model);
        }
    }
}

void anki_controller_settings_good_interval_changed(lv_event_t *e) {
    AnkiApp *app = lv_event_get_user_data(e);
    int val = (int)(intptr_t)lv_event_get_param(e);
    
    if (app && app->model) {
        int deck_idx = app->model->target_deck_idx;
        if (deck_idx == -1) {
            app->model->global_settings.good_interval = val;
            anki_storage_sync_global_settings(app->model);
        } else {
            app->model->decks[deck_idx].has_custom_settings = true;
            app->model->decks[deck_idx].custom_settings.good_interval = val;
            anki_storage_sync_decks(app->model);
        }
    }
}

void anki_controller_settings_easy_interval_changed(lv_event_t *e) {
    AnkiApp *app = lv_event_get_user_data(e);
    int val = (int)(intptr_t)lv_event_get_param(e);
    
    if (app && app->model) {
        int deck_idx = app->model->target_deck_idx;
        if (deck_idx == -1) {
            app->model->global_settings.easy_interval = val;
            anki_storage_sync_global_settings(app->model);
        } else {
            app->model->decks[deck_idx].has_custom_settings = true;
            app->model->decks[deck_idx].custom_settings.easy_interval = val;
            anki_storage_sync_decks(app->model);
        }
    }
}

AnkiSettings* anki_controller_settings_get_config(AnkiApp *app) {
    int deck_idx = app->model->target_deck_idx;
    if (deck_idx == -1) {
        return &app->model->global_settings; // 全局设置
    } else {
        // 如果是牌组独立设置，且该牌组已经自定义过，则返回自定义设置
        if (app->model->decks[deck_idx].has_custom_settings) {
            return &app->model->decks[deck_idx].custom_settings;
        } else {
            // 如果牌组没有自定义过，展示全局设置作为 Baseline
            return &app->model->global_settings;
        }
    }
}
