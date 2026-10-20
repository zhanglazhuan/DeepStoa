#include "esp_log.h"
#include "controller_settings.h"
#include "../app.h"
#include "../model.h"
#include "../storage.h"

static const char *TAG = "anki_controller_settings";
static AnkiSettings *editable_settings(AnkiApp *app);
static void save_editable_settings(AnkiApp *app);
static int clamp_value(int value, int minimum, int maximum);

// 修改原有的 task_plan 回调
void anki_controller_settings_new_cards_changed(lv_event_t *e) {
    AnkiApp *app = lv_event_get_user_data(e);
    int val = (int)(intptr_t)lv_event_get_param(e);
    
    AnkiSettings *settings = editable_settings(app);
    if (!settings) return;
    settings->new_cards_daily = clamp_value(val, 0, 999);
    save_editable_settings(app);
    ESP_LOGI(TAG, "New cards/day updated to %d",
             settings->new_cards_daily);
}

// 新增 max_cards 回调
void anki_controller_settings_max_cards_changed(lv_event_t *e) {
    AnkiApp *app = lv_event_get_user_data(e);
    int val = (int)(intptr_t)lv_event_get_param(e);
    
    AnkiSettings *settings = editable_settings(app);
    if (!settings) return;
    settings->max_cards_daily = clamp_value(val, 0, 9999);
    save_editable_settings(app);
    ESP_LOGI(TAG, "Maximum reviews/day updated to %d",
             settings->max_cards_daily);
}

void anki_controller_settings_study_duration_changed(lv_event_t *e) {
    AnkiApp *app = lv_event_get_user_data(e);
    int val = (int)(intptr_t)lv_event_get_param(e);
    
    AnkiSettings *settings = editable_settings(app);
    if (!settings) return;
    settings->study_duration_mins = clamp_value(val, 0, 1440);
    save_editable_settings(app);
}

void anki_controller_settings_advance_study_changed(lv_event_t *e) {
    AnkiApp *app = lv_event_get_user_data(e);
    bool is_on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    
    AnkiSettings *settings = editable_settings(app);
    if (!settings) return;
    settings->allow_study_ahead = is_on;
    save_editable_settings(app);
}

static int clamp_value(int value, int minimum, int maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static AnkiSettings *editable_settings(AnkiApp *app)
{
    if (!app || !app->model) return NULL;
    int deck_idx = app->model->target_deck_idx;
    if (deck_idx < 0) return &app->model->global_settings;
    if (deck_idx >= app->model->deck_count) return NULL;
    if (!app->model->decks[deck_idx].has_custom_settings) {
        app->model->decks[deck_idx].custom_settings =
            app->model->global_settings;
    }
    app->model->decks[deck_idx].has_custom_settings = true;
    return &app->model->decks[deck_idx].custom_settings;
}

static void save_editable_settings(AnkiApp *app)
{
    if (app->model->target_deck_idx < 0) {
        anki_storage_sync_global_settings(app->model);
    } else {
        anki_storage_sync_decks(app->model);
    }
}

void anki_controller_settings_retention_changed(lv_event_t *e) {
    AnkiApp *app = lv_event_get_user_data(e);
    AnkiSettings *settings = editable_settings(app);
    if (!settings) return;
    settings->desired_retention_percent = clamp_value(
        (int)(intptr_t)lv_event_get_param(e), 70, 97);
    save_editable_settings(app);
}

void anki_controller_settings_learning_step_changed(lv_event_t *e) {
    AnkiApp *app = lv_event_get_user_data(e);
    AnkiSettings *settings = editable_settings(app);
    if (!settings) return;
    settings->learning_step_minutes = clamp_value(
        (int)(intptr_t)lv_event_get_param(e), 1, 720);
    save_editable_settings(app);
}

void anki_controller_settings_relearning_step_changed(lv_event_t *e) {
    AnkiApp *app = lv_event_get_user_data(e);
    AnkiSettings *settings = editable_settings(app);
    if (!settings) return;
    settings->relearning_step_minutes = clamp_value(
        (int)(intptr_t)lv_event_get_param(e), 1, 720);
    save_editable_settings(app);
}

AnkiSettings* anki_controller_settings_get_config(AnkiApp *app) {
    if (!app || !app->model) return NULL;
    int deck_idx = app->model->target_deck_idx;
    if (deck_idx < 0 || deck_idx >= app->model->deck_count) {
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

AnkiSettings* anki_controller_settings_get_for_deck(AnkiApp *app,
                                                     uint32_t deck_id)
{
    if (!app || !app->model) return NULL;
    for (int i = 0; i < app->model->deck_count; i++) {
        if (app->model->decks[i].deck_id == deck_id) {
            return app->model->decks[i].has_custom_settings
                ? &app->model->decks[i].custom_settings
                : &app->model->global_settings;
        }
    }
    return &app->model->global_settings;
}
